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
| 3 | Speed model on the outputs: `speed <km/h>`, `wheel <mm>`, `ppr <n>`, `accel <m/s²>` ramps, distance from pulse count, settings in flash (last page 0x0803F800) | J8.6, J8.7 | **FW 0.4.0: automatic tests pass on board 1 (68/68 with loopback); scope check 3.4 open** (decided by the user 2026-11-06: ramp to target, explicit `save`, settings shared with the receiver) |
| 4 | Receiver: two inputs, edge time capture → frequency A/B, duty, phase A–B, direction, speed, distance, acceleration, standstill timeout, faults; `monitor` command prints them continuously. Self-test with loopback wires | J8.2, J8.3 (PA6, PA7, TIM3) | **FW 0.3.0: automatic tests pass on board 1 (loopback); live/fault check open** |
| 3b | Sensor fault emulation (`fault`): stuck channel, phase error, glitches, dropped pulses, wheel slip/slide, so a receiver's fault detection can be tested | J8.6, J8.7 | **FW 0.4.0: automatic tests pass on board 1 (117/117 with loopback)**; scope check open |
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

### Stage 3 checks (bench)

| # | Check | Result |
|---|---|---|
| 3.1 | `tools/build_all.sh`: debug + release, 0 warnings | PASS (2026-11-06) |
| 3.2 | Flash, POST, `status` FW 0.4.0, banner `Settings defaults (nothing saved)` on a board with an empty settings page | PASS (2026-11-06, board 1, UART bootloader; page held one non-valid record from earlier firmware, ignored) |
| 3.3 | `tools/pulse_test.py`: ALL CHECKS PASSED (speed, ramp, refused while moving, save/reset/reload) | PASS (2026-11-06, board 1: 54/54 without loopback, then 68/68 with loopback J8.6→J8.2, J8.7→J8.3) |
| 3.4 | Scope during a ramp (`accel 1`, `speed 100`, `speed 0`): frequency rises and falls smoothly, no short pulse, 90° kept, outputs hold at standstill | open |

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
- 2026-11-06: Stage 3 decisions (user): `speed` ramps to the target at `accel` (0 = at once), settings
  persist only with an explicit `save`, the receiver shares `ppr`/`wheel` with the outputs.
- 2026-11-06: Stage 3 firmware 0.4.0: `speed.c` (speed -> frequency, ramp stepped every 20 ms in the main
  loop, starts from standstill at the first step, stops with levels held at 0; `freq`/`start`/`stop` now go
  through it and cancel a ramp) and `settings.c` (32-byte CRC records appended in the page at 0x0803F800,
  erase after 64 saves). Commands `speed`, `rpm` (wheel turns per minute, added on request), `ppr`, `wheel`, `accel`, `save`, `defaults`; `ppr`/`wheel`/
  `save`/`defaults` refused while moving. `pulse_test.py` stage 3 checks. Build-tested only (0 warnings);
  not yet flashed. Note: 0.3.0 was never tagged; its tag would go on commit 0270392.
- 2026-11-06: Board 1 was found running Handoff bring-up FW 0.5.0 (attenuators 2.5 dB per stage, up 65 min);
  the user confirmed reflashing. Flashed 0.4.0 (`artifacts/v0.4.0-0270392-dirty`) over the UART bootloader
  with `tools/build_and_flash.sh`; POST passed, settings `defaults, nothing saved`. The settings page held one
  non-valid record (probably from earlier firmware): ignored by the CRC check, so the first `save` went to
  record 2. `pulse_test.py` first run 53/54: `speed 100` gave A/B 1921.968 Hz, outside the test's fixed
  +/-0.1 Hz; this is the A/B timer rounding (half period a multiple of 4 counts, -0.01 %), so the test now
  allows 0.02 %. Second run ALL CHECKS PASSED (54/54): speed 100 -> 1921.968 Hz, 100 at 100 ppr ->
  961.076 Hz, rpm 500 -> 1666.667 Hz / 86.71 km/h, 5 m/s2 ramp 17.71 km/h after 1.00 s, steady 36 km/h at
  691.994 Hz after 2.6 s, braked to standstill with levels held, save -> reset -> 123 ppr / 850 mm / 1.250 /
  rev loaded, original settings saved back. Receiver checks skipped (loopback wires not fitted).
  Not verified: receiver against the speed model, ramp on the scope (check 3.4).
- 2026-11-06: Board 1, FW 0.4.0, loopback wires J8.6→J8.2 and J8.7→J8.3 fitted: `pulse_test.py` ALL CHECKS
  PASSED (68/68). Receiver vs speed model: 36.0 km/h at `speed 36`, 500.0 rpm at `rpm 500`. Receiver: A/B
  1000.0 Hz, duty 50.0 %, phase 90.0° forward / 270.0° reverse, 52.02 km/h, distance +1730 pulses in 2 s,
  683 -> -317 in reverse, 1.0 Hz at `start 1`, standstill 2 s after stop, `monitor` with the rpm column
  (300.00 rpm at 1000 Hz). Still open: T13 (live fault by pulling a wire) and scope check 3.4.
- 2026-11-06: On request: `decel` (separate slow-down rate), `brake`, `vmax` speed limit (default 600 km/h,
  saved), accel/decel up to 50 m/s²; settings record version 2 (version 1 records load with decel = accel,
  vmax 600). Receiver tests at 10 kHz and 50 kHz added. Board 1 (UART flash, loopback fitted):
  `pulse_test.py` ALL CHECKS PASSED (87/87): vmax 600 -> speed 600 = 11534.0 Hz, 600.01 refused; vmax 100
  limits speed and rpm; brake at decel 10 from 36 km/h: 18.0 km/h after 0.50 s, standstill within 1.5 s;
  accel 50: 0 -> 180 km/h steady within 1.4 s (3460.2 Hz); receiver at 10 kHz: 10000.0 Hz, 50.0 %, 90.0°,
  520.25 km/h, 0 lost edges; at 50 kHz: 50000.0 Hz, 50.0 %, 90.0°, 2601.24 km/h, 0 lost edges.
- 2026-11-06: Sensor fault emulation (user chose option A, all faults): `fault.c` + fault support in `quad.c`
  (lag compare G = N/4 + N x phase / 180, frequency change generalised to any phase; stuck channel = OCxM
  forced, cleared by re-syncing to the ideal levels; dropped pulse = CC interrupt forces the channel low
  for two toggles, CC3 interrupt only while dropping on B; glitch = forced opposite level timed with DWT,
  only when the channel's next toggle is far) and wheel slip/slide in `speed.c`. First board run 113/117:
  drop off by one (1 in n+1), glitches made but lost by our receiver because the glitch masked all
  interrupts (now BASEPRI masks only TIM1/TIM2), and a test parse bug. Second run ALL CHECKS PASSED
  (117/117): stuck A/B -> FAULT A/B missing, ok -> 90.0° again; phase 45 -> 45.0° FAULT phase, kept at
  2000 Hz; phase 100 -> OK; drop a 10 -> A 900.0 Hz; drop b 4 -> B 750.0 Hz; glitch a 100 -> A 1100.0 Hz;
  slip 20 -> receiver 119.98 km/h while the model shows 100; slide -30 -> 70.0 km/h.
  Not verified on a scope: glitch width and shape.
