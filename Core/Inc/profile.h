/*
 * Drive profiles: a stored journey (up to PROFILE_MAX_STEPS steps such as
 * speed 100, reach, wait 60, brake) that the board plays back by itself, so a
 * receiver sees the same run every time without typing commands.
 *
 * Steps call the same speed model as the console commands. A manual drive
 * command (speed, rpm, brake, freq, start, stop) stops a running profile.
 * `profile save` keeps the profile in its own flash page (0x0803F000, just
 * before the settings page); it is loaded at power-up but never runs by itself.
 */
#ifndef PROFILE_H
#define PROFILE_H

#include <stdint.h>

#define PROFILE_ADDR        0x0803F000U   /* second-last 2 KB page, see STM32L451RCTX_FLASH.ld */
#define PROFILE_MAX_STEPS   32U
#define PROFILE_WAIT_MAX_DS 36000U        /* wait up to 3600.0 s (0.1 s units) */

typedef enum {
  PROFILE_SPEED = 1,     /* b = target, 0.01 km/h */
  PROFILE_RPM,           /* b = target, 0.01 rpm */
  PROFILE_ACCEL,         /* b = mm/s2 */
  PROFILE_DECEL,         /* b = mm/s2 */
  PROFILE_BRAKE,
  PROFILE_STOP,          /* stop at once, levels held */
  PROFILE_DIR,           /* a = quad_dir_t; only at standstill */
  PROFILE_WAIT,          /* b = 0.1 s */
  PROFILE_REACH,         /* wait until the target speed is reached */
  PROFILE_SLIP,          /* a = % (signed), b = 0.1 s */
} profile_op_t;

typedef struct {
  uint8_t  op;           /* profile_op_t */
  uint8_t  pad;
  int16_t  a;
  uint32_t b;
} profile_step_t;

typedef enum {
  PROFILE_IDLE = 0,      /* never run since the last change */
  PROFILE_RUNNING,
  PROFILE_DONE,
  PROFILE_STOPPED,       /* by `profile stop` or a manual drive command */
  PROFILE_FAILED,        /* a step could not run (e.g. speed above vmax) */
} profile_state_t;

void            profile_init(void);                 /* load the saved profile */
int             profile_add(const profile_step_t *s);   /* 0 = OK, -1 = full or running */
int             profile_del(uint32_t n);            /* 1-based; -1 = no such step or running */
int             profile_clear(void);
void            profile_demo(void);                 /* replace with the demo journey */
uint32_t        profile_count(void);
const profile_step_t *profile_step(uint32_t n);     /* 1-based, NULL if none */
/* Play the profile `passes` times (0 = loop until stopped). -1 = empty. */
int             profile_run(uint32_t passes);
int             profile_abort(void);                /* 1 if a running profile was stopped */
void            profile_poll(void);                 /* call from every loop that waits */
profile_state_t profile_state(void);
uint32_t        profile_current(void);              /* 1-based step running or failed */
uint32_t        profile_pass(void);
uint32_t        profile_passes(void);               /* 0 = loop */
uint32_t        profile_wait_left_ms(void);
int             profile_saved(void);                /* 1 = flash holds the profile in RAM */
/* Write to flash: 0 = OK, -1 = flash error. CPU stalls up to 22 ms: only at standstill. */
int             profile_save(void);

#endif /* PROFILE_H */
