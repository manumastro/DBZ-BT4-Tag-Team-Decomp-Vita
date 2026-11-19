/*
 * PS Vita: what BT4decomp's network files (gs/net.c, net_chat.c, net_relay.c, net_match.c, net_lobby.c,
 * build_identity.c, plat_fprint.c, match_supabase.cpp) call that the PC takes from files the Vita does not build:
 * the PC renderer's waits, its F1 window's lines, voice chat, the update downloader, the old Worker's signature, the
 * process restart. Only in a build with BT4_VITA_NETLINK=1 (online milestone M2a, docs/vita/online-bt4.md); there
 * vita_bt4.c's stand-ins of those files are renamed away (vita_netlink_rename.h) and upstream's own run.
 *
 * Each answer here is the Vita's for now: a log line where the PC would show something (the banner and the notice:
 * vita_netline.c), nothing for voice.
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voice.h"

extern void Vita_Log(const char *fmt, ...);
extern char gVitaDisc[];
extern int Update_OnlineRequired(void); /* vita_net.c: the server said UPDATE_REQUIRED */

/* ---- the PC renderer (gs_draw.c): the last picture held while the session waits ---- */
void Gs_HoldFrame(void) {
    extern void Vita_NetLineHold(void); /* (vita_netline.c, bt3.txt online_hold: the banner over the picture on show) */
    Vita_NetLineHold();
}
void Gs_NetworkWait(int waiting) { (void)waiting; }

/* ---- the F1 window's lines (ui.cpp): Port_UiNotice and Port_UiBanner are vita_netline.c's (drawn the Vita's way) ---- */
void Port_UiDragonBallFound(const char *finder, unsigned stars) {
    Vita_Log("net: Dragon Ball found (%u stars) by %s\n", stars, finder != NULL ? finder : "?");
}

/* ---- menu translations (menu_translation.c): the English text ---- */
const char *MenuTranslation_PortText(const char *id) {
    (void)id;
    return NULL;
}

/* ---- voice chat (voice.cpp): none on the Vita ---- */
int PortVoice_NewKey(unsigned char out[32]) {
    memset(out, 0, 32);
    return 0;
}
void PortVoice_DuelKey(const unsigned char key[32], int seat, const char *name) {
    (void)key;
    (void)seat;
    (void)name;
}
void PortVoice_Leave(int mode) { (void)mode; }
void PortVoice_P2PSession(int mode, const char *descriptor, const PortVoiceRoomMember *members, int count) {
    (void)mode;
    (void)descriptor;
    (void)members;
    (void)count;
}

/* ---- the update downloader (net_update.c): the Vita has none; the gate is the server's answer ---- */
int Update_BlocksOnline(void) { return Update_OnlineRequired(); }
const char *Update_OnlineNote(void) {
    return Update_OnlineRequired() ? "Online play needs a newer version of the PS Vita port. Install the latest release." : NULL;
}

/* ---- the old Worker's request signature (net_sign.c): that path is not used ---- */
int Sign_HasKey(void) { return 0; }
int Sign_Request(const char *method, const char *path, int release, long long when, const char *body, char sig[65]) {
    (void)method;
    (void)path;
    (void)release;
    (void)when;
    (void)body;
    sig[0] = '\0';
    return 0;
}
void Sign_SelfTest(void) {}

/* ---- the session's start and end (net.c relaunch): the PC sets its BT3_NET_* environment and starts the program
   again (execv). The Vita does the same with a file: the environment written to ux0:data/bt4/net/session.txt, then
   the application started again (sceAppMgrLoadExec); at the next start Vita_SessionLoad (vita_main.c, after bt3.txt)
   sets those variables and removes the file, and net.c finds its session as on the PC (Port_NetActive). The end of a
   session (role 0) restarts the same way into the player's own game. ---- */
