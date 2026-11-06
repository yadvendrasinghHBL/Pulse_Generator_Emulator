/*
 * Settings in the last flash page (0x0803F800).
 *
 * Why a record log instead of one fixed record: a page erase stalls the CPU
 * for about 22 ms and the flash is rated for 10 000 erase cycles. Each save
 * appends one 32-byte record (4 double words, the L4 programming unit) to the
 * next free slot; only when all 64 slots are used is the page erased and the
 * record written to slot 0. At boot the last slot with a valid record wins.
 * A record is valid when its magic, layout version, CRC-32 and value ranges
 * are all right, so an interrupted write falls back to the previous record
 * (or to the defaults).
 */
#include "settings.h"
#include "board.h"
#include "flashlog.h"
#include "quad.h"

#include <stddef.h>

#define REC_MAGIC     0x31534750U    /* "PGS1" */
#define REC_VERSION   2U             /* 1: no decel/vmax (reserved = 0), still loaded */
#define SLOTS         (FLASHLOG_PAGE_BYTES / sizeof(rec_t))

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t ppr;
  uint16_t wheel_mm;
  uint16_t accel_mm_s2;
  uint8_t  dir;          /* quad_dir_t */
  uint8_t  pad[3];
  uint32_t seq;          /* save counter, for diagnostics */
  uint16_t decel_mm_s2;  /* version 2; 0 in version 1 records */
  uint16_t vmax_kmh;     /* version 2; 0 in version 1 records */
  uint32_t reserved;
  uint32_t crc;          /* CRC-32 of all bytes before it */
} rec_t;

_Static_assert(sizeof(rec_t) == 32U, "record must be 4 double words");

static rec_t cur;        /* values in use */
static rec_t saved;      /* what the flash holds (defaults if nothing valid) */
static int from_flash;
static uint32_t used;    /* slots in use */

static const volatile rec_t *slot(uint32_t i)
{
  return (const volatile rec_t *)(SETTINGS_ADDR + i * sizeof(rec_t));
}

static int valid(const rec_t *r)
{
  if (r->magic != REC_MAGIC || r->crc != flashlog_crc32(r, offsetof(rec_t, crc))) {
    return 0;
  }
  if (r->version == 2U && (r->decel_mm_s2 > SETTINGS_ACCEL_MAX_MM_S2 ||
                           r->vmax_kmh < SETTINGS_VMAX_MIN_KMH || r->vmax_kmh > SETTINGS_VMAX_MAX_KMH)) {
    return 0;
  }
  return (r->version == 1U || r->version == 2U) &&
         r->ppr >= SETTINGS_PPR_MIN && r->ppr <= SETTINGS_PPR_MAX &&
         r->wheel_mm >= SETTINGS_WHEEL_MIN_MM && r->wheel_mm <= SETTINGS_WHEEL_MAX_MM &&
         r->accel_mm_s2 <= SETTINGS_ACCEL_MAX_MM_S2 && r->dir <= (uint8_t)QUAD_REV;
}

static void set_defaults(rec_t *r)
{
  r->magic = REC_MAGIC;
  r->version = REC_VERSION;
  r->ppr = SETTINGS_PPR_DEFAULT;
  r->wheel_mm = SETTINGS_WHEEL_DEFAULT_MM;
  r->accel_mm_s2 = SETTINGS_ACCEL_DEFAULT_MM_S2;
  r->decel_mm_s2 = SETTINGS_DECEL_DEFAULT_MM_S2;
  r->vmax_kmh = SETTINGS_VMAX_DEFAULT_KMH;
  r->dir = (uint8_t)QUAD_FWD;
}

/* Version 1 records had one rate for both directions and no speed limit */
static void upgrade(rec_t *r)
{
  if (r->version == 1U) {
    r->version = REC_VERSION;
    r->decel_mm_s2 = r->accel_mm_s2;
    r->vmax_kmh = SETTINGS_VMAX_DEFAULT_KMH;
  }
}

