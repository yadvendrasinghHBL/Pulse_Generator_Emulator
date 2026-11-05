/*
 * Sensor output A/B: J8.6 (PB0, TIM1_CH2N, AF1) and J8.7 (PB1, TIM1_CH3N, AF1).
 *
 * Why TIM1 and these channels: they are the only J8 pins that share one timer
 * other than TIM3, and TIM3 (PA6/PA7) is kept for the receiver inputs. One
 * timer for both outputs makes the 90 degrees exact by construction.
 *
 * How: both channels run in output-compare toggle mode. One counter period
 * (N counts) is one half period of the output. The leading channel toggles at
 * N/4, the lagging one at 3N/4, so they are N/2 counts = a quarter period =
 * 90 degrees apart and each has exactly 50 % duty. N is a multiple of 4.
 * TIM1 is 16-bit, so the prescaler is chosen per frequency (0.01 Hz needs
 * about 61000) and N is kept as large as possible for resolution.
 *
 * Only the complementary outputs (CHxN) are wired to J8. With CCxE = 0 and
 * CCxNE = 1, OCxN follows OCxREF (not inverted) once BDTR.MOE is set.
 * Registers are used directly: the HAL TIM state machine adds nothing here.
 */
#include "quad.h"
#include "board.h"

#define OC_TOGGLE   3U      /* OCxM = 0b0011 */
#define OC_LOW      4U      /* OCxM = 0b0100, force inactive */

static uint32_t psc1 = 1;           /* prescaler divider (PSC + 1) */
static uint32_t n_cnt = 4;          /* counts per half period, multiple of 4 */
static quad_dir_t dir = QUAD_FWD;
static pulse_state_t state = PULSE_OFF;
static volatile uint64_t toggles_a;

/* TIM1 kernel clock: PCLK2, doubled when the APB2 prescaler is not 1 */
static uint32_t tim_clk_hz(void)
{
  uint32_t pclk = HAL_RCC_GetPCLK2Freq();

  return (RCC->CFGR & RCC_CFGR_PPRE2_2) ? 2U * pclk : pclk;
}

/* Prescaler and counter period for f (mHz): smallest prescaler that fits 16 bits */
static void geometry(uint32_t mhz, uint32_t *p1, uint32_t *n)
{
  uint64_t num = (uint64_t)tim_clk_hz() * 1000U;   /* half period = num / den ticks */
  uint64_t den = 2U * (uint64_t)mhz;
  uint64_t d = den * 65536U;
  uint64_t p = (num + d - 1U) / d;
  uint64_t c = (num + den * p / 2U) / (den * p);

  c = (c + 2U) / 4U * 4U;
  if (c > 65536U) {
    c = 65536U;
  }
  if (c < 4U) {
    c = 4U;
  }
  *p1 = (uint32_t)p;
  *n = (uint32_t)c;
}

static void set_oc_modes(uint32_t mode)
{
  TIM1->CCMR1 = (TIM1->CCMR1 & ~TIM_CCMR1_OC2M) | (mode << TIM_CCMR1_OC2M_Pos);
  TIM1->CCMR2 = (TIM1->CCMR2 & ~TIM_CCMR2_OC3M) | (mode << TIM_CCMR2_OC3M_Pos);
}

/* A = CH2, B = CH3: the leading channel toggles at N/4, the lagging one at 3N/4 */
static void set_compare(uint32_t n)
{
  uint32_t lead = n / 4U;
  uint32_t lag = 3U * n / 4U;

  TIM1->CCR2 = dir == QUAD_FWD ? lead : lag;
  TIM1->CCR3 = dir == QUAD_FWD ? lag : lead;
}

/* Load prescaler and period; UG transfers PSC (URS = 1: no update interrupt) */
static void load_timebase(uint32_t p1, uint32_t n)
{
  TIM1->PSC = p1 - 1U;
  TIM1->ARR = n - 1U;
  set_compare(n);
  TIM1->EGR = TIM_EGR_UG;
}

static void pins_to_timer(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = QUAD_A_PIN | QUAD_B_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
  gpio.Alternate = QUAD_AF;
  HAL_GPIO_Init(QUAD_PORT, &gpio);
}

