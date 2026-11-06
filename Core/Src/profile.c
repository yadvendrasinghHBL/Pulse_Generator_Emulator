/*
 * Drive profile player and its flash storage.
 *
 * Player: profile_poll() runs from the main loop. It executes steps one after
 * the other until a step has to wait (`wait`, `reach`), so a profile never
 * blocks the console. A step that fails (speed above vmax, `dir` while
 * moving) stops the profile and is shown by `profile`.
 *
 * Storage: the same scheme as settings.c, in its own page (0x0803F000): each
 * `profile save` appends one record (header + 32 steps + CRC-32, 272 bytes, a
 * multiple of 8) to the next free slot; the page is erased only when its 7
 * slots are used. At boot the last valid record wins.
 */
#include "profile.h"
#include "board.h"
#include "quad.h"
#include "settings.h"
#include "speed.h"

#include <stddef.h>
#include <string.h>

#define REC_MAGIC     0x31504750U    /* "PGP1" */
#define REC_VERSION   1U
#define PAGE_BYTES    2048U
#define SLOTS         (PAGE_BYTES / sizeof(rec_t))

typedef struct {
  uint32_t       magic;
  uint16_t       version;
  uint16_t       count;
  uint32_t       seq;
  profile_step_t steps[PROFILE_MAX_STEPS];
  uint32_t       crc;          /* CRC-32 of all bytes before it */
} rec_t;

_Static_assert(sizeof(rec_t) % 8U == 0U, "record must be whole double words");

static profile_step_t steps[PROFILE_MAX_STEPS];
static uint32_t count;
static int saved;                /* RAM equals the flash record */
static uint32_t seq;
static uint32_t used;            /* slots in use */

static profile_state_t state;
static uint32_t cur;             /* 0-based step running */
static uint32_t pass;
static uint32_t passes;          /* 0 = loop */
static uint32_t wait_start;
static int waiting;

static const profile_step_t demo[] = {
  /* 0 -> 100 km/h at 1 m/s2 (27.8 s), 30 s at 100, down to 50 at 0.8 m/s2, 20 s, brake at 1 m/s2 */
  { PROFILE_ACCEL, 0, 0, 1000U },
  { PROFILE_DECEL, 0, 0, 800U },
  { PROFILE_SPEED, 0, 0, 10000U },
  { PROFILE_REACH, 0, 0, 0U },
  { PROFILE_WAIT,  0, 0, 300U },
  { PROFILE_SPEED, 0, 0, 5000U },
  { PROFILE_REACH, 0, 0, 0U },
  { PROFILE_WAIT,  0, 0, 200U },
  { PROFILE_DECEL, 0, 0, 1000U },
  { PROFILE_BRAKE, 0, 0, 0U },
  { PROFILE_REACH, 0, 0, 0U },
};

static const rec_t *slot(uint32_t i)
{
  return (const rec_t *)(PROFILE_ADDR + i * sizeof(rec_t));
}

