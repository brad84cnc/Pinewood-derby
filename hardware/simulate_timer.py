#!/usr/bin/env python3
"""Pretend to be the Arduino derby timer, for testing without hardware.

Speaks the same line protocol as derby_timer.ino (see GUIDE.md):
  commands in : ARM, RELEASE, RESET, PING, TIMEOUT <ms>
  replies out : READY, ARMED, GO, RESULT <t1> <t2> <t3>, PONG, OK, ERR <why>

Usage:
  python3 simulate_timer.py            # type commands, see replies
  python3 simulate_timer.py --selftest # run a few checks and exit

Times are random (about 2.6 - 3.2 s); roughly 1 race in 15 has a DNF lane.
"""
import random
import sys


class SimTimer:
    def __init__(self, rng=None):
        self.rng = rng or random.Random()
        self.state = "IDLE"
        self.timeout_ms = 10000

    def handle(self, line):
        """Return the list of reply lines for one command line."""
        cmd = line.strip().upper()
        if cmd == "PING":
            return ["PONG"]
        if cmd == "RESET":
            self.state = "IDLE"
            return ["READY"]
        if cmd == "ARM":
            if self.state in ("ARMED", "RUNNING"):
                return ["ERR BUSY"]
            self.state = "ARMED"
            return ["ARMED"]
        if cmd == "RELEASE":
            if self.state != "ARMED":
                return ["ERR NOTARMED"]
            self.state = "DONE"
            return ["GO", self._result()]
        if cmd.startswith("TIMEOUT "):
            try:
                v = int(cmd.split()[1])
            except ValueError:
                return ["ERR RANGE"]
            if 1000 <= v <= 99000:
                self.timeout_ms = v
                return ["OK"]
            return ["ERR RANGE"]
        return ["ERR UNKNOWN"] if cmd else []

    def _result(self):
        base = self.rng.uniform(2.6, 3.0)
        parts = []
        for _ in range(3):
            if self.rng.random() < 1 / 45:          # about 1 race in 15 has a DNF
                parts.append("DNF")
            else:
                parts.append(f"{base + self.rng.uniform(0, 0.25):.4f}")
        return "RESULT " + " ".join(parts)


def parse_result(line):
    """RESULT line -> list of 3 floats (seconds) or 'DNF'. Raises ValueError if malformed."""
    parts = line.split()
    if len(parts) != 4 or parts[0] != "RESULT":
        raise ValueError(line)
    return ["DNF" if p == "DNF" else float(p) for p in parts[1:]]


def selftest():
    t = SimTimer(random.Random(1))
    assert t.handle("PING") == ["PONG"]
    assert t.handle("RELEASE") == ["ERR NOTARMED"]
    assert t.handle("ARM") == ["ARMED"]
    assert t.handle("ARM") == ["ERR BUSY"]
    out = t.handle("RELEASE")
    assert out[0] == "GO" and len(parse_result(out[1])) == 3
    assert t.handle("RESET") == ["READY"]
    assert t.handle("TIMEOUT 5000") == ["OK"] and t.handle("TIMEOUT 5") == ["ERR RANGE"]
    assert t.handle("nonsense") == ["ERR UNKNOWN"]
    assert parse_result("RESULT 2.5000 DNF 3.1234") == [2.5, "DNF", 3.1234]
    seen_dnf = False
    for _ in range(300):
        t.handle("ARM")
        seen_dnf |= "DNF" in t.handle("RELEASE")[1]
        t.handle("RESET")
    assert seen_dnf
    print("selftest passed")


def main():
    if "--selftest" in sys.argv:
        selftest()
        return
    t = SimTimer()
    print("READY", flush=True)
    for line in sys.stdin:
        for reply in t.handle(line):
            print(reply, flush=True)


if __name__ == "__main__":
    main()
