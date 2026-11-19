/*
 * PS Vita: start-up, settings, the log, and the ways out.
 *
 * The game's main() runs on a thread of its own with a large stack (the PS2 code keeps big buffers in locals; the
 * PC builds give it 16 MB). main is wrapped by the linker (--wrap=main), as in the 64-bit PC builds, and so is
 * exit: the port ends the program in several places, and an error is shown on a text screen until START.
 *
 * Files, all under ux0:data/bt3/ :
 *   disc/              the files of the player's own disc (SLUS_216.78, BIN/DBZP.BIN, DATA/...), as they are on it
 *   saves/             the memory card (port/src/plat_mc.c), bt3_settings.txt the port's settings
 *   bt3.txt            optional, one name=value per line. The released program reads:
 *                        disc=<folder>     another place for the disc files
 *                        wide=0            the console's 4:3 picture in the middle of the screen
 *                        heap=cdram        the game heap in graphics memory (see vita_mem.c)
 *                        report=1          speed figures every two seconds into the log
 *                      the development build (build.py, BT3_VITA_DEV=1) reads every setting of read_settings
 *                      below: test fights, renderer switches, statistics; see docs/vita/development.md.
 *   log.txt            written by every run (log-vgl.txt by the development build), kept across runs
 */
#include "vita_paths.h"
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
#include "debugScreen.h"

#define HOME PORT_VITA_HOME ""
#ifdef PORT_VITA_SFX /* the development build has its own log, so that its runs and the release's can be kept side by side */
#define SFX PORT_VITA_SFX
#else
#define SFX ".txt"
#endif
#define GAME_STACK 0x00800000

unsigned int _newlib_heap_size_user = 80 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 0x00100000;

char gVitaDisc[256] = HOME "/disc";
static FILE *sLog;
static int sHeapCdram, sFloatChoice = -1; /* -1: ask */
static int sM0, sPs2Float; /* bt3.txt m0, ps2float (vita_m0.c, vita_ps2float.c) */
static unsigned long long sStart, sBattleStart, sLastDraw;
static int sTagMenu = 1; /* bt3.txt tagteam: 0 no Tag Team entry in the main menu, 1 BT4decomp's own screens, 2 the earlier
                            text setup (Vita_TagMenu, below) */
unsigned gVitaVuMax[2], gVitaVuCut; /* gs_vu1.c: the interpreter's most in a list, runs left out */
unsigned gVitaOtMax, gVitaOtSize, gVitaDmaMax; /* gfx_ot.c Ot_Draw, dma.c Dma_Flush: the most taken since the last report */
unsigned gVitaChainMax, gVitaChainLoop, gVitaCallFull; /* gs_core.c run_chain: the longest chain of a list since the last report */
int gVitaAudioCore = 1, gVitaVjCore; /* bt3.txt audiocore, vjcore (vita_audio.c, vita_vgl.c) */
static int gVitaHangDump; /* bt3.txt hangdump=<s> (development build): a crash dump after that long without a picture */
static int gVitaExitAfter; /* bt3.txt exitafter=<s> (development build): the program ends itself after that many seconds */
static unsigned sBattleStartVb, sLastVb, sLastState = 0xFFFFFFFFu;
static volatile int sEnded;
static int sMode = -1; /* 0 the menus (play), 1 the demo fight; -1: ask */
int gVitaReport = 1;   /* the numbers every two seconds into the log (the released program: bt3.txt report=1) */
static const int kDemoPick = -1; /* the demo's pairing (0..8), or -1: each run the next one (ux0:data/bt3/demo_next.txt) */
/* the sweep: in the demo fight, after this many seconds of fighting the program starts itself again
   (the next pairing: demo_next.txt), so that one test run sees several pairings; 0: off (bt3.txt: sweep=<s>) */
static int sSweepSec = 32;
/* the soak's fights ended after this many seconds of fighting (bt3.txt soaksec=<s>; 0: they end by themselves), so
   that a survey of the stages gives each the same time (port/src/headless.c Port_SoakEndFight) */
static int sSoakSec;
volatile int gVitaShotReq; /* the drawing thread takes two pictures for the log (vita_vgl.c) */
extern int gVitaGsFixedSkip;
extern unsigned gVitaGsDrawn, gVitaGsLastDrawMs, gVitaGsSkipNow;

extern unsigned gPortVBlanks;
static unsigned long long now_us(void);
static void watch_log(void);
static void logf_(const char *fmt, ...) __attribute__((cold));
static int sPerfOff; /* bt3.txt perf=0 */
extern void Vita_TextScreen(void);
extern unsigned gVglDraws, gVglDropped, gVglNewTex, gVglBatches, gVglFilm, gVitaUsGs, gVitaUsMovie, gVitaUsUpload, gVitaUp[5], gVitaUpStore[3];
extern void Vita_Pace(int lateUs);
extern unsigned gVglVuTris, gVglVuBatches;
extern int Vita_MovieStats(unsigned stat[4], unsigned *decodeUs);
static unsigned sLateSum;
void Vita_Log(const char *fmt, ...) {
    char msg[1400]; /* (was 400, which cut the long lines and their ends) */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    logf_("%s", msg);
}
extern int gPortFastFloat;
extern uint32_t gVitaHeapBase, gVitaHeapUsed;
extern unsigned gVitaFileOpens, gVitaFileBytes, gPortVBlanks;
extern void Vita_FloatMode(int fast);
extern int Vita_FloatSelfTest(unsigned long long (*now)(void), unsigned nsPerOp[2][7], unsigned diffPermille[7], const char **names);
extern void Vita_MapMemory(int cdram);
extern void Vita_LoadGameData(const char *disc);
extern int __real_main(int argc, char **argv);

/* The fight's state, read the way port/src/headless.c reads it (integer interfaces only: this file is compiled
   for the Vita's float ABI, the game's is software float). */
extern struct { int state; } *gBtlSeq;
/* The list may be drawn while the game goes on: not while a film plays (its pictures come from the game's thread). */
int Vita_InFight(void) { return gBtlSeq != NULL && gBtlSeq->state == 3; }
/* the battle from its stage intro to its end (states 0..6): the same stage and fighters the whole time, drawn the fast
   way (kept parts, fighters on the chip) and shown every two blanks */
int Vita_InBattle(void) { return gBtlSeq != NULL && gBtlSeq->state >= 0 && gBtlSeq->state <= 6; }
/* bt3.txt filmstate=<n>: the film's pictures in that state of the fight (3, the fight, unless told: 0 the
   fighters' intros) */
int gVitaFilmState = 3;
/* The stage's first seconds: the renderer meets the stage's textures, its geometry and the models' parts
   for the first time and makes them (a picture of 133 ms, a dozen of 40 to 90). In these blanks the game
   waits for a picture still being drawn instead of leaving its own out: the intro plays without jumps, a moment later.
   (bt3.txt warm=<blanks>, 0: off.) */
int gVitaWarmVb = 300;
static unsigned sState0Vb, sVbNow;
/* bt3.txt introwait=1 (the default): the same for the whole of the intros (states 0 to 2: the stage's
   flight, the fighters' entrances): a picture that takes long holds the game up a little instead of the next
   picture being left out, which showed as a skip in the stage's intro. Not in the fight. */
int gVitaIntroWait = 1;
int gVitaSeaOff; /* bt3.txt seaoff=1: the stage's surfaces (eft_c.c EftSurf_Draw) not drawn */
int gVitaSurfCull = 1; /* eft_c.c EftSurf_DrawMeshes: surface triangles wholly outside a clip plane not prepared */
int Vita_WarmUp(void) {
    if (gBtlSeq == NULL) {
        return 0;
    }
    if (gVitaIntroWait && gBtlSeq->state >= 0 && gBtlSeq->state <= 2) {
        return 1;
    }
    return gVitaWarmVb > 0 && gBtlSeq->state == 0 && sVbNow - sState0Vb < (unsigned)gVitaWarmVb;
}
unsigned gVitaWarmWaits;
int gVitaFilmMax; /* bt3.txt filmmax=<n>: at most n pictures (the log's size: 75 of them made 18 MB, too much to fetch) */
/* bt3.txt filmtrig=<n>: the film only in bursts of n pictures in a row, each from a skill's name coming up
   in the fight (HudCombo_SetText: Wild Sense, Dragon Fist...), the moments the flicker of the ground under the water
   was seen in */
int gVitaFilmTrig;
static volatile unsigned sLogLost; /* (the log, below) */
static volatile int sFilmBurst;
static unsigned sFilmTrigs;
void Vita_FilmTrigger(int side, int text) {
    if (gVitaFilmTrig > 0 && Vita_InFight() && sFilmBurst <= 0) {
        sFilmBurst = gVitaFilmTrig;
        sFilmTrigs++;
        logf_("film: burst %u from a skill's name (side %d, entry %d) at blank %u (log bytes lost so far %u)\n", sFilmTrigs, side, text, gPortVBlanks, sLogLost);
    }
}
/* bt3.txt filmlist=<a>-<b>: the film in the lists a..b of the fight (the demo fight is the same in every
   run: the same lists show the same moment), every one of them drawn with filmwait=1 (the game waits for the
   picture instead of leaving lists out), so that the pictures follow one another */
int gVitaFilmFrom, gVitaFilmTo, gVitaFilmWait, gVitaFilmFrom2, gVitaFilmTo2;
extern unsigned gGsFrame;
int Vita_FilmWait(void) {
    return gVitaFilmWait && gVitaFilmTo > 0 && gGsFrame + 40u >= (unsigned)gVitaFilmFrom && gGsFrame <= (unsigned)gVitaFilmTo;
}
static int film_window(void) {
    return gVitaFilmTo > 0 && gGsFrame + 2u >= (unsigned)gVitaFilmFrom && gGsFrame <= (unsigned)gVitaFilmTo;
}
int Vita_FilmNow(void) {
    if (gVitaFilmTo > 0) {
        return (gGsFrame >= (unsigned)gVitaFilmFrom && gGsFrame <= (unsigned)gVitaFilmTo) ||
               (gVitaFilmTo2 > 0 && gGsFrame >= (unsigned)gVitaFilmFrom2 && gGsFrame <= (unsigned)gVitaFilmTo2);
    }
    if (gVitaFilmTrig > 0) {
        return sFilmBurst > 0;
    }
    return gBtlSeq != NULL && gBtlSeq->state == gVitaFilmState;
}
/* the game's side of a filmed moment: the stage's effects of the frame being made (a line each frame of a
   burst; the pictures follow a frame or two later) */
static int sTintMed = -1, sTintOn, sTintAdd, sCamY16, sWaterY16;
void Vita_FilmTintNote(int medium, int enabled, int additive, int camY16, int waterY16) {
    sTintMed = medium;
    sTintOn = enabled;
    sTintAdd = additive;
    sCamY16 = camY16;
    sWaterY16 = waterY16;
}
void Vita_FilmFxNote(unsigned bits, unsigned flagsHi, unsigned flagsLo) {
    static unsigned lastHi, lastLo, lastBits;
    static int lastMed = -2;
    if ((gVitaFilmTrig > 0 && sFilmBurst > 0) || film_window()) {
        logf_("film: frame at blank %u (list drawn last %u): stage features %x, battle flags %08x %08x (glare and fog %s), depth tint medium %d (%s%s), camera y %d.%02d, water %d.%02d\n",
              gPortVBlanks, gGsFrame, bits, flagsHi, flagsLo, (flagsHi & 0x04000000u) ? "left out" : "drawn", sTintMed, sTintOn ? "on" : "off",
              sTintAdd ? ", adds" : "", sCamY16 / 16, (sCamY16 < 0 ? -sCamY16 : sCamY16) % 16 * 100 / 16, sWaterY16 / 16,
              (sWaterY16 < 0 ? -sWaterY16 : sWaterY16) % 16 * 100 / 16);
    } else if (gVitaFilmTrig > 0 && Vita_InFight() && (((flagsHi ^ lastHi) & 0x04000000u) || bits != lastBits || sTintMed != lastMed)) {
        logf_("film: at blank %u: stage features %x, battle flags %08x %08x, depth tint medium %d (%s), camera y %d, water %d (sixteenths)\n",
              gPortVBlanks, bits, flagsHi, flagsLo, sTintMed, sTintOn ? "on" : "off", sCamY16, sWaterY16);
    }
    lastHi = flagsHi;
    lastLo = flagsLo;
    lastBits = bits;
    lastMed = sTintMed;
}
void Vita_FilmTaken(void) {
    if (sFilmBurst > 0) {
        sFilmBurst--;
    }
}
extern int Vita_MoviePlaying(void);
int Vita_AsyncOk(void) { return (gBtlSeq != NULL && gBtlSeq->state >= 2) || !Vita_MoviePlaying(); }
extern int *BtlSeq_GetClock(void);
extern int BtlCharApi_GetHp(int objId);

static unsigned long long now_us(void) {
    return sceKernelGetProcessTimeWide();
}

/* The console's count of vertical blanks, as a number that does not wrap. The system's own count (sceDisplayGetVcount)
   goes round at 65536, 18 min 13 s after the game starts: the waits for "blank N" (here and in vita_vgl.c) then
   waited for a blank that never came, and the picture stopped with the sound going on (a soak test at 1091 s, and
   the freezes "after about 20 minutes" players told of). Only the low 16 bits of the system's count are used: each
   reading moves this count on by how far those went round since the last one (from any thread). It only goes
   forward: a reading a little behind the last (another thread read a later one meanwhile) is given as that much
   behind, and a gap of more than 18 minutes (away in the LiveArea) counts short, which nothing minds. */
static volatile unsigned long long sVcState; /* high half: our count; low half: the system's count last read, and bit 16 once set */
unsigned Vita_Vcount(void) {
    unsigned raw = (unsigned)sceDisplayGetVcount() & 0xFFFFu;
    unsigned long long o, n;
    do {
        unsigned last, ext, d;
        o = __atomic_load_n(&sVcState, __ATOMIC_ACQUIRE);
        last = (unsigned)o & 0xFFFFu;
        ext = (unsigned)(o >> 32);
        d = ((unsigned)o & 0x10000u) ? (raw - last) & 0xFFFFu : 1000u; /* (the first: from 1000) */
        if (d >= 0xFF00u) {
            return ext - (0x10000u - d);
        }
        if (d == 0) {
            return ext;
        }
        n = ((unsigned long long)(ext + d) << 32) | 0x10000u | raw;
    } while (!__atomic_compare_exchange_n(&sVcState, &o, n, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    return (unsigned)(n >> 32);
}

/* The log: written into memory by whoever logs, and to the file by a thread of its own on the second core, ten
   times a second (a line written straight to the memory card took the game's thread 1-3 ms: the 120 lines every
   two seconds were a fifth of its time). log_sync writes what waits at once (fatal messages, the end). */
static char sLogBuf[1024 * 1024]; /* (was 256 KB; a filmed list's picture can go out whole) */
static volatile unsigned sLogHead, sLogTail, sLogLost;
static unsigned sLogSplit, sLogBytes; /* bt3.txt logsplit=<KB> */
static int sLogPart = 1;
static volatile int sLogLock, sLogDrainLock, sLogThreaded;
static void log_put(const char *p, unsigned n) {
    while (__sync_lock_test_and_set(&sLogLock, 1)) {
    }
    if (sLogHead + n - sLogTail <= sizeof(sLogBuf)) {
        unsigned at = sLogHead % sizeof(sLogBuf), first = n < sizeof(sLogBuf) - at ? n : sizeof(sLogBuf) - at;
        memcpy(sLogBuf + at, p, first);
        memcpy(sLogBuf, p + first, n - first);
        __sync_synchronize();
        sLogHead += n;
    } else {
        sLogLost += n;
    }
    __sync_lock_release(&sLogLock);
}
static void log_sync(void) {
    unsigned h, t;
    while (__sync_lock_test_and_set(&sLogDrainLock, 1)) {
        sceKernelDelayThread(100);
    }
    h = sLogHead;
    __sync_synchronize();
    t = sLogTail;
    if (sLog != NULL && t != h) {
        while (t != h) {
            unsigned at = t % sizeof(sLogBuf), n = h - t < sizeof(sLogBuf) - at ? h - t : sizeof(sLogBuf) - at;
            fwrite(sLogBuf + at, 1, n, sLog);
            t += n;
            sLogBytes += n;
        }
        fflush(sLog);
        if (sLogSplit != 0 && sLogBytes >= sLogSplit && sLogPart < 9) { /* the next part */
            char path[64];
            FILE *f;
            snprintf(path, sizeof(path), HOME "/log-part%d" SFX, sLogPart + 1);
            if ((f = fopen(path, "w")) != NULL) {
                fclose(sLog);
                sLog = f;
                sLogPart++;
                sLogBytes = 0;
            }
        }
    }
    sLogTail = h;
    __sync_lock_release(&sLogDrainLock);
}
static int log_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (;;) {
        sceKernelDelayThread(100000);
        log_sync();
    }
    return 0;
}
static void logf_(const char *fmt, ...) {
    va_list ap;

    if (sLog != NULL) {
        if (sLogThreaded) {
            char line[1400];
            int n;
            va_start(ap, fmt);
            n = vsnprintf(line, sizeof(line), fmt, ap);
            va_end(ap);
            if (n > (int)sizeof(line) - 1) {
                n = (int)sizeof(line) - 1;
                line[n - 1] = '\n'; /* (a cut line still ends its line) */
            }
            if (n > 0) {
                log_put(line, (unsigned)n);
            }
            return;
        }
        va_start(ap, fmt);
        vfprintf(sLog, fmt, ap);
        va_end(ap);
        fflush(sLog);
    }
}

static unsigned buttons(void) {
    SceCtrlData pad;

    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);
    return pad.buttons;
}

static unsigned wait_buttons(unsigned mask) {
    unsigned b;

    while (buttons() & mask) { /* released first: the press that brought us here does not count */
        sceKernelDelayThread(20000);
    }
    for (;;) {
        b = buttons() & mask;
        if (b != 0) {
            return b;
        }
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        sceKernelDelayThread(20000);
    }
}

/* Leaving: the drawing and render threads stopped first where they touch nothing of the graphics chip's (gs_core.c
   gr_thread, vita_vgl.c rt_thread). The process ended while the render thread was inside SceGxm, which faulted as its
   memory went: a crash dump at every exitafter, and at START + SELECT. */
volatile int gVitaQuitting, gVitaParked;
static void quit(void) {
    {
        int k;
        gVitaQuitting = 1;
        __sync_synchronize();
        for (k = 0; k < 40 && (gVitaParked & 3) != 3; k++) {
            sceKernelDelayThread(5000);
        }
    }
    log_sync();
    if (sLog != NULL) {
        fflush(sLog);
    }
    fflush(stdout);
    fflush(stderr);
    sceKernelExitProcess(0);
    for (;;) {
        sceKernelDelayThread(1000000);
    }
}

/* A message the program cannot go on after: red on the screen, in the log, then START ends the program. */
void Vita_Fatal(const char *fmt, ...) {
    char msg[1400];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    sEnded = 1;
    logf_("FATAL: %s\n", msg);
    log_sync();
    Vita_TextScreen();
    psvDebugScreenPrintf("\n\e[31m%s\e[39m\n\nPress START to quit.\n", msg);
    wait_buttons(SCE_CTRL_START);
    quit();
}

/* A function of the PS2's libraries that the Vita build has no stand-in for (build.py generates one caller of this
   per missing name, port/build/gen/vita_<variant>/stubs.c). None of them is reached in the game. */
void Port_Missing(const char *name) {
    Vita_Fatal("not available on the Vita: %s", name);
}

static void summary(int final) {
    unsigned long long t = now_us();
    unsigned vb = gPortVBlanks - sBattleStartVb;
    unsigned long long us = t - sBattleStart;

    if (sBattleStart != 0 && us > 0 && vb > 0) {
        unsigned perSec10 = (unsigned)((unsigned long long)vb * 10000000ull / us);
        unsigned percent = perSec10 * 1000u / 5994u;
        char line[200];

        snprintf(line, sizeof(line), "fight so far: %u vertical blanks in %u.%u s = %u.%u per second = %u%% of the console's speed",
                 vb, (unsigned)(us / 1000000), (unsigned)(us / 100000 % 10), perSec10 / 10, perSec10 % 10, percent);
        psvDebugScreenPrintf("%s\n", line);
        if (final) {
            logf_("vita: %s\n", line);
        }
    }
}

/* (src/battle/btl_char_member.c BtlMember_Damage) 1: no fighter is knocked out, a blow leaves at least 1 of health, as
   in the game's modes 5 and 6. Set by the development build's demoult (below); 0 otherwise, in the release too. */
int gVitaNoKo;

#ifndef PORT_VITA_RELEASE
/* A fighter's technique name n (0, 1 the Blast 1 skills; 2, 3 the Blast 2 moves; 4 the Ultimate) from the
   character's command list, the text the HUD shows when the move is used (src/battle/hud_c_b.c: the battle
   object's script at +0xBC handed to BtlMenu_SetScript2, BtlText_FindEntry), as UTF-8 for the log. */
