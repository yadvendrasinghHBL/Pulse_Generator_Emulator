/*
 * Speed model (stage 3): turns a train speed into the output frequency,
 * f = v / (pi x wheel / ppr), and changes the speed at the set acceleration
 * like a real train instead of jumping.
 *
 * This module is the one place that starts, stops and retunes all outputs
 * (J8.1 and A/B), so a ramp can never fight a direct command:
 * - speed_set_mhz / speed_start / speed_stop are the direct controls
 *   (`freq`, `start`, `stop`): they apply at once and cancel a ramp.
 * - speed_set_target_ckmh / _crpm (`speed`, `rpm`) are the driver's lever:
 *   they start the outputs from standstill if needed, ramp toward the target
 *   and, at target 0, stop them at standstill with the levels held.
 */
#ifndef SPEED_H
#define SPEED_H

#include <stdint.h>

#define SPEED_STEP_MS   20U     /* ramp step: 50 frequency updates per second */

void     speed_poll(void);                   /* call from every loop that waits */
/* Target speed in 0.01 km/h, ramped at accel (up) or decel (down).
 * 0 = OK, -1 = above speed_max_ckmh() (vmax or 50 kHz) or below 0.01 Hz */
int      speed_set_target_ckmh(uint32_t ckmh);
uint32_t speed_max_ckmh(void);               /* lower of vmax and the speed at 50 kHz */
/* Same as a target in wheel turns per minute, 0.01 rpm (rpm = f x 60 / ppr) */
int      speed_set_target_crpm(uint32_t crpm);
/* `brake`: slow to standstill at decel (0 = at once), then stop with the levels held */
void     speed_brake(void);
uint32_t speed_max_crpm(void);               /* rpm at speed_max_ckmh() */
double   speed_rpm(void);                    /* wheel rpm now */
double   speed_kmh_to_rpm(double kmh);       /* with the current wheel diameter */
double   speed_target_rpm(void);
double   speed_kmh(void);                    /* speed now; 0 while the outputs are not running */
double   speed_target_kmh(void);
int      speed_ramping(void);
/* Ramp rates in use: the settings, or a running profile's own rates */
uint32_t speed_accel_mm_s2(void);
uint32_t speed_decel_mm_s2(void);
/* Profile rates in mm/s2 (-1 = back to the settings); the settings are not changed */
void     speed_override_rates(int32_t accel, int32_t decel);
int      speed_moving(void);                 /* outputs running, or a ramp about to start them */
double   speed_distance_m(void);             /* A pulses since start x distance per pulse, - in reverse */

/* Wheel slip (pct > 0, traction) or slide (pct < 0, braking) for ms: the
 * output frequency is the train speed x (1 + pct / 100), then back. Only
 * while moving. 0 = OK, -1 = not moving or pct out of range. */
#define SPEED_SLIP_MIN  (-90)
#define SPEED_SLIP_MAX  200
int      speed_set_slip(int pct, uint32_t ms);
int      speed_slip_pct(void);                /* 0 = no slip */
uint32_t speed_slip_left_ms(void);
void     speed_slip_off(void);

int      speed_set_mhz(uint32_t mhz);        /* `freq`: all outputs; 0 = OK, -1 = out of range */
void     speed_start(void);                  /* `start`: all outputs at the set frequency */
void     speed_stop(int force_low);          /* `stop`: all outputs; force_low = 0 holds the levels */

#endif /* SPEED_H */
