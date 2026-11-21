/*
 * PS Vita: the online session's two lines over the picture, drawn the Vita's way (online M2a; docs/vita/online-bt4.md).
 *
 * The PC shows them in its F1 window's layer (port/src/gs/ui.cpp): Port_UiBanner, a line in the middle of the screen
 * while a session starts ("Connecting to your rival...", "Your rival "X" is here! The match begins.", the round trip),
 * the picture dimmed while the game waits, then a little longer over the character select; Port_UiNotice, a line near
 * the top for six seconds (a desync, the other player gone, "That address is not known."). Same calls, same texts,
 * same times here; the drawing is the one the port's own screens use on the Vita (src/menu/port_screen.c: the window
 * of the Tag/Tenkaichi screens and their TrueType lines made for the Vita's screen, vita_tbo.c), from the game's
 * Gfx_EndFrame (src/sys/gfx.c, PORT_VITA) as the spectator's line (port_tbo.c Tb_SpecLine) is: one panel and a line or
 * three, in the game's frame, no pass of its own.
 *
 * bt3.txt online_line: 0 the lines only logged (the old way), 1 drawn. Default 0 until seen on the console.
 * While the game's thread waits (net.c Gs_HoldFrame) no frame is drawn: bt3.txt online_hold=1 writes the banner into
 * the picture on show instead (Vita_NetLineHold, at the end of this file).
 */
#include <stdio.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

extern void Vita_Log(const char *fmt, ...);
extern const char *PortOnline_Text(const char *text); /* (gs/online_language.c: the player's language) */

/* the kit (src/menu/port_screen.c; include/menu/port_screen.h's types: f32 float, s32 int, u32 unsigned) */
extern void Ps_PageSet(void);
extern void Ps_SetShift(float x);
extern void Ps_Align(int align);
extern void Ps_Panel(int id, float x0, float y0, float x1, float y1, int alpha);
extern void Ps_Rect(float x0, float y0, float x1, float y1, unsigned top, unsigned bottom);
extern void Ps_TextRaw(float x, float y, char *s, float scale, float width, int style);
extern float Ps_TextWidth(char *str, float scale);
#include "menu/tbo_art.h" /* (plain C: TBO_KIT_DIALOG, the window of the hosts' lines) */

int gVitaNetLine; /* bt3.txt online_line */
extern int gVitaNetHold; /* bt3.txt online_hold (the held picture, below) */

#define LINE_MAX 4
typedef struct NetLine {
    char text[256];
    volatile unsigned version; /* changed by the setter; the drawer wraps again when it differs */
    unsigned drawnVersion;
    unsigned long long until;  /* (process time, us) 0: (banner) while the game waits; (notice) from its first frame */
    int lines;
    char line[LINE_MAX][128];
    float scale, width;
} NetLine;
static NetLine sBanner = {.scale = 0.62f, .width = 380.0f}, sNotice = {.scale = 0.50f, .width = 420.0f};
static volatile int sNoticeNew;

static unsigned long long now_us(void) { return sceKernelGetProcessTimeWide(); }

void Port_UiNotice(const char *text) {
    if (text != NULL && text[0] != '\0') {
        Vita_Log("net: notice: %s\n", text);
    }
    snprintf(sNotice.text, sizeof(sNotice.text), "%s", text != NULL ? text : "");
    sNotice.version++;
    sNoticeNew = 1;
}

/* as ui.cpp: text NULL with ms <= 0 takes the line away; ms > 0: the wait is over, the line stays that long more */
void Port_UiBanner(const char *text, int ms) {
    if (text != NULL && text[0] != '\0') {
        Vita_Log("net: banner (%d ms): %s\n", ms, text);
    }
    if (text != NULL) {
        snprintf(sBanner.text, sizeof(sBanner.text), "%s", PortOnline_Text(text));
        sBanner.version++;
    }
    if (text == NULL && ms <= 0) {
        sBanner.text[0] = '\0';
        sBanner.version++;
    }
    sBanner.until = ms > 0 ? now_us() + (unsigned long long)ms * 1000ull : 0;
}

