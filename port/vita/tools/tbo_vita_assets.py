#!/usr/bin/env python3
"""BT4decomp's own screens' pictures (port/tbo/ui/*.png) made ready for the Vita at build time.

The Vita read each PNG at run time (port/vita/vita_tbo.c): decoded, its touches made (the window's teal, the logo's
halo, the main menu's emblem cut from the 2168 x 725 logo), halved and turned into the GS's alpha, on a thread of its
own; seconds of a core for the Tag Team set. Here that is done once, when the package is built, with the same numbers,
and each picture is written as the texture the renderer uploads as it is (no decoding at run time):

    <name>.vt: "VTX1", width, height, flags (u32 little-endian; flags 1: sides powers of two, mipmapped by the chip),
               then width * height RGBA, the alpha the GS's (0x80 opaque)

Sizes: the backdrops (tag_team_art, tag_hud_preview) at most the screen's 960 x 544; every other picture stretched
to the nearest powers of two (the screens place a picture by its measures in units, tbo_sizes.h, and its texture
coordinates go 0..1, so the stretch does not move anything) so the renderer can make its smaller copies
(bt3.txt tbomip), its see-through pixels given the colour of the nearest seen ones first (no dark edges in them).

    tbo_vita_assets.py <port/tbo/ui> <output folder> <name>...      (made again only when the PNG or this file changed)

Only the standard library (the build image has no imaging module): PNGs of 8 bits, RGB or RGBA, not interlaced.
"""
import os
import struct
import sys
import zlib

LIMIT_ART = (960, 544)
BACKDROPS = {"tag_team_art", "tag_hud_preview", "bg"}


def png_read(path):
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(path + ": not a PNG")
    pos, idat, w = 8, [], 0
    while pos < len(d):
        n, kind = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or inter != 0:
                raise ValueError(path + ": only 8-bit RGB / RGBA, not interlaced")
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
        pos += 12 + n
    raw = zlib.decompress(b"".join(idat))
    bpp = 4 if ctype == 6 else 3
    stride = w * bpp
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        if bpp == 4:
            out[y * w * 4:(y + 1) * w * 4] = line
        else:
            o = y * w * 4
            for x in range(w):
                out[o + x * 4:o + x * 4 + 3] = line[x * 3:x * 3 + 3]
                out[o + x * 4 + 3] = 255
    return w, h, out


# ---- the PC's touches (port/src/gs/gs_draw.c, as port/vita/vita_tbo.c had them), on straight RGBA ----
def outer_pixel(px, i):
    r, g, b, a = px[i], px[i + 1], px[i + 2], px[i + 3]
    hi, lo = max(r, g, b), min(r, g, b)
    return a < 128 or (hi >= 80 and hi - lo <= 24)


def remove_outer_halo(px, w, h):
    seen = bytearray(w * h)
    queue = []
    for y in range(h):
        for x in range(w):
            i = y * w + x
            if px[i * 4 + 3] < 128 or ((x == 0 or x + 1 == w or y == 0 or y + 1 == h) and outer_pixel(px, i * 4)):
                seen[i] = 1
                queue.append(i)
    head = 0
    while head < len(queue):
        i = queue[head]
        head += 1
        qx, qy = i % w, i // w
        px[i * 4 + 3] = 0
        for n in (i - 1 if qx else i, i + 1 if qx + 1 < w else i, i - w if qy else i, i + w if qy + 1 < h else i):
            if not seen[n] and outer_pixel(px, n * 4):
                seen[n] = 1
                queue.append(n)


