/*
 * PS Vita: what BT4decomp's network files (gs/net.c, net_chat.c, net_relay.c, net_match.c, net_lobby.c,
 * build_identity.c, plat_fprint.c, match_supabase.cpp) call that the PC takes from files the Vita does not build:
 * the PC renderer's waits, its F1 window's lines, voice chat, the update downloader, the old Worker's signature, the
 * process restart. Only in a build with BT4_VITA_NETLINK=1 (online milestone M2a, docs/vita/online-bt4.md); there
 * vita_bt4.c's stand-ins of those files are renamed away (vita_netlink_rename.h) and upstream's own run.
 *
 * Each answer here is the Vita's for now: a log line where the PC would show something (the Vita-made look of the
 * banner and the notice comes with the menus, M2a's next step), nothing for voice, no restart.
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "voice.h"

extern void Vita_Log(const char *fmt, ...);
extern char gVitaDisc[];
extern int Update_OnlineRequired(void); /* vita_net.c: the server said UPDATE_REQUIRED */

/* ---- the PC renderer (gs_draw.c): the last picture held while the session waits ---- */
void Gs_HoldFrame(void) {}
void Gs_NetworkWait(int waiting) { (void)waiting; }

/* ---- the F1 window's lines (ui.cpp): logged for now ---- */
void Port_UiNotice(const char *text) {
    if (text != NULL && text[0] != '\0') {
        Vita_Log("net: notice: %s\n", text);
    }
}
void Port_UiBanner(const char *text, int ms) {
    if (text != NULL && text[0] != '\0') {
        Vita_Log("net: banner (%d ms): %s\n", ms, text);
    }
}
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

/* ---- the session's restart (net.c relaunch): no execv on the Vita; the Vita's way (sceAppMgrLoadExec with a session
   file, study 3.5) is M2a's next step. Failing here makes net.c report the session as not started. ---- */
int execv(const char *path, char *const argv[]) {
    (void)argv;
    Vita_Log("net: a session restart was asked (%s): not on the Vita yet\n", path != NULL ? path : "?");
    errno = ENOSYS;
    return -1;
}

/* ---- match_supabase.cpp ends a stuck request thread; the Vita's pthread has no cancel ---- */
int pthread_cancel(pthread_t thread) {
    (void)thread;
    return ENOSYS;
}

/* ---- the files the gameplay identity reads (build_identity.c: "disc/SLUS_219.78", "disc/BIN/DBZ4.BIN", ...):
   the extracted disc folder (bt3.txt disc=). A disc image (.iso) is not read here yet: the identity then says
   "missing" and the online menu refuses, as the PC does with a missing file. ---- */
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
