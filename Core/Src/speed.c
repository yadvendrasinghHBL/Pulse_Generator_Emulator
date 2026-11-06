/*
 * Speed model: speed (m/s) -> output frequency, with acceleration ramps.
 *
 * The ramp runs in the main loop, not in an interrupt: every SPEED_STEP_MS
 * the speed moves by accel x dt toward the target and the new frequency is
 * handed to pulse/quad, which apply it at once without short pulses and
 * (A/B) without losing the 90 degrees. A step that does not change the
 * frequency (in mHz) is skipped, so a steady speed never touches the timers.
 *
 * From standstill the outputs start only at the first step, when the speed is
 * already above 0, so the first edge comes at a real (low) frequency. Below
 * 0.01 Hz (the timer limit) the frequency is held at 0.01 Hz until the speed
 * reaches 0; then the outputs stop and keep their levels, like a stopped wheel.
 */
#include "speed.h"
#include "board.h"
#include "pulse.h"
#include "quad.h"
#include "settings.h"

static double v_now;         /* m/s */
static double v_target;      /* m/s */
static int ramping;
static uint32_t last_step;

static int running(void)
{
  return quad_state() == PULSE_RUNNING;
}

/* Ramp rate toward the target: accel when speeding up, decel when slowing down */
static uint32_t rate_mm_s2(void)
{
  return v_target >= v_now ? settings_accel_mm_s2() : settings_decel_mm_s2();
}

static double mhz_exact(double v)
{
  return v / settings_m_per_pulse() * 1000.0;
}

static uint32_t mhz_for(double v)
{
  double mhz = mhz_exact(v) + 0.5;

  if (mhz < (double)PULSE_MIN_MHZ) {
    return PULSE_MIN_MHZ;
  }
  if (mhz > (double)PULSE_MAX_MHZ) {
    return PULSE_MAX_MHZ;
  }
  return (uint32_t)mhz;
}

/* Same range in both modules: pulse_set_mhz fails first, before quad changes */
static int set_outputs_mhz(uint32_t mhz)
{
  if (pulse_set_mhz(mhz)) {
    return -1;
  }
  return quad_set_mhz(mhz);
}

static void outputs_start(void)
{
  /* back to back, so J8.1 and A start within a few microseconds (not phase locked) */
  pulse_start();
  quad_start();
}

static void outputs_stop(int force_low)
{
  pulse_stop(force_low);
  quad_stop(force_low);
}

/* Move to speed v (m/s > 0): retune, and start the outputs if they stand still */
static void drive_at(double v)
{
  uint32_t mhz = mhz_for(v);

  if (mhz != pulse_get_mhz()) {
    set_outputs_mhz(mhz);
  }
  if (!running()) {
    outputs_start();
  }
}

void speed_poll(void)
{
  if (!ramping) {
    return;
  }
  uint32_t now = HAL_GetTick();
  uint32_t dt_ms = now - last_step;

  if (dt_ms < SPEED_STEP_MS) {
    return;
  }
  last_step = now;
  double dv = (double)rate_mm_s2() / 1000.0 * (double)dt_ms / 1000.0;

  if (dv <= 0.0) {
    v_now = v_target;             /* rate set to 0 during a ramp: go there at once */
  } else if (v_now < v_target) {
    v_now = v_now + dv < v_target ? v_now + dv : v_target;
  } else {
    v_now = v_now - dv > v_target ? v_now - dv : v_target;
  }
  if (v_now == v_target) {
    ramping = 0;
  }
  if (v_now <= 0.0) {
    v_now = 0.0;
    if (running()) {
      outputs_stop(0);            /* standstill: the code disc stays where it is */
    }
    return;
  }
  drive_at(v_now);
}

/* Highest speed in m/s: the vmax setting, or the 50 kHz output limit if lower */
static double v_max(void)
{
  double hw = (double)PULSE_MAX_MHZ / 1000.0 * settings_m_per_pulse();
  double lim = (double)settings_vmax_kmh() / 3.6;

  return lim < hw ? lim : hw;
}

uint32_t speed_max_ckmh(void)
{
  return (uint32_t)(v_max() * 3.6 * 100.0 + 1e-6);
}

/* Wheel circumference: one turn = ppr pulses */
static double circ_m(void)
{
  return settings_m_per_pulse() * (double)settings_ppr();
}

uint32_t speed_max_crpm(void)
{
  return (uint32_t)(v_max() / circ_m() * 60.0 * 100.0);
}

/* Target v in m/s: 0 = OK, -1 = above the limit or below 0.01 Hz (except 0).
 * The speed rises at accel and falls at decel (0 = at once). */
static int set_target(double v)
{
  if (v > v_max() + 1e-9 || (v > 0.0 && mhz_exact(v) < (double)PULSE_MIN_MHZ)) {
    return -1;
  }
  v_target = v;
  if (rate_mm_s2() > 0U && (v_now > 0.0 || v > 0.0)) {
    if (!ramping) {
      last_step = HAL_GetTick();
    }
    ramping = v_now != v_target;
    return 0;
  }
  /* rate 0, or already at standstill with target 0: apply at once */
  ramping = 0;
  v_now = v;
  if (v > 0.0) {
    drive_at(v);
  } else if (running()) {
    outputs_stop(0);
  }
  return 0;
}

int speed_set_target_ckmh(uint32_t ckmh)
{
  return set_target((double)ckmh / 360.0);  /* 0.01 km/h -> m/s */
}

int speed_set_target_crpm(uint32_t crpm)
{
  return set_target((double)crpm / 100.0 / 60.0 * circ_m());
}

void speed_brake(void)
{
  set_target(0.0);
}

double speed_rpm(void)
{
  return v_now / circ_m() * 60.0;
}

double speed_target_rpm(void)
{
  return v_target / circ_m() * 60.0;
}

double speed_kmh(void)
{
  return v_now * 3.6;
}

double speed_target_kmh(void)
{
  return v_target * 3.6;
}

int speed_ramping(void)
{
  return ramping;
}

int speed_moving(void)
{
  return ramping || running();
}

double speed_distance_m(void)
{
  double d = (double)quad_count() * settings_m_per_pulse();

  return quad_dir() == QUAD_FWD ? d : -d;
}

int speed_set_mhz(uint32_t mhz)
{
  if (set_outputs_mhz(mhz)) {
    return -1;
  }
  ramping = 0;
  if (running()) {
    v_now = (double)mhz / 1000.0 * settings_m_per_pulse();
    v_target = v_now;
  }
  return 0;
}

void speed_start(void)
{
  outputs_start();
  ramping = 0;
  v_now = (double)pulse_get_mhz() / 1000.0 * settings_m_per_pulse();
  v_target = v_now;
}

void speed_stop(int force_low)
{
  outputs_stop(force_low);
  ramping = 0;
  v_now = 0.0;
  v_target = 0.0;
}
