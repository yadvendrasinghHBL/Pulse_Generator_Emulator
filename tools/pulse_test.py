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

RATE_TOL = 0.02     # pulse count vs host clock: HSI16 (+/-1 %) plus USB timing jitter


class Board:
    def __init__(self, port):
        self.port = port

    def cmd(self, line):
        self.port.write(line.encode() + b"\r")
        self.port.flush()                               # wait until the bytes have left the PC
        self.sent_at = time.monotonic()                 # the board acts on the line about now
        lines = read_reply(self.port).split("\n")[1:]   # first line is the echo
        return "\n".join(ln for ln in lines if ln.strip() not in (">", ""))

    def counts(self):
        """(J8.1 pulses, A/B pulses) from one `status`; None where missing."""
        st = self.cmd("status")
        m = re.search(r"^pulses (\d+) since start", st, re.M)
        q = re.search(r"^A/B pulses (\d+) since start", st, re.M)
        return (int(m.group(1)) if m else None), (int(q.group(1)) if q else None)

    def pulses(self):
        return self.counts()[0]

    def levels_ab(self):
        m = re.search(r"levels A (high|low), B (high|low)", self.cmd("status"))
        return (m.group(1) == "high", m.group(2) == "high") if m else None


results = []


def check(ok, what, detail=""):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {what}{': ' + detail if detail else ''}")


def rate_check(b, hz, seconds):
    # Time between the two samples, not around the two `status` round trips: the reply wait
    # (until the prompt or a quiet gap) is not part of the counting window. v0.1.0 timed the
    # whole exchange and read about 1.5 % low while a counter on J8.1 showed 1000.00 Hz.
    b.cmd(f"start {hz}")
    c0 = b.counts()
    t0 = b.sent_at
    time.sleep(seconds)
    c1 = b.counts()
    dt = b.sent_at - t0
    for i, name in ((0, "J8.1"), (1, "A/B")):
        if c0[i] is None or c1[i] is None:
            check(False, f"{name} pulse rate at {hz} Hz", "no pulse count in status")
            continue
        n = c1[i] - c0[i]
        rate = n / dt
        check(abs(rate - hz) <= hz * RATE_TOL, f"{name} pulse rate at {hz} Hz",
              f"{n} pulses in {dt:.2f} s = {rate:.1f} Hz (tolerance {RATE_TOL:.0%})")


def sequence_check(b, direction):
    """At 0.5 Hz the A/B edges are 0.5 s apart, at 0.25, 0.75, 1.25, 1.75 s after `start`
    (first edge a quarter of a half period after start). Sample between them and check the
    order. Forward: A rises first, then B, then A falls (B 90 deg behind A). Reverse: B first."""
    want = [(1, 0), (1, 1), (0, 1)] if direction == "fwd" else [(0, 1), (1, 1), (1, 0)]
    b.cmd(f"dir {direction}")
    b.cmd("start 0.5")
    t0 = b.sent_at
    got = []
    for at in (0.5, 1.0, 1.5):              # halfway between two edges
        time.sleep(max(0.0, t0 + at - time.monotonic()))
        lv = b.levels_ab()
        got.append(None if lv is None else (int(lv[0]), int(lv[1])))
    b.cmd("stop low")
    fmt = lambda seq: " -> ".join("?" if x is None else f"A{x[0]}B{x[1]}" for x in seq)
    check(got == want, f"A/B order, dir {direction} at 0.5 Hz",
          f"{fmt(got)} (expected {fmt(want)})")


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

        check(b.cmd("dir rev").startswith("ERR"), "dir change rejected while running")
        check(b.cmd("dir up").startswith("ERR"), "dir up rejected")

        # frequency change while running, from a very slow to a fast setting
        b.cmd("start 0.05")
        time.sleep(0.5)
        b.cmd("freq 1000")
        time.sleep(0.2)
        c0 = b.counts()
        time.sleep(1.0)
        c1 = b.counts()
        for i, name in ((0, "J8.1"), (1, "A/B")):
            ok = None not in (c0[i], c1[i]) and c1[i] - c0[i] > 800
            check(ok, f"{name}: 0.05 Hz -> 1000 Hz applies at once (no wait for the 10 s half period)",
                  f"{(c1[i] or 0) - (c0[i] or 0)} pulses in about 1 s")

        b.cmd("stop low")
        sequence_check(b, "fwd")
        sequence_check(b, "rev")
        b.cmd("dir fwd")
        b.cmd("start 100")

        r = b.cmd("stop")
        check("stopped" in r, "stop holds the level", r)
        n0 = b.pulses()
        time.sleep(0.5)
        check(n0 is not None and b.pulses() == n0, "no pulses while stopped")

        r = b.cmd("stop low")
        check("level low" in r and "levels A low, B low" in r, "stop low forces all outputs low", r)

    failed = results.count(False)
    print(f"\n{'ALL CHECKS PASSED' if not failed else f'FAILED: {failed} of {len(results)} checks'}"
          " (output left stopped, low)")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
