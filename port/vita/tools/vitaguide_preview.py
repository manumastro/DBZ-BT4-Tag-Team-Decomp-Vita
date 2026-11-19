#!/usr/bin/env python3
"""Off-console previews of the Vita Guide (port/vita/vita_guide.c, bt3.txt vitaguide) at 960 x 544: the guide's
contents and a topic page over the main menu, the main menu's entry, the pause menu's Vita Controls page and the
fight's reminder line. Needs Pillow and numpy (a development machine, not the build image).

The letters and the buttons are the game's own: the boot file's font (its member 1) and icon file (member 8) decoded
from the player's disc as the GS lays them out (port/vita/tools/bt3tex.py), and drawn as Font_PrintAt draws them
(src/battle/col_c.c: a sprite a letter, the drop shadow or the 8-copy outline, the <PAD=> icons at 32 texels,
src/battle/col_c_b.c). The words come from port/vita/vita_guide_text.h; the lines are wrapped as vita_guide.c wraps
them. GS units to the screen as the Vita's 4:3 page maps them (1.4167 pixels across, 1.2143 down, the page in the
middle).

    vitaguide_preview.py <boot file (archive 1, member 5: pzs4us1/00004.bin of an extraction)> <out folder>
                         [--menu <main menu shot 960x544>] [--fight <fight shot>] [--es]

Approximate: the main menu and fight pictures are console screenshots (JPEG); the pause page's box is drawn as a
plain frame (the battle's own sheet is not decoded here); L3 / R3 show the PS2's sticks (the Vita draws its own
plates over them at run time, vita_optart.c); the fight's HUD mapping is taken as the menus' 4:3 page. Nothing of the
disc is written but the pictures asked for (keep them out of the repository).
"""
import os
import re
import struct
import sys

import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bt3tex as bt  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SS = 3  # supersampling of the GS canvas
UX, UY, PX0 = 725.33 / 512.0, 544.0 / 448.0, (960 - 725.33) / 2


def u32(d, o):
    return struct.unpack_from('<I', d, o)[0]


# ---- the game's font and icons ----
class Font:
    def __init__(self, boot):
        fd = u32(boot, 4)
        t = fd + u32(boot, fd + 0xC)
        self.line_h = u32(boot, fd + 0x20)
        pixels, palette = fd + u32(boot, t) + 96, fd + u32(boot, t + 4) + 96
        tex0 = struct.unpack_from('<Q', boot, t + 48)[0]
        blt = u32(boot, t + 24)
        W, H, tbw = 1 << ((tex0 >> 26) & 15), 1 << ((tex0 >> 30) & 15), (tex0 >> 14) & 63
        reg = struct.unpack_from('<Q', boot, pixels - 48)[0]
        uw, uh, dbw = reg & 4095, (reg >> 32) & 4095, blt & 63
        gs = np.full(bt.GS_BYTES, -1, dtype=np.int64)
        for y in range(uh):
            for x in range(uw):
                a = ((((y & ~31) * dbw + ((x >> 1) & ~31) + bt.kBlock32[(y >> 3) & 3][(x >> 3) & 7]) << 6) + bt.kCol32[y & 7][x & 7])
                at = (y * uw + x) * 4
                gs[a * 4:a * 4 + 4] = [at, at + 1, at + 2, at + 3]
        idx = np.zeros((H, W), dtype=np.uint8)
        for y in range(H):
            for x in range(W):
                col = (y & 15) >> 2
                sx = x ^ (4 if (col & 1) else 0)
                a = (((((y >> 2) & ~31) * (tbw >> 1) + ((x >> 2) & ~31) + bt.kMlBlock4[(y >> 4) & 7][(x >> 5) & 3]) << 9) + bt.kMlCol4[y & 3][sx & 31] + col * 128)
                g = gs[a >> 1]
                idx[y, x] = 0 if g < 0 else (boot[pixels + g] >> ((a & 1) * 4)) & 15
        pal = [struct.unpack_from('<4B', boot, palette + i * 4) for i in range(64)]
        # the 4 CLUTs of 16 in the 16 x 4 upload (CSM1): CLUT k at column (k & 1) * 8, row (k >> 1) * 2
        self.alpha = []
        for k in range(4):
            lut = np.zeros(16, dtype=np.float32)
            for i in range(16):
                row, col = (k >> 1) * 2 + i // 8, (k & 1) * 8 + i % 8
                lut[i] = pal[row * 16 + col][3] / 128.0
            self.alpha.append(lut[idx])
        n, gl = u32(boot, fd + 0x18), fd + u32(boot, fd + 0x14)
        self.glyphs = {}
        for i in range(n):
            c, u, v = struct.unpack_from('<3H', boot, gl + i * 6)
            self.glyphs[c] = (u & 1023, (u >> 10) & 31, v & 1023, (v >> 10) & 31, ((v >> 15) << 1) + (u >> 15))
        # icons: member 8, a texture file of 32 x 32 pictures in the order of the type-0 table's names
        icons = boot[u32(boot, 32):]
        self.icons = []
        for i in range(u32(icons, 0)):
            t = bt.texture(icons, i)
            self.icons.append(None if t is None else t['rgba'].astype(np.float32))

    def width(self, s, scale=1.0):
        w = 0
        for c in s:
            g = self.glyphs.get(ord(c))
            if g:
                w += int(g[1] * scale)
        return w


