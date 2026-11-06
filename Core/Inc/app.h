/*
 * Background work of the firmware, one call for every loop that waits (the
 * main loop and `monitor`), so no feature freezes while another loop runs.
 */
#ifndef APP_H
#define APP_H

/* Receiver values, speed ramps, fault glitches, drive profiles, heartbeat LED */
void app_poll(void);

#endif /* APP_H */
