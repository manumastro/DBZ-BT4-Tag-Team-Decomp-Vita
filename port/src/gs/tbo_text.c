/*
 * Tenkaichi Budokai Online's letters (src/menu/port_tbo.c), written with the TrueType fonts of the font list
 * (menu_text.c, MenuText_Line*): its headings and buttons in the Menu font, its talk box in the Dialogue / UI font,
 * each on "Original" the first font of fonts.tsv for that use. A font added to fonts.tsv is a choice in F1 > UI and
 * so a look for these screens too; any character the font has can be written.
 *
 * The game's side asks for a line each frame it draws it (PortTbo_TextId, on the game's thread) and gets a number
 * for it and its measures; it then draws it as one of the screens' pictures (TBO_PSM, the number in CBP). The
 * renderer asks here for the picture of that number (TboText_Pixels) the first time and whenever the line or the
 * font changed (its version). The picture is the line twice: filled above, outlined below, white, so the game
 * tints and blends it as it did its letter sheet; a heading (kind 2) is one picture in its colours, the main
 * menu's heading style (MenuText_Heading*).
 *
 * Nothing here is game state: the screens are a menu of the PC's, before and outside an online match.
 */
#include "menu_text.h"
#include "online_language.h"
#include "menu_translation.h" /* the online menus' lines in the chosen language */
#include "../../../include/menu/tbo_art.h"
#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned gGsFrame;
#define TEXT_MAX 128
#define IDLE_MS 1000 /* a line not asked for that long gives its place to another */

typedef struct {
    char text[TEXT_MAX];
    int kind;
    uint32_t generation; /* of the fonts it was measured with */
    uint32_t version;    /* changes whenever what the place holds changes (0: never used) */
    uint64_t askedMs;
    unsigned askedFrame;
    int advance, w, h, base, pad, cap, outline;
#ifdef PORT_VITA
    int fine; /* (taghudfine) written at the HUD's capitals: made again when that changes */
    int vsx, vsy; /* (taguivita) thousandths: made this much narrower (and, a heading, shorter) at write time; 1000 none */
#endif
} TboLine;

static TboLine sLines[TBO_TEXT_SLOTS];
static uint32_t sVersion;
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
unsigned PortTbo_TextGeneration(void){return MenuText_LineGeneration();}
uint32_t TboText_Version(uint32_t id);

/* The size the letters are drawn at, by kind (capitals, in pixels: the screens show them smaller, from smaller
   copies), and the outline's thickness: as the letter sheet had it, about a 16th of the capitals. */
#ifdef PORT_VITA
/* (bt3.txt tbotextsize=1, the default: port/vita/vita_tbo.c) The Vita shows a line with capitals of 7-20 pixels and
   made it smaller by halves to fit 128 rows: capitals of 40 (talk box 24) were all it kept. Written at that size
   straight away, with the outline as thick as it came out (about 1.5 pixels: the 3 x 3 disc of radius 1): a quarter of the
   pixels to write, outline and halve, so a screen's lines can be made as it comes in. The measures are in
   thousandths of the capitals, so the layout does not change. */
int gVitaTboTextSize = 1;
/* (bt3.txt taghudfine, vita_tbo.c Vita_TagHudCap: in a battle the lines are written with capitals about their size on
   the fight's picture, with an outline of 1) */
extern int Vita_TagHudCap(void);
/* (bt3.txt taguivita, src/menu/port_screen.c under PORT_VITA) the screens' lines written with the capitals they show,
   in pixels (gVitaTboWantCap, 0: as below), and made gVitaTboWantSx / Sy thousandths as wide / tall when the renderer
   takes them (vita_tbo.c line_pixels): a texel a pixel. A line of the same words at another size is another line. Set
   by the game's thread around PortTbo_TextId / PortTbo_TextMeasure only. */
