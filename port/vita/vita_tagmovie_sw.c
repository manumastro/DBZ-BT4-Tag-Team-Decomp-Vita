/*
 * The film behind BT4decomp's Tag Team screens on the PS Vita, the software path (bt3.txt tbomovie=2; the default path is
 * the hardware player of vita_tagmovie.c, tbomovie=1). Kept to compare with it: the PC's MPEG-2 decoder on one core,
 * about 4 pictures a second on the console (batch 29).
 *
 * On PC the film is tbo/ui/tag_ui_intro.pss, then tag_ui_loop.pss: 1280 x 720 MPEG-2, every picture intra (about 30 MB
 * with the port's pictures), played on a thread of its own (port/src/gs/tag_menu_movie.c) with the decoder of
 * port/src/plat_mpeg2.c, into one texture. This is the same: the decoder is that file, included below the way
 * tag_menu_movie.c includes it (its public names renamed, so that the two units do not clash); the thread, the pacing
 * at 24000/1001 pictures a second and the texture are tag_menu_movie.c's, on the Vita's threads and vitaGL.
 *
 *   tbomovie=2  the film: every picture decoded; the 1280 x 720 picture made 960 x 540 (nearest neighbour, the texture's
 *               size: the screens show it 960 across) and uploaded into one texture
 *
 * The package carries its files (tbo/ui/tag_ui_*.pss) only when built with BT4_VITA_TAGMOVIE=1 (port/vita/build.py; 30 MB
 * more). Without it the files are not found and the poster is drawn.
 *
 * When it runs: Port_TagMenuMovieBegin (port_tbo.c, the Tag Team screens opened) restarts the film; the drawing thread
 * starts the decoding thread when the screens ask for the film's picture, and the thread stops, its two buffers given
 * back, 0.5 s after the last one asked for. The texture is given back 600 pictures after it was last drawn (vita_tbo.c
 * KEEP_FRAMES). A film that cannot be opened is retried after 2 s; the poster is drawn meanwhile.
 *
 * Threads: the decoding thread on the fourth core when CapUnlocker's plugin is in (vita_main.c Vita_ThreadOnCore3), else
 * the second core, at the helpers' priority. The drawing thread only copies the newest picture into the texture (under
 * the lock: the decoder writes its other buffer meanwhile; the two buffers are never the same one at once). vitaGL's calls
 * are made on the drawing thread, after the render thread's work (vita_vgl.c Vita_VglTexRgba, Vita_VglTexUpdate).
 *
 * The log, every 2 s: "tagmovie:" with the pictures decoded, stepped over, replaced before they were uploaded, uploaded,
 * the decoding time a picture (average and worst), the upload time (average and worst), and the core the thread runs on.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>

/* the decoder of port/src/plat_mpeg2.c, as port/src/gs/tag_menu_movie.c includes it on PC. The build defines MPEG2_PLAYER
   for the opening movie's player (build.py): this unit has the plain decoder only, so the player's symbols are not made twice. */
#undef MPEG2_PLAYER
#define Mpeg2 TagMovieDecoder
#define Mpeg2_Open TagMovieDecoder_Open
#define Mpeg2_Close TagMovieDecoder_Close
#define Mpeg2_Next TagMovieDecoder_Next
#define Mpeg2_Width TagMovieDecoder_Width
#define Mpeg2_Height TagMovieDecoder_Height
#include "../src/plat_mpeg2.c"

extern void Vita_Log(const char *fmt, ...);
extern unsigned gGsFrame;
extern unsigned Vita_VglTexRgba(const void *px, int w, int h);
extern void Vita_VglTexUpdate(unsigned t, const void *px, int w, int h);
extern void Vita_VglTexFree(unsigned t);
extern SceUID Vita_ThreadOnCore3(const char *name, SceKernelThreadEntry entry, int prio, int stack, int fallbackMask);
extern int gVitaCore3;

extern int gVitaTagMovie; /* bt3.txt tbomovie (vita_tagmovie.c) */

#define TM_W 960
#define TM_H 540
#define TM_BYTES ((size_t)TM_W * TM_H * 4)
#define TM_IDLE_US 500000u   /* the thread stops this long after the last picture asked for */
#define TM_KEEP_FRAMES 600   /* the texture given back this many pictures after it was last drawn (vita_tbo.c) */
#define TM_RETRY_US 2000000u /* a film that could not be opened is not tried again for this long */
#define TM_LOG_US 2000000u
#define TM_PRIORITY 0x10000100 /* the helpers' (vita_tbo.c letter_thread, vita_audio.c's neighbours) */
#define TM_STACK 0x10000
#define TM_INTRO "app0:tbo/ui/tag_ui_intro.pss"
#define TM_LOOP "app0:tbo/ui/tag_ui_loop.pss"

