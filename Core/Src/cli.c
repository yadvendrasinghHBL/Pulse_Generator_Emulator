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
#include "pulse.h"
#include "quad.h"
#include "rx.h"
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

/*
 * Parse a frequency in Hz with up to 3 decimals ("100", "0.5", "1234.567")
 * into mHz. Digits only: no sign, no exponent, no unit. 0 = OK.
 */
static int parse_mhz(const char *s, uint32_t *mhz)
{
  uint64_t whole = 0;
  uint32_t frac = 0;
  int int_digits = 0;
  int frac_digits = 0;

  for (; *s >= '0' && *s <= '9'; s++, int_digits++) {
    whole = whole * 10U + (uint64_t)(*s - '0');
    if (whole > PULSE_MAX_MHZ / 1000U) {
      return -1;
    }
  }
  if (*s == '.') {
    for (s++; *s >= '0' && *s <= '9'; s++, frac_digits++) {
      if (frac_digits == 3) {
        return -1;
      }
      frac = frac * 10U + (uint32_t)(*s - '0');
    }
    for (int i = frac_digits; i < 3; i++) {
      frac *= 10U;
    }
  }
  if (*s != '\0' || int_digits + frac_digits == 0) {
    return -1;
  }
  *mhz = (uint32_t)(whole * 1000U + frac);
  return 0;
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

/* Apply a frequency to all outputs; both modules accept the same range */
static int set_all_mhz(uint32_t mhz)
{
  if (pulse_set_mhz(mhz)) {
    return -1;
  }
  return quad_set_mhz(mhz);
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
  if (parse_mhz(argv[1], &mhz) || set_all_mhz(mhz)) {
    freq_range_error();
    return;
  }
  con_puts("OK ");
  print_freq_line();
}

static void cmd_start(int argc, char **argv)
{
  uint32_t mhz;

  if (argc > 2) {
    con_puts("ERR usage: start | start <Hz 0.01..50000>\n");
    return;
  }
  if (argc == 2 && (parse_mhz(argv[1], &mhz) || set_all_mhz(mhz))) {
    freq_range_error();
    return;
  }
  /* back to back, so J8.1 and A start within a few microseconds (not phase locked) */
  pulse_start();
  quad_start();
  con_puts("OK ");
  print_output_line();
  print_freq_line();
}

static void cmd_stop(int argc, char **argv)
{
  int force_low = argc == 2 && strcmp(argv[1], "low") == 0;

  if (argc > 2 || (argc == 2 && !force_low)) {
    con_puts("ERR usage: stop | stop low\n");
    return;
  }
  pulse_stop(force_low);
  quad_stop(force_low);
  con_puts("OK ");
  print_output_line();
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
  con_printf("receiver %s, %u ppr, wheel %u mm: %s\n", RX_NAME, RX_PPR, RX_WHEEL_MM,
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
  con_puts(" km/h, accel ");
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
  con_printf("monitor: %s, %u ppr, wheel %u mm, every %u ms. Press Enter to stop.\n",
             RX_NAME, RX_PPR, RX_WHEEL_MM, RX_UPDATE_MS);
  con_puts("    km/h  m/s2      A Hz   A %      B Hz   B %  phase deg       dist m  status\n");
  rx_updated();
  uint32_t beat = HAL_GetTick();
  while (!con_line_pending()) {
    rx_poll();
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
