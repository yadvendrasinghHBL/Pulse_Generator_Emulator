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


def parse_rx(r):
    """Receiver values from `rx`, or None if the reply does not parse."""
    try:
        f = re.findall(r"^([AB]) ([\d.]+) Hz, duty ([\d.]+) %", r, re.M)
        ph = re.search(r"^phase (?:([\d.]+) deg (forward|reverse)|-)", r, re.M)
        sp = re.search(r"^speed ([\d.]+) km/h \(([\d.]+) rpm\)", r, re.M)
        pu = re.search(r"pulses (-?\d+)", r)
        le = re.search(r"lost edges (\d+)", r)
        st = re.search(r"^receiver .*: (.+)$", r, re.M)
        return {
            "fa": float(f[0][1]), "da": float(f[0][2]), "fb": float(f[1][1]), "db": float(f[1][2]),
            "phase": float(ph.group(1)) if ph.group(1) else None, "dir": ph.group(2),
            "speed": float(sp.group(1)), "rpm": float(sp.group(2)), "pulses": int(pu.group(1)), "lost": int(le.group(1)), "status": st.group(1).strip(),
        }
    except (AttributeError, IndexError, ValueError):
        return None


def parse_settings(r):
    """ppr, wheel, accel, dir and save state from the `settings:` line, or None."""
    m = re.search(r"settings: (\d+) ppr, wheel (\d+) mm .*, accel ([\d.]+) m/s2, decel ([\d.]+) m/s2, "
                  r"vmax (\d+) km/h, dir (fwd|rev): (.+)$", r, re.M)
    if not m:
        return None
    return {"ppr": int(m.group(1)), "wheel": int(m.group(2)), "accel": m.group(3), "decel": m.group(4),
            "vmax": int(m.group(5)), "dir": m.group(6), "state": m.group(7).strip()}


def parse_speed(r):
    m = re.search(r"^(?:OK )?speed ([\d.]+) km/h, target ([\d.]+) km/h, accel [\d.]+ m/s2, decel [\d.]+ m/s2: (\w+), "
                  r"distance (-?[\d.]+) m", r, re.M)
    if not m:
        return None
    return {"now": float(m.group(1)), "target": float(m.group(2)), "state": m.group(3),
            "dist": float(m.group(4))}


def ab_hz(r):
    m = re.search(r"^A/B actual ([\d.]+) Hz", r, re.M)
    return float(m.group(1)) if m else -1.0


def hz_for(kmh, ppr=200, wheel_mm=920):
    return kmh / 3.6 / (3.141592653589793 * wheel_mm / 1000 / ppr)


def near(hz, want):
    """A/B actual frequency vs the exact one: the 16-bit timer's half period is N x P counts with N a
    multiple of 4, so the produced frequency is off by up to 0.01 % (1922.16 Hz -> 1921.97 Hz)."""
    return abs(hz - want) <= want * 0.0002


def sleep_until(t):
    time.sleep(max(0.0, t - time.monotonic()))


