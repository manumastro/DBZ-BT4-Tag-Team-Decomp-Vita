/*
 * PS Vita: Ad Hoc (local wireless, Vita to Vita, no access point, no internet), the transport the Vita adds to
 * BT4decomp's online play (psvita-agent notes/online/vita-multiplayer-plan.md; docs/vita/online-bt4.md "Ad Hoc").
 *
 * Ad Hoc carries the same packets as online play: the room's discovery is ours (beacons on the group: the host's name,
 * its game version and data family), the match is upstream's own protocol (gs/net.c) over Ad Hoc datagrams instead of
 * internet UDP. This file is the transport and its first check (milestone A0); the plug into gs/net.c comes with A1.
 *
 * The calls are the PSP ad hoc layer the Vita keeps (as Rinnegatamante's vitaQuake does on HENkaku): the PSPNET_ADHOC
 * module, sceNetAdhocInit, sceNetAdhocctlInit with a 9-character ID, the system's own dialog to join or create the group
 * (sceNetCheckDialog, mode PSP_ADHOC_CONN; no PSN), then PDP sockets addressed by MAC and port.
 *
 * bt3.txt (the development build; off by default, and then nothing here runs: no module, no thread):
 *   adhoc_test=1          A0: after adhoc_delay seconds of the game, Ad Hoc started, the group dialog shown, a datagram
 *                         socket opened, a beacon broadcast twice a second for adhoc_seconds, every beacon and ping heard
 *                         logged (peers, round trips), then all of it closed. Lines "adhoc: ..." in the log
 *   adhoc_delay=<s>       seconds before the test starts (default 25: the game's title screen, the renderer up)
 *   adhoc_seconds=<s>     how long the beacons run (default 30)
 *   adhoc_dialog=<n>      the dialog's mode: 5 join or create (default), 6 create, 7 join; 0 no dialog (a group must
 *                         already be up)
 *   adhoc_port=<n>        the datagram port (default 26013)
 */
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Vita_Log(const char *fmt, ...);

int gVitaAdhocTest;         /* bt3.txt adhoc_test */
static int sDelay = 25, sSeconds = 30, sDialog = 5, sPort = 26013;

void Vita_AdhocSetting(const char *name, const char *value) {
    int v = atoi(value);
    if (strcmp(name, "adhoc_test") == 0) {
        gVitaAdhocTest = v;
    } else if (strcmp(name, "adhoc_delay") == 0) {
        sDelay = v < 0 ? 0 : v;
    } else if (strcmp(name, "adhoc_seconds") == 0) {
        sSeconds = v < 1 ? 1 : v > 600 ? 600 : v;
    } else if (strcmp(name, "adhoc_dialog") == 0) {
        sDialog = v == 0 || (v >= 5 && v <= 7) ? v : 5;
    } else if (strcmp(name, "adhoc_port") == 0) {
        sPort = v > 0 && v < 65536 ? v : 26013;
    }
}

/* the renderer's swap passes GL_TRUE while a system dialog is open (vita_vgl.c: vitaGL then updates the dialog) */
volatile int gVitaSysDialog;

#ifndef BT4_VITA_ONLINE
void Vita_AdhocBoot(void) {
    if (gVitaAdhocTest) {
        Vita_Log("adhoc: adhoc_test=%d, but this build has no network part (build.py BT4_VITA_ONLINE=1)\n", gVitaAdhocTest);
    }
}
#else
#include <psp2/common_dialog.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/netcheck_dialog.h>
#include <psp2/pspnet_adhoc.h>
#include <psp2/pspnet_adhocctl.h>

/* the game's identity on the group: the 9 characters every Vita of this port uses (the ad hoc ID and the dialog's
   communication ID; it keeps BT4 groups apart from other games' groups) */
#define ADHOC_ID "BT4VITA01"

/* ---- the transport (A0 uses it; A1 puts gs/net.c's packets on it) ---- */
static int sUp;          /* 1 SceNet and the ad hoc layer started by us, 2 the group joined */
static int sNetOurs;     /* 1: we started SceNet (vita_net.c had not) */
static void *sPool;
static int sPdp = -1;
static SceNetEtherAddr sMe;
static char sGroup[9], sNick[64];

static unsigned long long now_us(void) { return sceKernelGetProcessTimeWide(); }
static void mac_text(const SceNetEtherAddr *a, char out[18]) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a->data[0], a->data[1], a->data[2], a->data[3], a->data[4], a->data[5]);
}

