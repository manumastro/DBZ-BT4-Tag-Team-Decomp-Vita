#!/usr/bin/env python3
"""Makes the LiveArea images (port/vita/sce_sys) from two pieces of the game's art, laid out as the BT3 port's:

    livearea_from_art.py WIDE.png TALL.jpg port/vita/sce_sys

WIDE: a 16:9 picture with the logo on its left (the LiveArea's page and the start picture); TALL: an upright
cover with the logo at its foot (the bubble). Made:
    pic0.png                       960 x 544  WIDE as it is (the picture while the game starts)
    livearea/contents/bg0.png      840 x 500  WIDE without its logo (the logo's place filled from around it)
    livearea/contents/frame1.png   414 x 189  the logo alone, cut along its black outline (transparent around it)
    livearea/contents/startup.png  280 x 158  the logo on black
    icon0.png                      128 x 128  the foot of TALL, square
All 8-bit indexed but frame1 (RGBA), as the LiveArea wants. Needs Pillow, NumPy and SciPy.
"""
import sys

import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage


def fit(im, w, h):
    """Scaled to cover w x h, then the middle cut out."""
    s = max(w / im.width, h / im.height)
    im = im.resize((round(im.width * s), round(im.height * s)), Image.LANCZOS)
    x, y = (im.width - w) // 2, (im.height - h) // 2
    return im.crop((x, y, x + w, y + h))


def save_p(im, path):
    im.convert('RGB').convert('P', palette=Image.ADAPTIVE, colors=256).save(path, optimize=True)


def logo_mask(im, box):
    """The logo inside box: everything not joined to the box's edge through pixels lighter than its black outline."""
    a = np.asarray(im.crop(box).convert('RGB')).astype(int)
    dark = a.max(axis=2) < 60
    lab, _ = ndimage.label(~dark)
    edge = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    outside = np.isin(lab, list(edge))
    inside = ndimage.binary_fill_holes(~outside)
    keep, n = ndimage.label(inside)
    if n > 1:  # (the largest piece: the logo; sparks left out)
        sizes = ndimage.sum(inside, keep, range(1, n + 1))
        inside = keep == (1 + int(np.argmax(sizes)))
    return inside


def main():
    wide = Image.open(sys.argv[1]).convert('RGB')
    tall = Image.open(sys.argv[2]).convert('RGB')
    out = sys.argv[3]
    W, H = wide.size
    # the logo's box in WIDE (its left part, as in the BT4 key art): found from its black outline
    box = (int(W * 0.02), int(H * 0.27), int(W * 0.47), int(H * 0.82))
    m = logo_mask(wide, box)
    ys, xs = np.nonzero(m)
    lb = (box[0] + xs.min(), box[1] + ys.min(), box[0] + xs.max() + 1, box[1] + ys.max() + 1)
    full = np.zeros((H, W), bool)
    full[box[1]:box[3], box[0]:box[2]] = m
    alpha = Image.fromarray((full * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(1.5))
    logo = wide.copy()
    logo.putalpha(alpha)
    logo = logo.crop(lb)
    # WIDE without the logo: its place filled from the pixels around it only (a normalised blur at a few sizes, the
    # largest last: the green ground and its light carried in, no colour of the logo)
    hole = ndimage.distance_transform_edt(~full) < W * 0.016  # (the logo and the white glow round its outline)
    a = np.asarray(wide).astype(np.float32)
    known = (~hole).astype(np.float32)
    fill = np.zeros_like(a)
    got = np.zeros(hole.shape, np.float32)
    for sigma in (300, 120, 50, 20):
        k = ndimage.gaussian_filter(known, sigma)
        f = np.stack([ndimage.gaussian_filter(a[..., c] * known, sigma) for c in range(3)], axis=2) / np.maximum(k, 1e-6)[..., None]
        use = (k > 0.05).astype(np.float32)
        fill = fill * (1 - use[..., None]) + f * use[..., None]
        got = np.maximum(got, use)
    soft = ndimage.gaussian_filter(hole.astype(np.float32), 6)[..., None]
    out_a = a * (1 - soft) + fill * soft
    bg = Image.fromarray(np.clip(out_a, 0, 255).astype(np.uint8))
    save_p(fit(wide, 960, 544), out + '/pic0.png')
    save_p(fit(bg, 840, 500), out + '/livearea/contents/bg0.png')
    f = logo.copy()
    f.thumbnail((404, 179), Image.LANCZOS)
    frame = Image.new('RGBA', (414, 189), (0, 0, 0, 0))
    frame.alpha_composite(f, ((414 - f.width) // 2, (189 - f.height) // 2))
    frame.save(out + '/livearea/contents/frame1.png', optimize=True)
    s = logo.copy()
    s.thumbnail((260, 146), Image.LANCZOS)
    start = Image.new('RGBA', (280, 158), (0, 0, 0, 255))
    start.alpha_composite(s, ((280 - s.width) // 2, (158 - s.height) // 2))
    save_p(start, out + '/livearea/contents/startup.png')
    side = tall.width
    save_p(tall.crop((0, tall.height - side, side, tall.height)).resize((128, 128), Image.LANCZOS), out + '/icon0.png')
    print('logo at', lb, 'in', sys.argv[1])


if __name__ == '__main__':
    main()
