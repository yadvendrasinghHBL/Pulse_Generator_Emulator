# PLAN.md: Pulse Generator Emulator

## 1. Goal and scope

Emulate an axle-mounted **optical pulse generator** (OPG, e.g. HaslerRail Optical Pulse Generator,
datasheet 5.0301.035-04TEN) with the **Handoff Simulator rev A** board, using only expansion header **J8**.
The attenuator section of the board is not used.

The board has two roles at the same time (decided by the user 2026-11-05):

- **Sensor (emulator):** two output channels A and B, 50 % square waves, **90° apart**, like the OPG.
  The leading channel gives the direction.
- **Receiver (processing unit):** two input channels that measure the A/B signals and show all speed
  parameters: frequency per channel, phase A–B, direction, speed, distance, acceleration, pulse count,
  and faults (channel missing, phase outside 90° ± tolerance).

With J8.6 → J8.2 and J8.7 → J8.3 wired together, one board tests itself (loopback). A second board can
act as the receiver for the first, and the receiver can later measure a real OPG (needs the input stage, stage 6).

### Pin and timer plan (J8)

| J8 | Pin | Timer channel (AF) | Role |
|---:|-----|--------------------|------|
| 1 | PA5 | TIM2_CH1 (AF1), 32-bit | single output (stage 1, kept) |
| 2 | PA6 | TIM3_CH1 (AF2) | **input A** (input capture, both edges) |
| 3 | PA7 | TIM3_CH2 (AF2) | **input B** (input capture, both edges) |
| 6 | PB0 | TIM1_CH2N (AF1) | **output A** (toggle mode) |
| 7 | PB1 | TIM1_CH3N (AF1) | **output B** (toggle mode, compare point a quarter period after A) |

Why this split: both outputs come from **one** timer (TIM1), so the 90° phase is exact and stays exact
when the frequency changes; the direction is changed by swapping the two compare points. Both inputs go to
**another** timer (TIM3) that runs freely, so the output frequency never disturbs the measurement.
TIM3 on PB0/PB1 (the earlier plan) would put outputs and inputs on the same counter.
AF numbers are from the STM32CubeMX pin database for STM32L451RCTx (GPIO-STM32L45x); confirm against
DS11912 table 17 before coding. TIM1 is an advanced timer: the CHxN outputs need BDTR.MOE set.
All J8 pins are 3.3 V logic: **never connect a real OPG (10–30 V outputs) directly** to the inputs.

Reference OPG (datasheet §3.1 and product page): 50 % square wave (40–60 %), 1–6 isolated channels,
90° or 120° phase shift, 1–200 pulses per turn (400 on request), 1 or 2 code tracks, up to 3/6/10 kHz
(40 kHz special), outputs 0–30 V push-pull / open drain / 7–16 mA current, supply 10–30 V.

## 2. Stages

| Stage | Content | Pins | Status |
|---|---|---|---|
| 1 | Single 50 % pulse output, `freq` / `start` / `stop` / `status`, project + tools + CI | J8.1 (PA5, TIM2) | **done: v0.1.0 released 2026-11-05; automatic and scope tests pass on board 1** |
| 2 | Two-channel sensor output: A and B 50 %, B 90° behind A (forward) or ahead (reverse), `dir fwd/rev`, same `freq` / `start` / `stop` rules as stage 1 (no short pulse, off at boot) | J8.6, J8.7 (PB0, PB1, TIM1) | **done: v0.2.0 released 2026-11-05; automatic and scope tests pass on board 1** |
| 3 | Speed model on the outputs: `speed <km/h>`, `wheel <mm>`, `ppr <n>`, `accel <m/s²>` ramps, distance from pulse count, settings in flash (last page 0x0803F800) | J8.6, J8.7 | open |
| 4 | Receiver: two inputs, edge time capture → frequency A/B, duty, phase A–B, direction, speed, distance, acceleration, standstill timeout, faults; `monitor` command prints them continuously. Self-test with loopback wires | J8.2, J8.3 (PA6, PA7, TIM3) | **FW 0.3.0: automatic tests pass on board 1 (loopback); live/fault check open** |
| 5 | PC display for the receiver | PC tool | not planned (Q6: console `monitor` only) |
| 6 | Interface hardware (add-on board): isolated open-drain outputs (opto + MOSFET) and isolated, level-shifting inputs for a real 10–30 V OPG; ground from J6.2 | add-on board | open |

### Stage 1 checks (bench)

| # | Check | Result |
|---|---|---|
| 1.1 | `tools/build_all.sh`: debug + release, 0 warnings | PASS (2026-11-05) |
| 1.2 | Flash, banner, POST PASSED, `status` shows FW 0.1.0 | PASS (2026-11-05, board 1, SWD) |
| 1.3 | `tools/pulse_test.py`: ALL CHECKS PASSED | PASS (2026-11-05, board 1, 18/18) |
| 1.4 | Scope on J8.1 (GND J6.2): 1 kHz, 50 % duty, 3.3 V (procedure: `docs/MANUAL_TEST.md`) | PASS (2026-11-05, board 1, DS1054: 1000 Hz, 50 % duty; high 3.3 V with 1 kΩ load) |
| 1.5 | Scope: `freq` change while running gives no short pulse (single-shot trigger on pulse width < new half period) | PASS (2026-11-05, board 1: 1000→2500, 2500→1000, 1000→10000 Hz, width trigger never fired, control capture did) |
| 1.6 | Frequency counter: absolute error at 1 kHz (expect within ±1 %, HSI16) | PASS (2026-11-05, board 1: DS1054 counter 1000.00 Hz) |
| 1.7 | Scope: first edge after `start` (whether the timer toggles at once or after one half period) | PASS (2026-11-05, board 1: first high pulse 500 µs, full half period) |

