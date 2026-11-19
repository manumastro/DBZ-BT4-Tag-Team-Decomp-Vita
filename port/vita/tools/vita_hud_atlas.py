#!/usr/bin/env python3
"""The Tag Team fight HUD's shapes as one small texture made at build time, for bt3.txt taghudvita=1.

The PC's Tag Team HUD (src/battle/tag_team_hud.inc) draws its radar, the target's rings, the controller glyphs as
thin vector strokes, sized for a big monitor. On the Vita a fight is drawn at 640 x 368: the strokes were under a
pixel (broken, grainy) and the radar alone was ~150 triangles a picture. The game's own HUD does it the other way:
textured sprites, authored at the size they are shown, resident. So here each shape is drawn once, antialiased, at
the pixel size it has in the 640 x 368 picture, packed in one atlas; the HUD draws each as one textured quad
(port/vita/vita_tbo.c TBO_VITA_HUD, the UV table in port/vita/vita_hud_atlas.h written by this script).

The shapes are white (or white with a dark edge) so the HUD's own colours tint them as the PC colours its strokes
(gold target, cyan ally focus, the radar's contacts by side). Shapes meant for the menus too can be added the same way
(an element = a name, a box, a draw function).

    vita_hud_atlas.py <output folder> [--png file] [--header file]
        writes <output folder>/tag_hud_vita.vt ("VTX1", w, h, flags 0, then RGBA with the GS's alpha 0..0x80)

Only the standard library (the build image has no imaging module).
"""
import math
import os
import struct
import sys
import zlib

W, H = 128, 112
SS = 4  # samples a side per pixel (16 per pixel)

# the radar's face (TagHud_Radar: radius 29 page units = ~23.5 pixels of the 640 x 368 picture), the target's rings
# in three sizes (TagHud_Target: inner radius 10..32 units, a second thin ring 3 units out), the radar's contacts,
# the stick and shoulder glyphs (TagHud_Key)
RADAR_R = 23.5
RINGS = {"ring_l": 26.0, "ring_m": 17.0, "ring_s": 10.5}


def disc(r):
    return lambda x, y: x * x + y * y <= r * r


def annulus(r0, r1):
    return lambda x, y: r0 * r0 <= x * x + y * y <= r1 * r1


def tri(ax, ay, bx, by, cx, cy):
    def f(x, y):
        d1 = (x - bx) * (ay - by) - (ax - bx) * (y - by)
        d2 = (x - cx) * (by - cy) - (bx - cx) * (y - cy)
        d3 = (x - ax) * (cy - ay) - (cx - ax) * (y - ay)
        return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))
    return f


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[0.0, 0.0, 0.0, 0.0] for _ in range(w * h)]  # premultiplied floats, alpha 0..1

    def fill(self, box, shape, rgb, alpha=1.0):
        """`shape(x, y)` in pixels from the box's centre; painted over what is there (source over)"""
        bx, by, bw, bh = box
        cx, cy = bw / 2.0, bh / 2.0
        for j in range(bh):
            for i in range(bw):
                n = 0
                for sj in range(SS):
                    for si in range(SS):
                        if shape(i + (si + 0.5) / SS - cx, j + (sj + 0.5) / SS - cy):
                            n += 1
                if n == 0:
                    continue
                a = alpha * n / (SS * SS)
                p = self.px[(by + j) * self.w + bx + i]
                for k in range(3):
                    p[k] = rgb[k] / 255.0 * a + p[k] * (1 - a)
                p[3] = a + p[3] * (1 - a)

    def rgba(self, gs):
        """straight RGBA bytes; with gs the alpha 0..0x80 (the GS's); fully clear pixels take the colour of the nearest
        painted one in their row or column (no dark fringe under the filter)"""
        out = bytearray(self.w * self.h * 4)
        for idx, p in enumerate(self.px):
            a = p[3]
            if a > 0:
                c = [min(255, round(p[k] / a * 255)) for k in range(3)]
            else:
                c = [255, 255, 255]
            out[idx * 4:idx * 4 + 3] = bytes(c)
            out[idx * 4 + 3] = round(a * 128) if gs else round(a * 255)
        return out


WHITE, DARK = (255, 255, 255), (6, 14, 22)


