#!/usr/bin/env python3
"""The Tag Team screens' kit as one atlas made at the pixel size the Vita shows it (bt3.txt taguivita).

BT4decomp's screens (src/menu/port_tbo.c, port_screen.c, tag_team_select.inc) draw their bars, windows, talk box,
buttons, logo and the main menu's emblem from the PC's pictures (port/tbo/ui/*.png), 100 to 2168 pixels across, made
for a monitor. The Vita had them as textures stretched to powers of two (1024 x 128 for a bar that shows 28 pixels
tall, 256 x 256 for a button that shows 23) and sampled down through their smaller copies: soft rims, and ~6 MB of
textures kept for the whole session, fights included. The game's own menus have their pieces made at the size they
show (small sheets, resident). Here each piece is made at its size on the 960 x 544 screen, from the PC's picture with
an area filter (the colour weighted by the alpha), and all are packed in one texture:

  - the bars (kit_bar, kit_sel, their compact keys, kit_gold, kit_steel) 28 pixels tall: the Tag Team setup pages' rows
    are 23-25 units of the page's 448, 28-30 pixels (port_tbo.c Tb_TagOptionsPlate);
  - the windows' corners (kit_window, kit_dialog) at the size Ps_Panel draws them: the corner is c * 0.22 units, 0.267 of
    the picture's pixels;
  - the pad's buttons 24 pixels tall (Ps_Button: 18-20 units, 22-24 pixels);
  - the Tag Team logo 262 x 88 (port_tbo.c Tb_DrawTitle: 185 x 72 units) and the main menu's emblem 48 pixels tall
    (menu_a.c: 34 units, 41 pixels, more when the movie enlarges the chosen row).

Each piece has 2 pixels of its own edge around it (the bilinear samples at its border never reach a neighbour). The
renderer draws a piece by the atlas's coordinates (src/menu/port_screen.c Ps_SpriteScreenFiltered, PORT_VITA): the
screens' layout is not changed (they place pictures by their sizes in units, include/menu/tbo_sizes.h).

    vita_ui_atlas.py <port/tbo/ui> <out folder> [--png f] [--header f]
      writes <out>/tag_ui_vita.vt ("VTX1", width, height, flags 0, RGBA with the GS's alpha); --header the boxes
      (port/vita/vita_ui_atlas.h, checked in), --png a look at it. Made again only when this file or a PNG changed.
Only the standard library (the build image has no imaging module).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tbo_vita_assets as tva  # noqa: E402

W = 512
PAD = 2
BAR = 28    # the rows' height in pixels
BTN = 24    # the buttons'
PANEL = 0.22 * 544.0 / 448.0  # Ps_Panel's corner: c * 0.22 units, a unit 544 / 448 pixels

# (the C name of the picture, the PNG, how it is sized: ("h", pixels tall) or ("k", scale) or ("wh", w, h))
PIECES = [
    ("TBO_KIT_BAR", "kit_bar", ("h", BAR)),
    ("TBO_KIT_SEL", "kit_sel", ("h", BAR)),
    ("TBO_KIT_BAR_COMPACT", "kit_bar_compact", ("h", BAR)),
    ("TBO_KIT_SEL_COMPACT", "kit_sel_compact", ("h", BAR)),
    ("TBO_KIT_GOLD", "kit_gold", ("h", BAR)),
    ("TBO_KIT_STEEL", "kit_steel", ("h", BAR)),
    ("TBO_KIT_WINDOW", "kit_window", ("k", PANEL)),
    ("TBO_KIT_DIALOG", "kit_dialog", ("k", PANEL)),
    ("TBO_BTN_CROSS", "btn_cross", ("h", BTN)),
    ("TBO_BTN_SQUARE", "btn_square", ("h", BTN)),
    ("TBO_BTN_TRIANGLE", "btn_triangle", ("h", BTN)),
    ("TBO_BTN_CIRCLE", "btn_circle", ("h", BTN)),
    ("TBO_BTN_LEFTRIGHT", "btn_leftright", ("h", BTN)),
    ("TBO_BTN_START", "btn_start", ("h", BTN)),
    ("TBO_TAG_LOGO", "tag_team_logo", ("wh", 262, 88)),
    ("TBO_MAIN_TAG_EMBLEM", "main_tag_emblem", ("h", 48)),
]


def piece(name, src, how):
    """the picture at its size: (w, h, [(r, g, b, a)]) with the GS's alpha"""
    emblem = name == "main_tag_emblem"
    w, h, px = tva.png_read(os.path.join(src, ("tag_team_logo" if emblem else name) + ".png"))
    if emblem:
        w, h, px = tva.tag_emblem(px, w, h)
    elif name == "tag_team_logo":
        tva.remove_outer_halo(px, w, h)
        tva.logo_speck(px, w, h)
    p = tva.to_gs(px, w, h, name == "kit_window")
    if how[0] == "h":
        nw, nh = max(1, round(w * how[1] / h)), how[1]
    elif how[0] == "k":
        nw, nh = max(1, round(w * how[1])), max(1, round(h * how[1]))
    else:
        nw, nh = how[1], how[2]
    while w >= nw * 2 and h >= nh * 2:  # (by halves first: quick, the same average)
        p, w, h = tva.half(p, w, h)
    p = tva.resample(p, w, h, nw, nh)
    return nw, nh, p


