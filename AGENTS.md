# AGENTS.md: Pulse Generator Emulator firmware

Instructions for AI coding agents (Claude Code, Codex, Cursor, …) working in this
directory. Humans should start with `README.md`.

## What this is

Firmware that emulates an axle-mounted **optical pulse generator** (rail speed sensor) on the
**Handoff Simulator rev A** board: **STM32L451RCT6**, FT231X USB-UART console, outputs on expansion
header **J8** only. The board's QPC6713 attenuators are not used (their LE line is only held low).

- Schematic: `/home/hbl/Desktop/Handoff_Simulator/hardware/1_Handoff_Simulator/DesignFiles/Handoff_Simulator.kicad_pro`
- Hardware facts for firmware, J8 pin map, **known board issues**: `docs/HARDWARE.md`
- Commands and behaviour: `docs/USER_MANUAL.md`. Update it when a command or its output changes.
- Stages, status, open decisions: `PLAN.md`. Update its status and log when you finish a task, and do
  not decide anything listed under "Decisions needed" yourself.
- Origin: built from `PROJECT_TEMPLATE.md` (Handoff Simulator bring-up firmware).

> The schematic's MCU *value* field says STM32L152RET6. That is wrong. The fitted part is the
> **STM32L451RCT6**. Never write L1-family code.

## Build, flash, test

The toolchain is arm-none-eabi-gcc 14.3 (not on PATH; `cmake/arm-none-eabi.cmake` finds it,
or set `ARM_TOOLCHAIN_PATH`). The build uses CMake ≥ 3.22 and Ninja.

```bash
cmake --preset debug && cmake --build --preset debug       # -> build/debug/pulse_gen.{elf,hex,bin}
cmake --preset release && cmake --build --preset release
cmake --build --preset debug --target flash        # SWD via ST-LINK on J6 (STM32_Programmer_CLI)
cmake --build --preset debug --target flash_uart   # ROM bootloader via FT231X (send `bootloader` first)
tools/build_all.sh                                 # both from clean, fails on any warning (= definition of done)
tools/build_and_flash.sh                           # release run: build, artifacts/, delete build/, flash, check version
~/.venvs/bringup/bin/python tools/pulse_test.py    # on-target checks
```

**Definition of done for any code change:** both builds finish with **zero compiler warnings** in our
code (vendor code is compiled with `-w`). If hardware is attached, also flash and run `tools/pulse_test.py`.

Check whether a board is attached before trying to flash: `STM32_Programmer_CLI -l` lists ST-LINK
probes, and `ls /dev/ttyUSB*` shows the FT231X. If no board is attached, say so and stop at a
successful build. Never claim on-target or scope results you did not observe.

## Layout

```
Core/Inc/board.h          single source of truth for pins (from the netlist)
Core/Src/main.c           clock tree (HSI16->PLL 80 MHz), init order, main loop, attenuator LE hold
Core/Src/pulse.c          pulse output: TIM2_CH1 toggle mode on PA5 (J8.1), frequency, start/stop, count
Core/Src/quad.c           A/B outputs 90° apart: TIM1_CH2N/CH3N toggle mode on PB0/PB1 (J8.6/J8.7), sensor faults
Core/Src/rx.c             receiver: TIM3 input capture on PA6/PA7 (J8.2/J8.3), speed, phase, faults
Core/Src/speed.c          speed model: speed/rpm -> frequency, accel/decel ramps, brake, wheel slip
Core/Src/settings.c       ppr, wheel, accel, decel, vmax, dir in the last flash page (CRC records)
Core/Src/fault.c          sensor fault emulation: glitch scheduling, `fault` status line
Core/Src/console.c        USART1 115200 8N1, IRQ RX ring buffer, blocking TX
Core/Src/cli.c            command shell; `commands[]` is the command list (`help all` prints it)
Core/Src/diag.c           IDs, VREFINT/temp ADC, POST, BOOT0 readback
Core/Inc/version.h        FW_VERSION (banner, `status`)
Core/Src/stm32l4xx_it.c   exception + IRQ handlers (TIM2, USART1); faults stop the pulse timer
Core/Inc/stm32l4xx_hal_conf.h   enabled HAL modules
Core/Startup/, Core/Src/system_stm32l4xx.c   ST files (copied by vendor script)
Drivers/                  vendored CMSIS + HAL subset: DO NOT EDIT, re-run tools/vendor_cube.sh
STM32L451RCTX_FLASH.ld    256K flash (last 2 KB page reserved for stage 2 settings) / 160K RAM
tools/                    build_all, build_and_flash, con_send, pulse_test, debug (+gdb/), vendor_cube, udev/
.zed/                     Zed tasks and clangd settings
.github/workflows/build.yml   CI: build_all.sh on every push; tag v<FW_VERSION> -> GitHub release
artifacts/v<ver>-<hash>/  released images; commit only clean ones (-dirty is git-ignored)
.claude/                  Claude Code: settings.json (permissions + safety hook), skills/release, skills/bench
PROJECT_TEMPLATE.md       the reusable template this project was made from
```

