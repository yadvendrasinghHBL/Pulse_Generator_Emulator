# Pulse Generator Emulator: user manual (FW 0.2.0, stage 2)

## 1. What it does

The board emulates an axle-mounted **optical pulse generator** (OPG), the speed sensor on a rail
vehicle axle: **50 % duty-cycle square waves** whose frequency stands for wheel speed, set from a serial
terminal at **0.01 Hz to 50 kHz**:

- **A and B** on **J8.6** (PB0) and **J8.7** (PB1): two channels **90° apart**, like the two tracks of the
  OPG. Forward: A leads, B follows a quarter period later. Reverse (`dir rev`): B leads.
- **J8.1** (PA5): a single output (stage 1), same frequency, not phase-locked to A/B.

The receiver (stage 4) and the speed/wheel/pulses-per-turn settings (stage 3) come later (`PLAN.md`).

```
            |<------- 1 period = 360° ------->|
 A (J8.6) __|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|________________|‾‾‾‾   forward: A leads
 B (J8.7) ______|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|________________|   B 90° (1/4 period) later
```

| Item | Value |
|---|---|
| Outputs | A = J8.6 (PB0, TIM1_CH2N), B = J8.7 (PB1, TIM1_CH3N), J8.1 (PA5, TIM2_CH1); 3.3 V push-pull, not isolated |
| Phase A–B | 90° exactly (one timer, compare points N/4 and 3N/4 of the same counter) |
| Ground | **J8 has no GND pin.** Use J6 pin 2 (SWD header) |
| Frequency | 0.01 … 50 000 Hz, entered with up to 3 decimals |
| Duty cycle | 50 % (toggle mode: exact for every frequency) |
| Resolution | J8.1: 12.5 ns per half period (80 MHz, 32-bit timer). A/B: 16-bit timer with prescaler, half period = N × P ticks (N a multiple of 4); error below 0.01 % except at the top of the range (0.25 % at most). `freq` shows both actual values |
| Accuracy | MCU clock is HSI16 without a crystal: about ±1 % absolute. Measure with a frequency counter if it matters |
| Power-up | all outputs **off** (pins not driven) until `start`; direction forward |

> The real OPG outputs 10–30 V or a 7–16 mA current. J8 gives 3.3 V logic only: connect a scope, logic
> analyser or 3.3 V input. A train receiver needs an output stage (PLAN.md stage 4).

## 2. Connect

```bash
picocom -b 115200 --lower-dtr /dev/ttyUSB0      # exit: Ctrl+A, Ctrl+X
```

**Always open the port with DTR off** (`--lower-dtr`, pyserial `dtr=False`): on rev A boards DTR on holds
the MCU in reset (`docs/HARDWARE.md` finding 3). Windows: use a terminal where DTR can be switched off
(e.g. Tera Term with DTR unchecked), 115200 8N1.

At power-up: both LEDs on for 0.3 s, then the **green LED blinks** at 1 Hz. **Orange on** = POST failed.
After about 2.5 s the banner, chip info and POST result appear, followed by the `> ` prompt.

## 3. Commands

Every reply that changes something starts with `OK`; errors start with `ERR` and change nothing.

| Command | What it does |
|---|---|
| `help`, `help all` | list user commands; `all` adds engineering commands |
| `status` | firmware version, uptime, POST, state and levels of J8.1 and A/B, direction, frequency, pulses since start (J8.1 and A), VDDA, temperature |
| `freq` | show the set frequency, the actual frequency of J8.1 and of A/B, and their half periods in timer ticks |
| `freq <Hz>` | set the frequency, 0.01 … 50000 Hz, up to 3 decimals (`freq 1234.567`). Applies at once, also while running, without a short pulse |
| `start` | start all outputs at the set frequency. Every start begins with all outputs low and resets the pulse counts; the leading channel's first edge comes 1/8 period after `start` |
| `start <Hz>` | set the frequency and start |
| `stop` | stop; the outputs **hold their current levels** (like a stopped wheel: the code disc stays where it is) |
| `stop low` | stop and force all outputs low |
| `dir` | show the direction (`fwd` or `rev`) |
| `dir fwd`, `dir rev` | forward: A leads B by 90°; reverse: B leads A. Only while stopped (`ERR stop first` otherwise): a real wheel changes direction only at standstill |
| `reset` | software reset; the output is off (not driven) afterwards |