### Stage 2 checks (bench)

| # | Check | Result |
|---|---|---|
| 2.1 | `tools/build_all.sh`: debug + release, 0 warnings | PASS (2026-11-05) |
| 2.2 | Flash, POST, `status` FW 0.2.0, all outputs off at boot | PASS (2026-11-05, board 1, UART bootloader) |
| 2.3 | `tools/pulse_test.py`: ALL CHECKS PASSED (A/B rate, `dir`, A/B order both directions) | PASS (2026-11-05, board 1, 26/26) |
| 2.4 | Scope CH1 J8.6, CH2 J8.7: both 50 %, B 90° behind A (forward), ahead (reverse) | PASS (2026-11-05, board 1, DS1054, T11 reported completed by the user; phase readings not reported) |
| 2.5 | Scope: frequency change while running keeps 90°, no short pulse on A or B | PASS (2026-11-05, board 1, DS1054, T12: width trigger < 150 µs did not fire on A or B at 1000 → 2500 Hz, control fired, phase kept, 0.05 Hz → 1 kHz at once) |

### Stage 4 checks (bench)

| # | Check | Result |
|---|---|---|
| 4.1 | `tools/build_all.sh`: debug + release, 0 warnings | PASS (2026-11-05) |
| 4.2 | Flash, POST, `status` FW 0.3.0; receiver `standstill` with open inputs (no false edges) | PASS (2026-11-05, board 1, UART bootloader) |
| 4.3 | `tools/pulse_test.py` with loopback J8.6→J8.2, J8.7→J8.3: ALL CHECKS PASSED (38) | PASS (2026-11-05, board 1) |
| 4.4 | Live `monitor`, fault by pulling a wire, standstill, reverse (`docs/MANUAL_TEST.md` T13) | open |

## 3. Decisions needed (user)

| # | Question | Options |
|---|---|---|
| Q1 | Default pulses per turn and wheel diameter for stage 2 | **decided by the user 2026-11-05: 200 ppr, 920 mm** |
| Q2 | Channels | **decided by the user 2026-11-05: 2 outputs, 90° apart, plus 2 inputs as receiver** |
| Q3 | Real equipment the board must connect to (receiver for the outputs, real OPG for the inputs) | decides the stage 6 hardware (voltage, open drain / push-pull / current loop) |
| Q5 | Stage order | **decided by the user 2026-11-05: 2 → 4 → 3** (receiver before the speed model, so the receiver can check it) |
| Q6 | Receiver display | **decided by the user 2026-11-05: console `monitor` only** (stage 5 dropped for now) |
| Q7 | Receiver fault limits | **decided by the user 2026-11-05: phase 90° ± 20° (70°–110° OK), standstill after 2 s without an edge** |
| Q4 | Frequency accuracy needed | if better than ±1 %: add a reference crystal (hardware) or calibrate HSI (MSI/LSE trim not possible: no LSE fitted) |

## 4. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | J8 has no GND pin | ground from J6.2; stage 4 board brings its own ground connection |
| R2 | HSI16 accuracy (±1 %) | state it in reports; Q4 |
| R3 | 3.3 V outputs connected straight to a 24 V receiver | USER_MANUAL warning; stage 4 output stage |
| R4 | BOOT0 / DTR issues of rev A (HARDWARE.md findings 2, 3) | DTR off in all tools; option-byte fix on each board |
| R5 | Inputs connected to a real 10–30 V OPG | USER_MANUAL warning; stage 6 input stage |
| R6 | Receiver on the same clock as the emulator (loopback) cannot see clock errors | loopback proves the logic; absolute accuracy against the scope counter or a second board (HSI16 ±1 %) |
| R7 | Interrupt load: 2 inputs × 2 edges at 10 kHz = 40 000 captures/s | TIM3 capture with DMA or a short ISR; measure CPU load in stage 4 |

## 5. Log

- 2026-11-05: Project created from `PROJECT_TEMPLATE.md` (based on the Handoff Simulator bring-up firmware).
  Stage 1 firmware v0.1.0: TIM2 toggle-mode output on PA5, console commands, POST, `pulse_test.py`,
  CI. Build-tested only (debug + release, 0 warnings); no board was connected.
