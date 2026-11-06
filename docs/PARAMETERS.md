# Pulse Generator Emulator: parameters and formulas (FW 0.4.0)

Every parameter of the emulator and the receiver, with its formula, a short description and an example.
Commands are described in `USER_MANUAL.md`; this file explains the numbers behind them.
Formulas use `$$ … $$` (rendered by GitHub and most markdown previews).

## 1. Basics: the numbers everything comes from

Every parameter follows from **pulses per turn (ppr)** and **wheel diameter (D)**. With the defaults
(200 ppr, 920 mm), one pulse is **14.451 mm** of track.

| Symbol | Name | Unit | Default | Command |
| --- | --- | --- | --- | --- |
| ppr | pulses per turn of the wheel | – | 200 | `ppr <n>` |
| D | wheel diameter | mm | 920 | `wheel <mm>` |
| C | wheel circumference | m | 2.8903 | – (calculated) |
| s | distance per pulse | mm | 14.451 | – (calculated) |

**Wheel circumference**: the distance the train moves in one wheel turn.

$$C = \pi \cdot D$$

Example: C = π × 0.920 m = **2.8903 m**.

**Distance per pulse**: the track length one pulse stands for. Every speed, distance and rpm calculation uses it.

$$s = \frac{C}{ppr} = \frac{\pi \cdot D}{ppr}$$

Example: s = 2.8903 m / 200 = **0.014451 m = 14.451 mm**.

## 2. Settings parameters

Four settings describe the emulated sensor and train. They apply at once. They survive a reset only
after `save`, and `defaults` restores them.

| Parameter | Command | Range | Default | Change while moving? |
| --- | --- | --- | --- | --- |
| Pulses per turn | `ppr <n>` | 1 – 1000 | 200 | no |
| Wheel diameter | `wheel <mm>` | 100 – 2000 mm | 920 mm | no |
| Acceleration (speed-up rate) | `accel <m/s²>` | 0 – 50 m/s², 3 decimals | 0 (at once) | yes |
| Deceleration (slow-down rate) | `decel <m/s²>` | 0 – 50 m/s², 3 decimals | 0 (at once) | yes |
| Speed limit | `vmax <km/h>` | 1 – 9999 km/h | 600 km/h | yes |
| Direction | `dir fwd` / `dir rev` | forward, reverse | forward | no |

### Pulses per turn (ppr)

The number of slots on the sensor's code disc: one wheel turn gives ppr pulses on each channel. It is a
property of the sensor, not a calculation. A HaslerRail OPG has 1–200 ppr (400 on request). To find it for
an unknown sensor, measure f at a known speed or rpm:

$$ppr = \frac{f \cdot C}{v} = \frac{f \cdot 60}{n_{rpm}}$$

Example: at 100 km/h (27.778 m/s) you measure f = 1922.16 Hz → ppr = 1922.16 × 2.8903 / 27.778 = **200**.

### Wheel diameter (D)

The running diameter of the wheel. It sets C and s, so every speed and distance scales with it. A worn
wheel is smaller; if the receiver still assumes the new diameter, it shows a speed that is too high:

$$\text{error} = \frac{D_{assumed}}{D_{real}} - 1$$

Example: assumed 920 mm, real 860 mm → 920 / 860 − 1 = **+7.0 %** speed and distance error.

### Acceleration (accel)

How fast `speed` and `rpm` change the speed, in m/s². **accel** is used when the speed goes up,
**decel** when it goes down (`speed` lower, `speed 0`, `brake`). 0 means the new speed applies at once.
A ramp from v₁ to v₂ (in m/s) at rate a takes:

$$t = \frac{|v_2 - v_1|}{a} \qquad d = \frac{|v_2^2 - v_1^2|}{2a}$$

Example: 0 → 100 km/h (27.778 m/s) at a = 0.5 m/s² takes 27.778 / 0.5 = **55.6 s** and covers
27.778² / (2 × 0.5) = **771.6 m**. Useful rule: 1 m/s² = 3.6 km/h per second.
At the top rate, 50 m/s² = 180 km/h per second: 0 → 600 km/h takes 3.3 s.

