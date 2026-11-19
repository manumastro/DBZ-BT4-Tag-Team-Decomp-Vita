/* The Vita Guide (bt3.txt vitaguide, default 1): everything about playing on the Vita, in the game itself, drawn the
   way the game draws its own help pages.

   What the original does, and what this copies (docs/vita/development.md "vitaguide"):
   - its help pages (the battle pause menu's controls page, src/battle/hud_0_c.c BtlText_DrawControls; the menus'
     message windows) are text of the boot file's resident font, one sprite a letter (src/battle/col_c.c,
     Font_PutSprite), with the controller's buttons drawn inside the text by the font's <PAD=> tag from the resident
     icon file (col_c_b.c); no texture is made for them, nothing is uploaded but the font and icon sheets the game
     uploads for any text;
   - the pause menu's entries are its menu engine's items (hud_0_b.c), its pages the engine's boxes.
   So: the guide's words are Font_PrintAt lines of that font (the letters' outline only on the headings: the body has
   the drop shadow, 2 sprites a letter instead of the controls page's 9), the buttons the font's own pictures (L3 / R3
   the Vita's plates since vita_optart.c), the window two plain quads and two thin bars (no texture), drawn over the
   main menu, which keeps running behind it. In a fight the page is the engine's own controls box with these lines
   in it (Vita_GuideFightPage, hud_0_b.c). Nothing is loaded, kept or made: no memory beyond this file's text
   buffers (about 20 KB of .bss).

   Three places use it:
   - the main menu's "Vita Guide" entry (src/menu/menu_a.c, PORT_VITA; the PC's online item's slot, which the Vita
     never lists): a contents page, then each topic's pages (vita_guide_text.h);
   - the battle pause menu's "Vita Controls" entry (src/battle/hud_0.c, hud_0_b.c): the controls of the fight (the
     Tag Team page too in a Tag fight), and SQUARE there turns the reminder below on and off;
   - the reminder line at the foot of a fight (bt3.txt / saved hudhelp: 0 off, 1 on, 2 the first fights; default 2,
     the first 5 fights, the first 20 seconds of each), and the Tag Team setup page's legend (port_tbo.c). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
/* The game's sources are built with port_compat.h, where u64 is 64 bits; this file (port/vita, "hard") is not, and
   types.h's u64 is `unsigned long`: 32 bits on the Vita. The GS packet words written here must be the game's 64-bit
   ones (the first builds wrote the window's quads as 32-bit words: a GIF stream of garbage that sent every primitive
   after it into another frame buffer, the black screen and 0.4 pictures a second). */
#undef u64
#define u64 unsigned long long
#include "sys/pad.h"
#include "vita_guide_text.h"
#include "battle/col_c.h"

extern void Font_PushStyle(void);
extern void Font_PopStyle(void);
extern void Font_SetScale(f32 scale);
extern void Font_SetScaleXY(f32 sx, f32 sy);
extern void Font_SetAlign(s32 align);
extern void Font_SetSpacing(s32 x, s32 y);
extern void Font_SetShadowMode(s32 mode);
extern void Font_SetShadowOffset(s32 x, s32 y);
extern void Font_SetShadowColorRGBA(s32 r, s32 g, s32 b, s32 a);
extern void Font_SetColorRGBA(s32 r, s32 g, s32 b, s32 a);
extern void Font_PrintAt(s32 x, s32 y, u16 *str);
extern s32 Font_GetWidth(u16 *str);
extern void Font_FlushAll(void);
extern void FontIcon_SetPadType(s32 type);
extern void Font_SetClip(s32 x0, s32 y0, s32 x1, s32 y1);
extern void Font_SetFlags(s32 flags);
extern s32 Font_GetCmdCount(void);
extern s32 FontIcon_Begin(s32 vramBase);
extern void FontTag_Handle();
extern s32 gFontIconPadType;
static void style_base(void);
extern void FontIcon_ResetAnim(void);
extern u64 *Dma_BeginDirect(void);
extern void Dma_EndDirect(u64 *pkt);
extern void Gfx_PutDefaultEnv(u64 **pkt);
extern s32 Snd_PlaySe(u32 mask, s32 id);
extern void Port_MenuPage(int screen);
extern void Port_MenuFill(int on);
extern int Port_Setting(const char *name, int def);
extern void Port_SettingSave(const char *name, int value);
extern int Vita_TouchGetSpot(int b);
extern int gVitaBt4English;
extern void Vita_Log(const char *fmt, ...);

/* ---- settings ---- */
int gVitaGuide = 1;    /* bt3.txt vitaguide: 1 (default) the entries and the legend; 0 none of them */
int gVitaHudHelp = -1;
int gVitaGuideTest = 0; /* bt3.txt guidetest (development, the black window's hunt): bits leaving parts out of the guide's
                           picture: 1 the window's quads, 2 the flush before them, 4 the 4:3 page's markers, 8 the icons,
                           16 all its text, 32 the letters' shadows and outlines */ /* bt3.txt hudhelp: over the saved one (Vita Controls' SQUARE); -1 the saved one */
#define HUDHELP_FIGHTS 5    /* hudhelp 2: in the first fights */
#define HUDHELP_FRAMES 1200 /* ... their first 20 seconds */

int Vita_GuideOn(void) { return gVitaGuide != 0; }

static int hudhelp_mode(void) {
    int v = gVitaHudHelp >= 0 ? gVitaHudHelp : Port_Setting("hud_help", 2);
    return v < 0 || v > 2 ? 2 : v;
}

