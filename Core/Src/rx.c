/*
 * Receiver on J8.2 (PA6, TIM3_CH1, AF2) and J8.3 (PA7, TIM3_CH2, AF2).
 *
 * Why input capture: the timer latches the counter at each edge in hardware,
 * so the time stamps are exact to one 80 MHz tick (12.5 ns) whatever the
 * interrupt latency. TIM3 is 16-bit; the overflow count in software extends
 * it to 64 bits. Both edges are captured: rising edges give period, phase and
 * direction, falling edges the duty cycle.
 *
 * The interrupt only stores time stamps and counts. Frequencies, phase,
 * speed and faults are calculated in the main loop every RX_UPDATE_MS
 * (double precision: a few hundred microseconds every 0.5 s).
 *
 * Frequency: (rising edges in the window) / (time between the last rising
 * edge of this window and of the previous one). This averages over all
 * periods of the window and has no +/-1 count error. At low speed, when no
 * edge arrived in the window, the last period is used, limited by the time
 * since the last edge (a slowing wheel shows a falling speed at once).
 *
 * Direction (quadrature): at each rising edge of A, B low = forward (A leads),
 * B high = reverse. Phase: time from A's rising edge to B's next rising edge,
 * as a fraction of A's period: about 90 deg forward, 270 deg reverse.
 */
#include "rx.h"
#include "board.h"
#include "console.h"
#include "settings.h"

#define TICKS_PER_S   80000000.0     /* TIM3 clock, PSC = 0 (APB1 = 80 MHz) */
#define IC_FILTER     3U             /* ICxF = 0b0011: fCK_INT, N = 8 -> 100 ns */

typedef struct {
  uint32_t rises;
  uint64_t last_rise;
  uint64_t period;
  uint64_t high;
  uint64_t last_edge;
  uint8_t  have_rise;
  uint8_t  have_period;
  uint8_t  have_high;
  uint8_t  have_edge;
} chan_t;

typedef struct {
  chan_t   ch[2];
  int64_t  position;
  uint64_t phase_ticks;
  uint64_t phase_period;
  uint8_t  have_phase;
  uint32_t overcaptures;
} capture_t;

static volatile capture_t cap;
static volatile uint32_t ovf;
static volatile uint8_t last_lvl[2];  /* input level after the last edge handled */

static capture_t prev;               /* snapshot of the previous window */
static uint64_t prev_now;
static int have_prev;
static int64_t pos_offset;           /* rx_reset_distance() */
static rx_values_t val;
static uint32_t last_update;
static int updated;

static void snapshot(capture_t *out, uint64_t *now)
{
  __disable_irq();
  *out = *(const capture_t *)&cap;
  uint32_t o = ovf;
  uint32_t c = TIM3->CNT;
  if (TIM3->SR & TIM_SR_UIF) {       /* overflow not handled yet */
    c = TIM3->CNT;
    o++;
  }
  __enable_irq();
  *now = ((uint64_t)o << 16) | c;
}

static void edge(int n, uint64_t t, int high)
{
  volatile chan_t *p = &cap.ch[n];

  p->last_edge = t;
  p->have_edge = 1;
  if (high) {
    if (p->have_rise) {
      p->period = t - p->last_rise;
      p->have_period = 1;
    }
    p->last_rise = t;
    p->have_rise = 1;
    p->rises++;
    if (n == 0) {
      /* A rising: B low = A leads = forward */
      cap.position += last_lvl[1] ? -1 : 1;
    } else if (cap.ch[0].have_period) {
      cap.phase_ticks = t - cap.ch[0].last_rise;
      cap.phase_period = cap.ch[0].period;
      cap.have_phase = 1;
    }
  } else if (p->have_rise) {
    p->high = t - p->last_rise;
    p->have_high = 1;
  }
}

