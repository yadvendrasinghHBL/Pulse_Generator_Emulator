# Pulse Generator Emulator: user manual (FW 0.1.0, stage 1)

## 1. What it does

The board emulates one channel of an axle-mounted **optical pulse generator** (OPG), the speed
sensor on a rail vehicle axle: a **50 % duty-cycle square wave** whose frequency stands for wheel speed.
Stage 1 drives a single output on **J8.1** (PA5) at **0.01 Hz to 50 kHz**, set from a serial terminal.
Speed/wheel/pulses-per-turn settings, several phase-shifted channels and direction come in later
stages (`PLAN.md`).

| Item | Value |
|---|---|
| Output | J8.1 (PA5, TIM2_CH1), 3.3 V push-pull, not isolated |
| Ground | **J8 has no GND pin.** Use J6 pin 2 (SWD header) |
| Frequency | 0.01 … 50 000 Hz, entered with up to 3 decimals |
| Duty cycle | 50 % (toggle mode: exact for every frequency) |
| Resolution | 12.5 ns per half period (80 MHz timer clock); e.g. 1 kHz is exact, 6 kHz is 5999.7 Hz |
| Accuracy | MCU clock is HSI16 without a crystal: about ±1 % absolute. Measure with a frequency counter if it matters |
| Power-up | output **off** (pin not driven) until `start` |

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
| `status` | firmware version, uptime, POST, output state and level, frequency, pulses since start, VDDA, temperature |
| `freq` | show the set and the actual frequency and the half period in timer ticks |
| `freq <Hz>` | set the frequency, 0.01 … 50000 Hz, up to 3 decimals (`freq 1234.567`). Applies at once, also while running, without a short pulse |
| `start` | start the output at the set frequency. Every start begins with a low half period and resets the pulse count |
| `start <Hz>` | set the frequency and start |
| `stop` | stop; the output **holds its current level** (like a stopped wheel: the code disc stays where it is) |
| `stop low` | stop and force the output low |
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
freq 1000.000 Hz (actual 1000.000 Hz, half period 40000 ticks at 80 MHz)
> freq 2500
OK freq 2500.000 Hz (actual 2500.000 Hz, half period 16000 ticks at 80 MHz)
> stop
OK output J8.1 (PA5, TIM2_CH1): stopped, level high
```

### Speed to frequency (until stage 2 does it for you)

f [Hz] = pulses per turn × v [km/h] / (3.6 × π × wheel diameter [m])

Example: 200 pulses per turn, 0.92 m wheel, 100 km/h → 200 × 100 / (3.6 × π × 0.92) = **1922.3 Hz**.

## 4. How the output behaves

- **Frequency change while running:** applied at once. If the current half period has already lasted
  longer than the new one, it ends immediately; otherwise it ends at the new length. No pulse shorter
  than the new half period is produced.
- **Very low frequencies:** at 0.01 Hz each level lasts 50 s. `freq` with a higher value takes effect
  at once, it does not wait for the long half period to finish.
- **Pulse count:** `status` counts complete pulses since the last `start` (from the timer interrupt).
- **Crash safety:** a hard fault or init error stops the timer, so a crashed board does not keep sending pulses.

## 5. Test the board

```bash
tools/pulse_test.py            # on-target checks over the console (close picocom first)
```

It checks the version, POST, rejected inputs, the timer setup, and the pulse rate at 100 Hz, 1 kHz and
10 kHz against the PC clock (±5 %). It ends with the output stopped and low. The absolute frequency
and the waveform need a scope or counter on J8.1 (ground on J6 pin 2).

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
| Nothing on J8.1 | output is off until `start`; check ground on J6.2, not on J8 |