/* ---- text: UTF-8 with {marks} to the game's 16-bit strings with <PAD=> tags ---- */
static const struct { const char *mark; const u16 name[3]; int advance; const char *word; } kMarks[] = {
    {"X", {0x00D7}, 20}, {"O", {0x25CB}, 20}, {"S", {0x25A1}, 20}, {"T", {0x25B3}, 20},
    {"L1", {0xFF2C, 0xFF11}, 29}, {"L2", {0xFF2C, 0xFF12}, 29}, {"L3", {0xFF2C, 0xFF13}, 31},
    {"R1", {0xFF32, 0xFF11}, 29}, {"R2", {0xFF32, 0xFF12}, 29}, {"R3", {0xFF32, 0xFF13}, 31},
    {"SEL", {0xFF26, 0x32A7}, 24, "SELECT"}, {"ST", {0xFF26, 0x32A8}, 24, "START"}, /* (as words: the icon set's
                                     SELECT / START are the PS2's unlabelled dark pills, a blank plate on the Vita) */
    {"U", {0x2191}, 16}, {"D", {0x2193}, 16}, {"RT", {0x2192}, 16}, {"LT", {0x2190}, 16},
};
#define MARKS ((int)(sizeof(kMarks) / sizeof(kMarks[0])))

/* the mark at s (after its '{'), its length up to '}' in *len; -1 none */
static int mark_at(const char *s, int *len) {
    int k;
    for (k = 0; k < MARKS; k++) {
        size_t n = strlen(kMarks[k].mark);
        if (strncmp(s, kMarks[k].mark, n) == 0 && s[n] == '}') {
            *len = (int)n + 1;
            return k;
        }
    }
    return -1;
}

static unsigned utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    unsigned c = *s++;
    if ((c & 0xE0) == 0xC0 && s[0]) {
        c = ((c & 31) << 6) | (s[0] & 63);
        s++;
    } else if ((c & 0xF0) == 0xE0 && s[0] && s[1]) {
        c = ((c & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63);
        s += 2;
    }
    *p = (const char *)s;
    return c;
}

/* one token (a word, a mark, or the spaces before a word) into out; its width at scale 1 */
static int put_tag(u16 *out, int *n, int max, int k) {
    static const char pre[] = "<PAD=";
    int i;
    if (kMarks[k].word != NULL) { /* (letters: measured with the word's other letters) */
        for (i = 0; kMarks[k].word[i] && *n < max; i++) {
            out[(*n)++] = (u8)kMarks[k].word[i];
        }
        return 0;
    }
    for (i = 0; pre[i] && *n < max; i++) {
        out[(*n)++] = (u8)pre[i];
    }
    for (i = 0; i < 3 && kMarks[k].name[i] && *n < max; i++) {
        out[(*n)++] = kMarks[k].name[i];
    }
    if (*n < max) {
        out[(*n)++] = '>';
    }
    return kMarks[k].advance;
}

/* ---- the lines of a page: the text wrapped at `width` (GS units at scale `scale`) ---- */
#define LINES_MAX 64
#define LINE_CHARS 200
static u16 sLine[LINES_MAX][LINE_CHARS];
static int sLineCount;

static int word_width(const u16 *w, int n, f32 scale) {
    static u16 tmp[LINE_CHARS];
    if (n <= 0) {
        return 0;
    }
    memcpy(tmp, w, (size_t)n * 2);
    tmp[n] = 0;
    return (int)((f32)Font_GetWidth(tmp) * scale);
}

/* `text` with %TOUCH% / %WHERE% put in (where the touch buttons are now) into sLine[]; returns the lines */
static int wrap(const char *text, int width, f32 scale) {
    char buf[2048];
    const char *p;
    int x = 0, n = 0;
    u16 *line;

    {   /* the touch line, from Vita Settings > Touch */
        static const char *spotEn[3] = {"rear touch pad", "front screen corner", "off"};
        static const char *spotEs[3] = {"panel trasero", "esquina de la pantalla", "apagado"};
        static const char *shortEn[3] = {"rear pad", "corner", "off"}, *shortEs[3] = {"panel", "esquina", "no"};
        const char *const *spot = gVitaBt4English ? spotEn : spotEs;
        const char *const *spotShort = gVitaBt4English ? shortEn : shortEs;
        char touch[200], where[80];
        int s0 = Vita_TouchGetSpot(0), same = 1, b;
        const char *src = text;
        char *d = buf;
        for (b = 1; b < 4; b++) {
            same &= Vita_TouchGetSpot(b) == s0;
        }
        static const char *const cornerEn[4] = {"top left", "top right", "bottom left", "bottom right"};
        static const char *const cornerEs[4] = {"arriba izq.", "arriba der.", "abajo izq.", "abajo der."};
        const char *const *corner = gVitaBt4English ? cornerEn : cornerEs;
        if (same && s0 == 2) {
            snprintf(touch, sizeof(touch), "{L2} {R2} {L3} {R3}   %s", spot[2]);
            snprintf(where, sizeof(where), "%s", spot[2]);
        } else if (same) {
            /* the spots are the same corners on the rear pad and on the screen: whole pairs, two a line (the
               console's run vgr5: one sentence wrapped a word alone) */
            snprintf(touch, sizeof(touch), "%s:\n{L2}   %s        {R2}   %s\n{L3}   %s        {R3}   %s",
                     s0 == 1 ? (gVitaBt4English ? "Screen corners" : "Esquinas de la pantalla") : (gVitaBt4English ? "Rear touch pad" : "Panel trasero"),
                     corner[0], corner[1], corner[2], corner[3]);
            snprintf(where, sizeof(where), "%s", spot[s0 < 0 || s0 > 2 ? 0 : s0]);
        } else {
            static const char *const nm[4] = {"{L2}", "{R2}", "{L3}", "{R3}"};
            int o = 0;
            touch[0] = 0;
            for (b = 0; b < 4; b++) {
                int s = Vita_TouchGetSpot(b);
                o += snprintf(touch + o, sizeof(touch) - (size_t)o, "%s   %s%s", nm[b], spotShort[s < 0 || s > 2 ? 0 : s],
                              b == 0 || b == 2 ? "        " : b == 1 ? "\n" : "");
            }
            snprintf(where, sizeof(where), "%s", gVitaBt4English ? "Vita Settings > Touch" : "Vita Settings > Touch");
        }
        while (*src && d < buf + sizeof(buf) - 200) {
            if (strncmp(src, "%TOUCH%", 7) == 0) {
                d += snprintf(d, 200, "%s", touch);
                src += 7;
            } else if (strncmp(src, "%WHERE%", 7) == 0) {
                d += snprintf(d, 200, "%s", where);
                src += 7;
            } else {
                *d++ = *src++;
            }
        }
        *d = 0;
    }
    Font_PushStyle(); /* (Font_GetWidth measures with the style's scale and spacing: scale 1 here, `scale` below) */
    style_base();
    sLineCount = 0;
    line = sLine[0];
    p = buf;
    while (*p && sLineCount < LINES_MAX) {
        u16 word[LINE_CHARS];
        int wn = 0, ww = 0, spaces = 0, k, len;
        if (*p == '\n') {
            line[n] = 0;
            sLineCount++;
            line = sLine[sLineCount < LINES_MAX ? sLineCount : LINES_MAX - 1];
            n = x = 0;
            p++;
            continue;
        }
        while (*p == ' ') {
            spaces++;
            p++;
        }
        /* a word: up to a space or a line's end; marks count by their picture's width */
        while (*p && *p != ' ' && *p != '\n' && wn < LINE_CHARS - 12) {
            if (*p == '{' && (k = mark_at(p + 1, &len)) >= 0) {
                ww += put_tag(word, &wn, LINE_CHARS - 12, k);
                p += 1 + len;
            } else {
                unsigned c = utf8_next(&p);
                word[wn++] = (u16)c;
                ww += 0; /* (measured below, the letters together) */
            }
        }
        {   /* the letters' width: the word without its tags, measured by the font */
            u16 letters[LINE_CHARS];
            int ln = 0, i = 0;
            while (i < wn) {
                if (word[i] == '<' && i + 4 < wn && word[i + 1] == 'P' && word[i + 4] == '=') {
                    while (i < wn && word[i] != '>') {
                        i++;
                    }
                    i++;
                    continue;
                }
                letters[ln++] = word[i++];
            }
            ww = (int)((f32)ww + (f32)word_width(letters, ln, scale));
        }
        {
            int sw = spaces * word_width((u16[]){' '}, 1, scale);
            if (n > 0 && x + sw + ww > width) { /* the word on the next line */
                line[n] = 0;
                sLineCount++;
                if (sLineCount >= LINES_MAX) {
                    break;
                }
                line = sLine[sLineCount];
                n = x = 0;
                spaces = 0;
                sw = 0;
            }
            while (spaces-- > 0 && n < LINE_CHARS - 1) {
                line[n++] = ' ';
            }
            x += sw;
        }
        if (n + wn >= LINE_CHARS - 1) {
            wn = LINE_CHARS - 1 - n;
        }
        memcpy(line + n, word, (size_t)wn * 2);
        n += wn;
        x += ww;
    }
    if (sLineCount < LINES_MAX) {
        line[n] = 0;
        sLineCount++;
    }
    Font_PopStyle();
    return sLineCount;
}