There is no CubeMX `.ioc`. Peripheral init is hand-written code in the module that owns the
peripheral. Do not generate CubeMX code into this tree.

## Rules

### Hardware truth
- Every pin, AF number or connection must trace back to the schematic (`docs/HARDWARE.md`) and the
  STM32L451 datasheet. Put pin definitions only in `Core/Inc/board.h`.
- If code and schematic disagree, stop and report it.
- No crystals are fitted. Do not enable HSE or LSE. Frequency accuracy is HSI16 (about ±1 %).
- Never claim a frequency, duty cycle or waveform was verified unless someone measured it on J8.

### Safety (hardware and irreversible actions)
- **Never** change option bytes, RDP level, write protection, or do a mass erase without an explicit
  request from the user in the current conversation. RDP level 2 permanently bricks debug access.
- Nothing is driven on J8 at boot. The pulse output starts only on `start`. Faults stop the timer.
- Keep `board_hold_att_latch()` first in `main()`: the unused attenuators' LE (PB6) has no pull-down.
- The last flash page (0x0803F800) is reserved for settings; the linker script keeps ROM at 254K.
- **Open the FT231X port with DTR off**, always: pyserial `dtr=False` *before* `open()`, picocom
  `--lower-dtr`, STM32_Programmer_CLI `dtr=low`. DTR on holds the MCU in reset (HARDWARE.md finding 3).

### Code style
- C11, HAL style, matching the existing files: 2-space indent, braces on all blocks, a short
  comment on each module header explaining *why*, `snake_case` with a module prefix (`pulse_`,
  `con_`, `diag_`, `cli_`, `board_`).
- No dynamic allocation and no `printf` to stdio. Use `con_printf` (newlib-nano: no float, no `%llu`;
  print fixed-point integers, frequencies in mHz).
- Console output is parsed by `tools/pulse_test.py`. Keep the `OK` / `ERR` prefixes, `POST PASSED` /
  `POST FAILED`, `[PASS]` / `[FAIL]`, the `FW x.y.z,` status line and the prompt `> ` stable, or update the script too.
- New CLI command: `cmd_*` function + row in `commands[]` in `cli.c`, a line in `docs/USER_MANUAL.md`,
  and a check in `tools/pulse_test.py` if it can be tested over the console.

### Docs
- Every user-visible change gets a line under `[Unreleased]` in `CHANGELOG.md`.
- One fact, one place: commands in USER_MANUAL, hardware in HARDWARE.md, status and history in PLAN.md.

## Context-saving tips for agents
- Do not read KiCad files directly; use `Handoff_Simulator/software/board_bringup/tools/extract_pinmap.py`.
- Do not read `Drivers/`. Grep the headers for the register or function you need.
- Reference manual: RM0394 (STM32L41x–L46x). Datasheet: DS11912 (STM32L451xx).
