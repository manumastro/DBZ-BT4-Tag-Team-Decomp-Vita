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
 *   adhoc_test=3          A0b: as 1, then the app starts again (as an online session starts on the Vita) and the next start
 *                         looks, without the dialog, whether the group is still there (ad hoc state, a socket)
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
    } else if (strcmp(name, "adhoc_menu") == 0) {
        extern int gVitaAdhocMenu;
        gVitaAdhocMenu = v;
    } else if (strcmp(name, "adhoc_port") == 0) {
        sPort = v > 0 && v < 65536 ? v : 26013;
    }
}

/* the renderer's swap passes GL_TRUE while a system dialog is open (vita_vgl.c: vitaGL then updates the dialog) */
volatile int gVitaSysDialog;

/* bt3.txt adhoc_menu: 1 the online hub's Ad Hoc rows (Create Match > Connection "Ad Hoc", Join > "AD HOC"); 0 the
   PC's menus only (default until run on two Vitas) */
int gVitaAdhocMenu;

#include "vita_adhoc.h"

#ifndef BT4_VITA_ONLINE
void Vita_AdhocBoot(void) {
    if (gVitaAdhocTest) {
        Vita_Log("adhoc: adhoc_test=%d, but this build has no network part (build.py BT4_VITA_ONLINE=1)\n", gVitaAdhocTest);
    }
}
int Vita_AdhocMenuOn(void) { return 0; }
int Vita_AdhocMenuBegin(int host, const char *name, int port) { (void)host; (void)name; (void)port; return 0; }
int Vita_AdhocMenuState(void) { return -1; }
int Vita_AdhocRooms(VitaAdhocRoom *out, int max) { (void)out; (void)max; return 0; }
void Vita_AdhocArm(const char *how) { (void)how; }
void Vita_AdhocMenuEnd(int keep) { (void)keep; }
void Vita_AdhocSessionLoad(void) {}
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
#define RESTART_MARK "ux0:data/bt4/adhoc_restart.txt" /* (A0b: written before the restart, removed at the next start) */

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
    if (r >= 0) {
        sUp = 2;
    }
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
/* ---- A1: gs/net.c's UDP socket on Ad Hoc (a transport swap: net.c unchanged) ----
   In a build with BT4_VITA_NETLINK=1, gs/net.c is compiled with vita_adhocnet.h, which sends its socket calls here
   (VitaNet_*). Outside an Ad Hoc session (no BT3_NET_ADHOC in the environment, i.e. in session.txt) each is the C
   library's own. In an Ad Hoc session, net.c's datagram socket (the first one it makes; not one it only connects to
   learn the LAN address) is carried by a PDP socket on the group: an address 10.m3.m4.m5 is the Vita whose MAC ends in
   m3:m4:m5 (the whole MAC learned from its datagrams, or given in BT3_NET_ADHOC by the Join list: the host's), the
   port is the PDP port. Every packet is net.c's own (T_HELLO .. T_BYE): the protocol is the PC's.
   BT3_NET_ADHOC: "1" (the host), or the host's MAC "AA:BB:CC:DD:EE:FF" (a joiner). BT3_NET_ADHOC_PORT: the PDP port
   of a joiner, whose socket net.c does not bind (default 26014). */
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define NET_MACS 16
static int sNetMode = -1; /* -1 not read yet, 0 not an Ad Hoc session, 1 an Ad Hoc session */
static int sNetCand = -1; /* net.c's datagram socket (a real one, kept for its number) */
static int sNetOpen;      /* its PDP socket is open */
static int sNetPort;
static unsigned char sNetMacs[NET_MACS][6];
static int sNetMacN;
static unsigned char sStash[SCE_NET_ADHOC_PDP_MFS], sStashMac[6];
static int sStashLen = -1, sStashPort;
unsigned gVitaAdhocNetN[4]; /* (the log) datagrams sent, received, refused, to unknown addresses */