#define SESSION_DIR PORT_VITA_HOME "/net"
#define SESSION_FILE SESSION_DIR "/session.txt"
#ifndef PORT_VITA_HOME
#define PORT_VITA_HOME "ux0:data/bt4"
#endif
static const char *const kSessionEnv[] = {"BT3_NET_HOST", "BT3_NET_JOIN", "BT3_NET_SPECTATE", "BT3_NET_SESSION", "BT3_NET_NAME",
                                          "BT3_NET_LOCALPORT", "BT3_NET_PUNCH", "BT3_NET_RULES", "BT3_SOUND_TICKS", "BT3_NOMOVIE",
                                          "BT3_NET_VOICE", "BT3_NET_OPTS", "BT3_SAVES", "BT3_NET_SPEED",
                                          "BT3_NET_ADHOC", "BT3_NET_ADHOC_PORT"}; /* (the last two: an Ad Hoc session, vita_adhoc.c) */
int execv(const char *path, char *const argv[]) {
    extern int sceAppMgrLoadExec(const char *appPath, char *const argv[], void *optParam);
    extern int sceIoMkdir(const char *dir, int mode);
    FILE *f;
    unsigned i;
    int r;
    (void)path;
    (void)argv;
    sceIoMkdir(SESSION_DIR, 0777);
    f = fopen(SESSION_FILE, "w");
    if (f == NULL) {
        Vita_Log("net: the session file %s could not be written\n", SESSION_FILE);
        errno = EIO;
        return -1;
    }
    for (i = 0; i < sizeof(kSessionEnv) / sizeof(kSessionEnv[0]); i++) {
        const char *v = getenv(kSessionEnv[i]);
        if (v != NULL) {
            fprintf(f, "%s=%s\n", kSessionEnv[i], v);
        }
    }
    fclose(f);
    Vita_Log("net: %s: the application starts again (%s)\n", getenv("BT3_NET_SESSION") != NULL ? "a session begins" : "the session is over",
             SESSION_FILE);
    fflush(NULL);
    r = sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
    Vita_Log("net: sceAppMgrLoadExec failed: 0x%08X\n", (unsigned)r);
    remove(SESSION_FILE);
    errno = ENOSYS;
    return -1;
}

extern int gVitaNetNeutralWanted, gVitaNetSpeedOn; /* bt3.txt online_neutral, online_speed (vita_net.c) */

/* vita_main.c's pacing: the game's pace in a session, 1000 = the screen's 60 blanks a second. The PC paces a
   session's blanks by net.c's speed (plat_stub.c Port_SpeedMilli: 1000, or 1140 / 1200 when the room says so); with
   bt3.txt online_speed=1 the Vita does the same (its grid of screen blanks advanced by 1000/speed of a blank a game
   blank); 0: the old way, 60 a second whatever the room says (rooms at 100 % only). */
int Vita_NetPaceMilli(void) {
    extern int Port_NetSession(void), Port_NetSpeedMilli(void);
    int sp;
    if (!gVitaNetSpeedOn || !Port_NetSession()) {
        return 1000;
    }
    sp = Port_NetSpeedMilli();
    return sp > 1000 && sp <= 2000 ? sp : 1000;
}

/* vita_main.c, after bt3.txt: the session's variables (above), and what a session needs of the Vita (as m0=1:
   reads on the game's thread so that loads take the same blanks on both sides, the PC's key table). 1: a session */
