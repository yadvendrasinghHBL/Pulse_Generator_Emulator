/*
 * Pulse output on J8.1 (PA5, TIM2_CH1). Why TIM2 in toggle mode instead of PWM:
 * - toggle mode gives exactly 50 % duty cycle for every frequency, like the
 *   code disc of an optical pulse generator;
 * - TIM2 is 32-bit, so with no prescaler it covers 0.01 Hz .. 50 kHz at the full
 *   80 MHz resolution (half period = 800 .. 4 000 000 000 ticks);
 * - the same scheme extends to several phase-shifted channels on one timer
 *   (stage 3: TIM3 CH1..CH4 on J8.2/.3/.6/.7).
 *
 * The output toggles each time the counter wraps to 0 (CCR1 = 0), so one
 * timer period = one half period of the output. The timer is programmed
 * through its registers: the HAL TIM state machine adds nothing here.
 */
#include "pulse.h"
#include "board.h"

/* OC1M values (RM0394 TIMx_CCMR1): 0b011 toggle, 0b100 force inactive (low) */
#define OC1M_TOGGLE          (TIM_CCMR1_OC1M_0 | TIM_CCMR1_OC1M_1)
#define OC1M_FORCE_LOW       (TIM_CCMR1_OC1M_2)

static uint32_t req_mhz = PULSE_DEFAULT_MHZ;
static uint32_t half_ticks;               /* timer ticks per half period */
static pulse_state_t state = PULSE_OFF;
static volatile uint64_t halves;          /* half periods since the last start */

/* TIM2 kernel clock: PCLK1, doubled when the APB1 prescaler is not 1 */
static uint32_t tim_clk_hz(void)
{
  uint32_t pclk = HAL_RCC_GetPCLK1Freq();

  return (RCC->CFGR & RCC_CFGR_PPRE1_2) ? 2U * pclk : pclk;
}

static uint32_t ticks_for(uint32_t mhz)
{
  /* half period = clk / (2 f); f in mHz -> clk * 1000 / (2 * mHz), rounded */
  uint64_t num = (uint64_t)tim_clk_hz() * 1000U;
  uint64_t den = 2U * (uint64_t)mhz;

  return (uint32_t)((num + den / 2U) / den);
}

static void set_oc_mode(uint32_t oc1m)
{
  TIM2->CCMR1 = (TIM2->CCMR1 & ~TIM_CCMR1_OC1M) | oc1m;
}

static void pin_to_timer(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = PULSE1_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
  gpio.Alternate = PULSE1_AF;
  HAL_GPIO_Init(PULSE1_PORT, &gpio);
}

void pulse_init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM2_CLK_ENABLE();
  __HAL_RCC_TIM2_FORCE_RESET();
  __HAL_RCC_TIM2_RELEASE_RESET();

  TIM2->CR1 = 0;                  /* up-counting, ARR not buffered (see pulse_set_mhz) */
  TIM2->PSC = 0;
  TIM2->CCR1 = 0;                 /* toggle when the counter wraps to 0 */
  set_oc_mode(OC1M_FORCE_LOW);    /* OC1REF low until started */
  TIM2->CCER = TIM_CCER_CC1E;     /* active high; the pin stays analog until pulse_start() */
  half_ticks = ticks_for(req_mhz);
  TIM2->ARR = half_ticks - 1U;

  HAL_NVIC_SetPriority(TIM2_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(TIM2_IRQn);
  /* PA5 is left in its reset (analog) state: nothing is driven on J8 at boot */
}

int pulse_set_mhz(uint32_t mhz)
{
  if (mhz < PULSE_MIN_MHZ || mhz > PULSE_MAX_MHZ) {
    return -1;
  }
  uint32_t ticks = ticks_for(mhz);
  uint32_t arr = ticks - 1U;

  req_mhz = mhz;
  half_ticks = ticks;
  /*
   * ARR is written directly (not buffered) so a new speed applies at once,
   * even after a 50 s half period. If the counter is already past the new
   * end value, end the current half period now: it has already lasted longer
   * than the new half period, so no short pulse is produced. "Greater than"
   * (not ">=") so a counter that is about to wrap by itself is left alone.
   */
  __disable_irq();
  TIM2->ARR = arr;
  if (state == PULSE_RUNNING && TIM2->CNT > arr) {
    TIM2->CNT = arr;
  }
  __enable_irq();
  return 0;
}

uint32_t pulse_get_mhz(void)
{
  return req_mhz;
}

uint32_t pulse_half_period_ticks(void)
{
  return half_ticks;
}

uint32_t pulse_actual_mhz(void)
{
  uint64_t num = (uint64_t)tim_clk_hz() * 1000U;
  uint64_t den = 2U * (uint64_t)half_ticks;

  return (uint32_t)((num + den / 2U) / den);
}

void pulse_start(void)
{
  TIM2->CR1 &= ~TIM_CR1_CEN;
  TIM2->DIER = 0;
  set_oc_mode(OC1M_FORCE_LOW);    /* every start begins with a low half period */
  set_oc_mode(OC1M_TOGGLE);
  TIM2->CNT = 0;
  TIM2->EGR = TIM_EGR_UG;         /* load PSC */
  TIM2->SR = 0;
  __disable_irq();
  halves = 0;
  __enable_irq();
  if (state == PULSE_OFF) {
    pin_to_timer();
  }
  state = PULSE_RUNNING;
  TIM2->DIER = TIM_DIER_UIE;
  TIM2->CR1 |= TIM_CR1_CEN;
}

/* Continue after a stop with the level held: no forced low, count kept. The
 * held level stays for one full new half period, like a wheel that starts
 * turning again from where its code disc stopped. */
void pulse_resume(void)
{
  if (state != PULSE_STOPPED) {
    pulse_start();
    return;
  }
  int lvl = (PULSE1_PORT->IDR & PULSE1_PIN) != 0;

  TIM2->DIER = 0;
  set_oc_mode(OC1M_TOGGLE);       /* OC1REF keeps the held level */
  TIM2->CNT = 0;
  TIM2->SR = 0;
  __disable_irq();
  if ((int)(halves & 1U) != lvl) {
    halves++;                     /* count parity = level (after `stop low`) */
  }
  __enable_irq();
  state = PULSE_RUNNING;
  TIM2->DIER = TIM_DIER_UIE;
  TIM2->CR1 |= TIM_CR1_CEN;
}

void pulse_stop(int force_low)
{
  TIM2->CR1 &= ~TIM_CR1_CEN;
  TIM2->DIER = 0;
  if (force_low) {
    set_oc_mode(OC1M_FORCE_LOW);
    if (state == PULSE_OFF) {
      pin_to_timer();
    }
    state = PULSE_STOPPED;
  } else if (state == PULSE_RUNNING) {
    state = PULSE_STOPPED;        /* OC1REF keeps its level: the pin holds it */
  }
}

pulse_state_t pulse_state(void)
{
  return state;
}

int pulse_level(void)
{
  if (state == PULSE_OFF) {
    return -1;
  }
  return HAL_GPIO_ReadPin(PULSE1_PORT, PULSE1_PIN) == GPIO_PIN_SET;
}

uint64_t pulse_count(void)
{
  uint64_t h;

  __disable_irq();
  h = halves;
  __enable_irq();
  return h / 2U;
}

/* One update event per half period (the counter wrap that toggles the pin) */
void pulse_irq_handler(void)
{
  if (TIM2->SR & TIM_SR_UIF) {
    TIM2->SR = ~TIM_SR_UIF;
    halves++;
  }
}
