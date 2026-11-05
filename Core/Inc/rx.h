/*
 * Receiver (processing unit): measures the two tracks A/B of an optical pulse
 * generator on J8.2 (PA6, TIM3_CH1) and J8.3 (PA7, TIM3_CH2) and derives the
 * speed parameters: frequency and duty per channel, phase A-B, direction,
 * speed, distance, acceleration, standstill and faults.
 *
 * Inputs are 3.3 V logic. Never connect a real 10-30 V OPG directly
 * (PLAN.md stage 6). Loopback test: J8.6 -> J8.2, J8.7 -> J8.3.
 */
#ifndef RX_H
#define RX_H

#include <stdint.h>

#define RX_PPR              200U    /* pulses per turn (stage 3 makes it a setting) */
#define RX_WHEEL_MM         920U    /* wheel diameter */
#define RX_STANDSTILL_MS    2000U   /* no edge for this long = standstill (Q7) */
#define RX_PHASE_TOL_DEG    20.0    /* 90 +/- 20 degrees is OK (Q7) */
#define RX_UPDATE_MS        500U    /* values are recalculated this often */

typedef enum {
  RX_STANDSTILL = 0,
  RX_OK,
  RX_FAULT_A_MISSING,      /* B moves, A has no edges */
  RX_FAULT_B_MISSING,
  RX_FAULT_PHASE,          /* phase outside 90 +/- tol (forward) or 270 +/- tol (reverse) */
} rx_status_t;

typedef struct {
  double      freq_hz[2];  /* A, B */
  double      duty_pct[2];
  int         moving[2];   /* edges within the standstill time */
  double      phase_deg;   /* B rising edge after A rising edge, 0..360; < 0 = unknown */
  int         forward;     /* 1 forward (A leads), 0 reverse; valid when phase_deg >= 0 */
  double      speed_kmh;
  double      distance_m;  /* signed: forward adds, reverse subtracts */
  double      accel_ms2;
  int64_t     pulses;      /* A rising edges, forward minus reverse */
  uint32_t    overcaptures;/* edges lost because they came too fast */
  rx_status_t status;
} rx_values_t;

void        rx_init(void);
void        rx_poll(void);                 /* call from the main loop: recalculates every RX_UPDATE_MS */
int         rx_updated(void);              /* 1 once after each recalculation */
void        rx_get(rx_values_t *out);
void        rx_reset_distance(void);
const char *rx_status_name(rx_status_t s);
void        rx_irq_handler(void);
/* Raw diagnostics: pin levels, edges seen per channel, TIM3 registers */
void        rx_print_inputs(void);

#endif /* RX_H */