def speed_checks(b):
    """Stage 3: speed model, ramps, settings in flash. Leaves the saved settings as it found them."""
    b.cmd("stop low")
    orig = parse_settings(b.cmd("ppr"))
    check(orig is not None, "settings line parses", str(orig))
    if orig is None:
        return
    for c in ("ppr 200", "wheel 920", "accel 0", "decel 0", "vmax 600", "dir fwd"):
        b.cmd(c)
    for bad in ("ppr 0", "ppr 1001", "ppr 2.5", "wheel 99", "wheel 2001", "accel 50.001", "accel -1",
                "decel 50.001", "vmax 0", "vmax 10000", "vmax 1.5", "speed abc", "speed 1.234", "speed 600.01"):
        check(b.cmd(bad).startswith("ERR"), f"{bad} rejected")
    r = b.cmd("speed 600")
    check(r.startswith("OK speed") and near(ab_hz(r), hz_for(600)), "speed 600 = vmax accepted",
          f"A/B {ab_hz(r)} Hz (expect {hz_for(600):.1f})")
    b.cmd("speed 0")
    b.cmd("vmax 100")
    check(b.cmd("speed 100.01").startswith("ERR") and b.cmd("speed 100").startswith("OK"),
          "vmax 100: speed 100.01 rejected, 100 accepted")
    check(b.cmd("rpm 577").startswith("ERR"), "vmax 100 also limits rpm (577 rpm > 100 km/h)")
    b.cmd("speed 0")
    b.cmd("vmax 600")

    r = b.cmd("speed 100")
    s = parse_speed(r)
    check(r.startswith("OK speed") and s is not None and s["state"] == "steady" and near(ab_hz(r), hz_for(100)),
          "speed 100 (accel 0) -> 1922.16 Hz at once, outputs running", r.split("\n")[0] + f", A/B {ab_hz(r)} Hz")
    check(b.cmd("ppr 100").startswith("ERR stop first"), "ppr change rejected while moving")
    check(b.cmd("save").startswith("ERR stop first"), "save rejected while moving")
    s = parse_speed(b.cmd("speed 0"))
    check(s is not None and s["state"] == "standstill" and "stopped" in b.cmd("status"),
          "speed 0 (accel 0) stops at once, levels held", str(s))

    b.cmd("ppr 100")
    r = b.cmd("speed 100")
    check(near(ab_hz(r), hz_for(100, 100)), "speed 100 at 100 ppr -> 961.1 Hz", f"A/B {ab_hz(r)} Hz")
    b.cmd("speed 0")
    b.cmd("ppr 200")

    # wheel rpm: f = rpm x ppr / 60, so 500 rpm at 200 ppr = 1666.667 Hz = 86.71 km/h
    for bad in ("rpm 1.234", "rpm 15001", "rpm x"):
        check(b.cmd(bad).startswith("ERR"), f"{bad} rejected")
    r = b.cmd("rpm 500")
    s = parse_speed(r)
    m = re.search(r"^wheel ([\d.]+) rpm, target ([\d.]+) rpm", r, re.M)
    check(r.startswith("OK speed") and near(ab_hz(r), 500 * 200 / 60) and s is not None
          and abs(s["now"] - 86.71) < 0.01 and m is not None and float(m.group(1)) == 500.0,
          "rpm 500 at 200 ppr -> 1666.7 Hz, 86.71 km/h", f"A/B {ab_hz(r)} Hz, {s and s['now']} km/h, "
          f"{m and m.group(1)} rpm")
    time.sleep(1.2)
    v = parse_rx(b.cmd("rx"))
    if v is not None and v["fa"] > 0:
        check(abs(v["rpm"] - 500) < 1, "receiver shows the wheel rpm (loopback)", f"{v['rpm']} rpm")
    b.cmd("speed 0")

    # ramp: 5 m/s2 = 18 km/h per second, 0 -> 36 km/h in 2 s
    b.cmd("accel 5")
    r = b.cmd("speed 36")
    t0 = b.sent_at
    s = parse_speed(r)
    check(s is not None and s["state"] == "ramping" and s["target"] == 36.0, "speed 36 with accel 5 ramps", str(s))
    sleep_until(t0 + 1.0)
    s = parse_speed(b.cmd("speed"))
    el = b.sent_at - t0
    check(s is not None and s["state"] == "ramping" and abs(s["now"] - 18 * el) < 3,
          "ramp after about 1 s", f"{s and s['now']} km/h after {el:.2f} s (expect {18 * el:.1f} +/- 3)")
    sleep_until(t0 + 2.6)
    r = b.cmd("speed")
    s = parse_speed(r)
    check(s is not None and s["state"] == "steady" and s["now"] == 36.0 and near(ab_hz(r), hz_for(36)),
          "ramp ends steady at 36 km/h, 692.0 Hz", f"{s}, A/B {ab_hz(r)} Hz")
    time.sleep(1.2)                                  # two receiver windows at the steady speed
    v = parse_rx(b.cmd("rx"))
    if v is not None and v["fa"] > 0:
        check(abs(v["speed"] - 36) < 0.2, "receiver speed matches the speed model (loopback)", f"{v['speed']} km/h")
    # brake at decel 10 m/s2: 36 km/h (10 m/s) -> 0 in 1 s
    b.cmd("decel 10")
    r = b.cmd("brake")
    t0 = b.sent_at
    s = parse_speed(r)
    check(r.startswith("OK speed") and s is not None and s["state"] == "ramping" and s["target"] == 0,
          "brake starts a ramp to 0 at decel", str(s))
    sleep_until(t0 + 0.5)
    s = parse_speed(b.cmd("speed"))
    el = b.sent_at - t0
    check(s is not None and s["state"] == "ramping" and abs(s["now"] - (36 - 36 * el)) < 4,
          "brake at decel 10 after about 0.5 s", f"{s and s['now']} km/h after {el:.2f} s (expect {36 - 36 * el:.1f} +/- 4)")
    sleep_until(t0 + 1.5)
    s = parse_speed(b.cmd("speed"))
    check(s is not None and s["state"] == "standstill" and s["now"] == 0 and s["dist"] > 10
          and "stopped" in b.cmd("status"),
          "brake reaches standstill in 1 s, outputs stopped, distance kept", str(s))

    # top rate: accel 50 m/s2, 0 -> 180 km/h (50 m/s) in 1 s
    b.cmd("accel 50")
    b.cmd("speed 180")
    t0 = b.sent_at
    sleep_until(t0 + 1.4)
    r = b.cmd("speed")
    s = parse_speed(r)
    check(s is not None and s["state"] == "steady" and s["now"] == 180.0 and near(ab_hz(r), hz_for(180)),
          "accel 50: 0 -> 180 km/h within 1.4 s", f"{s}, A/B {ab_hz(r)} Hz")
    b.cmd("decel 0")
    s = parse_speed(b.cmd("brake"))
    check(s is not None and s["state"] == "standstill", "brake with decel 0 stops at once", str(s))

    # settings survive a reset
    for c in ("ppr 123", "wheel 850", "accel 1.25", "decel 2.5", "vmax 450", "dir rev"):
        b.cmd(c)
    r = b.cmd("save")
    check(r.startswith("OK saved"), "save", r.split("\n")[0])
    b.cmd("defaults")
    s = parse_settings(b.cmd("ppr"))
    check(s is not None and (s["ppr"], s["wheel"], s["accel"], s["decel"], s["vmax"], s["dir"])
          == (200, 920, "0.000", "0.000", 600, "fwd") and s["state"].startswith("not saved"),
          "defaults: 200 ppr, 920 mm, accel/decel 0, vmax 600, fwd, not saved", str(s))
    b.cmd("reset")
    if not wait_for_prompt(b.port):
        check(False, "board answers after reset")
        return
    s = parse_settings(b.cmd("ppr"))
    check(s is not None and (s["ppr"], s["wheel"], s["accel"], s["decel"], s["vmax"], s["dir"], s["state"])
          == (123, 850, "1.250", "2.500", 450, "rev", "saved"), "settings loaded from flash after reset", str(s))

    keys = ("ppr", "wheel", "accel", "decel", "vmax", "dir")
    for k in keys:
        b.cmd(f"{k} {orig[k]}")
    r = b.cmd("save")
    s = parse_settings(r)
    check(r.startswith("OK saved") and s is not None and all(s[k] == orig[k] for k in keys),
          "original settings saved back", str(s))


