# Manual test: stage 1 (FW 0.1.0), pulse output on J8.1

This procedure checks by hand what `tools/pulse_test.py` cannot check: the real waveform on the pin
(voltage, duty cycle, exact frequency, edges). It covers checks 1.4–1.7 in `PLAN.md`.
It takes about 30–45 minutes. Fill in the **Measured** columns as you go and keep the sheet.

---

## 1. What you need

| Item | Notes |
|---|---|
| Board with FW 0.1.0 | `status` must show `FW 0.1.0` |
| USB cable + PC | the board is powered from USB (J7) |
| Serial terminal | Linux: `picocom`; Windows: Tera Term (DTR **off**) |
| Oscilloscope | any scope with ≥ 20 MHz bandwidth; 10:1 probe |
| Frequency counter (optional) | or the scope's frequency measurement; a multimeter with Hz range also works |
| 2 short jumper wires | for the probe tip and ground |

**Do not connect anything else to J8** during the test. The output is 3.3 V logic and is not protected.

## 2. Where to connect

| Signal | Connector pin | Notes |
|---|---|---|
| Pulse output | **J8 pin 1** (PA5) | pin 1 is normally the square pad / the pin next to the "1" or triangle mark on the silkscreen; check on your board |
| Ground | **J6 pin 2** (SWD header) | **J8 has no ground pin.** J6: 1 SWCLK, 2 GND, 3 SWDIO, 4 NRST |

```
 scope probe tip  ──►  J8 pin 1
 scope ground clip ──►  J6 pin 2  (GND)
```

You can leave the ST-LINK plugged into J6 while testing; it shares the same ground.

## 3. Open the console

1. Plug in the USB cable. Both LEDs light for 0.3 s, then the **green LED blinks** about once per second.
2. Open the terminal **with DTR off**. If DTR is on, the board is held in reset and does not answer:

   ```bash
   picocom -b 115200 --lower-dtr /dev/ttyUSB0
   ```

   Windows (Tera Term): Setup → Serial port → 115200, 8N1; then make sure DTR is **not** asserted.
3. Press **Enter**. You should see the prompt `> `.
4. Type `status` and press Enter. Check the first two lines:

   ```
   FW 0.1.0, up ... s, POST passed, UART RX errors 0
   output J8.1 (PA5, TIM2_CH1): off, pin not driven
   ```

5. To leave picocom later: **Ctrl+A**, then **Ctrl+X**.

> Only one program can use the serial port. Close picocom before you run `tools/pulse_test.py`
> or `tools/con_send.sh`.

## 4. Scope starting settings

| Setting | Value |
|---|---|
| Channel 1 | DC coupling, **1 V/div**, probe set to 10:1 |
| Trigger | edge, **rising**, level **1.6 V**, mode **Auto** (Normal/Single where stated) |
| Timebase | given per test |
| Measurements | turn on **Frequency**, **Period**, **+Width**, **Duty**, **Vmax**, **Vmin** |

---

## 5. Tests

### T1: Output is off after reset (nothing driven at power-up)

1. Type `reset`. Wait about 3 s until the banner and the prompt appear.
2. Type `status`. Expected: `output J8.1 (PA5, TIM2_CH1): off, pin not driven`.
3. Scope: no square wave on J8.1. The line is not driven, so it may sit near 0 V or drift slowly.

| Check | Expected | Measured | Pass? |
|---|---|---|---|
| status after reset | `off, pin not driven` | | |
| scope | no pulses | | |

### T2: Basic waveform at 1 kHz (PLAN check 1.4)

1. Scope timebase **200 µs/div**.
2. Type `start 1000`. Reply: `OK output ... running` and `freq 1000.000 Hz (actual 1000.000 Hz ...)`.
3. Read the scope measurements.

| Check | Expected | Measured | Pass? |
|---|---|---|---|
| Frequency | 1000 Hz ± 1 % (990–1010 Hz) | | |
| Period | 1.000 ms ± 1 % | | |
| High time (+Width) | 500 µs | | |
| Duty cycle | 50 % (49–51 %) | | |
| High level (Vmax) | about 3.3 V (≥ 3.0 V) | | |
| Low level (Vmin) | about 0 V (≤ 0.3 V) | | |
| Edges | clean, no ringing larger than about 0.5 V | | |

Why ±1 %: the board has no crystal; the MCU clock (HSI16) is accurate to about ±1 %.
The duty cycle does not depend on the clock and should be 50 % exactly.

### T3: Exact frequency (PLAN check 1.6)

1. Keep `start 1000` running.
2. Measure the frequency with the counter (or the scope's frequency measurement with a long timebase,
   for example 10 ms/div, for a stable reading).
3. Error in % = (measured − 1000) / 1000 × 100.

| Check | Expected | Measured | Error % | Pass? |
|---|---|---|---|---|
| Frequency at 1 kHz | within ±1 % | | | |

This also answers the open question from the automatic test (it read 1.5 % low against the PC clock).
If the counter shows about 1000 Hz, the difference was the test's timing method, not the board.

### T4: Frequency range

For each row: type the command, set the timebase so that you see 2–5 periods, and read frequency and duty.
The last column shows which train speed this frequency means with **200 pulses per turn and a 920 mm wheel**
(f = speed in km/h × 19.22), the stage 2 defaults.

