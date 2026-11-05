---
name: bench
description: Run the on-target checks on a connected Pulse Generator Emulator board (find the board, flash if asked, pulse_test.py) and report the real console output. Use when the user says a board is connected and asks to test it, check its status, or verify a change on hardware.
---

# Bench session

Rules (AGENTS.md, CLAUDE.md): only with a board the user says is connected; DTR always off; never
option bytes, RDP or mass erase; report the actual output, never results you did not see.

1. Find the board: `ls /dev/ttyUSB*` and `STM32_Programmer_CLI -l st-link`.
   No port and no probe: say so and stop.
2. One program per port: if `tools/con_send.sh status` says the port is busy, ask the user to close
   their terminal program.
3. Flash only if asked: `tools/build_and_flash.sh` (release image, SWD or UART chosen automatically).
4. Before testing, ask whether anything is connected to J8.1; the test drives it up to 10 kHz.
5. Tests: `~/.venvs/bringup/bin/python tools/pulse_test.py`. The last line must be `ALL CHECKS PASSED`.
6. Report: firmware version, PASS/FAIL lines, anything unexpected. Scope results only if the user
   measured them. Update the PLAN.md stage check table and log.
