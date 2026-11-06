/*
 * Sensor output A/B: J8.6 (PB0, TIM1_CH2N, AF1) and J8.7 (PB1, TIM1_CH3N, AF1).
 *
 * Why TIM1 and these channels: they are the only J8 pins that share one timer
 * other than TIM3, and TIM3 (PA6/PA7) is kept for the receiver inputs. One
 * timer for both outputs makes the 90 degrees exact by construction.
 *
 * How: both channels run in output-compare toggle mode. One counter period
 * (N counts) is one half period of the output. The leading channel toggles at
 * L = N/4, the lagging one at G = L + N x phase / 180, so for 90 degrees they
 * are N/2 counts = a quarter period apart and each has exactly 50 % duty.
 * TIM1 is 16-bit, so the prescaler is chosen per frequency (0.01 Hz needs
 * about 61000) and N is kept as large as possible for resolution.
 *
 * Only the complementary outputs (CHxN) are wired to J8. With CCxE = 0 and
 * CCxNE = 1, OCxN follows OCxREF (not inverted) once BDTR.MOE is set.
 * Registers are used directly: the HAL TIM state machine adds nothing here.
 *
 * Sensor faults (stage 3b, `fault` command) are made in the same timer:
 * - stuck channel: that channel's OCxM forced low or high;
 * - phase error: G moved (phase 0..134 degrees);
 * - dropped pulse: the compare interrupt forces the channel low for the two
 *   toggles of one pulse;
 * - glitch: a short forced opposite level between two real toggles.
 * The "ideal" levels (what a healthy sensor would output now) follow from
 * the toggle count of A and the counter position: after the lead channel
 * toggled (L <= CNT < G) the two levels differ, otherwise they are equal.
 */
#include "quad.h"
#include "board.h"

#define OC_TOGGLE   3U      /* OCxM = 0b0011 */
#define OC_LOW      4U      /* OCxM = 0b0100, force inactive */
#define OC_HIGH     5U      /* OCxM = 0b0101, force active */
#define IRQ_PRIO    6U      /* TIM1 (and TIM2) interrupt priority */
/* BASEPRI value that masks TIM1/TIM2 but lets the receiver (TIM3, priority 5) run */
#define MASK_OUTPUT_IRQS  (IRQ_PRIO << (8U - __NVIC_PRIO_BITS))

static uint32_t psc1 = 1;           /* prescaler divider (PSC + 1) */
static uint32_t n_cnt = 4;          /* counts per half period, multiple of 4 */
static quad_dir_t dir = QUAD_FWD;
static pulse_state_t state = PULSE_OFF;
static volatile uint64_t toggles_a;
static volatile uint32_t toggles_b;          /* valid while the B interrupt is on */

static uint32_t phase_deg = QUAD_PHASE_DEFAULT;
static quad_force_t force[2];
static volatile uint32_t drop_every[2];
static volatile uint32_t drop_n[2];
static volatile uint32_t drop_skip[2];       /* compare events left in a dropped pulse */
static volatile uint32_t dropped[2];

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

static uint32_t lead_cnt(uint32_t n)
{
  return n / 4U;
}

/* Below n for phase <= QUAD_PHASE_MAX: 1/4 + 134/180 < 1 */
static uint32_t lag_cnt(uint32_t n)
{
  return n / 4U + (uint32_t)((uint64_t)n * phase_deg / 180U);
}

/* ch 0 = A (CH2), 1 = B (CH3) */
static void set_oc_mode(int ch, uint32_t mode)
{
  if (ch == 0) {
    TIM1->CCMR1 = (TIM1->CCMR1 & ~TIM_CCMR1_OC2M) | (mode << TIM_CCMR1_OC2M_Pos);
  } else {
    TIM1->CCMR2 = (TIM1->CCMR2 & ~TIM_CCMR2_OC3M) | (mode << TIM_CCMR2_OC3M_Pos);
  }
}

static void set_oc_modes(uint32_t mode)
{
  set_oc_mode(0, mode);
  set_oc_mode(1, mode);
}