- 2026-11-05: Board 1 (was running Handoff bring-up FW 0.5.0, all attenuator ports at 0 dB): flashed v0.1.0
  over SWD from `artifacts/v0.1.0-1d3b47c` with `tools/build_and_flash.sh`; `status` FW 0.1.0, POST passed,
  VDDA 3307 mV, 27 C. `tools/pulse_test.py`: ALL CHECKS PASSED (18/18). Pulse rate vs PC clock:
  98.4 / 984.6 / 9845.1 Hz at 100 / 1000 / 10000 Hz, all about 1.5 % low, the same ratio at every
  frequency. Most likely the test's timing window (it includes the serial round trips of the
  two `status` reads), possibly partly HSI16. To be settled with a frequency counter (check 1.6).
  Frequency change 0.05 Hz -> 1 kHz applied at once; stop holds the level; stop low works.
  Not verified: waveform, duty cycle and absolute frequency on J8.1 (no scope yet, checks 1.4–1.7).
  Note: the attenuators keep the values the old firmware latched (0 dB here) until the board is
  power-cycled, because this firmware never writes them.
- 2026-11-05: Manual scope test on board 1 (Rigol DS1054, `docs/MANUAL_TEST.md`), reported by the user:
  T1 output off after reset: PASS. T2 1 kHz, 50 % duty: PASS. T3 counter 1000.00 Hz: PASS (so the
  1.5 % low reading of `pulse_test.py` is its timing method, not the board; the script needs a fix).
  T4 1 Hz – 50 kHz all correct, 50 % duty: PASS. T5 Part A no short pulse on 1000→2500, 2500→1000,
  1000→10000 Hz, control capture triggered: PASS. T6 first pulse 500 µs: PASS. T7 stop holds high/low,
  `stop low`: PASS. T8 10 pulses counted, `status` 10: PASS. T9 off after `reset` and after a USB power
  cycle: PASS. T10 high level 3.3 V with 1 kΩ to GND: PASS.
  Not reported: T5 Part B by scope (0.05 Hz -> 1 kHz; already checked by `pulse_test.py`), Vmin and
  unloaded Vmax (T2).
- 2026-11-05: Plan changed (user): two-channel 90° sensor output plus a two-channel receiver that shows
  all speed parameters. New stages 2–6, pin plan TIM1_CH2N/CH3N (PB0/PB1) for outputs and
  TIM3_CH1/CH2 (PA6/PA7) for inputs; Q2 decided, Q5–Q7 added. Stage 1 unchanged.
- 2026-11-05: `tools/pulse_test.py` rate check now times the window between the two `status` samples
  (when each command reached the board), not around the replies. Board 1: 100.2 / 1001.0 / 10004.4 Hz
  at 100 / 1000 / 10000 Hz (was about 1.5 % low); tolerance tightened from 5 % to 2 %. ALL CHECKS PASSED.
- 2026-11-05: Decisions (user): stage order 2 → 4 → 3, console `monitor` only (stage 5 dropped for now),
  receiver limits 90° ± 20° and 2 s standstill.
- 2026-11-05: Stage 2 firmware 0.2.0: `quad.c`, TIM1 toggle mode on CH2N/CH3N (PB0/PB1), compare points
  N/4 and 3N/4, prescaler chosen per frequency (16-bit timer); frequency change while running restarts
  the counter at the position that keeps the edge sequence; `dir fwd|rev` while stopped. Board 1:
  SWD flash failed ("No STM32 target found", ST-LINK 3.23 V seen; probably J6 cable not on the board),
  flashed over the UART bootloader instead. `pulse_test.py` ALL CHECKS PASSED (26/26): A/B rate
  100.2 / 1000.6 / 10005.5 Hz; 0.05 Hz -> 1 kHz at once; edge order at 0.5 Hz forward
  A1B0 -> A1B1 -> A0B1, reverse A0B1 -> A1B1 -> A1B0. First run failed 2 checks because the test sampled
  on the edges (first edge is 1/8 period after `start`, not 1/4); test fixed, firmware unchanged.
  Not verified: 90° and waveform on the scope (checks 2.4, 2.5).
- 2026-11-05: Stage 2 scope test on board 1 (Rigol DS1054), reported by the user: T11 (90°, duty, direction)
  completed; T12 (frequency change keeps 90°, no short pulse on A or B, 0.05 Hz -> 1 kHz at once): all
  checks passed. Measured values of T11 (phase reading and its sign) not reported.
- 2026-11-05: v0.2.0 tagged and released (GitHub release job passed).
- 2026-11-05: Stage 4 firmware 0.3.0: `rx.c`, TIM3 input capture on both edges of PA6/PA7 (PSC 0, 16-bit
  extended by overflow count, 100 ns filter, pull-downs), values every 0.5 s in the main loop; commands
  `rx`, `rx reset`, `monitor`, `inputs`. Board 1, loopback wires: first run `FAULT A missing` (PA6 never
  changed level; `inputs` showed correct AF2/pull-down config, so a wire fault), then `FAULT B missing`
  after the A wire was refitted, then with both wires fixed `pulse_test.py` ALL CHECKS PASSED (38/38):
  A/B 1000.0 Hz, duty 50.0 %, phase 90.0° forward / 270.0° reverse, 52.02 km/h, distance +1926 pulses in
  2 s forward and falling in reverse, 1 Hz read as 1.0 Hz, standstill after stop, `monitor` OK.
  The fault detection reported both wire faults correctly.
