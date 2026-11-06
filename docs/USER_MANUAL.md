# Pulse Generator Emulator: user manual (FW 0.4.0, stages 3 and 4)

## 1. What it does

The board emulates an axle-mounted **optical pulse generator** (OPG), the speed sensor on a rail
vehicle axle: **50 % duty-cycle square waves** whose frequency stands for wheel speed, set from a serial
terminal at **0.01 Hz to 50 kHz**:

- **A and B** on **J8.6** (PB0) and **J8.7** (PB1): two channels **90° apart**, like the two tracks of the
  OPG. Forward: A leads, B follows a quarter period later. Reverse (`dir rev`): B leads.
- **J8.1** (PA5): a single output (stage 1), same frequency, not phase-locked to A/B.

- **Receiver** on **J8.2** (input A, PA6) and **J8.3** (input B, PA7): measures two A/B signals and shows
  frequency, duty, phase, direction, speed, distance, acceleration and faults (`rx`, `monitor`).
  Loopback test on one board: wire **J8.6 → J8.2** and **J8.7 → J8.3**.

- **Speed model**: `speed <km/h>` drives the outputs at a train speed, with acceleration and
  deceleration ramps (`accel`, `decel`), a speed limit (`vmax`, default 600 km/h) and `brake`. Pulses per turn (`ppr`) and wheel diameter (`wheel`) are shared by the outputs and the
  receiver; defaults **200 pulses per turn, 920 mm wheel**. `save` keeps the settings in flash.

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
| Power-up | all outputs **off** (pins not driven) until `start` or `speed`; ppr, wheel, accel, decel, vmax and direction from flash (`save`), else the defaults. Frequency 100 Hz until set: a plain `start` uses the last `freq`/`speed`/`rpm` value |
| Receiver inputs | J8.2 (PA6, TIM3_CH1), J8.3 (PA7, TIM3_CH2); **3.3 V logic only**, 40 kΩ pull-down, 100 ns filter; edge time stamps 12.5 ns |

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
| `status` | firmware version, uptime, POST, state and levels of J8.1 and A/B, direction, frequency, pulses since start (J8.1 and A), speed line, settings line, VDDA, temperature |
| `freq` | show the set frequency, the actual frequency of J8.1 and of A/B, and their half periods in timer ticks |
| `freq <Hz>` | set the frequency, 0.01 … 50000 Hz, up to 3 decimals (`freq 1234.567`). Applies at once, also while running, without a short pulse |
| `start` | start all outputs at the set frequency. Every start begins with all outputs low and resets the pulse counts; the leading channel's first edge comes 1/8 period after `start` |
| `start <Hz>` | set the frequency and start |
| `stop` | stop; the outputs **hold their current levels** (like a stopped wheel: the code disc stays where it is) |
| `stop low` | stop and force all outputs low |
| `dir` | show the direction (`fwd` or `rev`) |
| `dir fwd`, `dir rev` | forward: A leads B by 90°; reverse: B leads A. Only while stopped (`ERR stop first` otherwise): a real wheel changes direction only at standstill |
| `speed` | show the speed now, the target, accel and decel, the state (`ramping`, `steady`, `standstill`), the distance since start and the output frequency |
| `speed <km/h>` | drive at this speed, 0 … `vmax` (default 600 km/h; never above 50 kHz, which is 2601 km/h at 200 ppr, 920 mm), up to 2 decimals. Starts the outputs from standstill if needed. The speed rises at `accel` and falls at `decel` (0 = at once); `speed 0` slows to standstill and stops the outputs (levels held). See 4c |
| `brake` | slow to standstill at `decel`, then stop with the levels held (same as `speed 0`). With decel 0 it stops at once. `stop` always stops at once |
| `rpm` | same as `speed`; the second line shows the wheel rpm now and the target |
| `rpm <n>` | drive at a wheel speed in turns per minute, up to 2 decimals, like `speed` (same ramp, start and stop rules). rpm = f × 60 / ppr: 500 rpm at 200 ppr = 1666.7 Hz = 86.71 km/h with a 920 mm wheel. Maximum 50 kHz × 60 / ppr (15000 rpm at 200 ppr) |
| `ppr`, `ppr <n>` | show / set the pulses per turn, 1 … 1000. Only at standstill |
| `wheel`, `wheel <mm>` | show / set the wheel diameter, 100 … 2000 mm. Only at standstill |
| `accel`, `accel <m/s²>` | show / set the speed-up rate for `speed` and `rpm`, 0 … 50 m/s², up to 3 decimals. 0 = a higher speed applies at once. Also while moving |
| `decel`, `decel <m/s²>` | show / set the slow-down rate for `speed`, `rpm` and `brake`, 0 … 50 m/s². 0 = a lower speed applies at once. Also while moving |
| `vmax`, `vmax <km/h>` | show / set the speed limit for `speed` and `rpm`, 1 … 9999 km/h (default 600). `freq` is not limited. Also while moving: a train already above a new, lower limit keeps its speed until the next `speed` |
| `save` | write ppr, wheel, accel, decel, vmax and direction to flash (loaded at power-up). Only at standstill |
| `defaults` | 200 ppr, 920 mm, accel 0, decel 0, vmax 600 km/h, forward. Applies at once, kept only after `save`. Only at standstill |
| `rx` | receiver values once: status, frequency and duty of A and B, phase and direction, speed and wheel rpm, acceleration, distance, pulse count, lost edges |
| `rx reset` | set the receiver distance and pulse count to 0 |
| `monitor` | receiver values, one line every 0.5 s, until a key is pressed (Enter stops it; a typed command then runs) |
| `reset` | software reset; the output is off (not driven) afterwards |