/* R1.7 (ui.cpp): the Ranked result after a match. The PC slides a small panel in for ~4.2 s; the Vita shows the same
   three lines as its own banner for that long (no new drawing). */
void Port_UiRankedSettlement(int points, int delta) {
    char text[192], line[80];
    snprintf(text, sizeof(text), "%s\n", PortOnline_Text("Ranked result"));
    snprintf(line, sizeof(line), PortOnline_Text("%+d points"), delta);
    strncat(text, line, sizeof(text) - strlen(text) - 2);
    strncat(text, "\n", sizeof(text) - strlen(text) - 1);
    snprintf(line, sizeof(line), PortOnline_Text("Total: %d points"), points);
    strncat(text, line, sizeof(text) - strlen(text) - 1);
    Vita_Log("net: ranked settlement %+d (%d)\n", delta, points);
    snprintf(sBanner.text, sizeof(sBanner.text), "%s", text);
    sBanner.version++;
    sBanner.until = now_us() + 4200ull * 1000ull;
}

/* the text cut into the lines it is drawn as: at its own line ends, then between words to fit the width */
static void wrap(NetLine *l) {
    const char *p = l->text;
    l->lines = 0;
    while (*p != '\0' && l->lines < LINE_MAX) {
        char *out = l->line[l->lines];
        int n = 0, lastSpace = -1;
        const char *lastSpaceAt = NULL;
        while (*p != '\0' && *p != '\n' && n < (int)sizeof(l->line[0]) - 1) {
            out[n] = *p;
            out[n + 1] = '\0';
            if (*p == ' ') {
                lastSpace = n;
                lastSpaceAt = p;
            } else if (lastSpace > 0 && Ps_TextWidth(out, l->scale) > l->width) {
                out[lastSpace] = '\0';
                p = lastSpaceAt + 1;
                n = -1;
                break;
            }
            n++;
            p++;
        }
        if (n >= 0) {
            out[n] = '\0';
            if (*p == '\n') {
                p++;
            }
        }
        l->lines++;
    }
}

static void draw_line(NetLine *l, float middle, int dimmed, int style) {
    float step = 44.0f * l->scale, h = step * (float)l->lines, y0 = middle - h * 0.5f, w = 0.0f;
    int i;
    if (l->drawnVersion != l->version) {
        l->drawnVersion = l->version;
        wrap(l);
    }
    if (l->lines == 0) {
        return;
    }
    for (i = 0; i < l->lines; i++) {
        float lw = Ps_TextWidth(l->line[i], l->scale);
        w = lw > w ? lw : w;
    }
    w = w > l->width ? l->width : w;
    if (dimmed) { /* (the PC: black at 150 of 255 over the whole picture; the GS's 0x80 is full) */
        Ps_Rect(0.0f, 0.0f, 512.0f, 448.0f, 0x4B000000u, 0x4B000000u);
    }
    Ps_Panel(TBO_KIT_DIALOG, 256.0f - w * 0.5f - 18.0f, y0 - 10.0f, 256.0f + w * 0.5f + 18.0f, y0 + h + 8.0f, 0x70);
    Ps_Align(1);
    for (i = 0; i < l->lines; i++) {
        Ps_TextRaw(256.0f, y0 + step * (float)i, l->line[i], l->scale, l->width, style);
    }
    Ps_Align(0);
}

/* ---- the players' names under the health bars (bt3.txt online_names) ----
   The PC writes each side's online name under its health bar while the HUD is on screen (ui.cpp, `names`: 106 of the
   HUD's 512 x 448 units in from the outer edge, 56 down, white with a dark edge). Here the same place and words, in the
   kit's letters, drawn in the game's frame only when the game drew its life bars in it (src/battle/hud_b.c
   HudGauge_DrawHp, PORT_VITA: not in an intro or a cut-in) and not in a Tag Team fight's HUD. 0: not drawn; 2 (the
   development build) sample names in any fight, without a session, for the console's screenshots. */
