/*
 * BT4decomp's own screens on the PS Vita: the pictures and the TrueType lines that its own code draws, as its PC
 * renderer gives them (port/src/gs/gs_draw.c: tbo_texture, tbo_text_texture), for the Vita's renderer
 * (port/vita/vita_vgl.c, state_of_).
 *
 * Who draws them: the Tag Team screens (src/menu/port_tbo.c, opened from the main menu's Tag Team entry and from the
 * Duel menu's Tag type), the kit they draw with (src/menu/port_screen.c), the main menu's Tag Team entry
 * (src/menu/menu_a.c) and the Tag Team fight's words (src/battle/tag_team_hud.inc: the timer, the fighters' names over
 * the target, the gauges' figures, the joint attacks' and fusions' labels). Each is a GS sprite whose TEX0 has the
 * reserved format TBO_PSM and the picture's number in CBP (include/menu/tbo_art.h); the renderer asks here for a
 * texture of that number:
 *   - 0 .. TBO_ART_COUNT-1: app0:tbo/ui/<name>.png (port/tbo/ui in the repository, in the package by build.py), with
 *     the PC's own touches (the window's teal, the logo's halo, the main menu's emblem cut from the logo);
 *   - TBO_TEXT_BASE ..: a line of TrueType letters (port/src/gs/tbo_text.c, port/src/gs/menu_text.c, unchanged; the
 *     fonts are app0:fonts with fonts.tsv, port/third_party/fonts), made again whenever the line's version changes;
 *   - TBO_MAIN_TAG_OFF / ON: "Tag Team" in the main menu's lettering, put together from the letters of the player's
 *     own disc as on PC (port/src/gs/bt4_main_lettering.inc), in a thread of its own the first time;
 *   - TBO_TAG_MOVIE: the PC plays a film behind the Tag Team screens (tbo/ui/tag_ui_*.pss); with bt3.txt tbomovie=1
 *     the Vita plays it too, H.264 on the console's hardware player (vita_tagmovie.c, its texture; the films are
 *     tbo/ui/tag_ui_{intro,loop}.mp4); with tbomovie=2 the PC's own MPEG-2 films in software (vita_tagmovie_sw.c, only in
 *     a package built with BT4_VITA_TAGMOVIE=1). Otherwise, or until its first frame, its poster (TBO_TAG_ART), the picture the
 *     PC shows until the film's first frame.
 * The online hub's art (the radar, the hosts, the tournament grounds), the European disc's language selector and
 * the ranked Dragon Balls are not asked for offline; none of them is made here (0: not drawn).
 *
 * Sizes: the PC keeps every picture at its own size with its smaller copies (mipmaps). The Vita's screen is 960 x 544
 * and its renderer samples without mipmaps, so each picture is made smaller once, by halves (each pixel the average of
 * four, the colour weighted by the alpha as the PC's copies are), to about the size it is shown at: the backdrops up
 * to 1280 across, the other pictures up to 640, the lines of letters up to 128 rows (their capitals 20-24 pixels,
 * shown at 7-20). The layout is not changed: the screens place a picture by its measures in units (tbo_sizes.h,
 * PortTbo_TextInfo), never by the texture's pixels.
 *
 * What is kept: a texture not drawn for 600 pictures (20 s) is given back (the pictures of the menus during a fight),
 * but the main menu's emblem and lettering; a line's texture when its line changed (at the picture's end). All made
 * and given back on the drawing thread, the render thread's work done first (vita_vgl.c Vita_VglTexRgba,
 * Vita_VglTexFree).
 *
 * bt3.txt tboui: 1 (default) drawn; 0 not drawn, as before (src/menu/port_screen.c puts neither the pictures' sprites
 * nor the lines together, and the renderer refuses any that comes); 2 the soak's B battles only. The report's "tboui"
 * line: pictures and lines made, given back, textures held and their KB.
 */
#include <png.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/stat.h>
#include "vita_paths.h"
#include "../../include/menu/tbo_art.h"
#include "vita_hud_atlas.h" /* (TBO_VITA_HUD) */
#include "vita_ui_atlas.h"  /* (TBO_VITA_UI) */

extern void Vita_Log(const char *fmt, ...);
extern unsigned gGsFrame;
extern unsigned Vita_VglTexRgba(const void *px, int w, int h);
extern unsigned Vita_VglTexRgbaMip(const void *px, int w, int h); /* the same with its smaller copies (tbomip=1) */
extern void Vita_VglTexFree(unsigned t);
extern uint32_t TboText_Version(uint32_t id);                                       /* port/src/gs/tbo_text.c */
extern uint32_t *TboText_Pixels(uint32_t id, uint32_t *version, uint32_t *w, uint32_t *h);
extern int MenuLabels_Bt4MainItem(const char *root, const char *label, unsigned char *out[2]); /* bt4_main_lettering.inc */
extern int gVitaTagMovie;                /* vita_tagmovie.c: bt3.txt tbomovie */
extern unsigned Vita_TagMovieTexture(void);
extern void Vita_TagMovieIdle(void);
extern unsigned Vita_TagMovieSwTexture(void); /* vita_tagmovie_sw.c: tbomovie=2, the software path */
extern void Vita_TagMovieSwIdle(void);
extern int Vita_MemberFile(const char *archive, int flid, const char *dir);         /* vita_file.c */

int gVitaTboUi = 1;
unsigned gVitaTboN[6]; /* (the report) pictures made, lines made, textures given back, textures held, bytes held */

#define KEEP_FRAMES 600 /* not drawn for that long: given back */

/* bt3.txt tbotext (default 1 since wip/tagtext; 0 v1.4's way with tbopre): a line's picture is made when the screen's
   code asks for the line (PortTbo_TextId, tbo_text.c: on the game's thread, before the picture that shows it is put
   together) instead of by the maker or the drawing thread at its first draw, and the drawing thread uploads it in that
   picture whatever the upload budget: every line of a screen or of the fight's HUD is there from the first picture
   that shows it (the developer, v1.4: "the strings are loaded little by little, also inside the fights"), and a line's
   place taken by another line never shows the old line's letters meanwhile (it showed them until the maker came to it).
   The cost: the lines of a page that comes in are written together, on the game's thread, once (the screen comes a few
   pictures later, whole, as the game's own screens after a load).
   bt3.txt tbomip (default 1 since wip/tagtext; 0 as v1.4): the lines' and the pictures' textures with their smaller
   copies (mipmaps, made by the chip: glGenerateMipmap), sampled trilinearly, as the PC samples them (gs_draw.c
   tbo_upload: "one sample of the full picture per pixel shimmers"). v1.4 sampled a line written with capitals of 40
   pixels where it shows them at 7-20 with no smaller copy: grainy, broken letters (the developer: "too small, grainy").
   Only for textures whose sides are powers of two (the lines always; the buttons; not the backdrop); the pictures'
   see-through pixels take the colour of the nearest seen one first (tbo_bleed), so their edges do not darken. */
int gVitaTboText = 1;
int gVitaTboMip = 1;
unsigned gVitaTboLines[3];
unsigned gVitaTboGated; /* (the report) pictures' draws held back while a screen waited for its lines */
/* bt3.txt tbotextpct (default 100, the PC's size): the Tag Team screens' and HUD's lines of letters (not the talk box's,
   not the headings) drawn this many hundredths of their size, their capitals' middle kept (src/menu/port_screen.c
   Ps_TextExOuterXY, Ps_TextWidth, under PORT_VITA): to try on the console whether they read as the game's own text */
int gVitaTboTextPct = 100;
/* bt3.txt tagui_scale / taghud_scale (percent; defaults 130 / 135 since wip/v14rel after v1.4.1; 100 the PC's sizes):
   on the Vita's 5-inch screen the Tag Team screens' rows had capitals of 12-13 pixels against the main menu's 18-20, and
   the fight's HUD was smaller still (the developer: "very small, unclear"). The screens' letters (Ps_TextEx*, the talk
   box's Ps_Msg*: src/menu/port_screen.c) are drawn this much larger, measured as much (a row's value squeezed to its
   room, the talk box's lines broken again), their capitals' middle where it was; in a fight taghud_scale is used, and
   the HUD's gauges, the target's marks and the allies' column grow by it too (src/battle/tag_team_hud.inc, PORT_VITA).
   tbotextpct (earlier) multiplies both. */
int gVitaTagUiScale = -1, gVitaTagHudScale = 135; /* (tagui_scale -1: 100 with taguivita, 130 without; below) */
/* bt3.txt taguivita (default 1 since branch wip/tagui-vita, not compared on the console yet; 0 the way before; 2 the
   soak's B battles): the Tag Team screens (setup pages, character select's captions, talk box, headings, prompts, the
   main menu's emblem) drawn the way the game's own menus are (docs/vita/tagteam.md "The Vita's Tag Team screens"):
   - the kit's pieces (bars, windows, talk box, buttons, logo, emblem) from one atlas made at build time at the pixel
     size they show on the 960 x 544 screen (tbo/ui/tag_ui_vita.vt, 512 x 248, 496 KB, resident; tools/vita_ui_atlas.py),
     sampled 1:1 without smaller copies, instead of ~20 textures of the PC's pictures stretched to powers of two and
     sampled down through their mipmaps;
   - every line of letters written with the capitals it shows (the capitals' pixels rounded, the line drawn that size)
     and, as the page narrows the letters across, made that narrow at write time with an area filter: one texel a
     pixel, its corner on a whole pixel, no smaller copies (tbo_text.c; line_pixels below); the headings written at
     their row's own resolution (menu_text.c HEADING_RES 1);
   - the letters at the PC's proportions on the page (tagui_scale 100: at 130 the rows' words filled their bars and the
     talk box ran to three lines);
   - the backdrop (2 MB) given back in a fight and read again after it (it was kept for the whole session). */
int gVitaTagUiVita = 1;
int Vita_TagUiVita(void);
/* bt3.txt taghudvita (default 1 since branch wip/taghud-vita, not compared on the console yet; 0 the way before; 2 the
   soak's B battles): the Tag Team fight's HUD drawn the way the game's own HUD is (the developer: "optimized for the
   Vita as the normal fights' HUD is, in proportions and resolution"; docs/vita/tagteam.md "The Vita's Tag Team HUD"):
   - the renderer does not narrow its pieces a second time (vita_vgl.c GsGpu_Native 0x19, the narrowing): they are
     placed on the page already (the PC's gs_draw.c leaves them so). Before, the gauges' sprites and every line of
     letters were narrowed to 3/4 about the screen's middle: the gauge 80 pixels in from the left, letters squeezed;
   - the player's gauge exactly as the 1 vs 1 HUD's (its size and place), the allies' column at 0.6 of it, the
     floating allies' at 0.34, the game's own timer (hud_a.c) instead of the PC's line (tag_team_hud.inc);
   - the radar, the target's rings, the radar's contacts and the stick's glyph textured quads of one atlas made at build
     time at their size in the 640 x 368 picture (tbo/ui/tag_hud_vita.vt, port/vita/tools/vita_hud_atlas.py), drawn with
     the HUD's colours: no thin strokes (the radar was ~150 triangles, the rings 64 strokes);
   - the words written with capitals of TAGHUD_VITA_CAP pixels without smaller copies (as taghudfine) and drawn
     TAGHUD_VITA_TEXT times the PC's size (capitals 7-10 pixels as the game's HUD text), with the outline and one fill
     (2 sprites a line, not 12: the 8 copies of the outer border, 0.3 pixels out, are left out). */