Engineering commands (`help all`):

| Command | What it does |
|---|---|
| `info` | DEV_ID, flash size, UID, SYSCLK, option bytes, reset cause, BOOT0, UART error counters |
| `post` | run the power-on self-test again (ID, flash, clock, Vdrive, temperature) |
| `vdd` | measure VDDA (= Vdrive) and die temperature |
| `led <green\|orange> <on\|off>` | set an LED (green is overwritten by the heartbeat within 0.5 s) |
| `boot0` | read the BOOT0 pin level |
| `inputs` | receiver diagnostics: raw levels of PA6/PA7, rising edges counted per input, TIM3 registers (use it when an input shows 0 Hz) |
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

### Speed to frequency

f [Hz] = pulses per turn × v [km/h] / (3.6 × π × wheel diameter [m])

Example: 200 pulses per turn, 0.92 m wheel, 100 km/h → 200 × 100 / (3.6 × π × 0.92) = **1922.16 Hz**.
`speed 100` does this for you. Wheel rpm: f = rpm × pulses per turn / 60; `rpm 500` does this for you.
All parameters, their formulas and worked examples: `docs/PARAMETERS.md`.

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

## 4b. The receiver

**Values** (recalculated every 0.5 s):

| Value | How it is measured |
|---|---|
| Frequency A, B | rising edges in the last 0.5 s divided by the exact time between the last rising edges (no ±1 count error). Below 2 Hz: the last period, limited by the time since the last edge |
| Duty | high time / period of the last pulse |
| Phase | time from A rising to the next B rising, as a fraction of A's period: **90° forward, 270° reverse** |
| Direction | at each A rising edge: B low = forward (A leads), B high = reverse |
| Speed | f × π × wheel diameter / pulses per turn × 3.6; 1000 Hz = 52.02 km/h, 1922.16 Hz = 100 km/h |
| Wheel rpm | f × 60 / pulses per turn; 1000 Hz = 300 rpm at 200 ppr |
| Distance, pulses | A rising edges, + forward / − reverse; 1 pulse = 14.45 mm. `rx reset` sets them to 0 |
| Acceleration | change of speed over the last 0.5 s, in m/s² |

**Status:**

| Status | Meaning |
|---|---|
| `OK` | both inputs move, phase within **90° ± 20°** (forward) or **270° ± 20°** (reverse) |
| `standstill` | no edge on either input for **2 s**; speed shows 0 |
| `FAULT A missing` / `FAULT B missing` | one input moves, the other had no edge for 2 s (broken channel or cable) |
| `FAULT phase` | both move, but the phase is outside the window (e.g. a damaged disc or a bad receiver input) |

Example (`start 1922.16`, then `monitor`):

```
monitor: A J8.2 (PA6) / B J8.3 (PA7), TIM3, 200 ppr, wheel 920 mm, every 500 ms. Press Enter to stop.
    km/h     rpm  m/s2      A Hz   A %      B Hz   B %  phase deg       dist m  status
 100.00  576.65  0.00  1922.16  50.0  1922.16  50.0  90.0 fwd  27.780  OK
```