int Vita_SessionLoad(void) {
    extern int gVitaAsyncRead, gVitaSimExact;
    extern int sceIoMkdir(const char *dir, int mode);
    char line[256];
    FILE *f;
    int n = 0;
    if (getenv("BT3_NET_SAVES") == NULL) { /* (net.c's session save folder; emptied by net.c at each session start) */
        sceIoMkdir(SESSION_DIR, 0777);
        sceIoMkdir(SESSION_DIR "/saves", 0777);
        setenv("BT3_NET_SAVES", SESSION_DIR "/saves", 1);
    }
    f = fopen(SESSION_FILE, "r");
    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char *eq = strchr(line, '=');
        line[strcspn(line, "\r\n")] = '\0';
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        setenv(line, eq + 1, 1);
        n++;
    }
    fclose(f);
    remove(SESSION_FILE); /* (once: a crash in a session does not bring the next start back into it) */
    if (getenv("BT3_NET_SESSION") == NULL) {
        Vita_Log("net: back from a session (%d settings from %s)\n", n, SESSION_FILE);
        return 0;
    }
    gVitaAsyncRead = 0;
    gVitaSimExact = 1;
    if (gVitaNetNeutralWanted) {
        extern int gVitaNetNeutral;
        gVitaNetNeutral = 1; /* (vita_gfx.c, vita_file.c: the PC's neutral Vita Settings, no mods) */
    }
    {
        extern void Vita_AdhocSessionLoad(void); /* (vita_adhoc.c: BT3_NET_ADHOC, net.c's socket on the group) */
        Vita_AdhocSessionLoad();
    }
    Vita_Log("net: an online session starts (%s%s%s), saves %s; reads on the game's thread, the PC's keys\n",
             getenv("BT3_NET_HOST") != NULL ? "host, port " : getenv("BT3_NET_JOIN") != NULL ? "joining " : "watching ",
             getenv("BT3_NET_HOST") != NULL ? getenv("BT3_NET_HOST") : getenv("BT3_NET_JOIN") != NULL ? getenv("BT3_NET_JOIN") : "",
             getenv("BT3_NET_SPECTATE") != NULL ? getenv("BT3_NET_SPECTATE") : "", getenv("BT3_SAVES") != NULL ? getenv("BT3_SAVES") : "?");
    return 1;
}

/* plat_stub.c's Port_VBlank on the Vita: an online session's start-up runs ahead to the versus menu without waiting
   for the screen's blanks (net.c Port_NetWarp), as the PC's Port_VBlank does; vita_main.c's pacing reads this */
volatile int gVitaNetWarp;
void Vita_NetVBlank(void) {
    extern int Port_NetSession(void), Port_NetWarp(void);
    gVitaNetWarp = Port_NetSession() && Port_NetWarp();
}

/* ---- match_supabase.cpp ends a stuck request thread; the Vita's pthread has no cancel ---- */
int pthread_cancel(pthread_t thread) {
    (void)thread;
    return ENOSYS;
}

/* ---- the files the gameplay identity reads (build_identity.c: "disc/SLUS_219.78", "disc/BIN/DBZ4.BIN", ...),
   under gVitaDisc: the extracted disc folder, or the mounted disc image (.iso): vita_iso.c wraps the C library's file
   calls, so <image>/BIN/DBZ4.BIN reads the file inside the image. ---- */
FILE *Port_FileOpenHost(const char *rel, char *path, size_t n) {
    FILE *fp;
    const char *tail = rel;
    if (strncmp(rel, "disc/", 5) == 0) {
        tail = rel + 5;
    }
    snprintf(path, n, "%s/%s", gVitaDisc, tail);
    fp = fopen(path, "rb");
    if (fp == NULL) {
        Vita_Log("net: identity file not found: %s (a disc image is not read for this yet)\n", path);
    }
    return fp;
}

/* ---- gs/state.c's answers where it cannot save the game's state (its own fallback, lines 998-1008: what the PC's
   32-bit builds get): no rollback, no in-process session. The Vita's session start is M2a's next step. ---- */
int Port_RollSave(void) { return 0; }
void Port_RollBack(int k) { (void)k; }
int Port_SessionCan(void) { return 0; }
void Port_SessionRequest(int role, const char *address, int port) {
    Vita_Log("net: a session was asked (role %d, %s:%d): the Vita's session start is not built yet\n", role, address != NULL ? address : "?", port);
}
void Port_SessionReturn(void) {}
int Port_SessionAsked(void) { return 0; }
