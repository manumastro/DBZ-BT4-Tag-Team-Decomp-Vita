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
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include "vita_paths.h"
#include "../../include/menu/tbo_art.h"

extern void Vita_Log(const char *fmt, ...);
extern unsigned gGsFrame;
extern unsigned Vita_VglTexRgba(const void *px, int w, int h);
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

typedef struct {
    unsigned tex, bytes, version, frame;
    unsigned char missing; /* not found or not readable: not looked for again */
    /* (tbopre=1, the maker below: 0 nothing asked, 1 asked or being made, 2 made and waiting in px, 3 nothing came of it) */
    unsigned char state;
    uint32_t *px, w, h, pxVersion, failVersion, retry, readyFrame;
} TboTex;

#define TBO_NAME(id, name) name,
static const char *const kTboNames[TBO_ART_COUNT] = {TBO_ART_LIST(TBO_NAME)};
#undef TBO_NAME
static TboTex sPic[TBO_ART_COUNT], sText[TBO_TEXT_SLOTS], sEmblem, sLetters[2];

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

static unsigned tbo_make(TboTex *t, uint32_t *px, uint32_t w, uint32_t h, const char *what, int line) {
    unsigned tex;

    if (px == NULL) {
        return 0;
    }
    tex = Vita_VglTexRgba(px, (int)w, (int)h);
    free(px);
    if (tex == 0) {
        return 0;
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

/* A picture made as it is shown, as the GS's RGBA, or NULL (not readable: *missing). Everything but the texture's making:
   with tbopre=1 it is done by the maker's thread (below), else by the drawing thread at the picture's first use. */
static uint32_t *tbo_decode(uint32_t id, uint32_t *pw, uint32_t *ph, char *path, size_t pathSize, int *missing) {
    uint32_t w = 0, h = 0, limit;
    uint8_t *rgba;

    *missing = 0;
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
            px = TboText_Pixels(j.id, &version, &w, &h);
            if (px != NULL) {
                px = tbo_fit(px, &w, &h, 1024, 128);
            }
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
        pthread_mutex_unlock(&sMaker);
        sceKernelDelayThread(1500); /* (the game's thread asks the menus' font for its lines' measures: let go of it between two) */
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

static int upload_ok(unsigned bytes) {
    static unsigned frame, taken;
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
    return n;
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
            snprintf(path, sizeof(path), "app0:tbo/ui/%s.png", kTboNames[id == TBO_MAIN_TAG_EMBLEM || id == TBO_TAG_LOGO_OFF ? TBO_TAG_LOGO : id]);
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
static unsigned tbo_line(uint32_t id) {
    static unsigned frame, made;
    TboTex *t = &sText[id - TBO_TEXT_BASE];
    uint32_t version = TboText_Version(id), w, h, *px;

    if (t->tex != 0 && (t->version == version || t->frame == gGsFrame)) { /* (changed in this picture: the old one once more) */
        t->frame = gGsFrame;
        return t->tex;
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

/* ---- the main menu's "Tag Team" in the disc's lettering (bt4_main_lettering.inc), made once in a thread ---- */
#define LETTER_ROOT PORT_VITA_HOME "/gamedata"
static unsigned char *sLetterPic[2];
static volatile int sLetterState; /* 0 not begun, 1 being made, 2 done (sLetterPic NULL: not made) */

static int letter_thread(unsigned args, void *argp) {
    (void)args;
    (void)argp;
    /* (the PC reads the menus' archive members as loose files of its data folder: written once from the disc) */
    if (Vita_MemberFile("pzs4us1", 448, LETTER_ROOT) && Vita_MemberFile("pzs4us1", 449, LETTER_ROOT)) {
        if (!MenuLabels_Bt4MainItem(LETTER_ROOT, "Tag Team", sLetterPic)) {
            Vita_Log("tbo: the main menu's Tag Team lettering could not be put together from the disc\n");
        }
    } else {
        Vita_Log("tbo: the main menu's archive members 448 / 449 could not be written to " LETTER_ROOT "\n");
    }
    __sync_synchronize();
    sLetterState = 2;
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

static unsigned tbo_letters(int lit) {
    TboTex *t = &sLetters[lit];
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
    if (sLetterPic[lit] == NULL) {
        t->missing = 1;
        return 0;
    }
    px = malloc(512 * 32 * 4);
    if (px == NULL) {
        return 0;
    }
    for (i = 0; i < 512 * 32; i++) { /* (8-bit alpha to the GS's) */
        const uint8_t *c = sLetterPic[lit] + i * 4;
        px[i] = (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | ((((uint32_t)c[3] * 128 + 127) / 255) << 24);
    }
    return tbo_make(t, px, w, h, lit ? "main menu lettering, chosen" : "main menu lettering", 0);
}

/* The renderer (vita_vgl.c state_of_, the drawing thread): the texture of picture `id`, 0 for none (not drawn). */
unsigned Vita_TboTexture(uint32_t id) {
    if (!tbo_on()) {
        return 0;
    }
    if (id >= TBO_TEXT_BASE && id < TBO_TEXT_BASE + TBO_TEXT_SLOTS) {
        return tbo_line(id);
    }
    if (id == TBO_MAIN_TAG_OFF || id == TBO_MAIN_TAG_ON) {
        sPreAsked = gGsFrame; /* (the main menu, or the Duel menu's Tag type, is shown: the Tag Team screens' pictures are made ahead) */
        tbo_prefetch();
        return tbo_letters(id == TBO_MAIN_TAG_ON);
    }
    if (id == TBO_TAG_MOVIE) {
        /* the film behind the Tag Team screens (vita_tagmovie.c) when tbomovie is on and a picture is ready; else its poster */
        unsigned film = gVitaTagMovie == 1 ? Vita_TagMovieTexture() : gVitaTagMovie == 2 ? Vita_TagMovieSwTexture() : 0;
        if (film != 0) {
            return film;
        }
        id = TBO_TAG_ART;
    }
    if (id == TBO_MAIN_TAG_EMBLEM || (id < TBO_ART_COUNT && id != TBO_RADAR)) {
        return tbo_picture(id);
    }
    return 0; /* (the online hub's and the European disc's art, the ranked Dragon Balls: not offline) */
}

static void tbo_drop(TboTex *t, unsigned n) {
    unsigned i;
    for (i = 0; i < n; i++) {
        if (t[i].tex != 0 && gGsFrame - t[i].frame > KEEP_FRAMES) {
            Vita_VglTexFree(t[i].tex);
            t[i].tex = 0;
            t[i].version = 0;
            gVitaTboN[2]++;
            gVitaTboN[3]--;
            gVitaTboN[4] -= t[i].bytes;
        }
    }
}

/* At every picture's end (vita_vgl.c GsGpu_FrameEnd, after its batches are handed over): the lines' replaced
   textures given back; every 64th, the textures not drawn for a while. The main menu's emblem and lettering (~600 KB)
   are kept: made again, they would cost the logo's decoding and the disc's letters at every return to the menu. */
void Vita_TboFrameEnd(void) {
    unsigned held;
    if ((gGsFrame & 63) == 0) {
        Vita_TagMovieIdle(); /* the film's texture not drawn for a while */
        Vita_TagMovieSwIdle();
    }
    if (gVitaTboPre && !sBootDone && gGsFrame >= 240 && tbo_on()) {
        /* the main menu's emblem and lettering, made in the first seconds, long before the menu shows them */
        sBootDone = 1;
        letters_start();
        maker_ask(0, TBO_MAIN_TAG_EMBLEM, &sEmblem);
    }
    if (gVitaTboPre && (gGsFrame & 63) == 32 && gGsFrame - sPreAsked > KEEP_FRAMES) {
        /* the menus have not been shown for 20 s (a fight): the pixels kept for the Tag Team screens given back */
        unsigned i;
        pthread_mutex_lock(&sMaker);
        for (i = 0; i < TBO_ART_COUNT; i++) {
            if (sPic[i].state == 2) {
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
            }
        }
        pthread_mutex_unlock(&sMaker);
    }
    while (sLateN > 0) {
        sLateN--;
        Vita_VglTexFree(sLate[sLateN]);
        gVitaTboN[2]++;
        gVitaTboN[3]--;
        gVitaTboN[4] -= sLateBytes[sLateN];
    }
    held = gVitaTboN[3];
    if ((gGsFrame & 63) != 0 || held == 0) {
        return;
    }
    tbo_drop(sPic, TBO_ART_COUNT);
    tbo_drop(sText, TBO_TEXT_SLOTS);
    if (gVitaTboN[3] != held) {
        Vita_Log("tbo: textures not drawn for %u pictures given back: %u held, %u KB\n", KEEP_FRAMES, gVitaTboN[3], gVitaTboN[4] / 1024);
    }
}