ICONS = {'X': (13, 20), 'O': (10, 20), 'S': (11, 20), 'T': (12, 20), 'L1': (14, 29), 'L2': (15, 29), 'L3': (16, 31),
         'R1': (17, 29), 'R2': (18, 29), 'R3': (19, 31), 'SEL': (8, 24), 'ST': (9, 24),
         'U': (4, 16), 'D': (5, 16), 'RT': (6, 16), 'LT': (7, 16)}


class Canvas:
    """The GS's 512 x 448 at SS times, RGBA float, drawn over in GS units."""

    def __init__(self):
        self.c = np.zeros((448 * SS, 512 * SS, 4), dtype=np.float32)

    def blend(self, x0, y0, src, a):
        x0, y0 = int(round(x0 * SS)), int(round(y0 * SS))
        h, w = a.shape
        X0, Y0, X1, Y1 = max(x0, 0), max(y0, 0), min(x0 + w, self.c.shape[1]), min(y0 + h, self.c.shape[0])
        if X0 >= X1 or Y0 >= Y1:
            return
        s, aa = src[Y0 - y0:Y1 - y0, X0 - x0:X1 - x0], a[Y0 - y0:Y1 - y0, X0 - x0:X1 - x0, None]
        d = self.c[Y0:Y1, X0:X1]
        d[..., :3] = d[..., :3] * (1 - aa) + s * aa
        d[..., 3:] = d[..., 3:] * (1 - aa) + aa

    def quad(self, x0, y0, x1, y1, rgba):
        w, h = int(round((x1 - x0) * SS)), int(round((y1 - y0) * SS))
        src = np.ones((h, w, 3), dtype=np.float32) * np.array(rgba[:3], dtype=np.float32)
        self.blend(x0, y0, src, np.full((h, w), min(1.0, rgba[3] / 128.0), dtype=np.float32))

    def sprite(self, img_a, img_rgb, x0, y0, w, h, rgba):
        if w <= 0 or h <= 0:
            return
        W, H = max(1, int(round(w * SS))), max(1, int(round(h * SS)))
        a = np.array(Image.fromarray((img_a * 255).astype(np.uint8)).resize((W, H), Image.BILINEAR), dtype=np.float32) / 255.0
        a = a * min(1.0, rgba[3] / 128.0)
        if img_rgb is None:
            src = np.ones((H, W, 3), dtype=np.float32) * np.array(rgba[:3], dtype=np.float32)
        else:
            src = np.array(Image.fromarray(img_rgb.astype(np.uint8)).resize((W, H), Image.BILINEAR), dtype=np.float32) * 2.0 * np.array(rgba[:3]) / 255.0
            src = np.clip(src, 0, 255)
        self.blend(x0, y0, src, a)


def tokens(text):
    """a line's text with {marks} to a list of ('c', char) / ('i', mark)"""
    out, i = [], 0
    while i < len(text):
        m = re.match(r'\{(X|O|S|T|L1|L2|L3|R1|R2|R3|SEL|ST|U|D|RT|LT)\}', text[i:])
        if m:
            out.append(('i', m.group(1)))
            i += len(m.group(0))
        else:
            out.append(('c', text[i]))
            i += 1
    return out


