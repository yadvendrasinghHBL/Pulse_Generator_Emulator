# Changelog

User-visible changes per firmware version (`FW_VERSION` in `Core/Inc/version.h`, git tag `v<version>`).
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). The engineering log is in `PLAN.md` §5.
A GitHub release is created from the section of the tagged version.

## [Unreleased]

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

[Unreleased]: https://github.com/yadvendrasinghHBL/Pulse_Generator_Emulator/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/yadvendrasinghHBL/Pulse_Generator_Emulator/releases/tag/v0.1.0