static void net_mac_learn(const unsigned char m[6]) {
    int i;
    for (i = 0; i < sNetMacN; i++) {
        if (memcmp(sNetMacs[i], m, 6) == 0) {
            return;
        }
    }
    if (sNetMacN < NET_MACS) {
        memcpy(sNetMacs[sNetMacN++], m, 6);
    }
}
/* Ad Hoc for net.c's next socket: in a session started from session.txt with BT3_NET_ADHOC (Vita_AdhocSessionLoad),
   or in the menus once the hub's Ad Hoc rows armed it (Vita_AdhocArm: the lobby's socket), until that socket closes */
static int sNetArmed, sNetSessionStart;
int Vita_AdhocSession(void) {
    if (sNetMode < 0) {
        const char *v = getenv("BT3_NET_ADHOC");
        unsigned m[6];
        sNetMode = (sNetArmed || sNetSessionStart) && v != NULL && v[0] != '\0';
        if (sNetMode && sscanf(v, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
            unsigned char b[6];
            int i;
            for (i = 0; i < 6; i++) {
                b[i] = (unsigned char)m[i];
            }
            net_mac_learn(b);
        }
        if (sNetMode) {
            Vita_Log("adhoc: an Ad Hoc session (%s): net.c's datagrams go on the group\n", v);
        }
    }
    return sNetMode;
}
static uint32_t net_ip_of(const unsigned char m[6]) { /* (network order) */
    return htonl((10u << 24) | ((uint32_t)m[3] << 16) | ((uint32_t)m[4] << 8) | m[5]);
}
static int net_mac_of(uint32_t ip, unsigned char out[6]) {
    int i;
    for (i = 0; i < sNetMacN; i++) {
        if (net_ip_of(sNetMacs[i]) == ip) {
            memcpy(out, sNetMacs[i], 6);
            return 1;
        }
    }
    return 0;
}
/* the PDP socket, at the first use: the layer started, the group (joined before the session: the Join list, the
   Create's "Ad Hoc" row) read without the dialog */
static int sNetWantPort; /* (the port net.c bound, kept while the group is not there yet) */
static unsigned long long sNetTryAt;
static unsigned sNetTries;
static int net_open(int port) {
    if (sNetOpen) {
        return 1;
    }
    if (port != 0) {
        sNetWantPort = port;
    }
    port = sNetWantPort;
    if (port == 0) {
        const char *p = getenv("BT3_NET_ADHOC_PORT");
        port = p != NULL && atoi(p) > 0 ? atoi(p) : 26014;
    }
    if (now_us() < sNetTryAt) { /* (tried a moment ago: net.c sees a socket with nothing to read yet) */
        return 0;
    }
    sNetTryAt = now_us() + 500000ull;
    if (!Vita_AdhocStart() || (sUp < 2 && !Vita_AdhocGroup(0, 0)) || !Vita_AdhocOpen(port)) {
        if (sNetTries++ % 20 == 0) {
            Vita_Log("adhoc: net.c's socket: no group or no PDP socket yet (port %d, try %u): nothing sent or heard until there is\n", port,
                     sNetTries);
        }
        return 0;
    }
    sNetPort = port;
    sNetOpen = 1;
    Vita_Log("adhoc: net.c's socket is PDP port %d\n", port);
    return 1;
}
static int net_ours(int fd) { return fd >= 0 && fd == sNetCand && Vita_AdhocSession(); }

int VitaNet_socket(int domain, int type, int protocol) {
    int fd = socket(domain, type, protocol);
    if (sNetCand < 0) {
        sNetMode = -1; /* (read again for each new socket: the menus arm and disarm) */
    }
    if (fd >= 0 && sNetCand < 0 && domain == AF_INET && type == SOCK_DGRAM && Vita_AdhocSession()) {
        sNetCand = fd;
    }
    return fd;
}
int VitaNet_connect(int fd, const struct sockaddr *to, socklen_t len) {
    if (fd == sNetCand && !sNetOpen) {
        sNetCand = -1; /* (the LAN address's look-up: an ordinary socket) */
    }
    return connect(fd, to, len);
}
int VitaNet_bind(int fd, const struct sockaddr *a, socklen_t len) {
    if (!net_ours(fd)) {
        return bind(fd, a, len);
    }
    /* (never refused: net.c ends the program on a refused bind; without the group yet, the PDP socket opens at a later
       call, and net.c waits for the other player as it does on a quiet network) */
    net_open(a != NULL && len >= (socklen_t)sizeof(struct sockaddr_in) ? ntohs(((const struct sockaddr_in *)a)->sin_port) : 0);
    return 0;
}
ssize_t VitaNet_sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *to, socklen_t len) {
    unsigned char mac[6];
    const struct sockaddr_in *in = (const struct sockaddr_in *)to;
    if (!net_ours(fd)) {
        return sendto(fd, buf, n, flags, to, len);
    }
    if (!net_open(0)) {
        errno = ENETDOWN;
        return -1;
    }
    if (in == NULL || !net_mac_of(in->sin_addr.s_addr, mac)) {
        gVitaAdhocNetN[3]++;
        errno = EHOSTUNREACH;
        return -1;
    }
    if (n > SCE_NET_ADHOC_PDP_MFS || !Vita_AdhocSend(mac, ntohs(in->sin_port), buf, (int)n)) {
        gVitaAdhocNetN[2]++;
        errno = EAGAIN;
        return -1;
    }
    gVitaAdhocNetN[0]++;
    return (ssize_t)n;
}
static int net_pull(void) { /* one datagram into the stash; 1: there is one */
    if (sStashLen < 0) {
        int r = Vita_AdhocRecv(sStashMac, &sStashPort, sStash, sizeof(sStash));
        if (r > 0) {
            sStashLen = r;
        }
    }
    return sStashLen >= 0;
}
ssize_t VitaNet_recvfrom(int fd, void *buf, size_t n, int flags, struct sockaddr *from, socklen_t *len) {
    if (!net_ours(fd)) {
        return recvfrom(fd, buf, n, flags, from, len);
    }
    if (!net_open(0) || !net_pull()) {
        errno = EAGAIN;
        return -1;
    }
    n = (size_t)sStashLen < n ? (size_t)sStashLen : n;
    memcpy(buf, sStash, n);
    sStashLen = -1;
    net_mac_learn(sStashMac);
    if (from != NULL && len != NULL && *len >= (socklen_t)sizeof(struct sockaddr_in)) {
        struct sockaddr_in *in = (struct sockaddr_in *)from;
        memset(in, 0, sizeof(*in));
        in->sin_family = AF_INET;
        in->sin_port = htons((uint16_t)sStashPort);
        in->sin_addr.s_addr = net_ip_of(sStashMac);
        *len = sizeof(*in);
    }
    gVitaAdhocNetN[1]++;
    return (ssize_t)n;
}
/* net.c waits on its socket alone (select(sSock + 1, ...)); any other socket in the sets is looked at without waiting */
int VitaNet_select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, struct timeval *tv) {
    unsigned long long end;
    if (sNetCand < 0 || rd == NULL || !FD_ISSET(sNetCand, rd) || !Vita_AdhocSession()) {
        return select(nfds, rd, wr, ex, tv);
    }
    end = tv != NULL ? now_us() + (unsigned long long)tv->tv_sec * 1000000ull + (unsigned long long)tv->tv_usec : ~0ull;
    for (;;) {
        fd_set r2, w2, e2;
        struct timeval zero = {0, 0};
        int n = 0, mine = net_open(0) && net_pull(), others = 0, i;
        r2 = *rd;
        FD_CLR(sNetCand, &r2);
        FD_ZERO(&w2);
        FD_ZERO(&e2);
        if (wr != NULL) w2 = *wr;
        if (ex != NULL) e2 = *ex;
        for (i = 0; i < nfds; i++) {
            others |= FD_ISSET(i, &r2) || FD_ISSET(i, &w2) || FD_ISSET(i, &e2);
        }
        if (others) {
            n = select(nfds, &r2, wr != NULL ? &w2 : NULL, ex != NULL ? &e2 : NULL, &zero);
            n = n < 0 ? 0 : n;
        }
        if (mine || n > 0 || now_us() >= end) {
            if (mine) {
                FD_SET(sNetCand, &r2);
                n++;
            }
            *rd = r2;
            if (wr != NULL) *wr = w2;
            if (ex != NULL) *ex = e2;
            return n;
        }
        sceKernelDelayThread(500);
    }
}
int VitaNet_getsockname(int fd, struct sockaddr *a, socklen_t *len) {
    struct sockaddr_in *in = (struct sockaddr_in *)a;
    if (!net_ours(fd) || !sNetOpen) {
        return getsockname(fd, a, len);
    }
    if (in == NULL || len == NULL || *len < (socklen_t)sizeof(*in)) {
        errno = EINVAL;
        return -1;
    }
    memset(in, 0, sizeof(*in));
    in->sin_family = AF_INET;
    in->sin_port = htons((uint16_t)sNetPort);
    in->sin_addr.s_addr = net_ip_of(sMe.data);
    *len = sizeof(*in);
    return 0;
}
int VitaNet_close(int fd) {
    if (fd >= 0 && fd == sNetCand) {
        if (sNetOpen && sPdp >= 0) {
            sceNetAdhocPdpDelete(sPdp, 0);
            sPdp = -1;
        }
        Vita_Log("adhoc: net.c's socket closed (datagrams sent %u, received %u, refused %u, to unknown addresses %u)\n", gVitaAdhocNetN[0],
                 gVitaAdhocNetN[1], gVitaAdhocNetN[2], gVitaAdhocNetN[3]);
        sNetCand = -1;
        sNetOpen = 0;
        sNetWantPort = 0;
        sNetTryAt = 0;
        sStashLen = -1;
        sNetArmed = 0; /* (the lobby is over; a session carries BT3_NET_ADHOC in session.txt) */
        sNetMode = -1;
    }
    return close(fd);
}