def make():
    """the atlas: (Canvas, {name: (x, y, w, h)})"""
    cv = Canvas(W, H)
    boxes = {}

    # the radar: its dark green face, the grid (5 lines each way, 9 units = ~7.4 pixels apart), the rim. Coloured here
    # (it is drawn untinted), as the PC's colours: face (4,37,17) at 0x65, grid (109,205,122) at 0x28, rim (221,244,218)
    b = boxes["radar"] = (0, 0, 48, 48)
    r = RADAR_R
    cv.fill(b, disc(r), (4, 37, 17), 0x65 / 128.0)
    for i in range(-2, 3):
        o = i * 7.4
        cv.fill(b, lambda x, y, o=o: abs(y - o) <= 0.5 and x * x + y * y <= (r - 0.5) ** 2, (109, 205, 122), 0.45)
        cv.fill(b, lambda x, y, o=o: abs(x - o) <= 0.5 and x * x + y * y <= (r - 0.5) ** 2, (109, 205, 122), 0.45)
    cv.fill(b, annulus(r - 1.2, r), (221, 244, 218), 1.0)

    # the target's rings: a dark edge under both for contrast on any ground, the inner ring 1.7 pixels, the outer 1.0
    for name, (bw, ri) in zip(("ring_l", "ring_m", "ring_s"), ((64, RINGS["ring_l"]), (44, RINGS["ring_m"]), (30, RINGS["ring_s"]))):
        bx, by = (48, 0) if name == "ring_l" else (0, 64) if name == "ring_m" else (44, 64)
        b = boxes[name] = (bx, by, bw, bw)
        ro = ri + 2.6
        cv.fill(b, annulus(ri - 1.6, ri + 1.6), DARK, 0.55)
        cv.fill(b, annulus(ro - 1.1, ro + 1.1), DARK, 0.45)
        cv.fill(b, annulus(ri - 0.85, ri + 0.85), WHITE, 1.0)
        cv.fill(b, annulus(ro - 0.5, ro + 0.5), WHITE, 0.9)

    # a radar contact: a triangle pointing up, white with a dark edge (TagHud_Arrow 2.1 / 2.7 units: 5-7 pixels)
    b = boxes["contact"] = (112, 0, 9, 9)
    cv.fill(b, tri(0, -4.4, 4.0, 3.4, -4.0, 3.4), DARK, 0.85)
    cv.fill(b, tri(0, -3.0, 2.8, 2.5, -2.8, 2.5), WHITE, 1.0)

    # a dot (the revival's mark, spare for the menus)
    b = boxes["dot"] = (112, 10, 8, 8)
    cv.fill(b, disc(3.6), DARK, 0.8)
    cv.fill(b, disc(2.6), WHITE, 1.0)

    # the stick's glyph: a ring of 7 units (~6 pixels) with a dark edge (TagHud_Key, stick)
    b = boxes["stick"] = (74, 64, 16, 16)
    cv.fill(b, annulus(4.3, 7.4), DARK, 0.6)
    cv.fill(b, annulus(5.1, 6.6), WHITE, 1.0)

    # a shoulder button's cap: a dark parallelogram with white edges top and bottom (TagHud_Key, not stick)
    b = boxes["key"] = (90, 64, 18, 15)
    cv.fill(b, lambda x, y: abs(y) <= 6.0 and abs(x + y * 0.07) <= 7.6, DARK, 0.62)
    cv.fill(b, lambda x, y: 5.0 <= abs(y) <= 6.2 and abs(x + y * 0.07) <= 7.6, WHITE, 1.0)

    # the off-screen arrow (spare: the HUD draws it as a filled triangle, it turns)
    b = boxes["arrow"] = (108, 64, 16, 16)
    cv.fill(b, tri(0, -7.4, 6.4, 5.6, -6.4, 5.6), DARK, 0.8)
    cv.fill(b, tri(0, -5.6, 4.8, 4.4, -4.8, 4.4), WHITE, 1.0)

    # a small bar's frame for the target's health (spare: drawn as rectangles now)
    b = boxes["bar"] = (0, 108, 64, 4)
    cv.fill(b, lambda x, y: True, DARK, 0.6)
    return cv, boxes


def png_write(path, w, h, data):
    raw = b"".join(b"\0" + bytes(data[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def header(boxes):
    lines = ["/* Written by port/vita/tools/vita_hud_atlas.py: the Tag Team HUD's atlas (tbo/ui/tag_hud_vita.vt), its",
             "   elements' boxes in pixels. Do not edit; run the script with --header. */",
             "#ifndef VITA_HUD_ATLAS_H", "#define VITA_HUD_ATLAS_H",
             "#define TBO_VITA_HUD 0x240 /* its picture number (TEX0's CBP with TBO_PSM, include/menu/tbo_art.h: free) */",
             f"#define VHA_W {W}", f"#define VHA_H {H}"]
    for name, (x, y, w, h) in boxes.items():
        lines.append(f"#define VHA_{name.upper()} {x}, {y}, {w}, {h}")
    lines += ["#endif", ""]
    return "\n".join(lines)


def main():
    out = sys.argv[1]
    png = hdr = None
    args = sys.argv[2:]
    while args:
        if args[0] == "--png":
            png, args = args[1], args[2:]
        elif args[0] == "--header":
            hdr, args = args[1], args[2:]
        else:
            raise SystemExit("vita_hud_atlas.py <out> [--png f] [--header f]")
    os.makedirs(out, exist_ok=True)
    vt = os.path.join(out, "tag_hud_vita.vt")
    me = os.path.getmtime(__file__)
    cv = boxes = None
    if not (os.path.exists(vt) and os.path.getmtime(vt) >= me) or png or hdr:
        cv, boxes = make()
    if cv is not None and not (os.path.exists(vt) and os.path.getmtime(vt) >= me):
        tmp = vt + ".part"
        with open(tmp, "wb") as f:
            f.write(b"VTX1" + struct.pack("<III", W, H, 0) + cv.rgba(True))
        os.replace(tmp, vt)
        print(f"vita_hud_atlas: tag_hud_vita: {W} x {H}, {len(boxes)} shapes")
    if png:
        png_write(png, W, H, cv.rgba(False))
    if hdr:
        with open(hdr, "w") as f:
            f.write(header(boxes))


if __name__ == "__main__":
    main()