void settings_init(void)
{
  set_defaults(&saved);
  saved.seq = 0;
  from_flash = 0;
  used = flashlog_used(SETTINGS_ADDR, sizeof(rec_t));
  for (uint32_t i = 0; i < used; i++) {
    rec_t r = *(const rec_t *)slot(i);

    if (valid(&r)) {
      upgrade(&r);
      saved = r;
      from_flash = 1;
    }
  }
  cur = saved;
  quad_set_dir((quad_dir_t)cur.dir);
}

int settings_from_flash(void)
{
  return from_flash;
}

int settings_dirty(void)
{
  return cur.ppr != saved.ppr || cur.wheel_mm != saved.wheel_mm ||
         cur.accel_mm_s2 != saved.accel_mm_s2 || cur.decel_mm_s2 != saved.decel_mm_s2 ||
         cur.vmax_kmh != saved.vmax_kmh || (uint8_t)quad_dir() != saved.dir;
}

uint32_t settings_slots_used(void)
{
  return used;
}

uint32_t settings_ppr(void)
{
  return cur.ppr;
}

int settings_set_ppr(uint32_t ppr)
{
  if (ppr < SETTINGS_PPR_MIN || ppr > SETTINGS_PPR_MAX) {
    return -1;
  }
  cur.ppr = (uint16_t)ppr;
  return 0;
}

uint32_t settings_wheel_mm(void)
{
  return cur.wheel_mm;
}

int settings_set_wheel_mm(uint32_t mm)
{
  if (mm < SETTINGS_WHEEL_MIN_MM || mm > SETTINGS_WHEEL_MAX_MM) {
    return -1;
  }
  cur.wheel_mm = (uint16_t)mm;
  return 0;
}

uint32_t settings_accel_mm_s2(void)
{
  return cur.accel_mm_s2;
}

int settings_set_accel_mm_s2(uint32_t a)
{
  if (a > SETTINGS_ACCEL_MAX_MM_S2) {
    return -1;
  }
  cur.accel_mm_s2 = (uint16_t)a;
  return 0;
}

uint32_t settings_decel_mm_s2(void)
{
  return cur.decel_mm_s2;
}

int settings_set_decel_mm_s2(uint32_t a)
{
  if (a > SETTINGS_ACCEL_MAX_MM_S2) {
    return -1;
  }
  cur.decel_mm_s2 = (uint16_t)a;
  return 0;
}

uint32_t settings_vmax_kmh(void)
{
  return cur.vmax_kmh;
}

int settings_set_vmax_kmh(uint32_t kmh)
{
  if (kmh < SETTINGS_VMAX_MIN_KMH || kmh > SETTINGS_VMAX_MAX_KMH) {
    return -1;
  }
  cur.vmax_kmh = (uint16_t)kmh;
  return 0;
}

double settings_m_per_pulse(void)
{
  return 3.14159265358979 * (double)cur.wheel_mm / 1000.0 / (double)cur.ppr;
}

void settings_defaults(void)
{
  set_defaults(&cur);
  quad_set_dir(QUAD_FWD);
}

int settings_save(void)
{
  rec_t r = cur;

  r.dir = (uint8_t)quad_dir();
  r.pad[0] = r.pad[1] = r.pad[2] = 0;
  r.seq = saved.seq + 1U;
  r.version = REC_VERSION;
  r.reserved = 0;
  r.crc = flashlog_crc32(&r, offsetof(rec_t, crc));

  int i = flashlog_append(SETTINGS_ADDR, sizeof(rec_t), &used, &r);

  if (i < 0) {
    return -1;
  }
  rec_t back = *(const rec_t *)slot((uint32_t)i);
  if (!valid(&back) || back.seq != r.seq) {
    used = SLOTS;                 /* force an erase on the next try */
    return -1;
  }
  saved = back;
  from_flash = 1;
  return 0;
}
