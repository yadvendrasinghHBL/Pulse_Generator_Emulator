# PLAN.md: Pulse Generator Emulator

## 1. Goal and scope

Emulate an axle-mounted **optical pulse generator** (OPG, e.g. HaslerRail Optical Pulse Generator,
datasheet 5.0301.035-04TEN) with the **Handoff Simulator rev A** board, using only expansion header **J8**.
The attenuator section of the board is not used.

Reference OPG (datasheet §3.1 and product page): 50 % square wave (40–60 %), 1–6 isolated channels,
90° or 120° phase shift, 1–200 pulses per turn (400 on request), 1 or 2 code tracks, up to 3/6/10 kHz
(40 kHz special), outputs 0–30 V push-pull / open drain / 7–16 mA current, supply 10–30 V.

## 2. Stages

| Stage | Content | Pins | Status |
|---|---|---|---|
| 1 | Single 50 % pulse output, `freq` / `start` / `stop` / `status`, project + tools + CI | J8.1 (PA5, TIM2) | **done; automatic tests pass on board 1; scope checks open** |
| 2 | Speed model: `speed <km/h>`, `wheel <mm>`, `ppr <n>`, `accel <m/s²>` ramps, distance from pulse count, settings in flash (last page 0x0803F800) | J8.1 | open |
| 3 | 2 or 4 phase-shifted channels (90°/120°) on TIM3, `dir fwd/rev`; optional second code track on PA5 | J8.2/.3/.6/.7 (PA6, PA7, PB0, PB1) | open |
| 4 | Output stage hardware: isolated open-drain per channel (opto + MOSFET), receiver supplies the pull-up; ground from J6.2 | add-on board | open |

### Stage 1 checks (bench)

| # | Check | Result |
|---|---|---|
| 1.1 | `tools/build_all.sh`: debug + release, 0 warnings | PASS (2026-11-05) |
| 1.2 | Flash, banner, POST PASSED, `status` shows FW 0.1.0 | PASS (2026-11-05, board 1, SWD) |
| 1.3 | `tools/pulse_test.py`: ALL CHECKS PASSED | PASS (2026-11-05, board 1, 18/18) |
| 1.4 | Scope on J8.1 (GND J6.2): 1 kHz, 50 % duty, 3.3 V | open |
| 1.5 | Scope: `freq` change while running gives no short pulse (single-shot trigger on pulse width < new half period) | open |
| 1.6 | Frequency counter: absolute error at 1 kHz (expect within ±1 %, HSI16) | open |
| 1.7 | Scope: first edge after `start` (whether the timer toggles at once or after one half period) | open |

## 3. Decisions needed (user)

| # | Question | Options |
|---|---|---|
| Q1 | Default pulses per turn and wheel diameter for stage 2 | proposal: 200 ppr, 920 mm |
| Q2 | Channels for stage 3 | 2 (A/B quadrature) or 4 (two redundant pairs); 90° or 120° |
| Q3 | Receiver the emulator must drive | decides the stage 4 output stage (voltage, open drain / push-pull / current loop) |
| Q4 | Frequency accuracy needed | if better than ±1 %: add a reference crystal (hardware) or calibrate HSI (MSI/LSE trim not possible: no LSE fitted) |

## 4. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | J8 has no GND pin | ground from J6.2; stage 4 board brings its own ground connection |
| R2 | HSI16 accuracy (±1 %) | state it in reports; Q4 |
| R3 | 3.3 V outputs connected straight to a 24 V receiver | USER_MANUAL warning; stage 4 output stage |
| R4 | BOOT0 / DTR issues of rev A (HARDWARE.md findings 2, 3) | DTR off in all tools; option-byte fix on each board |

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