/* a short line (a heading, a footer) with its marks */
static void to_u16(const char *s, u16 *out, int max) {
    int n = 0, k, len;
    while (*s && n < max - 12) {
        if (*s == '{' && (k = mark_at(s + 1, &len)) >= 0) {
            put_tag(out, &n, max - 1, k);
            s += 1 + len;
        } else {
            out[n++] = (u16)utf8_next(&s);
        }
    }
    out[n] = 0;
}

/* ---- the window: untextured quads in the GS's packet, as the game's own direct packets are made ---- */
static int sQuads; /* (the log's count of the guide's first pictures) */
static void quad(u64 **p, int x0, int y0, int x1, int y1, u32 rgba) {
    sQuads++;
    x0 += 0x700;
    x1 += 0x700;
    y0 += 0x720;
    y1 += 0x720;
    (*p)[0] = 0x4400000000008001ULL; /* GIF tag: REGLIST, 4 registers, once, the end */
    (*p)[1] = 0x5510;                /* PRIM, RGBAQ, XYZ2, XYZ2 */
    *p += 2;
    (*p)[0] = 0x46;                  /* a sprite, blended, no texture */
    (*p)[1] = (u64)rgba | 0x3F80000000000000ULL;
    *p += 2;
    (*p)[0] = ((u64)x0 << 4) | ((u64)y0 << 20) | 0xFFFFFFF000000000ULL;
    (*p)[1] = ((u64)x1 << 4) | ((u64)y1 << 20) | 0xFFFFFFF000000000ULL;
    *p += 2;
}

static u64 *quads_begin(void) {
    u64 *p = Dma_BeginDirect();
    Gfx_PutDefaultEnv(&p);
    p[0] = 0x1000000000008002ULL; /* TEST_1: alpha and depth tests always pass; ZBUF_1 no depth writes (the game's own
                                     fade rectangle's state, sys/fade.c Fade_DrawRect, drawn over every menu) */
    p[1] = 0xE;
    p += 2;
    p[0] = 0x30000;
    p[1] = 0x47;
    p += 2;
    p[0] = 0x1310000E0ULL;
    p[1] = 0x4E;
    p += 2;
    return p;
}

#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))

/* ---- the guide over the main menu ---- */
/* The window's measures (the menu's page, 512 x 448 units; on the Vita's screen a unit is ~1.9 pixels across and ~1.2
   down). A page: lines GL_STEP apart (31 pixels), a heading GL_HGAP more above it (not at a page's top), an empty
   line half a line; GL_H units of lines (the footer's bar below them). The letters at GL_SCALE (capitals ~18 pixels
   on the screen, as the game's own message window), the headings at GL_HSCALE in the guide's orange. */
enum { GL_X0 = 18, GL_X1 = 494, GL_Y0 = 22, GL_Y1 = 426, GL_TEXT_X = 34, GL_TEXT_Y = 64, GL_STEP = 26, GL_HGAP = 8,
       GL_H = 324, GL_ROW = 25 };