int gVitaTboWantCap, gVitaTboWantSx = 1000, gVitaTboWantSy = 1000;
static int line_cap(int kind) {
    int f = Vita_TagHudCap();
    if (gVitaTboWantCap > 0 && kind != 2) {
        return gVitaTboWantCap;
    }
    return f > 0 ? f : gVitaTboTextSize ? (kind == 0 ? 40 : 24) : (kind == 0 ? 80 : 48);
}
static int line_outline(int kind) { return gVitaTboTextSize || Vita_TagHudCap() > 0 || gVitaTboWantCap > 0 ? 1 : (kind == 0 ? 3 : 2); }
/* (a heading: gVitaTboWantCap -1, written at its row's own resolution, menu_text.c HEADING_RES) */
static int line_fine(int kind) { return Vita_TagHudCap() > 0 || (kind == 2 ? gVitaTboWantCap < 0 : gVitaTboWantCap > 0); }
#else
static int line_cap(int kind) { return kind == 0 ? 80 : 48; }
static int line_outline(int kind) { return kind == 0 ? 3 : 2; }
#endif
/* Keep the fill and outline on aligned, power-of-two rows. Otherwise their
 * shared edge drifts through odd mip heights and clips short menu labels. */
static int line_extent(int value){int size=1;while(size<value&&size<16384)size*=2;return size;}

static int line_measure(TboLine *l, uint32_t generation) {
    l->generation = generation;
    l->version = ++sVersion;
    if (sVersion == 0) {
        l->version = sVersion = 1;
    }
#ifdef PORT_VITA
    l->vsx = gVitaTboWantSx;
    l->vsy = gVitaTboWantSy;
    l->fine = line_fine(l->kind);
#endif
    if (l->kind == 2) {
        l->outline = 0;
        l->advance = MenuText_HeadingMeasure(l->text, &l->cap, &l->w, &l->h, &l->base, &l->pad);
        return l->advance > 0;
    }
    l->cap = line_cap(l->kind);
    l->outline = line_outline(l->kind);
    l->pad = l->outline + 2;
    l->advance = MenuText_LineMeasure(l->kind, l->text, l->cap, l->pad, &l->w, &l->h, &l->base);
    return l->advance > 0;
}