def print_at(cv, font, x, y, text, scale=1.0, sx=None, sy=None, color=(255, 255, 255, 128), shadow=0,
             shadow_rgba=(0, 0, 0, 64), off=(1, 1), align=0):
    sx = scale if sx is None else sx
    sy = scale if sy is None else sy
    toks = tokens(text)
    width = sum(int(font.glyphs[ord(v)][1] * sx) if k == 'c' and ord(v) in font.glyphs else (ICONS[v][1] if k == 'i' else 0) for k, v in toks)
    if align == 1:
        x -= width // 2
    elif align == 2:
        x -= width
    for k, v in toks:
        if k == 'i':
            n, adv = ICONS[v]
            ic = font.icons[n]
            if ic is not None:
                cv.sprite(ic[..., 3] / 128.0, ic[..., :3], x, y - 6, 32, 32, (128, 128, 128, 128))
            x += adv
            continue
        g = font.glyphs.get(ord(v))
        if not g:
            continue
        u, w, vv, h, clut = g
        a = font.alpha[clut][vv:vv + h, u:u + w]
        dw, dh = int(w * sx), int(h * sy)
        dx, dy = off
        offs = []
        if shadow == 1:
            offs = [(dx, dy)]
        elif shadow == 2:
            offs = [(dx, dy), (-dx, dy), (dx, -dy), (-dx, -dy), (dx, 0), (-dx, 0), (0, dy), (0, -dy)]
        for ox, oy in offs:
            cv.sprite(a, None, x + ox, y + oy, dw, dh, shadow_rgba)
        cv.sprite(a, None, x, y, dw, dh, color)
        x += dw


def wrap(font, text, width, scale):
    """vita_guide.c wrap(): words at `width` GS units at `scale`"""
    lines, cur, x = [], '', 0
    for raw in text.split('\n'):
        cur, x = '', 0
        for m in re.finditer(r'( *)([^ ]+)', raw):
            spaces, word = m.group(1), m.group(2)
            ww = 0
            for k, v in tokens(word):
                ww += ICONS[v][1] if k == 'i' else 0
            ww += int(font.width(re.sub(r'\{[A-Z0-9]+\}', '', word)) * scale)
            sw = len(spaces) * int(font.width(' ') * scale)
            if cur and x + sw + ww > width:
                lines.append(cur)
                cur, x, spaces, sw = '', 0, '', 0
            cur += spaces + word
            x += sw + ww
        lines.append(cur)
    return lines


# ---- the words, from vita_guide_text.h ----
def c_strings(src, name):
    m = re.search(r'%s\[[^\]]*\]\s*=\s*\{(.*?)\n\};' % re.escape(name), src, re.S)
    body = m.group(1)
    parts = []
    for item in re.finditer(r'\{\s*((?:"(?:[^"\\]|\\.)*"\s*)+),\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\}', body):
        parts.append((cstr(item.group(1)), cstr(item.group(2))))
    if parts:
        return parts
    return [cstr(g.group(0)) for g in re.finditer(r'(?:"(?:[^"\\]|\\.)*"\s*)+', body)]


def cstr(s):
    out = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', s))
    return re.sub(r'\\(.)', lambda m: {'n': '\n', 't': '\t'}.get(m.group(1), m.group(1)), out)


def to_screen(cv, bg):
    """the canvas over a 960 x 544 picture (the 4:3 page in the middle)"""
    im = Image.fromarray(np.clip(cv.c * np.array([1, 1, 1, 255]), 0, 255).astype(np.uint8), 'RGBA')
    im = im.resize((int(round(512 * UX)), 544), Image.LANCZOS)
    out = bg.convert('RGBA').copy()
    out.alpha_composite(im, (int(round(PX0)), 0))
    return out.convert('RGB')


def dim(bg, a):
    black = Image.new('RGB', bg.size, (0, 0, 0))
    return Image.blend(bg.convert('RGB'), black, min(1.0, a / 128.0))


GL_X0, GL_X1, GL_Y0, GL_Y1, GL_TEXT_X, GL_TEXT_Y, GL_STEP, GL_ROWS, GL_SCALE = 18, 494, 22, 426, 34, 66, 23, 14, 0.80