#define GL_SCALE 0.90f
#define GL_HSCALE 0.95f
static int sOpen, sTopic, sPage, sPages, sCursor, sLastTopic = -1, sLastPages;
#define PAGES_MAX 16
static int sPageStart[PAGES_MAX + 1]; /* the first line of each page, the line count after the last */
static u16 sTitle[96], sFoot[160], sCount[16];

static const VgTopic *topics(void) { return gVitaBt4English ? kVgEn : kVgEs; }

int Vita_GuideIsOpen(void) { return sOpen; }

static int sLogFrames; /* the guide's first pictures, each logged (what it handed the renderer) */
void Vita_GuideOpen(void) {
    sOpen = 1;
    sLogFrames = 1;
    sTopic = -1; /* the contents */
    sPage = 0;
    Snd_PlaySe(1, 1);
    Vita_Log("vita: guide: opened\n");
}

/* ---- a page's lines: kinds, cells, columns ----
   After wrap(): each line's kind (empty, text, a heading: "# " first), and a text line cut into cells where 3 or more
   spaces stand ("{S}   Attack"): the first cell at the margin, the second at its group's column (the widest first cell
   of the lines under the same heading, and a margin), a third and fourth the same from the page's middle (two pairs
   on a line). The cells stay in sLine (a 0 at each cut), so the font's queued commands point at them as before. */
enum { LK_EMPTY, LK_TEXT, LK_HEAD };
static u8 sKind[LINES_MAX], sCellN[LINES_MAX];
static u8 sCellAt[LINES_MAX][4];
static s16 sColA[LINES_MAX], sColB[LINES_MAX], sW0[LINES_MAX], sW2[LINES_MAX];

static int cell_width(const u16 *s, f32 scale) { /* (the style at scale 1 pushed: letters measured, marks by their size) */
    u16 letters[LINE_CHARS];
    int n = 0, w = 0, k;
    while (*s) {
        if (s[0] == '<' && s[1] == 'P' && s[2] == 'A' && s[3] == 'D' && s[4] == '=') {
            u16 name[3] = {0, 0, 0};
            int i = 0;
            s += 5;
            while (*s && *s != '>') {
                if (i < 3) {
                    name[i++] = *s;
                }
                s++;
            }
            if (*s == '>') {
                s++;
            }
            for (k = 0; k < MARKS; k++) {
                if (kMarks[k].word == NULL && kMarks[k].name[0] == name[0] && kMarks[k].name[1] == name[1]) {
                    w += (int)((f32)kMarks[k].advance * scale + 4.0f);
                    break;
                }
            }
            continue;
        }
        if (n < LINE_CHARS - 1) {
            letters[n++] = *s;
        }
        s++;
    }
    return w + word_width(letters, n, scale);
}

static void line_cells(f32 scale, int width) {
    int l, a, b;
    Font_PushStyle();
    style_base();
    for (l = 0; l < sLineCount; l++) {
        u16 *s = sLine[l];
        int i = 0, n = 1;
        sCellAt[l][0] = 0;
        sKind[l] = s[0] == 0 ? LK_EMPTY : (s[0] == '#' && s[1] == ' ') ? LK_HEAD : LK_TEXT;
        if (sKind[l] == LK_HEAD) {
            sCellAt[l][0] = 2;
        }
        if (sKind[l] == LK_TEXT) {
            while (s[i] && n < 4) {
                if (s[i] == ' ' && s[i + 1] == ' ' && s[i + 2] == ' ') {
                    int j = i;
                    while (s[j] == ' ') {
                        j++;
                    }
                    if (s[j] == 0) {
                        break;
                    }
                    s[i] = 0;
                    sCellAt[l][n++] = (u8)(j < 255 ? j : 255);
                    i = j;
                    continue;
                }
                i++;
            }
            if (i == 0 && s[0] == 0 && n > 1) {
                sCellAt[l][0] = sCellAt[l][1]; /* (a line that starts with spaces: one cell, at the column) */
            }
        }
        sCellN[l] = (u8)(sKind[l] == LK_TEXT ? n : 1);
        sW0[l] = (s16)(sCellN[l] >= 2 ? cell_width(s + sCellAt[l][0], scale) : 0);
        sW2[l] = (s16)(sCellN[l] >= 4 ? cell_width(s + sCellAt[l][2], scale) : 0);
    }
    for (a = 0; a < sLineCount; a = b) { /* the columns of each heading's group */
        int colA = 0, colB = 0, colQ = 0; /* (colQ: the first column of the lines of two pairs, their own) */
        for (b = a + 1; b < sLineCount && sKind[b] != LK_HEAD; b++) {
        }
        for (l = a; l < b; l++) {
            if (sKind[l] == LK_TEXT && sCellN[l] >= 2) {
                int w0 = cell_width(sLine[l] + sCellAt[l][0], scale);
                if (w0 < width / 2 && w0 > colA) {
                    colA = w0;
                }
                if (sCellN[l] >= 4) {
                    int w2 = cell_width(sLine[l] + sCellAt[l][2], scale);
                    if (w2 > colB) {
                        colB = w2;
                    }
                    if (w0 > colQ) {
                        colQ = w0;
                    }
                }
            }
        }
        for (l = a; l < b; l++) {
            sColA[l] = (s16)((sCellN[l] >= 4 ? colQ : colA) + 14);
            sColB[l] = (s16)(colB + 14);
        }
    }
    Font_PopStyle();
}

/* one line drawn at (x, y): its cells at their columns (the style set by the caller: a heading's or the text's) */
static void line_draw(int l, int x, int y, int width) {
    int k;
    if (sKind[l] != LK_TEXT) {
        if (sKind[l] == LK_HEAD) {
            Font_PrintAt(x, y, sLine[l] + sCellAt[l][0]);
        }
        return;
    }
    for (k = 0; k < sCellN[l]; k++) {
        int cx = x;
        /* (a first cell wider than its group's column: the next one after it, never over it; console run vgr2) */
        int c1 = sColA[l] > sW0[l] + 12 ? sColA[l] : sW0[l] + 12;
        if (k == 1) {
            cx = x + c1;
        } else if (k == 2) {
            cx = x + width / 2;
        } else if (k == 3) {
            cx = x + width / 2 + (sColB[l] > sW2[l] + 12 ? sColB[l] : sW2[l] + 12);
        }
        Font_PrintAt(cx, y, sLine[l] + sCellAt[l][k]);
    }
}

