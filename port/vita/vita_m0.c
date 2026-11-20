/*
 * Online play, milestone M0: the Vita's simulation compared with the PC's, offline (docs/vita/online-bt4.md "M0").
 *
 *   BT3_HASH=<file>   (bt3.txt, as on the PC) one line per vertical blank in the PC's format (port/src/gs/state.c
 *                     Port_StateLog): the blank, a checksum of the game's memory (0 here: the Vita's memory is laid out
 *                     differently, so it could not be compared anyway), the checksum of the fight's own values (XXH3,
 *                     the same as the PC's) and, during a fight, the values: both fighters' health and position (the
 *                     float bits), the battle clock and the C library generator's state; " pad <n>" while a pad
 *                     recording plays. port/vita/tools/m0_compare.py compares two such files (Vita and PC, or two runs).
 *   m0=1              (bt3.txt) a start like an online session's, as far as the offline test needs it:
 *                       - files read on the game's own thread (asyncread=0: a load then takes the same blanks every run);
 *                       - its own empty save folder, ux0:data/bt4/m0save (the player's saves are not touched: only
 *                         files in that folder are removed, and only when the folder has this mode's mark file);
 *                       - the sound's lengths counted in blanks (BT3_SOUND_TICKS=1, as a session does);
 *                       - the PC's key configuration (the console's default, Save_PortKeys in src/sys/save.c), not the
 *                         Vita's Normal (Ki Charge on L): the same pad then gives the same action on both;
 *                       - the attract demo's own pairing (Demo_SetupBattle, as the PC's BT3_DEMO), not the Vita's list;
 *                       - BT3_HASH=ux0:data/bt4/m0hash.txt unless the settings name another file.
 *                     The fixed seed is the PC's (port/src/plat_sys.c, BT3_SEED=<n> to change it), the inputs a recording
 *                     (BT3_PAD_PLAY=<file>, 18 bytes per pad read, or BT3_PAD_TABLE_PLAY=<file>, 36 bytes per blank,
 *                     port/src/plat_stub.c) or the demo's computer players.
 *   ps2float=0/1      the PS2 float model (only in the BT4_PS2FLOAT=1 build: vita_ps2float.c); with BT3_HASH, every
 *                     600 blanks a log line with the model's operations per blank.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#define XXH_INLINE_ALL
#include "../third_party/xxhash/xxhash.h"

#ifndef PORT_VITA_HOME
#define PORT_VITA_HOME "ux0:data/bt4"
#endif

extern void Vita_Log(const char *fmt, ...);

int gVitaSimExact; /* bt3.txt m0=1: the game as the PC computes it, where the Vita had its own way (see above) */

/* The mode's own save folder, emptied: only when it carries the mark this mode wrote (never another folder's files). */
static void m0_saves(const char *dir) {
    char mark[300], path[600];
    SceUID d;
    SceIoDirent e;
    int n = 0;

    snprintf(mark, sizeof(mark), "%s/M0_SCRATCH_FOLDER.txt", dir);
    sceIoMkdir(dir, 0777);
    {
        SceIoStat st;
        if (sceIoGetstat(mark, &st) >= 0 && (d = sceIoDopen(dir)) >= 0) {
            while (sceIoDread(d, &e) > 0) {
                if (SCE_S_ISDIR(e.d_stat.st_mode) || strcmp(e.d_name, "M0_SCRATCH_FOLDER.txt") == 0) {
                    continue;
                }
                snprintf(path, sizeof(path), "%s/%s", dir, e.d_name);
                if (sceIoRemove(path) >= 0) {
                    n++;
                }
            }
            sceIoDclose(d);
        }
    }
    {
        FILE *fp = fopen(mark, "w");
        if (fp != NULL) {
            fputs("bt3.txt m0=1: this folder is the online test's empty save, emptied at each start. Not a player's save.\n", fp);
            fclose(fp);
        }
    }
    Vita_Log("vita: m0: the save folder %s, emptied (%d files)\n", dir, n);
}