/* The number of a line (TBO_TEXT_BASE and on), -1 if it cannot be written (no font, no text, no room). */
static int text_id(const char *text, int kind, int translate) {
    if(translate)text=PortOnline_Text(text);
    uint32_t generation = MenuText_LineGeneration();
    uint64_t now = SDL_GetTicks();
    int i, spare = -1, id = -1;

    if (text == NULL || text[0] == '\0' || kind < 0 || kind > 2) {
        return -1;
    }
    {   /* a line of the menu in the chosen language (translations/port/<language>.json; as it is without one), here
           where each is drawn and measured, so the two always agree */
        const char *t = translate ? MenuTranslation_Line(text) : NULL;
        if (t != NULL) {
            text = t;
        }
    }
    pthread_mutex_lock(&sLock);
    for (i = 0; i < TBO_TEXT_SLOTS; i++) {
        TboLine *l = &sLines[i];
#ifdef PORT_VITA
        /* (taguivita: the size it is written at and its narrowing are part of the line) */
        if (l->version != 0 && l->kind == kind && strncmp(l->text, text, TEXT_MAX - 1) == 0 &&
            (kind == 2 || gVitaTboWantCap <= 0 || l->cap == gVitaTboWantCap) &&
            (gVitaTboWantCap == 0 ? l->vsx == 1000 && l->vsy == 1000 : l->vsx == gVitaTboWantSx && l->vsy == gVitaTboWantSy)) {
#else
        if (l->version != 0 && l->kind == kind && strncmp(l->text, text, TEXT_MAX - 1) == 0) {
#endif
            l->askedMs = now; l->askedFrame = gGsFrame;
#ifdef PORT_VITA
            if ((l->generation != generation || l->fine != line_fine(kind) ||
                 (gVitaTboWantCap == 0 && kind != 2 && l->cap != line_cap(kind))) && !line_measure(l, generation)) {
#else
            if (l->generation != generation && !line_measure(l, generation)) {
#endif
                break;
            }
            id = l->advance > 0 ? TBO_TEXT_BASE + i : -1;
            break;
        }
        if (spare < 0 && (l->version == 0 || (now - l->askedMs >= IDLE_MS && gGsFrame - l->askedFrame > 2))) {
            spare = i;
        }
    }
    if (i == TBO_TEXT_SLOTS && spare >= 0) {
        TboLine *l = &sLines[spare];
        SDL_strlcpy(l->text, text, sizeof(l->text));
        l->kind = kind;
        l->askedMs = now; l->askedFrame = gGsFrame;
        if (line_measure(l, generation)) {
            id = TBO_TEXT_BASE + spare;
        }
    }
    pthread_mutex_unlock(&sLock);
#ifdef PORT_VITA
    if (id >= 0) {
        /* (bt3.txt tbotext=1: the line's picture made now, on the thread that asks for it, unless it is made already:
           ready before the picture that shows it is drawn, never a line coming in pictures later; port/vita/vita_tbo.c) */
        extern void Vita_TboLineAsked(uint32_t id, uint32_t version);
        Vita_TboLineAsked((uint32_t)id, TboText_Version((uint32_t)id));
    }
#endif
    return id;
}
int PortTbo_TextId(const char *text,int kind){return text_id(text,kind,1);}
int PortTbo_TextIdLiteral(const char *text,int kind){return text_id(text,kind,0);}

/* How far the pen goes for a line, in thousandths of its capitals' height, without keeping it (for laying out:
   where to break a line). 0 without a font. */
static int text_measure(const char *text, int kind, int translate) {
    if(translate)text=PortOnline_Text(text);
    int w, h, base, cap, adv;
    if (text == NULL || text[0] == '\0' || kind < 0 || kind > 1) {
        return 0;
    }
    {
        const char *t = translate ? MenuTranslation_Line(text) : NULL;
        if (t != NULL) {
            text = t;
        }
    }
    cap = line_cap(kind);
    adv = MenuText_LineMeasure(kind, text, cap, 0, &w, &h, &base);
    return (int)((int64_t)adv * 1000 / cap);
}
int PortTbo_TextMeasure(const char *text,int kind){return text_measure(text,kind,1);}
int PortTbo_TextMeasureLiteral(const char *text,int kind){return text_measure(text,kind,0);}

/* A measure of a line, in thousandths of its capitals' height: 0 how far the pen goes, 1 the picture's width,
   2 its height (of one of its two halves; a heading's: all of it), 3 the baseline from its top, 4 the room left of
   the pen's start. */
int PortTbo_TextInfo(int id, int what) {
    int r = 0, v;
    pthread_mutex_lock(&sLock);
    if (id >= TBO_TEXT_BASE && id < TBO_TEXT_BASE + TBO_TEXT_SLOTS) {
        const TboLine *l = &sLines[id - TBO_TEXT_BASE];
        v = what == 0 ? l->advance : what == 1 ? (l->kind==2?l->w:line_extent(l->w)) : what == 2 ? (l->kind==2?l->h:line_extent(l->h)) : what == 3 ? l->base : what == 4 ? l->pad : 0;
        r = l->cap > 0 ? (int)((int64_t)v * 1000 / l->cap) : 0;
    }
    pthread_mutex_unlock(&sLock);
    return r;
}

#ifdef PORT_VITA
/* (vita_tbo.c, a diagnostic) the words of line `id`, copied into out */
void TboText_Words(uint32_t id, char *out, int size) {
    out[0] = 0;
    pthread_mutex_lock(&sLock);
    if (id >= TBO_TEXT_BASE && id < TBO_TEXT_BASE + TBO_TEXT_SLOTS) {
        const TboLine *l = &sLines[id - TBO_TEXT_BASE];
        snprintf(out, (size_t)size, "%s (kind %d, cap %d, %d x %d, narrow %d/%d)", l->text, l->kind, l->cap, l->w, l->h, l->vsx, l->vsy);
    }
    pthread_mutex_unlock(&sLock);
}
/* (vita_tbo.c line_pixels, taguivita) how much narrower / shorter the line's picture is made, in thousandths: 1 when
   it is to be, 0 none */
int TboText_VitaScale(uint32_t id, int *sx, int *sy) {
    int r = 0;
    pthread_mutex_lock(&sLock);
    if (id >= TBO_TEXT_BASE && id < TBO_TEXT_BASE + TBO_TEXT_SLOTS) {
        const TboLine *l = &sLines[id - TBO_TEXT_BASE];
        *sx = l->vsx;
        *sy = l->vsy;
        r = l->fine && (l->vsx != 1000 || l->vsy != 1000) && l->vsx > 0 && l->vsy > 0;
    }
    pthread_mutex_unlock(&sLock);
    return r;
}
/* (vita_tbo.c tbo_make, taghudfine) the line was written at the HUD's capitals: no smaller copies */
int TboText_Fine(uint32_t id) {
    int r = 0;
    pthread_mutex_lock(&sLock);
    if (id >= TBO_TEXT_BASE && id < TBO_TEXT_BASE + TBO_TEXT_SLOTS) {
        r = sLines[id - TBO_TEXT_BASE].fine;
    }
    pthread_mutex_unlock(&sLock);
    return r;
}
#endif

/* The version of a line's place (0: nothing there). */
uint32_t TboText_Version(uint32_t id) {
    uint32_t r = 0;
    pthread_mutex_lock(&sLock);
    if (id >= TBO_TEXT_BASE && id < TBO_TEXT_BASE + TBO_TEXT_SLOTS) {
        r = sLines[id - TBO_TEXT_BASE].version;
    }
    pthread_mutex_unlock(&sLock);
    return r;
}

/* The outline's coverage: for every pixel the largest coverage of those within the disc dx*dx + dy*dy <= r*r + r, as the
   loop in TboText_Pixels finds it (the same numbers), but a row at a time: the largest of a row within k pixels (a plane
   for each k, each made from the one before) and, for each row of the disc, the plane of its width. About a tenth of the
   work. NULL if there is no memory (the loop is used). free() it. (bt3.txt tbopre, the Vita: gTboFastOutline) */
#ifdef PORT_VITA
int gTboFastOutline = 1;
#else
int gTboFastOutline = 0;
#endif
__attribute__((optimize("O3")))
static uint8_t *tbo_outline(const uint8_t *cov, int lw, int lh, int r) {
    int hw[16], maxw = 0, dy, k, y, x;
    uint8_t *planes[16] = {0}, *out;
    size_t n = (size_t)lw * lh;

    if (r < 0 || r > 7 || lw < 1 || lh < 1) {
        return NULL;
    }
    for (dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) {
            dx++;
        }
        hw[dy + r] = dx;
        if (dx > maxw) {
            maxw = dx;
        }
    }
    out = calloc(n, 1);
    if (out == NULL) {
        return NULL;
    }
    planes[0] = (uint8_t *)cov;
    for (k = 1; k <= maxw; k++) {
        planes[k] = malloc(n);
        if (planes[k] == NULL) {
            for (k--; k >= 1; k--) {
                free(planes[k]);
            }
            free(out);
            return NULL;
        }
        for (y = 0; y < lh; y++) {
            const uint8_t *prev = planes[k - 1] + (size_t)y * lw, *c = cov + (size_t)y * lw;
            uint8_t *dst = planes[k] + (size_t)y * lw;
            for (x = 0; x < lw; x++) {
                uint8_t v = prev[x], a = x >= k ? c[x - k] : 0, b = x + k < lw ? c[x + k] : 0;
                v = a > v ? a : v;
                dst[x] = b > v ? b : v;
            }
        }
    }
    for (y = 0; y < lh; y++) {
        uint8_t *dst = out + (size_t)y * lw;
        for (dy = -r; dy <= r; dy++) {
            const uint8_t *src;
            int yy = y + dy;
            if (yy < 0 || yy >= lh) {
                continue;
            }
            src = planes[hw[dy + r]] + (size_t)yy * lw;
            for (x = 0; x < lw; x++) {
                dst[x] = src[x] > dst[x] ? src[x] : dst[x];
            }
        }
    }
    for (k = 1; k <= maxw; k++) {
        free(planes[k]);
    }
    return out;
}

/* The picture of a line for the renderer: *w x *h RGBA (the GS's alpha, 0x80 opaque), the filled line in the upper
   half and its outline in the lower (a heading: itself); NULL if there is none. free() it. *version: what it
   shows. */
uint32_t *TboText_Pixels(uint32_t id, uint32_t *version, uint32_t *w, uint32_t *h) {
    TboLine l;
    uint8_t *cov, *outl;
    uint32_t *px;
    int lw, lh, lb, x, y, dx, dy, r,tw,th;

    if (id < TBO_TEXT_BASE || id >= TBO_TEXT_BASE + TBO_TEXT_SLOTS) {
        return NULL;
    }
    pthread_mutex_lock(&sLock);
    l = sLines[id - TBO_TEXT_BASE];
    pthread_mutex_unlock(&sLock);
    *version = l.version;
    if (l.version == 0 || l.advance <= 0) {
        return NULL;
    }
    if (l.kind == 2) {
        px = (uint32_t *)(void *)MenuText_HeadingRender(l.text, &lw, &lh);
        if (px != NULL && (lw != l.w || lh != l.h)) {
            free(px);
            px = NULL;
        }
        *w = (uint32_t)lw;
        *h = (uint32_t)lh;
        return px;
    }
    cov = MenuText_LineRender(l.kind, l.text, l.cap, l.pad, &lw, &lh, &lb);
    if (cov == NULL) {
        return NULL;
    }
#ifdef PORT_VITA
    if ((lw != l.w || lh != l.h) && lw > 0 && lh > 0 && lw <= l.w + 8 && lh <= l.h + 8 && l.w <= lw + 8 && l.h <= lh + 8) {
        /* (the Vita: a few pixels otherwise than measured; the Tag screens' "Change", 2026-11-19, never got a picture
           this way: the coverage cut or padded to the measured size, the letters where they are) */
        static int told;
        uint8_t *fit = calloc((size_t)l.w * l.h, 1);
        if (told < 8) {
            extern void Vita_Log(const char *fmt, ...);
            told++;
            Vita_Log("tbo_text: \"%s\" written %d x %d, measured %d x %d: fitted\n", l.text, lw, lh, l.w, l.h);
        }
        if (fit != NULL) {
            for (y = 0; y < lh && y < l.h; y++) {
                memcpy(fit + (size_t)y * l.w, cov + (size_t)y * lw, (size_t)(lw < l.w ? lw : l.w));
            }
            free(cov);
            cov = fit;
            lw = l.w;
            lh = l.h;
        }
    }
#endif
    if (lw != l.w || lh != l.h) { /* (the font changed since it was measured: it is measured again next frame) */
#ifdef PORT_VITA
        static int toldOther;
        if (toldOther < 8) {
            extern void Vita_Log(const char *fmt, ...);
            toldOther++;
            Vita_Log("tbo_text: \"%s\" written %d x %d, measured %d x %d: no picture\n", l.text, lw, lh, l.w, l.h);
        }
#endif
        free(cov);
        return NULL;
    }
    tw=line_extent(lw);th=line_extent(lh);
    px = malloc((size_t)tw * th * 2 * 4);
    if (px == NULL) {
        free(cov);
        return NULL;
    }
    r = l.outline;
    for(y=0;y<th*2;++y)for(x=0;x<tw;++x)px[(size_t)y*tw+x]=0x00FFFFFFu;
    outl = gTboFastOutline ? tbo_outline(cov, lw, lh, r) : NULL;
    for (y = 0; y < lh; y++) {
        for (x = 0; x < lw; x++) {
            int fill = cov[(size_t)y * lw + x], line = 0;
            if (outl != NULL) {
                line = outl[(size_t)y * lw + x];
                dy = r + 1; /* (no loop below) */
            } else {
                dy = -r;
            }
            for (; dy <= r && line < 255; dy++) {
                int yy = y + dy;
                if (yy < 0 || yy >= lh) {
                    continue;
                }
                for (dx = -r; dx <= r; dx++) {
                    int xx = x + dx, c;
                    if (xx < 0 || xx >= lw || dx * dx + dy * dy > r * r + r) {
                        continue;
                    }
                    c = cov[(size_t)yy * lw + xx];
                    if (c > line) {
                        line = c;
                    }
                }
            }
            px[(size_t)y * tw + x] = 0xFFFFFFu | (uint32_t)((fill * 128 + 127) / 255) << 24;
            px[(size_t)(y + th) * tw + x] = 0xFFFFFFu | (uint32_t)((line * 128 + 127) / 255) << 24;
        }
    }
    free(outl);
    free(cov);
    *w = (uint32_t)tw;
    *h = (uint32_t)th * 2;
    return px;
}