/* ---- A1: the hub's Ad Hoc rows (src/menu/port_tbo.c, PORT_VITA): the group through the system's dialog, then the
   host's beacons (its name and its lobby's port, twice a second) or the joiner's list of the rooms heard. All on a
   thread of its own below the game's: the dialog needs the game's frames going on (the swap updates it). ---- */
#define ADHOC_BEACON_PORT 26013
#define ROOMS 8
typedef struct RoomBeacon {
    char magic[4]; /* "BT4R" */
    unsigned version; /* 1 */
    unsigned port;
    char name[32];
} RoomBeacon;
static volatile int sMenuState; /* 0 idle, 1 the dialog, 2 in the group (beacons / list), -1 no group */
static volatile int sMenuStop;
static int sMenuHost, sMenuPort;
static char sMenuName[32];
static SceUID sMenuTh = -1;
static VitaAdhocRoom sRooms[ROOMS];
static unsigned long long sRoomSeen[ROOMS];
static int sRoomN;
static SceUID sRoomLock = -1;

int Vita_AdhocMenuOn(void) { return gVitaAdhocMenu != 0; }
int Vita_AdhocMenuState(void) { return sMenuState; }

static void room_heard(const unsigned char mac[6], const RoomBeacon *b) {
    int i;
    char m[18];
    mac_text((const SceNetEtherAddr *)mac, m);
    sceKernelLockMutex(sRoomLock, 1, NULL);
    for (i = 0; i < sRoomN && strcmp(sRooms[i].mac, m) != 0; i++) {
    }
    if (i == sRoomN && sRoomN < ROOMS) {
        sRoomN++;
        Vita_Log("adhoc: a room heard: \"%.31s\" (%s, port %u)\n", b->name, m, b->port);
    }
    if (i < sRoomN) {
        VitaAdhocRoom *r = &sRooms[i];
        snprintf(r->name, sizeof(r->name), "%.31s", b->name);
        memcpy(r->mac, m, sizeof(r->mac));
        snprintf(r->addr, sizeof(r->addr), "10.%u.%u.%u", mac[3], mac[4], mac[5]);
        r->port = (int)b->port;
        sRoomSeen[i] = now_us();
    }
    sceKernelUnlockMutex(sRoomLock, 1);
}