def receiver_checks(b):
    """Stage 4: needs loopback wires J8.6 -> J8.2 and J8.7 -> J8.3 (skipped without them)."""
    b.cmd("stop low")
    b.cmd("dir fwd")
    b.cmd("start 1000")
    time.sleep(1.6)
    v = parse_rx(b.cmd("rx"))
    if v is None:
        check(False, "rx reply parses")
        return
    if v["fa"] == 0 and v["fb"] == 0:
        print("[SKIP] receiver checks: no signal on J8.2/J8.3 (loopback wires J8.6->J8.2, J8.7->J8.3 not fitted)")
        b.cmd("stop low")
        return
    st = parse_settings(b.cmd("ppr")) or {"ppr": 200, "wheel": 920}
    kmh = 1000 / hz_for(1, st["ppr"], st["wheel"])                # 52.02 km/h at 1000 Hz, 200 ppr, 920 mm
    check(abs(v["fa"] - 1000) < 0.5 and abs(v["fb"] - 1000) < 0.5, "rx frequency A/B at 1000 Hz",
          f"A {v['fa']} Hz, B {v['fb']} Hz (same clock as the outputs: expect 1000 +/- 0.5)")
    check(49.5 <= v["da"] <= 50.5 and 49.5 <= v["db"] <= 50.5, "rx duty A/B 50 %",
          f"A {v['da']} %, B {v['db']} %")
    check(v["phase"] is not None and 89 <= v["phase"] <= 91 and v["dir"] == "forward",
          "rx phase forward 90 deg", f"{v['phase']} deg {v['dir']}")
    check(v["status"] == "OK", "rx status OK", v["status"])
    check(abs(v["speed"] - kmh) < kmh * 0.005, "rx speed at 1000 Hz", f"{v['speed']} km/h (expect {kmh:.2f})")

    b.cmd("rx reset")
    p0 = parse_rx(b.cmd("rx"))["pulses"]
    time.sleep(2.0)
    p1 = parse_rx(b.cmd("rx"))["pulses"]
    check(p1 - p0 > 1500, "rx distance counts up forward", f"{p1 - p0} pulses in about 2 s")

    b.cmd("stop")
    b.cmd("dir rev")
    b.cmd("start 1000")
    time.sleep(1.6)
    v = parse_rx(b.cmd("rx"))
    check(v["phase"] is not None and 269 <= v["phase"] <= 271 and v["dir"] == "reverse",
          "rx phase reverse 270 deg", f"{v['phase']} deg {v['dir']}")
    check(v["status"] == "OK", "rx status OK in reverse", v["status"])
    q0 = v["pulses"]
    time.sleep(1.0)
    q1 = parse_rx(b.cmd("rx"))["pulses"]
    check(q1 < q0, "rx distance counts down in reverse", f"{q0} -> {q1}")

    b.cmd("stop")
    b.cmd("dir fwd")
    b.cmd("start 1")
    time.sleep(3.2)
    v = parse_rx(b.cmd("rx"))
    check(abs(v["fa"] - 1) < 0.01, "rx low speed: 1 Hz", f"A {v['fa']} Hz, speed {v['speed']} km/h")

    b.cmd("stop")
    time.sleep(2.8)
    v = parse_rx(b.cmd("rx"))
    check(v["status"] == "standstill" and v["speed"] == 0, "rx standstill 2 s after stop",
          f"{v['status']}, {v['speed']} km/h")

    # high frequencies: 10 kHz (the real OPG's top, 520 km/h) and 50 kHz (the emulator's top)
    for hz in (10000, 50000):
        b.cmd("stop low")
        lost0 = parse_rx(b.cmd("rx"))["lost"]
        b.cmd(f"start {hz}")
        time.sleep(1.6)
        v = parse_rx(b.cmd("rx"))
        if v is None:
            check(False, f"rx reply parses at {hz} Hz")
            continue
        check(abs(v["fa"] - hz) <= hz * 1e-4 and abs(v["fb"] - hz) <= hz * 1e-4, f"rx frequency A/B at {hz} Hz",
              f"A {v['fa']} Hz, B {v['fb']} Hz (expect {hz} +/- 0.01 %)")
        check(49 <= v["da"] <= 51 and 49 <= v["db"] <= 51 and v["phase"] is not None and 88 <= v["phase"] <= 92
              and v["status"] == "OK", f"rx duty, phase, status at {hz} Hz",
              f"duty A {v['da']} %, B {v['db']} %, phase {v['phase']} deg, {v['status']}")
        check(abs(v["speed"] - hz * kmh / 1000) < hz * kmh / 1000 * 1e-3, f"rx speed at {hz} Hz",
              f"{v['speed']} km/h (expect {hz * kmh / 1000:.2f})")
        check(v["lost"] == lost0, f"rx no lost edges at {hz} Hz", f"lost edges {lost0} -> {v['lost']}")
        b.cmd("stop low")

    b.cmd("start 1000")
    b.port.write(b"monitor\r")
    time.sleep(1.8)
    b.port.write(b"\r")
    out = read_reply(b.port)
    lines = [ln for ln in out.split("\n") if re.match(r"^ \d+\.\d\d ", ln)]
    check(len(lines) >= 2 and "monitor stopped" in out and all(ln.rstrip().endswith("OK") for ln in lines),
          "monitor prints lines and stops on Enter", f"{len(lines)} lines" + (f", last: {lines[-1].strip()}" if lines else ""))
    b.cmd("stop low")


