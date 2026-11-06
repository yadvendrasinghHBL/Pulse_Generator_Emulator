/*
 * Command shell on the USB serial console. Replies start with OK / ERR so
 * tools/pulse_test.py and tools/con_send.sh can tell success from failure;
 * keep these markers and the "> " prompt stable.
 *
 * New command: add a cmd_* function and a row in commands[] (last field 0 =
 * user command shown by `help`, 1 = engineering command shown by `help all`),
 * then document it in docs/USER_MANUAL.md.
 */
#include "cli.h"
#include "board.h"
#include "console.h"
#include "diag.h"
#include "fault.h"
#include "profile.h"
#include "pulse.h"
#include "quad.h"
#include "rx.h"
#include "settings.h"
#include "speed.h"
#include "version.h"

#include <stdlib.h>
#include <string.h>

#define MAX_ARGS 6

typedef struct {
  const char *name;
  const char *usage;
  void (*fn)(int argc, char **argv);
  uint8_t eng;   /* 1 = engineering command: listed only by `help all` */
} cli_cmd_t;

static void cmd_help(int argc, char **argv);
static void print_fixed(double v, unsigned dec);
static void print_speed_line(void);
static void print_settings_line(void);

/*
 * Parse a decimal number with up to `dec` decimals ("100", "0.5", "1234.567")
 * into an integer in units of 10^-dec. Digits only: no sign, no exponent, no
 * unit. 0 = OK, -1 = bad syntax or more than `max`.
 */
static int parse_fixed(const char *s, unsigned dec, uint32_t max, uint32_t *out)
{
  uint64_t v = 0;
  unsigned frac_digits = 0;
  int digits = 0;

  for (; *s >= '0' && *s <= '9'; s++, digits++) {
    v = v * 10U + (uint64_t)(*s - '0');
    if (v > max) {
      return -1;                  /* the scaled value can only be larger */
    }
  }
  if (*s == '.') {
    for (s++; *s >= '0' && *s <= '9'; s++, digits++, frac_digits++) {
      if (frac_digits == dec) {
        return -1;
      }
      v = v * 10U + (uint64_t)(*s - '0');
    }
  }
  if (*s != '\0' || digits == 0) {
    return -1;
  }
  for (; frac_digits < dec; frac_digits++) {
    v *= 10U;
  }
  if (v > max) {
    return -1;
  }
  *out = (uint32_t)v;
  return 0;
}

/* Frequency in Hz with up to 3 decimals -> mHz */
static int parse_mhz(const char *s, uint32_t *mhz)
{
  return parse_fixed(s, 3, PULSE_MAX_MHZ, mhz);
}

static void print_hz(uint32_t mhz)
{
  con_printf("%lu.%03lu Hz", mhz / 1000U, mhz % 1000U);
}

static void freq_range_error(void)
{
  con_puts("ERR frequency must be 0.01..50000 Hz, up to 3 decimals\n");
}

static void print_freq_line(void)
{
  con_puts("freq ");
  print_hz(pulse_get_mhz());
  con_puts(" (actual ");
  print_hz(pulse_actual_mhz());
  con_printf(", half period %lu ticks at 80 MHz)\n", pulse_half_period_ticks());
  con_puts("A/B actual ");
  print_hz(quad_actual_mhz());
  con_printf(" (half period %lu x %lu ticks at 80 MHz)\n", quad_counter_period(), quad_prescaler());
}

static const char *state_name(pulse_state_t s)
{
  switch (s) {
  case PULSE_RUNNING:
    return "running";
  case PULSE_STOPPED:
    return "stopped";
  default:
    return "off";
  }
}

static const char *level_name(int lvl)
{
  return lvl ? "high" : "low";
}

static void print_output_line(void)
{
  int lvl = pulse_level();
  int a = quad_level_a();

  con_printf("output %s: %s", PULSE1_NAME, state_name(pulse_state()));
  if (lvl < 0) {
    con_puts(", pin not driven\n");
  } else {
    con_printf(", level %s\n", level_name(lvl));
  }
  con_printf("outputs %s: %s, %s", QUAD_NAME, state_name(quad_state()),
             quad_dir() == QUAD_FWD ? "forward (A leads B by 90 deg)" : "reverse (B leads A by 90 deg)");
  if (a < 0) {
    con_puts(", pins not driven\n");
  } else {
    con_printf(", levels A %s, B %s\n", level_name(a), level_name(quad_level_b()));
  }
}

static void print_count(const char *label, uint64_t n)
{
  /* newlib-nano printf has no %llu: print as two 32-bit halves when needed */
  if (n <= 0xFFFFFFFFULL) {
    con_printf("%spulses %lu since start\n", label, (uint32_t)n);
  } else {
    con_printf("%spulses %lu x 10^9 + %lu since start\n", label,
               (uint32_t)(n / 1000000000ULL), (uint32_t)(n % 1000000000ULL));
  }
}

/* A manual drive command takes over from a running profile */
static void take_over(void)
{
  if (profile_abort()) {
    con_puts("(profile stopped by a manual command)\n");
  }
}

