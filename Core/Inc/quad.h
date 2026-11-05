/*
 * Two-channel sensor output A/B on J8.6 (PB0, TIM1_CH2N) and J8.7 (PB1,
 * TIM1_CH3N): two 50 % square waves 90 degrees apart, like the two tracks of
 * an axle-mounted optical pulse generator. Forward: A leads, B follows a
 * quarter period later. Reverse: B leads.
 *
 * Both channels come from one timer, so the 90 degrees are exact at every
 * frequency. Frequencies are in mHz (no floats), the same range as pulse.h.
 */
#ifndef QUAD_H
#define QUAD_H

#include <stdint.h>

#include "pulse.h"   /* pulse_state_t, PULSE_MIN_MHZ, PULSE_MAX_MHZ */

typedef enum {
  QUAD_FWD = 0,      /* A leads B by 90 degrees */
  QUAD_REV,          /* B leads A by 90 degrees */
} quad_dir_t;

void          quad_init(void);
/* Set the frequency (mHz). Applies at once while running, keeps the A/B
 * sequence and never makes a level shorter than min(old, new) half period. */
int           quad_set_mhz(uint32_t mhz);
uint32_t      quad_actual_mhz(void);
uint32_t      quad_counter_period(void);  /* timer counts per half period (multiple of 4) */
uint32_t      quad_prescaler(void);       /* timer clock divider (PSC + 1) */
/* Direction: only while not running (a wheel reverses at standstill). 0 = OK */
int           quad_set_dir(quad_dir_t dir);
quad_dir_t    quad_dir(void);
void          quad_start(void);           /* both low; leading channel rises 1/8 period later */
void          quad_stop(int force_low);   /* force_low = 0: hold both levels */
pulse_state_t quad_state(void);
int           quad_level_a(void);         /* -1 if OFF */
int           quad_level_b(void);
uint64_t      quad_count(void);           /* complete pulses on A since the last start */
void          quad_irq_handler(void);

#endif /* QUAD_H */