/* the ad hoc layer up (SceNet shared with vita_net.c when online=1 started it) */
int Vita_AdhocStart(void) {
    SceNetInitParam p;
    SceNetAdhocctlAdhocId id;
    int r;
    if (sUp) {
        return 1;
    }
    r = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (r < 0) {
        Vita_Log("adhoc: SceNet module not loaded: 0x%08X\n", (unsigned)r);
        return 0;
    }
    r = sceSysmoduleLoadModule(SCE_SYSMODULE_PSPNET_ADHOC);
    if (r < 0) {
        Vita_Log("adhoc: PSPNET_ADHOC module not loaded: 0x%08X\n", (unsigned)r);
        return 0;
    }
    sPool = malloc(256 * 1024);
    if (sPool == NULL) {
        Vita_Log("adhoc: no memory for SceNet's pool\n");
        return 0;
    }
    p.memory = sPool;
    p.size = 256 * 1024;
    p.flags = 0;
    r = sceNetInit(&p);
    if (r >= 0) {
        sNetOurs = 1;
    } else {
        free(sPool);
        sPool = NULL;
        if ((unsigned)r != 0x80410110u) { /* SCE_NET_ERROR_EBUSY: vita_net.c has it up already */
            Vita_Log("adhoc: sceNetInit failed: 0x%08X\n", (unsigned)r);
            return 0;
        }
    }
    r = sceNetCtlInit();
    if (r < 0 && (unsigned)r != 0x80412102u) {
        Vita_Log("adhoc: sceNetCtlInit: 0x%08X\n", (unsigned)r);
    }
    r = sceNetAdhocInit();
    if (r < 0) {
        Vita_Log("adhoc: sceNetAdhocInit failed: 0x%08X\n", (unsigned)r);
        return 0;
    }
    memset(&id, 0, sizeof(id));
    id.type = SCE_NET_ADHOCCTL_ADHOCTYPE_RESERVED;
    memcpy(id.data, ADHOC_ID, SCE_NET_ADHOCCTL_ADHOCID_LEN);
    r = sceNetAdhocctlInit(&id);
    if (r < 0) {
        Vita_Log("adhoc: sceNetAdhocctlInit failed: 0x%08X\n", (unsigned)r);
        sceNetAdhocTerm();
        return 0;
    }
    sUp = 1;
    return 1;
}

/* the group joined or created through the system's dialog (mode 5, 6 or 7); called on a thread other than the
   renderer's, which swaps with GL_TRUE meanwhile. 1: in a group */
int Vita_AdhocGroup(int mode, int waitSeconds) {
    SceCommonDialogConfigParam cfg;
    SceNetCheckDialogParam param;
    SceNetCheckDialogResult res;
    SceNetAdhocctlGroupName group;
    SceNetAdhocctlParameter info;
    unsigned long long t0 = now_us();
    int r, status = 0;
    if (!sUp) {
        return 0;
    }
    if (mode != 0) {
        sceCommonDialogConfigParamInit(&cfg);
        sceCommonDialogSetConfigParam(&cfg);
        sceNetCheckDialogParamInit(&param);
        memset(&group, 0, sizeof(group));
        param.groupName = &group;
        memcpy(&param.npCommunicationId.data, ADHOC_ID, 9);
        param.npCommunicationId.term = '\0';
        param.npCommunicationId.num = 0;
        param.mode = mode;
        param.timeoutUs = 0;
        gVitaSysDialog = 1;
        r = sceNetCheckDialogInit(&param);
        if (r < 0) {
            gVitaSysDialog = 0;
            Vita_Log("adhoc: the group dialog did not open: 0x%08X\n", (unsigned)r);
            return 0;
        }
        Vita_Log("adhoc: the group dialog is open (mode %d)\n", mode);
        while ((status = sceNetCheckDialogGetStatus()) != SCE_COMMON_DIALOG_STATUS_FINISHED) {
            if (now_us() - t0 > (unsigned long long)waitSeconds * 1000000ull) {
                sceNetCheckDialogAbort();
                Vita_Log("adhoc: the group dialog was closed after %d s without an answer\n", waitSeconds);
            }
            sceKernelDelayThread(50 * 1000);
        }
        memset(&res, 0, sizeof(res));
        sceNetCheckDialogGetResult(&res);
        sceNetCheckDialogTerm();
        gVitaSysDialog = 0;
        Vita_Log("adhoc: the group dialog closed after %u ms: result %d (%s)\n", (unsigned)((now_us() - t0) / 1000), res.result,
                 res.result == SCE_COMMON_DIALOG_RESULT_OK ? "OK" : res.result == SCE_COMMON_DIALOG_RESULT_USER_CANCELED ? "cancelled" : "not OK");
        if (res.result != SCE_COMMON_DIALOG_RESULT_OK) {
            return 0;
        }
    }
    memset(&info, 0, sizeof(info));
    r = sceNetAdhocctlGetParameter(&info);
    sceNetAdhocctlGetEtherAddr(&sMe);
    memcpy(sGroup, info.groupName.data, 8);
    sGroup[8] = '\0';
    snprintf(sNick, sizeof(sNick), "%s", (const char *)info.nickname.data);
    {
        char m[18];
        mac_text(&sMe, m);
        Vita_Log("adhoc: in the group: GetParameter 0x%08X, channel %d, group \"%s\", nickname \"%s\", this Vita %s\n", (unsigned)r,
                 info.channel, sGroup, sNick, m);
    }
    sUp = 2;
    return r >= 0;
}