static void cmd_status(int argc, char **argv)
{
  con_stats_t st;
  int post = diag_post_result();
  uint32_t vdda = 0;
  int32_t temp = 0;
  con_get_stats(&st);
  con_printf("FW %s, up %lu s, POST %s, UART RX errors %lu\n", FW_VERSION,
             HAL_GetTick() / 1000U,
             post < 0 ? "not run" : (post == 0 ? "passed" : "failed"),
             st.rx_drops + st.ore + st.fe + st.ne);
  print_output_line();
  print_freq_line();
  print_count("", pulse_count());
  print_count("A/B ", quad_count());
  print_speed_line();
  print_settings_line();
  fault_print();
  if (diag_measure(&vdda, &temp) == 0) {
    con_printf("VDDA %lu mV, temperature %ld C\n", vdda, temp);
  } else {
    con_puts("VDDA / temperature: ADC error\n");
  }
}

static void cmd_freq(int argc, char **argv)
{
  uint32_t mhz;

  if (argc == 1) {
    print_freq_line();
    return;
  }
  if (argc != 2) {
    con_puts("ERR usage: freq | freq <Hz 0.01..50000>\n");
    return;
  }
  if (parse_mhz(argv[1], &mhz) || speed_set_mhz(mhz)) {
    freq_range_error();
    return;
  }
  con_puts("OK ");
  print_freq_line();
  take_over();
}

static void cmd_start(int argc, char **argv)
{
  uint32_t mhz;

  if (argc > 2) {
    con_puts("ERR usage: start | start <Hz 0.01..50000>\n");
    return;
  }
  if (argc == 2 && (parse_mhz(argv[1], &mhz) || speed_set_mhz(mhz))) {
    freq_range_error();
    return;
  }
  speed_start();
  con_puts("OK ");
  print_output_line();
  print_freq_line();
  take_over();
}

static void cmd_stop(int argc, char **argv)
{
  int force_low = argc == 2 && strcmp(argv[1], "low") == 0;

  if (argc > 2 || (argc == 2 && !force_low)) {
    con_puts("ERR usage: stop | stop low\n");
    return;
  }
  speed_stop(force_low);
  con_puts("OK ");
  print_output_line();
  take_over();
}

static void cmd_dir(int argc, char **argv)
{
  int fwd = argc == 2 && strcmp(argv[1], "fwd") == 0;
  int rev = argc == 2 && strcmp(argv[1], "rev") == 0;

  if (argc == 1) {
    con_printf("dir %s\n", quad_dir() == QUAD_FWD ? "fwd" : "rev");
    return;
  }
  if (!fwd && !rev) {
    con_puts("ERR usage: dir | dir fwd | dir rev\n");
    return;
  }
  if (quad_set_dir(fwd ? QUAD_FWD : QUAD_REV)) {
    con_puts("ERR stop first: a wheel changes direction only at standstill\n");
    return;
  }
  con_puts("OK ");
  print_output_line();
}

/* Ramp rates as stored (mm/s2), printed in m/s2 */
static void print_accel(void)
{
  uint32_t a = settings_accel_mm_s2();
  uint32_t d = settings_decel_mm_s2();

  con_printf("accel %lu.%03lu m/s2, decel %lu.%03lu m/s2", a / 1000U, a % 1000U, d / 1000U, d % 1000U);
}

static void print_speed_line(void)
{
  con_puts("speed ");
  print_fixed(speed_kmh(), 2);
  con_puts(" km/h, target ");
  print_fixed(speed_target_kmh(), 2);
  con_puts(" km/h, ");
  print_accel();
  con_printf(": %s, distance ", speed_ramping() ? "ramping"
             : (quad_state() == PULSE_RUNNING ? "steady" : "standstill"));
  print_fixed(speed_distance_m(), 3);
  con_puts(" m since start\nwheel ");
  print_fixed(speed_rpm(), 2);
  con_puts(" rpm, target ");
  print_fixed(speed_target_rpm(), 2);
  con_puts(" rpm\n");
}

static void print_settings_line(void)
{
  uint32_t max = speed_max_ckmh();

  con_printf("settings: %lu ppr, wheel %lu mm (", settings_ppr(), settings_wheel_mm());
  print_fixed(settings_m_per_pulse() * 1000.0, 3);
  con_printf(" mm per pulse, max %lu.%02lu km/h), ", max / 100U, max % 100U);
  print_accel();
  con_printf(", vmax %lu km/h, dir %s: %s\n", settings_vmax_kmh(), quad_dir() == QUAD_FWD ? "fwd" : "rev",
             settings_dirty() ? "not saved (type save)"
             : (settings_from_flash() ? "saved" : "defaults, nothing saved"));
}

static void stop_first(const char *what)
{
  con_printf("ERR stop first: %s only at standstill\n", what);
}