### Speed limit (vmax)

The highest speed `speed` and `rpm` accept (`freq` is not limited). The real limit is the lower of vmax
and the 50 kHz output limit:

$$v_{max} = \min\left(v_{vmax},\; f_{max} \cdot s \cdot 3.6\right)$$

Example: vmax 600 km/h at the defaults → max **600 km/h = 11 533 Hz = 3459.9 rpm**. `speed 600.01` is refused.

### Direction (dir)

Which channel leads. **Forward**: A leads, B rises a quarter period (90°) later. **Reverse**: B leads.
It changes only at standstill, because a real wheel cannot reverse while it is turning. No formula: it
sets the sign of the distance (+ forward, − reverse).

## 3. Output (emulator) parameters

The emulator turns a speed into a frequency on A (J8.6), B (J8.7) and J8.1. At the defaults,
**100 km/h = 1922.16 Hz = 576.65 rpm**. Shown by `speed`, `rpm`, `freq` and `status`.

### Output frequency (f)

Pulses per second on each channel. Set it directly with `freq <Hz>` (0.01 – 50 000 Hz), or let
`speed` / `rpm` calculate it:

$$f = \frac{v}{s} = \frac{ppr \cdot v_{km/h}}{3.6 \cdot \pi \cdot D} = \frac{ppr \cdot n_{rpm}}{60}$$

Example: `speed 100` → f = 200 × 100 / (3.6 × π × 0.92) = **1922.16 Hz**.
`rpm 500` → f = 200 × 500 / 60 = **1666.67 Hz**.

### Period (T) and half period

The time of one full pulse (high + low). Each level lasts half of it, because the duty is 50 %.

$$T = \frac{1}{f} \qquad t_{high} = t_{low} = \frac{T}{2}$$

Example: f = 1000 Hz → T = 1 ms, high 500 µs, low 500 µs.

### Duty cycle

High time as a share of the period. Exactly 50 % at every frequency, because the timer toggles the pin
(toggle mode).

$$duty = \frac{t_{high}}{T} \cdot 100\,\% = 50\,\%$$

### Phase A–B

The delay from A's rising edge to B's rising edge, as an angle of the period. Both channels come from one
timer, so it is exactly 90° forward, and 270° (B leads by 90°) in reverse.

$$\varphi = \frac{t_{B\uparrow} - t_{A\uparrow}}{T} \cdot 360^\circ \qquad t_{90^\circ} = \frac{T}{4}$$

Example: f = 1000 Hz → B rises **250 µs** after A (forward).

### Speed (v)

The emulated train speed. `speed <km/h>` sets the target; the speed ramps to it at accel.

$$v_{km/h} = f \cdot s \cdot 3.6 = \frac{3.6 \cdot \pi \cdot D \cdot f}{ppr}$$

Example: f = 1000 Hz → v = 1000 × 0.014451 × 3.6 = **52.02 km/h**.

### Wheel rpm (n)

Wheel turns per minute. `rpm <n>` sets the target in rpm instead of km/h, with the same ramp.

$$n_{rpm} = \frac{f \cdot 60}{ppr} = \frac{v_{m/s} \cdot 60}{\pi \cdot D}$$

Example: f = 1000 Hz → n = 1000 × 60 / 200 = **300 rpm**. 100 km/h → 27.778 × 60 / 2.8903 = **576.65 rpm**.

### Pulse count and distance

Complete pulses on A since the last start, and the track length they stand for. Negative in reverse.

$$d = \pm N \cdot s$$

Example: N = 69 198 pulses → d = 69 198 × 0.014451 m = **1000 m**. So 1 km = 69 198 pulses at the defaults.

### Limits

The highest speed and rpm are set by vmax (default 600 km/h) or, if lower, by the 50 kHz output limit
(the real OPG goes to 10 kHz):

$$v_{max} = f_{max} \cdot s \cdot 3.6 \qquad n_{max} = \frac{f_{max} \cdot 60}{ppr}$$

Example: at 200 ppr, 920 mm the 50 kHz limit is **2601.24 km/h** (520.25 km/h at 10 kHz) and
**15 000 rpm**, so with the default vmax the limit is **600 km/h = 3459.9 rpm**. Set `vmax 2601` to reach 50 kHz.