/* a datagram socket on the group (non-blocking use) */
int Vita_AdhocOpen(int port) {
    if (sUp < 2) {
        return 0;
    }
    if (sPdp >= 0) {
        return 1;
    }
    sPdp = sceNetAdhocPdpCreate(&sMe, (SceUShort16)port, 0x2000, 0);
    if (sPdp < 0) {
        Vita_Log("adhoc: sceNetAdhocPdpCreate(port %d) failed: 0x%08X\n", port, (unsigned)sPdp);
        sPdp = -1;
        return 0;
    }
    return 1;
}

/* send to a MAC (NULL: every Vita of the group); 1 sent */
int Vita_AdhocSend(const unsigned char mac[6], int port, const void *data, int len) {
    static const SceNetEtherAddr all = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
    int r;
    if (sPdp < 0) {
        return 0;
    }
    r = sceNetAdhocPdpSend(sPdp, mac != NULL ? (const SceNetEtherAddr *)mac : &all, (SceUShort16)port, data, len, 0, 1);
    return r >= 0;
}

/* one datagram if any: its length, 0 none, -1 error */
int Vita_AdhocRecv(unsigned char mac[6], int *port, void *buf, int size) {
    SceNetEtherAddr from;
    SceUShort16 fport = 0;
    int len = size, r;
    if (sPdp < 0) {
        return -1;
    }
    r = sceNetAdhocPdpRecv(sPdp, &from, &fport, buf, &len, 0, 1);
    if ((unsigned)r == (unsigned)SCE_ERROR_NET_ADHOC_WOULD_BLOCK) {
        return 0;
    }
    if (r < 0) {
        return -1;
    }
    if (mac != NULL) {
        memcpy(mac, from.data, 6);
    }
    if (port != NULL) {
        *port = fport;
    }
    return len;
}

void Vita_AdhocStop(void) {
    if (sPdp >= 0) {
        sceNetAdhocPdpDelete(sPdp, 0);
        sPdp = -1;
    }
    if (sUp) {
        sceNetCtlAdhocDisconnect();
        sceNetAdhocctlTerm();
        sceNetAdhocTerm();
        if (sNetOurs) {
            sceNetCtlTerm();
            sceNetTerm();
            free(sPool);
            sPool = NULL;
            sNetOurs = 0;
        }
        sceSysmoduleUnloadModule(SCE_SYSMODULE_PSPNET_ADHOC);
        sUp = 0;
        Vita_Log("adhoc: down\n");
    }
}

/* ---- A0: the check ---- */
/* the beacon and the ping: 'B','T','4','A', kind (1 beacon, 2 ping, 3 pong), sequence, the sender's clock (us), then
   for a beacon its nickname. Little-endian like the PC's packets. (A1 adds the room: version, wire, family, rules.) */
typedef struct AdhocBeacon {
    char magic[4];
    unsigned kind, seq;
    unsigned long long t;
    char nick[48];
} AdhocBeacon;

#define PEERS 8
typedef struct Peer {
    unsigned char mac[6];
    char nick[48];
    unsigned beacons, pongs;
    unsigned rttMin, rttMax, rttSum;
} Peer;

static Peer *peer_find(Peer *p, int *n, const unsigned char mac[6]) {
    int i;
    for (i = 0; i < *n; i++) {
        if (memcmp(p[i].mac, mac, 6) == 0) {
            return &p[i];
        }
    }
    if (*n == PEERS) {
        return NULL;
    }
    memset(&p[*n], 0, sizeof(Peer));
    memcpy(p[*n].mac, mac, 6);
    p[*n].rttMin = ~0u;
    return &p[(*n)++];
}