/* a line's height on the page: a heading's space above it (not at the page's top), an empty line half a line */
static int line_h(int l, int first) {
    if (sKind[l] == LK_HEAD) {
        return (first ? 0 : GL_HGAP) + GL_STEP + 2;
    }
    return sKind[l] == LK_EMPTY ? GL_STEP / 2 : GL_STEP;
}

/* The pages of a topic: lines as long as they fit in GL_H; a page that cannot hold the rest ends before its last
   heading (a group of lines is not torn across two pages), and never with a heading at its foot */
static void page_breaks(void) {
    int start = 0, pages = 0;
    while (sLineCount > 0 && sKind[sLineCount - 1] == LK_EMPTY) {
        sLineCount--; /* (no empty lines at the end) */
    }
    while (start < sLineCount && pages < PAGES_MAX) {
        int l, h = 0, end, lastHead = -1;
        while (start < sLineCount && sKind[start] == LK_EMPTY) {
            start++; /* (a page does not start with an empty line) */
        }
        if (start >= sLineCount) {
            break;
        }
        sPageStart[pages++] = start;
        for (l = start; l < sLineCount; l++) {
            int lh = line_h(l, l == start);
            if (h + lh > GL_H) {
                break;
            }
            if (sKind[l] == LK_HEAD && l > start) {
                lastHead = l;
            }
            h += lh;
        }
        end = l;
        if (end < sLineCount && sKind[end] != LK_HEAD && lastHead > start + 1) {
            int hh = 0, k;
            for (k = start; k < lastHead; k++) {
                hh += line_h(k, k == start);
            }
            if (hh * 4 >= GL_H * 3 || end - lastHead < 3) {
                end = lastHead; /* (the group that does not fit begins the next page; a page 3/4 full is not filled
                                   with a group's first lines, nor is a heading left with fewer than two) */
            }
        }
        while (end - 1 > start && sKind[end - 1] == LK_HEAD) {
            end--; /* (no heading at a page's foot) */
        }
        start = end > start ? end : start + 1;
    }
    if (pages == 0) {
        sPageStart[pages++] = 0;
    }
    sPageStart[pages] = sLineCount;
    sLastPages = pages;
}

/* one action column for each page (the console's review: actions started at a different place in every group): the
   widest first cell of the page's lines of one pair (a first cell wider than ~45 % keeps its own, after it) */
static void page_columns(int width) {
    int pg, l;
    for (pg = 0; pg < sLastPages; pg++) {
        int col = 0;
        for (l = sPageStart[pg]; l < sPageStart[pg + 1]; l++) {
            if (sKind[l] == LK_TEXT && sCellN[l] >= 2 && sCellN[l] < 4 && sW0[l] <= width * 45 / 100 && sW0[l] > col) {
                col = sW0[l];
            }
        }
        for (l = sPageStart[pg]; l < sPageStart[pg + 1]; l++) {
            if (sCellN[l] < 4) {
                sColA[l] = (s16)(col + 18);
            }
        }
    }
}

/* one topic's lines (cached: wrapped again only when the topic changes) */
static void topic_lines(int t) {
    if (t != sLastTopic) {
        wrap(topics()[t].body, GL_X1 - GL_X0 - 44, GL_SCALE);
        line_cells(GL_SCALE, GL_X1 - GL_X0 - 44);
        page_breaks();
        page_columns(GL_X1 - GL_X0 - 44);
        sLastTopic = t;
    }
    sPages = sLastPages;
}

/* the main menu's frame, in place of its own input (src/menu/menu_a.c): 0 when the guide closed */
int Vita_GuideInput(void) {
    u32 press = gPad[0].gamePressed, rep = gPad[0].gameRepeat;

    if (!sOpen) {
        return 0;
    }
    if (sTopic < 0) {
        if (rep & PADG_UP) {
            sCursor = (sCursor + VG_TOPICS - 1) % VG_TOPICS;
            Snd_PlaySe(1, 0);
        } else if (rep & PADG_DOWN) {
            sCursor = (sCursor + 1) % VG_TOPICS;
            Snd_PlaySe(1, 0);
        } else if (press & (PADG_CROSS | PADG_RIGHT)) {
            sTopic = sCursor;
            sPage = 0;
            Snd_PlaySe(1, 1);
        } else if (press & (PADG_TRIANGLE | PADG_CIRCLE | PADG_START)) {
            sOpen = 0;
            Snd_PlaySe(1, 2);
            return 0;
        }
        return 1;
    }
    topic_lines(sTopic);
    if (rep & (PADG_RIGHT | PADG_R1 | PADG_CROSS)) { /* the next page, then the next topic */
        if (sPage + 1 < sPages) {
            sPage++;
        } else {
            sTopic = (sTopic + 1) % VG_TOPICS;
            sCursor = sTopic;
            sPage = 0;
        }
        Snd_PlaySe(1, 0);
    } else if (rep & (PADG_LEFT | PADG_L1)) {
        if (sPage > 0) {
            sPage--;
        } else {
            sTopic = (sTopic + VG_TOPICS - 1) % VG_TOPICS;
            sCursor = sTopic;
            topic_lines(sTopic);
            sPage = sPages - 1;
        }
        Snd_PlaySe(1, 0);
    } else if (press & (PADG_TRIANGLE | PADG_CIRCLE)) {
        sTopic = -1; /* back to the contents */
        Snd_PlaySe(1, 2);
    } else if (press & PADG_START) {
        sOpen = 0;
        Snd_PlaySe(1, 2);
        return 0;
    }
    return 1;
}