void quad_init(void)
{
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_TIM1_CLK_ENABLE();
  __HAL_RCC_TIM1_FORCE_RESET();
  __HAL_RCC_TIM1_RELEASE_RESET();

  TIM1->CR1 = TIM_CR1_URS;        /* up-counting; ARR/CCR not buffered (see quad_set_mhz) */
  set_oc_modes(OC_LOW);
  TIM1->CCER = TIM_CCER_CC2NE | TIM_CCER_CC3NE;   /* only the N outputs are on J8 */
  TIM1->BDTR = TIM_BDTR_MOE;
  geometry(PULSE_DEFAULT_MHZ, &psc1, &n_cnt);
  load_timebase(psc1, n_cnt);
  TIM1->SR = 0;

  HAL_NVIC_SetPriority(TIM1_CC_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(TIM1_CC_IRQn);
  /* PB0/PB1 stay in their reset (analog) state: nothing is driven on J8 at boot */
}

int quad_set_mhz(uint32_t mhz)
{
  uint32_t p1;
  uint32_t n;

  if (mhz < PULSE_MIN_MHZ || mhz > PULSE_MAX_MHZ) {
    return -1;
  }
  geometry(mhz, &p1, &n);
  if (state != PULSE_RUNNING) {
    psc1 = p1;
    n_cnt = n;
    load_timebase(p1, n);
    return 0;
  }

  /*
   * Running: switch to the new timing at once, but continue the A/B sequence.
   * Edges alternate (lead at N/4, lag at 3N/4), a quarter period apart. Find
   * how long ago the last edge was; the next edge comes a new quarter period
   * after it, or now if that time has already passed. Then each level lasts
   * between the old and the new half period: never a short pulse, never a
   * missing or extra edge, so the 90 degrees and the direction are kept.
   */
  __disable_irq();
  TIM1->CR1 &= ~TIM_CR1_CEN;      /* outputs hold their levels */
  uint32_t c = TIM1->CNT;
  uint32_t lead = n_cnt / 4U;
  uint32_t lag = 3U * n_cnt / 4U;
  uint32_t since;
  int next_is_lead;

  if (c < lead) {
    since = c + n_cnt - lag;      /* last edge: lag of the previous period */
    next_is_lead = 1;
  } else if (c < lag) {
    since = c - lead;
    next_is_lead = 0;
  } else {
    since = c - lag;
    next_is_lead = 1;
  }

  /* convert to new counts; remaining counts to the next edge: 1 .. N/2 - 1 */
  uint64_t since_new = (uint64_t)since * psc1 / p1;
  uint32_t quarter = n / 2U;
  uint32_t remaining = since_new < quarter ? quarter - (uint32_t)since_new : 1U;

  if (remaining >= quarter) {
    remaining = quarter - 1U;     /* never start exactly on the previous edge */
  }
  int32_t pos = (int32_t)(next_is_lead ? n / 4U : 3U * n / 4U) - (int32_t)remaining;

  if (pos < 0) {
    pos += (int32_t)n;
  }
  psc1 = p1;
  n_cnt = n;
  load_timebase(p1, n);           /* UG sets CNT = 0 ... */
  TIM1->CNT = (uint32_t)pos;      /* ... continue from the matching position */
  TIM1->CR1 |= TIM_CR1_CEN;
  __enable_irq();
  return 0;
}

uint32_t quad_actual_mhz(void)
{
  uint64_t num = (uint64_t)tim_clk_hz() * 1000U;
  uint64_t den = 2U * (uint64_t)n_cnt * psc1;

  return (uint32_t)((num + den / 2U) / den);
}

uint32_t quad_counter_period(void)
{
  return n_cnt;
}

uint32_t quad_prescaler(void)
{
  return psc1;
}

int quad_set_dir(quad_dir_t d)
{
  if (state == PULSE_RUNNING) {
    return -1;
  }
  dir = d;
  set_compare(n_cnt);
  return 0;
}

quad_dir_t quad_dir(void)
{
  return dir;
}

void quad_start(void)
{
  TIM1->CR1 &= ~TIM_CR1_CEN;
  TIM1->DIER = 0;
  set_oc_modes(OC_LOW);           /* both start low */
  set_oc_modes(OC_TOGGLE);
  load_timebase(psc1, n_cnt);     /* CNT = 0: first edge (leading channel) at N/4 */
  TIM1->SR = 0;
  __disable_irq();
  toggles_a = 0;
  __enable_irq();
  if (state == PULSE_OFF) {
    pins_to_timer();
  }
  state = PULSE_RUNNING;
  TIM1->DIER = TIM_DIER_CC2IE;
  TIM1->CR1 |= TIM_CR1_CEN;
}

void quad_stop(int force_low)
{
  TIM1->CR1 &= ~TIM_CR1_CEN;
  TIM1->DIER = 0;
  if (force_low) {
    set_oc_modes(OC_LOW);
    if (state == PULSE_OFF) {
      pins_to_timer();
    }
    state = PULSE_STOPPED;
  } else if (state == PULSE_RUNNING) {
    state = PULSE_STOPPED;        /* OCxREF keeps its level: the pins hold it */
  }
}

pulse_state_t quad_state(void)
{
  return state;
}

int quad_level_a(void)
{
  if (state == PULSE_OFF) {
    return -1;
  }
  return HAL_GPIO_ReadPin(QUAD_PORT, QUAD_A_PIN) == GPIO_PIN_SET;
}

int quad_level_b(void)
{
  if (state == PULSE_OFF) {
    return -1;
  }
  return HAL_GPIO_ReadPin(QUAD_PORT, QUAD_B_PIN) == GPIO_PIN_SET;
}

uint64_t quad_count(void)
{
  uint64_t t;

  __disable_irq();
  t = toggles_a;
  __enable_irq();
  return t / 2U;
}

/* One compare event per edge on A (CH2) */
void quad_irq_handler(void)
{
  if (TIM1->SR & TIM_SR_CC2IF) {
    TIM1->SR = ~TIM_SR_CC2IF;
    toggles_a++;
  }
}
