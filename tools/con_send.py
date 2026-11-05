#!/usr/bin/env python3
"""Send one command to the firmware's shell and print the reply.

Why: pulse_test.py runs a fixed set of checks; bench steps where a person looks at the
board (scope on J8.1, LEDs) need single commands. Python + pyserial, because only pyserial can open
the port with DTR off: on rev A boards DTR# low holds the MCU in reset through D1
(docs/HARDWARE.md finding 3). Called through tools/con_send.sh.

    tools/con_send.sh "start 1000"        # port defaults to /dev/ttyUSB0
    PORT=/dev/ttyUSB1 tools/con_send.sh info

Exit status: 0 if the reply has no "ERR" line, 1 on ERR, 2 if the shell did not answer.
"""
import os
import sys
import time

import serial

PROMPT = b"\r\n> "
QUIET_S = 1.0    # fallback end of reply when no prompt comes (e.g. `bootloader`)
MAX_S = 60.0     # hard limit, e.g. if a `stream` is running
BOOT_S = 8.0     # after a reset the board is silent ~2.5 s (lamp test, banner delay, POST)


def port_users(path):
    """Other processes that have the port open, as "name (PID n)" strings.

    Linux lets two programs open the same tty; they then steal each other's bytes.
    """
    users = []
    try:
        real = os.path.realpath(path)
        for pid in os.listdir("/proc"):
            if not pid.isdigit() or int(pid) == os.getpid():
                continue
            try:
                for fd in os.listdir(f"/proc/{pid}/fd"):
                    if os.path.realpath(f"/proc/{pid}/fd/{fd}") == real:
                        with open(f"/proc/{pid}/comm") as f:
                            users.append(f"{f.read().strip()} (PID {pid})")
                        break
            except OSError:
                continue
    except OSError:
        pass
    return users


def open_port(path):
    port = serial.Serial()
    port.port = path
    port.baudrate = 115200
    port.timeout = 0.05
    port.dtr = False   # must be set before open(): DTR# low = MCU reset (D1)
    port.rts = True    # RTS# low keeps BOOT0 low (finding 2)
    port.open()
    return port


def read_reply(port, quiet=QUIET_S):
    """Read until the "> " prompt, or until the board is quiet for `quiet` seconds."""
    buf = b""
    start = last = time.monotonic()
    while time.monotonic() - last < quiet and time.monotonic() - start < MAX_S:
        chunk = port.read(4096)
        if chunk:
            buf += chunk
            last = time.monotonic()
            if buf.endswith(PROMPT):
                break
    return buf.decode(errors="replace").replace("\r", "")


def wait_for_prompt(port):
    """Press Enter until the shell answers; covers a board that is still booting."""
    end = time.monotonic() + BOOT_S
    while time.monotonic() < end:
        port.write(b"\r")   # empty line: flush anything half-typed, get a fresh prompt
        if read_reply(port, quiet=1.0).endswith("\n> "):
            return True
    return False


def main():
    if len(sys.argv) < 2 or not sys.argv[1]:
        print(f'usage: {sys.argv[0]} "<command>"', file=sys.stderr)
        return 2
    cmd = sys.argv[1]
    path = os.environ.get("PORT", "/dev/ttyUSB0")
    if not os.path.exists(path):
        print(f"ERR {path} not found: is the board's USB cable plugged in?", file=sys.stderr)
        return 2
    busy = port_users(path)
    if busy:
        print(f"ERR port busy: {path} is open in {', '.join(busy)}. Close it first.", file=sys.stderr)
        return 2
    try:
        port = open_port(path)
    except serial.SerialException as e:
        print(f"ERR cannot open {path}: {e}\n    Is picocom or another program using it?", file=sys.stderr)
        return 2

    with port:
        if not wait_for_prompt(port):
            print(f"ERR no prompt from the firmware on {path} within {BOOT_S:.0f} s.\n"
                  "    Is the MCU held in reset (DTR, see docs/HARDWARE.md finding 3)?\n"
                  "    Unplug/replug USB, or reset over SWD",
                  file=sys.stderr)
            return 2
        port.write(cmd.encode() + b"\r")
        lines = read_reply(port).split("\n")[1:]   # first line is the firmware's echo
        out = [ln for ln in lines if ln.strip() != ">"]
        while out and not out[-1].strip():
            out.pop()
        print("\n".join(out))
    return 1 if any(ln.startswith("ERR") for ln in out) else 0


if __name__ == "__main__":
    sys.exit(main())