/* Every field of the font's style set here: Font_PushStyle copies the style of whoever drew last, and the main menu's
   message window leaves its own (a clip box that grows letter by letter, its scale): text drawn with it came out
   clipped to a sliver and scaled wrong (the console's first run: a black screen, glyphs as huge sprites, scissor
   0 0 113 1). As the game's own pages do (dialog.c, pad_watch.c), nothing is taken from before. */
static void shadow_mode(int m) { /* (guidetest bit 32: no shadow, a sprite a letter) */
    Font_SetShadowMode((gVitaGuideTest & 32) ? 0 : m);
}

static void style_base(void) {
    Font_SetScaleXY(1.0f, 1.0f);
    Font_SetScale(1.0f);
    Font_SetSpacing(0, 0);
    Font_SetAlign(0);
    Font_SetFlags(0);
    Font_SetClip(0, 0, 0x200, 0x1C0);
    shadow_mode(0);
    Font_SetShadowOffset(1, 1);
    Font_SetColorRGBA(0xFF, 0xFF, 0xFF, 0x80);
    if (gVitaGuideTest & 8) { /* (test: no icons, their tags printed as letters) */
        Font_SetBeginCallback(NULL);
        Font_SetTagCallback(NULL, NULL);
        return;
    }
    Font_SetBeginCallback(FontIcon_Begin);           /* the <PAD=> icons, as FontIcon_Init hooks them at boot */
    Font_SetTagCallback((FontTagFn)FontTag_Handle, &gFontIconPadType);
}

static void style_body(void) {
    style_base();
    Font_SetAlign(0);
    Font_SetScale(GL_SCALE);
    Font_SetSpacing(0, 0);
    shadow_mode(1); /* the drop shadow: 2 sprites a letter */
    Font_SetShadowOffset(1, 1);
    Font_SetShadowColorRGBA(0, 0, 0x10, 0x60);
    Font_SetColorRGBA(0xFF, 0xFF, 0xFF, 0x80);
}

static void style_heading(void) {
    style_base();
    Font_SetAlign(0);
    Font_SetScale(1.0f);
    Font_SetSpacing(0, 0);
    shadow_mode(2); /* the outline, as the pause menu's headings */
    Font_SetShadowOffset(1, 1);
    Font_SetShadowColorRGBA(0x30, 0x10, 0x00, 0x60);
    Font_SetColorRGBA(0xFF, 0xB0, 0x40, 0x80);
}

/* after the main menu's own drawing, on its page */
void Vita_GuideDraw(void) {
    static u16 sRow[VG_TOPICS][96];
    u64 *p;
    int i;

    if (!sOpen) {
        return;
    }
    if (!(gVitaGuideTest & 2)) {
        Font_FlushAll(); /* (the menu's own letters under the window) */
    }
    sQuads = 0;
    if (!(gVitaGuideTest & 4)) {
        Port_MenuPage(50); /* the 4:3 page in the middle, at its true shape (as the language picker, port_language.c) */
        Port_MenuFill(1);  /* (the part's marker before the packet, not inside it) */
    }
    if (!(gVitaGuideTest & 1)) {
        p = quads_begin();
        quad(&p, 0, 0, 512, 448, RGBA(0, 0, 0, 0x50)); /* the menu dimmed, the screen's width */
        Dma_EndDirect(p);
    }
    if (!(gVitaGuideTest & 4)) {
        Port_MenuFill(0);
    }
    if (!(gVitaGuideTest & 1)) {
    p = quads_begin();
    quad(&p, GL_X0, GL_Y0, GL_X1, GL_Y1, RGBA(0x04, 0x10, 0x30, 0x80));      /* the window (~97 %: the menu behind it
                                                                                    showed through the letters at 0x74) */
    quad(&p, GL_X0, GL_Y0, GL_X1, GL_Y0 + 34, RGBA(0x10, 0x30, 0x68, 0x80)); /* its title band */
    quad(&p, GL_X0, GL_Y0 + 34, GL_X1, GL_Y0 + 36, RGBA(0x89, 0xC3, 0xE5, 0x70));
    quad(&p, GL_X0, GL_Y1 - 32, GL_X1, GL_Y1 - 30, RGBA(0x89, 0xC3, 0xE5, 0x50));
    if (sTopic < 0) { /* the cursor's bar */
        int y = GL_TEXT_Y - 3 + sCursor * GL_ROW;
        quad(&p, GL_X0 + 10, y, GL_X1 - 10, y + GL_ROW - 1, RGBA(0x30, 0x60, 0xA0, 0x60));
    }
    Dma_EndDirect(p);
    }
    if (gVitaGuideTest & 16) { /* (test: the window only) */
        if (!(gVitaGuideTest & 4)) {
            Port_MenuPage(0);
        }
        return;
    }

    FontIcon_SetPadType(0);
    Font_PushStyle();
    style_heading();
    if (sTopic < 0) {
        to_u16(gVitaBt4English ? "Vita Guide" : "Guía Vita", sTitle, 96);
        Font_PrintAt(GL_TEXT_X, GL_Y0 + 7, sTitle);
        Font_SetScale(GL_SCALE);
        shadow_mode(1);
        Font_SetShadowColorRGBA(0, 0, 0x10, 0x60);
        for (i = 0; i < VG_TOPICS; i++) {
            to_u16(topics()[i].title, sRow[i], 96);
            if (i == sCursor) {
                Font_SetColorRGBA(0xF4, 0xFF, 0x3F, 0x80); /* the pause menu's cursor colour */
            } else {
                Font_SetColorRGBA(0xE8, 0xF0, 0xFF, 0x80); /* (white: the pause menu's blue was dim on the window) */
            }
            Font_PrintAt(GL_TEXT_X + 12, GL_TEXT_Y + i * GL_ROW, sRow[i]);
        }
        to_u16(gVitaBt4English ? "{U}{D} Choose     {X} Open     {T} Close"
                               : "{U}{D} Elegir     {X} Abrir     {T} Cerrar", sFoot, 160);
    } else {
        int first, n;
        topic_lines(sTopic);
        to_u16(topics()[sTopic].title, sTitle, 96);
        Font_PrintAt(GL_TEXT_X, GL_Y0 + 7, sTitle);
        {
            char count[16];
            snprintf(count, sizeof(count), "%d/%d", sTopic + 1, VG_TOPICS);
            to_u16(count, sCount, 16);
        }
        Font_SetScale(0.8f);
        Font_SetColorRGBA(0x89, 0xC3, 0xE5, 0x80);
        Font_PrintAt(GL_X1 - 110, GL_Y0 + 10, sCount); /* (left-aligned, room for "13/13": nearer the edge, "10/13" lost its last figure) */
        first = sPageStart[sPage];
        n = sPageStart[sPage + 1] - first;
        {
            int y = GL_TEXT_Y;
            for (i = 0; i < n; i++) {
                int l = first + i, top = i == 0;
                if (sKind[l] == LK_HEAD) {
                    style_heading();
                    Font_SetScale(GL_HSCALE);
                    line_draw(l, GL_TEXT_X, y + (top ? 0 : GL_HGAP), GL_X1 - GL_X0 - 34);
                } else if (sKind[l] == LK_TEXT) {
                    style_body();
                    line_draw(l, GL_TEXT_X + 10, y, GL_X1 - GL_X0 - 44);
                }
                y += line_h(l, top);
            }
        }
        if (sPages > 1) {
            char pg[80];
            snprintf(pg, sizeof(pg), gVitaBt4English ? "{LT}{RT} Page %d of %d     {T} Topics" : "{LT}{RT} Página %d de %d     {T} Temas",
                     sPage + 1, sPages);
            to_u16(pg, sFoot, 160);
        } else {
            to_u16(gVitaBt4English ? "{LT}{RT} Other topics     {T} Topics"
                                   : "{LT}{RT} Otros temas     {T} Temas", sFoot, 160);
        }
    }
    style_body();
    Font_SetScale(0.82f);
    Font_SetColorRGBA(0xD0, 0xE0, 0xF0, 0x80);
    Font_PrintAt(GL_TEXT_X, GL_Y1 - 26, sFoot);
    Font_PopStyle();
    if (sLogFrames > 0) {
        sLogFrames--;
        Vita_Log("vita: guide: picture: %d quads, %d text commands queued, topic %d page %d, test %d\n", sQuads,
                 (int)Font_GetCmdCount(), sTopic, sPage, gVitaGuideTest);
    }
    Font_FlushAll();
    if (!(gVitaGuideTest & 4)) {
        Port_MenuPage(0);
    }
}