def fault_checks(b):
    """Sensor fault emulation. The receiver part needs the loopback wires (skipped without them)."""
    b.cmd("stop low")
    b.cmd("fault clear")
    check(b.cmd("fault").startswith("faults: none"), "fault: none after fault clear")
    for bad in ("fault c low", "fault a stuck", "fault phase 135", "fault glitch a 0", "fault glitch a 1001",
                "fault glitch a 10 51", "fault glitch c 10", "fault drop a 1", "fault drop a 10001",
                "fault slip 20 1", "fault slip -91 1"):
        check(b.cmd(bad).startswith("ERR"), f"{bad} rejected")

    for c in ("dir fwd", "accel 0", "decel 0"):
        b.cmd(c)
    b.cmd("start 1000")
    time.sleep(1.6)
    v = parse_rx(b.cmd("rx"))
    if v is None or v["fa"] == 0:
        print("[SKIP] fault checks with the receiver: no loopback wires")
        b.cmd("stop low")
        return

    def rx_after(cmd, wait):
        r = b.cmd(cmd)
        time.sleep(wait)
        return r, parse_rx(b.cmd("rx"))

    # stuck channels
    for ch, lvl in (("a", "low"), ("b", "high")):
        r, v = rx_after(f"fault {ch} {lvl}", 2.6)
        check(r.startswith("OK faults:") and f"{ch.upper()} stuck {lvl}" in r and
              v["status"] == f"FAULT {ch.upper()} missing", f"fault {ch} {lvl} -> receiver FAULT {ch.upper()} missing",
              v["status"])
        r, v = rx_after(f"fault {ch} ok", 1.6)
        check(v["status"] == "OK" and v["phase"] is not None and 89 <= v["phase"] <= 91,
              f"fault {ch} ok -> back in step, 90 deg", f"{v['status']}, {v['phase']} deg")

    # phase
    r, v = rx_after("fault phase 45", 1.6)
    check(v["phase"] is not None and 44 <= v["phase"] <= 46 and v["status"] == "FAULT phase",
          "fault phase 45 -> receiver 45 deg, FAULT phase", f"{v['phase']} deg, {v['status']}")
    r, v = rx_after("freq 2000", 1.6)
    check(v["phase"] is not None and 44 <= v["phase"] <= 46 and abs(v["fa"] - 2000) < 1,
          "phase 45 kept after freq 1000 -> 2000", f"{v['fa']} Hz, {v['phase']} deg")
    b.cmd("freq 1000")
    r, v = rx_after("fault phase 100", 1.6)
    check(v["phase"] is not None and 99 <= v["phase"] <= 101 and v["status"] == "OK",
          "fault phase 100 -> receiver 100 deg, OK (inside 90 +/- 20)", f"{v['phase']} deg, {v['status']}")
    r, v = rx_after("fault phase ok", 1.6)
    check(v["phase"] is not None and 89 <= v["phase"] <= 91, "fault phase ok -> 90 deg", f"{v['phase']} deg")

    # dropped pulses
    r, v = rx_after("fault drop a 10", 1.6)
    check(abs(v["fa"] - 900) < 5 and abs(v["fb"] - 1000) < 1, "fault drop a 10 -> A 900 Hz, B 1000 Hz",
          f"A {v['fa']} Hz, B {v['fb']} Hz")
    r, v = rx_after("fault drop b 4", 1.6)
    check(abs(v["fb"] - 750) < 5, "fault drop b 4 -> B 750 Hz", f"A {v['fa']} Hz, B {v['fb']} Hz")
    check(re.search(r"drop A 1 in 10 \((\d+) dropped\)", b.cmd("fault")) is not None, "fault shows the dropped count")
    r, v = rx_after("fault drop off", 1.6)
    check(abs(v["fa"] - 1000) < 1 and abs(v["fb"] - 1000) < 1 and v["status"] == "OK",
          "fault drop off -> A/B 1000 Hz, OK", f"A {v['fa']} Hz, B {v['fb']} Hz, {v['status']}")

    # glitches: each one adds a rising edge on that input
    r, v = rx_after("fault glitch a 100", 1.6)
    check(abs(v["fa"] - 1100) < 15 and abs(v["fb"] - 1000) < 1, "fault glitch a 100 -> A about 1100 Hz",
          f"A {v['fa']} Hz, B {v['fb']} Hz")
    r, v = rx_after("fault glitch off", 1.6)
    check(abs(v["fa"] - 1000) < 1, "fault glitch off -> A 1000 Hz", f"A {v['fa']} Hz")

    # wheel slip and slide: the receiver sees the wheel, the speed model keeps the train speed
    b.cmd("speed 100")
    time.sleep(1.2)
    r = b.cmd("fault slip 20 3")
    t0 = b.sent_at
    sleep_until(t0 + 1.6)
    v = parse_rx(b.cmd("rx"))
    s = parse_speed(b.cmd("speed"))
    check(abs(v["speed"] - 120) < 0.5 and s is not None and s["now"] == 100.0,
          "fault slip 20 -> receiver 120 km/h, train 100 km/h", f"rx {v['speed']} km/h, model {s and s['now']} km/h")
    sleep_until(t0 + 4.2)
    v = parse_rx(b.cmd("rx"))
    check(abs(v["speed"] - 100) < 0.5 and "slip +" not in b.cmd("fault"), "slip ends after 3 s -> 100 km/h",
          f"rx {v['speed']} km/h")
    r, v = rx_after("fault slip -30 5", 1.6)
    check(abs(v["speed"] - 70) < 0.5, "fault slip -30 (slide) -> receiver 70 km/h", f"rx {v['speed']} km/h")

    b.cmd("fault clear")
    time.sleep(1.6)
    v = parse_rx(b.cmd("rx"))
    check(b.cmd("fault").startswith("faults: none") and abs(v["speed"] - 100) < 0.5 and v["status"] == "OK",
          "fault clear -> healthy, 100 km/h, OK", f"{v['speed']} km/h, {v['status']}")
    b.cmd("stop low")


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

        speed_checks(b)
        receiver_checks(b)
        fault_checks(b)

    failed = results.count(False)
    print(f"\n{'ALL CHECKS PASSED' if not failed else f'FAILED: {failed} of {len(results)} checks'}"
          " (output left stopped, low)")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
