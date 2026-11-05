/*
 * Pulse output on J8.1 (PA5, TIM2_CH1): a 50 % square wave like one channel of
 * an axle-mounted optical pulse generator.
 *
 * TIM2 runs in output-compare toggle mode, so the duty cycle is exactly 50 %
 * by construction and a frequency change never produces a short pulse.
 * Frequencies are in mHz (1 Hz = 1000 mHz) so the console needs no floats.
 */
#ifndef PULSE_H
#define PULSE_H

#include <stdint.h>

#define PULSE_MIN_MHZ       10U          /* 0.01 Hz: half period 50 s */
#define PULSE_MAX_MHZ       50000000U    /* 50 kHz: OPG range is up to 10 kHz (40 kHz special) */
#define PULSE_DEFAULT_MHZ   100000U      /* 100 Hz */

typedef enum {
  PULSE_OFF = 0,      /* pin not driven (reset/analog state), as after power-up */
  PULSE_RUNNING,
  PULSE_STOPPED,      /* pin driven, holding the level it had when stopped */
} pulse_state_t;

void          pulse_init(void);
/* Set the frequency (mHz). Takes effect at once, also while running. 0 = OK */
int           pulse_set_mhz(uint32_t mhz);
uint32_t      pulse_get_mhz(void);        /* as requested */
uint32_t      pulse_actual_mhz(void);     /* as produced by the timer (rounded) */
uint32_t      pulse_half_period_ticks(void);
void          pulse_start(void);
/* Stop the output. force_low = 0: hold the current level (like a stopped wheel) */
void          pulse_stop(int force_low);
pulse_state_t pulse_state(void);
int           pulse_level(void);          /* pin level now, -1 if OFF */
uint64_t      pulse_count(void);          /* complete pulses since the last start */
void          pulse_irq_handler(void);

#endif /* PULSE_H */