def logo_speck(px, w, h):
    for row in range(h):
        for col in range(w * 620 // 2168, w):
            i = (row * w + col) * 4
            r, g, b = px[i], px[i + 1], px[i + 2]
            if max(r, g, b) >= 160 and max(r, g, b) - min(r, g, b) <= 24:
                px[i + 3] = 0


def tag_emblem(px, w, h):
    cw = (w * 676 + 1084) // 2168
    top = (h * 8 + 362) // 725
    ch = (h * 689 + 362) // 725
    out = bytearray(cw * ch * 4)
    for y in range(ch):
        sy = y + top if y + top < h else h - 1
        for x in range(cw):
            s = (sy * w + x) * 4
            d = (y * cw + x) * 4
            out[d:d + 4] = px[s:s + 4]
            dx = (x + .5) * 2168.0 / w - 354.892
            dy = (sy + .5) * 725.0 / h - 347.878
            if dx > 0:
                radius = (0.0000106049045 * dx * dx + 0.00000108280035 * dx * dy + 0.0000099139102 * dy * dy) ** 0.5
                cov = min(1.0, max(0.0, (1.014 - radius) / .007))
                out[d + 3] = int(out[d + 3] * cov + .5)
    remove_outer_halo(out, cw, ch)
    return cw, ch, out


def to_gs(px, w, h, window):
    """straight RGBA to premultiplied floats [r, g, b, a] per pixel (a in the GS's 0..128), the window's teal made one"""
    out = [None] * (w * h)
    for i in range(w * h):
        r, g, b, a8 = px[i * 4], px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3]
        a = (a8 * 128 + 127) // 255
        if window and a8 == 255 and r < 60 and 60 <= g <= 125 and b + 10 >= g and b <= g + 15:
            r, g, b = 27, 86, 92
        out[i] = (r, g, b, a)
    return out