int gVitaTagHudVita = 1;
#define TAGHUD_VITA_TEXT 1.6f
#define TAGHUD_VITA_CAP 11
static float pct_scale(int pct) {
    return pct >= 50 && pct <= 200 ? (float)pct / 100.0f : 1.0f;
}
float Vita_TagHudScale(void) {
    return pct_scale(gVitaTagHudScale);
}
int Vita_TagHudVita(void);
float Vita_TboTextScale(void) {
    extern int Vita_InBattle(void);
    if (Vita_InBattle() && Vita_TagHudVita()) { /* (taghudvita: the HUD's words at the game's HUD text size, below) */
        return pct_scale(gVitaTboTextPct) * TAGHUD_VITA_TEXT;
    }
    return pct_scale(gVitaTboTextPct) *
           pct_scale(Vita_InBattle() ? gVitaTagHudScale : gVitaTagUiScale >= 0 ? gVitaTagUiScale : Vita_TagUiVita() ? 100 : 130);
} /* (the report) lines made when asked, their ms, lines that had to wait for a picture */
static int tbo_ab(int v) { /* (2: the soak's B battles only) */
    extern int gVitaAbSide;
    return v == 2 ? gVitaAbSide == 1 : v != 0;
}
/* the atlas (tbo/ui/tag_ui_vita.vt) not in the package (an eboot.bin sent over an older install): taguivita's way is then
   left for the separate pictures, so that the main menu's emblem and the screens' kit and buttons still show (the
   console's run vgr1, 2026-11-19: "cannot read app0:tbo/ui/tag_ui_vita.vt", no emblem, no buttons) */
static volatile int sUiAtlasGone;
int Vita_TagUiVita(void) {
    return tbo_ab(gVitaTagUiVita) && !sUiAtlasGone;
}
/* (src/menu/tag_team_select.inc) the screens' letters at 100 because of taguivita, not because tagui_scale says so */
int Vita_TagUiDefault100(void) {
    return gVitaTagUiScale < 0 && Vita_TagUiVita();
}
/* (src/menu/port_screen.c) the screens' kit from the atlas and their lines at their pixel size: the menus only (a
   fight's pieces are the HUD's, taghudvita) */
int Vita_TagUiMenus(void) {
    extern int Vita_InBattle(void);
    return Vita_TagUiVita() && !Vita_InBattle();
}
#define TEXT_ON() tbo_ab(gVitaTboText)
#define MIP_ON() tbo_ab(gVitaTboMip)
/* bt3.txt taghudfine (0 off, the default until compared on the console; 1 on; 2 the soak's B battles): the Tag Team
   fight's HUD drawn for the picture it lands in. A fight is drawn at 640 x 368 (the system stretches it to the
   screen), where a page unit is 0.82 pixels down: the HUD's labels have capitals of 4-10 pixels, made from lines
   written with capitals of 40 and sampled down through their smaller copies (soft, grainy), and its thinnest
   strokes (the target's inner ring, 0.55 units) are under a pixel wide (broken). With it, in a battle: the lines are
   written with capitals of TAGHUD_CAP pixels, about their size on the screen, and drawn without smaller copies
   (tbo_text.c line_cap; tbo_make), and no stroke is drawn thinner than 1.25 pixels of the fight's picture
   (tag_team_hud.inc TagHud_Stroke). The layout does not change (the lines' measures are in thousandths of their
   capitals; the PC's layout logic as it is). */
int gVitaTagHudFine = 0;
#define TAGHUD_CAP 12
int Vita_TagHudFine(void) {
    extern int Vita_InBattle(void);
    return tbo_ab(gVitaTagHudFine) && Vita_InBattle();
}
int Vita_TagHudVita(void) {
    return tbo_ab(gVitaTagHudVita);
}
static int taghud_vita_battle(void) {
    extern int Vita_InBattle(void);
    return Vita_TagHudVita() && Vita_InBattle();
}
int Vita_TagHudCap(void) { /* (tbo_text.c: 0 for the usual capitals) */
    return taghud_vita_battle() ? TAGHUD_VITA_CAP : Vita_TagHudFine() ? TAGHUD_CAP : 0;
}
float Vita_TagHudMinStroke(void) { /* (tag_team_hud.inc) in page units; 0 off */
    extern int gVitaFightW;
    int h = gVitaFightW >= 960 ? 544 : gVitaFightW >= 720 ? 408 : 368;
    return Vita_TagHudFine() || taghud_vita_battle() ? 1.25f * 448.0f / (float)h : 0.0f;
}
static int tbo_pot(uint32_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

typedef struct {
    unsigned tex, bytes, version, frame;
    unsigned char missing; /* not found or not readable: not looked for again */
    /* (tbopre=1, the maker below: 0 nothing asked, 1 asked or being made, 2 made and waiting in px, 3 nothing came of it) */
    unsigned char state;
    uint32_t *px, w, h, pxVersion, failVersion, retry, readyFrame;
    uint32_t madeVersion; /* (tbotext=1, a line: the version whose picture was made by Vita_TboLineAsked; 0 none) */
    unsigned char mip;    /* the texture has its smaller copies (tbomip=1): sampled with them */
    unsigned lineTries;   /* (a line: times in a row its picture could not be made) */
} TboTex;

#define TBO_NAME(id, name) name,
static const char *const kTboNames[TBO_ART_COUNT] = {TBO_ART_LIST(TBO_NAME)};
#undef TBO_NAME
static TboTex sPic[TBO_ART_COUNT], sText[TBO_TEXT_SLOTS], sEmblem, sLetters[2], sGuideLetters[2];
static TboTex sHudAtlas; /* (taghudvita: tbo/ui/tag_hud_vita.vt, 128 x 112, 56 KB, kept from its first use) */
static TboTex sUiAtlas;  /* (taguivita: tbo/ui/tag_ui_vita.vt, 512 x 248, 496 KB, kept from the first seconds) */

static int tbo_on(void) {
    extern int gVitaAbSide;
    return gVitaTboUi == 2 ? gVitaAbSide == 1 : gVitaTboUi != 0;
}
/* (src/menu/port_screen.c, under PORT_VITA: with tboui=0 the screens' sprites and lines are not even put together) */
int Vita_TboOn(void) {
    return tbo_on();
}

/* A line's texture replaced is given back at the picture's end, not at once: a draw of this picture may still hold
   it (vita_vgl.c's states of this picture, the batches not yet handed over). */
static unsigned sLate[64], sLateBytes[64];
static int sLateN;

/* ---- pictures made smaller: by halves, each pixel the average of four, the colour weighted by the alpha (as the
   PC's smaller copies, gs_draw.c tbo_upload); RGBA with the GS's alpha (0..0x80). Takes px. ---- */
static uint32_t *tbo_half(uint32_t *px, uint32_t *w, uint32_t *h) {
    uint32_t pw = *w, ph = *h, nw = pw > 1 ? pw / 2 : 1, nh = ph > 1 ? ph / 2 : 1, x, y, k, ch;
    uint32_t *dst = malloc((size_t)nw * nh * 4);

    if (dst == NULL) {
        return px; /* (as it is) */
    }
    for (y = 0; y < nh; y++) {
        for (x = 0; x < nw; x++) {
            uint32_t x0 = x * 2 < pw ? x * 2 : pw - 1, x1 = x * 2 + 1 < pw ? x * 2 + 1 : pw - 1;
            uint32_t y0 = y * 2 < ph ? y * 2 : ph - 1, y1 = y * 2 + 1 < ph ? y * 2 + 1 : ph - 1;
            uint32_t c[4] = {px[y0 * pw + x0], px[y0 * pw + x1], px[y1 * pw + x0], px[y1 * pw + x1]}, out = 0, aw = 0;
            for (k = 0; k < 4; k++) {
                aw += c[k] >> 24;
            }
            for (ch = 0; ch < 24; ch += 8) { /* colour weighted by alpha: no dark rims round what is cut out */
                uint32_t sum = 0;
                for (k = 0; k < 4; k++) {
                    sum += ((c[k] >> ch) & 0xFF) * ((c[k] >> 24) + (aw == 0 ? 1 : 0));
                }
                out |= (aw != 0 ? sum / aw : sum / 4) << ch;
            }
            dst[y * nw + x] = out | ((aw + 2) / 4) << 24;
        }
    }
    free(px);
    *w = nw;
    *h = nh;
    return dst;
}

static uint32_t *tbo_fit(uint32_t *px, uint32_t *w, uint32_t *h, uint32_t maxW, uint32_t maxH) {
    while (px != NULL && (*w > maxW || *h > maxH) && *w > 1 && *h > 1) {
        uint32_t ow = *w;
        px = tbo_half(px, w, h);
        if (*w == ow) {
            break; /* (no memory to make it smaller) */
        }
    }
    return px;
}

/* A picture's see-through pixels (alpha 0) given the colour of the seen ones nearest (pull-push: the colour averaged
   by halves, weighted by the alpha, then given back down to the pixels that had none), so that the chip's smaller copies
   (a plain average of four, glGenerateMipmap) do not darken the edges with the black of the see-through. Only the
   colour of pixels nobody sees changes. (tbomip=1, the maker's thread) */
static void tbo_bleed(uint32_t *px, uint32_t w, uint32_t h) {
    uint32_t *lv[14], lw[14], lh[14], x, y, k;
    int n = 1, i;
    lv[0] = px;
    lw[0] = w;
    lh[0] = h;
    while (n < 14 && (lw[n - 1] > 1 || lh[n - 1] > 1)) {
        uint32_t pw = lw[n - 1], ph = lh[n - 1], nw = pw > 1 ? pw / 2 : 1, nh = ph > 1 ? ph / 2 : 1;
        uint32_t *src = lv[n - 1], *dst = malloc((size_t)nw * nh * 4);
        if (dst == NULL) {
            break;
        }
        for (y = 0; y < nh; y++) {
            for (x = 0; x < nw; x++) {
                uint32_t xs[2] = {x * 2 < pw ? x * 2 : pw - 1, x * 2 + 1 < pw ? x * 2 + 1 : pw - 1};
                uint32_t ys[2] = {y * 2 < ph ? y * 2 : ph - 1, y * 2 + 1 < ph ? y * 2 + 1 : ph - 1};
                uint32_t sum[3] = {0, 0, 0}, aw = 0, out = 0, ch;
                for (k = 0; k < 4; k++) {
                    uint32_t c = src[ys[k >> 1] * pw + xs[k & 1]], a = c >> 24;
                    aw += a;
                    for (ch = 0; ch < 3; ch++) {
                        sum[ch] += ((c >> (ch * 8)) & 0xFF) * a;
                    }
                }
                for (ch = 0; ch < 3 && aw != 0; ch++) {
                    out |= (sum[ch] / aw) << (ch * 8);
                }
                dst[y * nw + x] = out | (aw != 0 ? 0x01000000u : 0); /* (alpha here: has a colour or not) */
            }
        }
        lv[n] = dst;
        lw[n] = nw;
        lh[n] = nh;
        n++;
    }
    for (i = n - 2; i >= 0; i--) { /* from the smallest up: a pixel with no colour takes the one below it */
        uint32_t *up = lv[i + 1], uw = lw[i + 1], uh = lh[i + 1];
        for (y = 0; y < lh[i]; y++) {
            for (x = 0; x < lw[i]; x++) {
                uint32_t *c = &lv[i][y * lw[i] + x];
                if ((*c >> 24) == 0) {
                    uint32_t ux = x / 2 < uw ? x / 2 : uw - 1, uy = y / 2 < uh ? y / 2 : uh - 1;
                    *c = (up[uy * uw + ux] & 0xFFFFFFu) | (i == 0 ? 0 : (up[uy * uw + ux] & 0xFF000000u));
                }
            }
        }
    }
    for (i = 1; i < n; i++) {
        free(lv[i]);
    }
}

static unsigned tbo_make(TboTex *t, uint32_t *px, uint32_t w, uint32_t h, const char *what, int line) {
    unsigned tex;

    if (px == NULL) {
        return 0;
    }
    {
        extern int TboText_Fine(uint32_t id); /* (tbo_text.c: a line written at the HUD's capitals, taghudfine) */
        int mip = MIP_ON() && tbo_pot(w) && tbo_pot(h) && w >= 8 && h >= 8;
        if (line && t >= sText && t < sText + TBO_TEXT_SLOTS && TboText_Fine(TBO_TEXT_BASE + (uint32_t)(t - sText))) {
            mip = 0; /* (taghudfine: about its size on the screen already) */
        }
        tex = mip ? Vita_VglTexRgbaMip(px, (int)w, (int)h) : Vita_VglTexRgba(px, (int)w, (int)h);
        free(px);
        if (tex == 0) {
            return 0;
        }
        t->mip = (unsigned char)mip;
    }
    if (t->tex != 0) {
        if (sLateN < 64) {
            sLate[sLateN] = t->tex;
            sLateBytes[sLateN++] = t->bytes;
        } else {
            Vita_VglTexFree(t->tex); /* (not expected: LINES_A_PICTURE a picture) */
            gVitaTboN[3]--;
            gVitaTboN[4] -= t->bytes;
        }
    }
    t->tex = tex;
    t->bytes = w * h * 4;
    t->frame = gGsFrame;
    gVitaTboN[line ? 1 : 0]++;
    gVitaTboN[3]++;
    gVitaTboN[4] += t->bytes;
    if (!line) {
        Vita_Log("tbo: %s: %u x %u; %u textures held, %u KB\n", what, (unsigned)w, (unsigned)h, gVitaTboN[3], gVitaTboN[4] / 1024);
    }
    return tex;
}

/* ---- gs_draw.c's touches to the Tag Team logo and the main menu's emblem, on straight RGBA (8-bit alpha) ---- */
static int tbo_emblem_outer_pixel(const uint8_t *pixel) {
    unsigned hi = pixel[0], lo = pixel[0];
    int i;
    for (i = 1; i < 3; ++i) {
        if (pixel[i] > hi) hi = pixel[i];
        if (pixel[i] < lo) lo = pixel[i];
    }
    return pixel[3] < 128 || (hi >= 80 && hi - lo <= 24);
}

/* Strip white sticker outlines connected to transparent space, including the counters of letters. Keep enclosed
   highlights and the black contour. (gs_draw.c tbo_remove_outer_halo) */
static void tbo_remove_outer_halo(uint8_t *pixels, uint32_t w, uint32_t h) {
    const size_t count = (size_t)w * h;
    uint8_t *seen = calloc(count, 1);
    uint32_t *queue = malloc(count * sizeof(*queue));
    size_t head = 0, tail = 0;
    uint32_t x, y;
    if (!seen || !queue) {
        free(seen);
        free(queue);
        return;
    }
    for (y = 0; y < h; ++y) {
        for (x = 0; x < w; ++x) {
            const uint32_t i = y * w + x;
            const uint8_t *pixel = pixels + (size_t)i * 4;
            if (pixel[3] < 128 || ((!x || x + 1 == w || !y || y + 1 == h) && tbo_emblem_outer_pixel(pixel))) {
                seen[i] = 1;
                queue[tail++] = i;
            }
        }
    }
    while (head < tail) {
        const uint32_t i = queue[head++], qx = i % w, qy = i / w;
        const uint32_t next[4] = {qx ? i - 1 : i, qx + 1 < w ? i + 1 : i, qy ? i - w : i, qy + 1 < h ? i + w : i};
        int k;
        pixels[(size_t)i * 4 + 3] = 0;
        for (k = 0; k < 4; ++k) {
            const uint32_t n = next[k];
            if (!seen[n] && tbo_emblem_outer_pixel(pixels + (size_t)n * 4)) {
                seen[n] = 1;
                queue[tail++] = n;
            }
        }
    }
    free(queue);
    free(seen);
}

/* The crest and the T share one logo: the complete crest, the lettering outside its ellipse masked (gs_draw.c
   tbo_tag_emblem). Returns a new picture, or NULL. */
static uint8_t *tbo_tag_emblem(const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t *width, uint32_t *height) {
    uint32_t cw, top, ch, x, y;
    uint8_t *out;
    if (w < 16 || h < 16) return NULL;
    cw = (w * 676u + 1084u) / 2168u;
    top = (h * 8u + 362u) / 725u;
    ch = (h * 689u + 362u) / 725u;
    out = malloc((size_t)cw * ch * 4);
    if (!out) return NULL;
    for (y = 0; y < ch; ++y) {
        for (x = 0; x < cw; ++x) {
            uint8_t *dst = out + ((size_t)y * cw + x) * 4;
            const uint32_t sy = y + top < h ? y + top : h - 1;
            const uint8_t *src = rgba + ((size_t)sy * w + x) * 4;
            const float dx = ((float)x + .5f) * 2168.0f / w - 354.892f;
            const float dy = ((float)sy + .5f) * 725.0f / h - 347.878f;
            memcpy(dst, src, 4);
            if (dx > 0) {
                const float radius = __builtin_sqrtf(0.0000106049045f * dx * dx + 0.00000108280035f * dx * dy + 0.0000099139102f * dy * dy);
                float coverage = (1.014f - radius) / .007f;
                if (coverage < 0) coverage = 0;
                if (coverage > 1) coverage = 1;
                dst[3] = (uint8_t)(dst[3] * coverage + .5f);
            }
        }
    }
    tbo_remove_outer_halo(out, cw, ch);
    *width = cw;
    *height = ch;
    return out;
}

/* A picture of tbo/ui as straight RGBA (8-bit alpha), or NULL. */
static uint8_t *tbo_png(const char *name, uint32_t *w, uint32_t *h, char *path, size_t pathSize) {
    png_image img;
    uint8_t *buf;

    snprintf(path, pathSize, "app0:tbo/ui/%s.png", name);
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&img, path)) {
        return NULL;
    }
    img.format = PNG_FORMAT_RGBA;
    buf = malloc(PNG_IMAGE_SIZE(img));
    if (buf == NULL || !png_image_finish_read(&img, NULL, buf, 0, NULL)) {
        free(buf);
        png_image_free(&img);
        return NULL;
    }
    *w = img.width;
    *h = img.height;
    return buf;
}