/* ---- the main menu's entry: its label in the game's font on the plate's row, its description ---- */
static u16 sLabel[32];
void Vita_GuideMenuLabel(f32 x, f32 y, f32 sx, f32 sy, int alpha, int lit) {
    if (alpha <= 0) {
        return;
    }
    to_u16(gVitaBt4English ? "Vita Guide" : "Guía Vita", sLabel, 32);
    FontIcon_SetPadType(0);
    Font_PushStyle();
    style_base();
    Font_SetBeginCallback(NULL); /* (no button pictures in the label: the icon sheet is not uploaded for it) */
    Font_SetTagCallback(NULL, NULL);
    Font_SetScaleXY(1.30f * sx, 1.30f * sy); /* the plate's row is 32 high: capitals as tall as its neighbours' */
    Font_SetScale(1.0f);
    Font_SetSpacing(0, 0);
    shadow_mode(2);
    Font_SetShadowOffset(1, 1);
    Font_SetShadowColorRGBA(0x10, 0x08, 0x30, alpha > 0x80 ? 0x80 : alpha);
    if (lit) {
        Font_SetColorRGBA(0xFF, 0xD8, 0x50, alpha > 0x80 ? 0x80 : alpha);
    } else {
        Font_SetColorRGBA(0xE8, 0xF0, 0xFF, alpha > 0x80 ? 0x80 : alpha);
    }
    Font_PrintAt((s32)(x + 6.0f * sx), (s32)(y + 4.0f * sy), sLabel);
    Font_PopStyle();
}

/* the message window's line under the menu (MsgWin_PortDrawString's form: 0xFEFF, lines of at most ~31 letters) */
u16 *Vita_GuideMenuDescription(void) {
    static u16 line[160];
    const char *text = gVitaBt4English ? "Controls, Tag Team, settings and help:\nall about playing on the Vita."
                                       : "Controles, Tag Team, ajustes y ayuda:\ntodo sobre jugar en la Vita.";
    int n = 1;
    line[0] = 0xFEFF;
    while (*text && n < 158) {
        line[n++] = (u16)utf8_next(&text);
    }
    line[n] = 0;
    return line;
}

/* ---- the battle pause menu's "Vita Controls" page (src/battle/hud_0_b.c, the engine's controls box) ---- */
static int sFightPage;
static int sTagFight;

u16 *Vita_GuideFightItemText(void) {
    static u16 t[32];
    to_u16(gVitaBt4English ? "Vita Controls" : "Controles Vita", t, 32);
    return t;
}

static int sFightLog;
void Vita_GuideFightBegin(int tag) {
    sTagFight = tag;
    sFightPage = 0;
    sFightLog = 3;
}

/* the page's item, every frame while it is open: LEFT / RIGHT the Tag Team page, SQUARE the reminder (as the skill
   list's item reads its pad: hud_0.c PauseMenu_SkillListFunc) */
int Vita_GuideFightFunc(int pad) {
    u32 press = gPad[pad < 0 ? 0 : pad].gamePressed;
    if (sTagFight && (press & (PADG_LEFT | PADG_RIGHT))) {
        sFightPage ^= 1;
        Snd_PlaySe(1, 0);
    }
    if (press & PADG_SQUARE) {
        int m = hudhelp_mode() == 1 ? 0 : 1;
        Port_SettingSave("hud_help", m);
        gVitaHudHelp = -1;
        Snd_PlaySe(1, 1);
        Vita_Log("vita: guide: the fights' controls reminder %s\n", m ? "on" : "off");
    }
    return 0;
}

