/*
 * Diagnostics: chip identity, supply/temperature measurement, BOOT0 readback
 * and the power-on self-test.
 */
#ifndef DIAG_H
#define DIAG_H

#include <stdint.h>

void diag_init(void);            /* call once after clocks; latches reset cause */
void diag_print_info(void);
int  diag_measure(uint32_t *vdda_mv, int32_t *temp_c);
int  diag_post(void);            /* returns number of failed checks */
int  diag_post_result(void);     /* failures of the last POST, -1 if not run yet */
int  diag_boot0_level(void);

#endif /* DIAG_H */