def guide_window(cv, cursor=None):
    cv.quad(GL_X0, GL_Y0, GL_X1, GL_Y1, (0x04, 0x10, 0x30, 0x74))
    cv.quad(GL_X0, GL_Y0, GL_X1, GL_Y0 + 34, (0x10, 0x30, 0x68, 0x70))
    cv.quad(GL_X0, GL_Y0 + 34, GL_X1, GL_Y0 + 36, (0x89, 0xC3, 0xE5, 0x70))
    cv.quad(GL_X0, GL_Y1 - 32, GL_X1, GL_Y1 - 30, (0x89, 0xC3, 0xE5, 0x50))
    if cursor is not None:
        y = GL_TEXT_Y - 3 + cursor * 23
        cv.quad(GL_X0 + 10, y, GL_X1 - 10, y + 22, (0x30, 0x60, 0xA0, 0x50))


def heading(cv, font, x, y, text, align=0):
    print_at(cv, font, x, y, text, 1.0, color=(0xFF, 0xB0, 0x40, 0x80), shadow=2, shadow_rgba=(0x30, 0x10, 0, 0x60), align=align)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    opts = dict(zip(sys.argv[1:], sys.argv[2:]))
    es = '--es' in sys.argv
    boot = open(args[0], 'rb').read()
    out = args[1]
    os.makedirs(out, exist_ok=True)
    font = Font(boot)
    src = open(os.path.join(HERE, '..', 'vita_guide_text.h'), encoding='utf-8').read()
    topics = c_strings(src, 'kVgEs' if es else 'kVgEn')
    fight = c_strings(src, 'kVgFightEs' if es else 'kVgFightEn')
    hud = c_strings(src, 'kVgHudEs' if es else 'kVgHudEn')
    lang = 'es' if es else 'en'
    menu = Image.open(opts['--menu']).convert('RGB').resize((960, 544)) if '--menu' in opts else Image.new('RGB', (960, 544), (40, 70, 120))
    fightbg = Image.open(opts['--fight']).convert('RGB').resize((960, 544)) if '--fight' in opts else Image.new('RGB', (960, 544), (60, 90, 40))
    touch = '{L2} {R2} {L3} {R3}: ' + ('panel trasero.' if es else 'rear touch pad.')

    # 1. the contents
    cv = Canvas()
    guide_window(cv, cursor=3)
    heading(cv, font, GL_TEXT_X, GL_Y0 + 7, 'Guía Vita' if es else 'Vita Guide')
    for i, (title, _) in enumerate(topics):
        col = (0xF4, 0xFF, 0x3F, 0x80) if i == 3 else (0x89, 0xC3, 0xE5, 0x80)
        print_at(cv, font, GL_TEXT_X + 12, GL_TEXT_Y + i * 23, title, 0.9, color=col, shadow=1, shadow_rgba=(0, 0, 0x10, 0x60))
    print_at(cv, font, GL_TEXT_X, GL_Y1 - 25, '{U}{D} elegir   {X} abrir   {T} volver al menú' if es else '{U}{D} choose   {X} open   {T} back to the menu',
             0.75, color=(0xD0, 0xE0, 0xF0, 0x80), shadow=1, shadow_rgba=(0, 0, 0x10, 0x60))
    to_screen(cv, dim(menu, 0x50)).save(os.path.join(out, 'vitaguide_contents_%s.png' % lang))

    # 2. every topic's pages
    for t, (title, body) in enumerate(topics):
        lines = wrap(font, body, GL_X1 - GL_X0 - 34, GL_SCALE)
        pages = max(1, (len(lines) + GL_ROWS - 1) // GL_ROWS)
        for pg in range(pages):
            cv = Canvas()
            guide_window(cv)
            heading(cv, font, GL_TEXT_X, GL_Y0 + 7, title)
            print_at(cv, font, GL_X1 - 14, GL_Y0 + 10, '%d/%d' % (t + 1, len(topics)), 0.8, color=(0x89, 0xC3, 0xE5, 0x80), shadow=2,
                     shadow_rgba=(0x30, 0x10, 0, 0x60), align=2)
            for i, line in enumerate(lines[pg * GL_ROWS:(pg + 1) * GL_ROWS]):
                print_at(cv, font, GL_TEXT_X, GL_TEXT_Y + i * GL_STEP, line, GL_SCALE, shadow=1, shadow_rgba=(0, 0, 0x10, 0x60))
            if pages > 1:
                foot = ('{LT}{RT} página %d de %d   {T} índice' if es else '{LT}{RT} page %d of %d   {T} contents') % (pg + 1, pages)
            else:
                foot = '{LT}{RT} otros temas   {T} índice   {ST} cerrar' if es else '{LT}{RT} other topics   {T} contents   {ST} close'
            print_at(cv, font, GL_TEXT_X, GL_Y1 - 25, foot, 0.75, color=(0xD0, 0xE0, 0xF0, 0x80), shadow=1, shadow_rgba=(0, 0, 0x10, 0x60))
            to_screen(cv, dim(menu, 0x50)).save(os.path.join(out, 'vitaguide_topic%02d_%d_%s.png' % (t + 1, pg + 1, lang)))
            if len(lines) > GL_ROWS * 2:
                print('topic %d: %d lines' % (t + 1, len(lines)))

    # 3. the main menu's entry: its label over the row above the first (where Options shows in the shot)
    if '--menu' in opts:
        im = menu.copy()
        patch = im.crop((250, 82, 362, 112))
        im.paste(patch, (130, 82))
        cv = Canvas()
        # the shot's Options row: x 128..240, y 84..110 in pixels; the label's corner in GS units
        lx, ly = (128 - PX0) / UX, 82 / UY
        print_at(cv, font, lx, ly, 'Guía Vita' if es else 'Vita Guide', sx=1.30 * 0.8, sy=1.30 * 0.8, color=(0xE8, 0xF0, 0xFF, 0x80), shadow=2,
                 shadow_rgba=(0x10, 0x08, 0x30, 0x80))
        im = to_screen(cv, im)
        im.save(os.path.join(out, 'vitaguide_mainmenu_%s.png' % lang))

    # 4. the pause menu's Vita Controls page (the engine's controls box: 22 x 15 glyph heights and margins)
    for tag in (0, 1):
        for page in ((0, 1) if tag else (0,)):
            cv = Canvas()
            w, h = 20 * 22 + 28, 20 * 15 + 24
            x, y = (512 - w) // 2, (448 - h) // 2
            cv.quad(x, y, x + w, y + h, (0x10, 0x20, 0x40, 0x60))
            for (a, b, c, d) in ((x, y, x + w, y + 2), (x, y + h - 2, x + w, y + h), (x, y, x + 2, y + h), (x + w - 2, y, x + w, y + h)):
                cv.quad(a, b, c, d, (0x89, 0xC3, 0xE5, 0x70))
            x0, x1, y0, y1 = x + 14, x + w - 14, y + 12, y + h - 12
            title = ('Controles Vita' if es else 'Vita Controls') + (': Tag Team' if page else '')
            heading(cv, font, (x0 + x1) // 2, y0 + 4, title, align=1)
            lines = wrap(font, fight[page].replace('%TOUCH%', touch), x1 - x0 - 8, 0.78)
            for i, line in enumerate(lines[:10]):
                print_at(cv, font, x0 + 4, y0 + 34 + i * 22, line, 0.78, shadow=1, shadow_rgba=(0, 0, 0x10, 0x60))
            foot = ('{LT}{RT} página   ' if es else '{LT}{RT} page   ') if tag else ''
            foot += ('{S} recordatorio: primeros combates   {T} volver' if es else '{S} reminder: first fights   {T} back')
            print_at(cv, font, x0 + 4, y1 - 20, foot, 0.7, color=(0xD0, 0xE0, 0xF0, 0x80), shadow=1, shadow_rgba=(0, 0, 0x10, 0x60))
            to_screen(cv, dim(fightbg, 0x30)).save(os.path.join(out, 'vitaguide_pause_%s_%s.png' % ('tag%d' % (page + 1) if tag else '1v1', lang)))

    # 5. the fight's reminder line
    for tag in (0, 1):
        cv = Canvas()
        text = hud[tag].replace('%WHERE%', 'panel trasero' if es else 'rear touch pad')
        print_at(cv, font, 256, 412, text, 0.7, color=(255, 255, 255, 0x68), shadow=1, shadow_rgba=(0, 0, 0, 0x60), align=1)
        to_screen(cv, fightbg).save(os.path.join(out, 'vitaguide_hud_%s_%s.png' % ('tag' if tag else '1v1', lang)))
    print('previews in', out)


if __name__ == '__main__':
    main()
