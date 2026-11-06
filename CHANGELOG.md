# Changelog

User-visible changes per firmware version (`FW_VERSION` in `Core/Inc/version.h`, git tag `v<version>`).
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). The engineering log is in `PLAN.md` §5.
A GitHub release is created from the section of the tagged version.

## [Unreleased]

### Added
- Stage 4: receiver on J8.2 (PA6, TIM3_CH1) and J8.3 (PA7, TIM3_CH2): input capture on both edges,
  frequency and duty per input, phase A–B, direction, speed (200 ppr, 920 mm), signed distance,
  acceleration, standstill after 2 s, faults (input missing, phase outside 90° ± 20°).
- Commands `rx`, `rx reset`, `monitor`; engineering command `inputs` (raw pin levels, edge counts, TIM3).
- `pulse_test.py`: receiver checks with loopback wires J8.6 → J8.2, J8.7 → J8.3 (skipped without them).
- Stage 3 (FW 0.4.0): speed model. `speed <km/h>` drives all outputs at a train speed, ramping at
  `accel <m/s²>` (0 = at once); `speed 0` brakes to standstill and holds the levels. Settings `ppr`
  (1–1000) and `wheel` (100–2000 mm), shared with the receiver; `save` keeps ppr, wheel, accel and
  direction in the last flash page, `defaults` restores 200 ppr / 920 mm / 0 / forward.
- Drive profiles, command `profile`: up to 32 steps (`speed`, `rpm`, `accel`, `decel`, `brake`, `stop`,
  `dir`, `wait`, `reach`, `slip`), `run [n]`, `loop`, `stop`, `demo`, `save` (own flash page 0x0803F000,
  loaded at power-up, never starts by itself); a manual drive command stops a running profile.
- Firmware image limit 252 KB (was 254 KB): the second-last flash page holds the profile.
- Sensor fault emulation, command `fault`: stuck channel (`fault a low|high|ok`), phase error
  (`fault phase 0..134|ok`), glitches (`fault glitch a|b|ab <per s> [µs]`), dropped pulses
  (`fault drop a|b|ab <1 in n>`), wheel slip/slide (`fault slip <%> <s>`), `fault clear`; shown in `status`.
- `pulse_test.py`: every fault checked with the receiver in loopback.
- `decel <m/s²>` (slow-down rate, used by `speed` when lowering and by `brake`), `brake` (slow to
  standstill at decel), `vmax <km/h>` speed limit for `speed`/`rpm` (default 600 km/h); `accel` and
  `decel` up to 50 m/s². Saved settings from earlier 0.4.0 builds load with decel = accel, vmax 600.
- `pulse_test.py`: receiver checks at 10 kHz and 50 kHz (frequency, duty, phase, speed, no lost edges).
- `rpm <n>`: drive at a wheel speed in turns per minute (same ramp as `speed`); wheel rpm shown by
  `speed`, `status`, `rx` and `monitor`.
- `status` shows the speed, the distance since start and the settings; the banner shows where the
  settings came from.
- `pulse_test.py`: stage 3 checks (speed, ramp, settings refused while moving, save and reload after reset).

### Changed
- The receiver uses the `ppr` / `wheel` settings instead of fixed 200 ppr / 920 mm.

## [0.2.0] - 2026-11-05

### Added
- Stage 2: two-channel sensor output, A on J8.6 (PB0, TIM1_CH2N) and B on J8.7 (PB1, TIM1_CH3N),
  50 % duty, exactly 90° apart; frequency changes keep the A/B order without short pulses.
- Command `dir fwd|rev` (only while stopped); `status`/`freq`/`stop` show A/B state, levels, actual
  frequency and pulse count; `freq`, `start`, `stop` drive J8.1 and A/B together.
- `pulse_test.py`: A/B rate, `dir`, A/B edge order in both directions.

## [0.1.0] - 2026-11-05

### Added
- Stage 1: 50 % square-wave pulse output on J8.1 (PA5, TIM2_CH1), 0.01–50000 Hz.
- Commands `status`, `freq`, `start`, `stop`, `stop low`, `reset`; engineering commands `info`, `post`,
  `vdd`, `led`, `boot0`, `bootloader`.
- Output off at power-up; frequency changes apply at once without short pulses; pulse counter.
- Tools: `build_all.sh`, `build_and_flash.sh`, `con_send.sh`, `pulse_test.py`, `debug.sh`, `vendor_cube.sh`.
- GitHub Actions CI: both builds with zero warnings; a `v*` tag creates a GitHub release.

[Unreleased]: https://github.com/yadvendrasinghHBL/Pulse_Generator_Emulator/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/yadvendrasinghHBL/Pulse_Generator_Emulator/releases/tag/v0.2.0
[0.1.0]: https://github.com/yadvendrasinghHBL/Pulse_Generator_Emulator/releases/tag/v0.1.0
