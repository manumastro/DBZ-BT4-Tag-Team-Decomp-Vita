#!/usr/bin/env python3
"""The Vita's list of the GAME's own variables (port/tools/make_state.py is the PC's), written into the program.

An online session inside the running game (port/vita/vita_session.c, bt3.txt session_inproc) keeps the game's state
and puts it back: its heap, the blocks it took outside the heap, the game thread's stack and registers, and its
global variables. These lie in the program's .data and .bss between the port's own (the renderer's, the sound's,
the network's), which are not the game's state. This reads the linker's map (port/build/vita_*/bt3.map, GNU ld's)
and lists the address ranges of the game's objects (everything in the object folder but the port's own files: pc_*
and gs_*, except the few that hold what the game goes through, PORT_STATE), merged where they touch.

The list is written into the linked program itself (gVitaStateList, vita_session.c: a constant table of fixed size,
so filling it moves nothing), not into a file beside it: a list of another build cannot be paired with this one.
A Vita program is loaded where the system chooses, so the table holds link addresses and two marks (the link
addresses of gPortVBlanks and gPortResim); vita_session.c turns them into run-time ones by the distance of the first
mark, and checks the second.

Usage: vita_state_list.py <bt3.map> <bt3.elf>   (build.py link, BT4_VITA_NETLINK=1 builds)"""
import bisect, itertools, pathlib, re, struct, sys

# the port's own objects that are part of what the game goes through (as make_state.py PORT_STATE, with the Vita's
# names): the vector unit's registers, the C library generator, the memory layer (the heap's pointer, the emulated
# hardware registers), the memory card layer, the blank count, the sound players' view; and BT4decomp's game code
# built from port/src (the Z counter, the gameplay rules, the bombardment, the fight's panels)
PORT_STATE = {"headless", "mathf_pc", "plat_mc", "plat_sndstate", "plat_stub", "plat_sys", "softfloat_ps2", "vu0_a", "vu0_b",
              "vita_mem", "bt3_zcounter2", "bt3_gameplay_rules", "bt3_bombardment", "fight_panels"}
WRITABLE = (".data", ".bss")
MAGIC = 0x314C5356  # "VSL1"
CAP = 8192          # ranges the table holds (vita_session.c VITA_STATE_CAP)


def is_game(obj):
    p = pathlib.PurePosixPath(obj.split("(")[0])
    if "(" in obj or not p.parent.name.startswith("obj_vita"):
        return False  # libraries, the SDK's start files
    if p.name == "menu_port_language.o":
        return False  # (as make_state.py: the start's language window)
    if p.name.startswith("gs_"):
        return False
    if p.name.startswith("pc_"):
        return p.stem[3:] in PORT_STATE
    return True


def ranges_of(text):
    text = text[text.index("Linker script and memory map"):]
    out, game, port = None, [], []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        l = lines[i]
        i += 1
        m = re.match(r"^(\.[A-Za-z0-9_.]+|COMMON)\b", l)
        if m and not l.startswith(" "):
            out = m.group(1)
            continue
        if not l.startswith(" ") or l.startswith("  "):
            continue
        parts = l.split()
        if len(parts) == 1 and i < len(lines):
            parts += lines[i].split()
            i += 1
        if len(parts) < 4 or not parts[1].startswith("0x") or not parts[2].startswith("0x"):
            continue
        if out is None or not out.startswith(WRITABLE) or out.startswith(".data.rel.ro"):
            continue
        addr, size, obj = int(parts[1], 16), int(parts[2], 16), " ".join(parts[3:])
        if size and addr:
            (game if is_game(obj) else port).append((addr, addr + size))
    return game, port, text


def merge(ranges, others):
    ranges.sort()
    others.sort()
    starts = [x for x, _ in others]
    reach = list(itertools.accumulate((y for _, y in others), max))
    merged = []
    for a, b in ranges:
        # joined over an alignment gap, never over anything of the port's that lies in it (make_state.py)
        if merged and a <= merged[-1][1] + 64 and not ((n := bisect.bisect_left(starts, a)) and reach[n - 1] > merged[-1][1]):
            merged[-1][1] = max(merged[-1][1], b)
        else:
            merged.append([a, b])
    return merged


def symbol(text, name):
    m = re.search(r"^\s+0x([0-9a-fA-F]+)\s+" + name + r"\s*$", text, re.M)
    if m is None:
        sys.exit(f"vita_state_list: {name} is not in the map")
    return int(m.group(1), 16)


def patch_elf(elf, addr, data):
    b = bytearray(elf.read_bytes())
    shoff, = struct.unpack_from("<I", b, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", b, 0x2E)
    for k in range(shnum):
        name, typ, flags, sa, so, size = struct.unpack_from("<IIIIII", b, shoff + k * shentsize)
        if typ == 1 and sa <= addr and addr + len(data) <= sa + size:  # PROGBITS holding the table
            at = so + (addr - sa)
            if struct.unpack_from("<I", b, at)[0] != MAGIC:
                sys.exit("vita_state_list: gVitaStateList does not start with its mark in the program")
            b[at:at + len(data)] = data
            elf.write_bytes(bytes(b))
            return
    sys.exit("vita_state_list: no section holds gVitaStateList")


def main():
    mp, elf = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    game, port, text = ranges_of(mp.read_text(errors="replace"))
    merged = merge(game, port)
    if len(merged) > CAP:
        sys.exit(f"vita_state_list: {len(merged)} ranges, the table holds {CAP} (VITA_STATE_CAP)")
    words = [MAGIC, len(merged), symbol(text, "gPortVBlanks"), symbol(text, "gPortResim")]
    for a, b in merged:
        words += [a, b - a]
    patch_elf(elf, symbol(text, "gVitaStateList"), struct.pack(f"<{len(words)}I", *words))
    print(f"vita_state_list: {len(merged)} ranges of game variables, {sum(b - a for a, b in merged):,} bytes, into {elf.name}")


if __name__ == "__main__":
    main()