void rx_irq_handler(void)
{
  uint32_t sr = TIM3->SR;
  uint32_t o = ovf;
  uint64_t t[2];
  int got[2] = {0, 0};

  if (sr & TIM_SR_UIF) {
    TIM3->SR = ~TIM_SR_UIF;
    ovf = o + 1U;
  }
  /* A capture taken after the overflow has a small value: it belongs to o + 1 */
  if (sr & TIM_SR_CC1IF) {
    uint32_t c = TIM3->CCR1;         /* reading clears CC1IF */
    t[0] = ((uint64_t)(o + ((sr & TIM_SR_UIF) && c < 0x8000U)) << 16) | c;
    got[0] = 1;
  }
  if (sr & TIM_SR_CC2IF) {
    uint32_t c = TIM3->CCR2;
    t[1] = ((uint64_t)(o + ((sr & TIM_SR_UIF) && c < 0x8000U)) << 16) | c;
    got[1] = 1;
  }
  /*
   * Edge direction: the edges of one input always alternate, so each one is
   * the opposite of the last. The pin is read only when edges were lost
   * (overcapture): then the latest captured edge is the one that gave the
   * level now. Reading the pin for every edge would give the wrong direction
   * when the next edge follows within the interrupt latency (short glitch).
   */
  uint32_t of = sr & (TIM_SR_CC1OF | TIM_SR_CC2OF);

  if (of) {
    TIM3->SR = ~of;
    cap.overcaptures++;
  }
  int lvl_a = (of & TIM_SR_CC1OF) ? (RX_A_PORT->IDR & RX_A_PIN) != 0 : !last_lvl[0];
  int lvl_b = (of & TIM_SR_CC2OF) ? (RX_B_PORT->IDR & RX_B_PIN) != 0 : !last_lvl[1];

  if (got[0] && got[1] && t[1] < t[0]) {
    last_lvl[1] = (uint8_t)lvl_b;
    edge(1, t[1], lvl_b);            /* keep time order: B first */
    last_lvl[0] = (uint8_t)lvl_a;
    edge(0, t[0], lvl_a);
  } else {
    if (got[0]) {
      last_lvl[0] = (uint8_t)lvl_a;
      edge(0, t[0], lvl_a);
    }
    if (got[1]) {
      last_lvl[1] = (uint8_t)lvl_b;
      edge(1, t[1], lvl_b);
    }
  }
}

void rx_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();
  __HAL_RCC_TIM3_FORCE_RESET();
  __HAL_RCC_TIM3_RELEASE_RESET();

  /* Inputs only, nothing is driven. Pull-downs keep open inputs quiet. */
  gpio.Pin = RX_A_PIN | RX_B_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLDOWN;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = RX_AF;
  HAL_GPIO_Init(RX_A_PORT, &gpio);

  TIM3->PSC = 0;
  TIM3->ARR = 0xFFFFU;
  TIM3->CCMR1 = (1U << TIM_CCMR1_CC1S_Pos) | (IC_FILTER << TIM_CCMR1_IC1F_Pos) |
                (1U << TIM_CCMR1_CC2S_Pos) | (IC_FILTER << TIM_CCMR1_IC2F_Pos);
  TIM3->CCER = TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC1NP |    /* both edges */
               TIM_CCER_CC2E | TIM_CCER_CC2P | TIM_CCER_CC2NP;
  TIM3->EGR = TIM_EGR_UG;
  TIM3->SR = 0;
  last_lvl[0] = (RX_A_PORT->IDR & RX_A_PIN) != 0;
  last_lvl[1] = (RX_B_PORT->IDR & RX_B_PIN) != 0;
  TIM3->DIER = TIM_DIER_UIE | TIM_DIER_CC1IE | TIM_DIER_CC2IE;
  HAL_NVIC_SetPriority(TIM3_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(TIM3_IRQn);
  TIM3->CR1 = TIM_CR1_CEN;
  last_update = HAL_GetTick();
}

static double chan_freq(int n, const capture_t *s, uint64_t now)
{
  const chan_t *c = &s->ch[n];
  const double stand = RX_STANDSTILL_MS / 1000.0 * TICKS_PER_S;

  if (!c->have_rise || (double)(now - c->last_edge) > stand) {
    return 0.0;
  }
  uint32_t dr = c->rises - prev.ch[n].rises;
  if (have_prev && dr > 0 && prev.ch[n].have_rise && c->last_rise > prev.ch[n].last_rise) {
    return dr * TICKS_PER_S / (double)(c->last_rise - prev.ch[n].last_rise);
  }
  if (!c->have_period) {
    return 0.0;
  }
  /* no full period in this window: last period, but not more than 1 / time since the last rise */
  uint64_t t = c->period;
  if (now - c->last_rise > t) {
    t = now - c->last_rise;
  }
  return TICKS_PER_S / (double)t;
}