/* Straight RGBA to the GS's alpha (0x80 opaque), as gs_draw.c tbo_texture; the window's inside one teal. Takes it. */
static uint32_t *tbo_gs(uint8_t *rgba, uint32_t w, uint32_t h, uint32_t id) {
    uint32_t *px = (uint32_t *)(void *)rgba;
    size_t i, n = (size_t)w * h;
    for (i = 0; i < n; i++) {
        const uint8_t *c = rgba + i * 4;
        uint32_t a = ((uint32_t)c[3] * 128 + 127) / 255;
        uint32_t v = (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | (a << 24);
        /* the window: its inside is bands of two teals, which fall anywhere under the lines at the sizes it is drawn;
           every teal pixel is the darker one, the frame (grey, nearly black) stays (as the PC) */
        if (id == TBO_KIT_WINDOW && c[3] == 255 && c[0] < 60 && c[1] >= 60 && c[1] <= 125 && c[2] + 10 >= c[1] && c[2] <= c[1] + 15) {
            v = 27u | (86u << 8) | (92u << 16) | (a << 24);
        }
        px[i] = v;
    }
    return px;
}

/* tbo/ui/<name>.vt (tools/tbo_vita_assets.py): "VTX1", width, height, flags, then the texture's RGBA with the GS's alpha,
   sized, touched and bled at build time. NULL if there is none (the PNG is read then, as before). */
static uint32_t *tbo_vt(const char *name, uint32_t *pw, uint32_t *ph, char *path, size_t pathSize) {
    uint32_t hdr[4], *px;
    size_t n;
    FILE *f;

    snprintf(path, pathSize, "app0:tbo/ui/%s.vt", name);
    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fread(hdr, 4, 4, f) != 4 || memcmp(hdr, "VTX1", 4) != 0 || hdr[1] == 0 || hdr[2] == 0 || hdr[1] > 2048 || hdr[2] > 2048) {
        fclose(f);
        return NULL;
    }
    n = (size_t)hdr[1] * hdr[2];
    px = malloc(n * 4);
    if (px == NULL || fread(px, 4, n, f) != n) {
        free(px);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *pw = hdr[1];
    *ph = hdr[2];
    return px;
}

/* A picture made as it is shown, as the GS's RGBA, or NULL (not readable: *missing). Everything but the texture's making:
   with tbopre=1 it is done by the maker's thread (below), else by the drawing thread at the picture's first use. */
static uint32_t *tbo_decode(uint32_t id, uint32_t *pw, uint32_t *ph, char *path, size_t pathSize, int *missing) {
    uint32_t w = 0, h = 0, limit;
    uint8_t *rgba;

    *missing = 0;
    {   /* the picture made ready when the package was built (port/vita/tools/tbo_vita_assets.py): read as it is */
        uint32_t *px = tbo_vt(id == TBO_MAIN_TAG_EMBLEM ? "main_tag_emblem" : kTboNames[id == TBO_TAG_LOGO_OFF ? TBO_TAG_LOGO : id],
                              pw, ph, path, pathSize);
        if (px != NULL) {
            return px;
        }
    }
    rgba = tbo_png(kTboNames[id == TBO_MAIN_TAG_EMBLEM || id == TBO_TAG_LOGO_OFF ? TBO_TAG_LOGO : id], &w, &h, path, pathSize);
    if (rgba == NULL) {
        Vita_Log("tbo: cannot read %s\n", path);
        *missing = 1;
        return NULL;
    }
    if (id == TBO_MAIN_TAG_EMBLEM) {
        uint32_t ew, eh;
        uint8_t *e = tbo_tag_emblem(rgba, w, h, &ew, &eh);
        free(rgba);
        if (e == NULL) {
            *missing = 1;
            return NULL;
        }
        rgba = e;
        w = ew;
        h = eh;
    } else if (id == TBO_TAG_LOGO || id == TBO_TAG_LOGO_OFF) {
        uint32_t row, col, c;
        tbo_remove_outer_halo(rgba, w, h);
        /* the lettering's one enclosed white speck, which the flood cannot reach: neutral bright matte right of the
           emblem (as the PC) */
        for (row = 0; row < h; ++row) {
            for (col = w * 620 / 2168; col < w; ++col) {
                uint8_t *p = rgba + ((size_t)row * w + col) * 4;
                unsigned hi = p[0], lo = p[0];
                for (c = 1; c < 3; ++c) {
                    if (p[c] > hi) hi = p[c];
                    if (p[c] < lo) lo = p[c];
                }
                if (hi >= 160 && hi - lo <= 24) p[3] = 0;
            }
        }
    }
    limit = id == TBO_BG || id == TBO_TAG_ART ? 1280 : 640;
    {
        uint32_t *px = tbo_fit(tbo_gs(rgba, w, h, id), &w, &h, limit, limit);
        if (px != NULL && MIP_ON() && tbo_pot(w) && tbo_pot(h) && w >= 8 && h >= 8) {
            tbo_bleed(px, w, h); /* (its smaller copies made by the chip: tbo_make) */
        }
        *pw = w;
        *ph = h;
        return px;
    }
}

/* ---- the maker: the pictures and the lines made by a thread of its own (bt3.txt tbopre=1) ----
   v1.4 made them on the drawing thread when first drawn: the 1280 x 720 poster, the kit's pieces and the buttons (each a
   PNG read and halved), the main menu's emblem (the 2168 x 725 logo read and cut), the lines (a TrueType line written
   80 pixels high with its outline, several milliseconds each, four a picture): the main menu's Tag Team entry came late
   and the Tag Team screens froze for seconds (the watchdog's 3 s with the drawing thread busy). Now a thread at a lower
   priority than the game's makes them (the drawing thread only uploads, a few hundred KB a picture), the main menu
   asks for the Tag Team screens' set as soon as it is drawn, and until a picture or line is ready it is not drawn. */
#define MAKER_QUEUE 64
#define MAKER_PRIORITY 176 /* the game's thread 160: it runs when the cores have time */
#define UPLOAD_BYTES 1572864 /* uploaded in a picture (one more may follow when none was yet) */
int gVitaTboPre = 1;
unsigned gVitaTboMs[4]; /* (the report) ms of the drawing thread on uploads and pictures made there; of the maker; jobs; bytes uploaded */

typedef struct {
    int line;
    uint32_t id;
    TboTex *t;
} MakerJob;
static MakerJob sQueue[MAKER_QUEUE];
static int sQueueN;
static pthread_mutex_t sMaker = PTHREAD_MUTEX_INITIALIZER;
static SceUID sMakerSema, sMakerThread;
static int sMakerState; /* 0 not started, 1 running, -1 could not be started */
static unsigned sPreAsked, sPreFrame;
static int sBootDone;
/* (tbotext=1) Lines asked for by a screen and handed to the maker, not made yet: while there are any, none of the
   screens' pictures or lines is drawn (a screen shows whole, a few pictures later, never a line at a time); at most
   GATE_FRAMES pictures, then what is ready is drawn. */
#define GATE_FRAMES 45
static volatile int sGatePending;
static volatile unsigned sGateFrame;
static uint32_t *line_pixels(uint32_t id, uint32_t *version, uint32_t *w, uint32_t *h);

static int maker_thread(unsigned args, void *argp) {
    (void)args;
    (void)argp;
    for (;;) {
        MakerJob j;
        int have = 0, k, best = -1;
        uint32_t w = 0, h = 0, version = 0, *px;
        int missing = 0;
        char path[160];
        unsigned long long t0;

        pthread_mutex_lock(&sMaker);
        for (k = 0; k < sQueueN; k++) { /* (the lines the screen waits for before the pictures kept for later) */
            if (sQueue[k].line) {
                best = k;
                break;
            }
        }
        if (best < 0 && sQueueN > 0) {
            best = 0;
        }
        if (best >= 0) {
            j = sQueue[best];
            memmove(&sQueue[best], &sQueue[best + 1], (size_t)(sQueueN - best - 1) * sizeof(sQueue[0]));
            sQueueN--;
            have = 1;
        }
        pthread_mutex_unlock(&sMaker);
        if (!have) {
            sceKernelWaitSema(sMakerSema, 1, NULL);
            continue;
        }
        t0 = sceKernelGetProcessTimeWide();
        if (j.line) {
            px = line_pixels(j.id, &version, &w, &h);
        } else {
            px = tbo_decode(j.id, &w, &h, path, sizeof(path), &missing);
        }
        gVitaTboMs[1] += (unsigned)((sceKernelGetProcessTimeWide() - t0 + 500) / 1000);
        gVitaTboMs[2]++;
        pthread_mutex_lock(&sMaker);
        free(j.t->px);
        j.t->px = px;
        j.t->w = w;
        j.t->h = h;
        j.t->pxVersion = version;
        j.t->missing |= (unsigned char)missing;
        j.t->state = px != NULL ? 2 : 3; /* ready; 3: nothing came of it (a line: until its version changes) */
        j.t->failVersion = version;
        j.t->readyFrame = gGsFrame;
        if (j.line) {
            /* (nothing made: asked again, at most 60 times a version, as line_asked does) */
            j.t->madeVersion = px == NULL && ++j.t->lineTries <= 60 ? 0 : version;
            if (px != NULL) {
                j.t->lineTries = 0;
            }
            if (sGatePending > 0) {
                sGatePending--; /* (tbotext=1: one line fewer that the screen waits for) */
            }
        }
        pthread_mutex_unlock(&sMaker);
        if (!j.line) {
            sceKernelDelayThread(1500); /* (between two pictures: the cores to the game) */
        }
    }
    return 0;
}

static int maker_start(void) {
    if (sMakerState != 0) {
        return sMakerState > 0;
    }
    sMakerSema = sceKernelCreateSema("bt3_tbomake", 0, 0, 1000, NULL);
    sMakerThread = sMakerSema >= 0 ? sceKernelCreateThread("bt3_tbomake", maker_thread, MAKER_PRIORITY, 0x80000, 0, 0, NULL) : -1;
    if (sMakerThread < 0 || sceKernelStartThread(sMakerThread, 0, NULL) < 0) {
        sMakerState = -1;
        Vita_Log("tbo: the maker's thread could not be started: the pictures are made by the drawing thread\n");
        return 0;
    }
    sMakerState = 1;
    return 1;
}

/* A job for the maker unless one is queued or done for the picture (the drawing thread). 0 when it could not be put. */
static int maker_ask(int line, uint32_t id, TboTex *t) {
    int ok = 0;
    if (!maker_start()) {
        return 0;
    }
    pthread_mutex_lock(&sMaker);
    if (t->state == 0 || t->state == 3) {
        if (sQueueN < MAKER_QUEUE) {
            sQueue[sQueueN].line = line;
            sQueue[sQueueN].id = id;
            sQueue[sQueueN].t = t;
            sQueueN++;
            t->state = 1;
            ok = 1;
        }
    }
    pthread_mutex_unlock(&sMaker);
    if (ok) {
        sceKernelSignalSema(sMakerSema, 1);
    }
    return ok;
}

static unsigned sUploadFreeUntil; /* (Vita_TboSessionBack) pictures up to this one upload without the budget */
static int upload_ok(unsigned bytes) {
    static unsigned frame, taken;
    if (gGsFrame <= sUploadFreeUntil) {
        return 1;
    }
    if (frame != gGsFrame) {
        frame = gGsFrame;
        taken = 0;
    }
    if (taken != 0 && taken + bytes > UPLOAD_BYTES) {
        return 0;
    }
    taken += bytes;
    return 1;
}

/* The Tag Team screens' pictures, asked for while the main menu (or the Duel menu's Tag type) is shown: the PNG read
   and halved by the maker, kept as pixels (about 5 MB) until they are asked for or the menu has not been shown for 20 s. */
static unsigned tbo_prefetch_ids(unsigned *ids) {
    unsigned n = 0;
    extern int Port_GameProfilePortuguese(void);
    extern int Port_GameProfileEnglish(void);
    ids[n++] = TBO_TAG_ART;
    if (Vita_TagUiVita()) { /* (taguivita: the kit, the buttons, the logo and the emblem are the atlas's, sUiAtlas) */
        ids[n++] = Port_GameProfilePortuguese() ? TBO_TAG_BUTTONS_PT : Port_GameProfileEnglish() ? TBO_TAG_BUTTONS_EN : TBO_TAG_BUTTONS_ES;
        return n;
    }
    ids[n++] = TBO_KIT_WINDOW;
    ids[n++] = TBO_KIT_SEL;
    ids[n++] = TBO_KIT_BAR;
    ids[n++] = TBO_TAG_LOGO;
    ids[n++] = TBO_BTN_CIRCLE;
    ids[n++] = TBO_BTN_LEFTRIGHT;
    ids[n++] = TBO_BTN_CROSS;
    ids[n++] = TBO_BTN_TRIANGLE;
    ids[n++] = TBO_KIT_DIALOG;
    ids[n++] = TBO_KIT_GOLD;
    ids[n++] = TBO_KIT_STEEL;
    ids[n++] = TBO_BTN_SQUARE;
    ids[n++] = TBO_BTN_START;
    ids[n++] = Port_GameProfilePortuguese() ? TBO_TAG_BUTTONS_PT : Port_GameProfileEnglish() ? TBO_TAG_BUTTONS_EN : TBO_TAG_BUTTONS_ES;
    ids[n++] = TBO_TAG_LOGO_OFF;
    ids[n++] = TBO_KIT_BAR_COMPACT;
    ids[n++] = TBO_KIT_SEL_COMPACT;
    return n;
}

/* (tbopre=1) The Tag Team set is resident: read (tbo/ui/<name>.vt, no decoding) by the maker in the first seconds of
   play, uploaded by the drawing thread at the pictures' ends (UPLOAD_BYTES a picture), and never given back (tbo_drop,
   Vita_TboFrameEnd): entering the Tag Team screens neither reads nor uploads a picture. */
static int tbo_resident(const TboTex *t) {
    unsigned ids[24], n, i;
    if (!gVitaTboPre) {
        return 0;
    }
    if (t == &sEmblem) {
        return 1;
    }
    if (t == &sPic[TBO_TAG_ART] && Vita_TagUiVita()) {
        extern int Vita_InBattle(void);
        return !Vita_InBattle(); /* (taguivita: the backdrop's 2 MB given back in a fight, Vita_TboFrameEnd) */
    }
    n = tbo_prefetch_ids(ids);
    for (i = 0; i < n; i++) {
        if (t == &sPic[ids[i]]) {
            return 1;
        }
    }
    return 0;
}

static void tbo_prefetch(void) {
    unsigned ids[24], n, i;
    if (!gVitaTboPre || (sPreFrame != 0 && gGsFrame - sPreFrame < 30)) {
        return;
    }
    sPreFrame = gGsFrame;
    n = tbo_prefetch_ids(ids);
    for (i = 0; i < n; i++) {
        TboTex *t = &sPic[ids[i]];
        if (t->tex == 0 && !t->missing && t->state == 0) {
            maker_ask(0, ids[i], t);
        }
    }
}

/* (session_warm) the pictures after a session's end: draws of a kept picture that found no texture, counted for the log */
static unsigned char sSessKeep[TBO_ART_COUNT];
static unsigned sBackFrame, sBackMissing, sBackMissFrames, sBackLastMiss;
static int sSessPinned; /* (session_warm=1, during a session: the kept pictures are not given back) */
static unsigned char sSessKeepText[TBO_TEXT_SLOTS]; /* (and the screens' lines drawn then: the hub's labels) */
static unsigned tbo_picture(uint32_t id) {
    TboTex *t = id == TBO_MAIN_TAG_EMBLEM ? &sEmblem : &sPic[id];
    uint32_t w = 0, h = 0;
    char path[160];
    int missing = 0;
    uint32_t *px;
    unsigned long long t0;
    unsigned tex;

    if (t->tex != 0) {
        t->frame = gGsFrame;
        return t->tex;
    }
    if (t->missing) {
        return 0;
    }
    if (gVitaTboPre && maker_start()) {
        int state;
        pthread_mutex_lock(&sMaker);
        state = t->state;
        pthread_mutex_unlock(&sMaker);
        if (state == 2) {
            if (gGsFrame < t->retry || !upload_ok(t->w * t->h * 4)) {
                return 0;
            }
            t0 = sceKernelGetProcessTimeWide();
            pthread_mutex_lock(&sMaker);
            px = t->px;
            w = t->w;
            h = t->h;
            t->px = NULL;
            t->state = 0;
            pthread_mutex_unlock(&sMaker);
            snprintf(path, sizeof(path), "app0:tbo/ui/%s", id == TBO_MAIN_TAG_EMBLEM ? "main_tag_emblem" : kTboNames[id == TBO_TAG_LOGO_OFF ? TBO_TAG_LOGO : id]);
            gVitaTboMs[3] += w * h * 4;
            tex = tbo_make(t, px, w, h, path, 0);
            if (tex == 0) {
                t->retry = gGsFrame + 120; /* (no texture for it just now: not made over and over) */
            }
            gVitaTboMs[0] += (unsigned)((sceKernelGetProcessTimeWide() - t0 + 500) / 1000);
            return tex;
        }
        if (state == 0) {
            maker_ask(0, id, t);
        }
        return 0;
    }
    t0 = sceKernelGetProcessTimeWide();
    px = tbo_decode(id, &w, &h, path, sizeof(path), &missing);
    if (px == NULL) {
        t->missing = 1;
        return 0;
    }
    tex = tbo_make(t, px, w, h, path, 0);
    gVitaTboMs[0] += (unsigned)((sceKernelGetProcessTimeWide() - t0 + 500) / 1000);
    return tex;
}

/* ---- a line of letters (tbo_text.c) ----
   A line is written large (capitals 80 pixels, 48 for the talk box's; a long one 2048 x 512 with its outline) and made
   smaller here: several milliseconds of the drawing thread for a long one. A page that comes in asks for some thirty
   at once, so at most LINES_A_PICTURE are made in a picture; the others are drawn as they were (or not yet) and made
   in the pictures after. */
#define LINES_A_PICTURE 4

/* A picture stretched to the next powers of two (bilinear, the colour weighted by the alpha), so that it can have its
   smaller copies; drawn over the same box (its coordinates go 0..1 across the texture). Takes px. */
static uint32_t *tbo_pot_stretch(uint32_t *px, uint32_t *pw, uint32_t *ph) {
    uint32_t w = *pw, h = *ph, nw = 8, nh = 8, x, y, k, ch;
    uint32_t *dst;
    while (nw < w) nw *= 2;
    while (nh < h) nh *= 2;
    if (nw > 2048 || nh > 1024 || (dst = malloc((size_t)nw * nh * 4)) == NULL) {
        return px;
    }
    for (y = 0; y < nh; y++) {
        float fy = ((float)y + 0.5f) * (float)h / (float)nh - 0.5f;
        int y0 = fy < 0 ? 0 : (int)fy, y1 = y0 + 1 < (int)h ? y0 + 1 : (int)h - 1;
        float ty = fy < 0 ? 0 : fy - (float)y0;
        for (x = 0; x < nw; x++) {
            float fx = ((float)x + 0.5f) * (float)w / (float)nw - 0.5f;
            int x0 = fx < 0 ? 0 : (int)fx, x1 = x0 + 1 < (int)w ? x0 + 1 : (int)w - 1;
            float tx = fx < 0 ? 0 : fx - (float)x0;
            uint32_t c[4] = {px[y0 * w + x0], px[y0 * w + x1], px[y1 * w + x0], px[y1 * w + x1]}, out = 0;
            float wt[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty}, a = 0, sw = 0;
            for (k = 0; k < 4; k++) {
                a += (float)(c[k] >> 24) * wt[k];
                sw += wt[k];
            }
            for (ch = 0; ch < 24; ch += 8) {
                float sum = 0, aw = 0;
                for (k = 0; k < 4; k++) {
                    float f = wt[k] * ((float)(c[k] >> 24) + 0.001f);
                    sum += (float)((c[k] >> ch) & 0xFF) * f;
                    aw += f;
                }
                out |= ((uint32_t)(sum / aw + 0.5f) & 0xFF) << ch;
            }
            dst[y * nw + x] = out | ((uint32_t)(a / sw + 0.5f) << 24);
        }
    }
    free(px);
    *pw = nw;
    *ph = nh;
    return dst;
}

/* A line's picture as the texture takes it: written (tbo_text.c, at the Vita's size with tbotextsize=1), at most
   1024 x 128, a heading stretched to powers of two for its smaller copies. NULL: none. */
/* (taguivita) One side of a picture to n samples, each the area it covers (the colour weighted by the alpha, as
   tbo_half): a line written at its capitals' pixels made as narrow as the page draws it, so that a texel is a pixel.
   Takes px. */
static uint32_t *tbo_area(uint32_t *px, uint32_t *pw, uint32_t *ph, uint32_t n, int across) {
    uint32_t w = *pw, h = *ph, src = across ? w : h, other = across ? h : w, o, k, i;
    float scale = (float)src / (float)n;
    uint32_t *dst;
    if (n == 0 || n == src || n > 2048 || (dst = malloc((size_t)n * other * 4)) == NULL) {
        return px;
    }
    for (k = 0; k < other; k++) {
        for (o = 0; o < n; o++) {
            float a = (float)o * scale, b = (float)(o + 1) * scale, s[4] = {0, 0, 0, 0}, plain[3] = {0, 0, 0}, ws = 0;
            uint32_t out = 0, ch;
            if (scale < 1.0f) { /* (going up: linear; not expected) */
                a = ((float)o + 0.5f) * scale - 0.5f;
                b = a + 1.0f;
            }
            for (i = a < 0 ? 0 : (uint32_t)a; (float)i < b && i < src; i++) {
                float wt = ((float)(i + 1) < b ? (float)(i + 1) : b) - ((float)i > a ? (float)i : a);
                uint32_t c = across ? px[k * w + i] : px[i * w + k], al = c >> 24;
                if (wt <= 0) {
                    continue;
                }
                for (ch = 0; ch < 3; ch++) {
                    s[ch] += (float)((c >> (ch * 8)) & 0xFF) * (float)al * wt;
                    plain[ch] += (float)((c >> (ch * 8)) & 0xFF) * wt;
                }
                s[3] += (float)al * wt;
                ws += wt;
            }
            if (ws <= 0) {
                ws = 1;
            }
            for (ch = 0; ch < 3; ch++) {
                float v = s[3] > 0 ? s[ch] / s[3] : plain[ch] / ws;
                out |= ((uint32_t)(v + 0.5f) > 255 ? 255u : (uint32_t)(v + 0.5f)) << (ch * 8);
            }
            {
                uint32_t al = (uint32_t)(s[3] / ws + 0.5f);
                out |= (al > 128 ? 128u : al) << 24;
            }
            if (across) {
                dst[k * n + o] = out;
            } else {
                dst[o * w + k] = out;
            }
        }
    }
    free(px);
    if (across) {
        *pw = n;
    } else {
        *ph = n;
    }
    return dst;
}

static uint32_t *line_pixels(uint32_t id, uint32_t *version, uint32_t *w, uint32_t *h) {
    extern int TboText_Fine(uint32_t id);
    extern int TboText_VitaScale(uint32_t id, int *sx, int *sy); /* tbo_text.c: thousandths; 0 none */
    uint32_t *px = TboText_Pixels(id, version, w, h);
    int sx = 1000, sy = 1000;
    if (px != NULL && TboText_VitaScale(id, &sx, &sy)) {
        /* (taguivita) a texel a pixel: as narrow (and, a heading, as tall) as it is drawn */
        uint32_t nw = (uint32_t)(((uint64_t)*w * (uint32_t)sx + 500) / 1000), nh = (uint32_t)(((uint64_t)*h * (uint32_t)sy + 500) / 1000);
        if (sx > 0 && sx < 1000 && nw >= 1) {
            px = tbo_area(px, w, h, nw, 1);
        }
        if (px != NULL && sy > 0 && sy < 1000 && nh >= 1) {
            px = tbo_area(px, w, h, nh, 0);
        }
    }
    if (px != NULL) {
        px = tbo_fit(px, w, h, 1024, 128);
    }
    if (px != NULL && TboText_Fine(id)) {
        return px; /* (at its size on the screen: no smaller copies, any width) */
    }
    if (px != NULL && MIP_ON() && (!tbo_pot(*w) || !tbo_pot(*h))) {
        px = tbo_pot_stretch(px, w, h); /* (a heading: any size) */
    }
    return px;
}

/* (tbotext=1) A line asked for by the screens' code (tbo_text.c text_id, the game's thread; game 1) or by the drawing
   thread (game 0): its picture made unless the texture or a picture of this version is there already. On the game's
   thread at most LINE_BUDGET_US of a picture go to lines (the HUD's figures, a few labels: well under a millisecond
   each at the Vita's size); the others go to the maker, and the screens are not drawn until it has made them (the gate,
   GATE_FRAMES). Entering the Tag Team screens: some forty lines, a few made here, the rest by the maker on another core. */
#define LINE_BUDGET_US 4000
void Vita_TboLineAsked(uint32_t id, uint32_t version);
static void line_asked(uint32_t id, uint32_t version, int game) {
    static unsigned budgetFrame, budgetUs;
    TboTex *t;
    uint32_t w = 0, h = 0, made = 0, *px;
    int need;
    unsigned long long t0;

    if (!TEXT_ON() || !tbo_on() || id < TBO_TEXT_BASE || id >= TBO_TEXT_BASE + TBO_TEXT_SLOTS || version == 0) {
        return;
    }
    t = &sText[id - TBO_TEXT_BASE];
    pthread_mutex_lock(&sMaker);
    /* madeVersion: the version made here and either waiting (state 2) or uploaded since; set back to 0 when the
       texture or the waiting picture is given back (Vita_TboFrameEnd, tbo_drop) */
    need = t->state != 1 && t->madeVersion != version; /* (state 1: the maker has it) */
    pthread_mutex_unlock(&sMaker);
    if (!need) {
        return;
    }
    if (game) {
        if (budgetFrame != gGsFrame) {
            budgetFrame = gGsFrame;
            budgetUs = 0;
        }
        if (budgetUs >= LINE_BUDGET_US && gVitaTboPre && maker_start()) {
            int queued;
            pthread_mutex_lock(&sMaker);
            if (t->state == 2) { /* (an older version of the line waiting: let go) */
                free(t->px);
                t->px = NULL;
                t->state = 0;
            }
            pthread_mutex_unlock(&sMaker);
            queued = maker_ask(1, id, t);
            if (queued) {
                pthread_mutex_lock(&sMaker);
                if (sGatePending == 0) {
                    sGateFrame = gGsFrame;
                }
                sGatePending++;
                pthread_mutex_unlock(&sMaker);
                gVitaTboLines[2]++;
                return;
            }
        }
    }
    t0 = sceKernelGetProcessTimeWide();
    px = line_pixels(id, &made, &w, &h);
    if (game) {
        budgetUs += (unsigned)(sceKernelGetProcessTimeWide() - t0);
    }
    pthread_mutex_lock(&sMaker);
    free(t->px);
    t->px = px;
    t->w = w;
    t->h = h;
    t->pxVersion = made;
    t->madeVersion = made;
    t->state = px != NULL ? 2 : 3;
    t->failVersion = made;
    t->readyFrame = gGsFrame;
    if (px != NULL) {
        t->lineTries = 0;
    } else if (++t->lineTries <= 60) {
        /* (nothing made, e.g. asked while the fonts were changing: TboText_Pixels finds the line measured otherwise. Kept
           as "made", the line was never asked again and never drawn: the Tag screens' footer lost its "Change" this way.
           Asked again, at most 60 times a version.) */
        t->madeVersion = 0;
    }
    pthread_mutex_unlock(&sMaker);
    if (px == NULL && (t->lineTries == 1 || t->lineTries == 61)) {
        extern void TboText_Words(uint32_t id, char *out, int size);
        char words[200];
        TboText_Words(id, words, sizeof(words));
        Vita_Log("tbo: line %u: no picture made (try %u): %s\n", (unsigned)id, (unsigned)t->lineTries, words);
    }
    gVitaTboLines[0]++;
    gVitaTboLines[1] += (unsigned)(sceKernelGetProcessTimeWide() - t0);
}
void Vita_TboLineAsked(uint32_t id, uint32_t version) {
    line_asked(id, version, 1);
}

static unsigned tbo_line_(uint32_t id);
/* (a diagnostic: a line asked for and not drawn for 2 s is written in the log, once a version; the Tag screens' footer
   lost its "Change" this way, 2026-11-19) */
static unsigned tbo_line(uint32_t id) {
    static unsigned short missed[TBO_TEXT_SLOTS];
    static uint32_t told[TBO_TEXT_SLOTS];
    unsigned tex = tbo_line_(id), k = id - TBO_TEXT_BASE;
    if (tex != 0) {
        missed[k] = 0;
    } else if (++missed[k] == 120) {
        TboTex *t = &sText[k];
        uint32_t v = TboText_Version(id);
        if (told[k] != v) {
            extern void TboText_Words(uint32_t id, char *out, int size);
            char words[200];
            told[k] = v;
            TboText_Words(id, words, sizeof(words));
            Vita_Log("tbo: line %u not drawn for 120 pictures: %s; version %u, its picture's %u, state %d, made %u, failed %u\n",
                     (unsigned)id, words, (unsigned)v, (unsigned)t->version, (int)t->state, (unsigned)t->madeVersion,
                     (unsigned)t->failVersion);
        }
    }
    return tex;
}
static unsigned tbo_line_(uint32_t id) {
    static unsigned frame, made;
    TboTex *t = &sText[id - TBO_TEXT_BASE];
    uint32_t version = TboText_Version(id), w, h, *px;

    if (t->tex != 0 && (t->version == version || t->frame == gGsFrame)) { /* (changed in this picture: the old one once more) */
        t->frame = gGsFrame;
        return t->tex;
    }
    if (TEXT_ON()) {
        /* (tbotext=1) its picture was made when the line was asked for (Vita_TboLineAsked): uploaded now, whatever the
           budget; another line's picture of the same place is never shown for it */
        uint32_t *lpx = NULL, lw = 0, lh = 0, ready = 0;
        pthread_mutex_lock(&sMaker);
        if (t->state == 2 && t->pxVersion == version) {
            lpx = t->px;
            lw = t->w;
            lh = t->h;
            ready = t->pxVersion;
            t->px = NULL;
            t->state = 0;
        }
        pthread_mutex_unlock(&sMaker);
        if (lpx == NULL && t->state != 1) { /* (state 1: the maker is making it; the screen waits, the gate) */
            line_asked(id, version, 0); /* (not asked for by the game's thread: made here then, not in a later picture) */
            pthread_mutex_lock(&sMaker);
            if (t->state == 2 && t->pxVersion == version) {
                lpx = t->px;
                lw = t->w;
                lh = t->h;
                ready = t->pxVersion;
                t->px = NULL;
                t->state = 0;
            }
            pthread_mutex_unlock(&sMaker);
        }
        if (lpx != NULL) {
            unsigned long long t0 = sceKernelGetProcessTimeWide();
            gVitaTboMs[3] += lw * lh * 4;
            if (tbo_make(t, lpx, lw, lh, "line", 1) != 0) {
                t->version = ready;
            }
            gVitaTboMs[0] += (unsigned)((sceKernelGetProcessTimeWide() - t0 + 500) / 1000);
        }
        if (t->tex != 0 && t->version == version) {
            t->frame = gGsFrame;
            return t->tex;
        }
        return 0;
    }
    if (gVitaTboPre && maker_start()) { /* the maker writes it; the old picture of the place shows meanwhile, or none */
        int state;
        uint32_t readyVersion;
        pthread_mutex_lock(&sMaker);
        state = t->state;
        readyVersion = t->pxVersion;
        if (state == 2 && readyVersion != version) { /* (the line changed while it was being made: made again) */
            free(t->px);
            t->px = NULL;
            t->state = state = 0;
        }
        if (state == 3 && t->failVersion != version) {
            t->state = state = 0;
        }
        pthread_mutex_unlock(&sMaker);
        if (state == 2) {
            if (upload_ok(t->w * t->h * 4)) {
                unsigned long long t0 = sceKernelGetProcessTimeWide();
                uint32_t *px, w, h;
                pthread_mutex_lock(&sMaker);
                px = t->px;
                w = t->w;
                h = t->h;
                t->px = NULL;
                t->state = 0;
                pthread_mutex_unlock(&sMaker);
                gVitaTboMs[3] += w * h * 4;
                if (tbo_make(t, px, w, h, "line", 1) != 0) {
                    t->version = readyVersion;
                }
                gVitaTboMs[0] += (unsigned)((sceKernelGetProcessTimeWide() - t0 + 500) / 1000);
            }
        } else if (state == 0) {
            maker_ask(1, id, t);
        }
        if (t->tex != 0) {
            t->frame = gGsFrame;
        }
        return t->tex;
    }
    if (frame != gGsFrame) {
        frame = gGsFrame;
        made = 0;
    }
    if (made >= LINES_A_PICTURE) {
        if (t->tex != 0) {
            t->frame = gGsFrame;
        }
        return t->tex;
    }
    made++;
    px = TboText_Pixels(id, &version, &w, &h);
    if (px == NULL) {
        return t->tex;
    }
    px = tbo_fit(px, &w, &h, 1024, 128);
    if (tbo_make(t, px, w, h, "line", 1) == 0) {
        return t->tex;
    }
    t->version = version;
    return t->tex;
}

/* ---- the main menu's "Tag Team" in the disc's lettering (bt4_main_lettering.inc), made once in a thread ----
   The members 448 / 449 are written from the disc that is played, in a folder of their own per disc language
   (gamedata/en, gamedata/es): v1.4 wrote them to gamedata/pzs4us1 once and kept them, so a player who started with the
   Spanish disc and then played the English one had the Spanish menu's members put together with the English menu's
   names (bt4_main_lettering.inc: 11 names, the English rows), which fails: no Tag Team entry (the developer's run,
   2026-11-18). The old folder is left as it is. If the disc's letters still cannot be put together, the entry is
   written in the Menu font (letter_fallback), so it always shows. */
#define LETTER_HOME PORT_VITA_HOME "/gamedata"
static unsigned char *sLetterPic[2];
/* The Vita Guide's main-menu entry (src/menu/menu_a.c) in the same lettering: "Vita Guide" / "Guía Vita" (the i with
   its accent made from the disc's i, bt4_main_lettering.inc), pictures TBO_VITA_GUIDE_OFF / ON */
#define TBO_VITA_GUIDE_OFF 0x242
#define TBO_VITA_GUIDE_ON 0x243
static unsigned char *sGuidePic[2];
static unsigned char *sOnlinePic[2]; /* "Tenkaichi Budokai Online" (bt3.txt online_menu: the main menu's online entry, as the PC's) */
static TboTex sOnlineLetters[2];
static volatile int sLetterState; /* 0 not begun, 1 being made, 2 done (sLetterPic NULL: not made) */
extern uint8_t *MenuText_LineRender(int kind, const char *text, int cap, int pad, int *w, int *h, int *base); /* menu_text.c */
extern void MenuText_LinePrewarm(int kind, int cap);
extern int gVitaTboTextSize; /* tbo_text.c */

/* "Tag Team" in the Menu font as a 512 x 32 picture of the main menu's row (straight RGBA): the letters left at the
   row's margin, their capitals 18 pixels, a gold fill (the chosen row a brighter one) with a dark brown edge. */
static unsigned char *letter_fallback(const char *text, int lit) {
    int w, h, base, x, y, dx, dy;
    uint8_t *cov = MenuText_LineRender(0, text, 18, 3, &w, &h, &base);
    unsigned char *out;
    static const unsigned char top[2][3] = {{255, 244, 196}, {255, 255, 150}}, bottom[2][3] = {{232, 168, 56}, {255, 140, 16}},
                                edge[2][3] = {{58, 28, 8}, {96, 30, 0}};
    if (cov == NULL) {
        return NULL;
    }
    out = calloc(512 * 32, 4);
    if (out == NULL) {
        free(cov);
        return NULL;
    }
    for (y = 0; y < 32; y++) {
        int sy = y - (25 - base); /* (the baseline 25 rows down: the capitals in the row's middle) */
        for (x = 0; x < 512; x++) {
            int sx = x - 13, fill = 0, line = 0, k;
            unsigned char *o = out + (y * 512 + x) * 4;
            if (sy >= 0 && sy < h && sx >= 0 && sx < w) {
                fill = cov[sy * w + sx];
            }
            for (dy = -2; dy <= 2; dy++) {
                for (dx = -2; dx <= 2; dx++) {
                    int yy = sy + dy, xx = sx + dx;
                    if (dx * dx + dy * dy <= 6 && yy >= 0 && yy < h && xx >= 0 && xx < w && cov[yy * w + xx] > line) {
                        line = cov[yy * w + xx];
                    }
                }
            }
            if (line == 0) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                int f = top[lit][k] + (bottom[lit][k] - top[lit][k]) * y / 31;
                o[k] = (unsigned char)((edge[lit][k] * (255 - fill) + f * fill) / 255);
            }
            o[3] = (unsigned char)line;
        }
    }
    free(cov);
    return out;
}

