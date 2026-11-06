/*
 * Sensor faults: glitch scheduling, clear-all and the status line.
 *
 * Glitches are made from the main loop, not from a timer interrupt: a glitch
 * may only be placed where no real toggle of that channel is near
 * (quad_glitch refuses otherwise), so each one is simply retried on the next
 * pass. A per-channel count of glitches due (from HAL_GetTick) keeps the
 * average rate right; a backlog longer than one second is dropped.
 */
#include "fault.h"
#include "board.h"
#include "console.h"
#include "quad.h"
#include "speed.h"

static uint32_t g_mask;
static uint32_t g_rate;          /* per second, 0 = off */
static uint32_t g_width_us = FAULT_GLITCH_WIDTH_DEFAULT;
static uint32_t g_t0;
static uint32_t g_done[2];
static uint32_t g_count[2];

int fault_set_glitch(uint32_t mask, uint32_t per_s, uint32_t width_us)
{
  if (mask > 3U || per_s > FAULT_GLITCH_MAX_PER_S || width_us < 1U || width_us > FAULT_GLITCH_WIDTH_MAX ||
      (per_s > 0U && mask == 0U)) {
    return -1;
  }
  g_mask = per_s ? mask : 0U;
  g_rate = per_s;
  g_width_us = width_us;
  g_t0 = HAL_GetTick();
  g_done[0] = g_done[1] = 0;
  g_count[0] = g_count[1] = 0;
  return 0;
}

uint32_t fault_glitches(int ch)
{
  return g_count[ch & 1];
}

void fault_poll(void)
{
  if (!g_rate || quad_state() != PULSE_RUNNING) {
    g_t0 = HAL_GetTick();         /* the count starts again when running */
    g_done[0] = g_done[1] = 0;
    return;
  }
  uint32_t due = (uint32_t)((uint64_t)(HAL_GetTick() - g_t0) * g_rate / 1000U);
  uint32_t ticks = g_width_us * (SystemCoreClock / 1000000U);

  for (int ch = 0; ch < 2; ch++) {
    if (!(g_mask & (1U << ch))) {
      continue;
    }
    if (due - g_done[ch] > g_rate) {
      g_done[ch] = due - 1U;
    }
    if (g_done[ch] < due && quad_glitch(ch, ticks) == 0) {
      g_done[ch]++;
      g_count[ch]++;
    }
  }
}

void fault_clear(void)
{
  quad_set_force(0, QUAD_CH_OK);
  quad_set_force(1, QUAD_CH_OK);
  quad_set_phase(QUAD_PHASE_DEFAULT);
  quad_set_drop(0, 0);
  quad_set_drop(1, 0);
  fault_set_glitch(0, 0, FAULT_GLITCH_WIDTH_DEFAULT);
  speed_slip_off();
}

int fault_any(void)
{
  return quad_force(0) != QUAD_CH_OK || quad_force(1) != QUAD_CH_OK ||
         quad_phase() != QUAD_PHASE_DEFAULT || quad_drop(0) || quad_drop(1) || g_rate ||
         speed_slip_pct() != 0;
}

static const char *force_name(quad_force_t f)
{
  return f == QUAD_CH_LOW ? "stuck low" : (f == QUAD_CH_HIGH ? "stuck high" : "ok");
}

void fault_print(void)
{
  if (!fault_any()) {
    con_puts("faults: none (healthy sensor)\n");
    return;
  }
  con_printf("faults: A %s, B %s, phase %lu deg", force_name(quad_force(0)), force_name(quad_force(1)),
             quad_phase());
  if (g_rate) {
    con_printf(", glitch %s%s%s %lu/s %lu us (A %lu, B %lu made)", (g_mask & 1U) ? "A" : "",
               g_mask == 3U ? "+" : "", (g_mask & 2U) ? "B" : "", g_rate, g_width_us, g_count[0], g_count[1]);
  } else {
    con_puts(", glitch off");
  }
  for (int ch = 0; ch < 2; ch++) {
    if (quad_drop(ch)) {
      con_printf(", drop %c 1 in %lu (%lu dropped)", ch ? 'B' : 'A', quad_drop(ch), quad_dropped(ch));
    }
  }
  if (!quad_drop(0) && !quad_drop(1)) {
    con_puts(", drop off");
  }
  if (speed_slip_pct() != 0) {
    uint32_t ms = speed_slip_left_ms();

    con_printf(", slip %s%d %% (%lu.%lu s left)\n", speed_slip_pct() > 0 ? "+" : "", speed_slip_pct(),
               ms / 1000U, ms % 1000U / 100U);
  } else {
    con_puts(", slip off\n");
  }
}
