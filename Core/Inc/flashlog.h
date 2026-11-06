/*
 * Record log in one 2 KB flash page, shared by settings.c and profile.c.
 *
 * Why a log: a page erase stalls the CPU for about 22 ms and the flash is
 * rated for 10 000 erase cycles. Each save appends one record (a multiple of
 * 8 bytes, the L4 programming unit) to the next free slot; the page is erased
 * only when it is full. The caller decides which record is valid (magic,
 * version, CRC) and loads the last valid one at boot.
 */
#ifndef FLASHLOG_H
#define FLASHLOG_H

#include <stddef.h>
#include <stdint.h>

#define FLASHLOG_PAGE_BYTES 2048U

uint32_t flashlog_crc32(const void *data, size_t len);
int      flashlog_erased(uint32_t addr, size_t len);       /* 1 = all 0xFF */
/* Slots in use at boot: records are written in order, the first erased slot ends the log */
uint32_t flashlog_used(uint32_t page, size_t rec_len);
/* Write rec (rec_len bytes, multiple of 8) after `*used` slots, erasing the page
 * first when it is full. Returns the slot written (and updates *used), or -1 on
 * a flash error (then *used forces an erase on the next try). */
int      flashlog_append(uint32_t page, size_t rec_len, uint32_t *used, const void *rec);

#endif /* FLASHLOG_H */