static void cmd_speed(int argc, char **argv)
{
  uint32_t ckmh;

  if (argc == 1) {
    print_speed_line();
    print_freq_line();
    return;
  }
  if (argc != 2 || parse_fixed(argv[1], 2, 99999999U, &ckmh)) {
    con_puts("ERR usage: speed | speed <km/h, up to 2 decimals>\n");
    return;
  }
  if (speed_set_target_ckmh(ckmh)) {
    uint32_t max = speed_max_ckmh();

    con_printf("ERR speed must be 0 or 0.01 Hz..max: max %lu.%02lu km/h (vmax %lu km/h, 50 kHz at %lu ppr, wheel %lu mm)\n",
               max / 100U, max % 100U, settings_vmax_kmh(), settings_ppr(), settings_wheel_mm());
    return;
  }
  con_puts("OK ");
  print_speed_line();
  print_freq_line();
  take_over();
}

/* Same as `speed`, in wheel turns per minute (rpm = f x 60 / ppr) */
static void cmd_rpm(int argc, char **argv)
{
  uint32_t crpm;

  if (argc == 1) {
    print_speed_line();
    print_freq_line();
    return;
  }
  if (argc != 2 || parse_fixed(argv[1], 2, 99999999U, &crpm)) {
    con_puts("ERR usage: rpm | rpm <wheel rpm, up to 2 decimals>\n");
    return;
  }
  if (speed_set_target_crpm(crpm)) {
    uint32_t max = speed_max_crpm();

    con_printf("ERR rpm must be 0 or 0.01 Hz..max: max %lu.%02lu rpm (vmax %lu km/h, 50 kHz at %lu ppr)\n",
               max / 100U, max % 100U, settings_vmax_kmh(), settings_ppr());
    return;
  }
  con_puts("OK ");
  print_speed_line();
  print_freq_line();
  take_over();
}

static void cmd_ppr(int argc, char **argv)
{
  uint32_t n;

  if (argc == 1) {
    print_settings_line();
    return;
  }
  if (argc != 2 || parse_fixed(argv[1], 0, SETTINGS_PPR_MAX, &n) || n < SETTINGS_PPR_MIN) {
    con_printf("ERR usage: ppr | ppr <%u..%u>\n", SETTINGS_PPR_MIN, SETTINGS_PPR_MAX);
    return;
  }
  if (speed_moving()) {
    stop_first("pulses per turn change");
    return;
  }
  settings_set_ppr(n);
  con_puts("OK ");
  print_settings_line();
}

static void cmd_wheel(int argc, char **argv)
{
  uint32_t mm;

  if (argc == 1) {
    print_settings_line();
    return;
  }
  if (argc != 2 || parse_fixed(argv[1], 0, SETTINGS_WHEEL_MAX_MM, &mm) ||
      mm < SETTINGS_WHEEL_MIN_MM) {
    con_printf("ERR usage: wheel | wheel <mm %u..%u>\n", SETTINGS_WHEEL_MIN_MM, SETTINGS_WHEEL_MAX_MM);
    return;
  }
  if (speed_moving()) {
    stop_first("the wheel diameter changes");
    return;
  }
  settings_set_wheel_mm(mm);
  con_puts("OK ");
  print_settings_line();
}

/* accel / decel: same range and format; 0 = the speed changes at once */
static void rate_cmd(int argc, char **argv, int decel)
{
  uint32_t a;

  if (argc == 1) {
    print_settings_line();
    return;
  }
  if (argc != 2 || parse_fixed(argv[1], 3, SETTINGS_ACCEL_MAX_MM_S2, &a)) {
    con_printf("ERR usage: %s | %s <m/s2 0..%u, up to 3 decimals; 0 = at once>\n",
               argv[0], argv[0], SETTINGS_ACCEL_MAX_MM_S2 / 1000U);
    return;
  }
  if (decel) {
    settings_set_decel_mm_s2(a);
  } else {
    settings_set_accel_mm_s2(a);
  }
  con_puts("OK ");
  print_settings_line();
}

static void cmd_accel(int argc, char **argv)
{
  rate_cmd(argc, argv, 0);
}

static void cmd_decel(int argc, char **argv)
{
  rate_cmd(argc, argv, 1);
}

/* Speed limit for `speed` and `rpm`; also while moving (a lower limit does not
 * slow a train already above it: the next `speed` must respect it) */
static void cmd_vmax(int argc, char **argv)
{
  uint32_t kmh;

  if (argc == 1) {
    print_settings_line();
    return;
  }
  if (argc != 2 || parse_fixed(argv[1], 0, SETTINGS_VMAX_MAX_KMH, &kmh) || settings_set_vmax_kmh(kmh)) {
    con_printf("ERR usage: vmax | vmax <km/h %u..%u>\n", SETTINGS_VMAX_MIN_KMH, SETTINGS_VMAX_MAX_KMH);
    return;
  }
  con_puts("OK ");
  print_settings_line();
}

/* Channel argument: "a" = A, "b" = B, "ab" = both (mask bits 0/1); 0 = not a channel */
static uint32_t parse_channels(const char *s)
{
  if (strcmp(s, "a") == 0) {
    return 1U;
  }
  if (strcmp(s, "b") == 0) {
    return 2U;
  }
  if (strcmp(s, "ab") == 0) {
    return 3U;
  }
  return 0U;
}