static SceUID sLock = -1;
static struct {
    uint8_t *buf[2];        /* the decoder's two pictures (NULL while no thread runs) */
    int front;              /* the picture published last: the drawing thread reads buf[front] under sLock only (-1: none) */
    int running;
    int restart;            /* Port_TagMenuMovieBegin: the film from its start */
    uint32_t serial;        /* pictures published */
    uint32_t uploaded;      /* the serial of the picture in the texture */
    uint64_t touched;       /* us: the last picture asked for */
    uint64_t failedAt;      /* us: the last film that could not be played (0: none) */
    unsigned tex;           /* the texture (drawing thread) */
    unsigned texFrame;      /* gGsFrame when it was last drawn */
    /* the counters of the log (since the last line) */
    unsigned decoded, stepped, replaced, uploads;
    uint64_t decodeUs, decodeMax, uploadUs, uploadMax;
    int core;               /* the core the decoding thread runs on at its last log line */
} sM = {{NULL, NULL}, -1};

static uint64_t tm_now(void) { return sceKernelGetProcessTimeWide(); }
static void tm_lock(void) { sceKernelLockMutex(sLock, 1, NULL); }
static void tm_unlock(void) { sceKernelUnlockMutex(sLock, 1); }

/* The picture's YUV 4:2:0 planes of the decoder (after decode_unit) to RGBA at TM_W x TM_H with the GS's alpha 0x80:
   the same arithmetic as plat_mpeg2.c to_rgba (its tables, in this unit), nearest neighbour from the stream's size. */
static void tm_convert(const TagMovieDecoder *m, uint8_t *out, const uint16_t *xs, const uint16_t *ys) {
    int stride = m->mbw * 16, cstride = m->mbw * 8, x, y;
    for (y = 0; y < TM_H; y++) {
        int sy = ys[y];
        const uint8_t *py = m->y + sy * stride, *pb = m->cb + (sy >> 1) * cstride, *pr = m->cr + (sy >> 1) * cstride;
        uint32_t *o = (uint32_t *)(void *)(out + (size_t)y * TM_W * 4);
        for (x = 0; x < TM_W; x++) {
            int sx = xs[x], d = pb[sx >> 1], e = pr[sx >> 1];
            int c = sTy[py[sx]], r = sTrv[e], g = sTgu[d] + sTgv[e], b = sTbu[d];
            o[x] = (uint32_t)sClip[((c + r) >> 16) + 384] | (uint32_t)sClip[((c + g) >> 16) + 384] << 8 |
                   (uint32_t)sClip[((c + b) >> 16) + 384] << 16 | 0x80000000u;
        }
    }
}

/* One picture decoded into the planes (the file's units as plat_mpeg2.c next_planes, without its gVitaGsSkip: the
   game's frame skip must not step over the film). 1: a picture; 0: the end. */
static int tm_decode(TagMovieDecoder *m) {
    size_t size = next_unit(m);
    if (size == 0) {
        return 0;
    }
    m->havePicture = 0;
    m->headersOnly = 0;
    decode_unit(m, m->es, m->es + size);
    unit_done(m, size);
    return m->y != NULL;
}

/* A picture stepped over: its headers followed (the sequence state stays right), its slices not decoded. */
static int tm_step(TagMovieDecoder *m) {
    size_t size = next_unit(m);
    if (size == 0) {
        return 0;
    }
    m->headersOnly = 1;
    decode_unit(m, m->es, m->es + size);
    m->headersOnly = 0;
    unit_done(m, size);
    return 1;
}

/* The thread's end: its buffers given back (the drawing thread stops reading them: front -1), its state cleared. */
static void tm_finish(TagMovieDecoder *m, int failed) {
    uint8_t *b0, *b1;
    if (m != NULL) {
        TagMovieDecoder_Close(m);
    }
    tm_lock();
    b0 = sM.buf[0];
    b1 = sM.buf[1];
    sM.buf[0] = sM.buf[1] = NULL;
    sM.front = -1;
    sM.running = 0;
    if (failed) {
        sM.failedAt = tm_now();
    }
    tm_unlock();
    free(b0);
    free(b1);
    sceKernelExitDeleteThread(0);
}