int gVitaNetNames;
static unsigned sFrameN, sHudFrameN;
static int sHudTag;
void Vita_NetHudSeen(int tag) {
    sHudFrameN = sFrameN;
    sHudTag = tag;
}
static void draw_names(void) {
    extern int Port_NetSession(void), Port_HudSwap(void);
    extern const char *Port_NetName(int player);
    static char name[2][40];
    int place;
    int check = 0;
#ifndef PORT_VITA_RELEASE
    check = gVitaNetNames == 2 && !Port_NetSession(); /* (online_names=2: sample names in any fight, for screenshots) */
#endif
    if (!gVitaNetNames || (!Port_NetSession() && !check) || sHudTag || sFrameN - sHudFrameN > 2) {
        return;
    }
    Ps_PageSet();
    Ps_SetShift(0.0f);
    for (place = 0; place < 2; place++) {
        const char *t = check ? (place == 0 ? "Goku_Fan_99" : "VegetaPrince") : Port_NetName(place ^ Port_HudSwap());
        int i, k;
        if (t == NULL || t[0] == '\0') {
            continue;
        }
        for (i = k = 0; t[i] != '\0' && k < (int)sizeof(name[0]) - 1; i++) { /* (the other side's text: printable only) */
            if ((unsigned char)t[i] >= 0x20 && t[i] != 0x7F) {
                name[place][k++] = t[i];
            }
        }
        name[place][k] = '\0';
        Ps_Align(place == 0 ? 0 : 2);
        Ps_TextRaw(place == 0 ? 106.0f : 512.0f - 106.0f, 56.0f, name[place], 0.42f, 140.0f, 0);
    }
    Ps_Align(0);
}

/* Once a frame of every screen (src/sys/gfx.c Gfx_EndFrame, PORT_VITA), after the spectator's line. */
#ifndef PORT_VITA_RELEASE
/* online_line=2 (the development build): the lines on one Vita without a session, for the console's screenshots:
   from 30 s after the start the waiting banner (dimmed) for 8 s, then the round trip's banner held 4 s more over the
   game, then a long notice (wrapped) for its six seconds; again every 40 s */
static void line_check(void) {
    static unsigned long long t0;
    static int step = -1;
    unsigned long long t = now_us();
    int s;
    if (t0 == 0) {
        t0 = t;
    }
    s = t - t0 < 30000000ull ? -1 : (int)(((t - t0 - 30000000ull) / 1000000ull) % 40ull);
    s = s < 0 ? -1 : s < 8 ? 0 : s < 9 ? 1 : s < 18 ? 2 : 3;
    if (s == step) {
        return;
    }
    step = s;
    if (s == 0) {
        Port_UiBanner("Connecting to your rival...", 0);
        if (gVitaNetHold) { /* (online_hold=1 too: the game's thread held here 4 s, as net.c's wait holds it) */
            extern void Vita_NetLineHold(void);
            unsigned long long h = now_us();
            while (now_us() - h < 4000000ull) {
                if (now_us() - h > 2000000ull && strncmp(sBanner.text, "Your", 4) != 0) {
                    Port_UiBanner("Your rival \"Vegeta\" is here! The match begins.\nRound trip: ~12 ms", 0);
                }
                Vita_NetLineHold();
                sceKernelDelayThread(10000);
            }
            Port_UiBanner("Connecting to your rival...", 0);
        }
    } else if (s == 1) {
        Port_UiBanner("Your rival \"Vegeta\" is here! The match begins.\nRound trip: ~12 ms", 0);
        Port_UiBanner(NULL, 4000);
    } else if (s == 2) {
        Port_UiNotice("The two games no longer agree (desync): your screen and the other player's may now show different fights.");
    }
}
#endif