static int menu_thread(SceSize args, void *argp) {
    int bpdp = -1;
    unsigned long long next = 0;
    (void)args;
    (void)argp;
    sMenuState = 1;
    if (!Vita_AdhocStart() || !Vita_AdhocGroup(sMenuHost ? 6 : 7, 300)) {
        sMenuState = -1;
        return 0;
    }
    bpdp = sceNetAdhocPdpCreate(&sMe, ADHOC_BEACON_PORT, 0x2000, 0);
    if (bpdp < 0) {
        Vita_Log("adhoc: the beacons' socket did not open: 0x%08X\n", (unsigned)bpdp);
        sMenuState = -1;
        return 0;
    }
    sMenuState = 2;
    while (!sMenuStop) {
        if (sMenuHost && now_us() >= next) {
            static const SceNetEtherAddr all = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
            RoomBeacon b;
            memset(&b, 0, sizeof(b));
            memcpy(b.magic, "BT4R", 4);
            b.version = 1;
            b.port = (unsigned)sMenuPort;
            snprintf(b.name, sizeof(b.name), "%s", sMenuName);
            sceNetAdhocPdpSend(bpdp, &all, ADHOC_BEACON_PORT, &b, sizeof(b), 0, 1);
            next = now_us() + 500000ull;
        }
        for (;;) {
            SceNetEtherAddr from;
            SceUShort16 fport = 0;
            RoomBeacon b;
            int len = sizeof(b), r = sceNetAdhocPdpRecv(bpdp, &from, &fport, &b, &len, 0, 1);
            if (r < 0) {
                break;
            }
            if (!sMenuHost && len == (int)sizeof(b) && memcmp(b.magic, "BT4R", 4) == 0 && b.version == 1) {
                b.name[sizeof(b.name) - 1] = '\0';
                room_heard(from.data, &b);
            }
        }
        sceKernelDelayThread(50 * 1000);
    }
    sceNetAdhocPdpDelete(bpdp, 0);
    return 0;
}

