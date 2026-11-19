#!/usr/bin/env python3
"""An off-console preview of the Tag Team screens with bt3.txt taguivita=1 (docs/vita/tagteam.md "The Vita's Tag Team
screens"), next to a console screenshot of the same screen as it is now. Needs Pillow (a development machine; not the
build image).

It draws, at 960 x 544 as the console shows the menus, what taguivita draws at the places src/menu/port_tbo.c,
port_screen.c and tag_team_select.inc give them (the page's units to pixels as the Vita maps them: a unit 1.416 pixels
across, 1.214 down, the 4:3 page in the middle):
  - setup: the Tag Team "Local Battle" page (Tb_TagOptionsPlate, Tb_TagOptionText, Tb_DrawHelpCaption, the prompts):
    the backdrop (tag_team_art at 960 x 544), the logo, the window, the ten rows' bars from the atlas
    (port/build/tbo_vita/tag_ui_vita.vt, tools/vita_ui_atlas.py) and the lines written with the capitals they show
    (Fira Sans Condensed Bold, outline 1 pixel), narrowed to 0.755 across as the page draws them, then put on whole
    pixels;
  - select: the character select's captions over its console screenshot (the header's window and line, the teams'
    windows and lines, the prompts).
Each output is the console's screenshot above, the preview below, and a 2x crop of both.

    tagui_preview.py <screenshot> <atlas .vt> <ui folder> <fonts folder> <out .png> [setup|select] [es|en]
Approximate: the screenshots' JPEG blur; the letters by Pillow's FreeType, not the game's own rasterizer; the
backdrop's crop.
"""
import struct
import sys
from PIL import Image, ImageDraw, ImageFont

PX = 386.6667 / 512.0 * 960.0 / 512.0     # pixels a page unit across (the 4:3 page in a 960 x 544 picture)
PY = 544.0 / 448.0                         # and down
PAGE_X = (512.0 - 386.6667) * 0.5          # the page's left in the GS's 512
LEFT = -PAGE_X * 512.0 / 386.6667          # Ps_Left()
NARROW = 0.857 * (1333.333 / 1765.0) * PX / PY  # the letters across against down (PS_UNIT_X, Port_MenuTextNarrow)
CELL, CAP_OF, BASE_OF = 27.5, 0.49, 0.744
MSG_CAP, MSG_BASE = 11.5, 18.25


def X(u):
    return (PAGE_X + u * 386.6667 / 512.0) * 960.0 / 512.0


def Y(u):
    return u * PY