def pack(sizes):
    """shelves 512 wide, the tallest first: {index: (x, y)} of each piece's inside, and the height"""
    order = sorted(range(len(sizes)), key=lambda i: (-sizes[i][1], -sizes[i][0]))
    pos, x, y, shelf = {}, 0, 0, 0
    for i in order:
        w, h = sizes[i][0] + 2 * PAD, sizes[i][1] + 2 * PAD
        if x + w > W:
            x, y, shelf = 0, y + shelf, 0
        pos[i] = (x + PAD, y + PAD)
        x += w
        shelf = max(shelf, h)
    height = y + shelf
    return pos, (height + 7) // 8 * 8


def make(src):
    made = [piece(png, src, how) for _, png, how in PIECES]
    pos, height = pack([(w, h) for w, h, _ in made])
    out = bytearray(W * height * 4)
    boxes = []
    for i, (w, h, p) in enumerate(made):
        x0, y0 = pos[i]
        for yy in range(-PAD, h + PAD):
            sy = min(max(yy, 0), h - 1)
            for xx in range(-PAD, w + PAD):
                sx = min(max(xx, 0), w - 1)
                c = p[sy * w + sx]
                o = ((y0 + yy) * W + x0 + xx) * 4
                out[o:o + 4] = bytes((c[0], c[1], c[2], c[3]))
        boxes.append((PIECES[i][0], x0, y0, w, h))
    return height, out, boxes


def header(height, boxes):
    lines = ["/* Written by port/vita/tools/vita_ui_atlas.py: the Tag Team screens' kit made at its pixel size on the",
             "   960 x 544 screen (tbo/ui/tag_ui_vita.vt, bt3.txt taguivita), its pieces' boxes in pixels. Do not edit;",
             "   run the script with --header. */",
             "#ifndef VITA_UI_ATLAS_H", "#define VITA_UI_ATLAS_H",
             "#define TBO_VITA_UI 0x241 /* its picture number (TEX0's CBP with TBO_PSM, include/menu/tbo_art.h: free) */",
             f"#define VUA_W {W}", f"#define VUA_H {height}",
             "/* X(picture, x, y, w, h) */", "#define VUA_BOXES(X) \\"]
    for name, x, y, w, h in boxes:
        lines.append(f"    X({name}, {x}, {y}, {w}, {h}) \\")
    lines += ["", "#endif", ""]
    return "\n".join(lines)


def main():
    src, out = sys.argv[1], sys.argv[2]
    png = hdr = None
    args = sys.argv[3:]
    while args:
        if args[0] == "--png":
            png, args = args[1], args[2:]
        elif args[0] == "--header":
            hdr, args = args[1], args[2:]
        else:
            raise SystemExit("vita_ui_atlas.py <port/tbo/ui> <out> [--png f] [--header f]")
    os.makedirs(out, exist_ok=True)
    vt = os.path.join(out, "tag_ui_vita.vt")
    newest = max([os.path.getmtime(__file__), os.path.getmtime(tva.__file__)] +
                 [os.path.getmtime(os.path.join(src, ("tag_team_logo" if p == "main_tag_emblem" else p) + ".png"))
                  for _, p, _ in PIECES])
    fresh = os.path.exists(vt) and os.path.getmtime(vt) >= newest
    if fresh and not png and not hdr:
        print(f"vita_ui_atlas: tag_ui_vita: {os.path.getsize(vt) // 1024} KB (as it was)")
        return
    height, data, boxes = make(src)
    if not fresh:
        tmp = vt + ".part"
        with open(tmp, "wb") as f:
            f.write(b"VTX1" + struct.pack("<III", W, height, 0) + data)
        os.replace(tmp, vt)
        print(f"vita_ui_atlas: tag_ui_vita: {W} x {height}, {len(boxes)} pieces, {W * height * 4 // 1024} KB")
    if png:
        straight = bytearray(data)
        for i in range(3, len(straight), 4):
            straight[i] = min(255, straight[i] * 255 // 128)
        from vita_hud_atlas import png_write
        png_write(png, W, height, straight)
    if hdr:
        with open(hdr, "w") as f:
            f.write(header(height, boxes))


if __name__ == "__main__":
    main()