static void tech_name(void *chr, int n, char *out, int size) {
    extern void *BtlChar_GetObj(void *chr);
    extern unsigned short *BtlMenu_GetScript2(void);
    extern void BtlMenu_SetScript2(unsigned short *text);
    extern unsigned short *BtlText_FindEntry(int n);
    unsigned char *obj = BtlChar_GetObj(chr);
    unsigned short *keep = BtlMenu_GetScript2(), *s;
    int k = 0;

    out[0] = '\0';
    if (obj == NULL || *(unsigned short **)(obj + 0xBC) == NULL) {
        return;
    }
    BtlMenu_SetScript2(*(unsigned short **)(obj + 0xBC));
    s = BtlText_FindEntry(n);
    BtlMenu_SetScript2(keep);
    for (; s != NULL && *s != 0 && *s != '\n' && *s != '\r' && k < size - 4; s++) {
        unsigned c = *s;
        if (c >= 0xE000 && c < 0xF900) { /* (the font's own marks) */
            continue;
        }
        if (c < 0x80) {
            out[k++] = (char)c;
        } else if (c < 0x800) {
            out[k++] = (char)(0xC0 | (c >> 6));
            out[k++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[k++] = (char)(0xE0 | (c >> 12));
            out[k++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[k++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[k] = '\0';
}

/* Tests: the heavy effects again and again (bt3.txt demoult=<slot>, with mode=demo and demoduel=1). Every
   demoultsec seconds of the fight's own time (its blanks, the same game frame in every run) the fighter demoultp
   (0, 1, or 2 for both in turn) uses its technique of that slot (2, 3 a Blast 2; 4 the Ultimate; 0, 1 a Blast 1)
   through the story scripts' own command (BtlCtrl_UseTechnique, src/battle/btl_capi_b.c: the ki, the blast stock
   and for the Ultimate the MAX power mode given; the move goes when the fighter is free), with its opponent made
   to stand facing it first (demostand=1, default: BtlCtrl_ForceReaction, as the scripts' command 29 does; the stand
   ends at the blow). Both fighters' health is set to demohp percent (default 100) before each use; below 50 a
   technique that destroys the stage does it, as when it finishes a fight (the demo duel's rule allows it; once a
   fight: the destroyed stage has no further form). No knock-out ends the test (gVitaNoKo). The log: each fighter's
   five techniques by name, the stage's destroyed form, each use (asked for, started, over) in ms of the run.
   With more fighters (BT4_TAG_ACTOR_TEST=4: fighters 0 and 2 against 1 and 3; demochara3, demochara4 their
   characters, src/battle/battle_work.c TagBattle_Prepare): demoultp=4 every fighter in turn (0, 1, 2, 3), or
   demoultlist=<fighters in their order> (e.g. 0,3); the opponent is the user's target (BtlOpp_GetPlayer: always one
   of the other team); every fighter's health set to demohp; demostand=2 makes the other three stand, not only the
   target; whoever still stands when the use is over or given up is released then. Without these the 1 vs 1 test is
   as before. */
static void demo_heavy(unsigned state, unsigned vblanks, unsigned long long t) {
    extern void *BtlChar_Get(int player);
    extern int BtlChar_GetCount(void);
    extern int *BtlMember_GetActiveGauge(void *chr);
    extern int BtlCharApi_GetChara(int objId);
    extern int BattleSide_GetObjId(int side);
    extern int TagBattle_Team(int actor);
    extern int BtlSuper_GetId(void *chr, int slot);
    extern int BtlSuper_GetType(void *chr, int slot);
    extern int BtlSuper_GetKiCost(void *chr, int slot);
    extern int BtlSkill_GetId(void *chr, int slot);
    extern int BtlAct_GetCurrent(void *chr);
    extern int BtlAct_GetCurrentClass(void *chr);
    extern void BtlAct_Request(void *chr, int id);
    extern void BtlChar_ClearFlag(void *chr, unsigned n);
    extern int BtlOpp_GetPlayer(void *chr);
    extern int BtlCtrl_UseTechnique(int player, int kind);
    extern int BtlCtrl_ForceReaction(int player, unsigned kind);
    extern int BtlCtrl_CanAct(int player);
    extern int Battle_GetStage(void);
    extern int Battle_GetRuleUnk10(void);
    extern int BtlStage_GetChangeTarget(void);
    static int slot = -2, who, sec, hp, stand, turn, pending = -1, started, uses, lastStage = -1, logged[8];
    static int order[10], norder; /* (demoultlist, or -1 for demoultp=4: every fighter in turn; 0: as demoultp says) */
    static unsigned stood; /* (the fighters made to stand for the use pending, with more than two fighters) */
    static unsigned lastState = 0xFFFFFFFFu, nextVb, issuedVb, startVb, waitVb;
    unsigned ms = (unsigned)((t - sStart) / 1000);
    int count, p, i;
    void *chr;

    if (slot == -2) {
        const char *e = getenv("BT3_DEMO_ULT");
        slot = e != NULL && *e != '\0' ? atoi(e) : -1;
        if (slot > 4) {
            slot = -1;
        }
        who = getenv("BT3_DEMO_ULTP") != NULL ? atoi(getenv("BT3_DEMO_ULTP")) : 0;
        sec = getenv("BT3_DEMO_ULTSEC") != NULL ? atoi(getenv("BT3_DEMO_ULTSEC")) : 15;
        hp = getenv("BT3_DEMO_HP") != NULL ? atoi(getenv("BT3_DEMO_HP")) : 100;
        stand = getenv("BT3_DEMO_STAND") != NULL ? atoi(getenv("BT3_DEMO_STAND")) : 1;
        if (sec < 3) {
            sec = 3;
        }
        if (who == 4) {
            norder = -1;
        }
        if ((e = getenv("BT3_DEMO_ULTLIST")) != NULL) { /* fighters 0 .. 9, any separator */
            for (norder = 0; *e != '\0' && norder < 10; e++) {
                if (*e >= '0' && *e <= '9') {
                    order[norder++] = *e - '0';
                }
            }
        }
        gVitaNoKo = slot >= 0;
        if (slot >= 0 && norder == 0) {
            logf_("vita: heavy: technique slot %d of fighter %s every %d s of the fight, health %d%% before each, the opponent %s\n", slot,
                  who == 2 ? "0 and 1 in turn" : who == 1 ? "1" : "0", sec, hp, stand ? "made to stand" : "left to the computer");
        } else if (slot >= 0) {
            char w[40] = "every fighter";
            if (norder > 0) {
                for (i = 0, w[0] = '\0'; i < norder; i++) {
                    snprintf(w + strlen(w), sizeof(w) - strlen(w), i ? ", %d" : "%d", order[i]);
                }
            }
            logf_("vita: heavy: technique slot %d of %s in turn every %d s of the fight, health %d%% before each (every fighter's), %s\n", slot, w, sec,
                  hp, stand == 2 ? "the three others made to stand" : stand ? "the opponent made to stand" : "the opponent left to the computer");
        }
    }
    if (slot < 0 || sMode != 1) {
        return;
    }
    if (state != 3) {
        lastState = state;
        return;
    }
    count = BtlChar_GetCount();
    if (lastState != 3) { /* the fight starts (again, after a pause or a stage change) */
        if (uses == 0 && pending < 0) {
            nextVb = vblanks + 5 * 60; /* (the first use 5 s in) */
        }
        if (lastStage < 0) {
            lastStage = Battle_GetStage();
            logf_("vita: heavy: stage %d, its destroyed form %d (-1: none), the rule lets a stage be destroyed: %d\n", lastStage,
                  BtlStage_GetChangeTarget(), Battle_GetRuleUnk10());
        }
    }
    lastState = state;
    if (Battle_GetStage() != lastStage) {
        logf_("vita: heavy: the stage is now %d (was %d) at %u ms\n", Battle_GetStage(), lastStage, ms);
        lastStage = Battle_GetStage();
    }
    for (p = 0; p < count && p < 8; p++) { /* each fighter's techniques, once for each character it is */
        int c;
        chr = BtlChar_Get(p);
        c = chr != NULL ? BtlCharApi_GetChara(BattleSide_GetObjId(p)) : -1;
        if (chr != NULL && c != logged[p] - 1) {
            char n[5][96];
            for (i = 0; i < 5; i++) {
                tech_name(chr, i, n[i], sizeof(n[i]));
            }
            logf_("vita: heavy: fighter %d (character %d): 0 \"%s\" (skill %d), 1 \"%s\" (skill %d), 2 \"%s\" (technique %d, kind %d, ki %d), "
                  "3 \"%s\" (technique %d, kind %d, ki %d), 4 \"%s\" (technique %d, kind %d, ki %d)\n", p, c, n[0], BtlSkill_GetId(chr, 0), n[1],
                  BtlSkill_GetId(chr, 1), n[2], BtlSuper_GetId(chr, 2), BtlSuper_GetType(chr, 2), BtlSuper_GetKiCost(chr, 2), n[3],
                  BtlSuper_GetId(chr, 3), BtlSuper_GetType(chr, 3), BtlSuper_GetKiCost(chr, 3), n[4], BtlSuper_GetId(chr, 4),
                  BtlSuper_GetType(chr, 4), BtlSuper_GetKiCost(chr, 4));
            logged[p] = c + 1;
        }
    }
    if (pending >= 0) { /* the use asked for: started, over, or not taken */
        chr = BtlChar_Get(pending);
        if (chr == NULL) {
            pending = -1;
            return;
        }
        if (!started && BtlAct_GetCurrentClass(chr) == slot) {
            started = 1;
            startVb = vblanks;
            logf_("vita: heavy: use %d: fighter %d's technique started (action 0x%x) at %u ms, %u blanks after it was asked for\n", uses, pending,
                  BtlAct_GetCurrent(chr), ms, vblanks - issuedVb);
        } else if (started && BtlAct_GetCurrentClass(chr) != slot) {
            logf_("vita: heavy: use %d: over at %u ms (%u.%u s of the technique)\n", uses, ms, (vblanks - startVb) / 60, (vblanks - startVb) % 60 / 6);
            pending = -1;
        } else if (!started && vblanks - issuedVb > 4 * 60) { /* not taken in 4 s: the forced input given up (the fighter's
                                                                  own input was held off until then) */
            BtlChar_ClearFlag(chr, 0x120 + (unsigned)slot);
            BtlChar_ClearFlag(chr, 0x11E);
            BtlChar_ClearFlag(chr, 0x11F);
            logf_("vita: heavy: use %d: fighter %d's technique not taken in 4 s (action 0x%x), given up at %u ms\n", uses, pending, BtlAct_GetCurrent(chr), ms);
            pending = -1;
        }
        if (pending < 0 && stood != 0) { /* (more than two fighters: the ones still made to stand let go now, not at their
                                            own turn, as BtlAct_WaitHandler ends a stand) */
            for (i = 0; i < count && i < 10; i++) {
                void *sc = (stood >> i) & 1 ? BtlChar_Get(i) : NULL;
                int a = sc != NULL ? BtlAct_GetCurrent(sc) : 0;
                if (a >= 7 && a <= 10) {
                    logf_("vita: heavy: fighter %d still made to stand (action 0x%x) at %u ms: its stand ended\n", i, a, ms);
                    BtlAct_Request(sc, 0xB);
                }
            }
            stood = 0;
        }
        return;
    }
    if ((int)(vblanks - nextVb) < 0) {
        return;
    }
    if (norder != 0) { /* (the fighters in turn: demoultp=4 or demoultlist) */
        p = norder > 0 ? order[turn % norder] : count > 0 ? turn % count : 0;
        if (p >= count) { /* (not in this fight: the next one) */
            turn++;
            return;
        }
    } else {
        p = who == 2 ? (turn & 1) : who;
    }
    chr = p < count ? BtlChar_Get(p) : NULL;
    if (chr == NULL) {
        return;
    }
    if (!BtlCtrl_CanAct(p)) { /* (busy: tried again at the next blank) */
        int a = BtlAct_GetCurrent(chr);
        if (waitVb == 0) {
            waitVb = vblanks;
        } else if (a >= 7 && a <= 10 && vblanks - waitVb > 2 * 60) {
            /* made to stand by a use before and not hit: the stand ended as BtlAct_WaitHandler ends it */
            logf_("vita: heavy: fighter %d still made to stand (action 0x%x) at %u ms: its stand ended\n", p, a, ms);
            BtlAct_Request(chr, 0xB);
            waitVb = vblanks;
        } else if (vblanks - waitVb > 10 * 60) {
            logf_("vita: heavy: fighter %d has not been free for 10 s (action 0x%x) at %u ms\n", p, a, ms);
            waitVb = vblanks;
        }
        return;
    }
    waitVb = 0;
    {
        int opp = BtlOpp_GetPlayer(chr), *g;
        void *oc = opp >= 0 && opp < count ? BtlChar_Get(opp) : NULL;
        char teams[40] = "";
        if (hp > 0) {
            g = BtlMember_GetActiveGauge(chr); /* (src/battle/btl_char_member.h BtlMemberGauge: health, healthMax) */
            g[0] = (int)((long long)g[1] * (hp > 100 ? 100 : hp) / 100);
            if (g[0] < 1) {
                g[0] = 1;
            }
            if (oc != NULL) {
                g = BtlMember_GetActiveGauge(oc);
                g[0] = (int)((long long)g[1] * (hp > 100 ? 100 : hp) / 100);
                if (g[0] < 1) {
                    g[0] = 1;
                }
            }
            for (i = 0; count > 2 && i < count; i++) { /* (more fighters: the others too, whoever the blow finds) */
                void *oc2 = i != p && i != opp ? BtlChar_Get(i) : NULL;
                if (oc2 != NULL) {
                    g = BtlMember_GetActiveGauge(oc2);
                    g[0] = (int)((long long)g[1] * (hp > 100 ? 100 : hp) / 100);
                    if (g[0] < 1) {
                        g[0] = 1;
                    }
                }
            }
        }
        if (slot >= 2) { /* the technique's lock counter (src/battle/btl_act_j.c BtlAct_CanUseTechnique: a flag-0x2000 move
                            is refused while it is above 0, outside the story mode; include/battle/btl_act_j.h, gauge
                            + 0x4C + 4 x slot) */
            BtlMember_GetActiveGauge(chr)[0x4C / 4 + slot] = 0;
        }
        if (stand && oc != NULL) {
            BtlCtrl_ForceReaction(opp, 0);
        }
        if (count > 2) {
            stood = stand && oc != NULL && opp < 10 ? 1u << opp : 0;
            for (i = 0; stand == 2 && i < count && i < 10; i++) {
                if (i != p && i != opp && BtlChar_Get(i) != NULL) {
                    BtlCtrl_ForceReaction(i, 0);
                    stood |= 1u << i;
                }
            }
            snprintf(teams, sizeof(teams), " (team %d against team %d)", TagBattle_Team(p), TagBattle_Team(opp));
        }
        uses++;
        i = BtlCtrl_UseTechnique(p, slot);
        logf_("vita: heavy: use %d: fighter %d (character %d) asked for its technique %d at %u ms (the fight's blank %u)%s, opponent %d%s\n", uses, p,
              BtlCharApi_GetChara(BattleSide_GetObjId(p)), slot, ms, vblanks, i ? "" : ", refused", opp, teams);
        if (i) {
            pending = p;
            started = 0;
            issuedVb = vblanks;
        }
        turn++;
        nextVb = vblanks + (unsigned)sec * 60u;
    }
}
#endif

/* Called by Port_VBlank (port/src/plat_stub.c) at every vertical blank of the game: twice a second it redraws
   the status. The game does not wait for anything in this build, so blanks per second IS the game's speed. */
static unsigned long long sStallEndT; /* (the game's thread: when it last came out of a blank's wait, and the console's count then) */
static unsigned sStallEndVc;
static void unlock_in_menus(int options); /* (Unlock All from the menus, below) */
static volatile int sUnlockAsk; /* Vita Settings asked for the unlock screen: shown at the next half-second step, below */
void Vita_VBlank(unsigned vblanks) {
    unsigned long long t;

    {   /* with a picture the vertical blank is the clock, as with a window on PC: one every 1 / 59.94 s, on a grid
           that stays where it is. A frame that took too long is made up for by the waits after it being shorter
           (up to two blanks' time; beyond that the grid moves along with the game): a battle frame and a film's
           frame wait for two blanks, and the second wait must not add a whole blank to a frame that was only late
           for the first. This is the only pacing on the Vita (Port_VBlank in plat_stub.c leaves its own out). */
        /* the grid is the console's own blanks: the game waits for the blank of its tick itself, so the
           ticks and the pictures shown at them (vita_vgl.c) never drift apart. "late": how long after that blank
           the game came, worked out from the last blank it waited for. */
        static unsigned nextVc, vbAt;
        static unsigned long long vbT;
        static int have;
        unsigned long long now = now_us();
        unsigned vc = Vita_Vcount();
        long long late;
        {   /* (for the log: where the system's own count goes round) */
            static unsigned lastRaw, wraps;
            unsigned raw = (unsigned)sceDisplayGetVcount();
            if (lastRaw == 0) {
                logf_("vita: the system's count of blanks at the first frame: %u (ours %u)\n", raw, vc);
            } else if (raw < lastRaw && wraps < 8) {
                wraps++;
                logf_("vita: the system's count of blanks went from %u to %u at %u ms (ours %u)\n", lastRaw, raw, (unsigned)((now - sStart) / 1000), vc);
            }
            lastRaw = raw ? raw : 1;
        }
        {   /* Back from the LiveArea (the PS button) or from sleep: the whole program was put aside, its threads
               stopped, more than a second between two of the game's blanks of which it hardly ran. The system can come
               back with its own clocks (the LiveArea's are lower): ours are set again, and the grid of blanks starts
               anew from here. */
            static unsigned long long lastT, lastRun;
            SceKernelThreadInfo ti;
            memset(&ti, 0, sizeof(ti));
            ti.size = sizeof(ti);
            if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &ti) >= 0) {
                if (lastT != 0 && now - lastT > 1000000 && (ti.runClocks - lastRun) * 4 < now - lastT) {
                    int cpu = scePowerGetArmClockFrequency(), bus = scePowerGetBusClockFrequency(), gpu = scePowerGetGpuClockFrequency(),
                        xbar = scePowerGetGpuXbarClockFrequency();
                    scePowerSetArmClockFrequency(444);
                    scePowerSetBusClockFrequency(222);
                    scePowerSetGpuClockFrequency(222);
                    scePowerSetGpuXbarClockFrequency(166);
                    have = 0;
                    logf_("vita: back after %u ms away (the LiveArea or sleep); the clocks were %d / %d / %d / %d MHz, set again to %d / %d / %d / %d\n",
                          (unsigned)((now - lastT) / 1000), cpu, bus, gpu, xbar, scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
                          scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
                }
                lastRun = ti.runClocks;
            }
            lastT = now;
        }
        {   /* the game's thread stopped between two of its blanks without running: the whole program
               held up from outside (1.3 s of a fight at 64 s, with no thread of it busy) */
            static unsigned long long endRun;
            static unsigned stalls;
            SceKernelThreadInfo ti;
            memset(&ti, 0, sizeof(ti));
            ti.size = sizeof(ti);
            if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &ti) >= 0) {
                if (sStallEndT != 0 && now - sStallEndT > 60000 && (now - sStallEndT) - (ti.runClocks - endRun) > 40000 && stalls < 40) {
                    stalls++;
                    logf_("vita: the game's thread held up at %u ms: %u ms between two of its blanks, of them %u ms running; the console's blanks meanwhile %u\n",
                          (unsigned)((now - sStart) / 1000), (unsigned)((now - sStallEndT) / 1000), (unsigned)((ti.runClocks - endRun) / 1000), vc - sStallEndVc);
                }
                endRun = ti.runClocks;
            }
        }

        if (!have) {
            nextVc = vc;
            vbAt = vc;
            vbT = now;
            have = 1;
        }
        if ((int)(vc - nextVc) > 2) { /* (more than two blanks late: the grid moves along with the game) */
            nextVc = vc - 2;
        }
        if ((int)(vc - nextVc) < 0) {
            extern volatile unsigned char gProfGame;
            gProfGame = 1;
            {   /* (never more than the two blanks of a frame and a little: a grid out of reach starts anew) */
                int guard = 0;
                while ((int)(Vita_Vcount() - nextVc) < 0) {
                    if (++guard > 8) {
                        logf_("vita: waited %d blanks for blank %u (the count at %u): the grid starts anew\n", guard - 1, nextVc, Vita_Vcount());
                        nextVc = Vita_Vcount();
                        break;
                    }
                    sceDisplayWaitVblankStart();
                }
            }
            gProfGame = 0;
            vbT = now_us();
            vbAt = Vita_Vcount();
            late = -1;
        } else {
            late = (long long)now - ((long long)vbT + (long long)(int)(nextVc - vbAt) * 16683);
            if (late < 0) {
                late = 0;
            }
        }
        {
            extern volatile unsigned gVitaGridVb;
            extern unsigned long long gVitaTickT[64];
            extern volatile int gVitaGameLate;
            gVitaTickT[nextVc & 63] = (unsigned long long)((long long)vbT + (long long)(int)(nextVc - vbAt) * 16683);
            gVitaGameLate = late > 0 ? (int)late : 0;
            __sync_synchronize();
            gVitaGridVb = nextVc;
        }
        nextVc++;
        sStallEndT = now_us();
        sStallEndVc = Vita_Vcount();
        Vita_Pace((int)late); /* (vita_gfx.c: is the next frame drawn?) */
        sLateSum += late > 0 ? (unsigned)late : 0;
    }
    t = now_us();
    unsigned state = gBtlSeq != NULL ? (unsigned)gBtlSeq->state : 0xFFFFFFFEu;

    sVbNow = vblanks;
    if (state != sLastState) {
        logf_("vita: vertical blank %u, %u ms: fight sequence state %d\n", vblanks, (unsigned)((t - sStart) / 1000), (int)state);
        if (state == 0) {
            sState0Vb = vblanks; /* (the warm-up's blanks count from here) */
        }
        if (sBattleStart == 0 && gBtlSeq != NULL && gBtlSeq->state >= 2) {
            sBattleStart = t;
            sBattleStartVb = vblanks;
        }
        sLastState = state;
    }
#ifndef PORT_VITA_RELEASE
    {   /* Tests: each fight's fighters and the forms they can turn into (the log), and with bt3.txt demoxform=<s> the
           first player turns into its forms in turn every <s> seconds of the fight (transformation's cost: the reports
           of super saiyan 3 Goku and other transformations dropping the picture rate) */
        extern void *BtlChar_Get(int player);
        extern int BtlParam_GetSlotId(void *chr, int slot);
        extern int BtlCtrl_Transform(int player, int id);
        extern int BtlCharApi_GetChara(int objId);
        static int lastState = -9, lastChara[2] = {-1, -1}, next;
        static unsigned long long fightT, lastX;
        static const char *xs;
        int p, k;
        if (xs == NULL) {
            xs = getenv("BT3_DEMO_XFORM") != NULL ? getenv("BT3_DEMO_XFORM") : "";
        }
        if (state == 3 && lastState != 3) {
            fightT = t;
            lastX = t;
        }
        lastState = (int)state;
        for (p = 0; p < 2 && state == 3; p++) {
            void *chr = BtlChar_Get(p);
            int c = chr != NULL ? BtlCharApi_GetChara(p) : -1;
            if (chr != NULL && c != lastChara[p]) {
                logf_("vita: fighter %d is character %d (0x%02x) at %u ms; its forms: %d %d %d %d\n", p, c, c, (unsigned)((t - sStart) / 1000),
                      BtlParam_GetSlotId(chr, 0), BtlParam_GetSlotId(chr, 1), BtlParam_GetSlotId(chr, 2), BtlParam_GetSlotId(chr, 3));
                lastChara[p] = c;
            }
        }
        if (xs[0] != '\0' && atoi(xs) > 0 && state == 3 && t - lastX > (unsigned long long)atoi(xs) * 1000000ull) {
            void *chr = BtlChar_Get(0);
            if (chr != NULL) {
                for (k = 0; k < 4; k++) { /* the next form there is */
                    int slot = (next + k) & 3, id = BtlParam_GetSlotId(chr, slot);
                    if (id != 0xFF && id >= 0) {
                        int ok = BtlCtrl_Transform(0, id);
                        logf_("vita: test: fighter 0 (character %d) told to turn into %d (form %d), %s, %u ms into the fight\n", BtlCharApi_GetChara(0), id, slot,
                              ok ? "taken" : "not now", (unsigned)((t - fightT) / 1000));
                        if (ok) {
                            next = slot + 1;
                            lastX = t;
                        } else { /* (busy: again in a second) */
                            lastX = t - (unsigned long long)(atoi(xs) - 1) * 1000000ull;
                        }
                        break;
                    }
                }
                if (k == 4) {
                    lastX = t;
                }
            }
        }
    }
    demo_heavy(state, vblanks, t); /* (bt3.txt demoult) */
#endif
    {   /* the soak's fight ended after sSoakSec seconds of fighting (state 3) */
        static unsigned long long sSoakT;
        static int sSoakDone;
        if (sSoakSec > 0 && getenv("BT3_SOAK") != NULL) {
            if (state != 3) {
                sSoakT = 0;
                sSoakDone = 0;
            } else if (sSoakT == 0) {
                sSoakT = t;
            } else if (!sSoakDone && t - sSoakT > (unsigned long long)sSoakSec * 1000000ull) {
                extern void Port_SoakEndFight(void);
                sSoakDone = 1;
                logf_("vita: soak: the fight has lasted %d s; side 1's fighter knocked out\n", sSoakSec);
                Port_SoakEndFight();
            }
        }
    }
    {   /* the sweep (sSweepSec) */
        static unsigned long long sFightT;
        static int sSweepDone, sShots;
        if (sSweepSec > 0 && sMode == 1 && !sSweepDone) {
            if (sFightT == 0 && state == 3) {
                sFightT = t;
            }
            if (sFightT != 0 && sShots < 2 && t - sFightT > (sShots == 0 ? 10000000ull : 22000000ull)) { /* pictures for the log */
                extern volatile int gVitaShotReq;
                sShots++;
                gVitaShotReq = 1;
                logf_("vita: sweep: pictures asked for (pairing %s, %d s into the fight): with the ground shadows, then without\n",
                      getenv("BT3_DEMO_PICK") != NULL ? getenv("BT3_DEMO_PICK") : "?", sShots == 1 ? 10 : 22);
            }
            if (sFightT != 0 && t - sFightT > (unsigned long long)sSweepSec * 1000000ull) {
                extern int sceAppMgrLoadExec(const char *appPath, char *const argv[], void *optParam);
                int r;
                sSweepDone = 1;
                logf_("vita: sweep: pairing %s has fought %d s; the program starts again for the next pairing\n", getenv("BT3_DEMO_PICK") != NULL ? getenv("BT3_DEMO_PICK") : "?", sSweepSec);
                log_sync();
                if (sLog != NULL) {
                    fflush(sLog);
                }
                fflush(stdout);
                fflush(stderr);
                {
                    extern void Vita_PgoDump(const char *why);
                    Vita_PgoDump("the sweep starts the program again");
                }
                r = sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
                logf_("vita: sweep: starting again failed (%08x); this pairing goes on\n", (unsigned)r);
            }
        }
    }
    if (t - sLastDraw < 500000) {
        return;
    }
#ifndef PORT_VITA_RELEASE
    if (gVitaExitAfter > 0 && t - sStart >= (unsigned long long)gVitaExitAfter * 1000000ull) {
        /* a test run's end from inside (bt3.txt exitafter=<s>): its log written whole, and a line the runners wait for
           (port/vita/tools: vita_agent_run.sh's completion) */
        logf_("vita: exitafter: the test run ends after %d s\n", gVitaExitAfter);
        quit();
    }
#endif
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
    static int sClockFix;
    if (sClockFix < 8 && (scePowerGetGpuClockFrequency() < 222 || scePowerGetArmClockFrequency() < 444 || scePowerGetBusClockFrequency() < 222)) {
        /* the clocks put back when the system lowered them (after a suspend, say): the console's own top speeds
           (eight times at most: a system that will not give them is not asked forever) */
        sClockFix++;
        logf_("vita: clocks found lowered (processor %d, bus %d, graphics chip %d MHz): set again\n", scePowerGetArmClockFrequency(),
              scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency());
        scePowerSetArmClockFrequency(444);
        scePowerSetBusClockFrequency(222);
        scePowerSetGpuClockFrequency(222);
        scePowerSetGpuXbarClockFrequency(166);
    }
    {
        static unsigned n, vb0, drawn0;
        static unsigned long long t0;
#ifdef PORT_VITA_RELEASE
        if (++n % 4 == 0 && gVitaReport) { /* (the released program: only with bt3.txt report=1) */
#else
        if (++n % 4 == 0) {
#endif
            if (t0 != 0) {
                unsigned vb = vblanks - vb0, pics = gVitaGsDrawn - drawn0, perPic = pics != 0 ? pics : 1, film[4], filmUs;
                struct mallinfo mi = mallinfo();
                if (vb == 0) {
                    vb = 1;
                }
                logf_("vita: %u s: speed %u%%, %u.%u pictures/s, late %u us/blank | per picture: %u prims (%u dropped), %u batches | %u new textures |"
                      " per blank: list %u us, uploads %u us in %u calls: %u px one by one, %u by block, %u kept | store: %u uploads, %u KB, %u sweeps |"
                      " heap %u KB\n",
                      (unsigned)((t - sStart) / 1000000), (unsigned)((unsigned long long)(vblanks - vb0) * 1000000ull * 1000ull / 5994ull * 10ull / (t - t0)),
                      (unsigned)((unsigned long long)pics * 10000000ull / (t - t0)) / 10, (unsigned)((unsigned long long)pics * 10000000ull / (t - t0)) % 10,
                      sLateSum / vb, gVglDraws / perPic, gVglDropped / perPic, gVglBatches / perPic, gVglNewTex, gVitaUsGs / vb, gVitaUsUpload / vb,
                      gVitaUp[3] / vb, gVitaUp[0] / vb, gVitaUp[1] / vb, gVitaUp[2] / vb, gVitaUpStore[0], gVitaUpStore[1] >> 10, gVitaUpStore[2],
                      (unsigned)mi.uordblks >> 10);
                {   /* the list the drawing thread is on: windows of a fixed range of lists compared build to build */
                    extern unsigned gGsFrame;
                    logf_("vita:   list now %u\n", gGsFrame);
                }
                {
                    extern unsigned gVitaAudioBlocks, gVitaAudioStarved;
                    static unsigned ab;
                    if (gVitaAudioBlocks != ab) {
                        logf_("vita:   sound: %u blocks played in this time, %u times a stream ran dry\n", gVitaAudioBlocks - ab, gVitaAudioStarved);
                        ab = gVitaAudioBlocks;
                        gVitaAudioStarved = 0;
                    }
                }
                {   /* (adxasync: the sound streams' starts) */
                    extern unsigned gVitaAdxN[6];
                    if (gVitaAdxN[0] | gVitaAdxN[3]) {
                        logf_("vita:   sound streams started: %u with the first piece read by the game's thread (%u ms in all, the longest %u ms), %u with it read by the reading thread (%u ms from the start to the stream bound, the longest %u ms)\n",
                              gVitaAdxN[0], gVitaAdxN[1] / 1000, gVitaAdxN[2] / 1000, gVitaAdxN[3], gVitaAdxN[4] / 1000, gVitaAdxN[5] / 1000);
                        memset(gVitaAdxN, 0, sizeof(gVitaAdxN));
                    }
                }
                {
                    extern unsigned gVitaUnpack[2], gVitaList[2][2];
                    logf_("vita:   lists: %u not drawn, %u us each; %u drawn, %u us each | vectors unpacked per blank: %u the short way, %u the long way\n",
                          gVitaList[0][0], gVitaList[0][1] / (gVitaList[0][0] ? gVitaList[0][0] : 1), gVitaList[1][0],
                          gVitaList[1][1] / (gVitaList[1][0] ? gVitaList[1][0] : 1), gVitaUnpack[0] / vb, gVitaUnpack[1] / vb);
                    {   /* the replayed calls and the kept parts, without the counters plugin too (with it, these come with the
                           counters below, and are reset there) */
                        extern int gVitaPerfOn;
                        if (!gVitaPerfOn) {
                            extern unsigned gVitaCc[4], gVitaDp[8], gVitaCcWhy[9], gVitaCcMax, gVitaCcNoRoom, gVitaDpBusy, gVitaCcEvicted, gVitaDpWalk[3];
                            extern void GsFE_CcCensus(unsigned out[6]);
                            unsigned cen[6];
                            GsFE_CcCensus(cen);
                            logf_("vita:   call cache: %u of %u places taken (%u new, %u read once, %u kept, %u never), %u calls found no place, %u places"
                                  " given up | model calls: %u sent from copies, %u read the long way and recorded, %u kept, %u dropped (changed)\n",
                                  cen[0], cen[5], cen[1], cen[2], cen[3], cen[4], gVitaCcNoRoom, gVitaCcEvicted, gVitaCc[0], gVitaCc[1], gVitaCc[2], gVitaCc[3]);
                            gVitaCcEvicted = 0;
                            { extern unsigned gVitaEs[6], gVitaEsGens[2]; if (gVitaEs[0] | gVitaEs[1] | gVitaEs[3] | gVitaEs[4]) { logf_("vita:   engine seam (native): %u calls sent from their copies, %u read the long way, %u kept, %u to the cache (table full), %u of an old generation, %u places taken over; generations %u of the stage, %u of the models\n", gVitaEs[0], gVitaEs[1], gVitaEs[2], gVitaEs[3], gVitaEs[4], gVitaEs[5], gVitaEsGens[0], gVitaEsGens[1]); memset(gVitaEs, 0, sizeof(gVitaEs)); } } /* (and with the processor counters below, when libperf is there) */
                            logf_("vita:   kept parts: %u drawn directly, %u walked (%u kept now, %u refused, %u found in another state), %u not recorded"
                                  " (their place another part's) | calls not kept: %u too big (largest %u KB), %u wrapped, %u empty, %u strips copied,"
                                  " %u strips not planned, %u other constants, %u other records, %u writing the constants\n",
                                  gVitaDp[0], gVitaDp[6], gVitaDp[3], gVitaDp[4], gVitaDp[5], gVitaDpBusy, gVitaCcWhy[0], gVitaCcMax >> 10, gVitaCcWhy[1],
                                  gVitaCcWhy[2], gVitaCcWhy[3], gVitaCcWhy[4], gVitaCcWhy[5], gVitaCcWhy[6], gVitaCcWhy[8]);
                            memset(gVitaCc, 0, sizeof(gVitaCc));
                            memset(gVitaDp, 0, 7 * sizeof(gVitaDp[0])); /* ([7]: the KB of indices on the chip, a total) */
                            memset(gVitaCcWhy, 0, sizeof(gVitaCcWhy));
                            gVitaCcMax = 0;
                            gVitaCcNoRoom = 0;
                            gVitaDpBusy = 0;
                            {
                                extern unsigned gVitaFeHelp[4], vgl_bt3_gxm_skipped[2];
                                logf_("vita:   GXM state calls: %u left out (they changed nothing), %u made\n", vgl_bt3_gxm_skipped[0], vgl_bt3_gxm_skipped[1]);
                                vgl_bt3_gxm_skipped[0] = vgl_bt3_gxm_skipped[1] = 0;
                                { extern int gVitaUpProf; extern unsigned gVitaUpCat[8][2], gVitaKeyProf[2]; if (gVitaUpProf) { logf_("vita:   uploads by the way they went (count / us): small same %u/%u, small written %u/%u, large same %u/%u, large by the block %u/%u, large not by the block %u/%u, left out %u/%u, film %u/%u, other %u/%u | texture lookups %u/%u\n", gVitaUpCat[0][0], gVitaUpCat[0][1], gVitaUpCat[1][0], gVitaUpCat[1][1], gVitaUpCat[2][0], gVitaUpCat[2][1], gVitaUpCat[3][0], gVitaUpCat[3][1], gVitaUpCat[4][0], gVitaUpCat[4][1], gVitaUpCat[5][0], gVitaUpCat[5][1], gVitaUpCat[6][0], gVitaUpCat[6][1], gVitaUpCat[7][0], gVitaUpCat[7][1], gVitaKeyProf[0], gVitaKeyProf[1]); memset(gVitaUpCat, 0, sizeof(gVitaUpCat)); memset(gVitaKeyProf, 0, sizeof(gVitaKeyProf)); } }
                                { extern unsigned gVitaDest[7]; if (gVitaDest[0] | gVitaDest[2] | gVitaDest[3]) { logf_("vita:   uploads' destinations (updest): %u uploads found the same there, rectangles' numbers %u kept, %u worked out; texture keys %u from a destination, %u checked, %u wrong; %u destinations found changed\n", gVitaDest[0], gVitaDest[1], gVitaDest[2], gVitaDest[3], gVitaDest[4], gVitaDest[5], gVitaDest[6]); memset(gVitaDest, 0, sizeof(gVitaDest)); } }
                                { extern unsigned gVitaPlan[7]; if (gVitaPlan[0] | gVitaPlan[1] | gVitaPlan[4] | gVitaPlan[5]) { logf_("vita:   large uploads' plans (upplan): %u used, %u made, %u checked, %u wrong; rectangles' numbers %u not worked out (another upload's data there), %u worked out all the same, %u of them found the same\n", gVitaPlan[0], gVitaPlan[1], gVitaPlan[2], gVitaPlan[3], gVitaPlan[4], gVitaPlan[5], gVitaPlan[6]); memset(gVitaPlan, 0, sizeof(gVitaPlan)); } }
                                { extern unsigned gVitaVuMax[2], gVitaVuCut; logf_("vita:   VU1 interpreter, the most in a list: %u instructions in %u runs; runs left out %u\n", gVitaVuMax[0], gVitaVuMax[1], gVitaVuCut); gVitaVuMax[0] = gVitaVuMax[1] = gVitaVuCut = 0; }
                                { extern unsigned gVitaOtMax, gVitaOtSize, gVitaDmaMax; logf_("vita:   most packet memory of the effects' table in a frame: %u of %u KB; longest display list %u KB of 1024\n", gVitaOtMax >> 10, gVitaOtSize >> 10, gVitaDmaMax >> 10); gVitaOtMax = gVitaDmaMax = 0; }
                                { extern unsigned gVitaChainMax, gVitaChainLoop, gVitaCallFull, gVitaChainBad, gVitaBigFreeSync; logf_("vita:   longest DMA chain of a list: %u tags; chains that came back to where they were %u, calls with both return places taken %u, tags or data outside the memory %u; big blocks freed after the lists were read %u\n", gVitaChainMax, gVitaChainLoop, gVitaCallFull, gVitaChainBad, gVitaBigFreeSync); gVitaChainMax = gVitaChainLoop = gVitaCallFull = gVitaChainBad = gVitaBigFreeSync = 0; }
                                { extern unsigned gVitaTexSweep[3], gVitaPalExactN[1]; if (gVitaTexSweep[0] | gVitaPalExactN[0]) { logf_("vita:   texture table full: %u sweeps, %u let go not used for a second, %u not used in this frame; %u palettes known by their colours (palexact)\n", gVitaTexSweep[0], gVitaTexSweep[1], gVitaTexSweep[2], gVitaPalExactN[0]); memset(gVitaTexSweep, 0, sizeof(gVitaTexSweep)); gVitaPalExactN[0] = 0; } }
                                { extern unsigned gVitaBind[8]; if (gVitaBind[0] | gVitaBind[1] | gVitaBind[6] | gVitaBind[7]) { logf_("vita:   textures bound by their names (texbind): %u found by the names, %u bindings made, %u checked, %u wrong (%u of them other contents); %u destinations unbound by another's write, %u lookups in no destination, %u bindings found lost\n", gVitaBind[0], gVitaBind[1], gVitaBind[2], gVitaBind[3], gVitaBind[4], gVitaBind[5], gVitaBind[6], gVitaBind[7]); memset(gVitaBind, 0, sizeof(gVitaBind)); } }
                                { extern unsigned gVitaEsDirectN[2]; if (gVitaEsDirectN[0]) { logf_("vita:   the seam's call packets the direct way (esdirect): %u\n", gVitaEsDirectN[0]); memset(gVitaEsDirectN, 0, sizeof(gVitaEsDirectN)); } }
                                { extern unsigned gVitaSeaVtxN[4]; if (gVitaSeaVtxN[0] | gVitaSeaVtxN[3]) { logf_("vita:   the surfaces' corners once (seavtx): %u triangles from them, %u corners worked out, %u found worked out, %u meshes numbered\n", gVitaSeaVtxN[0], gVitaSeaVtxN[1], gVitaSeaVtxN[2], gVitaSeaVtxN[3]); memset(gVitaSeaVtxN, 0, sizeof(gVitaSeaVtxN)); } }
                                { extern unsigned gVitaDpFastN[3]; if (gVitaDpFastN[0]) { logf_("vita:   kept parts the quick way (dpfast): %u parts\n", gVitaDpFastN[0]); memset(gVitaDpFastN, 0, sizeof(gVitaDpFastN)); } }
                                { extern unsigned gVitaDpRtN[5]; if (gVitaDpRtN[0] | gVitaDpRtN[2]) { logf_("vita:   kept parts to the render thread (dprt): %u parts per picture with their blocks worked out there (%u blocks), %u groups per picture by their place; per picture %u KB not copied, %u KB of constants and jobs added\n", gVitaDpRtN[0] / perPic, gVitaDpRtN[1] / perPic, gVitaDpRtN[2] / perPic, gVitaDpRtN[3] / perPic >> 10, gVitaDpRtN[4] / perPic >> 10); memset(gVitaDpRtN, 0, sizeof(gVitaDpRtN)); } }
                                { extern unsigned gVitaQuadRunN[2], gVitaTriAg[2]; extern unsigned gVitaTriAgWhy[8]; if (gVitaQuadRunN[0] | gVitaTriAg[0] | gVitaTriAg[1] | gVitaTriAgWhy[7] | gVitaTriAgWhy[0]) { { extern unsigned gVitaTriAgWhy[8]; logf_("vita:   the effects as runs (fxrun): %u quad runs, %u quads in them; %u triangles of the runs into the additive groups (%u runs that could; not: %u no groups, %u target, %u blend, %u depth, %u date/fba, %u pad, %u tex moved, %u alpha)\n", gVitaQuadRunN[0], gVitaQuadRunN[1], gVitaTriAg[0], gVitaTriAg[1], gVitaTriAgWhy[0], gVitaTriAgWhy[1], gVitaTriAgWhy[2], gVitaTriAgWhy[3], gVitaTriAgWhy[4], gVitaTriAgWhy[5], gVitaTriAgWhy[6], gVitaTriAgWhy[7]); memset(gVitaTriAgWhy, 0, sizeof(gVitaTriAgWhy)); } memset(gVitaQuadRunN, 0, sizeof(gVitaQuadRunN)); memset(gVitaTriAg, 0, sizeof(gVitaTriAg)); } }
                                { extern unsigned gVitaTexId[5]; if (gVitaTexId[0] | gVitaTexId[3] | gVitaTexId[4]) { logf_("vita:   textures named by the game (nativetex): %u uploads' numbers from their names, %u checked by their bytes, %u found changed, %u named first or anew, %u read by their bytes (found changed before)\n", gVitaTexId[0], gVitaTexId[1], gVitaTexId[2], gVitaTexId[3], gVitaTexId[4]); memset(gVitaTexId, 0, sizeof(gVitaTexId)); } }
                                logf_("vita:   uploads' numbers on the fourth core: %u worked out (%u ms), %u left to the drawing thread (too many waiting), %u dropped (late)\n",
                                      gVitaFeHelp[0], gVitaFeHelp[1] / 1000, gVitaFeHelp[2], gVitaFeHelp[3]);
                                memset(gVitaFeHelp, 0, sizeof(gVitaFeHelp));
                            }
                            {   /* (and which GS field the kept parts found in another state differed in first, the three most) */
                                extern unsigned gVitaDpMiss[34];
                                unsigned best[3] = {0, 0, 0}, bi[3] = {0, 0, 0}, i2, j2, k2;
                                for (i2 = 0; i2 < 34; i2++) {
                                    for (j2 = 0; j2 < 3; j2++) {
                                        if (gVitaDpMiss[i2] > best[j2]) {
                                            for (k2 = 2; k2 > j2; k2--) {
                                                best[k2] = best[k2 - 1];
                                                bi[k2] = bi[k2 - 1];
                                            }
                                            best[j2] = gVitaDpMiss[i2];
                                            bi[j2] = i2;
                                            break;
                                        }
                                    }
                                }
                                extern unsigned gVitaDpRefWhy[4], gVitaDpBadWhy[4];
                                logf_("vita:   kept parts walked: %u not to be recorded (off, or their indices' room full), %u refused before; %u refused now as changing"
                                      " | found in another state, by field: %u x %u, %u x %u, %u x %u | not kept: %u vertices being written, %u a strip that cannot be;"
                                      " %u a GS primitive in it, %u an image upload, %u a register, %u another reason\n",
                                      gVitaDpWalk[0], gVitaDpWalk[1], gVitaDpWalk[2], bi[0], best[0], bi[1], best[1], bi[2], best[2], gVitaDpRefWhy[0], gVitaDpRefWhy[1],
                                      gVitaDpBadWhy[0], gVitaDpBadWhy[1], gVitaDpBadWhy[2], gVitaDpBadWhy[3]);
                                {   extern unsigned gVitaDpEndWhy[8];
                                    if (gVitaDpBadWhy[3] != 0) {
                                        logf_("vita:   kept parts not kept for another reason: %u nothing recorded, %u a strip not recordable, %u over 64 groups, %u indices beyond a bank,"
                                              " %u the indices' room full, %u no memory, %u too many uniform blocks, %u a GIF loop open\n",
                                              gVitaDpEndWhy[0], gVitaDpEndWhy[1], gVitaDpEndWhy[2], gVitaDpEndWhy[3], gVitaDpEndWhy[4], gVitaDpEndWhy[5], gVitaDpEndWhy[6], gVitaDpEndWhy[7]);
                                    }
                                    memset(gVitaDpEndWhy, 0, sizeof(gVitaDpEndWhy));
                                }
                                memset(gVitaDpRefWhy, 0, sizeof(gVitaDpRefWhy));
                                memset(gVitaDpBadWhy, 0, sizeof(gVitaDpBadWhy));
                                if (!gVitaPerfOn) {
                                    memset(gVitaDpMiss, 0, sizeof(gVitaDpMiss));
                                }
                            }
                            memset(gVitaDpWalk, 0, sizeof(gVitaDpWalk));
                        }
                    }
                    {
                        extern unsigned gVglUs[6], gVglCalls[6];
                        logf_("vita:   per picture, us (calls): vertex-program calls %u (%u), of them strips %u (%u); state %u (%u); GS primitives %u (%u); to the chip %u (%u); swap %u (%u)\n",
                              gVglUs[5] / perPic, gVglCalls[5] / perPic, gVglUs[0] / perPic, gVglCalls[0] / perPic, gVglUs[1] / perPic, gVglCalls[1] / perPic,
                              gVglUs[4] / perPic, gVglCalls[4] / perPic, gVglUs[2] / perPic, gVglCalls[2] / perPic, gVglUs[3] / perPic, gVglCalls[3] / perPic);
                        memset(gVglUs, 0, sizeof(gVglUs));
                        memset(gVglCalls, 0, sizeof(gVglCalls));
                    }
                    {
                        extern unsigned gProfHist[2][16], gProfGameHist[3], gVitaUnpackSkipped, gVitaWaitSkips, gVitaSnap[2];
                        static const char *const kName[13] = {"idle", "list", "unpack", "GS packets", "uploads", "program calls", "strips", "to the chip", "swap", "GS primitives",
                                                              "chip strips", "kept drawn", "kept walked"};
                        char line[700];
                        int n = 0, k;
                        for (k = 0; k < 13; k++) {
                            n += snprintf(line + n, sizeof(line) - (size_t)n, "%s %u/%u%s", kName[k], gProfHist[0][k], gProfHist[1][k], k < 12 ? ", " : "");
                        }
                        logf_("vita:   picture thread, ms in frames not drawn / drawn: %s | %u vectors per blank not unpacked\n", line, gVitaUnpackSkipped / vb);
                        {   /* (the list reader's and the render thread's own, counted apart since: the line above had them mixed in) */
                            extern unsigned gProfHistFe[12], gProfHistRt[12];
                            logf_("vita:   list reader, ms: idle %u, list %u, unpack %u, GS packets %u, program calls %u, other %u | render thread, ms: idle %u, to the chip %u\n",
                                  gProfHistFe[0], gProfHistFe[1], gProfHistFe[2], gProfHistFe[3], gProfHistFe[5],
                                  gProfHistFe[4] + gProfHistFe[6] + gProfHistFe[7] + gProfHistFe[8] + gProfHistFe[9] + gProfHistFe[10] + gProfHistFe[11], gProfHistRt[0], gProfHistRt[7]);
                            memset(gProfHistFe, 0, sizeof(gProfHistFe));
                            memset(gProfHistRt, 0, sizeof(gProfHistRt));
                        }
                        extern unsigned gVitaSnapSame2[2];
                        logf_("vita:   game thread, ms: running %u, waiting for the blank %u, waiting for the picture thread %u | %u lists not drawn for that wait | %u pieces (%u bytes) copied for the lists"
                              " | images read in place as unchanged %u, copied %u\n",
                              gProfGameHist[0], gProfGameHist[1], gProfGameHist[2], gVitaWaitSkips, gVitaSnap[0], gVitaSnap[1], gVitaSnapSame2[0], gVitaSnapSame2[1]);
                        gVitaSnapSame2[0] = gVitaSnapSame2[1] = 0;
                        {
                            extern unsigned gVitaSnapImg[4], gVitaSnapTags[2];
                            logf_("vita:   snap_make: images summed at the list's level %u the same, %u changed; inside a call %u the same, %u changed; tags walked at the list's level %u, inside a call %u\n",
                                  gVitaSnapImg[0], gVitaSnapImg[1], gVitaSnapImg[2], gVitaSnapImg[3], gVitaSnapTags[0], gVitaSnapTags[1]);
                            memset(gVitaSnapImg, 0, sizeof(gVitaSnapImg));
                            {
                                extern int gVitaSnapCut;
                                extern unsigned gVitaSnapCutN[4];
                                if (gVitaSnapCut != 0) {
                                    logf_("vita:   snapcut: images' sums not made %u (check: %u of them changed), calls not walked %u, images inside calls %u\n",
                                          gVitaSnapCutN[0], gVitaSnapCutN[1], gVitaSnapCutN[2], gVitaSnapCutN[3]);
                                    memset(gVitaSnapCutN, 0, sizeof(gVitaSnapCutN));
                                }
                            }
                            gVitaSnapTags[0] = gVitaSnapTags[1] = 0;
                        }
                        {
                            extern int gVitaGZOn;
                            extern unsigned long long gVitaGZ[64];
                            extern unsigned gVitaGZSteps;
                            if (gVitaGZOn && gVitaGZSteps != 0) {
                                static const char *const gzn[64] = {"", "jobs", "frame start", "scripts", "pads, pairs", "sound", "rules", "computer players", "",
                                    "input sampled", "input", "fighters", "scene", "effects", "scene after", "stage", "cameras", "", "objects", "", "game update",
                                    "stage models", "stage fx before", "fighters drawn", "scene drawn", "ordering table", "stage fx after", "HUD", "text", "result",
                                    "frame end", "list to the copies", "list handed over", "snap_make (walk, end)", "snap: image sums", "snap: copies", "snap: walk", "", "", "loop",
                                    "fighters: motion", "fighters: step 6", "fighters: step 7", "fighters: step 8", "fighters: step 9", "fighters: camera"};
                                char gl[900];
                                int len = 0, z;
                                unsigned long long all = 0;
                                for (z = 1; z < 64; z++) {
                                    all += gVitaGZ[z];
                                }
                                for (z = 1; z < 64 && len < (int)sizeof(gl) - 40; z++) {
                                    unsigned us = (unsigned)(gVitaGZ[z] / 444u / gVitaGZSteps);
                                    if (gzn[z] != NULL && gzn[z][0] != 0 && us >= 50) {
                                        len += snprintf(gl + len, sizeof(gl) - (size_t)len, " %s %u.%u;", gzn[z], us / 1000, us / 100 % 10);
                                    }
                                }
                                logf_("vita:   game thread by part, ms a step (%u steps, %u.%u ms a step in all):%s\n", gVitaGZSteps,
                                      (unsigned)(all / 444u / gVitaGZSteps / 1000), (unsigned)(all / 444u / gVitaGZSteps / 100 % 10), gl);
                                memset(gVitaGZ, 0, sizeof(gVitaGZ));
                                gVitaGZSteps = 0;
                            }
                        }
                        if (gVitaWarmWaits) {
                            logf_("vita:   the intro: the game waited for a picture %u times instead of leaving one out\n", gVitaWarmWaits);
                            gVitaWarmWaits = 0;
                        }
                        watch_log();
                        {
                            extern unsigned gVitaFileReads, gVitaFileReadsGame;
                            extern unsigned long long gVitaFileUs, gVitaFileUsGame;
                            logf_("vita:   file reads: %u taking %u ms (on the game's thread: %u taking %u ms)\n", gVitaFileReads, (unsigned)(gVitaFileUs / 1000), gVitaFileReadsGame, (unsigned)(gVitaFileUsGame / 1000));
                            gVitaFileReads = gVitaFileReadsGame = 0;
                            gVitaFileUs = gVitaFileUsGame = 0;
                        }
                        {
                            extern unsigned gProfWorkHist, gVitaVj[4], gVitaLz[3];
                            logf_("vita:   strip thread: busy %u ms; per picture: %u strips given to it (%u read in the list, %u copied), waited for %u times | vectors per blank left in the list %u, copied after all %u\n",
                                  gProfWorkHist, gVitaVj[0] / perPic, gVitaVj[1] / perPic, gVitaVj[2] / perPic, gVitaVj[3] / perPic, gVitaLz[0] / vb, gVitaLz[1] / vb);
                            gProfWorkHist = 0;
                            memset(gVitaVj, 0, sizeof(gVitaVj));
                            memset(gVitaLz, 0, sizeof(gVitaLz));
                        }
                        {
                            extern unsigned gVitaRef[5][2];
                            unsigned nl = gVitaList[0][0] + gVitaList[1][0];
                            nl = nl ? nl : 1;
                            logf_("vita:   data the lists refer to, per list, pieces (KB): up to 32 quadwords %u (%u), 128: %u (%u), 512: %u (%u), 2048: %u (%u), more: %u (%u)\n",
                                  gVitaRef[0][0] / nl, gVitaRef[0][1] / 64 / nl, gVitaRef[1][0] / nl, gVitaRef[1][1] / 64 / nl, gVitaRef[2][0] / nl, gVitaRef[2][1] / 64 / nl,
                                  gVitaRef[3][0] / nl, gVitaRef[3][1] / 64 / nl, gVitaRef[4][0] / nl, gVitaRef[4][1] / 64 / nl);
                            {
                                extern unsigned gVitaFresh;
                                { extern unsigned gVitaStage[3]; logf_("vita:   stage on the graphics chip: %u strips per picture drawn from it, %u strips written to it, %u vertices kept there\n", gVitaStage[0] / perPic, gVitaStage[1], gVitaStage[2]); gVitaStage[0] = gVitaStage[1] = 0; }
                                { extern unsigned gGrIdleUs; logf_("vita:   drawing thread waited for the list reader %u ms in drawn lists\n", gGrIdleUs / 1000); gGrIdleUs = 0; }
                                { extern unsigned long long gVitaTexMakeUs; extern unsigned gVitaBeList[3];
                                  extern unsigned long long gVitaTexReadUs, gVitaTexPx; extern float gVglCov[2]; extern uint32_t vgl_flip_hist[5];
                                  extern unsigned gVitaSwz, gVitaPal[3], gVitaPaints, gVitaSigCheck[4];
                                  logf_("vita:   page-number check: texture keys %u with pages unchanged, %u of them made otherwise; states %u, %u otherwise\n", gVitaSigCheck[0], gVitaSigCheck[1], gVitaSigCheck[2], gVitaSigCheck[3]);
                                  memset(gVitaSigCheck, 0, sizeof(gVitaSigCheck));
                                  logf_("vita:   sprites drawn into palette pages (GS memory): %u\n", gVitaPaints);
                                  gVitaPaints = 0;
                                  logf_("vita:   drawing thread: longest drawn list %u us, %u over 30 ms, %u over 33 ms | new textures took %u ms (reading GS memory %u ms, %u k pixels, %u swizzled; %u with a palette: %u index sets, %u palettes made)\n", gVitaBeList[0], gVitaBeList[1], gVitaBeList[2],
                                        (unsigned)(gVitaTexMakeUs / 1000), (unsigned)(gVitaTexReadUs / 1000), (unsigned)(gVitaTexPx / 1000), gVitaSwz, gVitaPal[2], gVitaPal[0], gVitaPal[1]);
                                  gVitaSwz = 0; gVitaPal[0] = gVitaPal[1] = gVitaPal[2] = 0;
                                  { extern void Vita_TexTopLog(void); Vita_TexTopLog(); }
                                  { extern void Vita_T121Log(void); Vita_T121Log(); }
                                  { extern void Vita_PaceLog(void); extern void Vita_HitchLog(void); Vita_PaceLog(); Vita_HitchLog(); }
                                  { extern void Vita_AmLog(void); Vita_AmLog(); }
                                  { extern void Vita_FastLog(void); Vita_FastLog(); }
                                  logf_("vita:   GS primitives cover %u.%02u screens opaque, %u.%02u blended per picture | pictures on the screen for 0 / 1 / 2 / 3 / 4+ blanks: %u %u %u %u %u\n",
                                        (unsigned)(gVglCov[0] / perPic / (512.0f * 448.0f)), (unsigned)(gVglCov[0] / perPic / (512.0f * 448.0f) * 100.0f) % 100,
                                        (unsigned)(gVglCov[1] / perPic / (512.0f * 448.0f)), (unsigned)(gVglCov[1] / perPic / (512.0f * 448.0f) * 100.0f) % 100,
                                        vgl_flip_hist[0], vgl_flip_hist[1], vgl_flip_hist[2], vgl_flip_hist[3], vgl_flip_hist[4]);
                                  { extern unsigned gVitaRej[2][8];
                                    logf_("vita:   not drawn (16-bit or depth target / work buffer target / depth as texture / picture as texture / drawn-into buffer as texture / no scissor / other): GS primitives %u %u %u %u %u %u %u, fighter strips %u %u %u %u %u %u %u\n",
                                          gVitaRej[0][1], gVitaRej[0][2], gVitaRej[0][3], gVitaRej[0][4], gVitaRej[0][5], gVitaRej[0][6], gVitaRej[0][0] + gVitaRej[0][7],
                                          gVitaRej[1][1], gVitaRej[1][2], gVitaRej[1][3], gVitaRej[1][4], gVitaRej[1][5], gVitaRej[1][6], gVitaRej[1][0] + gVitaRej[1][7]);
                                    memset(gVitaRej, 0, sizeof(gVitaRej)); }
                                  { extern unsigned gVglSkip[4], gVglScissorN; extern float gVglSkipArea[2];
                                    logf_("vita:   GS primitives left out as they change nothing: %u without alpha (%u.%02u screens per picture), %u writing only the alpha nothing reads or blended with an alpha of 0 (%u.%02u screens per picture); %u read the picture's alpha (%u model draws) | %u scissor changes per picture\n",
                                          gVglSkip[0], (unsigned)(gVglSkipArea[0] / perPic / (512.0f * 448.0f)), (unsigned)(gVglSkipArea[0] / perPic / (512.0f * 448.0f) * 100.0f) % 100,
                                          gVglSkip[1], (unsigned)(gVglSkipArea[1] / perPic / (512.0f * 448.0f)), (unsigned)(gVglSkipArea[1] / perPic / (512.0f * 448.0f) * 100.0f) % 100, gVglSkip[2], gVglSkip[3], gVglScissorN / perPic);
                                    memset(gVglSkip, 0, sizeof(gVglSkip)); gVglSkipArea[0] = gVglSkipArea[1] = 0.0f; gVglScissorN = 0;
                                    { extern unsigned gVitaAlDead[2]; logf_("vita:   of them after the picture's last read of its alpha: %u, %u.%02u screens per picture\n", gVitaAlDead[0], gVitaAlDead[1] / perPic / 100, gVitaAlDead[1] / perPic % 100); gVitaAlDead[0] = gVitaAlDead[1] = 0; } }
                                  memset(gVitaBeList, 0, sizeof(gVitaBeList)); gVitaTexMakeUs = gVitaTexReadUs = gVitaTexPx = 0; gVglCov[0] = gVglCov[1] = 0.0f; memset(vgl_flip_hist, 0, sizeof(vgl_flip_hist)); }
                                { extern unsigned long long gVitaFeUs[2][2], gGrRoomUs; logf_("vita:   list reader: %u us per drawn list, %u per list not drawn; %u ms waiting for room in the drawing thread's queue\n", (unsigned)(gVitaFeUs[1][1] / (gVitaFeUs[1][0] ? gVitaFeUs[1][0] : 1)), (unsigned)(gVitaFeUs[0][1] / (gVitaFeUs[0][0] ? gVitaFeUs[0][0] : 1)), (unsigned)(gGrRoomUs / 1000)); memset(gVitaFeUs, 0, sizeof(gVitaFeUs)); gGrRoomUs = 0; }
                                { extern unsigned gVitaGpuWait[2]; if (gVitaGpuWait[1]) { logf_("vita:   graphics chip still busy at the swap: %u us on average (%u times)\n", gVitaGpuWait[0] / gVitaGpuWait[1], gVitaGpuWait[1]); } gVitaGpuWait[0] = gVitaGpuWait[1] = 0; }
                                { extern unsigned gVitaFt[3]; logf_("vita:   fighters on the graphics chip: %u strips per picture drawn from it, %u strips written to it, %u vertices kept there\n", gVitaFt[0] / perPic, gVitaFt[1], gVitaFt[2]); gVitaFt[0] = gVitaFt[1] = 0; }
                            { extern unsigned gVitaUpSkipped; logf_("vita:   %u uploads not looked at in frames that are not drawn\n", gVitaUpSkipped); gVitaUpSkipped = 0; }
                            logf_("vita:   %u lists referred to something new and were finished before the game went on\n", gVitaFresh);
                                gVitaFresh = 0;
                            }
                            memset(gVitaRef, 0, sizeof(gVitaRef));
                        }
                        {
                            extern int gVitaPerfOn;
                            extern void Vita_PerfAccum(uint32_t last[5], uint64_t sum[5]);
                            extern uint64_t gVitaPerfList[2][5], gVitaPerfWork[5];
                            extern unsigned gVitaPerfLists[2];
                            static uint32_t gameLast[5];
                            static uint64_t game[5];
                            extern int gVitaZoneT;
                            if (gVitaPerfOn || gVitaZoneT) { /* (zones=1 without the plugin: the zones only, some lines twice) */
                                const uint64_t *t[4] = {game, gVitaPerfList[0], gVitaPerfList[1], gVitaPerfWork};
                                static const char *const nm[4] = {"game thread, per frame", "picture thread, per list not drawn", "picture thread, per list drawn", "strip thread, per drawn picture"};
                                unsigned div[4], k;
                                memset(game, 0, sizeof(game));
                                Vita_PerfAccum(gameLast, game);
                                div[0] = gVitaPerfLists[0] + gVitaPerfLists[1];
                                div[1] = gVitaPerfLists[0];
                                div[2] = gVitaPerfLists[1];
                                div[3] = gVitaPerfLists[1];
                                for (k = 0; k < (gVitaPerfOn ? 4u : 0u); k++) {
                                    const uint64_t *c = t[k];
                                    unsigned d = div[k] ? div[k] : 1;
                                    unsigned ki = (unsigned)(c[3] / 1000) ? (unsigned)(c[3] / 1000) : 1;
                                    logf_("vita:   counters, %s: %u k cycles, %u k instructions (%u.%02u per cycle), data cache misses %u, instruction cache misses %u, mispredicted branches %u (per 1000 instructions: %u, %u, %u)\n",
                                          nm[k], (unsigned)(c[0] / d / 1000), (unsigned)(c[3] / d / 1000), (unsigned)(c[0] ? c[3] * 100 / c[0] / 100 : 0), (unsigned)(c[0] ? c[3] * 100 / c[0] % 100 : 0),
                                          (unsigned)(c[1] / d), (unsigned)(c[2] / d), (unsigned)(c[4] / d), (unsigned)(c[1] / ki), (unsigned)(c[2] / ki), (unsigned)(c[4] / ki));
                                }
                                {
                                    extern uint64_t gVitaRegion[12][5];
                                    extern unsigned gVitaRegionLists;
                                    static const char *const rn[11] = {"(end)", "list", "unpack", "GS packets", "uploads", "program calls", "strips", "to the chip", "swap", "GS primitives", "kept on chip"};
                                    if (gVitaRegionLists) {
                                        unsigned r, nl = gVitaRegionLists;
                                        for (r = 1; r < 11; r++) {
                                            const uint64_t *c = gVitaRegion[r];
                                            logf_("vita:   counters, drawn list by part: %-13s %6u k cycles, %6u k instructions, %5u mispredicted, %5u data misses, %5u times\n", rn[r],
                                                  (unsigned)(c[0] / nl / 1000), (unsigned)(c[1] / nl / 1000), (unsigned)(c[2] / nl), (unsigned)(c[3] / nl), (unsigned)(c[4] / nl));
                                        }
                                        logf_("vita:   counters, one switch of part costs %u cycles, %u instructions (included in the lines above)\n",
                                              (unsigned)(gVitaRegion[11][0] / (gVitaRegion[11][4] ? gVitaRegion[11][4] : 1)), (unsigned)(gVitaRegion[11][1] / (gVitaRegion[11][4] ? gVitaRegion[11][4] : 1)));
                                        memset(gVitaRegion, 0, sizeof(gVitaRegion));
                                        gVitaRegionLists = 0;
                                    }
                                }
                                {
                                    extern uint64_t gVitaZone[56];
                                    extern unsigned gVitaZoneN[56], gVitaZoneLists;
                                    static const char *const zn[31] = {"rest of the list", "kept part drawn directly", "frame end (draws, swap)",
                                        "list end", "consts built, to strip", "strip: state", "strip: flush", "GIF: image upload",
                                        "GIF: up to a register", "GIF: A+D register write", "GS prim: parse to draw", "GS prim: strips flushed", "upload: found unchanged", "GS prim: to state",
                                        "GS prim: state", "GS prim: to flush", "upload: bytes looked at", "fighter: to DrawVu0", "GS prim: vertices (b205)", "record: ring/loop", "record: GIF", "record: REG", "record: VU consts", "record: draw tail", "back end waiting", "chip: fighter upload", "chip: fighter batch", "chip: stage upload", "chip: stage batch", "draw: to chip_emit", "GS prim: flush (draw call)"};
                                    if (gVitaZoneLists) {
                                        unsigned z, nl = gVitaZoneLists;
                                        for (z = 0; z < 31; z++) {
                                            if (gVitaZoneN[z]) {
                                                logf_("vita:   zone %2u %-26s %7u k cycles per drawn list, %6u marks, %4u cycles each\n", z, zn[z],
                                                      (unsigned)(gVitaZone[z] / nl / 1000), gVitaZoneN[z] / nl, (unsigned)(gVitaZone[z] / gVitaZoneN[z]));
                                            }
                                        }
                                        {   /* the parts of a kept part's draw and of a batch's (zones 32..) */
                                            static const char *const zx[16] = {"kept part: matched", "kept part: GS prims before", "kept part: uniform blocks",
                                                "kept part: groups, textures", "flush: program, texture", "flush: fixed state", "flush: vertices to the chip",
                                                "flush: the draw call", "upload: small, pixels (b216)", "upload: small, pages (b216)", "upload: small, rect (b216)",
                                                "kept parts drawn: draws", "upload: small, compared", "upload: large, compared", "upload: written", "GS prim: state_of (b205)"};
                                            for (z = 32; z < 48; z++) {
                                                if (gVitaZoneN[z]) {
                                                    logf_("vita:   zone %2u %-26s %7u k cycles per drawn list, %6u marks, %4u cycles each\n", z, zx[z - 32],
                                                          (unsigned)(gVitaZone[z] / nl / 1000), gVitaZoneN[z] / nl, (unsigned)(gVitaZone[z] / gVitaZoneN[z]));
                                                }
                                            }
                                        }
                                        {   /* the parts of a state's lookup (zones 48..50, inside the zones of its caller: 5, 14, 47); 51 a kept
                                               group's texture lookup (inside zone 35's loop: 35 is then the rest of it, its marks one per group) */
                                            static const char *const zs[4] = {"state: registers, the way", "state: the targets' way", "state: the table's way", "kept part: group texture"};
                                            for (z = 48; z < 52; z++) {
                                                if (gVitaZoneN[z]) {
                                                    logf_("vita:   zone %2u %-26s %7u k cycles per drawn list, %6u marks, %4u cycles each\n", z, zs[z - 48],
                                                          (unsigned)(gVitaZone[z] / nl / 1000), gVitaZoneN[z] / nl, (unsigned)(gVitaZone[z] / gVitaZoneN[z]));
                                                }
                                            }
                                        }
                                        {   /* (zones 52..55: the later marks) */
                                            static const char *const zl[4] = {"GS prim: kept parts handed", "kept part: to its constants", "", ""};
                                            for (z = 52; z < 56; z++) {
                                                if (gVitaZoneN[z]) {
                                                    logf_("vita:   zone %2u %-26s %7u k cycles per drawn list, %6u marks, %4u cycles each\n", z, zl[z - 52],
                                                          (unsigned)(gVitaZone[z] / nl / 1000), gVitaZoneN[z] / nl, (unsigned)(gVitaZone[z] / gVitaZoneN[z]));
                                                }
                                            }
                                        }
                                        logf_("vita:   zone mark costs %u cycles\n", (unsigned)(gVitaZone[31] / (gVitaZoneN[31] ? gVitaZoneN[31] : 1)));
                                        {   /* */
                                            extern unsigned gVitaStOf[6];
                                            extern unsigned gVitaFl[3];
                                            {
                                                extern unsigned long long gVitaGrCyc[16];
                                                extern unsigned gVitaGrN[16];
                                                extern unsigned gVitaZoneLists;
                                                static const char *const gn[16] = {"wrap", "GIF in place", "GIF copied", "register", "strip", "skip", "FBP", "frame end", "list begin",
                                                                                     "list end", "init", "GIF channel", "chip reset", "kept part", "strip pair", "t116"};
                                                char line[700];
                                                int len = 0, t;
                                                unsigned nl = gVitaZoneLists ? gVitaZoneLists : 1;
                                                for (t = 0; t < 16; t++) {
                                                    if (gVitaGrN[t] != 0 && len < (int)sizeof(line) - 60) {
                                                        len += snprintf(line + len, sizeof(line) - (unsigned)len, " %s %llu k (%u)", gn[t], gVitaGrCyc[t] / nl / 1000, gVitaGrN[t] / nl);
                                                    }
                                                }
                                                logf_("vita:   drawing thread by record, cycles per measured list (records):%s\n", line);
                                                {
                                                    extern unsigned long long gVitaGifCyc[2], gVitaDrawCyc[8];
                                                    extern unsigned gVitaGifCnt[2], gVitaDrawCnt[8];
                                                    logf_("vita:   of the GIF records, per measured list: triangle runs %llu k (%u triangles), images %llu k (%u); GS primitives the GS's way: "
                                                          "sprites %llu k (%u), triangles %llu k (%u), strips %llu k (%u), fans %llu k (%u), lines %llu k (%u)\n",
                                                          gVitaGifCyc[0] / nl / 1000, gVitaGifCnt[0] / nl, gVitaGifCyc[1] / nl / 1000, gVitaGifCnt[1] / nl,
                                                          gVitaDrawCyc[6] / nl / 1000, gVitaDrawCnt[6] / nl, gVitaDrawCyc[3] / nl / 1000, gVitaDrawCnt[3] / nl,
                                                          gVitaDrawCyc[4] / nl / 1000, gVitaDrawCnt[4] / nl, gVitaDrawCyc[5] / nl / 1000, gVitaDrawCnt[5] / nl,
                                                          gVitaDrawCyc[1] / nl / 1000, gVitaDrawCnt[1] / nl);
                                                    memset(gVitaGifCyc, 0, sizeof(gVitaGifCyc));
                                                    memset(gVitaGifCnt, 0, sizeof(gVitaGifCnt));
                                                    memset(gVitaDrawCyc, 0, sizeof(gVitaDrawCyc));
                                                    memset(gVitaDrawCnt, 0, sizeof(gVitaDrawCnt));
                                                }
                                                memset(gVitaGrCyc, 0, sizeof(gVitaGrCyc));
                                                memset(gVitaGrN, 0, sizeof(gVitaGrN));
                                            }
                                            {
                                                extern unsigned gVitaKeyPart[3], gVitaFlm[2];
                                                extern unsigned gVitaStLast;
                                                logf_("vita:   texture keys: %u with the texture's number kept, %u checked, %u of them found otherwise; quick states by PRIM and TEX0: %u taken, %u put; states as the call before's %u\n",
                                                      gVitaKeyPart[0], gVitaKeyPart[1], gVitaKeyPart[2], gVitaFlm[0], gVitaFlm[1], gVitaStLast);
                                                gVitaStLast = 0;
                                                memset(gVitaKeyPart, 0, sizeof(gVitaKeyPart));
                                                memset(gVitaFlm, 0, sizeof(gVitaFlm));
                                            }
                                            extern unsigned gVitaStOfEvict;
                                            logf_("vita:   GS primitives' states: %u the work targets' way, %u the page's way, %u found, %u found (pages unchanged), %u worked out as a check, %u worked out (%u of them pushed out by another of the picture)\n",
                                                  gVitaStOf[0], gVitaStOf[1], gVitaStOf[2], gVitaStOf[3], gVitaStOf[4], gVitaStOf[5], gVitaStOfEvict);
                                            gVitaStOfEvict = 0;
            {
                extern unsigned gVitaStX[3];
                logf_("vita:   states kept from a picture before: %u, worked out again as a check %u, %u of them found otherwise\n", gVitaStX[0], gVitaStX[1], gVitaStX[2]);
                {
                    extern unsigned gVitaStWhy[16], Vita_StGen(void);
                    static unsigned genWas;
                    unsigned gen = Vita_StGen();
                    logf_("vita:   states worked out, why their place did not give them: empty %u, the table's generation changed %u (%u new generations), another state of the picture there %u, "
                          "another of a picture before %u, the texture's pages changed %u, kept but its texture gone or checked %u; kept for their picture only: not the picture %u, into a target %u, "
                          "texture moved %u, pad %u, texture not the table's %u, the last palette %u, no texture %u, other %u\n",
                          gVitaStWhy[0], gVitaStWhy[1], gen - genWas, gVitaStWhy[2], gVitaStWhy[3], gVitaStWhy[4], gVitaStWhy[5], gVitaStWhy[9], gVitaStWhy[10], gVitaStWhy[11],
                          gVitaStWhy[12], gVitaStWhy[13], gVitaStWhy[14], gVitaStWhy[15], gVitaStWhy[8]);
                    genWas = gen;
                    memset(gVitaStWhy, 0, sizeof(gVitaStWhy));
                }
                memset(gVitaStX, 0, sizeof(gVitaStX));
                {
                    extern unsigned gVitaWtC[3];
                    extern unsigned gVitaRunEstN;
                    extern unsigned gVitaWtNoRem[8];
                    logf_("vita:   the work targets' way: %u states remembered taken, %u worked out, %u remembered (not: %u refused, %u no texture transform, %u no texture, "
                          "%u not a picture as texture, %u no picture yet, %u no target found, %u the target's texture another); runs' first triangles the quick way %u\n",
                          gVitaWtC[0], gVitaWtC[1], gVitaWtC[2], gVitaWtNoRem[0], gVitaWtNoRem[1], gVitaWtNoRem[2], gVitaWtNoRem[3], gVitaWtNoRem[4],
                          gVitaWtNoRem[5], gVitaWtNoRem[6], gVitaRunEstN);
                    memset(gVitaWtNoRem, 0, sizeof(gVitaWtNoRem));
                    {
                        extern unsigned gVitaVuSt[3][2];
                        logf_("vita:   strips' states (program 0 / 4 / 6): the usual way %u / %u / %u, the work targets' way %u / %u / %u\n",
                              gVitaVuSt[0][0], gVitaVuSt[1][0], gVitaVuSt[2][0], gVitaVuSt[0][1], gVitaVuSt[1][1], gVitaVuSt[2][1]);
                        memset(gVitaVuSt, 0, sizeof(gVitaVuSt));
                    }
                    {
                        extern unsigned gVitaMemLow[3];
                        if (gVitaMemLow[0] != 0xFFFFFFFFu) { /* (read every 1024th picture in a fight: most stretches have none) */
                            logf_("vita:   memory free, the lowest since the last report: %u KB video, %u KB main (the longest reading %u us)\n", gVitaMemLow[0], gVitaMemLow[1], gVitaMemLow[2]);
                        }
                        gVitaMemLow[0] = gVitaMemLow[1] = 0xFFFFFFFFu;
                        gVitaMemLow[2] = 0;
                    }
                    {
                        extern int gVitaWtRem, gVitaStL4;
                        extern unsigned gVitaWtRemN[3], gVitaStL4N[3];
                        if (gVitaWtRem != 0 || gVitaStL4 != 0) {
                            logf_("vita:   wtrem: %u states without a transform taken (check: %u, %u differed); stl4: %u states of an earlier call taken (check: %u, %u differed)\n",
                                  gVitaWtRemN[0], gVitaWtRemN[1], gVitaWtRemN[2], gVitaStL4N[0], gVitaStL4N[1], gVitaStL4N[2]);
                        }
                        memset(gVitaWtRemN, 0, sizeof(gVitaWtRemN));
                        memset(gVitaStL4N, 0, sizeof(gVitaStL4N));
                    }
                    {
                        extern int gVitaTexCont;
                        extern unsigned gVitaTexContN[3];
                        if (gVitaTexCont != 0) {
                            logf_("vita:   texcont: %u textures found at another place than they were made at; %u made, %u looked up the long way\n",
                                  gVitaTexContN[0], gVitaTexContN[1], gVitaTexContN[2]);
                        }
                        memset(gVitaTexContN, 0, sizeof(gVitaTexContN));
                    }
                    {
                        extern unsigned gVitaDpBusyL[2], gVitaSilGateN[2], gVitaSkipCcN[1], gVitaWtSigN[2], gVitaRtKickN[1];
                        logf_("vita:   kept parts not recorded (their place another's in use): %u; given their second place (dpways): %u | shadow-page strips held back (silgate): %u, gone through %u\n",
                              gVitaDpBusyL[0], gVitaDpBusyL[1], gVitaSilGateN[0], gVitaSilGateN[1]);
                        logf_("vita:   kept calls sent as their registers in lists not drawn (skipcc): %u | targets' signatures taken as they were (wtsig): %u, worked out %u | kept parts handed over without waking the render thread (rtkick): %u\n",
                              gVitaSkipCcN[0], gVitaWtSigN[0], gVitaWtSigN[1], gVitaRtKickN[0]);
                        {
                            extern unsigned gVitaDpTexaN[2], gVitaTexRtN[3], gVitaFxClampN[2];
                            extern void Gs_GifShapesLog(void);
                            {
                                extern unsigned gVitaDpGenN[1];
                                extern unsigned gVitaSkipFastN[1];
                                logf_("vita:   fxclamp: %u runs, %u triangles | dpgen: %u state fields changed by kept parts | skipfast: %u packets' corners not built\n", gVitaFxClampN[0], gVitaFxClampN[1], gVitaDpGenN[0],
                                      gVitaSkipFastN[0]);
                                gVitaDpGenN[0] = gVitaSkipFastN[0] = 0;
                            }
                            {
                                extern unsigned gVitaDpGTexN[3];
                                logf_("vita:   dpgtex: %u group textures remembered, %u looked up, %u checks differed\n", gVitaDpGTexN[0], gVitaDpGTexN[1], gVitaDpGTexN[2]);
                                memset(gVitaDpGTexN, 0, sizeof(gVitaDpGTexN));
                            }
                            {
                                extern int gVitaUpRepeat, gVitaUpIdMemo;
                                extern unsigned gVitaUpRepN[5], gVitaUpIdMemoN[6];
                                if (gVitaUpRepeat) {
                                    logf_("vita:   uprepeat: %u small, %u of them seen in the last 8 lists (%u to where the same already lies); %u large, %u seen\n",
                                          gVitaUpRepN[0], gVitaUpRepN[1], gVitaUpRepN[2], gVitaUpRepN[3], gVitaUpRepN[4]);
                                }
                                if (gVitaUpIdMemo != 0) {
                                    logf_("vita:   upidmemo: %u written pieces' block ids from the memo (%u blocks), %u pieces' ids worked out and kept, %u not kept (over 8 blocks); "
                                          "check: %u blocks compared, %u differed\n",
                                          gVitaUpIdMemoN[0], gVitaUpIdMemoN[1], gVitaUpIdMemoN[2], gVitaUpIdMemoN[5], gVitaUpIdMemoN[3], gVitaUpIdMemoN[4]);
                                }
                                {
                                    extern int gVitaUpSame;
                                    extern unsigned gVitaUpSameN[6];
                                    if (gVitaUpSame != 0) {
                                        logf_("vita:   upsame: %u small uploads compared with GS memory, %u the same (not written; 3: would not have been), %u with a block taken note of; "
                                              "not compared: %u on a drawn-into page, %u over 72 blocks; check: %u found the same but changed by the write\n",
                                              gVitaUpSameN[0], gVitaUpSameN[1], gVitaUpSameN[2], gVitaUpSameN[3], gVitaUpSameN[4], gVitaUpSameN[5]);
                                    }
                                    memset(gVitaUpSameN, 0, sizeof(gVitaUpSameN));
                                }
                                memset(gVitaUpRepN, 0, sizeof(gVitaUpRepN));
                                memset(gVitaUpIdMemoN, 0, sizeof(gVitaUpIdMemoN));
                            }
                            {
                                extern unsigned gVitaSprRunN[7];
                                logf_("vita:   sprrun: %u packets, %u sprites the run's way (%u early, %u refused, %u tail, %u other); check: %u differed\n", gVitaSprRunN[0],
                                      gVitaSprRunN[1], gVitaSprRunN[2], gVitaSprRunN[3], gVitaSprRunN[4], gVitaSprRunN[5], gVitaSprRunN[6]);
                                memset(gVitaSprRunN, 0, sizeof(gVitaSprRunN));
                            }
                            {
                                extern unsigned gVitaHudBatchN[2], gVitaHudRunN[2];
                                logf_("vita:   hudbatch: %u packets of plain shapes, %u triangles in them | hudrun: %u runs, %u triangles\n", gVitaHudBatchN[0], gVitaHudBatchN[1],
                                      gVitaHudRunN[0], gVitaHudRunN[1]);
                                memset(gVitaHudBatchN, 0, sizeof(gVitaHudBatchN));
                                memset(gVitaHudRunN, 0, sizeof(gVitaHudRunN));
                            }
                            {
                                extern unsigned gVitaTboN[6];
                                logf_("vita:   tboui: %u pictures and %u lines made, %u given back; %u textures held, %u KB\n", gVitaTboN[0], gVitaTboN[1],
                                      gVitaTboN[2], gVitaTboN[3], gVitaTboN[4] / 1024);
                                gVitaTboN[0] = gVitaTboN[1] = gVitaTboN[2] = 0;
                                {
                                    extern unsigned gVitaTboMs[4];
                                    extern int gVitaTboPre;
                                    if (gVitaTboMs[0] != 0 || gVitaTboMs[1] != 0 || gVitaTboMs[2] != 0) {
                                        logf_("vita:   tbopre=%d: the drawing thread spent %u ms on pictures and lines (uploads %u KB); the maker %u ms on %u jobs\n",
                                              gVitaTboPre, gVitaTboMs[0], gVitaTboMs[3] / 1024, gVitaTboMs[1], gVitaTboMs[2]);
                                        gVitaTboMs[0] = gVitaTboMs[1] = gVitaTboMs[2] = gVitaTboMs[3] = 0;
                                    }
                                }
                                {
                                    extern unsigned gVitaTboLines[3];
                                    extern int gVitaTboText, gVitaTboMip;
                                    if (gVitaTboLines[0] != 0 || gVitaTboLines[2] != 0) {
                                        logf_("vita:   tbotext=%d tbomip=%d: %u lines made at once, in %u us; %u handed to the maker (the screen waited for them)\n",
                                              gVitaTboText, gVitaTboMip, gVitaTboLines[0], gVitaTboLines[1], gVitaTboLines[2]);
                                        gVitaTboLines[0] = gVitaTboLines[1] = gVitaTboLines[2] = 0;
                                    }
                                    {
                                        extern unsigned gVitaTboGated;
                                        if (gVitaTboGated != 0) {
                                            logf_("vita:   tbotext: %u pictures' draws held back while a screen waited for its lines\n", gVitaTboGated);
                                            gVitaTboGated = 0;
                                        }
                                    }
                                }
                            }
                            memset(gVitaFxClampN, 0, sizeof(gVitaFxClampN));
                            Gs_GifShapesLog();
                            logf_("vita:   kept forms without TEXA (dptexa): %u; parts walked whose kept form differed in TEXA alone: %u | texrt: %u pieces from the pools, %u made the old way, %u textures set up by the render thread\n",
                                  gVitaDpTexaN[0], gVitaDpTexaN[1], gVitaTexRtN[0], gVitaTexRtN[1], gVitaTexRtN[2]);
                            memset(gVitaDpTexaN, 0, sizeof(gVitaDpTexaN));
                            memset(gVitaTexRtN, 0, sizeof(gVitaTexRtN));
                        }
                        memset(gVitaDpBusyL, 0, sizeof(gVitaDpBusyL));
                        memset(gVitaSilGateN, 0, sizeof(gVitaSilGateN));
                        gVitaSkipCcN[0] = gVitaRtKickN[0] = 0;
                        memset(gVitaWtSigN, 0, sizeof(gVitaWtSigN));
                    }
                    gVitaRunEstN = 0;
                    memset(gVitaWtC, 0, sizeof(gVitaWtC));
                }
            }
                                            extern unsigned gVitaTriRunN[2];
                                            logf_("vita:   GS triangles the quick way %u; the full way %u (its state open to the next), %u; triangle packets read as runs: %u runs, %u triangles\n",
                                                  gVitaFl[0], gVitaFl[1], gVitaFl[2], gVitaTriRunN[0], gVitaTriRunN[1]);
                                            gVitaTriRunN[0] = gVitaTriRunN[1] = 0;
                                            {
                                                extern unsigned gVitaFlWhy[2][8], gVitaWtWay[5];
                                                logf_("vita:   the full way not open to the next (not drawn, texture moved, DATE, reads alpha, no colour or depth, into a target, other): triangles %u %u %u %u %u %u %u, sprites %u %u %u %u %u %u %u; "
                                                      "the work targets' way: target as texture %u, the picture %u, page 0 %u, page 70 %u, into a target %u\n",
                                                      gVitaFlWhy[0][0], gVitaFlWhy[0][1], gVitaFlWhy[0][2], gVitaFlWhy[0][3], gVitaFlWhy[0][4], gVitaFlWhy[0][5], gVitaFlWhy[0][6],
                                                      gVitaFlWhy[1][0], gVitaFlWhy[1][1], gVitaFlWhy[1][2], gVitaFlWhy[1][3], gVitaFlWhy[1][4], gVitaFlWhy[1][5], gVitaFlWhy[1][6],
                                                      gVitaWtWay[0], gVitaWtWay[1], gVitaWtWay[2], gVitaWtWay[3], gVitaWtWay[4]);
                                                memset(gVitaFlWhy, 0, sizeof(gVitaFlWhy));
                                                memset(gVitaWtWay, 0, sizeof(gVitaWtWay));
                                            }
                                            {
                                                extern uint64_t gVitaTriCyc[7];
                                                unsigned long long nt = gVitaTriCyc[6] ? gVitaTriCyc[6] : 1;
                                                logf_("vita:   a run's quick triangle, cycles (%llu measured): checks %llu, bookkeeping %llu, Q %llu, batch %llu, checks %llu, vertices %llu\n",
                                                      (unsigned long long)gVitaTriCyc[6], (unsigned long long)(gVitaTriCyc[0] / nt), (unsigned long long)(gVitaTriCyc[1] / nt),
                                                      (unsigned long long)(gVitaTriCyc[2] / nt), (unsigned long long)(gVitaTriCyc[3] / nt), (unsigned long long)(gVitaTriCyc[4] / nt),
                                                      (unsigned long long)(gVitaTriCyc[5] / nt));
                                                memset(gVitaTriCyc, 0, sizeof(gVitaTriCyc));
                                                {
                                                    extern unsigned long long gVitaTriRunCyc[2];
                                                    extern unsigned gVitaGifN[6];
                                                    logf_("vita:   runs read into vertices: %llu cycles a triangle (%llu measured); GIF tags: %u packed, %u register lists, %u images; %u A+D writes, %u register-list registers\n",
                                                          gVitaTriRunCyc[1] ? gVitaTriRunCyc[0] / gVitaTriRunCyc[1] : 0ull, gVitaTriRunCyc[1], gVitaGifN[0], gVitaGifN[1], gVitaGifN[2], gVitaGifN[3], gVitaGifN[4]);
                                                    memset(gVitaTriRunCyc, 0, sizeof(gVitaTriRunCyc));
                                                    memset(gVitaGifN, 0, sizeof(gVitaGifN));
                                                }
                                            }
                                            memset(gVitaStOf, 0, sizeof(gVitaStOf));
                                            memset(gVitaFl, 0, sizeof(gVitaFl));
                                        }
                                        memset(gVitaZone, 0, sizeof(gVitaZone));
                                        memset(gVitaZoneN, 0, sizeof(gVitaZoneN));
                                        gVitaZoneLists = 0;
                                    }
                                }
                                {   /* the list reader's zones */
                                    extern uint64_t gVitaZoneF[56];
                                    extern unsigned gVitaZoneNF[56], gVitaZoneListsF;
                                    static const char *const fn[31] = {"rest of the list", "up to a program call", "program call: buffers", "program 0: its batch read",
                                        "", "", "", "strip: up to the plan", "strip: the plan", "strip: the record", "", "", "program call: the rest", "", "", "",
                                        "program 0: a layer's registers", "", "program 4: its batch read", "", "DMA: the rest", "VIF commands", "unpack", "DIRECT data",
                                        "kept calls sent", "program 0: both layers sent", "", "", "", "", ""};
                                    if (gVitaZoneListsF) {
                                        unsigned z, nl = gVitaZoneListsF;
                                        for (z = 0; z < 31; z++) {
                                            if (gVitaZoneNF[z]) {
                                                logf_("vita:   list reader zone %2u %-28s %7u k cycles per drawn list, %6u marks, %4u cycles each\n", z, fn[z],
                                                      (unsigned)(gVitaZoneF[z] / nl / 1000), gVitaZoneNF[z] / nl, (unsigned)(gVitaZoneF[z] / gVitaZoneNF[z]));
                                            }
                                        }
                                        logf_("vita:   list reader zone mark costs %u cycles\n", (unsigned)(gVitaZoneF[31] / (gVitaZoneNF[31] ? gVitaZoneNF[31] : 1)));
                                        memset(gVitaZoneF, 0, sizeof(gVitaZoneF));
                                        memset(gVitaZoneNF, 0, sizeof(gVitaZoneNF));
                                        gVitaZoneListsF = 0;
                                    }
                                    {   /* the slowest of the fight's first drawn lists, zone by zone */
                                        extern uint64_t gVitaWarmZ[2][48];
                                        extern unsigned gVitaWarmUs[2], gVitaWarmN[2];
                                        static const char *const zn2[31] = {"rest of the list", "kept part drawn directly", "frame end (draws, swap)",
                                            "list end", "consts built, to strip", "strip: state", "strip: flush", "GIF: image upload",
                                            "GIF: up to a register", "GIF: A+D register write", "GS prim: parse to draw", "GS prim: strips flushed", "upload: found unchanged", "GS prim: to state",
                                            "GS prim: state", "GS prim: to flush", "upload: bytes looked at", "fighter: to DrawVu0", "GS prim: vertices (b205)", "record: ring/loop", "record: GIF", "record: REG", "record: VU consts", "record: draw tail", "back end waiting", "chip: fighter upload", "chip: fighter batch", "chip: stage upload", "chip: stage batch", "draw: to chip_emit", "GS prim: flush (draw call)"};
                                        unsigned th, z;
                                        for (th = 0; th < 2; th++) {
                                            if (gVitaWarmN[th] == 0) {
                                                continue;
                                            }
                                            {
                                                char wl[900];
                                                int wn = snprintf(wl, sizeof(wl), "vita:   the fight's first drawn lists (%u): the slowest took the %s %u us; its zones, ms:", gVitaWarmN[th],
                                                                  th == 0 ? "drawing thread" : "list reader", gVitaWarmUs[th]);
                                                for (z = 0; z < 31 && wn > 0 && wn < (int)sizeof(wl) - 80; z++) {
                                                    if (gVitaWarmZ[th][z] >= 222000u) { /* (0.5 ms and more) */
                                                        wn += snprintf(wl + wn, sizeof(wl) - (size_t)wn, " %u %s %u.%u;", z, th == 0 ? zn2[z] : fn[z][0] ? fn[z] : "?",
                                                                       (unsigned)(gVitaWarmZ[th][z] / 444000u), (unsigned)(gVitaWarmZ[th][z] / 44400u % 10));
                                                    }
                                                }
                                                logf_("%s\n", wl);
                                            }
                                            gVitaWarmN[th] = 0;
                                            gVitaWarmUs[th] = 0;
                                            memset(gVitaWarmZ[th], 0, sizeof(gVitaWarmZ[th]));
                                        }
                                    }
                                }
                                { extern unsigned gVitaEs[6], gVitaEsGens[2]; if (gVitaEs[0] | gVitaEs[1] | gVitaEs[3] | gVitaEs[4]) { logf_("vita:   engine seam (native): %u calls sent from their copies, %u read the long way, %u kept, %u to the cache (table full), %u of an old generation, %u places taken over; generations %u of the stage, %u of the models\n", gVitaEs[0], gVitaEs[1], gVitaEs[2], gVitaEs[3], gVitaEs[4], gVitaEs[5], gVitaEsGens[0], gVitaEsGens[1]); memset(gVitaEs, 0, sizeof(gVitaEs)); } } /* (the seam's line with the counters: printed above without libperf) */
                                {
                                    extern unsigned long long gVitaWall[6];
                                    logf_("vita:   drawing thread, wall ms in this stretch: swap %u, buffer maps %u, draw calls %u, new textures %u, asleep waiting for work %u; fighter glDrawElements alone %u\n",
                                          (unsigned)(gVitaWall[0] / 1000), (unsigned)(gVitaWall[1] / 1000), (unsigned)(gVitaWall[2] / 1000), (unsigned)(gVitaWall[3] / 1000), (unsigned)(gVitaWall[4] / 1000), (unsigned)(gVitaWall[5] / 1000));
                                    {
                                        extern unsigned long long gVitaWallK[4][2];
                                        logf_("vita:   draw calls by kind (ms, calls): stage %u/%u, fighters %u/%u, other models %u/%u, GS primitives %u/%u\n",
                                              (unsigned)(gVitaWallK[0][0] / 1000), (unsigned)gVitaWallK[0][1], (unsigned)(gVitaWallK[1][0] / 1000), (unsigned)gVitaWallK[1][1],
                                              (unsigned)(gVitaWallK[2][0] / 1000), (unsigned)gVitaWallK[2][1], (unsigned)(gVitaWallK[3][0] / 1000), (unsigned)gVitaWallK[3][1]);
                                        memset(gVitaWallK, 0, sizeof(gVitaWallK));
                                        {
                                            extern unsigned gVitaFlushWhy[20][2];
                                            int w;
                                            logf_("vita:   batch flushes (times/batches) by cause: GS prim %u/%u, frame end %u/%u, fighter bins full %u/%u, stage %u/%u+%u/%u, general bins full %u/%u, other",
                                                  gVitaFlushWhy[0][0], gVitaFlushWhy[0][1], gVitaFlushWhy[1][0], gVitaFlushWhy[1][1], gVitaFlushWhy[5][0], gVitaFlushWhy[5][1],
                                                  gVitaFlushWhy[6][0], gVitaFlushWhy[6][1], gVitaFlushWhy[7][0], gVitaFlushWhy[7][1], gVitaFlushWhy[11][0], gVitaFlushWhy[11][1]);
                                            for (w = 0; w < 20; w++) { if (w != 0 && w != 1 && w != 5 && w != 6 && w != 7 && w != 11 && gVitaFlushWhy[w][0]) { logf_(" [%d] %u/%u", w, gVitaFlushWhy[w][0], gVitaFlushWhy[w][1]); } }
                                            logf_("\n");
                                            memset(gVitaFlushWhy, 0, sizeof(gVitaFlushWhy));
                                            { extern unsigned gVitaOrderRuns[2]; logf_("vita:   kept parts' draws: %u runs begun anew to keep their order, %u put with an earlier run of their state\n", gVitaOrderRuns[0], gVitaOrderRuns[1]); gVitaOrderRuns[0] = gVitaOrderRuns[1] = 0; }
                                            { extern void Vita_Tex1Log(void); Vita_Tex1Log(); }
                                            { extern void Vita_UpLog(void); Vita_UpLog(); }
                                            { extern unsigned gVitaXf[3]; if (gVitaXf[0] | gVitaXf[1] | gVitaXf[2]) { logf_("vita:   the intro's cross-fade: %u pictures copied, %u fade draws, %u left out\n", gVitaXf[0], gVitaXf[1], gVitaXf[2]); memset(gVitaXf, 0, sizeof(gVitaXf)); } }
                                            { extern unsigned gVitaCc[4]; logf_("vita:   model calls: %u sent from copies, %u read the long way and recorded, %u kept, %u dropped (changed)\n", gVitaCc[0], gVitaCc[1], gVitaCc[2], gVitaCc[3]); memset(gVitaCc, 0, sizeof(gVitaCc)); }
                                            { extern unsigned gVitaDp[8], gVitaDpMerge[2]; logf_("vita:   model parts drawn directly: %u (%u draws in %u states), %u parts walked (%u kept now, %u refused, %u found in another state), %u KB of indices on the chip; the stage's draws: %u merged into %u\n", gVitaDp[0], gVitaDp[1], gVitaDp[2], gVitaDp[6], gVitaDp[3], gVitaDp[4], gVitaDp[5], gVitaDp[7], gVitaDpMerge[0], gVitaDpMerge[1]); gVitaDpMerge[0] = gVitaDpMerge[1] = 0; { unsigned kb = gVitaDp[7]; memset(gVitaDp, 0, sizeof(gVitaDp)); gVitaDp[7] = kb; } }
                                            { extern unsigned gVitaCcWhy[9], gVitaCcMax, gVitaFtWhere[4], gVitaDpMiss[34], gVitaDpShared, gVitaList[2][2]; extern unsigned long long gVitaDpUs;
                                              unsigned perList = gVitaList[1][0] ? gVitaList[1][0] : 1, best[3] = {0, 0, 0}, bi[3] = {0, 0, 0}, i2, j2;
                                              for (i2 = 0; i2 < 34; i2++) { for (j2 = 0; j2 < 3; j2++) { if (gVitaDpMiss[i2] > best[j2]) { unsigned k2; for (k2 = 2; k2 > j2; k2--) { best[k2] = best[k2 - 1]; bi[k2] = bi[k2 - 1]; } best[j2] = gVitaDpMiss[i2]; bi[j2] = i2; break; } } }
                                              logf_("vita:   calls not kept: %u too big (largest %u KB), %u wrapped, %u empty, %u strips copied, %u strips not planned, %u other constants, %u other records, %u writing the constants; %u GIF pieces copied in\n",
                                                    gVitaCcWhy[0], gVitaCcMax >> 10, gVitaCcWhy[1], gVitaCcWhy[2], gVitaCcWhy[3], gVitaCcWhy[4], gVitaCcWhy[5], gVitaCcWhy[6], gVitaCcWhy[8], gVitaCcWhy[7]);
                                              logf_("vita:   fighter strips read by the list reader: %u in a call being recorded, %u in another call, %u in no call (%u planned on the chip)\n", gVitaFtWhere[0], gVitaFtWhere[1], gVitaFtWhere[2], gVitaFtWhere[3]);
                                              logf_("vita:   kept parts in another state, by field: %u x %u, %u x %u, %u x %u | %u groups found on the chip already | direct draws %u us per drawn list\n",
                                                    bi[0], best[0], bi[1], best[1], bi[2], best[2], gVitaDpShared, (unsigned)(gVitaDpUs / perList));
                                              { extern unsigned gVitaDs[4], gVglL2[2]; extern volatile int gVglL2Drawn; logf_("vita:   fighter strips drawn directly: %u in %u draws (%u pieces); %u went the batches' way | shadow-page strips (program 2): %u drawn, %u dropped%s\n", gVitaDs[0], gVitaDs[1], gVitaDs[2], gVitaDs[3], gVglL2[0], gVglL2[1], gVglL2Drawn ? "" : ", the others not sent"); memset(gVitaDs, 0, sizeof(gVitaDs)); gVglL2[0] = gVglL2[1] = 0; }
                                              memset(gVitaCcWhy, 0, sizeof(gVitaCcWhy)); gVitaCcMax = 0; memset(gVitaFtWhere, 0, sizeof(gVitaFtWhere)); memset(gVitaDpMiss, 0, sizeof(gVitaDpMiss)); gVitaDpShared = 0; gVitaDpUs = 0; }
                                            { extern unsigned gVitaFtMiss[4]; logf_("vita:   fighter strips that started a batch: other state %u, other screen matrix %u, place taken %u, full %u\n", gVitaFtMiss[0], gVitaFtMiss[1], gVitaFtMiss[2], gVitaFtMiss[3]); memset(gVitaFtMiss, 0, sizeof(gVitaFtMiss)); }
                                        }
                                    }
                                    {
                                        extern uint64_t vgl_dbg_time[8];
                                        logf_("vita:   inside vitaGL's glDrawElements (ms): shader setup %u (of it: fragment program %u, uniforms %u in %u cached-layout draws), indices %u, sceGxmDraw %u, in %u calls\n",
                                              (unsigned)(vgl_dbg_time[0] / 1000), (unsigned)(vgl_dbg_time[4] / 1000), (unsigned)(vgl_dbg_time[5] / 1000), (unsigned)vgl_dbg_time[6],
                                              (unsigned)(vgl_dbg_time[1] / 1000), (unsigned)(vgl_dbg_time[2] / 1000), (unsigned)vgl_dbg_time[3]);
                                        memset(vgl_dbg_time, 0, sizeof(vgl_dbg_time));
                                    }
                                    memset(gVitaWall, 0, sizeof(gVitaWall));
                                }
                                memset(gVitaPerfList, 0, 2 * 5 * sizeof(uint64_t));
                                memset(gVitaPerfWork, 0, 5 * sizeof(uint64_t));
                                gVitaPerfLists[0] = gVitaPerfLists[1] = 0;
                            }
                        }
                        memset(gProfHist, 0, sizeof(gProfHist));
                        memset(gProfGameHist, 0, sizeof(gProfGameHist));
                        gVitaUnpackSkipped = 0;
                        gVitaWaitSkips = 0;
                        gVitaSnap[0] = gVitaSnap[1] = 0;
                    }
                    memset(gVitaList, 0, sizeof(gVitaList));
                    gVitaUnpack[0] = gVitaUnpack[1] = 0;
                }
                if (gBtlSeq != NULL && gBtlSeq->state >= 2) {
                    extern unsigned gVglKeys;
                    logf_("vita:   fight: sequence %d, battle clock %d ticks, health %d / %d; %u texture keys made per picture\n", gBtlSeq->state,
                          BtlSeq_GetClock()[0], BtlCharApi_GetHp(0), BtlCharApi_GetHp(1), gVglKeys / perPic);
                    gVglKeys = 0;
                }
                if (gVglVuBatches != 0) {
                    logf_("vita:   vertex programs without the interpreter: %u triangles in %u strips per picture\n", gVglVuTris / perPic, gVglVuBatches / perPic);
                    gVglVuTris = gVglVuBatches = 0;
                }
                if (Vita_MovieStats(film, &filmUs)) {
                    logf_("vita:   film: %u pictures on screen in this time (%u film blocks/blank not uploaded); since its start %u shown, %u decoded too late,"
                          " %u left out, %u asked for early; %u ms per picture; %u us/blank on the game's thread\n",
                          gVglFilm, gVitaUp[4] / vb, film[0], film[1], film[2], film[3], filmUs / 1000, gVitaUsMovie / vb);
                }
                gVitaUsGs = gVitaUsMovie = gVitaUsUpload = 0;
                memset(gVitaUp, 0, sizeof(gVitaUp));
                gVglFilm = 0;
                sLateSum = 0;
            }
            gVglDraws = gVglDropped = gVglBatches = gVglNewTex = 0;
            t0 = t; vb0 = vblanks; drawn0 = gVitaGsDrawn;
        }
    }
    {   /* Unlock All from the menus: SELECT alone held for about a second (two of these half-second steps; three, a
           second and a half, were missed by players), or asked from Vita Settings */
        extern int gVitaInMenus;
        static int selHeld;
        if (sUnlockAsk) {
            sUnlockAsk = 0;
            selHeld = 0;
            unlock_in_menus(1);
        } else if (gVitaInMenus && (buttons() & (SCE_CTRL_START | SCE_CTRL_SELECT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) == SCE_CTRL_SELECT) {
            if (++selHeld >= 2) {
                selHeld = 0;
                unlock_in_menus(0);
            }
        } else {
            selHeld = 0;
        }
    }
    if ((buttons() & (SCE_CTRL_START | SCE_CTRL_SELECT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) == (SCE_CTRL_START | SCE_CTRL_SELECT)) {
        static int held;
        if (++held >= 3) { /* a second and a half: both are buttons of the game too */
            exit(9);
        }
    }
    sLastDraw = t;
    sLastVb = vblanks;
}

/* exit(), from anywhere in the port or the game: keep the screen until it has been read. */
void __wrap_exit(int code) {
    if (!sEnded) {
        extern void Vita_PgoDump(const char *why);
        Vita_PgoDump("exit");
        sEnded = 1;
        fflush(stdout);
        fflush(stderr);
        Vita_TextScreen();
        psvDebugScreenPrintf("\n\e[32mThe program ended with code %d%s.\e[39m\n", code,
                             code == 0 ? " (the fight ran to its end)" : code == 9 ? " (stopped by you)" : "");
        logf_("vita: exit(%d) at vertical blank %u\n", code, gPortVBlanks);
        if (gBtlSeq != NULL && gBtlSeq->state >= 2) {
            psvDebugScreenPrintf("last state: sequence %d, battle clock %d ticks, health %d / %d\n", gBtlSeq->state, BtlSeq_GetClock()[0],
                                 BtlCharApi_GetHp(0), BtlCharApi_GetHp(1));
            logf_("vita: last state: sequence %d, battle clock %d ticks, health %d / %d\n", gBtlSeq->state, BtlSeq_GetClock()[0],
                  BtlCharApi_GetHp(0), BtlCharApi_GetHp(1));
        }
        summary(1);
        psvDebugScreenPrintf("\nThe log is " HOME "/log" SFX ". Press START to quit.\n");
        log_sync();
        while (__sync_lock_test_and_set(&sLogDrainLock, 1)) {
            sceKernelDelayThread(100);
        }
        sLogThreaded = 0;
        if (sLog != NULL) { /* closed now, so that the files can be fetched while this screen is still up */
            fclose(sLog);
            sLog = NULL;
        }
        __sync_lock_release(&sLogDrainLock);
        fclose(stdout);
        fclose(stderr);
        wait_buttons(SCE_CTRL_START);
    }
    quit();
    for (;;) {
    }
}

/* bt3.txt: name=value lines; '#' starts a comment. */
/* Where the disc's files are. gVitaDisc (ux0:data/bt3/disc, or disc= of bt3.txt, which may name an .iso) when
   SLUS_216.78 is there; else the places players put them by mistake: ux0:data/bt3/disk, ux0:data/bt3 itself, or a
   folder inside either (an image unpacked into a folder of its own name), two levels down at most; else the disc's
   image as it is, an .iso of any name in ux0:data/bt3/disc, /disk or ux0:data/bt3 (vita_iso.c, arielxflores's, reads
   the files from it). When none has the files, a message says where they go and what was found instead. (The BT4
   port: ux0:data/bt4 and SLUS_219.78, PORT_VITA_HOME and PORT_VITA_ELF.) */
static int has_slus(const char *dir) {
    char path[300];
    SceIoStat st;
    snprintf(path, sizeof(path), "%s/" PORT_VITA_ELF, dir);
    return sceIoGetstat(path, &st) >= 0 && !SCE_S_ISDIR(st.st_mode);
}

static int find_below(const char *dir, int depth, char *out, size_t size) {
    SceUID d;
    SceIoDirent e;
    int found = 0;
    if (has_slus(dir)) {
        snprintf(out, size, "%s", dir);
        return 1;
    }
    if (depth == 0 || (d = sceIoDopen(dir)) < 0) {
        return 0;
    }
    memset(&e, 0, sizeof(e));
    while (!found && sceIoDread(d, &e) > 0) {
        char sub[300];
        if (!SCE_S_ISDIR(e.d_stat.st_mode) || e.d_name[0] == '.' || strcasecmp(e.d_name, "saves") == 0 ||
            strcasecmp(e.d_name, "shcache") == 0 || strcasecmp(e.d_name, "mods") == 0 || strcasecmp(e.d_name, "dump") == 0) {
            continue;
        }
        snprintf(sub, sizeof(sub), "%s/%s", dir, e.d_name);
        found = find_below(sub, depth - 1, out, size);
    }
    sceIoDclose(d);
    return found;
}

/* An .iso one folder down in `dir`, as an archive unpacks it (Team BT4's release holds PS2_NTSC/<image>.iso), or up to
   `depth` folders down (the archive unpacked into a folder of its own name, then its PS2_NTSC): 1, its path in `found`,
   mounted. The game's own folders are skipped. */
static int find_iso_below(const char *dir, const char *elf, char *found, int size, int *wrong, int depth) {
    extern int Vita_IsoFind(const char *dir, const char *file, int keep, char *out, int size, int *seen);
    static const char *const kSkip[] = {"saves", "mods", "shcache", "disc", "disk"};
    SceUID d = sceIoDopen(dir);
    SceIoDirent e;
    int ok = 0, k;

    if (d < 0) {
        return 0;
    }
    memset(&e, 0, sizeof(e));
    while (!ok && sceIoDread(d, &e) > 0) {
        char sub[256];
        int looked = 0, skip = e.d_name[0] == '.' || !SCE_S_ISDIR(e.d_stat.st_mode);
        for (k = 0; !skip && k < (int)(sizeof(kSkip) / sizeof(kSkip[0])); k++) {
            skip = strcasecmp(e.d_name, kSkip[k]) == 0;
        }
        if (skip || snprintf(sub, sizeof(sub), "%s/%s", dir, e.d_name) >= (int)sizeof(sub)) {
            continue;
        }
        ok = Vita_IsoFind(sub, elf, 1, found, size, &looked);
        *wrong += looked;
        if (!ok && depth > 1) {
            ok = find_iso_below(sub, elf, found, size, wrong, depth - 1);
        }
    }
    sceIoDclose(d);
    return ok;
}

/* --- when the disc's files are not found: what is there instead, said in a few short lines --- */

#define MB(x) ((long long)(x) >> 20)
#ifdef PORT_VITA_BT4
#define VITA_RELEASE_NAME "Team BT4's B14 Rev2"
#define VITA_RELEASE_NEED "This port needs Team BT4's B14 Rev2 (English or Spanish).\n" \
                          "Other builds (B13, older revisions) are not supported."
#else
#define VITA_RELEASE_NAME "the USA release (SLUS-21678)"
#define VITA_RELEASE_NEED "This port needs the USA release (SLUS-21678)."
#endif
#define FAT32_NOTE "\n  4 GB is the most a FAT32 card holds in one file: use an\n  exFAT card, or unpack the .iso (README)."

typedef struct DiscNotes {
    char text[900]; /* the first lines, for the screen */
    int n, wrong;   /* notes, of which images of another disc */
} DiscNotes;

static void disc_note(DiscNotes *ns, const char *fmt, ...) {
    char line[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    logf_("vita: found: %s\n", line);
    if (ns->n++ < 5 && strlen(ns->text) + strlen(line) + 2 < sizeof(ns->text)) {
        strcat(ns->text, line);
        strcat(ns->text, "\n");
    }
}

static int has_ext(const char *name, const char *ext) {
    size_t l = strlen(name), e = strlen(ext);
    return l > e && strcasecmp(name + l - e, ext) == 0;
}

/* One file of a folder the disc's files were looked for in: an archive, or an .iso that is cut short, not a disc
   image, or of another disc. (A file of 4 GB less a byte, or nearly: what a FAT32 card keeps of a larger one.) */
static void disc_look_at(DiscNotes *ns, const char *dir, const char *name, long long size) {
    extern int Vita_IsoProbe(const char *path, long long *have, long long *need, int *lost, char *label, int labelSize);
    extern int Vita_IsoDescribe(const char *path, const char *want, char *out, int size);
    static const char *const kPack[] = {".7z", ".zip", ".rar", ".001", ".tar", ".gz", ".xz", ".bz2"};
    char path[300], what[200], label[40];
    long long have, need;
    int lost, k, r, fat;

    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) {
        return;
    }
    for (k = 0; k < (int)(sizeof(kPack) / sizeof(kPack[0])); k++) {
        if (has_ext(name, kPack[k])) {
            disc_note(ns, "%s (%lld MB) is the archive:\n  extract it on a computer first, then copy the .iso it holds.", path, MB(size));
            return;
        }
    }
    if (!has_ext(name, ".iso")) {
        return;
    }
    r = Vita_IsoProbe(path, &have, &need, &lost, label, sizeof(label));
    fat = have >= 0xFFF00000ll && have <= 0x100000000ll;
    if (r < 0) {
        disc_note(ns, "%s cannot be opened.", path);
    } else if (r == 0) {
        disc_note(ns, "%s (%lld MB) is not a disc image%s%s", path, MB(have),
                  have < (1ll << 30) ? ":\n  smaller than a DVD image should be (a download or an\n  extraction that stopped early?)" : ".",
                  fat ? FAT32_NOTE : "");
    } else if (have < need) {
        disc_note(ns, "%s is cut short:\n  %lld MB of the disc's %lld MB (a copy that stopped early).%s", path, MB(have), MB(need),
                  fat ? FAT32_NOTE : "");
    } else if ((r = Vita_IsoDescribe(path, PORT_VITA_ELF, what, sizeof(what))) == 0) {
        disc_note(ns, "%s (%lld MB): its folders cannot be read.", path, MB(have));
    } else {
        ns->wrong += r == 1; /* (2: it has PORT_VITA_ELF, in a place not looked in) */
        disc_note(ns, "%s (%lld MB): found %s%s", path, MB(have), what,
                  r == 1 ? "." : ":\n  move it to " HOME "/.");
    }
}

static void disc_look_in(DiscNotes *ns, const char *dir, int depth) {
    static const char *const kSkip[] = {"saves", "mods", "shcache", "dump", "pgo"};
    SceUID d = sceIoDopen(dir);
    SceIoDirent e;
    int k;

    if (d < 0) {
        return;
    }
    memset(&e, 0, sizeof(e));
    while (sceIoDread(d, &e) > 0) {
        char sub[300];
        int skip = e.d_name[0] == '.';
        if (!SCE_S_ISDIR(e.d_stat.st_mode)) {
            if (!skip) {
                disc_look_at(ns, dir, e.d_name, (long long)e.d_stat.st_size);
            }
            continue;
        }
        for (k = 0; !skip && k < (int)(sizeof(kSkip) / sizeof(kSkip[0])); k++) {
            skip = strcasecmp(e.d_name, kSkip[k]) == 0;
        }
        if (!skip && depth > 0 && snprintf(sub, sizeof(sub), "%s/%s", dir, e.d_name) < (int)sizeof(sub)) {
            disc_look_in(ns, sub, depth - 1);
        }
    }
    sceIoDclose(d);
}

static void find_disc(void) {
    extern int Vita_IsoMount(const char *path);
    extern int Vita_IsoFind(const char *dir, const char *file, int keep, char *out, int size, int *seen);
    char found[256], seen[400];
    SceUID d;
    SceIoDirent e;
    int n = 0, wrong = 0, k;
    size_t len = strlen(gVitaDisc);
    if (len > 4 && strcasecmp(gVitaDisc + len - 4, ".iso") == 0 && !Vita_IsoMount(gVitaDisc)) { /* (disc= naming the image) */
        logf_("vita: %s is not a disc image that can be read\n", gVitaDisc);
    }
    if (has_slus(gVitaDisc)) {
        return;
    }
    if (strcmp(gVitaDisc, HOME "/disc") == 0 && (find_below(HOME "/disk", 1, found, sizeof(found)) || find_below(HOME, 2, found, sizeof(found)))) {
        logf_("vita: the disc's files are in %s (not %s)\n", found, gVitaDisc);
        snprintf(gVitaDisc, sizeof(gVitaDisc), "%s", found);
        return;
    }
    if (strcmp(gVitaDisc, HOME "/disc") == 0) { /* the disc's image as it is */
        static const char *const kWhere[] = {HOME "/disc", HOME "/disk", HOME};
        for (k = 0; k < 3; k++) {
            int looked = 0;
            if (Vita_IsoFind(kWhere[k], PORT_VITA_ELF, 1, found, sizeof(found), &looked)) {
                logf_("vita: the disc's files are read from its image %s\n", found);
                snprintf(gVitaDisc, sizeof(gVitaDisc), "%s", found);
                return;
            }
            wrong += looked;
        }
        if (find_iso_below(HOME, PORT_VITA_ELF, found, sizeof(found), &wrong, 2)) {
            logf_("vita: the disc's files are read from its image %s\n", found);
            snprintf(gVitaDisc, sizeof(gVitaDisc), "%s", found);
            return;
        }
    }
#ifdef PORT_VITA_BT4
    if (strcmp(gVitaDisc, HOME "/disc") == 0) {
        /* where else players keep BT4's files, unpacked or as the .iso: the BT3 Vita port's own BT4 folders (its 1.2
           reads BT4 from ux0:data/bt3/bt4, the Spanish release from its es/), so that one copy serves both apps, and
           the folders of its 1.2.1's list (arielxflores's VITA_BT4_ALSO) */
        static const char *const kAlso[] = {"ux0:data/bt3/bt4", "ux0:data/bt3/bt4/es", "ux0:bt4", "ux0:bt3/bt4"};
        for (k = 0; k < (int)(sizeof(kAlso) / sizeof(kAlso[0])); k++) {
            int looked = 0;
            if (has_slus(kAlso[k])) {
                logf_("vita: the disc's files are in %s (not %s)\n", kAlso[k], gVitaDisc);
                snprintf(gVitaDisc, sizeof(gVitaDisc), "%s", kAlso[k]);
                return;
            }
            if (Vita_IsoFind(kAlso[k], PORT_VITA_ELF, 1, found, sizeof(found), &looked)) {
                logf_("vita: the disc's files are read from its image %s\n", found);
                snprintf(gVitaDisc, sizeof(gVitaDisc), "%s", found);
                return;
            }
            wrong += looked;
        }
        for (k = 0; k < (int)(sizeof(kAlso) / sizeof(kAlso[0])); k++) { /* (an archive unpacked there: its PS2_NTSC/) */
            if (find_iso_below(kAlso[k], PORT_VITA_ELF, found, sizeof(found), &wrong, 1)) {
                logf_("vita: the disc's files are read from its image %s\n", found);
                snprintf(gVitaDisc, sizeof(gVitaDisc), "%s", found);
                return;
            }
        }
    }
#endif
    seen[0] = '\0';
    if ((d = sceIoDopen(HOME)) >= 0) {
        memset(&e, 0, sizeof(e));
        while (sceIoDread(d, &e) > 0) {
            size_t l = strlen(e.d_name);
            if (e.d_name[0] == '.' || (!SCE_S_ISDIR(e.d_stat.st_mode) && !(l > 4 && strcasecmp(e.d_name + l - 4, ".iso") == 0))) {
                continue;
            }
            if (n++ < 8 && strlen(seen) + l + 4 < sizeof(seen)) {
                strcat(seen, "  ");
                strcat(seen, e.d_name);
                strcat(seen, SCE_S_ISDIR(e.d_stat.st_mode) ? "/\n" : "\n");
            }
        }
        sceIoDclose(d);
    }
    {   /* what is there instead: archives, .iso files cut short or of another disc (each in the log too) */
        DiscNotes ns;
        memset(&ns, 0, sizeof(ns));
        if (strcmp(gVitaDisc, HOME "/disc") != 0) { /* (disc= of bt3.txt: that place only) */
            const char *slash = strrchr(gVitaDisc, '/');
            if (has_ext(gVitaDisc, ".iso") && slash != NULL) { /* (its size is the probe's: a mounted image stats as a folder) */
                char dir[256];
                snprintf(dir, sizeof(dir), "%.*s", (int)(slash - gVitaDisc), gVitaDisc);
                disc_look_at(&ns, dir, slash + 1, 0);
            } else {
                disc_look_in(&ns, gVitaDisc, 0);
            }
        } else {
            disc_look_in(&ns, HOME, 2);
#ifdef PORT_VITA_BT4
            disc_look_in(&ns, "ux0:data/bt3/bt4", 1);
            disc_look_in(&ns, "ux0:bt4", 1);
            disc_look_in(&ns, "ux0:bt3/bt4", 1);
#endif
        }
        if (ns.n > 5) {
            snprintf(ns.text + strlen(ns.text), sizeof(ns.text) - strlen(ns.text), "(and %d more: " HOME "/log" SFX ")\n", ns.n - 5);
        }
        Vita_Fatal("The game's files were not found. Copy your own disc of\n"
                   VITA_RELEASE_NAME " into\n  %s/\n"
                   "as its .iso (a 1:1 image of the disc), or " PORT_VITA_ELF "\n"
                   "and the folders BIN and DATA from it.\n\n%s%s%s%s",
                   gVitaDisc, ns.n ? "Found:\n" : n ? "In " HOME " there is:\n" : "", ns.n ? ns.text : seen,
                   ns.wrong ? "\n" VITA_RELEASE_NEED : "",
                   wrong && !ns.n ? "\nThe .iso found is not of " VITA_RELEASE_NAME "\n(it has no " PORT_VITA_ELF "), or it could not be read." : "");
    }
}

/* The disc's place in a few words, for the start's messages (vita_mem.c): the image's path, size and volume name,
   or the folder. */
void Vita_DiscSay(char *out, int size) {
    extern int Vita_IsoIsMounted(const char *path);
    extern int Vita_IsoProbe(const char *path, long long *have, long long *need, int *lost, char *label, int labelSize);
    long long have, need;
    int lost;
    char label[40];

    if (Vita_IsoIsMounted(gVitaDisc) && Vita_IsoProbe(gVitaDisc, &have, &need, &lost, label, sizeof(label)) == 1) {
        snprintf(out, (size_t)size, "%s (%lld MB, volume \"%s\")", gVitaDisc, MB(have), label);
    } else {
        snprintf(out, (size_t)size, "the folder %s", gVitaDisc);
    }
}

/* Where the game's files are read from and where the port's own are kept, in the log; an image found with files cut
   off (a copy that stopped early, or a FAT32 card's 4 GB) stops here, before the game asks for one of them. */
static void disc_found(void) {
    extern int Vita_IsoIsMounted(const char *path);
    extern int Vita_IsoProbe(const char *path, long long *have, long long *need, int *lost, char *label, int labelSize);
    long long have, need;
    int lost;
    char label[40];

    if (Vita_IsoIsMounted(gVitaDisc) && Vita_IsoProbe(gVitaDisc, &have, &need, &lost, label, sizeof(label)) == 1) {
        logf_("vita: the game's files: the disc image %s (%lld bytes, the disc's %lld, volume \"%s\")\n", gVitaDisc, have, need, label);
        if (lost > 0) {
            Vita_Fatal("The disc image\n  %s\nis cut short: %lld MB of the disc's %lld MB, %d of its files\n"
                       "are missing (a copy that stopped early). Copy it again.%s",
                       gVitaDisc, MB(have), MB(need), lost, have >= 0xFFF00000ll && have <= 0x100000000ll ? FAT32_NOTE : "");
        }
    } else {
        logf_("vita: the game's files: the folder %s\n", gVitaDisc);
    }
    logf_("vita: the port's own files (log, saves, settings, bt3.txt, shaders, mods): " HOME "\n");
}

static void read_settings(void) {
    FILE *fp = fopen(HOME "/bt3.txt", "r");
    char line[400];
    int sweepSet = 0;

    if (fp == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *eq = strchr(line, '='), *end;

        if (line[0] == '#' || eq == NULL) {
            continue;
        }
        *eq++ = '\0';
        end = eq + strlen(eq);
        while (end > eq && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) {
            *--end = '\0';
        }
#ifdef PORT_VITA_RELEASE
        /* the released program: only the player's settings. The test runs leave theirs in the same file
           (mode=demo, film=...): a game started from its bubble goes to its title screen, not into a test fight */
        if (strcmp(line, "disc") != 0 && strcmp(line, "heap") != 0 && strcmp(line, "wide") != 0 && strcmp(line, "report") != 0 &&
            strcmp(line, "fightres") != 0 && strcmp(line, "tagteam") != 0 && strcmp(line, "es_disc") != 0 && strcmp(line, "anime_music") != 0) {
            continue;
        }
#endif
        if (strcmp(line, "disc") == 0) {
            snprintf(gVitaDisc, sizeof(gVitaDisc), "%s", eq);
#ifdef PORT_VITA_BT4
        } else if (strcmp(line, "es_disc") == 0) { /* the Spanish B14 Rev2 (.iso or folder) whose songs the English disc plays: the anime soundtrack (vita_file.c) */
            extern char gVitaEsDisc[256];
            snprintf(gVitaEsDisc, sizeof(gVitaEsDisc), "%s", eq);
        } else if (strcmp(line, "anime_music") == 0) { /* the anime soundtrack: 0 off, 1 on, over the Vita Settings one (vita_file.c) */
            extern int gVitaAnimeMusic;
            gVitaAnimeMusic = atoi(eq) != 0;
#endif
        } else if (strcmp(line, "discdir") == 0) { /* (the test runs, whose settings cannot hold a path) disc=<home>/<name> */
            snprintf(gVitaDisc, sizeof(gVitaDisc), HOME "/%s", eq);
        } else if (strcmp(line, "float") == 0) {
            sFloatChoice = strcmp(eq, "fast") == 0;
        } else if (strcmp(line, "heap") == 0) {
            sHeapCdram = strcmp(eq, "cdram") == 0;
        } else if (strcmp(line, "skip") == 0) {
            gVitaGsFixedSkip = atoi(eq);
        } else if (strcmp(line, "mode") == 0) {
            sMode = strcmp(eq, "demo") == 0;
        } else if (strcmp(line, "artdump") == 0) { /* the menus' packs written out (vita_optart.c art_dump) */
            setenv("BT3_ARTDUMP", eq, 1);
        } else if (strcmp(line, "soaksec") == 0) {
            sSoakSec = atoi(eq);
        } else if (strcmp(line, "sweep") == 0) {
            sSweepSec = atoi(eq);
            sweepSet = 1;
        } else if (strcmp(line, "texearly") == 0) { /* 0: a new texture waits for the render thread before its pixels are read (vita_vgl.c) */
            extern int gVitaTexEarly;
            gVitaTexEarly = atoi(eq);
        } else if (strcmp(line, "fxhalf") == 0) { /* 0: the full-screen passes in full precision (vita_vgl.c) */
            extern int gVitaFxHalf;
            gVitaFxHalf = atoi(eq) != 0;
        } else if (strcmp(line, "shbank") == 0) { /* 1: the run's shaders written to ux0:data/bt3/shbank.bin, the bank's making (vita_vgl.c) */
            extern int gVitaShHarvest;
            gVitaShHarvest = atoi(eq); /* (2: the bank checked, each program not in it said in the log) */
        } else if (strcmp(line, "fightres") == 0) { /* the fights drawn at 640 x 368 (default), 720 x 408 or 960 x 544 (vita_vgl.c) */
            extern int gVitaFightW;
            gVitaFightW = atoi(eq);
        } else if (strcmp(line, "wide") == 0) {
            extern int gVitaWide;
            gVitaWide = atoi(eq) != 0; /* wide=0: the console's 4:3 picture with bars at the sides */
        } else if (strcmp(line, "msaa") == 0) { /* the chip's multisampling: 0, 2 or 4 */
            extern int gVitaMsaa;
            gVitaMsaa = atoi(eq);
        } else if (strcmp(line, "menuhi") == 0) { /* 0: the menus at 640 x 368 as the fights, as before */
            extern int gVitaMenuHi;
            gVitaMenuHi = atoi(eq) != 0;
        } else if (strcmp(line, "fadepass") == 0) { /* VU1 program 1, the close-up shine: 0 not drawn, 2 its shine layer twice */
            extern int gPortFadePass;
            gPortFadePass = atoi(eq);
        } else if (strcmp(line, "perf") == 0) { /* 0: no processor counters (libperf), as the released program: its speed */
            sPerfOff = atoi(eq) == 0;
        } else if (strcmp(line, "fl6") == 0) { /* 0: sprites never take the state of the sprite before (as an earlier version) */
            extern int gVitaFl6;
            gVitaFl6 = atoi(eq) != 0;
        } else if (strcmp(line, "runest") == 0) { /* 0: a run's first triangle the full way when its state is not known */
            extern int gVitaRunEst;
            gVitaRunEst = atoi(eq) != 0;
        } else if (strcmp(line, "wtc") == 0) { /* 0: the work targets' reads not remembered */
            extern int gVitaWtCOn;
            gVitaWtCOn = atoi(eq) != 0;
        } else if (strcmp(line, "stx") == 0) { /* 0: no states kept from one picture to the next */
            extern int gVitaStXOn;
            gVitaStXOn = atoi(eq) != 0;
        } else if (strcmp(line, "wtrem") == 0) { /* the work targets' reads without a texture transform remembered: 0 off, 1 on, 2 soak B, 3 a check */
            extern int gVitaWtRem;
            gVitaWtRem = atoi(eq);
        } else if (strcmp(line, "dpways") == 0) { /* a kept part's second place when its first is another's: 0 off, 1 on, 2 soak B */
            extern int gVitaDpWays;
            gVitaDpWays = atoi(eq);
        } else if (strcmp(line, "silgate") == 0) { /* the shadow page's strips not sent while the page is not used: 0 off, 1 on, 2 soak B */
            extern int gVitaSilGate;
            gVitaSilGate = atoi(eq);
        } else if (strcmp(line, "silprobe") == 0) { /* with silgate, one frame in this many sends the strips all (a power of two, 2-256; 16) */
            extern unsigned gVitaSilProbe;
            unsigned v = (unsigned)atoi(eq);
            if (v >= 2 && v <= 256 && (v & (v - 1)) == 0) {
                gVitaSilProbe = v;
            }
        } else if (strcmp(line, "skipcc") == 0) { /* kept calls sent as their registers in lists not drawn: 0 off, 1 on, 2 soak B */
            extern int gVitaSkipCc;
            gVitaSkipCc = atoi(eq);
        } else if (strcmp(line, "wtsig") == 0) { /* the targets' signatures worked out only when GS memory changed: 0 off, 1 on, 2 soak B */
            extern int gVitaWtSig;
            gVitaWtSig = atoi(eq);
        } else if (strcmp(line, "skipfast") == 0) { /* lists not drawn: the runs build only their last packet's corners: 0 off, 1 on, 2 soak B */
            extern int gVitaSkipFast;
            gVitaSkipFast = atoi(eq);
        } else if (strcmp(line, "dpgen") == 0) { /* a kept part's register changes bump the state's generation: 0 off, 1 on, 2 soak B */
            extern int gVitaDpGen;
            gVitaDpGen = atoi(eq);
        } else if (strcmp(line, "dpgtex") == 0) { /* a kept group's texture remembered by its names: 0 off, 1 on, 2 soak B, 3 on + checked */
            extern int gVitaDpGTex;
            gVitaDpGTex = atoi(eq);
        } else if (strcmp(line, "uprepeat") == 0) { /* 1: the uploads seen again by their bytes counted, in the report (a measure) */
            extern int gVitaUpRepeat;
            gVitaUpRepeat = atoi(eq);
        } else if (strcmp(line, "upidmemo") == 0) { /* the written uploads' block ids remembered by their bytes: 0 off, 1 on, 2 soak B, 3 on + check */
            extern int gVitaUpIdMemo;
            gVitaUpIdMemo = atoi(eq);
        } else if (strcmp(line, "upsame") == 0) { /* a small upload not written when GS memory holds its bytes there already: 0 off, 1 on, 2 soak B, 3 written + check */
            extern int gVitaUpSame;
            gVitaUpSame = atoi(eq);
        } else if (strcmp(line, "sprrun") == 0) { /* the GS's sprite packets read as runs: 0 off, 1 on, 2 soak B, 3 on and a check */
            extern int gVitaSprRun;
            gVitaSprRun = atoi(eq);
        } else if (strcmp(line, "dpptr") == 0) { /* the kept parts' groups handed over by their place: 0 off, 1 on, 2 soak B */
            extern int gVitaDpPtr;
            gVitaDpPtr = atoi(eq);
        } else if (strcmp(line, "dpcrec") == 0) { /* a kept part's constants read from its record: 0 off, 1 on, 2 soak B */
            extern int gVitaDpCrec;
            gVitaDpCrec = atoi(eq);
        } else if (strcmp(line, "adfast") == 0) { /* GIF tags of A+D writes written in one loop: 0 off, 1 on, 2 soak B */
            extern int gVitaAdFast;
            gVitaAdFast = atoi(eq);
        } else if (strcmp(line, "hudrun") == 0) { /* the HUD's plain triangles (hudbatch's packets) as a run: 0 off, 1 on, 2 soak B */
            extern int gVitaHudRun;
            gVitaHudRun = atoi(eq);
        } else if (strcmp(line, "hudbatch") == 0) { /* the HUD's plain shapes (the Tag Team radar) in one packet: 0 off, 1 on, 2 soak B */
            extern int gVitaHudBatch;
            gVitaHudBatch = atoi(eq);
        } else if (strcmp(line, "tboui") == 0) { /* BT4decomp's own screens' pictures and TrueType lines drawn (vita_tbo.c): 0 off, 1 on, 2 soak B */
            extern int gVitaTboUi;
            gVitaTboUi = atoi(eq);
        } else if (strcmp(line, "tbopre") == 0) { /* BT4decomp's screens' pictures and lines made by a thread of its own, the Tag Team set ahead, the outline faster (vita_tbo.c, tbo_text.c): 0 the drawing thread at first use (v1.4), 1 (default) the maker */
            extern int gVitaTboPre, gTboFastOutline;
            gVitaTboPre = atoi(eq) != 0;
            gTboFastOutline = gVitaTboPre;
        } else if (strcmp(line, "tbotext") == 0) { /* BT4decomp's screens' lines (Tag Team screens and HUD) made when asked for, there from the first picture that shows them (vita_tbo.c): 0 v1.4's way (tbopre), 1 (default) on, 2 the soak's B battles */
            extern int gVitaTboText, gTboFastOutline;
            gVitaTboText = atoi(eq);
            if (gVitaTboText) {
                gTboFastOutline = 1;
            }
        } else if (strcmp(line, "tbotextpct") == 0) { /* the Tag Team screens' and HUD's letters this many hundredths of the PC's size (port_screen.c, vita_tbo.c): 100 (default) the PC's */
            extern int gVitaTboTextPct;
            gVitaTboTextPct = atoi(eq);
        } else if (strcmp(line, "tbotextsize") == 0) { /* BT4decomp's screens' lines written at the Vita's size (capitals 40 / 24, tbo_text.c): 1 (default), 0 the PC's 80 / 48 then halved */
            extern int gVitaTboTextSize;
            gVitaTboTextSize = atoi(eq) != 0;
        } else if (strcmp(line, "tbomip") == 0) { /* BT4decomp's screens' lines and pictures with their smaller copies, sampled trilinearly as on PC (vita_tbo.c, vita_vgl.c): 0 v1.4's way, 1 (default) on, 2 the soak's B battles */
            extern int gVitaTboMip;
            gVitaTboMip = atoi(eq);
        } else if (strcmp(line, "tagcam") == 0) { /* BT4decomp's free / spectator camera (vita_spectator.c): 0 the game's camera, 1 as on PC */
            extern int gVitaTagCam;
            gVitaTagCam = atoi(eq) != 0;
        } else if (strcmp(line, "taglang") == 0) { /* BT4decomp's screens in the disc's language (vita_onlinelang.c): 0 English, 1 as on PC */
            extern int gVitaTagLang;
            gVitaTagLang = atoi(eq) != 0;
        } else if (strcmp(line, "tbomovie") == 0) { /* the film behind the Tag Team screens (vita_tagmovie.c, the hardware player): 0 the poster (default), 1 the film, 2 the software MPEG-2 film */
            extern int gVitaTagMovie;
            gVitaTagMovie = atoi(eq);
        } else if (strcmp(line, "tbomoviecopy") == 0) { /* tbomovie=1: 1 each picture copied into the film's own aligned buffers (default), 0 the player's frames used in place */
            extern int gVitaTagMovieCopy;
            gVitaTagMovieCopy = atoi(eq);
        } else if (strcmp(line, "tbomoviecsc") == 0) { /* tbomovie=1: the colour conversion of the chip's YUV texture: 0 CSC0 (default), 1 CSC1 (vitaGL's sample) */
            extern int gVitaTagMovieCsc;
            gVitaTagMovieCsc = atoi(eq);
        } else if (strcmp(line, "tbomoviemem") == 0) { /* tbomovie=1: MB of physically contiguous memory vitaGL leaves to the player (default 0: vitaGL pool given back while the film plays; the decoder asks for a 14 MB block) */
            extern int gVitaTagMovieMem;
            gVitaTagMovieMem = atoi(eq);
        } else if (strcmp(line, "tbomoviereserve") == 0) { /* tbomovie=1: MB of physically contiguous memory the film leaves free in the system (default 6) */
            extern int gVitaTagMovieReserve;
            gVitaTagMovieReserve = atoi(eq);
        } else if (strcmp(line, "tbomoviestage") == 0) { /* tbomovie=1: 1 only gives vitaGL's pool back and takes it again, no player (a test) */
            extern int gVitaTagMovieStage;
            gVitaTagMovieStage = atoi(eq);
        } else if (strcmp(line, "fxclamp") == 0) { /* the animated effects' triangles (with their CLAMP writes) as runs: 0 off, 1 on, 2 soak B */
            extern int gVitaFxClamp;
            gVitaFxClamp = atoi(eq);
        } else if (strcmp(line, "gifshapes") == 0) { /* 1: the GIF tags outside the runs counted by shape, in the report (a measure) */
            extern int gVitaGifShapes;
            gVitaGifShapes = atoi(eq);
        } else if (strcmp(line, "dptexa") == 0) { /* a kept part that cannot read TEXA does not compare it: 0 off, 1 on, 2 soak B */
            extern int gVitaDpTexa;
            gVitaDpTexa = atoi(eq);
        } else if (strcmp(line, "dppf") == 0) { /* the next record's kept part fetched ahead: 0 off, 1 on, 2 soak B */
            extern int gVitaDpPf;
            gVitaDpPf = atoi(eq);
        } else if (strcmp(line, "texrt") == 0) { /* textures with a palette made without waiting for the render thread: 0 off, 1 on, 2 soak B */
            extern int gVitaTexRt;
            gVitaTexRt = atoi(eq);
        } else if (strcmp(line, "rtkick") == 0) { /* the render thread not woken for every kept part: 0 off, 1 on, 2 soak B */
            extern int gVitaRtKick;
            gVitaRtKick = atoi(eq);
        } else if (strcmp(line, "texcont") == 0) { /* a texture found by its contents wherever GS memory holds them: 0 off, 1 on, 2 soak B */
            extern int gVitaTexCont;
            gVitaTexCont = atoi(eq);
        } else if (strcmp(line, "stl4") == 0) { /* the state of the call before for four register sets: 0 off, 1 on, 2 soak B, 3 a check */
            extern int gVitaStL4;
            gVitaStL4 = atoi(eq);
        } else if (strcmp(line, "stlast") == 0) { /* 0: no state of the call before (as an earlier version) */
            extern int gVitaStLastOn;
            gVitaStLastOn = atoi(eq) != 0;
        } else if (strcmp(line, "shadelog") == 0) { /* 1: the fighters' shading layers in the log (an earlier version's) */
            extern int gVitaShadeLog;
            gVitaShadeLog = atoi(eq) != 0;
        } else if (strcmp(line, "snapcut") == 0) { /* snap_make lighter: 0 off, 1 on, 2 in the soak's B battles, 3 a check (gs_core.c gVitaSnapCut) */
            extern int gVitaSnapCut;
            gVitaSnapCut = atoi(eq);
        } else if (strcmp(line, "hitchlog") == 0) { /* the hitch recorder: a picture on the screen longer than this many ms is explained in the log (vita_hitch.c); 0 off */
            extern int gVitaHitchWant;
            gVitaHitchWant = atoi(eq);
        } else if (strcmp(line, "hitchmax") == 0) { /* hitchlog: the most blocks written (default 60) */
            extern int gVitaHitchMax;
            gVitaHitchMax = atoi(eq);
        } else if (strcmp(line, "memsample") == 0) { /* vitaGL's memory walk (mem_sample) in a fight: 0 every 1024th picture as before, 1 none in a fight, 2 none in the soak's B battles */
            extern int gVitaMemSample;
            gVitaMemSample = atoi(eq);
        } else if (strcmp(line, "gzones") == 0) { /* 1: the game's thread timed by part, a step at a time (vita_perf.c Vita_GZ): the report's "game thread by part" */
            extern int gVitaGZOn;
            gVitaGZOn = atoi(eq) != 0;
        } else if (strcmp(line, "trirun") == 0) { /* 0: the effects' triangle packets register by register (gs_core.c tri_run) */
            extern int gVitaTriRun;
            gVitaTriRun = atoi(eq) != 0;
        } else if (strcmp(line, "searun") == 0) { /* 0: the ordering table sent in its own order (src/sys/gfx_ot.c Ot_Send) */
            extern int gVitaSeaRun;
            gVitaSeaRun = atoi(eq) != 0;
        } else if (strcmp(line, "seaoff") == 0) { /* 1: the stage's surfaces not drawn (what they cost) */
            gVitaSeaOff = atoi(eq) != 0;
        } else if (strcmp(line, "fastlane") == 0) { /* 0: every GS triangle the full way (vita_vgl.c) */
            extern int gVitaFastLane;
            gVitaFastLane = atoi(eq) != 0;
        } else if (strcmp(line, "sprinset") == 0) { /* 0: filtered sprites' coordinates as given (the lines under the HUD) */
            extern int gVitaSprInset;
            gVitaSprInset = atoi(eq) != 0;
        } else if (strcmp(line, "warm") == 0) { /* blanks from the stage's first picture in which the game waits for the pictures (0: off) */
            gVitaWarmVb = atoi(eq);
        } else if (strcmp(line, "gpuprof") == 0) { /* the GPU profile in a window of the fight (lists gpuproffrom..gpuprofto) */
            extern int gVitaGpuProf;
            gVitaGpuProf = atoi(eq);
        } else if (strcmp(line, "gpuproffrom") == 0) {
            extern int gVitaGpuProfFrom;
            gVitaGpuProfFrom = atoi(eq);
        } else if (strcmp(line, "gpuprofto") == 0) {
            extern int gVitaGpuProfTo;
            gVitaGpuProfTo = atoi(eq);
        } else if (strcmp(line, "surfcull") == 0) { /* 0: every water-surface triangle prepared and clipped, as before */
            extern int gVitaSurfCull;
            gVitaSurfCull = atoi(eq);
        } else if (strcmp(line, "snapsame") == 0) { /* 0: every image the lists refer to copied at the handover, as earlier versions */
            extern int gVitaSnapSame;
            gVitaSnapSame = atoi(eq);
        } else if (strcmp(line, "qlinopt") == 0) { /* 0: every perspective GS triangle's colour divided in full precision, as before */
            extern int gVitaQlinOpt;
            gVitaQlinOpt = atoi(eq);
        } else if (strcmp(line, "zlead") == 0) { /* depth steps a GS primitive leads by (0: none, as before) */
            extern int gVitaZLead;
            gVitaZLead = atoi(eq);
        } else if (strcmp(line, "drawlogverts") == 0) { /* with drawlog: the vertices of its GS draws of n vertices or more */
            extern int gVitaDrawLogVerts;
            gVitaDrawLogVerts = atoi(eq);
        } else if (strcmp(line, "drawlog") == 0) { /* each draw of lists a..b into the log: drawlog=a-b (vita_vgl.c bis_draw) */
            extern int gVitaDrawLogFrom, gVitaDrawLogTo;
            gVitaDrawLogFrom = atoi(eq);
            gVitaDrawLogTo = strchr(eq, '-') != NULL ? atoi(strchr(eq, '-') + 1) : gVitaDrawLogFrom;
        } else if (strcmp(line, "bisinv") == 0) { /* 1: only the draws bisfrom..bisto-1 of the kinds biskind drawn (the others of the list left out) */
            extern int gVitaBisInv;
            gVitaBisInv = atoi(eq) != 0;
        } else if (strcmp(line, "bislist") == 0 || strcmp(line, "bisfrom") == 0 || strcmp(line, "bisto") == 0 || strcmp(line, "biskind") == 0) {
            extern int gVitaBisList, gVitaBisFrom, gVitaBisTo, gVitaBisKind; /* draws of a list left out (vita_vgl.c bis_draw) */
            *(line[3] == 'l' ? &gVitaBisList : line[3] == 'f' ? &gVitaBisFrom : line[3] == 't' ? &gVitaBisTo : &gVitaBisKind) = atoi(eq);
        } else if (strcmp(line, "bit7px") == 0) { /* 0: the stencil's copy of alpha bit 7 left as it was under a primitive whose alpha crosses 0x80 */
            extern int gVitaBit7Px;
            gVitaBit7Px = atoi(eq);
        } else if (strcmp(line, "afail") == 0) { /* the alpha test's AFAIL modes (vita_vgl.c gVitaAFail; 0: no test, as before) */
            extern int gVitaAFail;
            gVitaAFail = atoi(eq);
        } else if (strcmp(line, "zclamp") == 0) { /* 0: a model's vertex beyond the depth range cut off by the GPU, as before */
            extern int gVitaZClamp;
            gVitaZClamp = atoi(eq);
        } else if (strcmp(line, "introwait") == 0) { /* 0: only the first blanks of the intro wait (warm=) */
            gVitaIntroWait = atoi(eq);
        } else if (strcmp(line, "planes") == 0) { /* 0: an upload into part of the 32-bit pixels compared by all of them, as before */
            extern int gVitaPlanes;
            gVitaPlanes = atoi(eq);
        } else if (strcmp(line, "order") == 0) { /* 0: the kept parts' draws of one state put together across others, as before */
            extern int gVitaOrder;
            gVitaOrder = atoi(eq);
        } else if (strcmp(line, "upquick") == 0) { /* 0: every large upload's number from all of its bytes */
            extern int gVitaUpQuick;
            gVitaUpQuick = atoi(eq);
        } else if (strcmp(line, "upcheck") == 0) { /* 1: uploads' numbers checked against all of their bytes (the log) */
            extern int gVitaUpCheck;
            gVitaUpCheck = atoi(eq);
        } else if (strcmp(line, "filmlist") == 0) { /* the film in lists a..b: filmlist=a-b (and c..d after a comma) */
            char *dash = strchr(eq, '-'), *comma = strchr(eq, ',');
            gVitaFilmFrom = atoi(eq);
            gVitaFilmTo = dash != NULL ? atoi(dash + 1) : gVitaFilmFrom;
            if (comma != NULL) {
                char *dash2 = strchr(comma + 1, '-');
                gVitaFilmFrom2 = atoi(comma + 1);
                gVitaFilmTo2 = dash2 != NULL ? atoi(dash2 + 1) : gVitaFilmFrom2;
            }
        } else if (strcmp(line, "filmwait") == 0) { /* 1: the game waits for the pictures around the filmed lists */
            gVitaFilmWait = atoi(eq);
        } else if (strcmp(line, "filmtrig") == 0) { /* the film in bursts of n pictures from each skill's name */
            gVitaFilmTrig = atoi(eq);
        } else if (strcmp(line, "logsplit") == 0) { /* the log in parts of n KB (log-part2-vgl.txt...): a part a fetch can take */
            sLogSplit = (unsigned)atoi(eq) * 1024u;
        } else if (strcmp(line, "filmmax") == 0) {
            gVitaFilmMax = atoi(eq);
        } else if (strcmp(line, "filmstate") == 0) { /* the film in that state of the fight (0: the intros) */
            gVitaFilmState = atoi(eq);
        } else if (strcmp(line, "film") == 0) { /* a small picture for the log every <n> pictures of a fight (test runs) */
            extern int gVitaFilm;
            gVitaFilm = atoi(eq);
        } else if (strcmp(line, "shadows") == 0) { /* the ground shadows' silhouettes: 1 every picture (as the console), 2 each fighter's every other picture */
            extern int gVitaSilMode;
            gVitaSilMode = atoi(eq);
        } else if (strcmp(line, "pbuf") == 0) { /* the graphics chip's parameter buffer, MB (vitaGL's default 16) */
            extern int gVitaPbufMb;
            gVitaPbufMb = atoi(eq);
        } else if (strcmp(line, "safe") == 0) { /* vita_vgl.c's gVglSafe, for a test run's A/B (268435456: textures with a palette converted to colours) */
            extern int gVglSafe;
            gVglSafe = (int)strtol(eq, NULL, 0);
        } else if (strcmp(line, "safe2") == 0) { /* vita_vgl.c's gVglSafe2: 1 a picture handed to the chip whole, 2 no render thread, 8 the render thread on the fourth core */
            extern int gVglSafe2;
            gVglSafe2 = (int)strtol(eq, NULL, 0);
        } else if (strcmp(line, "fxsweep") == 0) { /* the effects left out in turn: their cost in the log */
            extern int gVitaFxSweep;
            gVitaFxSweep = atoi(eq);
        } else if (strcmp(line, "wtshot") == 0) { /* with each film picture, the effects' work targets and the picture's alpha */
            extern int gVitaWtShot;
            gVitaWtShot = atoi(eq);
        } else if (strcmp(line, "sliver") == 0) { /* fighters' triangles far off the screen, into the log */
            extern int gVitaSliver;
            gVitaSliver = atoi(eq);
        } else if (strcmp(line, "report") == 0) {
            gVitaReport = atoi(eq) != 0;
        } else if (strcmp(line, "touchlr") == 0) { /* L2 / R2: rear (the rear pad), front (the screen's bottom corners), off */
            extern int gVitaTouchLR;
            gVitaTouchLR = strcmp(eq, "rear") == 0 ? 0 : strcmp(eq, "front") == 0 ? 1 : strcmp(eq, "off") == 0 ? 2 : -1;
        } else if (strcmp(line, "touchlog") == 0) { /* what the touches do, into the log */
            extern int gVitaTouchLog;
            gVitaTouchLog = atoi(eq);
        } else if (strcmp(line, "dump") == 0) { /* a copy of each file the game loads whole: dump/<id>.bin */
            extern int gVitaDump;
            gVitaDump = atoi(eq);
        } else if (strcmp(line, "demostage") == 0 || strcmp(line, "demochara1") == 0 || strcmp(line, "demochara2") == 0 ||
                   strcmp(line, "demoduel") == 0 || strcmp(line, "demoxform") == 0 || strcmp(line, "demosplit") == 0 ||
                   strcmp(line, "demoult") == 0 || strcmp(line, "demoultp") == 0 || strcmp(line, "demoultsec") == 0 ||
                   strcmp(line, "demohp") == 0 || strcmp(line, "demostand") == 0 || strcmp(line, "demoultlist") == 0 ||
                   (strncmp(line, "demochara", 9) == 0 && atoi(line + 9) >= 3 && atoi(line + 9) <= 10)) {
            /* the test fight: stage, fighters, a duel with intros, the screen split for two players (a second
               controller that nobody holds: the game wants one there); demoult...: a technique used again and again
               (demo_heavy); demochara3 .. 10: the further fighters' characters with BT4_TAG_ACTOR_TEST
               (src/battle/battle_work.c TagBattle_Prepare) */
            char env[32];
            snprintf(env, sizeof(env), "BT3_DEMO_%s", line + 4);
            for (char *c = env; *c; c++) {
                *c = (char)toupper((unsigned char)*c);
            }
            setenv(env, eq, 1);
            if (strcmp(line, "demosplit") == 0) {
                extern int gVitaPad2Fake;
                gVitaPad2Fake = atoi(eq) != 0;
            }
        } else if (strcmp(line, "soak") == 0) { /* battle after battle, never the end (port/src/headless.c Port_SoakNext) */
            if (atoi(eq) != 0) {
                setenv("BT3_SOAK", "1", 1);
            }
        } else if (strcmp(line, "soakstages") == 0) { /* the soak's stages in turn: ids and ranges, "0-34" or "1,6,0x10" */
            setenv("BT3_SOAK_STAGES", eq, 1);
        } else if (strcmp(line, "ccsize") == 0) { /* the replayed calls' places (gs_core.c cc_find): a power of two, 2048 .. 16384 */
            extern int gVitaCcBits;
            int n = atoi(eq), b = 11;
            while (b < 14 && (1 << b) < n) {
                b++;
            }
            gVitaCcBits = b;
        } else if (strcmp(line, "ccevict") == 0) { /* 0: a replayed call's place is never given up (as before; gs_core.c cc_find) */
            extern int gVitaCcEvict;
            gVitaCcEvict = atoi(eq) != 0;
        } else if (strcmp(line, "dppick") == 0) { /* 0: a kept part's place is its number's, taken or not (as before; gs_core.c dp_pick_serial) */
            extern int gVitaDpPick;
            gVitaDpPick = atoi(eq) != 0;
        } else if (strcmp(line, "dpslots") == 0) { /* the kept parts' places: a power of two, 1024 (as before) to 8192 (gs_core.c dp_alloc) */
            extern int gVitaDpSlots;
            int v = atoi(eq), n = 1024;
            while (n < v && n < 8192) {
                n *= 2;
            }
            gVitaDpSlots = n;
        } else if (strcmp(line, "fehelper") == 0) { /* the uploads' numbers on the fourth core: 0 off, 1 on, 2 on in the soak's B battles (gs_core.c) */
            extern int gVitaFeHelper;
            gVitaFeHelper = atoi(eq);
        } else if (strcmp(line, "rtswap") == 0) { /* a fight picture's swap made by the render thread: 0 off, 1 on, 2 in the soak's B battles */
            extern int gVitaRtSwap;
            gVitaRtSwap = atoi(eq);
        } else if (strcmp(line, "dpfreelate") == 0) { /* the kept parts let go freed a picture late, without a wait for the render thread: 0 off, 1 on, 2 in the soak's B battles */
            extern int gVitaDpFreeLate;
            gVitaDpFreeLate = atoi(eq);
        } else if (strcmp(line, "rtcap") == 0) { /* at most rtcapn pictures waiting in the render thread's ring behind the one handed over: 0 off, 1 on, 2 in the soak's B battles */
            extern int gVitaRtCap;
            gVitaRtCap = atoi(eq);
        } else if (strcmp(line, "rtcapn") == 0) { /* (rtcap) the pictures: 1 (default) to 6 */
            extern int gVitaRtCapN;
            gVitaRtCapN = atoi(eq);
        } else if (strcmp(line, "skipasync") == 0) { /* a list not drawn in a fight ends without waiting for the render thread: 0 off, 1 on, 2 in the soak's B battles */
            extern int gVitaSkipAsync;
            gVitaSkipAsync = atoi(eq);
        } else if (strcmp(line, "texrtfill") == 0) { /* (texrt) the texture pools filled by the render thread: 0 off, 1 on, 2 in the soak's B battles */
            extern int gVitaTexRtFill;
            gVitaTexRtFill = atoi(eq);
        } else if (strcmp(line, "chipkeep") == 0) { /* the stage's copies kept on the chip when the fighters' fill up: 0 off, 1 on, 2 in the soak's B battles */
            extern int gVitaChipKeep;
            gVitaChipKeep = atoi(eq);
        } else if (strcmp(line, "esrebind") == 0) { /* an object's model file bound again begins no new generation of the seam: 0 off, 1 on, 2 in the soak's B battles (gs_marker.c) */
            extern int gVitaEsRebind;
            gVitaEsRebind = atoi(eq);
        } else if (strcmp(line, "bindrange") == 0) { /* a model bind drops only the seam's calls whose memory its file meets: 0 off, 1 on, 2 in the soak's B battles, 3 on and each call so kept compared once (gs_marker.c, gs_core.c es_carry) */
            extern int gVitaBindRange;
            gVitaBindRange = atoi(eq);
        } else if (strcmp(line, "gxmcache") == 0) { /* vitaGL's GXM state calls left out when they change nothing: 0 off, 1 on, 2 in the soak's B battles */
            extern int gVitaGxmCache;
            gVitaGxmCache = atoi(eq);
        } else if (strcmp(line, "dpsplit") == 0) { /* kept parts' stage strips grouped by their bank too: 0 off, 1 on, 2 in the soak's B battles (vita_vgl.c GsGpu_DpCapEnd) */
            extern int gVitaDpSplit;
            gVitaDpSplit = atoi(eq);
        } else if (strcmp(line, "dpframe") == 0) { /* kept parts matched with FRAME's buffer as "the picture": 0 off, 1 on, 2 in the soak's B battles (gs_core.c dp_norm) */
            extern int gVitaDpFrame;
            gVitaDpFrame = atoi(eq);
        } else if (strcmp(line, "exitafter") == 0) { /* the program ends itself after that many seconds, its log written (test runs) */
            gVitaExitAfter = atoi(eq);
        } else if (strcmp(line, "native") == 0) { /* the engine seam: the stage's meshes named by the game, kept for the stage's life: 0 off, 1 on (default), 2 in the soak's B battles (gs_marker.c, gs_core.c es_find) */
            extern int gVitaNative;
            gVitaNative = atoi(eq);
        } else if (strcmp(line, "fxrun") == 0) { /* the effects as runs: quad particles decoded as triangle runs, triangle runs into the additive groups: 0 off, 1 on (default), 2 in the soak's B battles (gs_core.c quad_run, vita_vgl.c GsGpu_DrawTriRun) */
            extern int gVitaFxRun;
            gVitaFxRun = atoi(eq);
        } else if (strcmp(line, "dpfast") == 0) { /* kept parts drawn with what does not change worked out once, their textures remembered while GS memory is the same: 0 off, 1 on (default), 2 in the soak's B battles (vita_vgl.c GsGpu_DpDraw) */
            extern int gVitaDpFast;
            gVitaDpFast = atoi(eq);
        } else if (strcmp(line, "dprt") == 0) { /* kept parts: the fighters' uniform blocks worked out by the render thread, the parts' groups handed to it by their place: 0 off (default), 1 on, 2 in the soak's B battles (vita_vgl.c GsGpu_DpDraw, rt_put_dp) */
            extern int gVitaDpRt;
            gVitaDpRt = atoi(eq);
        } else if (strcmp(line, "updest") == 0) { /* native textures, step 2: each upload's destination remembers what it left there; the texture keys take the rectangle's number from it: 0 off, 1 on (default), 2 in the soak's B battles (gs_core.c Gs_DestRect) */
            extern int gVitaUpDest;
            gVitaUpDest = atoi(eq);
        } else if (strcmp(line, "seavtx") == 0) { /* the surfaces' (the sea's) corners tested and projected once a frame each, not once for each of their triangles: 0 off, 1 on (default), 2 in the soak's B battles (src/battle/eft_c.c sea_tri) */
            extern int gVitaSeaVtx;
            gVitaSeaVtx = atoi(eq);
        } else if (strcmp(line, "esdirect") == 0) { /* the seam's call packets of the models' parts done as what they are (their constants into VU memory, the program's registers) instead of read code by code: 0 off (default), 1 on, 2 in the soak's B battles (gs_core.c es_direct) */
            extern int gVitaEsDirect;
            gVitaEsDirect = atoi(eq);
        } else if (strcmp(line, "audiocore") == 0) { /* the sound thread: 0 on the game's core, 1 on the list reader's (default; vita_audio.c) */
            extern int gVitaAudioCore;
            gVitaAudioCore = atoi(eq);
        } else if (strcmp(line, "vjcore") == 0) { /* the strip thread: 0 where the system puts it (default), 1 on the fourth core (vita_vgl.c) */
            extern int gVitaVjCore;
            gVitaVjCore = atoi(eq);
        } else if (strcmp(line, "palexact") == 0) { /* a palette's identity from its colours, not its GS memory blocks' numbers: 0 off, 1 on (default), 2 in the soak's B battles (vita_vgl.c clut_id) */
            extern int gVitaPalExact;
            gVitaPalExact = atoi(eq);
        } else if (strcmp(line, "maxtex") == 0) { /* the texture table's room, 64..768 (768 by default; vita_vgl.c MAX_TEX) */
            extern int gVitaMaxTex;
            int n = atoi(eq);
            gVitaMaxTex = n < 64 ? 64 : n > 768 ? 768 : n;
        } else if (strcmp(line, "texbind") == 0) { /* native textures, step 3: textures bound by the names of what their destinations hold, a destination unbound at another's write: 0 off, 1 on (default), 2 in the soak's B battles (gs_core.c Gs_TexNames, vita_vgl.c texture_get_k) */
            extern int gVitaTexBind;
            gVitaTexBind = atoi(eq);
        } else if (strcmp(line, "upplan") == 0) { /* the large uploads' plans: their blocks' places remembered for each rectangle, and no rectangle's number worked out where another upload's data lies unchanged: 0 off, 1 on (default), 2 in the soak's B battles (gs_core.c plan_get, plan_rect_same) */
            extern int gVitaUpPlan;
            gVitaUpPlan = atoi(eq);
        } else if (strcmp(line, "updestlog") == 0) {
            extern int gVitaUpDestLog;
            gVitaUpDestLog = atoi(eq);
        } else if (strcmp(line, "upprof") == 0) { /* a measuring run: every upload timed by the way it went, and the texture lookups (gs_core.c transfer_data) */
            extern int gVitaUpProf;
            gVitaUpProf = atoi(eq);
        } else if (strcmp(line, "nativetex") == 0) { /* the stage's and the fighters' textures named by the game: their uploads' numbers not worked out from their bytes each time: 0 off, 1 on (default), 2 in the soak's B battles (gs_marker.c, gs_core.c texid_hash) */
            extern int gVitaNativeTex;
            gVitaNativeTex = atoi(eq);
        } else if (strcmp(line, "nativetexcheck") == 0) { /* a named texture's bytes read again every n-th upload (default 128; 0 never) */
            extern int gVitaTexCheck;
            gVitaTexCheck = atoi(eq);
        } else if (strcmp(line, "nativecheck") == 0) { /* the engine seam's meshes read again the long way every n-th call (default 512; 0 never) */
            extern int gVitaEsCheck;
            gVitaEsCheck = atoi(eq);
        } else if (strcmp(line, "fxlow") == 0) { /* the screen-filling additive effects drawn at a lower resolution and added: 0 off (as before), 1 on (default), 2 in the soak's B battles (vita_vgl.c) */
            extern int gVitaFxLow;
            gVitaFxLow = atoi(eq);
        } else if (strcmp(line, "fxlowmin") == 0) { /* the screens (of 512 x 448) an additive group covers at least to be drawn smaller (default 2) */
            extern float gVitaFxLowMin;
            gVitaFxLowMin = (float)atof(eq);
        } else if (strcmp(line, "fxlowdiv") == 0) { /* the smaller picture: the view's size / this each way, 2 to 4 (default 2) */
            extern int gVitaFxLowDiv;
            gVitaFxLowDiv = atoi(eq);
        } else if (strcmp(line, "soakab") == 0) { /* each soak battle twice, the settings of value 2 off then on (headless.c) */
            setenv("BT3_SOAK_AB", eq, 1);
        } else if (strcmp(line, "dpbank") == 0) { /* 0: the stage's kept groups only from the stage buffer's first 2 MB (as before; vita_vgl.c gVitaDpBank) */
            extern int gVitaDpBank;
            gVitaDpBank = atoi(eq) != 0;
        } else if (strcmp(line, "soakduel") == 0) { /* 1: the soak's battles all duels (the same work on every stage) */
            setenv("BT3_SOAK_DUEL", eq, 1);
        } else if (strcmp(line, "pgoname") == 0) { /* (a BT3_PGO=gen build) its counts in pgo/<name>.pgo (vita_pgo.c) */
            extern char gVitaPgoName[32];
            int k;
            for (k = 0; eq[k] != '\0' && k < 31; k++) { /* (letters, digits, '-' and '_' only: a file name) */
                char c = eq[k];
                gVitaPgoName[k] = (char)((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ? c : '_');
            }
            gVitaPgoName[k] = '\0';
        } else if (strcmp(line, "pgodump") == 0) { /* (a BT3_PGO=gen build) its counts written every <seconds> */
            extern int gVitaPgoEvery;
            gVitaPgoEvery = atoi(eq);
        } else if (strcmp(line, "ftdedupe") == 0) { /* the fighters' strips found on the chip by their vertices, and a table of 32768 places: 0 off, 1 on, 2 in the soak's B battles (vita_vgl.c) */
            extern int gVitaFtDedupe;
            gVitaFtDedupe = atoi(eq);
        } else if (strcmp(line, "adxasync") == 0) { /* a sound stream's first piece read by the reading thread, the stream begun there: 0 off (the game's thread reads it), 1 on, 2 in the soak's B battles (snd_adx.c) */
            extern int gVitaAdxAsync;
            gVitaAdxAsync = atoi(eq);
        } else if (strcmp(line, "m0") == 0) { /* online play, M0: a start like a session's, for comparing the simulation
                                                 with the PC's (vita_m0.c: asyncread=0, an empty save, the PC's keys) */
            sM0 = atoi(eq);
        } else if (strcmp(line, "ps2float") == 0) { /* the PS2's float arithmetic: 0 the float unit's, 1 the PS2's (the PC's);
                                                       only in the BT4_PS2FLOAT=1 build (port/vita/ps2float.py, vita_ps2float.c) */
            sPs2Float = atoi(eq);
        } else if (strcmp(line, "asyncread") == 0) { /* 0: reads on the caller's thread (vita_read.c) */
            extern int gVitaAsyncRead;
            gVitaAsyncRead = atoi(eq) != 0;
        } else if (strcmp(line, "vufquick") == 0) { /* the vector stand-ins' check: 1 short, 0 full (port_vufast.c) */
            extern int gVitaVufQuick;
            gVitaVufQuick = atoi(eq) != 0;
        } else if (strcmp(line, "startmode") == 0) { /* the menus start on this screen (62 the options) */
            extern int gVitaStartMode;
            gVitaStartMode = atoi(eq);
        } else if (strcmp(line, "tagscreen") == 0) { /* with startmode=80 (the port's screen mode): 1 the menus start on
                                                        BT4decomp's own Tag Team screens, as its main menu entry opens them */
            extern int gPortScreen;
            gPortScreen = atoi(eq) ? 2 : 0; /* (include/menu/port_screen.h PORT_SCREEN_TAG) */
        } else if (strcmp(line, "hangdump") == 0) {
            gVitaHangDump = atoi(eq);
        } else if (strcmp(line, "zones") == 0) { /* 1: the zones timed by the system's clock when libperf is not there */
            extern int gVitaZoneT;
            gVitaZoneT = atoi(eq) != 0;
        } else if (strcmp(line, "tagteam") == 0) { /* 0: no Tag Team entry in the main menu; 1 BT4decomp's own Tag Team
                                                      screens (the default); 2 the Vita's earlier text setup (Vita_TagMenu) */
            sTagMenu = atoi(eq) < 0 || atoi(eq) > 2 ? 1 : atoi(eq);
        } else if (strncmp(line, "adhoc", 5) == 0) { /* Ad Hoc (port/vita/vita_adhoc.c): adhoc_test, adhoc_delay, ... */
            extern void Vita_AdhocSetting(const char *name, const char *value);
            Vita_AdhocSetting(line, eq);
        } else if (strncmp(line, "online", 6) == 0) { /* the network (port/vita/vita_net.c): online, online_server, online_test, ... */
            extern void Vita_NetSetting(const char *name, const char *value);
            Vita_NetSetting(line, eq);
        } else if (strcmp(line, "replay") == 0) {
            setenv("BT3_REPLAY", eq, 1);
        } else if (strncmp(line, "BT3_", 4) == 0 || strncmp(line, "BT4_", 4) == 0) {
            /* BT4_ too: BT4decomp's own test switches, e.g. BT4_TAG_ACTOR_TEST=4 (src/battle/battle_work.c
               TagBattle_Prepare: every fight with 4, 6, 8 or 10 fighters, the extra ones CPUs) */
            setenv(line, eq, 1);
        }
        logf_("vita: setting %s=%s\n", line, eq);
    }
    fclose(fp);
    {   /* online play, M0 (vita_m0.c) and the PS2 float model (vita_ps2float.c), after every line is read */
        extern void Vita_M0Start(int on);
        extern void PortPF_Init(int on);
        Vita_M0Start(sM0);
        PortPF_Init(sPs2Float);
#ifdef BT4_PS2FLOAT_BUILD
        {
            extern int Vita_PfSelfTest(void (*log)(const char *fmt, ...));
            logf_("vita: ps2float=%d: the game's float instructions that can differ from the PS2's go through the model's"
                  " entry points (the BT4_PS2FLOAT build); %s\n", sPs2Float, sPs2Float == 0 ? "the float unit's own results" :
                  sPs2Float == 3 ? "the PS2's results, by the C model alone" : "the PS2's results");
            Vita_PfSelfTest(Vita_Log);
        }
#else
        if (sPs2Float) {
            logf_("vita: ps2float ignored: this build has no PS2 float model (build with BT4_PS2FLOAT=1)\n");
        }
#endif
    }
    if (getenv("BT3_SOAK") != NULL && !sweepSet) {
        /* the soak goes from battle to battle itself: the sweep's start again after 32 s would put it back to its first
           battle every time (a sweep= line still has its say) */
        sSweepSec = 0;
    }
}

/* Unlock All, a cheat on a text screen. Three ways in: SELECT pressed while the game starts (as in arielxflores's Vita
   port, whose start menu has it), SELECT held for about a second in the menus, and SQUARE on Options > Controller >
   Vita Settings. It gives every character and form, stage, song and item, the seven dragon flags, every Dragon
   History slot, and at least 4,850,000 Zenni: the game's own unlock (Save_UnlockAll, a routine the game has but never
   calls) without what that routine erases (src/sys/save.c Save_PortUnlockAll).
   Where it is done and how it is kept. v1.3 lost it two ways: from the start it was done as the boot's card flow ended
   (BootCard_OnFlowDone), and with no save on the card the title's New Game then wrote the defaults over it
   (McFlow_UpdateNewSave, Save_SetDefaults); from the menus it stayed in memory until the player saved. Now:
   - from the start, and from SELECT held before the title screen was first left (the logos, the title: no save in use
     yet), it is done as the title screen is left (src/menu/menu_a_c.c Title_Run), to the save the menus use;
   - from the menus and from Vita Settings, at once;
   - then the save file is written again with it: saves/card1/BASLUS-21678DBZT3/BASLUS-21678DBZT3, the save's 0x4000
     bytes with their checksum, as McFlow_UpdateSave writes them. Before that the file is copied beside it, read back:
     to ".bak" the first time (never written over: the file from before Unlock All first wrote it), to ".last.bak"
     each time after (the file just before this change). The file is not written when the save in use is not the
     file's (src/sys/mcflow_a.c McFlow_PortSaveIsFile: a new game the player chose not to save, which would go over
     the card's save), when there is no file yet (the game's next save keeps the unlock; from Vita Settings, leaving
     Options saves), when what is there is not a whole save with a right checksum (a damaged save: left alone), or
     while a card flow runs.
   The save as it was before the first unlock is also copied to UNLOCK_BACKUP (the first copy only), and Undo puts that
   copy back, written to the save file the same way; the copy is removed only once the save file has it. v1.2 to v1.3
   kept theirs in unlock_backup.bin, which v1.3 could fill with a new game's defaults (an unlock at the start with no
   save): that file is left as it is and Undo does not use it. Every step, with what the save held before and after,
   goes to the log. */
#define UNLOCK_BACKUP HOME "/saves/unlock_undo.bin"
#define UNLOCK_BACKUP_OLD HOME "/saves/unlock_backup.bin" /* (v1.2 to v1.3's: not used, not touched) */
#define UNLOCK_SAVE_SIZE 0x4000 /* SAVE_SIZE, include/sys/save.h */
#define UNLOCK_SAVE_FILE "/card1/BASLUS-21678DBZT3/BASLUS-21678DBZT3" /* port/src/plat_mc.c, src/battle/stgm_a_b.c McCardFile_SetNames */
#define UNLOCK_COUNTS 7 /* SAVE_PORT_COUNTS, include/sys/save.h */
static int sUnlock; /* chosen at the start: 1 unlock, 2 undo; done as the title screen is left */
static int sTitleLeft; /* the title screen was left once (Vita_UnlockAtTitle): there is a save in use */
static int sSelectSeen; /* SELECT down at one of the start's steps (start_step): it need not be held to the screen */
static volatile int sTextPause; /* the menus' unlock screen is up: the game waits (the watchdog leaves it alone) */
static volatile int sUnlockChanged; /* the screen asked from Vita Settings changed the save: leaving Options saves */

static int file_there(const char *path) {
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

static int unlock_es(void) {
#ifdef PORT_VITA_BT4
    extern int gVitaBt4English; /* the Spanish release: the screen in Spanish (its font has no accents) */
    return !gVitaBt4English;
#else
    return 0;
#endif
}

/* The file read into buf (at most `size` bytes): the bytes read, size + 1 when the file is longer, < 0 when it cannot
   be opened */
static int unlock_read(const char *path, void *buf, int size) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    char extra;
    int n, more = 0;

    if (fd < 0) {
        return fd;
    }
    n = sceIoRead(fd, buf, size);
    if (n == size) {
        more = sceIoRead(fd, &extra, 1);
    }
    sceIoClose(fd);
    return more > 0 ? size + 1 : n;
}

/* buf written to the file, then read back: 1 when the file holds exactly those bytes */
static int unlock_write_file(const char *path, const void *buf, int size) {
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    void *back;
    int n;

    if (fd < 0) {
        return 0;
    }
    n = sceIoWrite(fd, buf, size);
    sceIoClose(fd);
    if (n != size || (back = malloc(size)) == NULL) {
        return 0;
    }
    n = unlock_read(path, back, size) == size && memcmp(back, buf, size) == 0;
    free(back);
    return n;
}

/* The save in memory written to the save file (above). 1 written; 0 not written (the reason in the log); -1 the write
   failed (the file as it was put back, or in the .bak). */
static int unlock_save_file(void *save) {
    extern int McFlow_PortBusy(void), McFlow_PortSaveIsFile(void); /* src/sys/mcflow_a.c */
    extern int Port_McOpenFiles(void);                             /* port/src/plat_mc.c */
    extern int McCard_Checksum(void *buf, int size, int write);    /* src/battle/stgm_a_b.c */
    const char *root = getenv("BT3_SAVES");
    char path[300], bak[320];
    void *old;
    int n;

    snprintf(path, sizeof(path), "%s" UNLOCK_SAVE_FILE, root != NULL ? root : HOME "/saves");
    snprintf(bak, sizeof(bak), "%s.bak", path);
    if (file_there(bak)) {
        snprintf(bak, sizeof(bak), "%s.last.bak", path); /* (the first copy is kept) */
    }
    if (!McFlow_PortSaveIsFile()) {
        logf_("vita: unlock all: the save in use is not the save file's (a new game not saved, or none loaded): the file left"
              " alone; the game's next save keeps the unlock\n");
        return 0;
    }
    if (McFlow_PortBusy() || Port_McOpenFiles() != 0) {
        logf_("vita: unlock all: the save file not written now (a card flow is running): the game's next save keeps it\n");
        return 0;
    }
    if ((old = malloc(UNLOCK_SAVE_SIZE)) == NULL) {
        return -1;
    }
    n = unlock_read(path, old, UNLOCK_SAVE_SIZE);
    if (n < 0) {
        logf_("vita: unlock all: no save file yet (%s, %08x): the game's next save keeps it\n", path, (unsigned)n);
        free(old);
        return 0;
    }
    if (n != UNLOCK_SAVE_SIZE || McCard_Checksum(old, UNLOCK_SAVE_SIZE, 0) != 0) {
        logf_("vita: unlock all: %s is not a whole save (%d bytes) or its checksum is wrong: left alone\n", path, n);
        free(old);
        return 0;
    }
    if (!unlock_write_file(bak, old, UNLOCK_SAVE_SIZE)) {
        logf_("vita: unlock all: the save file's copy %s could not be written: the save file left alone\n", bak);
        free(old);
        return 0;
    }
    McCard_Checksum(save, UNLOCK_SAVE_SIZE, 1); /* (as the game does before each save: McFlow_Start) */
    if (!unlock_write_file(path, save, UNLOCK_SAVE_SIZE)) {
        n = unlock_write_file(path, old, UNLOCK_SAVE_SIZE);
        logf_("vita: unlock all: writing %s failed; %s\n", path, n ? "the file as it was put back" : "the file as it was is in the .bak beside it");
        free(old);
        return -1;
    }
    logf_("vita: unlock all: written to the save file %s (the file as it was copied first to %s)\n", path, bak);
    free(old);
    return 1;
}

static void unlock_log_counts(const char *what, const int *a, const int *b) {
    logf_("vita: unlock all: %s: characters %d -> %d (of 161 bits; BT4's own, ids 165..249, are always open), stages %d -> %d"
          " (of 64), songs %d -> %d (of 32), dragon flags %d -> %d (of 7), items %d -> %d (of 350), Dragon History slots"
          " %d -> %d (of 9), Zenni %d -> %d\n",
          what, a[0], b[0], a[1], b[1], a[2], b[2], a[3], b[3], a[4], b[4], a[5], b[5], a[6], b[6]);
}

/* `what` (1 unlock, 2 undo) done to `save`, then written to the save file. 0 nothing done; 1 done and in the save
   file; 2 done in the game only (the save file not written: unlock_save_file's reason in the log). */
static int unlock_apply(int what, void *save) {
    extern void Save_PortCount(const void *opt, int *count); /* src/sys/save.c */
    extern void Save_PortUnlockAll(void *opt);
    int before[UNLOCK_COUNTS], after[UNLOCK_COUNTS], w;

    if (file_there(UNLOCK_BACKUP_OLD)) {
        logf_("vita: unlock all: %s (v1.2 to v1.3's copy) is left as it is, not used\n", UNLOCK_BACKUP_OLD);
    }
    if (what == 1) {
        if (file_there(UNLOCK_BACKUP)) {
            logf_("vita: unlock all: the copy from before the first unlock stays in %s\n", UNLOCK_BACKUP);
        } else if (unlock_write_file(UNLOCK_BACKUP, save, UNLOCK_SAVE_SIZE)) {
            logf_("vita: unlock all: the save as it was copied to %s\n", UNLOCK_BACKUP);
        } else {
            sceIoRemove(UNLOCK_BACKUP); /* (ours, just made and not whole) */
            logf_("vita: unlock all: not done, the save's copy %s could not be written\n", UNLOCK_BACKUP);
            return 0;
        }
        Save_PortCount(save, before);
        Save_PortUnlockAll(save);
        Save_PortCount(save, after);
        unlock_log_counts("given to the save", before, after);
    } else if (what == 2) {
        void *old = malloc(UNLOCK_SAVE_SIZE);
        int n = old != NULL ? unlock_read(UNLOCK_BACKUP, old, UNLOCK_SAVE_SIZE) : -1;
        if (n != UNLOCK_SAVE_SIZE) {
            logf_("vita: unlock all: the undo failed, the copy %s could not be read (%d)\n", UNLOCK_BACKUP, n);
            free(old);
            return 0;
        }
        Save_PortCount(save, before);
        memcpy(save, old, UNLOCK_SAVE_SIZE);
        Save_PortCount(save, after);
        free(old);
        unlock_log_counts("undone, the save as it was before put back", before, after);
    } else {
        return 0;
    }
    w = unlock_save_file(save);
    if (what == 2) {
        if (w == 1) {
            sceIoRemove(UNLOCK_BACKUP);
            logf_("vita: unlock all: the copy %s removed (the save file has it now)\n", UNLOCK_BACKUP);
        } else {
            logf_("vita: unlock all: the copy %s kept (the save file does not have it yet)\n", UNLOCK_BACKUP);
        }
    }
    return w == 1 ? 1 : 2;
}

/* The screen's text and its choice: 1 unlock, 2 undo (only with the copy there), 0 nothing. `now`: from the menus. */
static int unlock_ask(int now) {
    int es = unlock_es(), there = file_there(UNLOCK_BACKUP);
    unsigned b;

    psvDebugScreenPrintf("\e[H\e[2J\n\e[33m%s\e[39m\n\n", es ? "Desbloquear todo (truco)" : "Unlock All (cheat)");
    psvDebugScreenPrintf("%s\n  %s\n\n",
                         es ? (now ? "Todos los personajes y transformaciones, escenarios,\ncanciones y objetos, y al menos 4.850.000 Zenis,\n"
                                     "en tu partida ahora, y se escribe en su archivo de\nguardado. Antes se copia tu partida en"
                                   : "Todos los personajes y transformaciones, escenarios,\ncanciones y objetos, y al menos 4.850.000 Zenis,\n"
                                     "en tu partida al dejar la pantalla de titulo, y se\nescribe en su archivo. Antes se copia tu partida en")
                            : (now ? "Every character and form, stage, song and item, and\nat least 4,850,000 Zenni, given to your save now and\n"
                                     "written to its file. Your save as it is now is\ncopied first to"
                                   : "Every character and form, stage, song and item, and\nat least 4,850,000 Zenni, given to your save as the\n"
                                     "title screen is left, and written to its file. Your\nsave as it is then is copied first to"),
                         UNLOCK_BACKUP);
    psvDebugScreenPrintf("%s\n", es ? "CROSS: desbloquear todo" : "CROSS: unlock everything");
    if (there) {
        psvDebugScreenPrintf("%s\n", es ? "TRIANGLE: deshacer: tu partida como era antes de\n  desbloquear (lo jugado desde entonces se pierde)"
                                        : "TRIANGLE: undo: your save as it was before the unlock\n  (what you played since then is lost)");
    }
    psvDebugScreenPrintf("%s\n", now ? (es ? "CIRCLE: volver al juego" : "CIRCLE: back to the game")
                                     : (es ? "CIRCLE o START: jugar sin cambios" : "CIRCLE or START: play without changes"));
    b = wait_buttons(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | (now ? 0 : SCE_CTRL_START) | (there ? SCE_CTRL_TRIANGLE : 0));
    return (b & SCE_CTRL_CROSS) ? 1 : (b & SCE_CTRL_TRIANGLE) ? 2 : 0;
}

/* SELECT looked at every 16 ms from the program's first steps to the unlock screen, on a thread of its own: a short
   press at any moment of the start counts (the steps alone missed a press between two of them). */
static volatile int sSelectWatch = 1;
static int select_watch(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    while (sSelectWatch) {
        if (buttons() & SCE_CTRL_SELECT) {
            sSelectSeen = 1;
        }
        sceKernelDelayThread(16000);
    }
    return 0;
}

static void select_watch_start(void) {
    SceUID th = sceKernelCreateThread("bt3_select", select_watch, 0x10000100, 0x1000, 0, 0, NULL);
    if (th >= 0) {
        sceKernelStartThread(th, 0, NULL);
    }
}

/* At the start (SELECT down at any moment of it, or now): the choice is done as the title screen is left */
static void unlock_menu(void) {
    sSelectWatch = 0;
    if (!sSelectSeen && !(buttons() & SCE_CTRL_SELECT)) {
        return;
    }
    logf_("vita: start: the unlock screen\n");
    sUnlock = unlock_ask(0);
    logf_("vita: start: unlock screen: %s\n", sUnlock == 1 ? "unlock (done as the title screen is left)"
                                              : sUnlock == 2 ? "undo (done as the title screen is left)" : "nothing");
    psvDebugScreenPrintf("\e[H\e[2J");
}

/* src/menu/menu_a_c.c Title_Run, as the title screen is left for the menus: the start's choice done to the save in use
   (the one loaded at boot, or the one New Game has just made) */
void Vita_UnlockAtTitle(void) {
    extern void *gSaveData;
    int what = sUnlock, r;

    sTitleLeft = 1;
    if (what == 0 || gSaveData == NULL) {
        return;
    }
    sUnlock = 0;
    r = unlock_apply(what, gSaveData);
    logf_("vita: start: the unlock screen's choice done as the title screen is left: %s\n",
          r == 1 ? "in the save and its file" : r == 2 ? "in the save (its file not written: above)" : "not done");
}

/* From the menus (Vita_VBlank, on the game's thread between two of its pictures): the game waits while the text
   screen is up, and its next picture takes the screen back. `options`: asked from Vita Settings. */
static void unlock_in_menus(int options) {
    extern void *gSaveData;
    int what, r = 0, es = unlock_es();

    if (gSaveData == NULL) {
        return;
    }
    sTextPause = 1;
    logf_("vita: menus: the unlock screen (%s)\n", options ? "Vita Settings" : "SELECT held");
    Vita_TextScreen();
    if (!sTitleLeft) {
        /* (the logos or the title: the save in use is not settled yet, New Game would write the defaults over it) the
           choice is kept, and done as the title screen is left, as the start's is */
        what = unlock_ask(0);
        if (what != 0) {
            sUnlock = what;
        }
        logf_("vita: menus: unlock screen before the title was left: %s\n", what == 1 ? "unlock (done as the title screen is left)"
                                                                         : what == 2 ? "undo (done as the title screen is left)" : "nothing");
        if (what != 0) {
            psvDebugScreenPrintf("\n\e[32m%s\e[39m\n", es ? "Se hara al dejar la pantalla de titulo." : "It is done as the title screen is left.");
            sceKernelDelayThread(2000000);
        }
        while (buttons() & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE | SCE_CTRL_SQUARE | SCE_CTRL_SELECT | SCE_CTRL_START)) {
            sceKernelDelayThread(20000);
        }
        psvDebugScreenPrintf("\e[H\e[2J");
        sTextPause = 0;
        return;
    }
    what = unlock_ask(1);
    if (what != 0) {
        r = unlock_apply(what, gSaveData);
    }
    if (r != 0 && options) {
        sUnlockChanged = 1;
    }
    logf_("vita: menus: unlock screen: %s, %s\n", what == 1 ? "unlock" : what == 2 ? "undo" : "nothing",
          r == 1 ? "in the save and its file" : r == 2 ? "in the save (its file not written: above)" : "nothing changed");
    if (what != 0) {
        if (r == 1) {
            psvDebugScreenPrintf("\n\e[32m%s\e[39m\n", es ? "Hecho, y escrito en tu archivo de guardado." : "Done, and written to your save file.");
        } else if (r == 2) {
            psvDebugScreenPrintf("\n\e[32m%s\e[39m\n", es ? "Hecho en el juego. Guarda en Opciones para\nconservarlo (el archivo no se escribio ahora)."
                                                          : "Done in the game. Save in Options to keep it\n(the save file was not written now).");
        } else {
            psvDebugScreenPrintf("\n\e[31m%s\e[39m\n", es ? "No se hizo: mira " HOME "/log" SFX : "Not done: see " HOME "/log" SFX);
        }
        sceKernelDelayThread(2000000);
    }
    while (buttons() & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE | SCE_CTRL_SQUARE | SCE_CTRL_SELECT | SCE_CTRL_START)) {
        sceKernelDelayThread(20000); /* (all let go first: the press that chose is not the menu's too) */
    }
    psvDebugScreenPrintf("\e[H\e[2J");
    sTextPause = 0;
}

/* src/menu/menu_x_c.c, Vita Settings: SQUARE asks for the unlock screen; whether it changed the save (then leaving
   Options runs the game's own save) */
void Vita_UnlockFromOptions(void) {
    sUnlockAsk = 1;
}

int Vita_UnlockTakeChanged(void) {
    int c = sUnlockChanged;
    sUnlockChanged = 0;
    return c;
}

/* ---- Tag Team offline: the setup of a local room (one player and CPUs) ----
   bt3.txt tagteam=1 (the default): the main menu's Tag Team entry is BT4decomp's own (src/menu/menu_a.c item 4) and
   opens its own Tag Team screens (src/menu/port_tbo.c), drawn by the Vita since its pictures and TrueType lines are
   (port/vita/vita_tbo.c). tagteam=2, the earlier way, kept as a fallback: the entry after item 3 opens the port's
   screen mode (src/menu/port_screen.c), and this text screen takes the online hub's place: the mode, the time limit
   and the CPUs' difficulty, then the room is opened as the PC's offline test does (port_tbo.c Tb_TagUpdate) and the
   game's own character select follows. docs/vita/tagteam.md */
int Vita_TagMenu(void) { return sTagMenu; }

int Vita_TagSetup(void) {
    extern void Port_TagCPUOptions(int fill, int difficulty), Port_TagModeOptions(int free_for_all, int quick);
    extern int Port_TagConnect(const char *server), Port_TagTeams(int left, int right), Port_TagSlots(void);
    extern int Port_TagSlot(int seat, int cpu, int difficulty), Port_TagPhase(void);
    extern void Port_TagOpen(int size, int limit, int delay, const char *name, unsigned long long content);
    extern void Port_TagCommand(int command, int a, int b), Port_TagClose(void);
    static const char *const kModeEn[5] = {"2 vs 2", "3 vs 3 (experimental)", "4 vs 4 (experimental)", "5 vs 5 (experimental)", "Free for All, 6 (experimental)"};
    static const char *const kModeEs[5] = {"2 contra 2", "3 contra 3 (experimental)", "4 contra 4 (experimental)", "5 contra 5 (experimental)", "Todos contra todos, 6 (experimental)"};
    static const char *const kTimeEn[5] = {"60 s", "90 s", "180 s", "240 s", "No limit"};
    static const char *const kTimeEs[5] = {"60 s", "90 s", "180 s", "240 s", "Sin limite"};
    static const char *const kDiffEn[5] = {"Very easy", "Easy", "Normal", "Hard", "Very hard"};
    static const char *const kDiffEs[5] = {"Muy facil", "Facil", "Normal", "Dificil", "Muy dificil"};
    static int mode, timeIdx = 2, diff = 2, row;
    int es = unlock_es(), i, ffa, n, ok;
    unsigned b;

    sTextPause = 1;
    logf_("vita: menus: the Tag Team setup\n");
    Vita_TextScreen();
    for (;;) {
        psvDebugScreenPrintf("\e[H\e[2J\n\e[33m%s\e[39m\n\n", es ? "Tag Team (sin conexion): tu y la CPU" : "Tag Team (offline): you and CPU fighters");
        psvDebugScreenPrintf("%s %-12s %s\n", row == 0 ? "\e[36m>" : " ", es ? "Modo" : "Mode", (es ? kModeEs : kModeEn)[mode]);
        psvDebugScreenPrintf("%s %-12s %s\n", row == 1 ? "\e[36m>" : "\e[39m ", es ? "Tiempo" : "Time limit", (es ? kTimeEs : kTimeEn)[timeIdx]);
        psvDebugScreenPrintf("%s %-12s %s\n\e[39m\n", row == 2 ? "\e[36m>" : "\e[39m ", es ? "CPU" : "CPU level", (es ? kDiffEs : kDiffEn)[diff]);
        psvDebugScreenPrintf("%s\n\n", es ? "Elegiras a tu luchador y a los de la CPU, luego el\nescenario. L3 (panel trasero, abajo a la izquierda)\ncambia de rival."
                                          : "You choose your fighter and the CPUs' ones, then the\nstage. L3 (the rear pad, lower left) changes target.");
        psvDebugScreenPrintf("%s\n", es ? "ARRIBA / ABAJO: opcion   IZQUIERDA / DERECHA: cambiar\nCROSS: empezar   CIRCLE: volver"
                                        : "UP / DOWN: row   LEFT / RIGHT: change\nCROSS: start   CIRCLE: back");
        b = wait_buttons(SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_CROSS | SCE_CTRL_CIRCLE);
        if (b & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE)) {
            break;
        }
        if (b & SCE_CTRL_UP) row = (row + 2) % 3;
        if (b & SCE_CTRL_DOWN) row = (row + 1) % 3;
        if (b & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) {
            int d = (b & SCE_CTRL_LEFT) ? 4 : 1;
            if (row == 0) mode = (mode + d) % 5;
            if (row == 1) timeIdx = (timeIdx + d) % 5;
            if (row == 2) diff = (diff + d) % 5;
        }
    }
    ok = 0;
    if (b & SCE_CTRL_CROSS) {
        /* as port_tbo.c Tb_TagUpdate's offline room: CPUs in every other place */
        ffa = mode == 4;
        n = ffa ? 3 : mode + 2;
        Port_TagCPUOptions(1, diff);
        if (Port_TagConnect(NULL)) {
            Port_TagModeOptions(ffa, 0);
            Port_TagOpen(ffa ? 3 : 2, timeIdx, 0, "Player", 0);
            Port_TagTeams(n, n);
            for (i = 1; i < Port_TagSlots(); i++) {
                Port_TagSlot(i, 1, diff);
            }
            Port_TagCommand(3 /* TTW_SELECT */, 0, 0);
            ok = Port_TagPhase() == 2; /* TT_SELECT */
        }
        if (!ok) {
            Port_TagClose();
        }
    }
    logf_("vita: menus: Tag Team setup: %s (mode %d, time %d, CPU %d)\n", ok ? "start" : "back", mode, timeIdx, diff);
    while (buttons() & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE)) {
        sceKernelDelayThread(20000); /* (let go first: the press is not the next screen's too) */
    }
    psvDebugScreenPrintf("\e[H\e[2J");
    sTextPause = 0;
    return ok ? 2 : 0;
}

static void start_screen(void) {
#ifdef PORT_VITA_RELEASE
    /* The released program: no start screen. The game from its start (logos, title, menus). */
    sFloatChoice = 1;
    if (sMode < 0) {
        sMode = 0;
    }
#else
    unsigned ns[2][7], diff[7];
    const char *names[7];
    int bad, i;

    psvDebugScreenPrintf("\e[33mTenkaichi3Decomp, PS Vita development build\e[39m (" __DATE__ ")\n\n");
    /* The vector library's arithmetic (vita_float.c): the exact model's sums against the PC's, and the hardware
       model against the exact one. The game's own float code uses the processor's float unit directly. */
    psvDebugScreenPrintf("Float self-test, please wait...\n");
    bad = Vita_FloatSelfTest(now_us, ns, diff, names);
    psvDebugScreenPrintf("\n  operation         exact model   hardware model   results that differ\n");
    logf_("vita: float self-test: %d of 7 exact sums differ from the PC's\n", bad);
    for (i = 0; i < 7; i++) {
        psvDebugScreenPrintf("  %-16s %7u ns     %7u ns        %u per 1000\n", names[i], ns[0][i], ns[1][i], diff[i]);
        logf_("vita:   %-16s exact %u ns, hardware %u ns, %u per 1000 differ\n", names[i], ns[0][i], ns[1][i], diff[i]);
    }
    psvDebugScreenPrintf(bad == 0 ? "\n\e[32mExact model: the same results as on the PC.\e[39m\n"
                                  : "\n\e[31mExact model: some sums differ from the PC's.\e[39m\n");
    sFloatChoice = 1;
#endif
    if (sMode < 0) {
        psvDebugScreenPrintf("\nCROSS: the game from its start\nCIRCLE: the demo fight (computer against computer)\nSTART: quit\n\n"
                             "Hold START + SELECT for two seconds to stop. The log is " HOME "/log" SFX ".\n");
        switch (wait_buttons(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START)) {
            case SCE_CTRL_CROSS: sMode = 0; break;
            case SCE_CTRL_CIRCLE: sMode = 1; break;
            default: quit();
        }
    }
    if (sMode == 1) {
        setenv("BT3_DEMO", "1", 1);
        if (sM0 && getenv("BT3_DEMO_PICK") == NULL) { /* (m0=1: the attract demo's own pairing, as the PC's BT3_DEMO) */
            setenv("BT3_DEMO_PICK", "-1", 1);
        }
        if (getenv("BT3_DEMO_PICK") == NULL && kDemoPick >= 0) { /* (one pairing, the heavy one, while it is worked on) */
            char b[8];
            snprintf(b, sizeof(b), "%d", kDemoPick);
            setenv("BT3_DEMO_PICK", b, 1);
        }
        if (getenv("BT3_DEMO_PICK") == NULL) { /* the demo's nine pairings in turn, one per run */
            FILE *fp = fopen(HOME "/demo_next.txt", "r");
            int n = 0;
            char b[8];
            if (fp != NULL) {
                if (fscanf(fp, "%d", &n) != 1) {
                    n = 0;
                }
                fclose(fp);
            }
            if (n < 0 || n > 8) {
                n = 0;
            }
            snprintf(b, sizeof(b), "%d", n);
            setenv("BT3_DEMO_PICK", b, 1);
            fp = fopen(HOME "/demo_next.txt", "w");
            if (fp != NULL) {
                fprintf(fp, "%d\n", (n + 1) % 9);
                fclose(fp);
            }
        }
        logf_("vita: demo pairing %s of 0..8 (one per run, in turn: %s/demo_next.txt)\n", getenv("BT3_DEMO_PICK"), HOME);
    }
    gPortFastFloat = sFloatChoice;
    psvDebugScreenPrintf("\e[2J\e[H");
}

/* The fourth core is the system's; an application's threads may run there only with the CapUnlocker
   kernel plugin (taiHEN, *KERNEL). A helper thread is made there when the system lets it, else on `fallbackMask`. */
int gVitaCore3 = -1; /* 1: the fourth core takes our threads, 0: it does not, -1: not tried yet */
SceUID Vita_ThreadOnCore3(const char *name, SceKernelThreadEntry entry, int prio, int stack, int fallbackMask) {
    SceUID th = -1;
    if (gVitaCore3 != 0) {
        th = sceKernelCreateThread(name, entry, prio, stack, 0, 0x00080000, NULL);
        if (gVitaCore3 < 0) {
            gVitaCore3 = th >= 0;
            logf_("vita: the fourth core %s (%08x)\n", th >= 0 ? "takes our helper threads (the CapUnlocker plugin is in)" :
                  "is the system's alone (no CapUnlocker plugin): the helper threads share the first three", th >= 0 ? 0u : (unsigned)th);
        }
    }
    if (th < 0) {
        th = sceKernelCreateThread(name, entry, prio, stack, 0, fallbackMask, NULL);
    }
    return th;
}

/* For the menus (Options > Controller, the "Vita Settings" row's guide line): 1 the fourth core takes our threads
   (the CapUnlocker plugin is in), 0 it does not, -1 not tried yet. */
int Vita_Core3State(void) {
    return gVitaCore3;
}

/* The threads that matter, as the system sees them (for the log): processor time, where they ran, how often
   something else took their core. */
#define NWATCH 10 /* (0 game, 1 list reader, 2 drawing, 3 sound, 4 sampler, 5 render, 6 log, 7 strips, 8 reads, 9 the hitch recorder) */
static struct { int id; const char *name; unsigned long long run; unsigned pre, ipre; int chg; } sWatch[NWATCH];
int Vita_WatchGet(int slot, int *id, const char **name) { /* (vita_hitch.c) */
    if (slot < 0 || slot >= NWATCH || sWatch[slot].id <= 0) {
        return 0;
    }
    *id = sWatch[slot].id;
    *name = sWatch[slot].name;
    return 1;
}
unsigned Vita_UpMs(void) { /* (the log's own clock: ms since the start) */
    return (unsigned)((now_us() - sStart) / 1000);
}
void Vita_WatchThread(int slot, int id, const char *name) {
    if (slot >= 0 && slot < NWATCH) {
        sWatch[slot].id = id;
        sWatch[slot].name = name;
    }
}
static void watch_log(void) {
    char line[1300];
    int n = 0, i;
    for (i = 0; i < NWATCH; i++) {
        SceKernelThreadInfo ti;
        if (sWatch[i].id <= 0) {
            continue;
        }
        memset(&ti, 0, sizeof(ti));
        ti.size = sizeof(ti);
        if (sceKernelGetThreadInfo(sWatch[i].id, &ti) < 0) {
            continue;
        }
        n += snprintf(line + n, sizeof(line) - (size_t)n, "%s%s %u ms on core %d (mask %x, priority %d), %u times put aside, %u by interrupts, %d moves", n ? "; " : "",
                      sWatch[i].name, (unsigned)((ti.runClocks - sWatch[i].run) / 1000), (int)ti.lastExecutedCpuId, (unsigned)ti.currentCpuAffinityMask, (int)ti.currentPriority,
                      ti.threadPreemptCount - sWatch[i].pre, ti.intrPreemptCount - sWatch[i].ipre, (int)ti.changeCpuCount - sWatch[i].chg);
        sWatch[i].run = ti.runClocks;
        sWatch[i].pre = ti.threadPreemptCount;
        sWatch[i].ipre = ti.intrPreemptCount;
        sWatch[i].chg = (int)ti.changeCpuCount;
        if (n > (int)sizeof(line) - 200) {
            break;
        }
    }
    if (n > 0) {
        logf_("vita:   threads: %s\n", line);
    }
}

/* The watchdog: when the drawing thread has made no picture for three seconds, where every thread is
   (its state, what it waits on) and where the drawing and render threads are in their work, written to a file of its
   own straight away (not through the log thread, which may be what is stuck) and to the log. */
static int watchdog_thread(SceSize args, void *argp) {
    extern unsigned gGsFrame;
    extern void Vita_VglState(char *b, int n);
    unsigned last = 0, still = 0, dumps = 0;
    (void)args;
    (void)argp;
    for (;;) {
        sceKernelDelayThread(500000);
        {   /* a line a minute for bug reports, in the released program too: the picture rate and the memory in use */
            static unsigned ticks, drawn0, minutes;
            extern unsigned gVitaGsDrawn;
            extern int gVitaInMenus;
            if (++ticks % 120 == 0 && sLogThreaded) {
                struct mallinfo mi = mallinfo();
                unsigned d = gVitaGsDrawn - drawn0;
                drawn0 = gVitaGsDrawn;
                minutes++;
                char tables[200];
                extern int Vita_VglTables(char *out, int size);
                Vita_VglTables(tables, sizeof(tables));
                logf_("vita: minute %u: %u.%u pictures/s, in the %s; heap %u KB; %s\n", minutes, d / 60, (d % 60) * 10 / 60,
                      gVitaInMenus ? "menus" : "fights", (unsigned)mi.uordblks >> 10, tables);
            }
        }
        {   /* (a BT3_PGO=gen build: its counts so far now and then, vita_pgo.c) */
            extern void Vita_PgoTick(void);
            Vita_PgoTick();
        }
        if (gGsFrame != last || sTextPause) { /* (the menus' unlock screen up: the game waits on purpose) */
            last = gGsFrame;
            still = 0;
            continue;
        }
#ifndef PORT_VITA_RELEASE
        if (gVitaHangDump > 0 && last != 0 && sLogThreaded && (still + 1) / 2 >= (unsigned)gVitaHangDump) {
            /* bt3.txt hangdump=<s>: the program stopped on purpose after that long without a picture, so that the
               system writes its crash dump (ux0:data/psp2core-*.psp2dmp) with every thread's registers and stack:
               where a hung thread is, with vita-parse-core and bt3.unstripped.elf */
            logf_("vita: hangdump: no picture for %d s: the program is stopped on purpose for a crash dump\n", gVitaHangDump);
            log_sync();
            sceKernelDelayThread(500000);
            *(volatile int *)0 = 0x48414e47;
        }
#endif
        if (last == 0 || !sLogThreaded || ++still % 6 != 0 || dumps >= 4) { /* (not before the first picture, nor at the end) */
            continue;
        }
        dumps++;
        {
            static char out[3000];
            int n = 0, i;
            SceUID fd;
            extern int gVitaInMenus;
            n += snprintf(out + n, sizeof(out) - (size_t)n, "vita: WATCHDOG: no picture for %u s (picture %u) after %u s of play, in the %s; log %u written, %u kept, %u lost\n",
                          still / 2, gGsFrame, (unsigned)((now_us() - sStart) / 1000000), gVitaInMenus ? "menus" : "fight", sLogTail, sLogHead, sLogLost);
            Vita_VglState(out + n, (int)sizeof(out) - n);
            n += (int)strlen(out + n);
            for (i = 0; i < NWATCH && n < (int)sizeof(out) - 300; i++) {
                SceKernelThreadInfo ti;
                if (sWatch[i].id <= 0) {
                    continue;
                }
                memset(&ti, 0, sizeof(ti));
                ti.size = sizeof(ti);
                if (sceKernelGetThreadInfo(sWatch[i].id, &ti) < 0) {
                    continue;
                }
                n += snprintf(out + n, sizeof(out) - (size_t)n, "vita: WATCHDOG:   %s: status %x, priority %d, core %d (last %d), waits for %x (object %x), run %u ms\n", sWatch[i].name,
                              (unsigned)ti.status, (int)ti.currentPriority, (int)ti.currentCpuId, (int)ti.lastExecutedCpuId, (unsigned)ti.waitType, (unsigned)ti.waitId,
                              (unsigned)(ti.runClocks / 1000));
            }
            fd = sceIoOpen(HOME "/hang" SFX, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
            if (fd >= 0) {
                sceIoWrite(fd, out, (SceSize)n);
                sceIoClose(fd);
            }
            logf_("%s", out);
        }
    }
    return 0;
}

static int game_thread(SceSize args, void *argp) {
    static char *argv[] = {"bt3", NULL};

    (void)args;
    (void)argp;
    Vita_FloatMode(gPortFastFloat); /* the float unit's mode belongs to the thread */
    {
        extern int gVitaGameThreadId;
        gVitaGameThreadId = sceKernelGetThreadId();
    }
    {
        extern void Vita_PerfThreadStart(void);
        Vita_PerfThreadStart();
    }
    exit(__real_main(1, argv));
    return 0;
}

/* The start's steps, in the log (the released program's too): a start that ends early ends the log at the step it
   was in. Until the game's thread starts the log is written line by line (the log thread's tenth of a second could
   lose the last lines before a crash). arielxflores's, from his v1.2.1 (882e0fe). */
static void start_step(const char *what) {
    logf_("vita: start: %s\n", what);
    sSelectSeen |= (buttons() & SCE_CTRL_SELECT) != 0; /* (Unlock All: SELECT pressed at any step of the start counts) */
}

/* The plugins taiHEN loads (their file names, from ur0:tai and ux0:tai config.txt), in the log: a start that fails on
   some Vitas only is often a plugin's doing. */
static void log_plugins(void) {
    static const char *const kConfig[2] = {"ur0:tai/config.txt", "ux0:tai/config.txt"};
    char line[256], out[700];
    size_t n = 0;
    int k;
    out[0] = '\0';
#define PLUG_PUT(...) do { if (n < sizeof(out) - 1) { int w = snprintf(out + n, sizeof(out) - n, __VA_ARGS__); \
                              n = w < 0 ? n : (n + (size_t)w < sizeof(out) - 1 ? n + (size_t)w : sizeof(out) - 1); } } while (0)
    for (k = 0; k < 2; k++) {
        FILE *f = fopen(kConfig[k], "r");
        if (f == NULL) {
            continue;
        }
        PLUG_PUT("%s%s:", n ? "; " : "", kConfig[k]);
        while (fgets(line, sizeof(line), f) != NULL) {
            char *q = line, *e, *name;
            while (*q == ' ' || *q == '\t') {
                q++;
            }
            if (*q == '#' || (*q != '*' && strstr(q, ".skprx") == NULL && strstr(q, ".suprx") == NULL)) {
                continue;
            }
            for (e = q; *e != '\0' && *e != '\r' && *e != '\n' && *e != ' '; e++) {
            }
            *e = '\0';
            if (*q == '*') { /* (the section: *KERNEL, *main, *ALL, a title's id) */
                PLUG_PUT(" [%s]", q);
                continue;
            }
            name = strrchr(q, '/');
            name = name != NULL ? name + 1 : q;
            PLUG_PUT(" %s", name);
        }
        fclose(f);
    }
#undef PLUG_PUT
    logf_("vita: plugins: %s\n", out[0] ? out : "(no taiHEN config.txt readable)");
}

int __wrap_main(int argc, char **argv) {
    SceUID th;
    int logThread = 0, homeMade;

    (void)argc;
    (void)argv;
    scePowerSetArmClockFrequency(444); /* the console's own top speed: no overclocking */
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE); /* a DualShock's sticks through the Ext2 reads (PS TV, a second pad) */
    psvDebugScreenInit();
    sceIoMkdir("ux0:data", 0777);
    homeMade = sceIoMkdir(HOME, 0777); /* (0 made now; else it is there already, or cannot be made: below) */
    sceIoMkdir(HOME "/mods", 0777); /* (where mods go: vita_file.c) */
#ifdef PORT_VITA_RELEASE
    {   /* the released program's log, appended to at every start, kept under 1 MB: a longer one becomes log-prev.txt
           (the one before it is let go) and this run starts a new one */
        SceIoStat st;
        if (sceIoGetstat(HOME "/log" SFX, &st) >= 0 && st.st_size > 1024 * 1024) {
            sceIoRemove(HOME "/log-prev" SFX);
            sceIoRename(HOME "/log" SFX, HOME "/log-prev" SFX);
        }
    }
#endif
    sLog = fopen(HOME "/log" SFX, "a");
    logf_("\n======== new run ========\n");
    {   /* the port's own folder: the log, the saves, the settings are kept there, wherever the disc's files are read from */
        SceIoStat st;
        if (sLog == NULL && (sceIoGetstat(HOME, &st) < 0 || !SCE_S_ISDIR(st.st_mode))) {
            Vita_Fatal("The folder " HOME " could not be made\n(error 0x%08X): the saves and the log go there.\n"
                       "Make it with VitaShell, then start the game again.", (unsigned)homeMade);
        }
        logf_("vita: data folder " HOME " (%s)\n", homeMade >= 0 ? "made now" : "there already");
    }
    {   /* the parts of a log split before (logsplit=): this run's own, if any, are made anew */
        int k;
        for (k = 2; k <= 9; k++) {
            char path[64];
            snprintf(path, sizeof(path), HOME "/log-part%d" SFX, k);
            sceIoRemove(path);
        }
    }
#ifdef PORT_VITA_RELEASE
    gVitaReport = 0;
    logf_("vita: Dragon Ball Z Budokai Tenkaichi 3 (Tenkaichi3Decomp) for the PS Vita, built " __DATE__ " " __TIME__ ", processor clock %d MHz\n", scePowerGetArmClockFrequency());
#else
    logf_("vita: Tenkaichi3Decomp development build of " __DATE__ " " __TIME__ ", processor clock %d MHz\n", scePowerGetArmClockFrequency());
#endif
    logf_("vita: clocks as set: processor %d MHz, bus %d MHz, graphics chip %d MHz, its crossbar %d MHz\n", scePowerGetArmClockFrequency(),
          scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
    {
        SceUID lt = Vita_ThreadOnCore3("bt3_log", log_thread, 180, 0x4000, 0x00020000 /* else the second core */);
        if (lt >= 0 && sceKernelStartThread(lt, 0, NULL) >= 0) {
            logThread = 1; /* (the log goes through it from the game's start on) */
            Vita_WatchThread(6, lt, "log");
        }
    }
    {   /* the watchdog: any core, above everything of ours */
        SceUID wd = sceKernelCreateThread("bt3_watchdog", watchdog_thread, 64, 0x4000, 0, 0, NULL);
        if (wd >= 0) {
            sceKernelStartThread(wd, 0, NULL);
        }
    }
    if (freopen(HOME "/out" SFX, "w", stdout) != NULL) { /* what the port's own code prints */
        setvbuf(stdout, NULL, _IOLBF, 0);
    }
    if (freopen(HOME "/err" SFX, "w", stderr) != NULL) {
        /* a line at a time, not a write per piece of each line: the memory card's writes are slow */
        setvbuf(stderr, NULL, _IOLBF, 1024);
    }
    setenv("BT3_DATA", HOME, 1);
    setenv("BT3_SAVES", HOME "/saves", 1);
    setenv("BT3_SETTINGS", HOME "/bt3_settings.txt", 1);
#ifndef PORT_VITA_RELEASE
    setenv("BT3_TRACE", "600", 1); /* the fight's state every 600 blanks into out.txt: comparable with a PC run */
#endif
    select_watch_start(); /* (Unlock All: SELECT pressed at any moment of the start) */
    log_plugins();
    start_step("settings");
    read_settings();
    {   /* (a BT3_PGO=gen build: where its counts go, vita_pgo.c) */
        extern void Vita_PgoInit(void);
        Vita_PgoInit();
    }
    start_screen();
#ifndef PORT_VITA_RELEASE /* (the released program shows nothing until the game's first picture; an error still shows) */
    psvDebugScreenPrintf("Reading the game's data tables from %s ...\n", gVitaDisc);
#endif
    start_step("the disc's files");
    find_disc();
    disc_found();
    start_step("the game's data tables");
    Vita_LoadGameData(gVitaDisc);
    {   /* mods (vita_file.c): the packs left out are told of before the game starts */
        extern const char *Vita_ModsStart(void);
        const char *note;
        start_step("mods");
        note = Vita_ModsStart();
        if (note != NULL) {
            logf_("vita: mods notice shown\n");
            psvDebugScreenPrintf("\n\e[33m%s\e[39m\n\nPress CROSS to go on.\n", note);
            wait_buttons(SCE_CTRL_CROSS);
            psvDebugScreenPrintf("\e[H\e[2J");
        }
    }
    start_step("the unlock screen");
    unlock_menu(); /* (SELECT held) */
    start_step("memory");
    Vita_MapMemory(sHeapCdram);
    logf_("vita: %s floats, game heap at 0x%08X, disc folder %s, %s\n", gPortFastFloat ? "hardware" : "exact", (unsigned)gVitaHeapBase, gVitaDisc,
          getenv("BT3_REPLAY") != NULL ? getenv("BT3_REPLAY") : "attract-demo fight");
    {   /* the network, only with bt3.txt online=1 or online_test (port/vita/vita_net.c): its start-up and self-test */
        extern int Vita_NetBoot(void);
        start_step("the network");
        if (Vita_NetBoot()) { /* online_test=2: the run ends after the self-test, as an exitafter run does */
            logf_("vita: exitafter: the network self-test ended the run (online_test=2)\n");
            quit();
        }
    }
    {   /* Ad Hoc's first check, only with bt3.txt adhoc_test (port/vita/vita_adhoc.c): on its own thread, later */
        extern void Vita_AdhocBoot(void);
        Vita_AdhocBoot();
    }
    sStart = now_us();
    {
        extern int Vita_PerfInit(void);
#ifdef PORT_VITA_RELEASE
        int r = gVitaReport ? Vita_PerfInit() : 0;
#else
        int r = sPerfOff ? 0 : Vita_PerfInit();
#endif
        logf_(r == 1 ? "vita: processor counters on (libperf)\n" : "vita: processor counters off (libperf kernel plugin not loaded: %08x)\n", r);
        { extern int gVitaPerfK; if (gVitaPerfK != 0) { logf_("vita: libperf's kernel plugin loaded from the app's folder: %08x\n", (unsigned)gVitaPerfK); } }
    }
    sLogThreaded = logThread;
    th = sceKernelCreateThread("bt3_game", game_thread, 0x10000100, GAME_STACK, 0, 0x00010000 /* the first core, to itself */, NULL);
    Vita_WatchThread(0, th, "game");
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
        Vita_Fatal("cannot start the game's thread (error 0x%08X)", (unsigned)th);
    }
    sceKernelWaitThreadEnd(th, NULL, NULL);
    for (;;) { /* the game's thread ends the program itself (exit); nothing to do here */
        sceKernelDelayThread(1000000);
    }
    return 0;
}