/* host 1: the group created (its dialog) and this lobby advertised; 0: a group joined (its dialog) and its rooms
   listed. 1 started */
int Vita_AdhocMenuBegin(int host, const char *name, int port) {
    if (sMenuTh >= 0) {
        return 1;
    }
    if (sRoomLock < 0) {
        sRoomLock = sceKernelCreateMutex("bt4_adhoc_rooms", 0, 0, NULL);
    }
    sMenuHost = host;
    sMenuPort = port;
    snprintf(sMenuName, sizeof(sMenuName), "%s", name != NULL ? name : "");
    sMenuStop = 0;
    sRoomN = 0;
    sMenuState = 1;
    sMenuTh = sceKernelCreateThread("bt4_adhoc_menu", menu_thread, 0x10000100 + 20, 64 * 1024, 0, 0x00020000 | 0x00040000, NULL);
    if (sMenuTh < 0 || sceKernelStartThread(sMenuTh, 0, NULL) < 0) {
        Vita_Log("adhoc: the menu's thread did not start: 0x%08X\n", (unsigned)sMenuTh);
        sMenuTh = -1;
        sMenuState = -1;
        return 0;
    }
    Vita_Log("adhoc: %s (the group's dialog)\n", host ? "hosting a room" : "looking for rooms");
    return 1;
}

/* the rooms heard in the last three seconds, newest first as heard */
int Vita_AdhocRooms(VitaAdhocRoom *out, int max) {
    int i, n = 0;
    unsigned long long t = now_us();
    if (sRoomLock < 0) {
        return 0;
    }
    sceKernelLockMutex(sRoomLock, 1, NULL);
    for (i = 0; i < sRoomN && n < max; i++) {
        if (t - sRoomSeen[i] < 3000000ull) {
            out[n] = sRooms[i];
            out[n].age = (int)((t - sRoomSeen[i]) / 1000ull);
            n++;
        }
    }
    sceKernelUnlockMutex(sRoomLock, 1);
    return n;
}

/* net.c's next socket goes on the group: "1" a host, a MAC a joiner (BT3_NET_ADHOC, written into session.txt by the
   session's start) */
void Vita_AdhocArm(const char *how) {
    setenv("BT3_NET_ADHOC", how, 1);
    sNetArmed = 1;
    sNetMode = -1;
}