static int letter_thread(unsigned args, void *argp) {
    extern int Port_GameProfileEnglish(void);
    char root[96];
    int ok = 0;
    (void)args;
    (void)argp;
    /* (the PC reads the menus' archive members as loose files of its data folder: written once from the disc) */
    snprintf(root, sizeof(root), "%s/%s", LETTER_HOME, Port_GameProfileEnglish() ? "en" : "es");
    sceIoMkdir(LETTER_HOME, 0777);
    if (Vita_MemberFile("pzs4us1", 448, root) && Vita_MemberFile("pzs4us1", 449, root)) {
        extern int MenuLabels_Bt4MainItems(const char *root, const char *const *labels, int count, unsigned char **out);
        extern int Vita_OnlineMenu(void);
        const char *labels[3] = {"Tag Team", Port_GameProfileEnglish() ? "Vita Guide" : "Gu\xC3\xAD" "a Vita", "Tenkaichi Budokai Online"};
        unsigned char *pics[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
        ok = MenuLabels_Bt4MainItems(root, labels, Vita_OnlineMenu() ? 3 : 2, pics);
        sOnlinePic[0] = pics[4];
        sOnlinePic[1] = pics[5];
        sLetterPic[0] = pics[0];
        sLetterPic[1] = pics[1];
        sGuidePic[0] = pics[2];
        sGuidePic[1] = pics[3];
        if (ok && (pics[2] == NULL || pics[3] == NULL)) {
            Vita_Log("tbo: the Vita Guide's lettering could not be put together from the disc (%s): written in the Menu font\n", root);
        }
        if (!ok) {
            Vita_Log("tbo: the main menu's Tag Team lettering could not be put together from the disc (%s): written in the Menu font\n", root);
        }
    } else {
        Vita_Log("tbo: the main menu's archive members 448 / 449 could not be written to %s: the entry written in the Menu font\n", root);
    }
    if (!ok) {
        free(sLetterPic[0]);
        free(sLetterPic[1]);
        sLetterPic[0] = letter_fallback("Tag Team", 0);
        sLetterPic[1] = letter_fallback("Tag Team", 1);
    }
    {
        extern int Vita_OnlineMenu(void);
        if (Vita_OnlineMenu() && (sOnlinePic[0] == NULL || sOnlinePic[1] == NULL)) {
            free(sOnlinePic[0]);
            free(sOnlinePic[1]);
            sOnlinePic[0] = letter_fallback("Tenkaichi Budokai Online", 0);
            sOnlinePic[1] = letter_fallback("Tenkaichi Budokai Online", 1);
        }
    }
    if (sGuidePic[0] == NULL || sGuidePic[1] == NULL) {
        const char *guide = Port_GameProfileEnglish() ? "Vita Guide" : "Gu\xC3\xAD" "a Vita";
        free(sGuidePic[0]);
        free(sGuidePic[1]);
        sGuidePic[0] = letter_fallback(guide, 0);
        sGuidePic[1] = letter_fallback(guide, 1);
    }
    {   /* (a diagnostic, guidetest bit 256: the four pictures written as raw 512 x 32 RGBA to ux0:data/bt4/letters_*.rgba) */
        extern int gVitaGuideTest;
        if (gVitaGuideTest & 256) {
            unsigned char *pics[4] = {sLetterPic[0], sLetterPic[1], sGuidePic[0], sGuidePic[1]};
            static const char *const names[4] = {"tag0", "tag1", "guide0", "guide1"};
            int k;
            for (k = 0; k < 4; k++) {
                char path[96];
                SceUID fd;
                snprintf(path, sizeof(path), PORT_VITA_HOME "/letters_%s.rgba", names[k]);
                if (pics[k] != NULL && (fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666)) >= 0) {
                    sceIoWrite(fd, pics[k], 512 * 32 * 4);
                    sceIoClose(fd);
                }
            }
        }
    }
    __sync_synchronize();
    sLetterState = 2;
    if (TEXT_ON()) {
        /* the lines' glyphs written ahead (menu_text.c's cache): the Tag Team screens' lines are then copies of them */
        unsigned long long t0 = sceKernelGetProcessTimeWide();
        MenuText_LinePrewarm(0, gVitaTboTextSize ? 40 : 80);
        MenuText_LinePrewarm(1, gVitaTboTextSize ? 24 : 48);
        Vita_Log("tbo: the lines' glyphs written ahead in %u ms\n", (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000));
    }
    return sceKernelExitDeleteThread(0);
}

