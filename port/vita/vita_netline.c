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
 * Not yet: the PC draws the waiting banner over the last picture again and again while the game's thread waits
 * (Gs_HoldFrame); on the Vita nothing is drawn while the game's thread waits, so the line shows from the first
 * frame after the wait (the handoff in notes/online/vita-multiplayer-plan.md section 7).
 */
#include <stdio.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>

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