static uint32_t crc32(const void *data, size_t len)
{
  const uint8_t *p = data;
  uint32_t crc = 0xFFFFFFFFU;

  while (len--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) {
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

static int erased(uint32_t i)
{
  const volatile uint32_t *w = (const volatile uint32_t *)slot(i);

  for (size_t k = 0; k < sizeof(rec_t) / 4U; k++) {
    if (w[k] != 0xFFFFFFFFU) {
      return 0;
    }
  }
  return 1;
}

static int valid(const rec_t *r)
{
  return r->magic == REC_MAGIC && r->version == REC_VERSION && r->count <= PROFILE_MAX_STEPS &&
         r->crc == crc32(r, offsetof(rec_t, crc));
}

/* Any change to the list: the player starts again from the beginning */
static void changed(void)
{
  saved = 0;
  state = PROFILE_IDLE;
}

void profile_init(void)
{
  for (uint32_t i = 0; i < SLOTS; i++) {
    if (erased(i)) {
      break;
    }
    used = i + 1U;
    if (valid(slot(i))) {
      count = slot(i)->count;
      seq = slot(i)->seq;
      memcpy(steps, slot(i)->steps, sizeof(steps));
      saved = 1;
    }
  }
}

int profile_add(const profile_step_t *s)
{
  if (count >= PROFILE_MAX_STEPS || state == PROFILE_RUNNING) {
    return -1;
  }
  steps[count++] = *s;
  changed();
  return 0;
}

int profile_del(uint32_t n)
{
  if (n < 1U || n > count || state == PROFILE_RUNNING) {
    return -1;
  }
  memmove(&steps[n - 1U], &steps[n], (count - n) * sizeof(steps[0]));
  count--;
  changed();
  return 0;
}

int profile_clear(void)
{
  if (state == PROFILE_RUNNING) {
    return -1;
  }
  count = 0;
  changed();
  return 0;
}

void profile_demo(void)
{
  profile_abort();
  memcpy(steps, demo, sizeof(demo));
  count = sizeof(demo) / sizeof(demo[0]);
  changed();
}

uint32_t profile_count(void)
{
  return count;
}

const profile_step_t *profile_step(uint32_t n)
{
  return n >= 1U && n <= count ? &steps[n - 1U] : NULL;
}

int profile_run(uint32_t n)
{
  if (count == 0U) {
    return -1;
  }
  passes = n;
  pass = 1;
  cur = 0;
  waiting = 0;
  state = PROFILE_RUNNING;
  profile_poll();                 /* the first steps start at once */
  return 0;
}

int profile_abort(void)
{
  if (state != PROFILE_RUNNING) {
    return 0;
  }
  state = PROFILE_STOPPED;
  return 1;
}

/* Run step s. 1 = done, go on; 0 = still waiting; -1 = failed */
static int exec(const profile_step_t *s)
{
  switch ((profile_op_t)s->op) {
  case PROFILE_SPEED:
    return speed_set_target_ckmh(s->b) ? -1 : 1;
  case PROFILE_RPM:
    return speed_set_target_crpm(s->b) ? -1 : 1;
  case PROFILE_ACCEL:
    return settings_set_accel_mm_s2(s->b) ? -1 : 1;
  case PROFILE_DECEL:
    return settings_set_decel_mm_s2(s->b) ? -1 : 1;
  case PROFILE_BRAKE:
    speed_brake();
    return 1;
  case PROFILE_STOP:
    speed_stop(0);
    return 1;
  case PROFILE_DIR:
    return speed_moving() || quad_set_dir((quad_dir_t)s->a) ? -1 : 1;
  case PROFILE_SLIP:
    return speed_set_slip(s->a, s->b * 100U) ? -1 : 1;
  case PROFILE_WAIT:
    if (!waiting) {
      waiting = 1;
      wait_start = HAL_GetTick();
    }
    return HAL_GetTick() - wait_start >= s->b * 100U;
  case PROFILE_REACH:
    return !speed_ramping();
  default:
    return -1;
  }
}

void profile_poll(void)
{
  /* At most one full pass per call, so a profile of only quick steps in a loop
   * cannot hold the main loop */
  for (uint32_t n = 0; state == PROFILE_RUNNING && n <= count; n++) {
    int r = exec(&steps[cur]);

    if (r < 0) {
      state = PROFILE_FAILED;
      return;
    }
    if (r == 0) {
      return;
    }
    waiting = 0;
    if (++cur >= count) {
      cur = 0;
      if (passes != 0U && pass >= passes) {
        cur = count - 1U;
        state = PROFILE_DONE;
        return;
      }
      pass++;
    }
  }
}

profile_state_t profile_state(void)
{
  return state;
}

uint32_t profile_current(void)
{
  return cur + 1U;
}

uint32_t profile_pass(void)
{
  return pass;
}

uint32_t profile_passes(void)
{
  return passes;
}

uint32_t profile_wait_left_ms(void)
{
  if (state != PROFILE_RUNNING || !waiting || steps[cur].op != PROFILE_WAIT) {
    return 0;
  }
  uint32_t el = HAL_GetTick() - wait_start;
  uint32_t total = steps[cur].b * 100U;

  return el < total ? total - el : 0U;
}

int profile_saved(void)
{
  return saved;
}

int profile_save(void)
{
  static rec_t r;                 /* 272 bytes: not on the stack */
  uint32_t i = used;
  const uint64_t *dw = (const uint64_t *)(const void *)&r;
  int err = 0;

  memset(&r, 0, sizeof(r));
  r.magic = REC_MAGIC;
  r.version = REC_VERSION;
  r.count = (uint16_t)count;
  r.seq = seq + 1U;
  memcpy(r.steps, steps, count * sizeof(steps[0]));
  r.crc = crc32(&r, offsetof(rec_t, crc));

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS | FLASH_FLAG_OPTVERR);
  if (i >= SLOTS || !erased(i)) {
    FLASH_EraseInitTypeDef e = {0};
    uint32_t page_err = 0;

    e.TypeErase = FLASH_TYPEERASE_PAGES;
    e.Banks = FLASH_BANK_1;
    e.Page = (PROFILE_ADDR - FLASH_BASE) / FLASH_PAGE_SIZE;
    e.NbPages = 1;
    err = HAL_FLASHEx_Erase(&e, &page_err) != HAL_OK;
    i = 0;
  }
  for (size_t k = 0; !err && k < sizeof(rec_t) / 8U; k++) {
    err = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (uint32_t)slot(i) + 8U * k, dw[k]) != HAL_OK;
  }
  HAL_FLASH_Lock();

  if (err || !valid(slot(i)) || slot(i)->seq != r.seq) {
    used = SLOTS;                 /* force an erase on the next try */
    return -1;
  }
  seq = r.seq;
  used = i + 1U;
  saved = 1;
  return 0;
}