def half(px, w, h):
    """by halves, each pixel the average of four, the colour weighted by the alpha (vita_tbo.c tbo_half)"""
    nw, nh = max(1, w // 2), max(1, h // 2)
    out = [None] * (nw * nh)
    for y in range(nh):
        y0, y1 = min(2 * y, h - 1), min(2 * y + 1, h - 1)
        for x in range(nw):
            x0, x1 = min(2 * x, w - 1), min(2 * x + 1, w - 1)
            c = (px[y0 * w + x0], px[y0 * w + x1], px[y1 * w + x0], px[y1 * w + x1])
            aw = c[0][3] + c[1][3] + c[2][3] + c[3][3]
            col = []
            for ch in range(3):
                if aw:
                    col.append(sum(k[ch] * k[3] for k in c) // aw)
                else:
                    col.append(sum(k[ch] for k in c) // 4)
            out[y * nw + x] = (col[0], col[1], col[2], (aw + 2) // 4)
    return out, nw, nh


def resample_axis(px, w, h, nw, horizontal):
    """one axis to nw samples: an area average going down, linear going up; the colour weighted by the alpha"""
    src_n = w if horizontal else h
    other = h if horizontal else w
    taps = []
    scale = src_n / nw
    for o in range(nw):
        t = []
        if scale >= 1.0:
            a, b = o * scale, (o + 1) * scale
            i = int(a)
            while i < b and i < src_n:
                ov = min(b, i + 1) - max(a, i)
                if ov > 0:
                    t.append((i, ov))
                i += 1
        else:
            f = (o + .5) * scale - .5
            i = int(f) if f >= 0 else 0
            fr = f - i if f >= 0 else 0.0
            t.append((min(i, src_n - 1), 1 - fr))
            if fr > 0:
                t.append((min(i + 1, src_n - 1), fr))
        taps.append(t)
    if horizontal:
        out = [None] * (nw * h)
    else:
        out = [None] * (w * nw)
    for k in range(other):
        for o in range(nw):
            s = [0.0, 0.0, 0.0, 0.0]
            plain = [0.0, 0.0, 0.0]
            wsum = 0.0
            for i, wt in taps[o]:
                p = px[k * w + i] if horizontal else px[i * w + k]
                s[0] += p[0] * p[3] * wt
                s[1] += p[1] * p[3] * wt
                s[2] += p[2] * p[3] * wt
                s[3] += p[3] * wt
                plain[0] += p[0] * wt
                plain[1] += p[1] * wt
                plain[2] += p[2] * wt
                wsum += wt
            if s[3] > 0:
                c = (s[0] / s[3], s[1] / s[3], s[2] / s[3])
            else:
                c = (plain[0] / wsum, plain[1] / wsum, plain[2] / wsum)
            a = s[3] / wsum
            v = (min(255, int(c[0] + .5)), min(255, int(c[1] + .5)), min(255, int(c[2] + .5)), min(128, int(a + .5)))
            if horizontal:
                out[k * nw + o] = v
            else:
                out[o * w + k] = v
    return out


def resample(px, w, h, nw, nh):
    if nw != w:
        px = resample_axis(px, w, h, nw, True)
        w = nw
    if nh != h:
        px = resample_axis(px, w, h, nh, False)
    return px


def bleed(px, w, h):
    """the see-through pixels take the colour of the nearest seen ones (pull-push): no dark edges in the chip's copies"""
    levels = [(px, w, h)]
    while w > 1 or h > 1:
        nw, nh = max(1, w // 2), max(1, h // 2)
        src = levels[-1][0]
        dst = [None] * (nw * nh)
        for y in range(nh):
            for x in range(nw):
                acc = [0, 0, 0]
                n = 0
                for yy in (min(2 * y, h - 1), min(2 * y + 1, h - 1)):
                    for xx in (min(2 * x, w - 1), min(2 * x + 1, w - 1)):
                        p = src[yy * w + xx]
                        if p is not None and p[3] > 0:
                            wt = p[3]
                            acc[0] += p[0] * wt
                            acc[1] += p[1] * wt
                            acc[2] += p[2] * wt
                            n += wt
                dst[y * nw + x] = (acc[0] // n, acc[1] // n, acc[2] // n, 1) if n else None
        levels.append((dst, nw, nh))
        w, h = nw, nh
    for k in range(len(levels) - 2, -1, -1):
        cur, cw, ch = levels[k]
        up, uw, uh = levels[k + 1]
        for y in range(ch):
            for x in range(cw):
                i = y * cw + x
                p = cur[i]
                if p is None or p[3] == 0:
                    q = up[min(y // 2, uh - 1) * uw + min(x // 2, uw - 1)]
                    if q is not None:
                        cur[i] = (q[0], q[1], q[2], 0 if k == 0 else 1)
                    elif k == 0 and p is None:
                        cur[i] = (0, 0, 0, 0)
    return levels[0][0]


def nearest_pot(v):
    p = 8
    while p * 2 <= v:
        p *= 2
    return p * 2 if v - p > p * 2 - v else p


def make(name, src_dir):
    """the picture of `name` as the Vita draws it: (w, h, flags, RGBA bytes)"""
    emblem = name == "main_tag_emblem"
    w, h, px = png_read(os.path.join(src_dir, ("tag_team_logo" if emblem else name) + ".png"))
    if emblem:
        w, h, px = tag_emblem(px, w, h)
    elif name == "tag_team_logo":
        remove_outer_halo(px, w, h)
        logo_speck(px, w, h)
    p = to_gs(px, w, h, name == "kit_window")
    limit = 1280 if name in BACKDROPS else 640
    while (w > limit or h > limit) and w > 1 and h > 1:  # (as the Vita made it smaller before)
        p, w, h = half(p, w, h)
    flags = 0
    if name in BACKDROPS:
        k = min(1.0, LIMIT_ART[0] / w, LIMIT_ART[1] / h)
        nw, nh = max(1, round(w * k)), max(1, round(h * k))
        p = resample(p, w, h, nw, nh)
        w, h = nw, nh
    else:
        nw, nh = nearest_pot(w), nearest_pot(h)
        p = resample(p, w, h, nw, nh)
        w, h = nw, nh
        p = bleed(p, w, h)
        flags = 1
    out = bytearray(w * h * 4)
    for i, c in enumerate(p):
        out[i * 4:i * 4 + 4] = bytes((c[0], c[1], c[2], c[3]))
    return w, h, flags, out


def main():
    src, dst, names = sys.argv[1], sys.argv[2], sys.argv[3:]
    os.makedirs(dst, exist_ok=True)
    me = os.path.getmtime(__file__)
    total = 0
    for name in names:
        png = os.path.join(src, ("tag_team_logo" if name == "main_tag_emblem" else name) + ".png")
        out = os.path.join(dst, name + ".vt")
        if not (os.path.exists(out) and os.path.getmtime(out) >= max(os.path.getmtime(png), me)):
            w, h, flags, data = make(name, src)
            tmp = out + ".part"
            with open(tmp, "wb") as f:
                f.write(b"VTX1" + struct.pack("<III", w, h, flags) + data)
            os.replace(tmp, out)
            print(f"tbo_vita_assets: {name}: {w} x {h}{' (mipmapped)' if flags & 1 else ''}")
        total += os.path.getsize(out)
    print(f"tbo_vita_assets: {len(names)} pictures, {total // 1024} KB as textures")


if __name__ == "__main__":
    main()
