#!/usr/bin/env python3
"""An off-console preview of the Tag Team HUD with bt3.txt taghudvita=1 (docs/vita/tagteam.md), for a look before a
console run. Needs Pillow (a development machine; not the build image).

It composes, over a 1 vs 1 fight's screenshot of the console (640 x 368, the fight's picture), what taghudvita draws
there at the places src/battle/tag_team_hud.inc gives them:
  - the player's gauge: the 1 vs 1 HUD's own, as it is in the screenshot (taghudvita draws it at the same size and
    place); the game's timer as it is; the opponent's gauge painted over (the Tag HUD has the radar there);
  - the radar (the atlas's face at its pixel size, three contacts), the target's rings (the nearest of the atlas's
    three sizes), its health bar and name (Fira Sans Condensed Bold, capitals 8.5 pixels, 1-pixel outline);
  - the allies' column: the player's gauge at 0.6 at the foot of the left edge (cut from the screenshot with a mask
    made by comparing two screenshots: what is the same in both is the HUD).
The picture is then stretched to 960 x 544 as the console shows it, next to a screenshot of today's Tag HUD.

    taghud_preview.py <runs folder> <atlas png> <font ttf> <out png>
Approximate: the target's place and size are set by hand, the floating allies' gauges and the assist panel are not
drawn, and the screenshots' JPEG blur is there.
"""
import sys
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

PX_X, PX_Y = 0.9375, 368.0 / 448.0  # pixels of the fight picture a page unit (across, down), 16:9
LEFT = -85.333                     # Ps_Left()


def page_x(u):
    return 80.0 + u * PX_X


def page_y(v):
    return v * PX_Y