static int adhoc_test(SceSize args, void *argp) {
    Peer peers[PEERS];
    int n = 0, i, sent = 0, heard = 0, bad = 0;
    unsigned seq = 0;
    unsigned long long t0, next = 0, end;
    (void)args;
    (void)argp;
    sceKernelDelayThread((SceUInt)sDelay * 1000000u);
    Vita_Log("adhoc: A0 check: start (dialog mode %d, port %d, %d s of beacons)\n", sDialog, sPort, sSeconds);
    t0 = now_us();
    if (!Vita_AdhocStart()) {
        Vita_Log("adhoc: A0 check: FAILED at start-up\n");
        return 0;
    }
    Vita_Log("adhoc: started in %u ms (modules, SceNet, ad hoc, ID %s)\n", (unsigned)((now_us() - t0) / 1000), ADHOC_ID);
    if (!Vita_AdhocGroup(sDialog, 180) || !Vita_AdhocOpen(sPort)) {
        Vita_Log("adhoc: A0 check: FAILED (no group or no socket)\n");
        Vita_AdhocStop();
        return 0;
    }
    end = now_us() + (unsigned long long)sSeconds * 1000000ull;
    while (now_us() < end) {
        unsigned char mac[6];
        int port, len;
        AdhocBeacon b;
        if (now_us() >= next) {
            AdhocBeacon out;
            memset(&out, 0, sizeof(out));
            memcpy(out.magic, "BT4A", 4);
            out.kind = 1;
            out.seq = ++seq;
            out.t = now_us();
            snprintf(out.nick, sizeof(out.nick), "%s", sNick);
            sent += Vita_AdhocSend(NULL, sPort, &out, (int)sizeof(out));
            for (i = 0; i < n; i++) { /* a ping to each Vita heard, for the round trip */
                out.kind = 2;
                Vita_AdhocSend(peers[i].mac, sPort, &out, (int)sizeof(out));
            }
            next = now_us() + 500000;
        }
        while ((len = Vita_AdhocRecv(mac, &port, &b, (int)sizeof(b))) > 0) {
            Peer *p;
            if (len != (int)sizeof(b) || memcmp(b.magic, "BT4A", 4) != 0) {
                bad++;
                continue;
            }
            if (memcmp(mac, sMe.data, 6) == 0) {
                continue; /* (our own broadcast) */
            }
            heard++;
            p = peer_find(peers, &n, mac);
            if (p == NULL) {
                continue;
            }
            if (b.kind == 1) {
                if (p->beacons++ == 0) {
                    char m[18];
                    mac_text((const SceNetEtherAddr *)mac, m);
                    snprintf(p->nick, sizeof(p->nick), "%s", b.nick);
                    Vita_Log("adhoc: a Vita heard: %s \"%s\" (port %d)\n", m, p->nick, port);
                }
            } else if (b.kind == 2) {
                b.kind = 3;
                Vita_AdhocSend(mac, sPort, &b, (int)sizeof(b));
            } else if (b.kind == 3) {
                unsigned rtt = (unsigned)(now_us() - b.t);
                p->pongs++;
                p->rttSum += rtt;
                p->rttMin = rtt < p->rttMin ? rtt : p->rttMin;
                p->rttMax = rtt > p->rttMax ? rtt : p->rttMax;
            }
        }
        if (len < 0) {
            bad++;
        }
        sceKernelDelayThread(4000);
    }
    Vita_Log("adhoc: %d beacons sent, %d packets heard, %d bad, %d Vita(s) in the group\n", sent, heard, bad, n);
    for (i = 0; i < n; i++) {
        char m[18];
        mac_text((const SceNetEtherAddr *)peers[i].mac, m);
        Vita_Log("adhoc: peer %s \"%s\": %u beacons, %u pongs, round trip min %u / mean %u / max %u us\n", m, peers[i].nick, peers[i].beacons,
                 peers[i].pongs, peers[i].pongs ? peers[i].rttMin : 0, peers[i].pongs ? peers[i].rttSum / peers[i].pongs : 0, peers[i].rttMax);
    }
    Vita_AdhocStop();
    Vita_Log("adhoc: A0 check: done in %u s (%s)\n", (unsigned)((now_us() - t0) / 1000000), n ? "peers heard" : "no other Vita heard: one console");
    return 0;
}

/* vita_main.c, after the network's boot: the A0 check on its own thread (below the game's priority) */
void Vita_AdhocBoot(void) {
    SceUID th;
    if (!gVitaAdhocTest) {
        return;
    }
    th = sceKernelCreateThread("bt4_adhoc_test", adhoc_test, 0x10000100 + 20, 64 * 1024, 0, 0x00020000 | 0x00040000, NULL);
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
        Vita_Log("adhoc: the check's thread did not start: 0x%08X\n", (unsigned)th);
    }
}
#endif