static int tm_thread(SceSize args, void *argp) {
    TagMovieDecoder *m = NULL;
    uint16_t xs[TM_W], ys[TM_H];
    int mapW = 0, mapH = 0, inLoop = 0, got = 0;
    uint64_t origin = 0, logAt, now, due;
    uint32_t frames = 0;
    (void)args;
    (void)argp;

    tm_lock();
    sM.buf[0] = malloc(TM_BYTES);
    sM.buf[1] = malloc(TM_BYTES);
    sM.front = -1;
    sM.restart = 0;
    sM.decoded = sM.stepped = sM.replaced = sM.uploads = 0;
    sM.decodeUs = sM.decodeMax = sM.uploadUs = sM.uploadMax = 0;
    tm_unlock();
    if (sM.buf[0] == NULL || sM.buf[1] == NULL) {
        Vita_Log("tagmovie(sw): no memory for two pictures of %u KB: the poster\n", (unsigned)(2 * TM_BYTES / 1024));
        tm_finish(NULL, 1);
    }
    m = TagMovieDecoder_Open(TM_INTRO);
    if (m == NULL) {
        Vita_Log("tagmovie(sw): cannot open %s (not in the package: build with BT4_VITA_TAGMOVIE=1): the poster\n", TM_INTRO);
        tm_finish(NULL, 1);
    }
    Vita_Log("tagmovie(sw): started, tbomovie=%d, %u KB of pictures, on the %s core\n", gVitaTagMovie, (unsigned)(2 * TM_BYTES / 1024),
             gVitaCore3 == 1 ? "fourth" : "second");
    logAt = tm_now() + TM_LOG_US;
    for (;;) {
        int restart = 0, step;
        tm_lock();
        now = tm_now();
        if (now - sM.touched > TM_IDLE_US) {
            tm_unlock();
            break;
        }
        if (sM.restart) {
            sM.restart = 0;
            restart = 1;
        }
        tm_unlock();
        if (restart) {
            TagMovieDecoder_Close(m);
            m = TagMovieDecoder_Open(TM_INTRO);
            inLoop = got = 0;
            origin = 0;
            frames = 0;
            if (m == NULL) {
                tm_finish(NULL, 1);
            }
        }
        if (now >= logAt) {
            unsigned n;
            uint64_t dm, um;
            tm_lock();
            n = sM.decoded;
            dm = sM.decodeMax;
            um = sM.uploadMax;
            sM.core = (int)sceKernelGetCpuId();
            Vita_Log("tagmovie(sw): %u pictures decoded, %u stepped over, %u replaced before uploaded, %u uploaded in 2 s; decode %u ms a picture (worst %u), upload %u us (worst %u); the %s core (CPU %d)\n",
                     n, sM.stepped, sM.replaced, sM.uploads, n ? (unsigned)(sM.decodeUs / n / 1000) : 0u, (unsigned)(dm / 1000),
                     sM.uploads ? (unsigned)(sM.uploadUs / sM.uploads) : 0u, (unsigned)um, gVitaCore3 == 1 ? "fourth" : "second", sM.core);
            sM.decoded = sM.stepped = sM.replaced = sM.uploads = 0;
            sM.decodeUs = sM.decodeMax = sM.uploadUs = sM.uploadMax = 0;
            tm_unlock();
            logAt = now + TM_LOG_US;
        }
        /* the picture's time: the PC's 24000 / 1001 pictures a second; a picture is shown late when it is late, and the
           time is set again when the film is more than 85 ms behind (tag_menu_movie.c) */
        due = origin + (uint64_t)frames * 1001000000ull / 24000;
        if (origin != 0 && now < due) {
            uint64_t wait = due - now;
            sceKernelDelayThread((SceUInt)(wait > 5000 ? 5000 : wait));
            continue;
        }
        if (origin == 0) {
            origin = now;
        }
        step = 0; /* (every picture decoded; tm_step is kept for a later try) */
        if (step ? tm_step(m) : 0) {
            tm_lock();
            sM.stepped++;
            tm_unlock();
            got = 1;
        } else if (!step) {
            uint64_t d0 = tm_now(), d1;
            int back;
            if (tm_decode(m)) {
                d1 = tm_now();
                if (m->width != mapW || m->height != mapH) {
                    int x, y;
                    for (x = 0; x < TM_W; x++) {
                        xs[x] = (uint16_t)((x * m->width) / TM_W);
                    }
                    for (y = 0; y < TM_H; y++) {
                        ys[y] = (uint16_t)((y * m->height) / TM_H);
                    }
                    mapW = m->width;
                    mapH = m->height;
                }
                back = sM.front == 0 ? 1 : 0; /* (the drawing thread reads only buf[front], under the lock) */
                tm_convert(m, sM.buf[back], xs, ys);
                tm_lock();
                sM.front = back;
                sM.serial++;
                sM.decoded++;
                sM.decodeUs += d1 - d0;
                if (d1 - d0 > sM.decodeMax) {
                    sM.decodeMax = d1 - d0;
                }
                tm_unlock();
                got = 1;
            } else {
                /* the end of the intro: the loop, and the loop again at its end (tag_menu_movie.c). A loop that gives no
                   picture ends the film: the poster. */
                if (inLoop && !got) {
                    Vita_Log("tagmovie(sw): the loop %s gives no picture: the poster\n", TM_LOOP);
                    tm_finish(m, 1);
                }
                TagMovieDecoder_Close(m);
                m = TagMovieDecoder_Open(TM_LOOP);
                inLoop = 1;
                got = 0;
                origin = 0;
                frames = 0;
                if (m == NULL) {
                    Vita_Log("tagmovie(sw): cannot open %s: the poster\n", TM_LOOP);
                    tm_finish(NULL, 1);
                }
                continue;
            }
        } else {
            /* stepping over reached the end: the same as a decode reaching it */
            TagMovieDecoder_Close(m);
            m = TagMovieDecoder_Open(TM_LOOP);
            inLoop = 1;
            got = 0;
            origin = 0;
            frames = 0;
            if (m == NULL) {
                tm_finish(NULL, 1);
            }
            continue;
        }
        frames++;
        now = tm_now();
        if (now > origin + (uint64_t)frames * 1001000000ull / 24000 + 85000u) {
            origin = now - (uint64_t)frames * 1001000000ull / 24000;
        }
    }
    tm_finish(m, 0);
    return 0;
}