Engineering commands (`help all`):

| Command | What it does |
|---|---|
| `info` | DEV_ID, flash size, UID, SYSCLK, option bytes, reset cause, BOOT0, UART error counters |
| `post` | run the power-on self-test again (ID, flash, clock, Vdrive, temperature) |
| `vdd` | measure VDDA (= Vdrive) and die temperature |
| `led <green\|orange> <on\|off>` | set an LED (green is overwritten by the heartbeat within 0.5 s) |
| `boot0` | read the BOOT0 pin level |
| `bootloader` | stop the output and jump to the ROM bootloader for UART flashing |

Example:

```
> start 1000
OK output J8.1 (PA5, TIM2_CH1): running, level low
outputs A J8.6 (PB0) / B J8.7 (PB1), TIM1: running, forward (A leads B by 90 deg), levels A low, B low
freq 1000.000 Hz (actual 1000.000 Hz, half period 40000 ticks at 80 MHz)
A/B actual 1000.000 Hz (half period 40000 x 1 ticks at 80 MHz)
> stop
OK output J8.1 (PA5, TIM2_CH1): stopped, level high
outputs A J8.6 (PB0) / B J8.7 (PB1), TIM1: stopped, forward (A leads B by 90 deg), levels A high, B low
> dir rev
OK output J8.1 (PA5, TIM2_CH1): stopped, level high
outputs A J8.6 (PB0) / B J8.7 (PB1), TIM1: stopped, reverse (B leads A by 90 deg), levels A high, B low
```

### Speed to frequency (until stage 2 does it for you)

f [Hz] = pulses per turn × v [km/h] / (3.6 × π × wheel diameter [m])

Example: 200 pulses per turn, 0.92 m wheel, 100 km/h → 200 × 100 / (3.6 × π × 0.92) = **1922.3 Hz**.

## 4. How the output behaves

- **Frequency change while running:** applied at once. If the current half period has already lasted
  longer than the new one, it ends immediately; otherwise it ends at the new length. No pulse shorter
  than the new half period is produced.
- **A/B on a frequency change:** the next A or B edge comes a new quarter period after the last edge
  (or at once if that time has passed). The A/B order and the 90° are kept; no edge is lost or added,
  and no level is shorter than the shorter of the old and new half periods.
- **Very low frequencies:** at 0.01 Hz each level lasts 50 s. `freq` with a higher value takes effect
  at once, it does not wait for the long half period to finish.
- **Pulse count:** `status` counts complete pulses since the last `start` (from the timer interrupt).
- **Crash safety:** a hard fault or init error stops the timer, so a crashed board does not keep sending pulses.

## 5. Test the board

```bash
tools/pulse_test.py            # on-target checks over the console (close picocom first)
```

It checks the version, POST, rejected inputs, the timer setup, the pulse rate of J8.1 and A/B at 100 Hz,
1 kHz and 10 kHz against the PC clock (±2 %), slow-to-fast changes, `dir`, and the A/B edge order in
both directions (at 0.5 Hz, from the pin levels). It ends with all outputs stopped and low. The absolute
frequency, the 90° phase and the waveform need a scope (`docs/MANUAL_TEST.md`; ground on J6 pin 2).

## 6. Firmware update

```bash
tools/build_and_flash.sh       # clean build, artifacts/, flash (ST-LINK on J6 if present, else UART), version check
```

Without an ST-LINK the script sends `bootloader` and flashes over the USB serial port with DTR low.
Manual: `cmake --build --preset release --target flash` (SWD) or `tools/con_send.sh bootloader` then
`cmake --build --preset release --target flash_uart`.

## 7. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| No banner, green LED off while the terminal is open | terminal asserts DTR → MCU held in reset. Use `--lower-dtr` |
| Console silent after a reset with the terminal closed | BOOT0 high → ROM bootloader (HARDWARE.md finding 2). Power-cycle, or apply the option-byte fix |
| `ERR port busy` from the tools | close picocom: one program per port |
| Frequency about 1 % off | expected: no crystal on the board (HSI16) |
| Nothing on J8.1, J8.6 or J8.7 | outputs are off until `start`; check ground on J6.2, not on J8 |
| `ERR stop first` on `dir` | stop the outputs, change the direction, start again |