static void fault_usage(void)
{
  con_puts("ERR usage: fault | fault clear | fault <a|b> <ok|low|high> | fault phase <0..134|ok>\n"
           "  | fault glitch <a|b|ab> <1..1000 per s> [width 1..50 us] | fault glitch off\n"
           "  | fault drop <a|b|ab> <1 in 2..10000> | fault drop off\n"
           "  | fault slip <-90..+200 %> <seconds 0.1..600> | fault slip off (slip only while moving)\n");
}

/* Sensor faults on the A/B outputs, to test a receiver's fault detection */
static void cmd_fault(int argc, char **argv)
{
  uint32_t v;
  uint32_t mask;
  int ok = 0;

  if (argc == 1) {
    fault_print();
    return;
  }
  if (argc == 2 && strcmp(argv[1], "clear") == 0) {
    fault_clear();
    ok = 1;
  } else if (argc == 3 && (strcmp(argv[1], "a") == 0 || strcmp(argv[1], "b") == 0)) {
    int ch = argv[1][0] == 'b';

    if (strcmp(argv[2], "ok") == 0) {
      ok = quad_set_force(ch, QUAD_CH_OK) == 0;
    } else if (strcmp(argv[2], "low") == 0) {
      ok = quad_set_force(ch, QUAD_CH_LOW) == 0;
    } else if (strcmp(argv[2], "high") == 0) {
      ok = quad_set_force(ch, QUAD_CH_HIGH) == 0;
    }
  } else if (argc == 3 && strcmp(argv[1], "phase") == 0) {
    if (strcmp(argv[2], "ok") == 0) {
      ok = quad_set_phase(QUAD_PHASE_DEFAULT) == 0;
    } else {
      ok = parse_fixed(argv[2], 0, QUAD_PHASE_MAX, &v) == 0 && quad_set_phase(v) == 0;
    }
  } else if (argc >= 3 && strcmp(argv[1], "glitch") == 0) {
    uint32_t w = FAULT_GLITCH_WIDTH_DEFAULT;

    if (argc == 3 && strcmp(argv[2], "off") == 0) {
      ok = fault_set_glitch(0, 0, FAULT_GLITCH_WIDTH_DEFAULT) == 0;
    } else if ((argc == 4 || argc == 5) && (mask = parse_channels(argv[2])) != 0U &&
               parse_fixed(argv[3], 0, FAULT_GLITCH_MAX_PER_S, &v) == 0 && v > 0U &&
               (argc == 4 || parse_fixed(argv[4], 0, FAULT_GLITCH_WIDTH_MAX, &w) == 0)) {
      ok = fault_set_glitch(mask, v, w) == 0;
    }
  } else if (argc >= 3 && strcmp(argv[1], "drop") == 0) {
    if (argc == 3 && strcmp(argv[2], "off") == 0) {
      ok = quad_set_drop(0, 0) == 0 && quad_set_drop(1, 0) == 0;
    } else if (argc == 4 && (mask = parse_channels(argv[2])) != 0U &&
               parse_fixed(argv[3], 0, QUAD_DROP_MAX, &v) == 0 && v >= QUAD_DROP_MIN) {
      ok = 1;
      for (int ch = 0; ch < 2; ch++) {
        if (mask & (1U << ch)) {
          ok = ok && quad_set_drop(ch, v) == 0;
        }
      }
    }
  } else if (argc >= 3 && strcmp(argv[1], "slip") == 0) {
    uint32_t ds;

    if (argc == 3 && strcmp(argv[2], "off") == 0) {
      speed_slip_off();
      ok = 1;
    } else if (argc == 4) {
      const char *p = argv[2];
      int neg = *p == '-';

      if (*p == '-' || *p == '+') {
        p++;
      }
      if (parse_fixed(p, 0, 200U, &v) == 0 && v > 0U && parse_fixed(argv[3], 1, 6000U, &ds) == 0 && ds > 0U) {
        if (speed_set_slip(neg ? -(int)v : (int)v, ds * 100U)) {
          con_puts("ERR slip only while moving, -90..+200 %\n");
          return;
        }
        ok = 1;
      }
    }
  }
  if (!ok) {
    fault_usage();
    return;
  }
  con_puts("OK ");
  fault_print();
}

/* One profile step in `profile add` syntax (the list prints the same text) */
static void print_step(const profile_step_t *p)
{
  switch ((profile_op_t)p->op) {
  case PROFILE_SPEED:
    con_printf("speed %lu.%02lu", p->b / 100U, p->b % 100U);
    break;
  case PROFILE_RPM:
    con_printf("rpm %lu.%02lu", p->b / 100U, p->b % 100U);
    break;
  case PROFILE_ACCEL:
  case PROFILE_DECEL:
    con_printf("%s %lu.%03lu", p->op == PROFILE_ACCEL ? "accel" : "decel", p->b / 1000U, p->b % 1000U);
    break;
  case PROFILE_BRAKE:
    con_puts("brake");
    break;
  case PROFILE_STOP:
    con_puts("stop");
    break;
  case PROFILE_DIR:
    con_puts(p->a == (int16_t)QUAD_FWD ? "dir fwd" : "dir rev");
    break;
  case PROFILE_WAIT:
    con_printf("wait %lu.%lu", p->b / 10U, p->b % 10U);
    break;
  case PROFILE_REACH:
    con_puts("reach");
    break;
  case PROFILE_SLIP:
    con_printf("slip %s%d %lu.%lu", p->a > 0 ? "+" : "", p->a, p->b / 10U, p->b % 10U);
    break;
  default:
    con_puts("?");
    break;
  }
}