void Vita_NetLineDraw(int vsyncs) {
    unsigned long long t;
    int banner, notice;
    (void)vsyncs;
    sFrameN++;
    draw_names();
#ifndef PORT_VITA_RELEASE
    if (gVitaNetLine == 2) {
        line_check();
    }
#endif
    if (!gVitaNetLine || (sBanner.text[0] == '\0' && sNotice.text[0] == '\0')) {
        return;
    }
    t = now_us();
    if (sBanner.text[0] != '\0' && sBanner.until != 0 && t >= sBanner.until) {
        sBanner.text[0] = '\0';
        sBanner.version++;
    }
    if (sNoticeNew) { /* (the time starts when it is first drawn, as on the PC) */
        sNoticeNew = 0;
        sNotice.until = t + 6000000ull;
    }
    banner = sBanner.text[0] != '\0';
    notice = sNotice.text[0] != '\0' && t < sNotice.until;
    if (!banner && !notice) {
        return;
    }
    Ps_PageSet();
    Ps_SetShift(0.0f);
    if (banner) { /* in the middle, gold (the PC's yellow) */
        draw_line(&sBanner, 224.0f, sBanner.until == 0, 1);
    }
    if (notice) { /* near the top (the PC: 12 % down), white */
        draw_line(&sNotice, 54.0f, 0, 0);
    }
}

/* ---- the held picture (net.c Gs_HoldFrame; bt3.txt online_hold) ----
   While the game's thread waits for the other side (the start of a session, a spectator's wait, the data check), the
   PC draws its last picture again and again with the banner over it, dimmed. On the Vita the screen goes on showing
   the last picture by itself (nothing is swapped meanwhile); what is missing is the banner. online_hold=1 writes it
   straight into the picture on show (sceDisplayGetFrameBuf), on the game's thread, the renderer idle (Port_GsSync):
   the picture dimmed once, then a dark window with the banner's lines in the port's own TrueType letters (the same
   font and lines the kit draws: gs/menu_text.c MenuText_LineRender), drawn again only when the text changes or the
   picture on show is another one. No vitaGL call, no texture, no pass on the chip. 0: the old way (nothing drawn
   while the game waits). Not done here: a system dialog during the wait (it needs vitaGL's swaps). */
#include <stdlib.h>
#include <stdint.h>
#include <psp2/display.h>

int gVitaNetHold; /* bt3.txt online_hold */
extern uint8_t *MenuText_LineRender(int kind, const char *text, int cap, int pad, int *w, int *h, int *base);

static void *sHoldBase;
static unsigned sHoldVersion, sHoldShown;
static unsigned long long sHoldLast;
unsigned gVitaHoldN[2]; /* (the log) pictures dimmed, windows drawn */

static void hold_rect(uint32_t *px, int pitch, int x0, int y0, int x1, int y1, uint32_t c) {
    int x, y;
    for (y = y0; y < y1; y++) {
        uint32_t *row = px + (size_t)y * pitch;
        for (x = x0; x < x1; x++) {
            row[x] = c;
        }
    }
}

/* a line's coverage blended into the picture at (x, y): a dark outline (the eight neighbours), then the colour */
static void hold_text(uint32_t *px, int pitch, int fw, int fh, const char *text, int cap, int cx, int y, int maxw, uint32_t rgb) {
    int w = 0, h = 0, base = 0, i, j, pass;
    uint8_t *cov = MenuText_LineRender(0, text, cap, 2, &w, &h, &base);
    int x0;
    if (cov == NULL) {
        return;
    }
    x0 = cx - (w < maxw ? w : maxw) / 2;
    for (pass = 0; pass < 2; pass++) {
        uint32_t r = pass ? (rgb & 0xFF) : 0, g = pass ? ((rgb >> 8) & 0xFF) : 0, b = pass ? ((rgb >> 16) & 0xFF) : 0;
        for (j = 0; j < h; j++) {
            int ty = y + j;
            if (ty < 0 || ty >= fh) {
                continue;
            }
            for (i = 0; i < w && i < maxw; i++) {
                int a = cov[j * w + i], tx = x0 + i;
                if (pass == 0) { /* (the outline: the most of the neighbours' coverage) */
                    int dx, dy;
                    a = 0;
                    for (dy = -1; dy <= 1; dy++) {
                        for (dx = -1; dx <= 1; dx++) {
                            int ii = i + dx, jj = j + dy;
                            if (ii >= 0 && jj >= 0 && ii < w && jj < h && cov[jj * w + ii] > a) {
                                a = cov[jj * w + ii];
                            }
                        }
                    }
                }
                if (a == 0 || tx < 0 || tx >= fw) {
                    continue;
                }
                {
                    uint32_t *d = px + (size_t)ty * pitch + tx, o = *d;
                    uint32_t or_ = o & 0xFF, og = (o >> 8) & 0xFF, ob = (o >> 16) & 0xFF;
                    or_ = (or_ * (255 - a) + r * a) / 255;
                    og = (og * (255 - a) + g * a) / 255;
                    ob = (ob * (255 - a) + b * a) / 255;
                    *d = 0xFF000000u | (ob << 16) | (og << 8) | or_;
                }
            }
        }
    }
    free(cov);
}