/* bt3.txt m0=<n>, after every setting is read (vita_main.c read_settings) */
void Vita_M0Start(int on) {
    extern int gVitaAsyncRead;

    gVitaSimExact = on != 0;
    if (!gVitaSimExact) {
        return;
    }
    gVitaAsyncRead = 0;
    setenv("BT3_SOUND_TICKS", "1", 1);
    if (getenv("BT3_HASH") == NULL) {
        setenv("BT3_HASH", PORT_VITA_HOME "/m0hash.txt", 1);
    }
    if (strcmp(getenv("BT3_SAVES") != NULL ? getenv("BT3_SAVES") : "", PORT_VITA_HOME "/saves") == 0) {
        setenv("BT3_SAVES", PORT_VITA_HOME "/m0save", 1); /* (a BT3_SAVES= line of the settings is kept as it is) */
    }
    if (strcmp(getenv("BT3_SAVES"), PORT_VITA_HOME "/saves") != 0) {
        m0_saves(getenv("BT3_SAVES"));
    }
    Vita_Log("vita: m0: the start of an online session (asyncread=0, sound by blanks, the PC's keys, saves %s, state log %s,"
             " seed %s)\n", getenv("BT3_SAVES"), getenv("BT3_HASH"), getenv("BT3_SEED") != NULL ? getenv("BT3_SEED") : "the PC's fixed one");
}

/* port/src/gs/state.c Port_StateLog, the PC's format; called at every vertical blank (port/src/headless.c Port_Trace) */
void Port_StateLog(unsigned vblank, const void *fight, unsigned fightSize) {
    static FILE *fp;
    static int mode = -1;

    if (mode < 0) {
        const char *path = getenv("BT3_HASH");
        mode = 0;
        if (path != NULL && (fp = fopen(path, "w")) != NULL) {
            setvbuf(fp, NULL, _IOFBF, 64 * 1024);
            mode = 1;
            Vita_Log("vita: state log %s (the PC's BT3_HASH format, the memory checksum 0)\n", path);
        }
    }
#ifdef BT4_PS2FLOAT_BUILD
    if (vblank % 600 == 0) { /* the model's operations per blank since the last line (the cost's other factor: vita_ps2float.c) */
        extern unsigned gVitaPfCount[8];
        extern int gVitaPs2Float;
        static unsigned last[8], lastBlank;
        unsigned d = vblank - lastBlank ? vblank - lastBlank : 1, k, now[8];
        for (k = 0; k < 8; k++) {
            now[k] = gVitaPfCount[k];
        }
        Vita_Log("ps2float=%d: per blank add %u sub %u mul %u div %u f2u %u u2f %u\n", gVitaPs2Float, (now[0] - last[0]) / d,
                 (now[1] - last[1]) / d, (now[2] - last[2]) / d, (now[3] - last[3]) / d, (now[4] - last[4]) / d, (now[5] - last[5]) / d);
        memcpy(last, now, sizeof(last));
        lastBlank = vblank;
    }
#endif
    if (mode != 1) {
        return;
    }
    fprintf(fp, "%u %016llx %016llx", vblank, 0ull, fight != NULL ? (unsigned long long)XXH3_64bits(fight, fightSize) : 0ull);
    if (fight != NULL) {
        const int *w = fight;
        fprintf(fp, " hp %d %d pos %08x %08x %08x  %08x %08x %08x clock %d rnd %08x%08x", w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[11], w[10]);
    }
    {
        extern long Port_PadPlayPos(void); /* plat_stub.c: how far a pad recording has been played */
        long pos = Port_PadPlayPos();
        if (pos >= 0) {
            fprintf(fp, " pad %ld", pos);
        }
    }
    fputc('\n', fp);
    if (vblank % 600 == 0) {
        fflush(fp);
    }
}