The receiver runs on the same clock as the outputs. In loopback it therefore reads the set frequency
exactly; against an external sensor the absolute error is that of HSI16 (about ±1 %).

## 4c. Speed model

`speed <km/h>` is the driver's lever; `freq`, `start` and `stop` stay the direct controls.

| Situation | What happens |
|---|---|
| `speed 100`, accel 0 | frequency set to 1922.16 Hz at once; outputs started if they were off or stopped |
| `speed 100`, accel 0.5 | outputs start from standstill and the speed rises by 0.5 m/s² (1.8 km/h per second) to 100 km/h; the frequency is updated every 20 ms without short pulses |
| `speed 50` while ramping or steady | ramps from the speed now to 50 km/h (up or down) |
| `rpm 500` | the same as `speed` with the target given as wheel rpm (86.71 km/h at 920 mm) |
| `speed 0` or `brake`, decel > 0 | slows at decel; at 0 the outputs stop and **hold their levels** (like a stopped wheel), state `standstill` |
| `speed 0` or `brake`, decel 0 | stops at once, levels held |
| `speed 50` while at 100 km/h | slows at **decel**; speeding up always uses **accel** |
| `freq`, `start` | set the frequency directly, no ramp; a running ramp is cancelled |
| `stop`, `stop low` | stop at once; a running ramp is cancelled |
| `accel 0` / `decel 0` during a ramp | jumps to the target speed |
| `speed 700` with vmax 600 | `ERR … max 600.00 km/h (vmax 600 km/h, …)`, nothing changes |

Below 0.01 Hz (the timer limit) a ramp holds 0.01 Hz until the speed reaches 0. **Distance** in the speed
line = A pulses since the last start × π × wheel / ppr (negative in reverse). `ppr`, `wheel`, `save` and
`defaults` answer `ERR stop first` while the outputs run or a ramp is about to start them: a real wheel
does not change size at speed, and a flash page erase stalls the CPU for about 22 ms.

```
> accel 0.5
OK settings: 200 ppr, wheel 920 mm (14.451 mm per pulse, max 600.00 km/h), accel 0.500 m/s2, decel 0.000 m/s2, vmax 600 km/h, dir fwd: not saved (type save)
> speed 100
OK speed 0.00 km/h, target 100.00 km/h, accel 0.500 m/s2, decel 0.000 m/s2: ramping, distance 0.000 m since start
wheel 0.00 rpm, target 576.65 rpm
...
> save
ERR stop first: settings are saved only at standstill
```

**Settings in flash:** `save` appends a record to the last flash page (0x0803F800); the page is erased
only after 64 saves. At power-up the last valid record is loaded (CRC checked); without one the defaults
apply. The banner shows `Settings from flash` or `Settings defaults (nothing saved)`.

## 5. Test the board

```bash
tools/pulse_test.py            # on-target checks over the console (close picocom first)
```

It checks the version, POST, rejected inputs, the timer setup, the pulse rate of J8.1 and A/B at 100 Hz,
1 kHz and 10 kHz against the PC clock (±2 %), slow-to-fast changes, `dir`, and the A/B edge order in
both directions (at 0.5 Hz, from the pin levels). With the loopback wires fitted it also checks the
receiver (frequency, duty, phase 90°/270°, speed, distance up/down, 1 Hz, standstill, `monitor`); without
them these checks are skipped. Stage 3 checks: rejected settings, `speed 100` → 1922.16 Hz, a 5 m/s² ramp
to 36 km/h and back to standstill, `ppr`/`save` refused while moving, and `save` → `reset` → settings
loaded from flash. Receiver checks also run at 10 kHz and 50 kHz (frequency, duty, phase, speed, no lost edges). The test
saves your original ppr, wheel, accel, decel, vmax and direction back at the end. It ends with all outputs stopped and low. The absolute
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
| `ERR stop first` on `dir`, `ppr`, `wheel`, `save`, `defaults` | stop the outputs (`stop` or `speed 0`), change the setting, start again |
| Receiver `FAULT A missing` / `FAULT B missing` in loopback | loose or wrong wire. `inputs`: the `edges` count of that input stays 0. Check J8.6 → J8.2 and J8.7 → J8.3 |