/* Parse a step from argv (argv[0] = step name). 0 = OK */
static int parse_step(int argc, char **argv, profile_step_t *p)
{
  uint32_t v;
  const char *op = argv[0];

  memset(p, 0, sizeof(*p));
  if (argc == 1) {
    if (strcmp(op, "brake") == 0) {
      p->op = PROFILE_BRAKE;
    } else if (strcmp(op, "stop") == 0) {
      p->op = PROFILE_STOP;
    } else if (strcmp(op, "reach") == 0) {
      p->op = PROFILE_REACH;
    } else {
      return -1;
    }
    return 0;
  }
  if (argc == 2) {
    if (strcmp(op, "speed") == 0 && parse_fixed(argv[1], 2, 99999999U, &v) == 0) {
      p->op = PROFILE_SPEED;
    } else if (strcmp(op, "rpm") == 0 && parse_fixed(argv[1], 2, 99999999U, &v) == 0) {
      p->op = PROFILE_RPM;
    } else if (strcmp(op, "accel") == 0 && parse_fixed(argv[1], 3, SETTINGS_ACCEL_MAX_MM_S2, &v) == 0) {
      p->op = PROFILE_ACCEL;
    } else if (strcmp(op, "decel") == 0 && parse_fixed(argv[1], 3, SETTINGS_ACCEL_MAX_MM_S2, &v) == 0) {
      p->op = PROFILE_DECEL;
    } else if (strcmp(op, "wait") == 0 && parse_fixed(argv[1], 1, PROFILE_WAIT_MAX_DS, &v) == 0 && v > 0U) {
      p->op = PROFILE_WAIT;
    } else if (strcmp(op, "dir") == 0 && (strcmp(argv[1], "fwd") == 0 || strcmp(argv[1], "rev") == 0)) {
      p->op = PROFILE_DIR;
      p->a = (int16_t)(strcmp(argv[1], "fwd") == 0 ? QUAD_FWD : QUAD_REV);
      return 0;
    } else {
      return -1;
    }
    p->b = v;
    return 0;
  }
  if (argc == 3 && strcmp(op, "slip") == 0) {
    const char *s = argv[1];
    int neg = *s == '-';
    uint32_t ds;

    if (*s == '-' || *s == '+') {
      s++;
    }
    if (parse_fixed(s, 0, (uint32_t)SPEED_SLIP_MAX, &v) || v == 0U || (neg && v > (uint32_t)-SPEED_SLIP_MIN) ||
        parse_fixed(argv[2], 1, 6000U, &ds) || ds == 0U) {
      return -1;
    }
    p->op = PROFILE_SLIP;
    p->a = (int16_t)(neg ? -(int32_t)v : (int32_t)v);
    p->b = ds;
    return 0;
  }
  return -1;
}

static void print_profile(void)
{
  static const char *const names[] = {"ready", "running", "done", "stopped", "FAILED"};
  profile_state_t st = profile_state();

  con_printf("profile: %lu of %u steps, %s, %s", profile_count(), PROFILE_MAX_STEPS,
             profile_saved() ? "saved" : "not saved (type profile save)", names[st]);
  if (st == PROFILE_RUNNING) {
    uint32_t ms = profile_wait_left_ms();

    con_printf(" step %lu", profile_current());
    if (ms) {
      con_printf(" (%lu.%lu s left)", ms / 1000U, ms % 1000U / 100U);
    }
    if (profile_passes() == 0U) {
      con_printf(", pass %lu (loop)", profile_pass());
    } else {
      con_printf(", pass %lu of %lu", profile_pass(), profile_passes());
    }
  } else if (st == PROFILE_FAILED) {
    con_printf(" at step %lu (out of range now, or dir while moving)", profile_current());
  }
  con_puts("\n");
  for (uint32_t n = 1; n <= profile_count(); n++) {
    con_printf("%c%2lu ", st == PROFILE_RUNNING && n == profile_current() ? '>' : ' ', n);
    print_step(profile_step(n));
    con_puts("\n");
  }
}

static void profile_usage(void)
{
  con_puts("ERR usage: profile | profile add <step> | profile del <n> | profile clear | profile demo\n"
           "  | profile run [times] | profile loop | profile stop | profile save\n"
           "  steps: speed <km/h> | rpm <n> | accel <m/s2> | decel <m/s2> | brake | stop | dir <fwd|rev>\n"
           "         | wait <s> | reach | slip <%> <s>\n");
}

