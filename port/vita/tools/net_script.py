#!/usr/bin/env python3
"""A pad table for net.c's BT3_NET_SCRIPT (online play tests without hands: docs/vita/online-bt4.md "1 vs 1 by address").

net.c reads, at each blank of a session, its own player's 18 bytes from <file> at offset blank * 36 (pad 1, pad 2:
BT3_PAD_TABLE's format). The two copies of a match each get one table: each plays its own player from it, the other
player comes over the line. This writes a table of made-up but repeatable play (a seed):

  - nothing pressed for the first --quiet blanks (the session's start-up ignores the pads anyway: Port_NetWarp);
  - then short presses of CROSS (confirm at the character and stage selects; a melee attack in the fight), SQUARE
    (ki blasts), held directions (left stick and d-pad: moving the select cursors, moving in the fight), R1 (dash),
    L1 (guard) and idle stretches.

Never pressed: START, SELECT (the pause menu could leave the match), CIRCLE and TRIANGLE (going back from a select
ends the session: headless.c / Duel_Main). PS2 pad record (18 bytes): two button bytes, active low (byte 0: SELECT
0x01, L3 0x02, R3 0x04, START 0x08, UP 0x10, RIGHT 0x20, DOWN 0x40, LEFT 0x80; byte 1: L2 0x01, R2 0x02, L1 0x04,
R1 0x08, TRIANGLE 0x10, CIRCLE 0x20, CROSS 0x40, SQUARE 0x80), then the sticks (right x, right y, left x, left y;
0x80 = centre), then 12 pressure bytes (0 here, as net.c's own idle record kIdle).

  net_script.py <out> [--blanks N] [--seed S] [--quiet N]
"""
import argparse
import random
import struct

PAD = 18
UP, RIGHT, DOWN, LEFT = 0x10, 0x20, 0x40, 0x80          # byte 0
L1, R1, CROSS, SQUARE = 0x04, 0x08, 0x40, 0x80          # byte 1


def record(b0=0, b1=0, lx=0x80, ly=0x80):
    return bytes([0xFF & ~b0, 0xFF & ~b1, 0x80, 0x80, lx, ly]) + bytes(PAD - 6)


IDLE = record()


def play(rng, blanks, quiet):
    out = [IDLE] * min(quiet, blanks)
    while len(out) < blanks:
        kind = rng.choice(["cross", "cross", "square", "dir", "dir", "dirs", "dash", "guard", "idle"])
        if kind == "cross" or kind == "square":
            press = record(b1=CROSS if kind == "cross" else SQUARE)
            seg = [press] * 3 + [IDLE] * rng.randint(3, 12)
        elif kind in ("dir", "dirs"):
            d = rng.choice([(UP, 0x80, 0x00), (DOWN, 0x80, 0xFF), (LEFT, 0x00, 0x80), (RIGHT, 0xFF, 0x80)])
            hold = rng.randint(4, 40)
            seg = [record(b0=d[0], lx=d[1], ly=d[2])] * hold
            if kind == "dirs":  # moving and hitting
                seg += [record(b0=d[0], b1=CROSS, lx=d[1], ly=d[2])] * 3
            seg += [IDLE] * 2
        elif kind == "dash":
            seg = [record(b1=R1)] * 3 + [IDLE] * rng.randint(4, 20)
        elif kind == "guard":
            seg = [record(b1=L1)] * rng.randint(10, 50) + [IDLE] * 2
        else:
            seg = [IDLE] * rng.randint(5, 40)
        out += seg
    return out[:blanks]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--blanks", type=int, default=60 * 60 * 10, help="table length (default 10 minutes)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--quiet", type=int, default=600, help="blanks with nothing pressed at the start")
    a = ap.parse_args()
    p1 = play(random.Random(a.seed * 2 + 1), a.blanks, a.quiet)
    p2 = play(random.Random(a.seed * 2 + 2), a.blanks, a.quiet)
    with open(a.out, "wb") as f:
        for x, y in zip(p1, p2):
            f.write(x + y)
    print(f"{a.out}: {a.blanks} blanks x 36 bytes, seed {a.seed}")


if __name__ == "__main__":
    main()