def atlas(path):
    d = open(path, "rb").read()
    _, w, h, _ = struct.unpack("<4sIII", d[:16])
    im = Image.frombytes("RGBA", (w, h), d[16:16 + w * h * 4])
    r, g, b, a = im.split()
    a = a.point(lambda v: min(255, v * 255 // 128))
    return Image.merge("RGBA", (r, g, b, a))


BOX = {}  # filled from port/vita/vita_ui_atlas.h


def load_boxes(header):
    for line in open(header):
        line = line.strip()
        if line.startswith("X(TBO_"):
            name, x, y, w, h = [v.strip() for v in line[2:line.index(")")].split(",")]
            BOX[name] = (int(x), int(y), int(w), int(h))


class Page:
    def __init__(self, base, atl, fonts):
        self.im = base.convert("RGBA")
        self.atl = atl
        # (the console's lines: the Menu font DejaVu Sans Condensed Bold, the talk box's Fira Sans Condensed Bold, both
        #  narrowed to 0.755 across: measured on the console's screenshots against the fonts' advances)
        self.fonts = (fonts + "/dejavu/DejaVuSansCondensed-Bold.ttf", fonts + "/fira/FiraSansCondensed-Bold.ttf")
        self.cap_per_size = []
        for font in self.fonts:
            b = ImageFont.truetype(font, 200).getbbox("H")
            self.cap_per_size.append((b[3] - b[1]) / 200.0)

    def piece(self, name, u0, v0, u1, v1, x0, y0, x1, y1, alpha=1.0):
        bx, by, bw, bh = BOX[name]
        src = self.atl.crop((round(bx + u0 * bw), round(by + v0 * bh), round(bx + u1 * bw), round(by + v1 * bh)))
        w, h = max(1, round(x1) - round(x0)), max(1, round(y1) - round(y0))
        if w <= 0 or h <= 0 or src.width <= 0 or src.height <= 0:
            return
        src = src.resize((w, h), Image.BILINEAR)
        if alpha < 1.0:
            a = src.getchannel("A").point(lambda v: int(v * alpha))
            src.putalpha(a)
        self.im.alpha_composite(src, (round(x0), round(y0)))

    def pill(self, name, x0, y0, x1, y1):  # Ps_Pill, page units
        sizes = {"TBO_KIT_BAR": (1273, 100, 50), "TBO_KIT_SEL": (1280, 107, 54)}
        w, h, c = sizes[name]
        cap = c * (y1 - y0) / h * 0.857
        cu = c / w
        self.piece(name, 0, 0, cu, 1, X(x0), Y(y0), X(x0 + cap), Y(y1))
        self.piece(name, cu, 0, 1 - cu, 1, X(x0 + cap), Y(y0), X(x1 - cap), Y(y1))
        self.piece(name, 1 - cu, 0, 1, 1, X(x1 - cap), Y(y0), X(x1), Y(y1))

    def panel(self, x0, y0, x1, y1, alpha=1.0):  # Ps_Panel of kit_window
        w, h, c = 465, 224, 49
        cy = min(c * 0.22, (y1 - y0) * 0.34)
        cx = cy * 0.857
        us, vs = [0, c / w, 1 - c / w, 1], [0, c / h, 1 - c / h, 1]
        xs, ys = [x0, x0 + cx, x1 - cx, x1], [y0, y0 + cy, y1 - cy, y1]
        for j in range(3):
            for i in range(3):
                self.piece("TBO_KIT_WINDOW", us[i], vs[j], us[i + 1], vs[j + 1], X(xs[i]), Y(ys[j]), X(xs[i + 1]), Y(ys[j + 1]), alpha)

    def line(self, x, middle_or_top, text, cap_units, top, bottom, outline, align=0, width=0.0, msg=False):
        """a line written with the capitals it shows (whole pixels), narrowed across, on whole pixels"""
        cappx = max(5, round(cap_units * PY))
        k = cappx / (cap_units * PY)
        size = cappx / self.cap_per_size[1 if msg else 0]
        f = ImageFont.truetype(self.fonts[1 if msg else 0], round(size * 4) / 4)
        b = f.getbbox(text)
        tw, th = b[2] + 4, cappx * 3
        cov = Image.new("L", (tw + 4, th), 0)
        ImageDraw.Draw(cov).text((2, cappx * 2 - f.getbbox("H")[3] + 0), text, font=f, fill=255)
        # the outline: the 3 x 3 disc of radius 1 (tbo_text.c: dx*dx + dy*dy <= r*r + r)
        out = Image.new("L", cov.size, 0)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                out = Image.composite(Image.new("L", cov.size, 255), out, cov.transform(cov.size, Image.AFFINE, (1, 0, -dx, 0, 1, -dy)))
        fit = 1.0
        nw = max(1, round(cov.width * NARROW))
        if width > 0 and nw * k > width * PX:
            fit = max(0.6, width * PX / (nw * k))
        nw = max(1, round(cov.width * NARROW * fit))
        cov = cov.resize((nw, cov.height), Image.BOX)
        out = out.resize((nw, out.height), Image.BOX)
        layer = Image.new("RGBA", cov.size, (0, 0, 0, 0))
        oc = Image.new("RGBA", cov.size, outline + (255,))
        layer = Image.composite(oc, layer, out)
        grad = Image.new("RGBA", cov.size)
        gd = ImageDraw.Draw(grad)
        for yy in range(cov.height):
            t = min(1.0, max(0.0, (yy - cappx) / max(1.0, cappx * 1.0)))
            c = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
            gd.line([(0, yy), (cov.width, yy)], fill=c + (255,))
        layer = Image.composite(grad, layer, cov)
        if msg:  # (middle_or_top: the line's top; its baseline MSG_BASE under it)
            ty = Y(middle_or_top) + MSG_BASE * k * PY - cappx * 2
        else:  # (the capitals' middle)
            ty = Y(middle_or_top) - cappx * 1.5
        tx = X(x) - 2
        if align == 2:
            tx -= nw - 4
        self.im.alpha_composite(layer, (round(tx), round(ty)))
        return nw


def clean_band(shot):
    """the game's talk box under the page (MsgWin_PortDrawStringHd, drawn by the game) from the screenshot, its words
    painted over with each row's middle colour"""
    im = Image.open(shot).convert("RGBA").resize((960, 544))
    px = im.load()
    for y0, y1, x0, x1 in ((412, 500, 30, 800), (500, 532, 230, 800)):
        for y in range(y0, y1):
            row = sorted(px[x, y][:3] for x in range(x0, x1))
            m = row[len(row) // 2]
            for x in range(x0, x1):
                px[x, y] = m + (255,)
    return im


def setup(shot, atl, ui, font, lang):
    base = Image.open(f"{ui}/tag_team_art.png").convert("RGBA").resize((960, 544), Image.LANCZOS)
    band = clean_band(shot).crop((0, 402, 960, 544))
    base.paste(band, (0, 402))
    pg = Page(base, atl, font)
    es = lang == "es"
    pg.piece("TBO_TAG_LOGO", 0, 0, 1, 1, X(LEFT + 4), Y(4), X(LEFT + 189), Y(76))
    white, lav, purple = (255, 255, 255), (0xE4, 0xD8, 0xFF), (0x40, 0x14, 0x64)
    cyan0, cyan1, navy = (0xD0, 0xFC, 0xFF), (0x70, 0xE0, 0xFF), (0x08, 0x28, 0x50)
    dark, rim = (43, 24, 7), (255, 235, 162)
    pg.line(LEFT + 196, 36, "Combate local" if es else "Local Battle", CELL * .79 * CAP_OF, white, lav, purple, width=180)
    left = LEFT + 18
    right = left + min(340, max(260, (512 - 2 * LEFT) * .49))
    rows = 10
    pg.panel(left, 74, right, 78 + 24 * (rows - 1) + 23 + 2)
    labels = (["Modo", "Equipo 1", "Equipo 2", "Tiempo límite", "Dificultad CPU", "Personalizaciones", "Recuperación",
               "Ajustes de luchador", "Reglas de combate", "Elegir luchadores"] if es else
              ["Mode", "Team 1", "Team 2", "Time Limit", "CPU Difficulty", "Custom Presets", "Recovery settings",
               "Fighter settings", "Battle rules", "Select Fighters"])
    values = (["Un jugador", "2", "2", "480 s", "Normal", "Activado"] if es else
              ["Singleplayer", "2", "2", "480 s", "Normal", "Enabled"]) + [""] * 4
    cursor = 9 if es else 0
    for i in range(rows):
        y = 78 + 24 * i
        pg.pill("TBO_KIT_SEL" if i == cursor else "TBO_KIT_BAR", left + 10, y, right - 10, y + 23)
    width = right - left - 59
    for i in range(rows):
        y = 78 + 24 * i + 12
        lw = width * .57 if values[i] else width
        sel = i == cursor
        pg.line(left + 26, y, labels[i], CELL * .70 * CAP_OF, dark if sel else white, dark if sel else lav, rim if sel else purple, width=lw)
        if values[i]:
            pg.line(right - 26, y, values[i], CELL * .68 * CAP_OF, dark if sel else cyan0, dark if sel else cyan1, rim if sel else navy, align=2, width=width - lw)
    help_es = ["En combate: stick derecho elige el objetivo. Quédate cerca de un aliado caído para reanimarlo.",
               "Senzu: L2 + SELECT. Fusión: R3 + dirección. L2 / R2 / L3 / R3: esquinas del panel trasero."]
    help_en = ["Choose solo play, two local opponents, two allied players or spectator mode."]
    for i, t in enumerate(help_es if es else help_en):  # (Tb_DrawHelpCaption: scale 1.08 while it takes 3 lines or fewer)
        pg.line(LEFT + 34, 344 + i * 22, t, MSG_CAP * 1.08, white, white, (16, 20, 40), msg=True)
    prompts = ([("TBO_BTN_CIRCLE", "Modo de cámara"), ("TBO_BTN_LEFTRIGHT", "Cambiar"), ("TBO_BTN_CROSS", "Confirmar"),
                ("TBO_BTN_TRIANGLE", "Volver")] if es else
               [("TBO_BTN_CIRCLE", "Camera mode"), ("TBO_BTN_LEFTRIGHT", "Change"), ("TBO_BTN_CROSS", "Confirm"),
                ("TBO_BTN_TRIANGLE", "Back")])
    draw_prompts(pg, prompts, 470.0)
    return pg.im


def draw_prompts(pg, prompts, right):
    """Ps_PromptsDrawAt: laid from the right edge leftwards, buttons 19 units tall, labels the talk box's letters"""
    white = (255, 255, 255)
    widths = []
    f = ImageFont.truetype(pg.fonts[1], 200)
    for btn, label in prompts:
        bx, by, bw, bh = BOX[btn]
        lw = f.getlength(label) / (200 * pg.cap_per_size[1]) * MSG_CAP * 0.857 * (1333.333 / 1765.0)
        widths.append((btn, label, 19 * bw / bh * 0.857, lw))
    x = right
    starts = []
    for btn, label, bwu, lw in reversed(widths):
        x -= lw + bwu + 16
        starts.append((x, btn, label, bwu))
    for x, btn, label, bwu in starts:
        pg.piece(btn, 0, 0, 1, 1, X(x), Y(425 - 9.5), X(x + bwu), Y(425 + 9.5))
        pg.line(x + bwu + 5, 425 - (MSG_BASE - MSG_CAP * 0.5), label, MSG_CAP, white, white, (16, 20, 40), msg=True)


def select(shot, atl, ui, font, lang):
    pg = Page(Image.open(shot), atl, font)
    es = lang == "es"
    left = LEFT
    # the header: its window repainted over the console's, its line at the captions' size (.56 * 1.3)
    d = ImageDraw.Draw(pg.im)
    d.rectangle([round(X(10 + left)) - 2, 0, round(X(502 - left)) + 2, round(Y(29)) + 2], fill=(18, 58, 66, 255))
    pg.panel(10 + left, 1, 502 - left, 29, 0x78 / 128)
    pg.line(24 + left, 15, "TAG TEAM | SELECCIÓN DE PERSONAJES" if es else "TAG TEAM | CHARACTER SELECT", CELL * .56 * 1.3 * 1.3 * CAP_OF,
            (0xD0, 0xFC, 0xFF), (0x70, 0xE0, 0xFF), (0x08, 0x28, 0x50), width=365 - 2 * left)
    for team in range(2):
        x0, x1 = (263, 502 - left) if team else (10 + left, 249)
        d.rectangle([round(X(x0)) - 2, round(Y(422)) - 2, round(X(x1)) + 2, 544], fill=(18, 58, 66, 255))
        pg.panel(x0, 422, x1, 447, 0x78 / 128)
        t = ("EQUIPO ROJO: CPU 02 | 1-2/2" if team else "EQUIPO AZUL: Player 1 | 1-2/2 | TU TURNO") if es else \
            ("RED TEAM: CPU 02 | 1-2/2" if team else "BLUE TEAM: Player 1 | 1-2/2 | YOUR TURN")
        if team:
            pg.line(273, 434.5, t, CELL * .43 * 1.4 * 1.3 * CAP_OF, (0xFF, 0xEC, 0xB8), (0xFF, 0x94, 0x30), (0x5C, 0x24, 0x08), width=120)
        else:
            pg.line(20 + left, 434.5, t, CELL * .43 * 1.4 * 1.3 * CAP_OF, (0xD0, 0xFC, 0xFF), (0x70, 0xE0, 0xFF), (0x08, 0x28, 0x50), width=217 - left)
    draw_prompts(pg, [("TBO_BTN_TRIANGLE", "Volver" if es else "Back"), ("TBO_BTN_CROSS", "Confirmar" if es else "Confirm")],
                 494 - left)
    return pg.im


def main():
    shot, vt, ui, font, out = sys.argv[1:6]
    which = sys.argv[6] if len(sys.argv) > 6 else "setup"
    lang = sys.argv[7] if len(sys.argv) > 7 else "es"
    import os
    load_boxes(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "vita_ui_atlas.h"))
    atl = atlas(vt)
    after = (setup if which == "setup" else select)(shot, atl, ui, font, lang).convert("RGB")
    before = Image.open(shot).convert("RGB").resize((960, 544))
    zoom = (20, 80, 500, 230) if which == "setup" else (0, 0, 480, 40)
    zh = (zoom[3] - zoom[1]) * 2
    g = Image.new("RGB", (960, 544 * 2 + 16 + zh * 2 + 16), (30, 30, 30))
    g.paste(before, (0, 0))
    g.paste(after, (0, 544 + 16))
    g.paste(before.crop(zoom).resize(((zoom[2] - zoom[0]) * 2, zh), Image.NEAREST), (0, 544 * 2 + 32))
    g.paste(after.crop(zoom).resize(((zoom[2] - zoom[0]) * 2, zh), Image.NEAREST), (0, 544 * 2 + 32 + zh))
    d = ImageDraw.Draw(g)
    d.text((6, 546), "above: the console screenshot (today); below: the taguivita=1 preview", fill=(255, 255, 0))
    d.text((6, 544 * 2 + 18), "2x crops: console now / preview", fill=(255, 255, 0))
    g.save(out)
    after.save(out.replace(".png", "_after.png"))
    print(out)


if __name__ == "__main__":
    main()
