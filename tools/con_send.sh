#!/usr/bin/env bash
# Send one command to the firmware's shell (USART1 over the FT231X) and print the reply.
# Thin wrapper around con_send.py: the port must be opened with DTR off, because on rev A boards
# DTR# low holds the MCU in reset through D1 (docs/HARDWARE.md finding 3), and `stty` cannot do
# that. Uses the bring-up venv (pyserial) if present, else python3.
#
#   tools/con_send.sh "start 1000"            # port defaults to /dev/ttyUSB0
#   PORT=/dev/ttyUSB1 tools/con_send.sh info
#
# Exit status: 0 if the reply contains no "ERR" line, 1 on ERR, 2 if the shell did not answer.
PY=${PYTHON:-$HOME/.venvs/bringup/bin/python}
[ -x "$PY" ] || PY=python3
exec "$PY" "$(dirname "$0")/con_send.py" "$@"