static void letters_start(void) {
    if (sLetterState == 0) {
        /* (v1.4: at the game thread's own priority, 160, begun when the main menu first drew: the disc read and the
           letters put together took the menu's time. With tbopre=1 a lower one, begun in the first seconds of play.) */
        SceUID th = sceKernelCreateThread("bt3_letters", letter_thread, gVitaTboPre ? MAKER_PRIORITY : 0x10000100, 0x80000, 0, 0, NULL);
        sLetterState = 1;
        if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
            sLetterState = 2;
        }
    }
}

static unsigned tbo_letters(int lit, int guide) { /* guide: 0 Tag Team, 1 the Vita Guide, 2 the online entry */
    TboTex *t = guide == 2 ? &sOnlineLetters[lit] : guide ? &sGuideLetters[lit] : &sLetters[lit];
    unsigned char *const *pic = guide == 2 ? sOnlinePic : guide ? sGuidePic : sLetterPic;
    uint32_t w = 512, h = 32, *px;
    size_t i;

    if (t->tex != 0) {
        t->frame = gGsFrame;
        return t->tex;
    }
    letters_start();
    if (sLetterState != 2 || t->missing) {
        return 0; /* (not yet: the plate without its words for a moment) */
    }
    __sync_synchronize();
    if (pic[lit] == NULL) {
        t->missing = 1;
        return 0;
    }
    px = malloc(512 * 32 * 4);
    if (px == NULL) {
        return 0;
    }
    for (i = 0; i < 512 * 32; i++) { /* (8-bit alpha to the GS's) */
        const uint8_t *c = pic[lit] + i * 4;
        px[i] = (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | ((((uint32_t)c[3] * 128 + 127) / 255) << 24);
    }
    if (MIP_ON()) {
        tbo_bleed(px, w, h);
    }
    return tbo_make(t, px, w, h, guide == 2 ? (lit ? "online entry lettering, chosen" : "online entry lettering") :
                                 guide ? (lit ? "Vita Guide lettering, chosen" : "Vita Guide lettering")
                                       : (lit ? "main menu lettering, chosen" : "main menu lettering"), 0);
}

/* src/menu/menu_a.c: the Vita Guide entry's picture number (as it is / chosen), 0 while its lettering is not made yet
   (the entry then written in the game's font) */
unsigned Vita_GuideLetterId(int lit) {
    if (!tbo_on()) {
        return 0;
    }
    letters_start();
    if (sLetterState != 2) {
        return 0;
    }
    __sync_synchronize();
    return sGuidePic[0] != NULL && sGuidePic[1] != NULL ? (lit ? TBO_VITA_GUIDE_ON : TBO_VITA_GUIDE_OFF) : 0;
}

/* (taguivita) tbo/ui/tag_ui_vita.vt, read on the drawing thread the first time (496 KB, no decoding) and kept */
static unsigned tbo_ui_atlas(void) {
    if (sUiAtlas.tex == 0 && !sUiAtlas.missing) {
        char path[160];
        uint32_t w = 0, h = 0, *px = tbo_vt("tag_ui_vita", &w, &h, path, sizeof(path));
        if (px == NULL) {
            Vita_Log("tbo: cannot read %s: the Tag Team screens' separate pictures instead\n", path);
            sUiAtlas.missing = 1;
            sUiAtlasGone = 1;
            return 0;
        }
        sUiAtlas.tex = Vita_VglTexRgba(px, (int)w, (int)h); /* (drawn at its size: no smaller copies) */
        free(px);
        if (sUiAtlas.tex != 0) {
            sUiAtlas.bytes = w * h * 4;
            gVitaTboN[0]++;
            gVitaTboN[3]++;
            gVitaTboN[4] += sUiAtlas.bytes;
            Vita_Log("tbo: %s: %u x %u, kept (the Tag Team screens' kit at its pixel size)\n", path, (unsigned)w, (unsigned)h);
        }
    }
    return sUiAtlas.tex;
}

/* The renderer (vita_vgl.c state_of_, the drawing thread): the texture of picture `id`, 0 for none (not drawn).
   *mip: it has its smaller copies (tbomip=1): to be sampled with them. */
static unsigned tbo_texture(uint32_t id, int *mip) {
    /* (tbotext=1) lines the screen waits for at the maker: the screen's pictures and lines not drawn meanwhile */
    int gated = sGatePending > 0 && gGsFrame - sGateFrame < GATE_FRAMES && TEXT_ON();
    *mip = 0;
    if (!tbo_on()) {
        return 0;
    }
    if (id >= TBO_TEXT_BASE && id < TBO_TEXT_BASE + TBO_TEXT_SLOTS) {
        unsigned tex = tbo_line(id);
        *mip = tex != 0 && sText[id - TBO_TEXT_BASE].mip;
        if (tex == 0 && sBackFrame != 0 && gGsFrame - sBackFrame < 120 && sSessKeepText[id - TBO_TEXT_BASE]) {
            sBackMissing++;
            if (sBackLastMiss != gGsFrame) {
                sBackLastMiss = gGsFrame;
                sBackMissFrames++;
            }
        }
        return gated ? 0 : tex;
    }
    if (gated && id != TBO_MAIN_TAG_OFF && id != TBO_MAIN_TAG_ON && id != TBO_MAIN_TAG_EMBLEM && id != TBO_VITA_GUIDE_OFF &&
        id != TBO_VITA_GUIDE_ON && id != TBO_MAIN_ONLINE_OFF && id != TBO_MAIN_ONLINE_ON) {
        gVitaTboGated++;
        return 0;
    }
    if (id == TBO_MAIN_TAG_OFF || id == TBO_MAIN_TAG_ON) {
        unsigned tex;
        sPreAsked = gGsFrame; /* (the main menu, or the Duel menu's Tag type, is shown: the Tag Team screens' pictures are made ahead) */
        tbo_prefetch();
        tex = tbo_letters(id == TBO_MAIN_TAG_ON, 0);
        *mip = tex != 0 && sLetters[id == TBO_MAIN_TAG_ON].mip;
        return tex;
    }
    if (id == TBO_MAIN_ONLINE_OFF || id == TBO_MAIN_ONLINE_ON) { /* (the main menu's online entry, online_menu=1) */
        unsigned tex = tbo_letters(id == TBO_MAIN_ONLINE_ON, 2);
        *mip = tex != 0 && sOnlineLetters[id == TBO_MAIN_ONLINE_ON].mip;
        return tex;
    }
    if (id == TBO_VITA_GUIDE_OFF || id == TBO_VITA_GUIDE_ON) {
        unsigned tex = tbo_letters(id == TBO_VITA_GUIDE_ON, 1);
        *mip = tex != 0 && sGuideLetters[id == TBO_VITA_GUIDE_ON].mip;
        return tex;
    }
    if (id == TBO_TAG_MOVIE) {
        /* the film behind the Tag Team screens (vita_tagmovie.c) when tbomovie is on and a picture is ready; else its poster */
        unsigned film = gVitaTagMovie == 1 ? Vita_TagMovieTexture() : gVitaTagMovie == 2 ? Vita_TagMovieSwTexture() : 0;
        if (film != 0) {
            return film;
        }
        id = TBO_TAG_ART;
    }
    if (id == TBO_VITA_HUD) { /* (taghudvita) the Tag Team HUD's atlas: read once, kept */
        if (sHudAtlas.tex == 0 && !sHudAtlas.missing) {
            char path[160];
            uint32_t w = 0, h = 0, *px = tbo_vt("tag_hud_vita", &w, &h, path, sizeof(path));
            if (px == NULL) {
                Vita_Log("tbo: cannot read %s\n", path);
                sHudAtlas.missing = 1;
                return 0;
            }
            sHudAtlas.tex = Vita_VglTexRgba(px, (int)w, (int)h); /* (drawn at its size: no smaller copies) */
            free(px);
            if (sHudAtlas.tex != 0) {
                sHudAtlas.bytes = w * h * 4;
                gVitaTboN[0]++;
                gVitaTboN[3]++;
                gVitaTboN[4] += sHudAtlas.bytes;
                Vita_Log("tbo: %s: %u x %u, kept\n", path, (unsigned)w, (unsigned)h);
            }
        }
        return sHudAtlas.tex;
    }
    if (id == TBO_VITA_UI) { /* (taguivita) the screens' kit at its pixel size: read once, kept */
        return tbo_ui_atlas();
    }
    if (id == TBO_MAIN_TAG_EMBLEM || (id < TBO_ART_COUNT && id != TBO_RADAR)) {
        unsigned tex = tbo_picture(id);
        *mip = tex != 0 && (id == TBO_MAIN_TAG_EMBLEM ? sEmblem.mip : sPic[id].mip);
        if (tex == 0 && sBackFrame != 0 && gGsFrame - sBackFrame < 120 && id < TBO_ART_COUNT && sSessKeep[id]) {
            sBackMissing++;
            if (sBackLastMiss != gGsFrame) {
                sBackLastMiss = gGsFrame;
                sBackMissFrames++;
            }
        }
        return tex;
    }
    return 0; /* (the online hub's and the European disc's art, the ranked Dragon Balls: not offline) */
}
unsigned Vita_TboTexture(uint32_t id) {
    int mip;
    return tbo_texture(id, &mip);
}
unsigned Vita_TboTextureMip(uint32_t id, int *mip) {
    return tbo_texture(id, mip);
}

static void tbo_drop(TboTex *t, unsigned n) {
    unsigned i;
    for (i = 0; i < n; i++) {
        if (t[i].tex != 0 && gGsFrame - t[i].frame > KEEP_FRAMES && !tbo_resident(&t[i]) &&
            !(sSessPinned && &t[i] >= sPic && &t[i] < sPic + TBO_ART_COUNT && sSessKeep[&t[i] - sPic]) &&
            !(sSessPinned && &t[i] >= sText && &t[i] < sText + TBO_TEXT_SLOTS && sSessKeepText[&t[i] - sText])) {
            Vita_VglTexFree(t[i].tex);
            pthread_mutex_lock(&sMaker);
            t[i].tex = 0;
            t[i].version = 0;
            t[i].madeVersion = 0; /* (a line asked for again is made again) */
            pthread_mutex_unlock(&sMaker);
            gVitaTboN[2]++;
            gVitaTboN[3]--;
            gVitaTboN[4] -= t[i].bytes;
        }
    }
}

/* At every picture's end (vita_vgl.c GsGpu_FrameEnd, after its batches are handed over): the lines' replaced
   textures given back; every 64th, the textures not drawn for a while. The main menu's emblem and lettering (~600 KB)
   are kept: made again, they would cost the logo's decoding and the disc's letters at every return to the menu. */
volatile int gVitaTboPhase; /* (the watchdog, vita_vgl.c Vita_HangWhere: the step below the drawing thread is at; 0 none) */
void Vita_TboFrameEnd(void) {
    unsigned held;
    if (sBackFrame != 0 && gGsFrame - sBackFrame == 120) {
        Vita_Log("tbo: the 120 pictures after the session's end: %u of them drew without a picture kept for them (%u draws)\n",
                 sBackMissFrames, sBackMissing);
        sBackFrame = 0;
    }
    if ((gGsFrame & 63) == 0) {
        gVitaTboPhase = 1;
        Vita_TagMovieIdle(); /* the film's texture not drawn for a while */
        Vita_TagMovieSwIdle();
    }
    gVitaTboPhase = 2;
    if (gVitaTboPre && !sBootDone && gGsFrame >= 240 && tbo_on()) {
        /* the main menu's emblem and lettering, made in the first seconds, long before the menu shows them */
        unsigned ids[24], n, i;
        sBootDone = 1;
        letters_start();
        if (Vita_TagUiVita() && tbo_ui_atlas() != 0) {
            sEmblem.missing = 1; /* (taguivita: the emblem is the atlas's) */
        } else {
            maker_ask(0, TBO_MAIN_TAG_EMBLEM, &sEmblem);
        }
        n = tbo_prefetch_ids(ids); /* the Tag Team set, resident from now on (tbo_resident) */
        for (i = 0; i < n; i++) {
            if (sPic[ids[i]].tex == 0 && !sPic[ids[i]].missing) {
                maker_ask(0, ids[i], &sPic[ids[i]]);
            }
        }
    }
    gVitaTboPhase = 3;
    if (gVitaTboPre && sBootDone) {
        /* the resident set's pictures read by the maker: uploaded here, at the pictures' ends, within the budget */
        static unsigned done, logged;
        if (!done) {
            unsigned ids[24], n, i, left = 0, kb = 0;
            n = tbo_prefetch_ids(ids);
            for (i = 0; i <= n; i++) {
                uint32_t id = i < n ? ids[i] : TBO_MAIN_TAG_EMBLEM;
                TboTex *t = i < n ? &sPic[id] : &sEmblem;
                if (t->tex == 0 && !t->missing) {
                    int state;
                    pthread_mutex_lock(&sMaker);
                    state = t->state;
                    pthread_mutex_unlock(&sMaker);
                    if (state == 2) {
                        tbo_picture(id);
                    }
                    left += t->tex == 0;
                }
                kb += t->bytes / 1024;
            }
            if (left == 0) {
                done = 1;
                if (!logged++) {
                    Vita_Log("tbo: the Tag Team set resident: %u pictures, %u KB (with their smaller copies about a third more)\n", n + 1, kb);
                }
            }
        }
    }
    gVitaTboPhase = 4;
    if (gVitaTboPre && sBootDone && Vita_TagUiVita()) {
        /* (taguivita) the backdrop behind the Tag Team screens (960 x 544, 2 MB): given back two seconds into a fight,
           read again by the maker as soon as the fight is over (the game's own screens load their backdrops too) */
        extern int Vita_InBattle(void);
        TboTex *t = &sPic[TBO_TAG_ART];
        if (Vita_InBattle()) {
            if ((gGsFrame & 63) == 16 && t->tex != 0 && gGsFrame - t->frame > 60) {
                Vita_VglTexFree(t->tex);
                t->tex = 0;
                gVitaTboN[2]++;
                gVitaTboN[3]--;
                gVitaTboN[4] -= t->bytes;
                Vita_Log("tbo: the Tag Team backdrop given back in the fight: %u textures held, %u KB\n", gVitaTboN[3], gVitaTboN[4] / 1024);
            }
        } else if (t->tex == 0 && !t->missing && (gGsFrame & 15) == 8) {
            int state;
            pthread_mutex_lock(&sMaker);
            state = t->state;
            pthread_mutex_unlock(&sMaker);
            if (state == 0) {
                maker_ask(0, TBO_TAG_ART, t);
            } else if (state == 2) {
                tbo_picture(TBO_TAG_ART);
            }
        }
    }
    if (gVitaTboPre && (gGsFrame & 63) == 32 && gGsFrame - sPreAsked > KEEP_FRAMES) {
        /* the menus have not been shown for 20 s (a fight): the pixels kept for the Tag Team screens given back */
        unsigned i;
        pthread_mutex_lock(&sMaker);
        for (i = 0; i < TBO_ART_COUNT; i++) {
            if (sPic[i].state == 2 && !tbo_resident(&sPic[i])) {
                free(sPic[i].px);
                sPic[i].px = NULL;
                sPic[i].state = 0;
            }
        }
        pthread_mutex_unlock(&sMaker);
        sPreFrame = 0;
    }
    if (gVitaTboPre && (gGsFrame & 63) == 48) {
        /* a line made that was not asked for again (its screen was left): its pixels given back */
        unsigned i;
        pthread_mutex_lock(&sMaker);
        for (i = 0; i < TBO_TEXT_SLOTS; i++) {
            if (sText[i].state == 2 && gGsFrame - sText[i].readyFrame > KEEP_FRAMES) {
                free(sText[i].px);
                sText[i].px = NULL;
                sText[i].state = 0;
                sText[i].madeVersion = 0;
            }
        }
        pthread_mutex_unlock(&sMaker);
    }
    gVitaTboPhase = 5;
    while (sLateN > 0) {
        sLateN--;
        Vita_VglTexFree(sLate[sLateN]);
        gVitaTboN[2]++;
        gVitaTboN[3]--;
        gVitaTboN[4] -= sLateBytes[sLateN];
    }
    held = gVitaTboN[3];
    if ((gGsFrame & 63) != 0 || held == 0) {
        gVitaTboPhase = 0;
        return;
    }
    gVitaTboPhase = 6;
    tbo_drop(sPic, TBO_ART_COUNT);
    tbo_drop(sText, TBO_TEXT_SLOTS);
    if (gVitaTboN[3] != held) {
        Vita_Log("tbo: textures not drawn for %u pictures given back: %u held, %u KB\n", KEEP_FRAMES, gVitaTboN[3], gVitaTboN[4] / 1024);
    }
    gVitaTboPhase = 0;
}

/* (online sessions inside the running game, vita_session.c; bt3.txt session_warm, default 1) The screens' pictures
   shown when a session began (the online hub's background, radar and host, the lobby's) are not drawn during the match
   and are given back after KEEP_FRAMES; at the return the menus came back with them gone, and the first picture was
   drawn without them while the maker read them again (online_review v3/05, console run online_sess_s2). Kept: which
   ones were drawn in the last pictures before the session; back: the maker reads them again at once, the game's thread
   reads them itself (the maker's job if it had begun one: waited for, at most 1.5 s; the screen keeps the match's last picture meanwhile) and the next pictures upload them whatever the
   budget. 0: as before. */
int gVitaSessionWarm = 1;
void Vita_TboSessionKeep(void) {
    unsigned id, n = 0, nText = 0;
    for (id = 0; id < TBO_ART_COUNT; id++) {
        sSessKeep[id] = sPic[id].tex != 0 && gGsFrame - sPic[id].frame < 8;
        n += sSessKeep[id];
    }
    for (id = 0; id < TBO_TEXT_SLOTS; id++) {
        sSessKeepText[id] = sText[id].tex != 0 && gGsFrame - sText[id].frame < 8;
        nText += sSessKeepText[id];
    }
    sSessPinned = gVitaSessionWarm != 0;
    Vita_Log("tbo: %u pictures and %u lines of the screens shown when the session began%s\n", n, nText,
             sSessPinned ? ": kept as textures through it (session_warm=1)" : "");
}
void Vita_TboSessionBack(void) {
    unsigned id, asked = 0, left = 0, waited = 0;
    unsigned long long t0 = sceKernelGetProcessTimeWide();
    sSessPinned = 0;
    sBackFrame = gGsFrame != 0 ? gGsFrame : 1; /* (the count of pictures drawn without them, both ways) */
    sBackMissing = sBackMissFrames = 0;
    if (!gVitaSessionWarm || !tbo_on()) {
        return;
    }
    for (id = 0; id < TBO_ART_COUNT; id++) {
        TboTex *t = &sPic[id];
        int state;
        if (!sSessKeep[id] || t->tex != 0 || t->missing) {
            continue;
        }
        pthread_mutex_lock(&sMaker);
        state = t->state;
        if (state == 0 || state == 3) {
            t->state = 1; /* (read here, on the game's thread: the maker's queue and its pauses take longer; console run
                             online_lobby_screens: 460 ms, the background still not ready) */
        }
        pthread_mutex_unlock(&sMaker);
        if (state == 0 || state == 3) {
            uint32_t w = 0, h = 0, *px;
            int missing = 0;
            char path[160];
            px = tbo_decode(id, &w, &h, path, sizeof(path), &missing);
            pthread_mutex_lock(&sMaker);
            free(t->px);
            t->px = px;
            t->w = w;
            t->h = h;
            t->missing |= (unsigned char)missing;
            t->state = px != NULL ? 2 : 3;
            t->readyFrame = gGsFrame;
            pthread_mutex_unlock(&sMaker);
            asked++;
        }
    }
    do {
        left = 0;
        for (id = 0; id < TBO_ART_COUNT; id++) {
            if (sSessKeep[id] && sPic[id].tex == 0 && !sPic[id].missing) {
                int state;
                pthread_mutex_lock(&sMaker);
                state = sPic[id].state;
                pthread_mutex_unlock(&sMaker);
                left += state == 1;
            }
        }
        if (left != 0) {
            sceKernelDelayThread(2000);
            waited += 2;
        }
    } while (left != 0 && waited < 1500); /* (only a job the maker had already begun) */
    sUploadFreeUntil = gGsFrame + 3;
    sBackFrame = gGsFrame != 0 ? gGsFrame : 1;
    sBackMissing = sBackMissFrames = 0;
    Vita_Log("tbo: the session's end: %u pictures of the screens read again in %u ms (%u not ready)\n", asked,
             (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000), left);
}
