/*
 * Sensor fault emulation (`fault` command): makes the A/B outputs look like a
 * failing optical pulse generator, so a receiver's fault detection can be
 * tested. The faults themselves are made in quad.c (stuck channel, phase,
 * dropped pulses, glitches) and speed.c (wheel slip/slide); this module
 * schedules the glitches, clears everything at once and prints the state.
 * Faults are not saved in flash: after a reset the sensor is healthy.
 */
#ifndef FAULT_H
#define FAULT_H

#include <stdint.h>

#define FAULT_GLITCH_MAX_PER_S      1000U
#define FAULT_GLITCH_WIDTH_DEFAULT  2U       /* us */
#define FAULT_GLITCH_WIDTH_MAX      50U      /* us */

/* Glitches on A (mask bit 0) and/or B (bit 1): per_s short opposite pulses per
 * second, width_us wide. per_s 0 = off. 0 = OK, -1 = out of range. */
int      fault_set_glitch(uint32_t mask, uint32_t per_s, uint32_t width_us);
uint32_t fault_glitches(int ch);       /* made since set */
void     fault_poll(void);             /* call from every loop that waits */
void     fault_clear(void);            /* healthy sensor again */
int      fault_any(void);
void     fault_print(void);            /* one line: "faults: ..." */

#endif /* FAULT_H */
