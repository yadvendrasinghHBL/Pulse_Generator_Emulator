#!/usr/bin/env python3
"""On-target checks for the pulse generator firmware, over the USB serial console.

Why: proves the console commands and the pulse timer on a real board without a scope.
The pulse count reported by `status` is compared with the host clock, so a wrong
timer setup (frequency off by 2x, no interrupts, counter not stopping) is caught.
The absolute frequency still needs a scope or frequency counter on J8.1: the MCU
runs from HSI16 (no crystal), which is only accurate to about +/-1 %.

    tools/pulse_test.py                  # port defaults to /dev/ttyUSB0
    PORT=/dev/ttyUSB1 tools/pulse_test.py

The output is left stopped and low (`stop low`) at the end.
Exit status: 0 = all checks passed, 1 = a check failed, 2 = no board / port busy.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from con_send import open_port, port_users, read_reply, wait_for_prompt  # noqa: E402

RATE_TOL = 0.05     # pulse count vs host clock; serial latency is the main error


class Board:
    def __init__(self, port):
        self.port = port

    def cmd(self, line):
        self.port.write(line.encode() + b"\r")
        lines = read_reply(self.port).split("\n")[1:]   # first line is the echo
        return "\n".join(ln for ln in lines if ln.strip() not in (">", ""))

    def pulses(self):
        m = re.search(r"^pulses (\d+) since start", self.cmd("status"), re.M)
        return int(m.group(1)) if m else None


results = []


def check(ok, what, detail=""):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {what}{': ' + detail if detail else ''}")


def rate_check(b, hz, seconds):
    b.cmd(f"start {hz}")
    t0 = time.monotonic()
    n0 = b.pulses()
    time.sleep(seconds)
    n1 = b.pulses()
    dt = time.monotonic() - t0
    if n0 is None or n1 is None:
        check(False, f"pulse rate at {hz} Hz", "no pulse count in status")
        return
    rate = (n1 - n0) / dt
    check(abs(rate - hz) <= hz * RATE_TOL, f"pulse rate at {hz} Hz",
          f"{n1 - n0} pulses in {dt:.2f} s = {rate:.1f} Hz (tolerance {RATE_TOL:.0%})")


def main():
    path = os.environ.get("PORT", "/dev/ttyUSB0")
    if not os.path.exists(path):
        print(f"ERR {path} not found: is the board's USB cable plugged in?", file=sys.stderr)
        return 2
    busy = port_users(path)
    if busy:
        print(f"ERR port busy: {path} is open in {', '.join(busy)}. Close it first.", file=sys.stderr)
        return 2
    with open_port(path) as port:
        if not wait_for_prompt(port):
            print(f"ERR no prompt from the firmware on {path}", file=sys.stderr)
            return 2
        b = Board(port)

        st = b.cmd("status")
        print(st)
        check(re.search(r"^FW \d+\.\d+\.\d+,", st, re.M) is not None, "status shows the FW version")
        check("POST passed" in st, "POST passed")

        for bad in ("0", "50000.001", "abc", "1.2345", "-5"):
            check(b.cmd(f"freq {bad}").startswith("ERR"), f"freq {bad} rejected")
        check(b.cmd("stop high").startswith("ERR"), "stop high rejected")

        r = b.cmd("freq 1000")
        check(r.startswith("OK freq 1000.000 Hz (actual 1000.000 Hz, half period 40000 ticks"),
              "freq 1000 -> 40000 ticks", r)
        r = b.cmd("freq 0.01")
        check("half period 4000000000 ticks" in r, "freq 0.01 -> 32-bit half period", r)

        r = b.cmd("start 100")
        check(r.startswith("OK output") and "running" in r, "start 100", r.split("\n")[0])
        rate_check(b, 100, 3.0)
        rate_check(b, 1000, 3.0)
        rate_check(b, 10000, 3.0)

        # frequency change while running, from a very slow to a fast setting
        b.cmd("start 0.05")
        time.sleep(0.5)
        b.cmd("freq 1000")
        time.sleep(0.2)
        n0 = b.pulses()
        time.sleep(1.0)
        n1 = b.pulses()
        check(n0 is not None and n1 is not None and n1 - n0 > 800,
              "0.05 Hz -> 1000 Hz applies at once (no wait for the 10 s half period)",
              f"{(n1 or 0) - (n0 or 0)} pulses in about 1 s")

        r = b.cmd("stop")
        check("stopped" in r, "stop holds the level", r)
        n0 = b.pulses()
        time.sleep(0.5)
        check(n0 is not None and b.pulses() == n0, "no pulses while stopped")

        r = b.cmd("stop low")
        check("level low" in r, "stop low forces the output low", r)

    failed = results.count(False)
    print(f"\n{'ALL CHECKS PASSED' if not failed else f'FAILED: {failed} of {len(results)} checks'}"
          " (output left stopped, low)")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