### Timer resolution (actual frequency)

The timers count an 80 MHz clock (12.5 ns), so the output frequency is rounded to whole timer counts.
J8.1 uses a 32-bit timer. A/B uses a 16-bit timer with a prescaler P, and N is a multiple of 4.

$$f_{J8.1} = \frac{80\,\text{MHz}}{2 \cdot ticks} \qquad f_{A/B} = \frac{80\,\text{MHz}}{2 \cdot N \cdot P}$$

Example: 1922.16 Hz → J8.1 20 810 ticks = 1922.15 Hz; A/B N = 20 812, P = 1 → **1921.97 Hz** (−0.01 %).
`freq` shows both actual values. The absolute accuracy is that of the HSI16 clock, about ±1 %.

## 4. Receiver parameters

The receiver measures A on J8.2 and B on J8.3 from edge time stamps (12.5 ns resolution) and recalculates
every **0.5 s**. It uses the same ppr and wheel as the emulator. Shown by `rx` and `monitor`.

### Measured frequency (f)

Rising edges in the last 0.5 s window, divided by the exact time between the last rising edge of this
window and that of the previous one. This averages all periods and has no ±1 count error. Below 2 Hz
(no edge in a window) the last period is used, limited by the time since the last edge.

$$f = \frac{N_{rises}}{t_{last\uparrow} - t_{prev\uparrow}}$$

Example: 500 rising edges, 0.500 000 s apart → f = **1000.0 Hz**.

### Measured duty

High time of the last pulse as a share of its period.

$$duty = \frac{t_{\downarrow} - t_{\uparrow}}{T} \cdot 100\,\%$$

Example: high 500 µs, period 1000 µs → **50.0 %**.

### Measured phase and direction

The time from A's rising edge to B's next rising edge, as an angle of A's period. Direction is read at
each rising edge of A: B low = A leads (forward), B high = B leads (reverse).

$$\varphi = \frac{t_{B\uparrow} - t_{A\uparrow}}{T_A} \cdot 360^\circ$$

Example: B rises 250 µs after A, T = 1 ms → φ = **90°, forward**. In reverse the same signal reads **270°**.

### Speed and wheel rpm

The same formulas as the emulator (section 3), from the measured f of A (of B if A is missing).

$$v_{km/h} = f \cdot s \cdot 3.6 \qquad n_{rpm} = \frac{f \cdot 60}{ppr}$$

Example: f = 1000 Hz → **52.02 km/h, 300 rpm**.

### Distance and pulse count

A rising edges: +1 forward, −1 reverse. `rx reset` sets both to 0.

$$d = (N_{fwd} - N_{rev}) \cdot s$$

Example: +2000 pulses → d = 2000 × 0.014451 = **28.903 m** (10 wheel turns).

### Acceleration (a)

The change of speed over the last 0.5 s window.

$$a = \frac{v_2 - v_1}{\Delta t}$$

Example: 50.00 → 50.90 km/h in 0.5 s → (0.90 / 3.6) / 0.5 = **0.50 m/s²**.

### Status and faults

| Status | Condition | Meaning |
| --- | --- | --- |
| `OK` | both inputs move, φ within 90° ± 20° (forward) or 270° ± 20° (reverse) | healthy sensor |
| `standstill` | no edge on either input for 2 s; speed shows 0 | train stopped |
| `FAULT A missing` / `FAULT B missing` | one input moves, the other had no edge for 2 s | broken channel or cable |
| `FAULT phase` | both move, φ outside the window | damaged disc or bad input |

The slowest speed not read as standstill is one edge per 2 s: f = 0.5 Hz = 0.026 km/h at the defaults.
The fastest tested is **50 kHz** (2601.24 km/h): frequency, duty 50.0 %, phase 90.0° and no lost edges
(board 1, loopback, 2026-11-06).

## 5. Worked example: one train journey