/* The game's Tag Team screens opened (src/menu/port_tbo.c): the film from its start. */
void Vita_TagMovieSwBegin(void) {
    if (sLock < 0) {
        sLock = sceKernelCreateMutex("bt3_tagmovie", 0, 0, NULL);
    }
    if (sLock < 0) {
        return;
    }
    tm_lock();
    sM.restart = 1;
    tm_unlock();
}

/* The drawing thread (vita_tbo.c, for the film's picture): the texture of the newest picture, 0 for none (the poster). */
unsigned Vita_TagMovieSwTexture(void) {
    uint64_t t0;
    unsigned tex;
    if (sLock < 0) {
        return 0;
    }
    tm_lock();
    t0 = tm_now();
    sM.touched = t0;
    if (!sM.running && (sM.failedAt == 0 || t0 - sM.failedAt > TM_RETRY_US)) {
        SceUID th = Vita_ThreadOnCore3("bt3_tagmovie", tm_thread, TM_PRIORITY, TM_STACK, 0x00020000 /* else the second core */);
        if (th >= 0 && sceKernelStartThread(th, 0, NULL) >= 0) {
            sM.running = 1;
        } else {
            sM.failedAt = t0;
        }
    }
    if (sM.front >= 0 && (sM.tex == 0 || sM.serial != sM.uploaded)) {
        uint64_t u0 = tm_now(), dt;
        if (sM.tex == 0) {
            sM.tex = Vita_VglTexRgba(sM.buf[sM.front], TM_W, TM_H);
        } else {
            Vita_VglTexUpdate(sM.tex, sM.buf[sM.front], TM_W, TM_H);
        }
        if (sM.serial - sM.uploaded > 1) {
            sM.replaced += sM.serial - sM.uploaded - 1;
        }
        sM.uploaded = sM.serial;
        dt = tm_now() - u0;
        sM.uploads++;
        sM.uploadUs += dt;
        if (dt > sM.uploadMax) {
            sM.uploadMax = dt;
        }
    }
    tex = sM.tex;
    sM.texFrame = gGsFrame;
    tm_unlock();
    return tex;
}

/* vita_tbo.c's frame end (every 64th picture): the texture not drawn for TM_KEEP_FRAMES given back. */
void Vita_TagMovieSwIdle(void) {
    unsigned t = 0;
    if (sLock < 0) {
        return;
    }
    tm_lock();
    if (sM.tex != 0 && gGsFrame - sM.texFrame > TM_KEEP_FRAMES) {
        t = sM.tex;
        sM.tex = 0;
    }
    tm_unlock();
    if (t != 0) {
        Vita_VglTexFree(t);
    }
}