/* A = CH2, B = CH3: the leading channel toggles at L, the lagging one at G */
static void set_compare(uint32_t n)
{
  uint32_t lead = lead_cnt(n);
  uint32_t lag = lag_cnt(n);

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

/* The B interrupt is needed only to drop pulses on B (it doubles the load) */
static void set_irqs(void)
{
  uint32_t dier = TIM_DIER_CC2IE;

  if (drop_every[1]) {
    dier |= TIM_DIER_CC3IE;
  }
  TIM1->DIER = dier;
}

/*
 * Levels a healthy sensor would output now. Counter stopped, IRQs off.
 * A follows its toggle count; B differs from A after the lead toggled.
 */
static void ideal_levels(int *a, int *b)
{
  if (TIM1->SR & TIM_SR_CC2IF) {
    TIM1->SR = ~TIM_SR_CC2IF;     /* a toggle the interrupt has not counted yet */
    toggles_a++;
  }
  uint32_t c = TIM1->CNT;
  int between = c >= lead_cnt(n_cnt) && c < lag_cnt(n_cnt);

  *a = (int)(toggles_a & 1U);
  *b = between ? !*a : *a;
}

/* Put each channel at its level: a fault's fixed level, or the ideal level
 * and back to toggling. Ends any dropped pulse in progress. Counter stopped. */
static void apply_levels(int a, int b)
{
  int lvl[2] = {a, b};

  for (int ch = 0; ch < 2; ch++) {
    drop_skip[ch] = 0;
    drop_n[ch] = 0;
    if (force[ch] != QUAD_CH_OK) {
      set_oc_mode(ch, force[ch] == QUAD_CH_HIGH ? OC_HIGH : OC_LOW);
    } else {
      set_oc_mode(ch, lvl[ch] ? OC_HIGH : OC_LOW);
      set_oc_mode(ch, OC_TOGGLE);
    }
  }
  if ((int)(toggles_a & 1U) != a) {
    toggles_a++;                  /* keep the count's parity = A's level */
  }
  toggles_b = (uint32_t)b;
  TIM1->SR = ~TIM_SR_CC3IF;
}

/* Counter stopped, ideal levels re-applied, counter running again */
static void resync(void)
{
  int a;
  int b;

  TIM1->CR1 &= ~TIM_CR1_CEN;
  ideal_levels(&a, &b);
  apply_levels(a, b);
  set_irqs();
  TIM1->CR1 |= TIM_CR1_CEN;
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

  /* Cycle counter: times the glitch width (CPU clock = timer clock) */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  HAL_NVIC_SetPriority(TIM1_CC_IRQn, IRQ_PRIO, 0);
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
   * Edges alternate: lead at L, lag at G. Find how long ago the last edge
   * was; the next edge comes the new gap after it, or now if that time has
   * already passed. Then each level lasts between the old and the new length:
   * never a short pulse, never a missing or extra edge, so the phase and the
   * direction are kept.
   */
  __disable_irq();
  TIM1->CR1 &= ~TIM_CR1_CEN;      /* outputs hold their levels */
  uint32_t c = TIM1->CNT;
  uint32_t lead = lead_cnt(n_cnt);
  uint32_t lag = lag_cnt(n_cnt);
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

  /* convert to new counts; remaining counts to the next edge: 1 .. gap - 1 */
  uint64_t since_new = (uint64_t)since * psc1 / p1;
  uint32_t lead_n = lead_cnt(n);
  uint32_t lag_n = lag_cnt(n);
  uint32_t gap = next_is_lead ? n - (lag_n - lead_n) : lag_n - lead_n;
  uint32_t remaining = since_new < gap ? gap - (uint32_t)since_new : 1U;

  if (remaining >= gap && gap > 1U) {
    remaining = gap - 1U;         /* never start exactly on the previous edge */
  }
  int32_t pos = (int32_t)(next_is_lead ? lead_n : lag_n) - (int32_t)remaining;

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
  load_timebase(psc1, n_cnt);     /* CNT = 0: first edge (leading channel) at L = N/4 */
  TIM1->SR = 0;
  __disable_irq();
  toggles_a = 0;
  apply_levels(0, 0);             /* stuck channels take their fault level */
  __enable_irq();
  if (state == PULSE_OFF) {
    pins_to_timer();
  }
  state = PULSE_RUNNING;
  set_irqs();
  TIM1->CR1 |= TIM_CR1_CEN;
}

void quad_stop(int force_low)
{
  TIM1->CR1 &= ~TIM_CR1_CEN;
  TIM1->DIER = 0;
  drop_skip[0] = drop_skip[1] = 0;
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

/* ---- sensor faults ---- */

int quad_set_force(int ch, quad_force_t f)
{
  if (ch < 0 || ch > 1) {
    return -1;
  }
  __disable_irq();
  force[ch] = f;
  if (state == PULSE_RUNNING) {
    if (f != QUAD_CH_OK) {
      drop_skip[ch] = 0;
      set_oc_mode(ch, f == QUAD_CH_HIGH ? OC_HIGH : OC_LOW);
    } else {
      resync();                   /* back in step with the other channel */
    }
  }
  __enable_irq();
  return 0;
}

quad_force_t quad_force(int ch)
{
  return force[ch & 1];
}

int quad_set_phase(uint32_t deg)
{
  int a;
  int b;

  if (deg > QUAD_PHASE_MAX) {
    return -1;
  }
  __disable_irq();
  if (state != PULSE_RUNNING) {
    phase_deg = deg;
    set_compare(n_cnt);
    __enable_irq();
    return 0;
  }
  /* The lead point L does not move, so the lead keeps its level; the lag
   * channel takes the level the new G gives at this counter position. */
  TIM1->CR1 &= ~TIM_CR1_CEN;
  ideal_levels(&a, &b);
  int lead = dir == QUAD_FWD ? a : b;

  phase_deg = deg;
  set_compare(n_cnt);
  uint32_t c = TIM1->CNT;
  int lag = (c >= lead_cnt(n_cnt) && c < lag_cnt(n_cnt)) ? !lead : lead;

  apply_levels(dir == QUAD_FWD ? lead : lag, dir == QUAD_FWD ? lag : lead);
  set_irqs();
  TIM1->CR1 |= TIM_CR1_CEN;
  __enable_irq();
  return 0;
}

uint32_t quad_phase(void)
{
  return phase_deg;
}

int quad_set_drop(int ch, uint32_t every)
{
  if (ch < 0 || ch > 1 || (every != 0U && every < QUAD_DROP_MIN) || every > QUAD_DROP_MAX) {
    return -1;
  }
  __disable_irq();
  drop_every[ch] = every;
  dropped[ch] = 0;
  if (state == PULSE_RUNNING) {
    resync();                     /* ends a dropped pulse, sets the B interrupt */
  }
  __enable_irq();
  return 0;
}

uint32_t quad_drop(int ch)
{
  return drop_every[ch & 1];
}

uint32_t quad_dropped(int ch)
{
  return dropped[ch & 1];
}

int quad_glitch(int ch, uint32_t width_ticks)
{
  if (ch < 0 || ch > 1 || state != PULSE_RUNNING || force[ch] != QUAD_CH_OK) {
    return -1;
  }
  /* Mask only the output timers' interrupts: the receiver (TIM3) must still
   * capture both glitch edges in loopback. Its interrupt may lengthen the
   * glitch by about 1 us. */
  uint32_t basepri = __get_BASEPRI();

  __set_BASEPRI(MASK_OUTPUT_IRQS);
  if (drop_skip[ch]) {
    __set_BASEPRI(basepri);
    return -1;
  }
  /* Only if the channel's next real toggle is well after the glitch */
  uint32_t c = TIM1->CNT;
  uint32_t ccr = ch == 0 ? TIM1->CCR2 : TIM1->CCR3;
  uint32_t dist = ccr > c ? ccr - c : ccr + n_cnt - c;

  if ((uint64_t)dist * psc1 < 2ULL * width_ticks + 400U) {
    __set_BASEPRI(basepri);
    return -1;
  }
  uint16_t pin = ch == 0 ? QUAD_A_PIN : QUAD_B_PIN;
  int lvl = (QUAD_PORT->IDR & pin) != 0;

  set_oc_mode(ch, lvl ? OC_LOW : OC_HIGH);
  uint32_t t0 = DWT->CYCCNT;
  while (DWT->CYCCNT - t0 < width_ticks) {
  }
  set_oc_mode(ch, lvl ? OC_HIGH : OC_LOW);
  set_oc_mode(ch, OC_TOGGLE);
  __set_BASEPRI(basepri);
  return 0;
}

/* One compare event of channel ch: count it, and drop a pulse when due.
 * A pulse is dropped from the falling toggle: the channel is held low for
 * the next two toggles (the missing rise and fall), then toggles again. */
static void drop_event(int ch, int lvl)
{
  if (!drop_every[ch] || force[ch] != QUAD_CH_OK) {
    return;
  }
  if (drop_skip[ch]) {
    if (--drop_skip[ch] == 0U) {
      set_oc_mode(ch, OC_TOGGLE);
    }
    return;
  }
  if (!lvl && ++drop_n[ch] >= drop_every[ch] - 1U) {   /* every-1 pulses, then 1 missing */
    drop_n[ch] = 0;
    set_oc_mode(ch, OC_LOW);
    drop_skip[ch] = 2;
    dropped[ch]++;
  }
}

/* One compare event per edge on A (CH2), and on B (CH3) while dropping B pulses */
void quad_irq_handler(void)
{
  uint32_t sr = TIM1->SR;

  if (sr & TIM_SR_CC2IF) {
    TIM1->SR = ~TIM_SR_CC2IF;
    toggles_a++;
    drop_event(0, (int)(toggles_a & 1U));
  }
  if ((sr & TIM_SR_CC3IF) && (TIM1->DIER & TIM_DIER_CC3IE)) {
    TIM1->SR = ~TIM_SR_CC3IF;
    toggles_b++;
    drop_event(1, (int)(toggles_b & 1U));
  }
}