static u16 sFightTitle[48], sFightFoot[160];
void Vita_GuideFightPage(int x0, int x1, int y0, int y1) {
    const char *body = (gVitaBt4English ? kVgFightEn : kVgFightEs)[sTagFight ? sFightPage : 0];
    char foot[200];
    int i, n, y, step = 24, width = x1 - x0 - 12;
    f32 scale = 0.88f;

    wrap(body, width, scale);
    line_cells(scale, width);
    sLastTopic = -1; /* (the lines are shared with the guide's) */
    Font_PushStyle();
    style_heading();
    if (sTagFight && sFightPage) {
        to_u16(gVitaBt4English ? "Vita Controls: Tag Team" : "Controles Vita: Tag Team", sFightTitle, 48);
    } else {
        to_u16(gVitaBt4English ? "Vita Controls" : "Controles Vita", sFightTitle, 48);
    }
    Font_SetAlign(1);
    Font_PrintAt((x0 + x1) / 2, y0 + 2, sFightTitle);
    style_body();
    Font_SetScale(scale);
    shadow_mode(2); /* (the pause menu's items: light letters with a dark outline on the orange window; plain white
                       with a soft shadow was low-contrast there, the console's run vgr5) */
    Font_SetShadowColorRGBA(0x30, 0x10, 0x00, 0x80);
    Font_SetColorRGBA(0xFF, 0xF4, 0xD0, 0x80);
    n = 0;
    y = y0 + 36;
    for (i = 0; i < sLineCount && y + step <= y1 - 30; i++) {
        if (sKind[i] == LK_TEXT) {
            line_draw(i, x0 + 6, y, width);
            n++;
        }
        y += sKind[i] == LK_EMPTY ? step / 2 : step;
    }
    snprintf(foot, sizeof(foot), gVitaBt4English ? "%s{S} Reminder: %s     {T} Back" : "%s{S} Recordatorio: %s     {T} Volver",
             sTagFight ? (gVitaBt4English ? "{LT}{RT} Page     " : "{LT}{RT} Página     ") : "",
             hudhelp_mode() == 1 ? (gVitaBt4English ? "on" : "sí") : hudhelp_mode() == 2 ? (gVitaBt4English ? "first fights" : "primeros combates") : "no");
    to_u16(foot, sFightFoot, 160);
    Font_SetScale(0.78f);
    Font_SetColorRGBA(0xD0, 0xE0, 0xF0, 0x80);
    Font_PrintAt(x0 + 6, y1 - 22, sFightFoot);
    Font_PopStyle();
    if (sFightLog > 0) {
        sFightLog--;
        Vita_Log("vita: guide: Vita Controls page %d: box %d,%d-%d,%d, %d lines, %d text commands queued\n", sFightPage, x0, y0, x1, y1, n,
                 (int)Font_GetCmdCount());
    }
}

/* ---- the fight's reminder line (src/battle/battle.c, after the HUD, before the font is flushed) ---- */
static int sHudFrames, sHudCounted;
#define HUD_SCALE 0.82f /* (0.7 and see-through before: the console's run, too small to read in a fight) */
#define HUD_Y 414
static u16 sHud[200];

/* a fight began (src/battle/hud_0.c PauseMenu_Init) */
void Vita_GuideFightStart(int tag) {
    Vita_GuideFightBegin(tag);
    sHudFrames = 0;
    sHudCounted = 0;
}

void Vita_GuideHudHint(int visible) {
    int mode = hudhelp_mode();
    if (!gVitaGuide || mode == 0 || !visible) {
        return;
    }
    if (mode == 2) {
        int fights = Port_Setting("hud_help_fights", 0);
        if (!sHudCounted) { /* (counted once a fight, when its reminder first shows) */
            sHudCounted = 1;
            if (fights >= HUDHELP_FIGHTS) {
                sHudFrames = HUDHELP_FRAMES;
            } else {
                Port_SettingSave("hud_help_fights", fights + 1);
            }
        }
        if (sHudFrames >= HUDHELP_FRAMES) {
            return;
        }
    }
    sHudFrames++;
    if (sHudFrames == 1 || sHud[0] == 0) {
        wrap((gVitaBt4English ? kVgHudEn : kVgHudEs)[sTagFight], 2000, HUD_SCALE);
        sLastTopic = -1;
        memcpy(sHud, sLine[0], sizeof(sHud) < sizeof(sLine[0]) ? sizeof(sHud) : sizeof(sLine[0]));
    }
    {   /* a dark strip under the line (the game's own message box's way: an untextured sprite, blended), then the line */
        static int sHudW;
        u64 *p;
        if (sHudFrames == 1 || sHudW == 0) {
            Font_PushStyle();
            style_base();
            sHudW = cell_width(sHud, HUD_SCALE);
            Font_PopStyle();
        }
        if (!(gVitaGuideTest & 64)) { /* (guidetest bit 64: the line without its strip) */
        p = quads_begin();
        quad(&p, 256 - sHudW / 2 - 12, HUD_Y - 4, 256 + sHudW / 2 + 12, HUD_Y + 22, RGBA(0x00, 0x04, 0x14, 0x58));
        Dma_EndDirect(p);
        }
    }
    FontIcon_SetPadType(0);
    Font_PushStyle();
    style_base();
    Font_SetAlign(1);
    Font_SetScale(HUD_SCALE);
    Font_SetSpacing(0, 0);
    shadow_mode(1);
    Font_SetShadowOffset(1, 1);
    Font_SetShadowColorRGBA(0, 0, 0, 0x70);
    Font_SetColorRGBA(0xFF, 0xFF, 0xFF, 0x80);
    Font_PrintAt(256, HUD_Y, sHud);
    Font_PopStyle();
}

/* ---- the Tag Team setup page's legend: its lines (port_tbo.c draws them with the page's own kit) ---- */
const char *Vita_GuideTagLegend(int i) {
    if (!gVitaGuide || i < 0 || i >= 8) {
        return NULL;
    }
    return (gVitaBt4English ? kVgTagEn : kVgTagEs)[i];
}
