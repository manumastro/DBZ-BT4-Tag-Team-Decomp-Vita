"""The LiveArea images of the BT4 build (port/vita/sce_sys): drawn here, text and abstract shapes only, no artwork of
the game. Needs Pillow and a bold font (Impact / Arial Black on macOS; pass another with --font).

    python3 port/vita/tools/livearea_bt4.py [--font FILE] [--out port/vita/sce_sys]
"""
import argparse
import math
import pathlib
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ap = argparse.ArgumentParser()
ap.add_argument("--font", default="/System/Library/Fonts/Supplemental/Impact.ttf")
ap.add_argument("--small", default="/System/Library/Fonts/Supplemental/Arial Bold.ttf")
ap.add_argument("--out", default=str(pathlib.Path(__file__).resolve().parents[1] / "sce_sys"))
o = ap.parse_args()
out = pathlib.Path(o.out)

TOP, BOTTOM = (255, 150, 30), (120, 20, 10)   # orange to a deep red
NIGHT = (14, 18, 40)


def backdrop(w, h, seed=4):
    """A vertical gradient with rays and sparks from a point low on the right: an energy burst, no figure."""
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        c = tuple(int(TOP[i] * (1 - t) + BOTTOM[i] * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(glow)
    cx, cy = w * 0.72, h * 0.62
    rnd = random.Random(seed)
    for k in range(28):
        a = k / 28 * 2 * math.pi + rnd.uniform(-0.05, 0.05)
        r = max(w, h) * 1.2
        spread = 0.035
        pts = [(cx, cy), (cx + r * math.cos(a - spread), cy + r * math.sin(a - spread)),
               (cx + r * math.cos(a + spread), cy + r * math.sin(a + spread))]
        d.polygon(pts, fill=(255, 240, 180, 40 if k % 2 else 22))
    for rad, alpha in ((min(w, h) * 0.34, 70), (min(w, h) * 0.22, 110), (min(w, h) * 0.11, 200)):
        d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=(255, 245, 200, alpha))
    for _ in range(int(w * h / 4000)):
        x, y, s = rnd.uniform(0, w), rnd.uniform(0, h), rnd.uniform(1, 3)
        d.ellipse([x - s, y - s, x + s, y + s], fill=(255, 255, 230, rnd.randint(60, 180)))
    glow = glow.filter(ImageFilter.GaussianBlur(max(w, h) / 160))
    im = Image.alpha_composite(im.convert("RGBA"), glow)
    return im


def text(im, xy, s, size, font, fill=(255, 255, 255), outline=NIGHT, stroke=None, anchor="la"):
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(font, size)
    d.text(xy, s, font=f, fill=fill, stroke_width=stroke if stroke is not None else max(2, size // 14),
           stroke_fill=outline, anchor=anchor)


def save(im, name, size=None):
    if size:
        im = im.resize(size, Image.LANCZOS)
    im.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(out / name, optimize=True)


def title(im, x, y, big, font, small_font, sub=True, anchor="la"):
    text(im, (x, y), "BT4", big, font, fill=(255, 236, 90), anchor=anchor)
    if sub:
        text(im, (x + big * 0.06, y + big * 1.02), "BUDOKAI TENKAICHI 4", int(big * 0.24), font, anchor=anchor)
        text(im, (x + big * 0.06, y + big * 1.34), "PS VITA PORT", int(big * 0.17), small_font, fill=(255, 220, 200), anchor=anchor)


# icon0 (128 x 128): drawn at 4x
ic = backdrop(512, 512, seed=2)
text(ic, (256, 230), "BT4", 230, o.font, fill=(255, 236, 90), anchor="mm")
text(ic, (256, 405), "PS VITA", 70, o.small, anchor="mm")
save(ic, "icon0.png", (128, 128))

# pic0 (960 x 544): the background behind the LiveArea
p = backdrop(960, 544, seed=3)
title(p, 60, 120, 180, o.font, o.small)
save(p, "pic0.png")

# bg0 (840 x 500)
b = backdrop(840, 500, seed=5)
save(b, "livearea/contents/bg0.png")

# startup (280 x 158): the start button
s = backdrop(560, 316, seed=7)
text(s, (280, 120), "BT4", 150, o.font, fill=(255, 236, 90), anchor="mm")
text(s, (280, 250), "START", 56, o.small, anchor="mm")
save(s, "livearea/contents/startup.png", (280, 158))

# frame1 (414 x 189, with alpha): the title over the LiveArea's background
f = Image.new("RGBA", (414, 189), (0, 0, 0, 0))
text(f, (10, 0), "BT4", 120, o.font, fill=(255, 236, 90))
text(f, (14, 124), "BUDOKAI TENKAICHI 4", 30, o.font)
text(f, (16, 160), "PS VITA PORT", 20, o.small, fill=(255, 220, 200))
f.save(out / "livearea/contents/frame1.png", optimize=True)
print("LiveArea images written to", out)
