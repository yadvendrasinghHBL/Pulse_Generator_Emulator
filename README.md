# Pulse Generator Emulator

[![build](https://github.com/yadvendrasinghHBL/Pulse_Generator_Emulator/actions/workflows/build.yml/badge.svg)](https://github.com/yadvendrasinghHBL/Pulse_Generator_Emulator/actions/workflows/build.yml)

Firmware that turns the Handoff Simulator rev A board (STM32L451RCT6, FT231X USB-UART) into an emulator of an
axle-mounted **optical pulse generator**, the speed sensor of a rail vehicle. Only expansion header **J8** is used.

**Stage 1 (v0.1.0):** a 50 % square wave on **J8.1 (PA5)**, 0.01 Hz – 50 kHz, set from a serial terminal.
Speed model, multi-channel/direction and an output stage follow ([`PLAN.md`](PLAN.md)).

## Quick start

```bash
tools/build_all.sh                                   # debug + release, must show 0 warnings
tools/build_and_flash.sh                             # clean build, artifacts/, flash, version check
picocom -b 115200 --lower-dtr /dev/ttyUSB0           # then: start 1000   (exit: Ctrl+A, Ctrl+X)
```

Scope on **J8.1**, ground on **J6 pin 2** (J8 has no GND pin). Output is 3.3 V logic.

**Always open the serial port with DTR off** (`--lower-dtr`, pyserial `dtr=False`): on rev A boards DTR on
holds the MCU in reset (`docs/HARDWARE.md` finding 3). All project tools do this.

## Documentation

| For | Read |
|---|---|
| Commands, output behaviour, testing, firmware update | [`docs/USER_MANUAL.md`](docs/USER_MANUAL.md) |
| Manual bench test with a scope (stage 1) | [`docs/MANUAL_TEST.md`](docs/MANUAL_TEST.md) |
| Pins, J8 header, known board issues (**read before powering a board**) | [`docs/HARDWARE.md`](docs/HARDWARE.md) |
| Stages, status, open decisions, log | [`PLAN.md`](PLAN.md) |
| Changes per version | [`CHANGELOG.md`](CHANGELOG.md) |
| AI coding agents | [`AGENTS.md`](AGENTS.md), [`CLAUDE.md`](CLAUDE.md) |
| Reusable project template this repo was made from | [`PROJECT_TEMPLATE.md`](PROJECT_TEMPLATE.md) |

## Tools

| Script | What it does |
|---|---|
| `tools/build_all.sh [--keep]` | debug + release from clean (incremental with `--keep`); fails on any warning. CI runs it too |
| `tools/build_and_flash.sh [--no-flash]` | clean build → release image + `SHA256SUMS` + `BUILD_INFO.txt` in `artifacts/v<ver>-<hash>/` → delete `build/` → flash (ST-LINK if attached, else `bootloader` + UART) → version check |
| `tools/pulse_test.py` | on-target checks: commands, rejected inputs, pulse rate at 100 Hz / 1 kHz / 10 kHz vs PC clock |
| `tools/con_send.sh "<cmd>"` | send one command, print the reply (exit 1 on `ERR`, 2 on no answer or busy port) |
| `tools/debug.sh` | GDB over SWD; `pulse` command shows TIM2 and the J8.1 level |
| `tools/vendor_cube.sh` | copy HAL/CMSIS from the STM32CubeL4 package into `Drivers/` |

Python tools need pyserial: `python3 -m venv ~/.venvs/bringup && ~/.venvs/bringup/bin/pip install pyserial`
(the same venv as the bring-up project).

**Zed:** Ctrl+Shift+P → `task: spawn` for build, flash, test, board and debug tasks (`.zed/tasks.json`).

## GitHub

- Every push runs the **build** workflow (both builds, zero warnings) and keeps the release image as a
  workflow artifact.
- Pushing a tag `v<FW_VERSION>` creates a **GitHub release** with `.hex/.bin/.elf` and `SHA256SUMS`;
  the release notes come from `CHANGELOG.md`.