/* Drive profiles: a stored journey the board plays back by itself */
static void cmd_profile(int argc, char **argv)
{
  uint32_t n = 0;
  const char *sub = argc > 1 ? argv[1] : "";

  if (argc == 1) {
    print_profile();
    return;
  }
  if (strcmp(sub, "add") == 0 && argc >= 3) {
    profile_step_t p;

    if (parse_step(argc - 2, &argv[2], &p)) {
      profile_usage();
      return;
    }
    if (profile_add(&p)) {
      con_printf("ERR profile full (%u steps) or running\n", PROFILE_MAX_STEPS);
      return;
    }
  } else if (strcmp(sub, "del") == 0 && argc == 3) {
    if (parse_fixed(argv[2], 0, PROFILE_MAX_STEPS, &n) || profile_del(n)) {
      con_puts("ERR no such step, or the profile is running\n");
      return;
    }
  } else if (strcmp(sub, "clear") == 0 && argc == 2) {
    if (profile_clear()) {
      con_puts("ERR profile running: profile stop first\n");
      return;
    }
  } else if (strcmp(sub, "demo") == 0 && argc == 2) {
    profile_demo();
  } else if ((strcmp(sub, "run") == 0 && (argc == 2 || argc == 3)) || (strcmp(sub, "loop") == 0 && argc == 2)) {
    n = 1;
    if (strcmp(sub, "loop") == 0) {
      n = 0;
    } else if (argc == 3 && (parse_fixed(argv[2], 0, 10000U, &n) || n == 0U)) {
      profile_usage();
      return;
    }
    if (profile_run(n)) {
      con_puts("ERR profile empty: profile add <step> or profile demo\n");
      return;
    }
  } else if (strcmp(sub, "stop") == 0 && argc == 2) {
    profile_abort();
  } else if (strcmp(sub, "save") == 0 && argc == 2) {
    if (speed_moving() || profile_state() == PROFILE_RUNNING) {
      stop_first("the profile is saved");
      return;
    }
    if (profile_save()) {
      con_puts("ERR flash write failed: profile not saved\n");
      return;
    }
  } else {
    profile_usage();
    return;
  }
  con_puts("OK ");
  print_profile();
}

/* Service brake: slow to standstill at decel, levels held at the end */
static void cmd_brake(int argc, char **argv)
{
  if (argc != 1) {
    con_puts("ERR usage: brake\n");
    return;
  }
  speed_brake();
  con_puts("OK ");
  print_speed_line();
  take_over();
}

static void cmd_save(int argc, char **argv)
{
  if (argc != 1) {
    con_puts("ERR usage: save\n");
    return;
  }
  if (speed_moving()) {
    stop_first("settings are saved");   /* a page erase stalls the CPU for about 22 ms */
    return;
  }
  if (settings_save()) {
    con_puts("ERR flash write failed: settings not saved\n");
    return;
  }
  con_printf("OK saved (record %lu of %u in the page)\n", settings_slots_used(),
             (unsigned)(SETTINGS_PAGE_BYTES / 32U));
  print_settings_line();
}

static void cmd_defaults(int argc, char **argv)
{
  if (argc != 1) {
    con_puts("ERR usage: defaults\n");
    return;
  }
  if (speed_moving()) {
    stop_first("defaults are loaded");
    return;
  }
  settings_defaults();
  con_puts("OK defaults loaded (type save to keep them)\n");
  print_settings_line();
}

/* Print v with `dec` decimals (newlib-nano printf has no float) */
static void print_fixed(double v, unsigned dec)
{
  static const uint32_t p10[] = {1U, 10U, 100U, 1000U};
  int neg = v < 0.0;

  if (neg) {
    v = -v;
  }
  if (v > 4.0e9) {
    v = 4.0e9;
  }
  uint32_t whole = (uint32_t)v;
  uint32_t frac = (uint32_t)((v - (double)whole) * p10[dec] + 0.5);

  if (frac >= p10[dec]) {
    whole++;
    frac = 0;
  }
  con_printf("%s%lu", neg && (whole || frac) ? "-" : "", whole);
  if (dec == 1U) {
    con_printf(".%01lu", frac);
  } else if (dec == 2U) {
    con_printf(".%02lu", frac);
  } else if (dec == 3U) {
    con_printf(".%03lu", frac);
  }
}

static void print_phase(const rx_values_t *v)
{
  if (v->phase_deg < 0.0) {
    con_puts("phase -");
  } else {
    con_puts("phase ");
    print_fixed(v->phase_deg, 1);
    con_printf(" deg %s", v->forward ? "forward" : "reverse");
  }
}

/* Wheel rpm from a speed, with the shared wheel diameter */
static double kmh_to_rpm(double kmh)
{
  return kmh / 3.6 / (settings_m_per_pulse() * (double)settings_ppr()) * 60.0;
}

