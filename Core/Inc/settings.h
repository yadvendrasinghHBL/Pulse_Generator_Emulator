/*
 * Settings that survive a reset: pulses per turn, wheel diameter,
 * acceleration and direction. The outputs (speed model) and the receiver use
 * the same pulses per turn and wheel (decided by the user, stage 3).
 *
 * Changes apply at once but reach the flash only with settings_save()
 * (`save`), so typing values does not wear the flash. Storage: the last flash
 * page (0x0803F800, 2 KB), which the linker script keeps out of the image.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

#define SETTINGS_ADDR               0x0803F800U   /* last 2 KB page, see STM32L451RCTX_FLASH.ld */
#define SETTINGS_PAGE_BYTES         2048U

#define SETTINGS_PPR_MIN            1U
#define SETTINGS_PPR_MAX            1000U         /* HaslerRail OPG: 1..200, 400 on request */
#define SETTINGS_PPR_DEFAULT        200U          /* Q1 */
#define SETTINGS_WHEEL_MIN_MM       100U
#define SETTINGS_WHEEL_MAX_MM       2000U
#define SETTINGS_WHEEL_DEFAULT_MM   920U          /* Q1 */
#define SETTINGS_ACCEL_MAX_MM_S2    10000U        /* 10 m/s2 */
#define SETTINGS_ACCEL_DEFAULT_MM_S2 0U           /* 0 = a new speed applies at once */

/* Load the last saved record (or the defaults) and apply the direction.
 * Call after quad_init(), before anything starts the outputs. */
void     settings_init(void);
int      settings_from_flash(void);    /* 1 = a saved record was loaded at boot or saved since */
int      settings_dirty(void);         /* 1 = current values differ from the saved ones */
uint32_t settings_slots_used(void);    /* records in the page (erased after the last one) */

uint32_t settings_ppr(void);
int      settings_set_ppr(uint32_t ppr);          /* 0 = OK, -1 = out of range */
uint32_t settings_wheel_mm(void);
int      settings_set_wheel_mm(uint32_t mm);
uint32_t settings_accel_mm_s2(void);
int      settings_set_accel_mm_s2(uint32_t a);
double   settings_m_per_pulse(void);              /* pi x wheel / ppr */

/* Defaults in RAM (200 ppr, 920 mm, accel 0, forward); not saved until settings_save().
 * The outputs must be stopped (the direction changes). */
void     settings_defaults(void);
/* Write the current values (and the direction) to flash: 0 = OK, -1 = flash error.
 * The CPU stalls during a page erase (about 22 ms): call only while the outputs are stopped. */
int      settings_save(void);

#endif /* SETTINGS_H */