static void recalc(void)
{
  capture_t s;
  uint64_t now;
  const double stand = RX_STANDSTILL_MS / 1000.0 * TICKS_PER_S;
  const double m_per_pulse = settings_m_per_pulse();
  double old_speed = val.speed_kmh;

  snapshot(&s, &now);
  for (int n = 0; n < 2; n++) {
    const chan_t *c = &s.ch[n];
    val.moving[n] = c->have_edge && (double)(now - c->last_edge) <= stand;
    val.freq_hz[n] = val.moving[n] ? chan_freq(n, &s, now) : 0.0;
    val.duty_pct[n] = val.moving[n] && c->have_period && c->have_high && c->period > 0
                      ? 100.0 * (double)c->high / (double)c->period : 0.0;
  }

  val.phase_deg = -1.0;
  if (val.moving[0] && val.moving[1] && s.have_phase && s.phase_period > 0 &&
      s.phase_ticks <= s.phase_period) {
    val.phase_deg = 360.0 * (double)s.phase_ticks / (double)s.phase_period;
    val.forward = val.phase_deg < 180.0;
  }

  double f = val.moving[0] ? val.freq_hz[0] : val.freq_hz[1];
  val.speed_kmh = f * m_per_pulse * 3.6;
  val.pulses = s.position - pos_offset;
  val.distance_m = (double)val.pulses * m_per_pulse;
  if (have_prev && now > prev_now) {
    double dt = (double)(now - prev_now) / TICKS_PER_S;
    val.accel_ms2 = (val.speed_kmh - old_speed) / 3.6 / dt;
  } else {
    val.accel_ms2 = 0.0;
  }
  val.overcaptures = s.overcaptures;

  if (!val.moving[0] && !val.moving[1]) {
    val.status = RX_STANDSTILL;
    val.speed_kmh = 0.0;
  } else if (!val.moving[0]) {
    val.status = RX_FAULT_A_MISSING;
  } else if (!val.moving[1]) {
    val.status = RX_FAULT_B_MISSING;
  } else if (val.phase_deg < 0.0) {
    val.status = RX_OK;              /* both move, first phase not measured yet */
  } else {
    double target = val.forward ? 90.0 : 270.0;
    double err = val.phase_deg - target;
    val.status = (err <= RX_PHASE_TOL_DEG && err >= -RX_PHASE_TOL_DEG) ? RX_OK : RX_FAULT_PHASE;
  }

  prev = s;
  prev_now = now;
  have_prev = 1;
  updated = 1;
}

void rx_poll(void)
{
  if (HAL_GetTick() - last_update >= RX_UPDATE_MS) {
    last_update += RX_UPDATE_MS;
    recalc();
  }
}

int rx_updated(void)
{
  int u = updated;

  updated = 0;
  return u;
}

void rx_get(rx_values_t *out)
{
  *out = val;
}

void rx_reset_distance(void)
{
  capture_t s;
  uint64_t now;

  snapshot(&s, &now);
  pos_offset = s.position;
  val.pulses = 0;
  val.distance_m = 0.0;
}

/* Mode, alternate function and pull of one pin, read from its port registers */
static void print_pin_config(const char *name, GPIO_TypeDef *port, uint32_t pin)
{
  uint32_t i = (uint32_t)__builtin_ctz(pin);

  con_printf("%s %s: MODER %lu AF %lu PUPDR %lu", name, (port->IDR & pin) ? "high" : "low",
             (port->MODER >> (2U * i)) & 3U, (port->AFR[i >> 3] >> (4U * (i & 7U))) & 15U,
             (port->PUPDR >> (2U * i)) & 3U);
}

int rx_signal_present(void)
{
  capture_t s;
  uint64_t now;
  const uint64_t recent = (uint64_t)(TICKS_PER_S / 10.0);   /* 100 ms */

  snapshot(&s, &now);
  for (int n = 0; n < 2; n++) {
    if (s.ch[n].have_edge && now - s.ch[n].last_edge < recent) {
      return 1;
    }
  }
  return 0;
}

void rx_print_inputs(void)
{
  con_printf("pins (%s): ", RX_NAME);
  print_pin_config("A", RX_A_PORT, RX_A_PIN);
  con_puts("; ");
  print_pin_config("B", RX_B_PORT, RX_B_PIN);
  con_puts("\n");
  con_printf("edges: A %lu rising, B %lu rising, lost %lu\n",
             cap.ch[0].rises, cap.ch[1].rises, cap.overcaptures);
  con_printf("TIM3: CR1 %04lx CCMR1 %04lx CCER %04lx DIER %04lx SR %04lx OR1 %lx\n",
             TIM3->CR1, TIM3->CCMR1, TIM3->CCER, TIM3->DIER, TIM3->SR, TIM3->OR1);
}

const char *rx_status_name(rx_status_t s)
{
  switch (s) {
  case RX_OK:
    return "OK";
  case RX_FAULT_A_MISSING:
    return "FAULT A missing";
  case RX_FAULT_B_MISSING:
    return "FAULT B missing";
  case RX_FAULT_PHASE:
    return "FAULT phase";
  default:
    return "standstill";
  }
}