static void cmd_rx(int argc, char **argv)
{
  rx_values_t v;

  if (argc == 2 && strcmp(argv[1], "reset") == 0) {
    rx_reset_distance();
    con_puts("OK distance and pulse count set to 0\n");
    return;
  }
  if (argc != 1) {
    con_puts("ERR usage: rx | rx reset\n");
    return;
  }
  rx_get(&v);
  con_printf("receiver %s, %lu ppr, wheel %lu mm: %s\n", RX_NAME, settings_ppr(), settings_wheel_mm(),
             rx_status_name(v.status));
  for (int n = 0; n < 2; n++) {
    con_printf("%c ", n ? 'B' : 'A');
    print_fixed(v.freq_hz[n], 3);
    con_puts(" Hz, duty ");
    print_fixed(v.duty_pct[n], 1);
    con_puts(" %\n");
  }
  print_phase(&v);
  con_puts("\nspeed ");
  print_fixed(v.speed_kmh, 2);
  con_puts(" km/h (");
  print_fixed(kmh_to_rpm(v.speed_kmh), 2);
  con_puts(" rpm), accel ");
  print_fixed(v.accel_ms2, 2);
  con_puts(" m/s2\ndistance ");
  print_fixed(v.distance_m, 3);
  con_printf(" m, pulses %ld%s, lost edges %lu\n",
             (int32_t)v.pulses, (v.pulses > INT32_MAX || v.pulses < INT32_MIN) ? " (overflow)" : "",
             v.overcaptures);
}

/* One line per update until a key is pressed; the key stays in the input, so
 * typing a command (e.g. `status` + Enter) stops the monitor and runs it. */
static void cmd_monitor(int argc, char **argv)
{
  rx_values_t v;

  if (argc != 1) {
    con_puts("ERR usage: monitor (press Enter to stop)\n");
    return;
  }
  con_printf("monitor: %s, %lu ppr, wheel %lu mm, every %u ms. Press Enter to stop.\n",
             RX_NAME, settings_ppr(), settings_wheel_mm(), RX_UPDATE_MS);
  con_puts("    km/h     rpm  m/s2      A Hz   A %      B Hz   B %  phase deg       dist m  status\n");
  rx_updated();
  uint32_t beat = HAL_GetTick();
  while (!con_line_pending()) {
    rx_poll();
    speed_poll();
    fault_poll();
    profile_poll();
    if (HAL_GetTick() - beat >= 500U) {
      beat += 500U;
      HAL_GPIO_TogglePin(LED_GREEN_PORT, LED_GREEN_PIN);
    }
    if (!rx_updated()) {
      continue;
    }
    rx_get(&v);
    con_puts(" ");
    print_fixed(v.speed_kmh, 2);
    con_puts("  ");
    print_fixed(kmh_to_rpm(v.speed_kmh), 2);
    con_puts("  ");
    print_fixed(v.accel_ms2, 2);
    for (int n = 0; n < 2; n++) {
      con_puts("  ");
      print_fixed(v.freq_hz[n], 2);
      con_puts("  ");
      print_fixed(v.duty_pct[n], 1);
    }
    con_puts("  ");
    if (v.phase_deg < 0.0) {
      con_puts("-");
    } else {
      print_fixed(v.phase_deg, 1);
      con_puts(v.forward ? " fwd" : " rev");
    }
    con_puts("  ");
    print_fixed(v.distance_m, 3);
    con_printf("  %s\n", rx_status_name(v.status));
  }
  con_puts("monitor stopped\n");
}

static void cmd_inputs(int argc, char **argv)
{
  rx_print_inputs();
}

static void cmd_reset(int argc, char **argv)
{
  con_puts("resetting\n");
  HAL_Delay(10);
  NVIC_SystemReset();
}

static void cmd_info(int argc, char **argv)
{
  diag_print_info();
}

static void cmd_post(int argc, char **argv)
{
  diag_post();
}

static void cmd_vdd(int argc, char **argv)
{
  uint32_t mv;
  int32_t t;

  if (diag_measure(&mv, &t) == 0) {
    con_printf("VDDA %lu mV, temp %ld C\n", mv, t);
  } else {
    con_puts("ERR ADC\n");
  }
}

static void cmd_led(int argc, char **argv)
{
  if (argc != 3 || (strcmp(argv[2], "on") != 0 && strcmp(argv[2], "off") != 0)) {
    con_puts("ERR usage: led <green|orange> <on|off>\n");
    return;
  }
  int on = strcmp(argv[2], "on") == 0;
  if (strcmp(argv[1], "green") == 0) {
    board_led(LED_GREEN_PORT, LED_GREEN_PIN, on);
  } else if (strcmp(argv[1], "orange") == 0) {
    board_led(LED_ORANGE_PORT, LED_ORANGE_PIN, on);
  } else {
    con_puts("ERR unknown LED\n");
    return;
  }
  con_puts("OK\n");
}

static void cmd_boot0(int argc, char **argv)
{
  con_printf("BOOT0 %d\n", diag_boot0_level());
}

/* Enter the ROM bootloader (AN2606) without touching BOOT0, so the board can be
 * reflashed over the FT231X: run this, close the terminal, then flash_uart. */
static void cmd_bootloader(int argc, char **argv)
{
  const uint32_t sysmem = 0x1FFF0000U;

  pulse_stop(0);
  quad_stop(0);
  con_puts("jumping to system bootloader on USART1\n");
  HAL_Delay(10);

  __disable_irq();
  HAL_RCC_DeInit();
  HAL_DeInit();
  /* HAL_RCC_DeInit re-arms SysTick through HAL_InitTick, so stop it only now */
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
  for (size_t i = 0; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); i++) {
    NVIC->ICER[i] = 0xFFFFFFFFU;
    NVIC->ICPR[i] = 0xFFFFFFFFU;
  }
  /* HAL_DeInit reset GPIOB to analog; keep the attenuator LE low while the bootloader runs */
  board_hold_att_latch();
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
  __enable_irq();

  __set_MSP(*(volatile uint32_t *)sysmem);
  ((void (*)(void))(*(volatile uint32_t *)(sysmem + 4U)))();
}