def atlas_piece(atlas, box, k, tint=None):
    x, y, w, h = box
    p = atlas.crop((x, y, x + w, y + h))
    if k != 1.0:
        p = p.resize((max(1, round(w * k)), max(1, round(h * k))), Image.BILINEAR)
    if tint is not None:
        r, g, b, a = p.split()
        r = r.point(lambda v: v * tint[0] // 255)
        g = g.point(lambda v: v * tint[1] // 255)
        b = b.point(lambda v: v * tint[2] // 255)
        p = Image.merge("RGBA", (r, g, b, a))
    return p


def put(dst, piece, cx, cy):
    dst.alpha_composite(piece, (round(cx - piece.width / 2), round(cy - piece.height / 2)))


def main():
    runs, atlas_png, font_ttf, out = sys.argv[1:5]
    atlas = Image.open(atlas_png).convert("RGBA")
    base = Image.open(runs + "/sweep_h1duel_en1/shots/00390.jpg").convert("RGBA")
    other = Image.open(runs + "/sweep_h1duel_en1/shots/01134.jpg").convert("RGBA")
    now = Image.open(runs + "/sweep_t1tag_en1/shots/00229.jpg").convert("RGB")
    img = base.copy()

    # the opponent's gauge painted over with the sky under it (mirrored rows), softened
    patch = img.crop((380, 64, 640, 128)).transpose(Image.FLIP_TOP_BOTTOM).filter(ImageFilter.GaussianBlur(4))
    fade = Image.linear_gradient("L").rotate(90).resize(patch.size).point(lambda v: min(255, v * 16))
    img.paste(patch, (380, 0), fade)

    # the radar: centre (right - 39, 43) of the page; contacts: the player, an ally, the target (marked, gold)
    right = 512 - LEFT
    cx, cy = page_x(right - 39), page_y(43)
    put(img, atlas_piece(atlas, (0, 0, 48, 48), 1.0), cx, cy)
    contact = (112, 0, 9, 9)
    put(img, atlas_piece(atlas, contact, 1.0, (245, 249, 224)), cx, cy)
    put(img, atlas_piece(atlas, contact, 0.8, (69, 211, 246)), cx - 9, cy + 6)
    put(img, atlas_piece(atlas, contact, 0.8, (255, 223, 80)), cx + 4, cy - 12)
    put(img, atlas_piece(atlas, contact, 0.8, (249, 104, 78)), cx + 12, cy - 4)

    # the target (the opponent far away, about x 446, y 170 in the screenshot): rings, bar, name
    tx, ty, ring_px = 446, 168, 14.0
    inner = {(48, 0, 64, 64): 26.0, (0, 64, 44, 44): 17.0, (44, 64, 30, 30): 10.5}
    box = min(inner, key=lambda b: max(ring_px / inner[b], inner[b] / ring_px))
    put(img, atlas_piece(atlas, box, ring_px / inner[box], (255, 214, 100)), tx, ty)
    k = 1.2
    bar_y = ty - ring_px - 9 * PX_Y - 4
    d = ImageDraw.Draw(img, "RGBA")
    d.rectangle((tx - 30 * k * PX_X, bar_y - 1 * k * PX_Y, tx + 30 * k * PX_X, bar_y + 5 * k * PX_Y), fill=(16, 23, 33, 235))
    d.rectangle((tx - 29 * k * PX_X, bar_y, tx + 29 * k * PX_X, bar_y + 3 * k * PX_Y), fill=(72, 91, 102, 170))
    d.rectangle((tx - 29 * k * PX_X, bar_y, tx - 29 * k * PX_X + 58 * k * PX_X * 0.7, bar_y + 3 * k * PX_Y), fill=(116, 235, 83, 255))
    font = ImageFont.truetype(font_ttf, 12)  # Fira's capitals are ~0.69 of its size: ~8.5 pixels
    name = "Goku (Early)"
    w = d.textlength(name, font=font)
    d.text((tx - w / 2, bar_y - 3), name, font=font, fill=(245, 248, 255, 255), anchor="ls",
           stroke_width=1, stroke_fill=(3, 10, 17, 255))

    # the allies' column: the player's gauge at 0.6, from the foot of the screen (y = 448 - 44 of the page)
    g0 = base.crop((0, 0, 232, 64))
    g1 = other.crop((0, 0, 232, 64))
    diff = ImageChops.difference(g0.convert("RGB"), g1.convert("RGB")).convert("L")
    mask = diff.point(lambda v: 255 if v < 28 else 0)
    r, g, b, _ = g0.split()
    sky = ImageChops.subtract(b, r).point(lambda v: 0 if v > 45 else 255)  # (the sky the two screenshots share)
    mask = ImageChops.multiply(mask, sky)
    hexagon = Image.new("L", g0.size, 0)
    ImageDraw.Draw(hexagon).polygon([(16, 3), (44, 3), (58, 31), (44, 59), (16, 59), (3, 31)], fill=255)
    mask = ImageChops.lighter(mask, hexagon).filter(ImageFilter.MedianFilter(3))
    g0.putalpha(mask)
    ally = g0.resize((round(232 * 0.6), round(64 * 0.6)), Image.BILINEAR)
    img.alpha_composite(ally, (0, round(page_y(448 - 44))))

    shown = img.convert("RGB").resize((960, 544), Image.BILINEAR)
    before = now.resize((960, 544), Image.BILINEAR)
    sheet = Image.new("RGB", (960, 544 * 2 + 64), (20, 20, 24))
    sheet.paste(before, (0, 30))
    sheet.paste(shown, (0, 544 + 64))
    d = ImageDraw.Draw(sheet)
    small = ImageFont.truetype(font_ttf, 20)
    d.text((8, 4), "Now (v1.4.1, a 2 vs 2 screenshot of the console)", font=small, fill=(230, 230, 230))
    d.text((8, 578), "taghudvita=1 (preview composed over a 1 vs 1 screenshot; target and ally placed by hand)",
           font=small, fill=(230, 230, 230))
    sheet.save(out)
    img.convert("RGB").resize((960, 544), Image.BILINEAR).save(out.replace(".png", "_960.png"))


if __name__ == "__main__":
    main()