/* the screens left: the beacons stopped; keep 0 also leaves the group (a session's restart takes it in any case) */
void Vita_AdhocMenuEnd(int keep) {
    if (sMenuTh >= 0) {
        sMenuStop = 1;
        sceKernelWaitThreadEnd(sMenuTh, NULL, NULL);
        sceKernelDeleteThread(sMenuTh);
        sMenuTh = -1;
    }
    sMenuState = 0;
    if (!keep) {
        if (sNetCand < 0) {
            sNetArmed = 0;
            unsetenv("BT3_NET_ADHOC");
            Vita_AdhocStop();
        }
    }
}

/* vita_main.c's session start (vita_netglue.c Vita_SessionLoad): an Ad Hoc session's net.c socket goes on the group */
void Vita_AdhocSessionLoad(void) {
    if (getenv("BT3_NET_ADHOC") != NULL && getenv("BT3_NET_SESSION") != NULL) {
        sNetSessionStart = 1;
        sNetMode = -1;
        Vita_Log("adhoc: this session is an Ad Hoc one (%s)\n", getenv("BT3_NET_ADHOC"));
    }
}

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
    if (gVitaAdhocTest == 3) { /* A0b: does the group outlive the app's restart? (the next start looks: Vita_AdhocBoot) */
        extern int sceAppMgrLoadExec(const char *appPath, char *const argv[], void *optParam);
        FILE *f = fopen(RESTART_MARK, "w");
        if (f != NULL) {
            fclose(f);
            Vita_Log("adhoc: A0b: the app starts again with the group up\n");
            Vita_Log("adhoc: sceAppMgrLoadExec: 0x%08X\n", (unsigned)sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL));
            remove(RESTART_MARK);
        }
    }
    Vita_AdhocStop();
    Vita_Log("adhoc: A0 check: done in %u s (%s)\n", (unsigned)((now_us() - t0) / 1000000), n ? "peers heard" : "no other Vita heard: one console");
    return 0;
}

/* A0b's second start: the ad hoc state as the system has it, then the layer started again without the dialog */
static int adhoc_after_restart(SceSize args, void *argp) {
    int state = -1, r;
    (void)args;
    (void)argp;
    sceKernelDelayThread(5u * 1000000u);
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    r = sceNetCtlAdhocGetState(&state);
    Vita_Log("adhoc: A0b: after the restart, sceNetCtlAdhocGetState 0x%08X state %d\n", (unsigned)r, state);
    if (Vita_AdhocStart() && Vita_AdhocGroup(0, 0) && Vita_AdhocOpen(sPort)) {
        static const char hi[] = "BT4A";
        Vita_Log("adhoc: A0b: a socket opened after the restart without the dialog (a broadcast %s)\n",
                 Vita_AdhocSend(NULL, sPort, hi, 4) ? "sent" : "refused");
    } else {
        Vita_Log("adhoc: A0b: no group after the restart: a session must join it again\n");
    }
    Vita_AdhocStop();
    return 0;
}

/* vita_main.c, after the network's boot: the A0 check on its own thread (below the game's priority); at the start
   after A0b's restart, the look at the group instead */
void Vita_AdhocBoot(void) {
    SceUID th;
    FILE *mark = fopen(RESTART_MARK, "r");
    if (mark != NULL) {
        fclose(mark);
        remove(RESTART_MARK);
        th = sceKernelCreateThread("bt4_adhoc_after", adhoc_after_restart, 0x10000100 + 20, 64 * 1024, 0, 0x00020000 | 0x00040000, NULL);
        if (th >= 0) {
            sceKernelStartThread(th, 0, NULL);
        }
        return;
    }
    if (!gVitaAdhocTest) {
        return;
    }
    th = sceKernelCreateThread("bt4_adhoc_test", adhoc_test, 0x10000100 + 20, 64 * 1024, 0, 0x00020000 | 0x00040000, NULL);
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
        Vita_Log("adhoc: the check's thread did not start: 0x%08X\n", (unsigned)th);
    }
}
#endif