static const cli_cmd_t commands[] = {
  /* User commands (`help`) */
  { "help",       "help | help all: list commands",                      cmd_help,       0 },
  { "status",     "outputs, frequency, pulse counts, POST, VDDA",        cmd_status,     0 },
  { "freq",       "freq | freq <Hz>: show / set frequency, 0.01..50000 Hz", cmd_freq,    0 },
  { "start",      "start | start <Hz>: J8.1 and A/B (J8.6, J8.7)",       cmd_start,      0 },
  { "stop",       "stop | stop low: stop, hold the levels or force low", cmd_stop,       0 },
  { "dir",        "dir | dir fwd | dir rev: which channel leads (stopped)", cmd_dir,     0 },
  { "speed",      "speed | speed <km/h>: drive at a speed, ramps at accel/decel", cmd_speed, 0 },
  { "rpm",        "rpm | rpm <n>: drive at a wheel rpm, ramps at accel/decel", cmd_rpm,  0 },
  { "brake",      "brake to standstill at decel, then hold the levels",  cmd_brake,      0 },
  { "fault",      "fault | fault clear | a/b stuck, phase, glitch, drop, slip (see manual)", cmd_fault, 0 },
  { "profile",    "profile | add/del/clear/demo/run/loop/stop/save: drive profiles", cmd_profile, 0 },
  { "ppr",        "ppr | ppr <n>: pulses per turn, 1..1000 (stopped)",   cmd_ppr,        0 },
  { "wheel",      "wheel | wheel <mm>: wheel diameter, 100..2000 (stopped)", cmd_wheel,  0 },
  { "accel",      "accel | accel <m/s2>: speed-up rate, 0..50, 0 = at once", cmd_accel,  0 },
  { "decel",      "decel | decel <m/s2>: slow-down rate, 0..50, 0 = at once", cmd_decel, 0 },
  { "vmax",       "vmax | vmax <km/h>: speed limit for speed/rpm, 1..9999", cmd_vmax,   0 },
  { "save",       "save ppr, wheel, accel, decel, vmax, dir in flash (stopped)", cmd_save, 0 },
  { "defaults",   "200 ppr, 920 mm, accel/decel 0, vmax 600, fwd (stopped)", cmd_defaults, 0 },
  { "rx",         "rx | rx reset: receiver values (J8.2, J8.3) / zero distance", cmd_rx, 0 },
  { "monitor",    "receiver values every 0.5 s until Enter",             cmd_monitor,    0 },
  { "reset",      "software reset (output off until the next start)",    cmd_reset,      0 },
  /* Engineering commands (`help all`): bring-up, service, firmware update */
  { "info",       "chip ID, flash, UID, clocks, option bytes, reset",    cmd_info,       1 },
  { "post",       "run the power-on self-test again",                    cmd_post,       1 },
  { "vdd",        "measure Vdrive (VDDA) and die temperature",           cmd_vdd,        1 },
  { "led",        "led <green|orange> <on|off>",                         cmd_led,        1 },
  { "boot0",      "read the BOOT0 pin level",                            cmd_boot0,      1 },
  { "inputs",     "receiver pin levels, raw edge counts, TIM3 registers", cmd_inputs,    1 },
  { "bootloader", "jump to ROM bootloader (UART flashing)",              cmd_bootloader, 1 },
};

static void cmd_help(int argc, char **argv)
{
  int all = argc == 2 && strcmp(argv[1], "all") == 0;

  if (argc > 2 || (argc == 2 && !all)) {
    con_puts("ERR usage: help | help all\n");
    return;
  }
  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    if (commands[i].eng && !all) {
      continue;
    }
    if (commands[i].eng && (i == 0 || !commands[i - 1].eng)) {
      con_puts("Engineering commands (bring-up and service):\n");
    }
    con_printf("  %-10s %s\n", commands[i].name, commands[i].usage);
  }
  if (!all) {
    con_puts("Type 'help all' for engineering commands.\n");
  }
}

void cli_prompt(void)
{
  con_puts("> ");
}

void cli_execute(char *line)
{
  char *argv[MAX_ARGS];
  int argc = 0;

  for (char *tok = strtok(line, " \t"); tok && argc < MAX_ARGS; tok = strtok(NULL, " \t")) {
    argv[argc++] = tok;
  }
  if (argc > 0) {
    size_t i;
    for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
      if (strcmp(argv[0], commands[i].name) == 0) {
        commands[i].fn(argc, argv);
        break;
      }
    }
    if (i == sizeof(commands) / sizeof(commands[0])) {
      con_printf("ERR unknown command '%s' (try help)\n", argv[0]);
    }
  }
  cli_prompt();
}