| Command | Timebase | Expected f | Measured f | Duty | Pass? | Train speed |
|---|---|---|---|---|---|---|
| `freq 1` | 500 ms/div (or roll mode) | 1 Hz | | | | 0.05 km/h |
| `freq 10` | 50 ms/div | 10 Hz | | | | 0.5 km/h |
| `freq 192.2` | 2 ms/div | 192.2 Hz | | | | 10 km/h |
| `freq 1922.3` | 200 µs/div | 1922.3 Hz | | | | 100 km/h |
| `freq 4805.7` | 100 µs/div | 4805.7 Hz | | | | 250 km/h |
| `freq 6000` | 50 µs/div | 5999.7 Hz (firmware shows the actual value) | | | | 312 km/h |
| `freq 10000` | 50 µs/div | 10 kHz | | | | 520 km/h |
| `freq 50000` | 10 µs/div | 50 kHz | | | | (max of firmware) |

Pass: frequency within ±1 %, duty 49–51 %. At 50 kHz also check that the edges are still clean.

### T5: Frequency change while running, no short pulse (PLAN check 1.5)

A real sensor never produces a pulse shorter than the current speed allows, so the emulator must not either.

**Part A: fast change (1000 → 2500 Hz).** The new half period is 200 µs; nothing shorter may appear.

1. Type `freq 1000` (output keeps running).
2. Scope trigger: **pulse width**, positive or negative pulse, **less than 150 µs**, mode **Single**.
   (If your scope has no pulse-width trigger, use edge trigger in Single mode with 1 ms/div and look
   at the captured screen around the change.)
3. Arm the scope (Single), then type `freq 2500`.
4. Expected: the scope does **not** trigger (no pulse narrower than 150 µs). The waveform changes
   from 1 kHz to 2.5 kHz.
5. Repeat with `freq 1000` (slowing down) and `freq 10000` (speeding up; then use a trigger < 40 µs).

**Part B: slow to fast (no waiting).**

1. Type `start 0.05`. The output changes level only every 10 s.
2. After 2–3 s type `freq 1000`.
3. Expected: the 1 kHz square wave appears **at once** (within a fraction of a second), not after the 10 s level.

| Check | Expected | Measured | Pass? |
|---|---|---|---|
| 1000 → 2500 Hz | no trigger on width < 150 µs | | |
| 2500 → 1000 Hz | no trigger on width < 150 µs | | |
| 1000 → 10000 Hz | no trigger on width < 40 µs | | |
| 0.05 → 1000 Hz | 1 kHz starts at once | | |

### T6: First pulse after start (PLAN check 1.7)

1. Type `stop low`. The output goes to 0 V.
2. Scope: edge trigger **rising**, **Single**, timebase **200 µs/div**, trigger point at the left of the screen.
3. Arm the scope, then type `start 1000`.
4. Look at the first high pulse after the trigger.

| Check | Expected | Measured | Pass? |
|---|---|---|---|
| Width of the first high pulse | 500 µs (full half period, no short first pulse) | | |
| Following pulses | regular 1 kHz | | |

### T7: Stop holds the level, stop low

1. Type `start 0.5`. The output changes level once per second (watch on the scope in roll mode, or
   with a multimeter on DC volts: 0 V ↔ 3.3 V).
2. Type `stop` while the output is **high**. Expected: it **stays high** (3.3 V), and `status` shows
   `stopped, level high`.
3. Type `start 0.5`, then `stop` while it is **low**. Expected: it stays low, `status` shows `level low`.
4. Type `stop low` after a high stop. Expected: 0 V and `level low`.

| Check | Expected | Measured | Pass? |
|---|---|---|---|
| stop while high | stays 3.3 V, status `level high` | | |
| stop while low | stays 0 V, status `level low` | | |
| stop low | 0 V, status `level low` | | |

### T8: Pulse counter

1. Type `start 1`. Count the rising edges on the scope (roll mode) or the multimeter for **about 10 s**.
2. Type `stop`, then `status`.
3. Expected: `pulses N since start` matches your count (±1, because of when you started and stopped counting).

| Check | Expected | Measured | Pass? |
|---|---|---|---|
| pulses after about 10 s at 1 Hz | your count ± 1 | | |

### T9: Reset and power-up

1. Type `start 1000`, then `reset`. Expected: the pulses stop and J8.1 is not driven; `status` shows `off`.
2. Unplug USB, wait 5 s, plug it in again. Expected: green LED blinks, J8.1 has no pulses until `start`.

| Check | Expected | Measured | Pass? |
|---|---|---|---|
| after `reset` | output off | | |
| after power cycle | output off, green LED blinks | | |

### T10 (optional): Output under load

1. Type `start 1000`. Connect a **1 kΩ** resistor from J8.1 to GND (J6 pin 2).
2. Expected: high level still ≥ 3.0 V (about 3.3 mA load).
3. Remove the resistor afterwards.

| Check | Expected | Measured | Pass? |
|---|---|---|---|
| high level with 1 kΩ load | ≥ 3.0 V | | |

---

## 6. Finish

1. Type `stop low` so the output is left at 0 V.
2. Close the terminal (Ctrl+A, Ctrl+X).
3. Send the filled-in tables (a photo is fine) and, if possible, scope screenshots of T2 and T5.
   The results go into `PLAN.md` (checks 1.4–1.7). If everything passes, v0.1.0 is tagged and stage 2 starts.

## 7. If something goes wrong

| What you see | Likely cause | What to do |
|---|---|---|
| No prompt, green LED stops while the terminal is open | DTR is on, the MCU is held in reset | reopen with `--lower-dtr` (Tera Term: DTR off) |
| `picocom: ... Device or resource busy` | another program has the port | close it (only one program per port) |
| Flat line on the scope | output is off, wrong pin, or no ground | `status`; check J8 pin 1 and ground on J6 pin 2 |
| Amplitude about 0.33 V or 33 V | probe and scope set to different ratios (1:1 vs 10:1) | set both to 10:1 |
| Frequency about 1 % off | expected (no crystal) | note the value; more than 2 % is a fail |
| Unstable picture | trigger level outside 0–3.3 V | trigger level 1.6 V, rising edge |