Defaults (200 ppr, 920 mm, s = 14.451 mm) and `accel 0.5`. The journey takes **171.1 s** and covers
**3209.9 m = 222 116 pulses**. With the loopback wires J8.6 → J8.2 and J8.7 → J8.3 fitted, type `monitor`
between the commands to watch the receiver follow (the next command you type stops it).

| Step | Command | Speed | f (Hz) | rpm | Time (s) | Distance (m) | Pulses |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `accel 0.5` | – | – | – | – | – | – |
| 2 | `speed 100` | 0 → 100 km/h | 0 → 1922.16 | 0 → 576.65 | 55.6 | 771.6 | 53 393 |
| 3 | (cruise 60 s) | 100 km/h | 1922.16 | 576.65 | 60.0 | 1666.7 | 115 330 |
| 4 | `speed 50` | 100 → 50 km/h | → 961.08 | → 288.32 | 27.8 | 578.7 | 40 045 |
| 5 | `speed 0` | 50 → 0 km/h | → 0 | → 0 | 27.8 | 192.9 | 13 348 |
| | **Total** | | | | **171.1** | **3209.9** | **222 116** |

How a row is calculated, for step 4 (braking from 27.778 m/s to 13.889 m/s at 0.5 m/s²):

1. Time: t = (27.778 − 13.889) / 0.5 = **27.8 s**.
2. Distance: d = (27.778² − 13.889²) / (2 × 0.5) = (771.6 − 192.9) / 1 = **578.7 m**.
3. Pulses: N = 578.7 / 0.014451 = **40 045**.
4. End frequency: f = 13.889 / 0.014451 = **961.08 Hz**.
5. End rpm: n = 961.08 × 60 / 200 = **288.32 rpm**.

After step 5 the outputs stop with their levels held and `speed` shows `standstill`. The receiver shows the
speed falling, then `standstill` and 0 km/h 2 s after the last edge. The same journey in rpm:
`rpm 576.65`, then `rpm 288.32`, then `rpm 0`.

## 6. Quick reference

Values for 200 ppr and a 920 mm wheel.

| Parameter | Formula | Unit | Value at the defaults |
| --- | --- | --- | --- |
| Circumference C | π × D | m | 2.8903 |
| Distance per pulse s | π × D / ppr | mm | 14.451 |
| Frequency f from speed | ppr × v / (3.6 × π × D) | Hz | 19.22 per km/h; 100 km/h = 1922.16 |
| Frequency f from rpm | ppr × rpm / 60 | Hz | 3.333 per rpm; 500 rpm = 1666.67 |
| Speed v from f | f × s × 3.6 | km/h | 1000 Hz = 52.02 |
| Wheel rpm from f | f × 60 / ppr | rpm | 1000 Hz = 300 |
| Wheel rpm from speed | v / (π × D) × 60 | rpm | 100 km/h = 576.65 |
| Period T | 1 / f | s | 1000 Hz = 1 ms |
| Phase delay A–B | T / 4 (90°) | s | 1000 Hz = 250 µs |
| Distance d | N × s | m | 1 km = 69 198 pulses |
| Ramp time | Δv / a | s | 0 → 100 km/h at 0.5 m/s² = 55.6 |
| Ramp distance | Δ(v²) / (2a) | m | 0 → 100 km/h at 0.5 m/s² = 771.6 |
| Max speed | min(vmax, 50 000 × s × 3.6) | km/h | 600 (vmax); 50 kHz = 2601.24; 10 kHz = 520.25 |
| Max rpm | max speed / (π × D) × 60 | rpm | 3459.9 at 600 km/h; 15 000 at 50 kHz |
| Braking time / distance | v / decel, v² / (2 × decel) | s, m | 100 km/h at decel 1 = 27.8 s, 385.8 m |
| Wheel-wear error | D_assumed / D_real − 1 | % | 920 vs 860 mm = +7.0 |
| Receiver standstill | no edge for 2 s | – | below 0.5 Hz = 0.026 km/h |
| Receiver phase OK | 90° ± 20° fwd, 270° ± 20° rev | ° | 70–110°, 250–290° |

Unit conversions: 1 m/s = 3.6 km/h; 1 m/s² = 3.6 km/h per second; clock accuracy (HSI16) about ±1 %.
