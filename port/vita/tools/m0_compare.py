#!/usr/bin/env python3
"""Online play, M0: two state logs (BT3_HASH=<file>: the PC's port/src/gs/state.c, the Vita's port/vita/vita_m0.c)
compared fight value by fight value: the first blank at which they part, and in which value.

    m0_compare.py A.txt B.txt [--offset N] [--all] [--context K]

A line: "<blank> <memory checksum> <fight checksum> [hp H1 H2 pos X1 Y1 Z1  X2 Y2 Z2 clock C rnd R] [pad P]". The memory
checksum is never compared (the two programs' memory differs by construction; the Vita writes 0). The fight lines are
lined up from each file's first one (the blanks before a fight can differ: start-up, loading), or with --offset N
(B's blank = A's blank + N). --all lists every differing line, not only the first. Exit status: 0 the same, 1 they
part, 2 a file has no fight line.
"""
import argparse
import struct
import sys

FIELDS = ["hp1", "hp2", "x1", "y1", "z1", "x2", "y2", "z2", "clock", "rnd"]


def parse(path):
    """[(blank, values tuple or None, pad or None)] for every line; values: hp1 hp2 x1..z2 (bit patterns) clock rnd."""
    out = []
    with open(path, encoding="latin-1") as fp:
        for line in fp:
            p = line.split()
            if len(p) < 3 or not p[0].isdigit():
                continue
            blank = int(p[0])
            vals = pad = None
            if "hp" in p:
                i = p.index("hp")
                try:
                    vals = (int(p[i + 1]), int(p[i + 2]), *[int(x, 16) for x in p[i + 4:i + 10]], int(p[i + 11]), int(p[i + 13], 16))
                except (ValueError, IndexError):
                    vals = None
            if "pad" in p:
                pad = int(p[p.index("pad") + 1])
            out.append((blank, vals, pad))
    return out


def fbits(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


def show(name, v):
    if name.startswith(("x", "y", "z")):
        return f"{v:08x} ({fbits(v):.6g})"
    if name == "rnd":
        return f"{v:016x}"
    return str(v)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--offset", type=int, default=None)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--context", type=int, default=2)
    o = ap.parse_args()
    A, B = parse(o.a), parse(o.b)
    fa = [r for r in A if r[1] is not None]
    fb = [r for r in B if r[1] is not None]
    print(f"{o.a}: {len(A)} blanks, {len(fa)} with a fight" + (f" (blanks {fa[0][0]}..{fa[-1][0]})" if fa else ""))
    print(f"{o.b}: {len(B)} blanks, {len(fb)} with a fight" + (f" (blanks {fb[0][0]}..{fb[-1][0]})" if fb else ""))
    if not fa or not fb:
        print("no fight line in one of the files: nothing to compare")
        return 2
    off = o.offset if o.offset is not None else fb[0][0] - fa[0][0]
    print(f"lined up: B's blank = A's blank {'+' if off >= 0 else '-'} {abs(off)}"
          + (" (each file's first fight line)" if o.offset is None else ""))
    byb = {r[0]: r for r in fb}
    same = parted = 0
    first = None
    for ra in fa:
        rb = byb.get(ra[0] + off)
        if rb is None:
            continue
        if ra[1] == rb[1]:
            same += 1
            continue
        parted += 1
        diff = [n for n, x, y in zip(FIELDS, ra[1], rb[1]) if x != y]
        if first is None:
            first = (ra, rb, diff)
        if o.all or parted == 1:
            print(f"blank A {ra[0]} / B {rb[0]}: " + ", ".join(f"{n} {show(n, ra[1][FIELDS.index(n)])} / {show(n, rb[1][FIELDS.index(n)])}" for n in diff))
    common = same + parted
    print(f"{common} fight blanks in both: {same} the same, {parted} differ")
    if first is not None:
        ra, rb, diff = first
        print(f"FIRST DIFFERENCE at A's blank {ra[0]} (B's {rb[0]}), clock {ra[1][8]}: {', '.join(diff)}")
        k = fa.index(ra)
        for r in fa[max(0, k - o.context):k + o.context + 1]:
            r2 = byb.get(r[0] + off)
            print(f"  A {r[0]:7d} {' '.join(show(n, v) for n, v in zip(FIELDS, r[1]))}")
            if r2 is not None:
                print(f"  B {r2[0]:7d} {' '.join(show(n, v) for n, v in zip(FIELDS, r2[1]))}")
        return 1
    print("the same in every fight blank both have")
    return 0


if __name__ == "__main__":
    sys.exit(main())
