#!/usr/bin/env bash
# SWD debug session: build the debug image, start ST-LINK_gdbserver, open GDB in this terminal.
# Why a script: the editor (Zed) runs it as a task, and the same command works from any shell.
#
#   tools/debug.sh           # load, start at the reset vector, stop in main()
#
# GDB helpers from tools/gdb/common.gdb: `pulse` shows the TIM2 registers and the J8.1 level.
# Tools are looked up in the STM32Cube bundles (~/.local/share/stm32cube/bundles); override with
# STLINK_GDBSERVER, ARM_GDB, CUBEPROG_BIN. The ST-LINK is busy while this runs: quit GDB before flashing.
set -eu
cd "$(dirname "$0")/.."

B=$HOME/.local/share/stm32cube/bundles
newest() { ls -d $1 2>/dev/null | sort -V | tail -1; }
SERVER=${STLINK_GDBSERVER:-$(newest "$B/stlink-gdbserver/*/bin/ST-LINK_gdbserver")}
GDB=${ARM_GDB:-$(newest "$B/gnu-gdb-for-stm32/*/bin/arm-none-eabi-gdb")}
PROG=${CUBEPROG_BIN:-$(newest "$B/programmer/*/bin")}
for t in "$SERVER" "$GDB" "$PROG"; do
  if [ ! -e "$t" ]; then
    echo "ERR tool not found ('$t'). Set STLINK_GDBSERVER / ARM_GDB / CUBEPROG_BIN." >&2
    exit 2
  fi
done

cmake --preset debug >/dev/null
cmake --build --preset debug

LOG=$(mktemp)
"$SERVER" -p 61234 -l 1 -d -s -cp "$PROG" -m 0 -k >"$LOG" 2>&1 &
SERVER_PID=$!
# On exit stop the server and wait for it, so the ST-LINK is free for the next task.
trap 'kill $SERVER_PID 2>/dev/null || true; wait $SERVER_PID 2>/dev/null || true; rm -f "$LOG"' EXIT

for _ in $(seq 1 40); do
  if grep -q "Waiting for debugger" "$LOG"; then
    break
  fi
  if ! kill -0 $SERVER_PID 2>/dev/null; then
    cat "$LOG" >&2
    echo "ERR ST-LINK_gdbserver stopped. Is the ST-LINK plugged in and J6 connected?" >&2
    exit 1
  fi
  sleep 0.25
done

"$GDB" -q build/debug/pulse_gen.elf -x tools/gdb/common.gdb -ex "break main" -ex "continue"