void Vita_NetLineHold(void) {
    extern void Port_GsSync(void);
    extern volatile unsigned gVitaShown; /* (vita_vgl.c: pictures the display has taken) */
    SceDisplayFrameBuf fb;
    unsigned long long t = now_us();
    uint32_t *px;
    int fw, fh, pitch, cap, step, i, pw, ph, x0, y0;
    if (!gVitaNetHold || !gVitaNetLine) {
        return;
    }
    if (t - sHoldLast < 33000ull) { /* (as the PC: at most 30 a second) */
        return;
    }
    sHoldLast = t;
    Port_GsSync(); /* (the renderer has nothing in hand) */
    memset(&fb, 0, sizeof(fb));
    fb.size = sizeof(fb);
    if (sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME) < 0 || fb.base == NULL || fb.pixelformat != SCE_DISPLAY_PIXELFORMAT_A8B8G8R8) {
        return;
    }
    px = (uint32_t *)fb.base;
    fw = (int)fb.width;
    fh = (int)fb.height;
    pitch = (int)fb.pitch;
    if (fb.base != sHoldBase || gVitaShown != sHoldShown) { /* another picture on show: dimmed once (black at 150 of 255, as the PC) */
        int x, y;
        for (y = 0; y < fh; y++) {
            uint32_t *row = px + (size_t)y * pitch;
            for (x = 0; x < fw; x++) {
                uint32_t o = row[x];
                row[x] = 0xFF000000u | (((o >> 16 & 0xFF) * 105 / 255) << 16) | (((o >> 8 & 0xFF) * 105 / 255) << 8) | ((o & 0xFF) * 105 / 255);
            }
        }
        sHoldBase = fb.base;
        sHoldShown = gVitaShown;
        sHoldVersion = sBanner.version - 1; /* (the window drawn again on it) */
        gVitaHoldN[0]++;
    }
    if (sHoldVersion == sBanner.version) {
        return;
    }
    sHoldVersion = sBanner.version;
    if (sBanner.drawnVersion != sBanner.version) {
        sBanner.drawnVersion = sBanner.version;
        wrap(&sBanner);
    }
    /* the window: fixed in size (a shorter text after a longer one leaves nothing behind), dark navy with a gold edge,
       in the middle; its lines gold, as the frames' banner */
    cap = fh / 20;
    step = cap * 2;
    pw = fw * 8 / 10;
    ph = step * LINE_MAX + cap;
    x0 = (fw - pw) / 2;
    y0 = (fh - ph) / 2;
    hold_rect(px, pitch, x0 - 2, y0 - 2, x0 + pw + 2, y0 + ph + 2, 0xFF3C9CD8u);
    hold_rect(px, pitch, x0, y0, x0 + pw, y0 + ph, 0xFF280E0Au);
    for (i = 0; i < sBanner.lines; i++) {
        int y = y0 + (ph - step * sBanner.lines) / 2 + step * i + cap / 4;
        hold_text(px, pitch, fw, fh, sBanner.line[i], cap, fw / 2, y, pw - cap, i == 0 ? 0x78ECFFu : 0xFFFFFFu);
    }
    gVitaHoldN[1]++;
}
