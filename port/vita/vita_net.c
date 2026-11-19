/*
 * PS Vita: the network's start-up and shut-down, and a self-test of everything BT4decomp's online play stands on
 * (docs/vita/online-bt4.md, "The network on the Vita"; milestone M1 of the online plan).
 *
 * bt3.txt (the development build; every one off by default, and then nothing here runs: no module, no pool, no thread):
 *   online=1               the network up from the start (SceNet with its pool, SceNetCtl, curl with OpenSSL)
 *   online_server=<url>    the server the self-test and the signaling client talk to, https://<host>[:<port>]
 *                          (a test server: agent-tools/online_testserver.py). Empty: no server at all; the signaling
 *                          client is then pointed at a name that does not resolve, never at BT4decomp's own service
 *   online_ca=<file>       a CA certificate (PEM) trusted besides the package's list (lib/cacert.pem): the test server's
 *   online_udp=<ip:port>   the UDP echo's address (default: online_server's host, port 26011)
 *   online_pool=<KB>       SceNet's memory pool (default 1024), from the C library's heap
 *   online_test=1          the self-test before the game starts: DNS, UDP echo, HTTPS GET, WebSocket echo, BT4decomp's
 *                          own signaling client (auth/start, the socket, bt4.ready, a command's reply); 2 the same, then
 *                          the program ends
 *   online_test_dns=<name> the name looked up (default stun.l.google.com, the PC's first STUN server)
 *   online_test_public=<url> also an HTTPS GET of this address with the package's CA list only (default none)
 *
 * Every HTTP request curl makes (BT4decomp's signaling included) leaves a line in the log: method, address without
 * its query, status, times (the calls are wrapped: build.py, -Wl,--wrap=curl_easy_perform). No body and no token is
 * ever logged.
 */
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PORT_VITA_HOME
#define PORT_VITA_HOME "ux0:data/bt4"
#endif

extern void Vita_Log(const char *fmt, ...);
extern int psvDebugScreenPrintf(const char *format, ...);

int gVitaOnline;              /* bt3.txt online */
int gVitaOnlineTest;          /* bt3.txt online_test */
static char sServer[200];     /* online_server, as given */
static char sHost[160];       /* its host[:port] */
static char sHostName[128] __attribute__((unused)); /* its host alone */
static char sUrl[200];        /* https://<host[:port]> */
static char sCa[160];         /* online_ca */
static char sUdp[160];        /* online_udp */
static char sDns[128] = "stun.l.google.com";
static char sPublic[200];
static int sPoolKb = 1024;

void Vita_NetSetting(const char *name, const char *value) {
    if (strcmp(name, "online") == 0) {
        gVitaOnline = atoi(value) != 0;
    } else if (strcmp(name, "online_test") == 0) {
        gVitaOnlineTest = atoi(value);
    } else if (strcmp(name, "online_server") == 0) {
        snprintf(sServer, sizeof(sServer), "%s", value);
    } else if (strcmp(name, "online_ca") == 0) {
        snprintf(sCa, sizeof(sCa), "%s", value);
    } else if (strcmp(name, "online_udp") == 0) {
        snprintf(sUdp, sizeof(sUdp), "%s", value);
    } else if (strcmp(name, "online_pool") == 0) {
        sPoolKb = atoi(value);
        if (sPoolKb < 128) {
            sPoolKb = 128;
        }
    } else if (strcmp(name, "online_test_dns") == 0) {
        snprintf(sDns, sizeof(sDns), "%s", value);
    } else if (strcmp(name, "online_test_public") == 0) {
        snprintf(sPublic, sizeof(sPublic), "%s", value);
    }
}

/* the signaling client's server (cloudflare_signal.inc under PORT_VITA): online_server, or a name that never resolves */
const char *Vita_NetHost(void) { return sHost[0] != '\0' ? sHost : "no-server.invalid"; }
const char *Vita_NetUrl(void) { return sUrl[0] != '\0' ? sUrl : "https://no-server.invalid"; }

#ifndef BT4_VITA_ONLINE
/* ---- a build without the online part (the release, unless BT4_VITA_ONLINE=1) ---- */
int Vita_NetBoot(void) {
    if (gVitaOnline || gVitaOnlineTest) {
        Vita_Log("net: online=%d online_test=%d, but this build has no network part (build.py BT4_VITA_ONLINE=1)\n", gVitaOnline, gVitaOnlineTest);
    }
    return 0;
}
void Vita_NetStop(void) {}
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <curl/curl.h>
#include "curl_ws_io.h"
#include "supabase_signal.h"

extern char gVitaSdlBase[160];

/* gs/net_update.c's online gate, without its update check (no GitHub, no signed route policy on the Vita) */
static int sOnlineRequired;
int Update_FetchOnlineRoute(char *out, size_t capacity) {
    (void)out;
    (void)capacity;
    return 0;
}
void Update_SetOnlineRequired(int required) {
    sOnlineRequired = required;
    if (required) {
        Vita_Log("net: the server says this client is too old (UPDATE_REQUIRED)\n");
    }
}
int Update_OnlineRequired(void) { return sOnlineRequired; }

static void *sPool;
static int sUp;             /* 1: SceNet and SceNetCtl started */
static char sCaFile[200];   /* the CA list curl reads */
static int sHeap0;          /* the C heap in use before the network */
static int sHeapPeak;
static unsigned long long sT0;

static unsigned long long now_us(void) { return sceKernelGetProcessTimeWide(); }

static int heap_used(void) {
    struct mallinfo mi = mallinfo();
    int used = (int)mi.uordblks;
    if (used > sHeapPeak) {
        sHeapPeak = used;
    }
    return used;
}

/* a line in the log and, during the self-test, on the start-up screen */
static int sScreen;
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) {
    char msg[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    Vita_Log("%s", msg);
    if (sScreen) {
        psvDebugScreenPrintf("%s", msg);
    }
}

/* SceNet's own counters (its pool's free memory now and its lowest) */

static void say_memory(const char *when) {
    SceNetStatisticsInfo st;
    int used = heap_used();
    memset(&st, 0, sizeof(st));
    sceNetGetStatisticsInfo(&st, 0);
    say("net: memory %s: C heap in use %d KB (%+d KB since before the network, peak %+d KB), SceNet pool %d KB free (lowest %d KB)\n", when,
        used / 1024, (used - sHeap0) / 1024, (sHeapPeak - sHeap0) / 1024, st.libnet_mem_free_size / 1024, st.libnet_mem_free_min / 1024);
}

/* online_server -> host[:port], host, https://host[:port] */
static void parse_server(void) {
    const char *p = sServer;
    size_t n;
    sHost[0] = sHostName[0] = sUrl[0] = '\0';
    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7; /* (BT4decomp's client speaks HTTPS only: the host is kept, the scheme becomes https) */
    }
    n = strcspn(p, "/?");
    if (n == 0 || n >= sizeof(sHost)) {
        return;
    }
    memcpy(sHost, p, n);
    sHost[n] = '\0';
    snprintf(sUrl, sizeof(sUrl), "https://%s", sHost);
    if (sHost[0] == '[') { /* [v6]:port */
        n = strcspn(sHost + 1, "]");
        snprintf(sHostName, sizeof(sHostName), "%.*s", (int)n, sHost + 1);
    } else {
        n = strcspn(sHost, ":");
        snprintf(sHostName, sizeof(sHostName), "%.*s", (int)n, sHost);
    }
}

/* the CA list: the package's, plus online_ca's certificate in a list of its own under ux0:data/bt4/net/lib/ (curl_ws_io.h
   looks for <base>lib/cacert.pem, so the SDL base folder becomes ux0:data/bt4/net/) */
static void make_ca(void) {
    snprintf(sCaFile, sizeof(sCaFile), "app0:lib/cacert.pem");
    snprintf(gVitaSdlBase, sizeof(gVitaSdlBase), "app0:");
    if (sCa[0] != '\0') {
        FILE *in, *out;
        char buf[4096];
        size_t n, total = 0;
        SDL_CreateDirectory(PORT_VITA_HOME "/net/lib");
        out = fopen(PORT_VITA_HOME "/net/lib/cacert.pem", "wb");
        if (out == NULL) {
            say("net: cannot write %s/net/lib/cacert.pem: online_ca left out\n", PORT_VITA_HOME);
            return;
        }
        for (int k = 0; k < 2; k++) {
            in = fopen(k == 0 ? "app0:lib/cacert.pem" : sCa, "rb");
            if (in == NULL) {
                say("net: cannot read %s\n", k == 0 ? "app0:lib/cacert.pem" : sCa);
                continue;
            }
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
                fwrite(buf, 1, n, out);
                total += n;
            }
            fputc('\n', out);
            fclose(in);
        }
        fclose(out);
        snprintf(sCaFile, sizeof(sCaFile), PORT_VITA_HOME "/net/lib/cacert.pem");
        snprintf(gVitaSdlBase, sizeof(gVitaSdlBase), PORT_VITA_HOME "/net/");
        say("net: CA list = the package's + %s (%u bytes, %s)\n", sCa, (unsigned)total, sCaFile);
    }
}

/* ---- every curl request logged (-Wl,--wrap) ---- */
CURL *__real_curl_easy_init(void);
CURLcode __real_curl_easy_perform(CURL *c);
CURLMcode __real_curl_multi_add_handle(CURLM *m, CURL *c);

CURL *__wrap_curl_easy_init(void) {
    CURL *c = __real_curl_easy_init();
    if (c != NULL && sCaFile[0] != '\0') {
        curl_easy_setopt(c, CURLOPT_CAINFO, sCaFile); /* (curl's own default list is a path the Vita does not have) */
    }
    return c;
}

static void url_noquery(CURL *c, char *out, size_t size) {
    char *u = NULL;
    curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &u);
    snprintf(out, size, "%.*s", u != NULL ? (int)strcspn(u, "?") : 0, u != NULL ? u : "");
}

CURLcode __wrap_curl_easy_perform(CURL *c) {
    unsigned long long t = now_us();
    CURLcode rc = __real_curl_easy_perform(c);
    char url[256];
    char *method = NULL;
    long code = 0;
    curl_off_t tls = 0, total = 0;
    url_noquery(c, url, sizeof(url));
    curl_easy_getinfo(c, CURLINFO_EFFECTIVE_METHOD, &method);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_getinfo(c, CURLINFO_APPCONNECT_TIME_T, &tls);
    curl_easy_getinfo(c, CURLINFO_TOTAL_TIME_T, &total);
    Vita_Log("net: %s %s -> %ld in %u ms (TLS ready at %u ms), curl %d%s%s\n", method != NULL ? method : "?", url, code,
             (unsigned)((now_us() - t) / 1000), (unsigned)(tls / 1000), (int)rc, rc != CURLE_OK ? " " : "",
             rc != CURLE_OK ? curl_easy_strerror(rc) : "");
    return rc;
}

CURLMcode __wrap_curl_multi_add_handle(CURLM *m, CURL *c) {
    char url[256];
    url_noquery(c, url, sizeof(url));
    Vita_Log("net: opening %s (a connection held open: WebSocket)\n", url[0] != '\0' ? url : "(address not known yet)");
    return __real_curl_multi_add_handle(m, c);
}

/* ---- start-up and shut-down ---- */
static int net_start(void) {
    SceNetInitParam p;
    SceNetCtlInfo info;
    int r, state = -1;
    unsigned long long t = now_us();
    if (sUp) {
        return 1;
    }
    sHeap0 = heap_used();
    sHeapPeak = sHeap0;
    r = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (r < 0) {
        say("net: SceNet module not loaded: 0x%08X\n", (unsigned)r);
        return 0;
    }
    sPool = malloc((size_t)sPoolKb * 1024);
    if (sPool == NULL) {
        say("net: no memory for SceNet's pool (%d KB)\n", sPoolKb);
        return 0;
    }
    p.memory = sPool;
    p.size = sPoolKb * 1024;
    p.flags = 0;
    r = sceNetInit(&p);
    if (r < 0 && (unsigned)r != 0x80410110u /* SCE_NET_ERROR_EBUSY: already up */) {
        say("net: sceNetInit failed: 0x%08X\n", (unsigned)r);
        free(sPool);
        sPool = NULL;
        return 0;
    }
    r = sceNetCtlInit();
    if (r < 0 && (unsigned)r != 0x80412102u /* already */) {
        say("net: sceNetCtlInit failed: 0x%08X\n", (unsigned)r);
    }
    sUp = 1;
    sceNetCtlInetGetState(&state);
    memset(&info, 0, sizeof(info));
    sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info);
    {
        char ip[16];
        SceNetCtlInfo rssi;
        snprintf(ip, sizeof(ip), "%s", info.ip_address);
        memset(&rssi, 0, sizeof(rssi));
        sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &rssi);
        say("net: up in %u ms: pool %d KB, state %d (%s), address %s, Wi-Fi signal %u%%\n", (unsigned)((now_us() - t) / 1000), sPoolKb, state,
            state == SCE_NETCTL_STATE_CONNECTED ? "connected" : state == SCE_NETCTL_STATE_CONNECTING ? "connecting" : "not connected",
            ip[0] != '\0' ? ip : "none", rssi.rssi_percentage);
    }
    make_ca();
    r = curl_global_init(CURL_GLOBAL_DEFAULT);
    say("net: %s, CA list %s%s\n", curl_version(), sCaFile, r != 0 ? " (curl_global_init failed)" : "");
    say_memory("after start-up");
    return 1;
}

void Vita_NetStop(void) {
    if (!sUp) {
        return;
    }
    Bt4Signal_Close();
    curl_global_cleanup();
    sceNetCtlTerm();
    sceNetTerm();
    free(sPool);
    sPool = NULL;
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    sUp = 0;
    Vita_Log("net: down (C heap in use %d KB, %+d KB against before the network)\n", heap_used() / 1024, (heap_used() - sHeap0) / 1024);
}

/* ---- the self-test ---- */
static int sPass, sFail;
static void result(int ok, const char *what) {
    ok ? sPass++ : sFail++;
    say("net: test %s: %s\n", what, ok ? "OK" : "FAILED");
}

static int t_dns(const char *name) {
    struct addrinfo hints, *res = NULL, *a;
    unsigned long long t = now_us();
    char list[200] = "";
    int r;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    r = getaddrinfo(name, NULL, &hints, &res);
    for (a = res; a != NULL && strlen(list) < sizeof(list) - 20; a = a->ai_next) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((struct sockaddr_in *)a->ai_addr)->sin_addr, ip, sizeof(ip));
        strcat(list, list[0] ? " " : "");
        strcat(list, ip);
    }
    if (res != NULL) {
        freeaddrinfo(res);
    }
    say("net: dns %s -> %s in %u ms (getaddrinfo %d)\n", name, r == 0 ? list : "nothing", (unsigned)((now_us() - t) / 1000), r);
    return r == 0;
}

/* to <ip:port>: ten numbered datagrams, each answered by the test server; as gs/net.c does it (a non-blocking socket,
   recvfrom polled); the LAN address as gs/net.c's lan_address finds it (connect + getsockname) */
static int t_udp(const char *target) {
    char host[128];
    const char *colon = strrchr(target, ':');
    struct sockaddr_in to, from, me;
    socklen_t len;
    int s, i, got = 0;
    unsigned minUs = ~0u, maxUs = 0, sumUs = 0;
    if (colon == NULL || colon == target) {
        say("net: udp: no address (online_udp=<ip:port>)\n");
        return 0;
    }
    snprintf(host, sizeof(host), "%.*s", (int)(colon - target), target);
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons((unsigned short)atoi(colon + 1));
    if (inet_pton(AF_INET, host, &to.sin_addr) != 1) {
        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        if (getaddrinfo(host, NULL, &hints, &res) != 0 || res == NULL) {
            say("net: udp: %s does not resolve\n", host);
            return 0;
        }
        to.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        say("net: udp: socket() failed\n");
        return 0;
    }
    { /* the LAN address (gs/net.c lan_address's way: a UDP socket connected, then asked its own name) */
        int c = socket(AF_INET, SOCK_DGRAM, 0);
        char ip[INET_ADDRSTRLEN] = "?";
        if (c >= 0) {
            len = sizeof(me);
            if (connect(c, (struct sockaddr *)&to, sizeof(to)) == 0 && getsockname(c, (struct sockaddr *)&me, &len) == 0) {
                inet_ntop(AF_INET, &me.sin_addr, ip, sizeof(ip));
            }
            close(c);
        }
        say("net: udp: this console's LAN address towards %s: %s\n", host, ip);
    }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    for (i = 0; i < 10; i++) {
        char out[64], in[256];
        unsigned long long t = now_us();
        int n = snprintf(out, sizeof(out), "BT4M1 %d %llu", i, t);
        if (sendto(s, out, (size_t)n, 0, (struct sockaddr *)&to, sizeof(to)) != n) {
            say("net: udp: sendto failed\n");
            continue;
        }
        while (now_us() - t < 500000) { /* half a second for each */
            int r;
            len = sizeof(from);
            r = (int)recvfrom(s, in, sizeof(in) - 1, 0, (struct sockaddr *)&from, &len);
            if (r > 0) {
                in[r] = '\0';
                if (r >= n && memcmp(in, out, (size_t)n) == 0) {
                    unsigned us = (unsigned)(now_us() - t);
                    got++;
                    sumUs += us;
                    minUs = us < minUs ? us : minUs;
                    maxUs = us > maxUs ? us : maxUs;
                    break;
                }
                continue;
            }
            sceKernelDelayThread(1000);
        }
    }
    close(s);
    if (got) {
        say("net: udp echo %s: %d of 10 back, round trip %u / %u / %u ms (min / mean / max)\n", target, got, minUs / 1000, sumUs / got / 1000, maxUs / 1000);
    } else {
        say("net: udp echo %s: none of 10 back\n", target);
    }
    return got >= 8;
}

static size_t keep_text(char *data, size_t size, size_t n, void *p) {
    char *out = (char *)p;
    size_t have = strlen(out), add = size * n;
    if (have + add < 1024) {
        memcpy(out + have, data, add);
        out[have + add] = '\0';
    }
    return size * n;
}

/* two GETs on one handle: the first with the TLS handshake, the second on the kept connection */
static int t_https(const char *url, int ownCa) {
    CURL *c = curl_easy_init();
    int ok = 1, k;
    if (c == NULL) {
        return 0;
    }
    if (!ownCa) {
        curl_easy_setopt(c, CURLOPT_CAINFO, "app0:lib/cacert.pem");
    }
    for (k = 0; k < 2; k++) {
        char body[1024] = "";
        long code = 0;
        curl_off_t dns = 0, tcp = 0, tls = 0, total = 0;
        CURLcode rc;
        int heapBefore = heap_used();
        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, keep_text);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, body);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 10000L);
        rc = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_getinfo(c, CURLINFO_NAMELOOKUP_TIME_T, &dns);
        curl_easy_getinfo(c, CURLINFO_CONNECT_TIME_T, &tcp);
        curl_easy_getinfo(c, CURLINFO_APPCONNECT_TIME_T, &tls);
        curl_easy_getinfo(c, CURLINFO_TOTAL_TIME_T, &total);
        body[strcspn(body, "\r\n")] = '\0';
        say("net: https GET %s #%d: %ld, name %u ms, TCP %u ms, TLS %u ms, all %u ms, C heap %+d KB, %s%.100s\n", url, k + 1, code,
            (unsigned)(dns / 1000), (unsigned)(tcp / 1000), (unsigned)(tls / 1000), (unsigned)(total / 1000), (heap_used() - heapBefore) / 1024,
            rc == CURLE_OK ? "body: " : curl_easy_strerror(rc), rc == CURLE_OK ? body : "");
        ok = ok && rc == CURLE_OK && code >= 200 && code < 300;
    }
    curl_easy_cleanup(c);
    return ok;
}

/* upstream's own WebSocket code (gs/curl_ws_io.h, as voice_transport_curl.inc uses it): connect, five texts echoed */
static int t_ws(const char *url) {
    Bt4CurlWs w;
    unsigned long long t = now_us();
    int r = 0, i, got = 0;
    unsigned sumUs = 0, maxUs = 0;
    if (!Bt4CurlWs_Open(&w, url, NULL)) {
        say("net: websocket %s: not opened\n", url);
        return 0;
    }
    while (r == 0 && now_us() - t < 10000000) {
        r = Bt4CurlWs_Connect(&w);
        if (r == 0) {
            Bt4CurlWs_Wait(&w, 0, 50);
        }
    }
    say("net: websocket %s: %s in %u ms%s%s\n", url, r == 1 ? "connected" : "not connected", (unsigned)((now_us() - t) / 1000),
        w.error[0] ? ": " : "", w.error);
    for (i = 0; r == 1 && i < 5; i++) {
        char msg[64], in[256];
        size_t sent = 0;
        int n = snprintf(msg, sizeof(msg), "bt4-m1 echo %d", i);
        unsigned long long s = now_us();
        CURLcode rc = curl_ws_send(w.easy, msg, (size_t)n, &sent, 0, CURLWS_TEXT);
        if (rc != CURLE_OK) {
            say("net: websocket send: %s\n", curl_easy_strerror(rc));
            break;
        }
        while (now_us() - s < 3000000) {
            size_t count = 0;
            const struct curl_ws_frame *meta = NULL;
            rc = curl_ws_recv(w.easy, in, sizeof(in) - 1, &count, &meta);
            if (rc == CURLE_AGAIN) {
                Bt4CurlWs_Wait(&w, 0, 20);
                continue;
            }
            if (rc != CURLE_OK) {
                say("net: websocket recv: %s\n", curl_easy_strerror(rc));
                break;
            }
            if (meta != NULL && (meta->flags & CURLWS_TEXT) && count == (size_t)n && memcmp(in, msg, count) == 0) {
                unsigned us = (unsigned)(now_us() - s);
                got++;
                sumUs += us;
                maxUs = us > maxUs ? us : maxUs;
                break;
            }
        }
    }
    Bt4CurlWs_Close(&w);
    if (got) {
        say("net: websocket echo: %d of 5 back, round trip mean %u ms, max %u ms\n", got, sumUs / got / 1000, maxUs / 1000);
    }
    return got == 5;
}

/* BT4decomp's own signaling client (gs/supabase_signal.cpp + cloudflare_signal.inc) against online_server:
   /v5/auth/start, the socket /v5/socket, its authentication, bt4.ready, then "get" (consumer 0) and "nickname_get"
   (consumer 4) answered. Its identity file: ux0:data/bt4/network/<server>/ (BT4_SIGNAL_SESSION_DIR) */
static int t_signal(void) {
    char dir[256], buf[4096];
    unsigned long long t = now_us();
    int ready = 0, replies = 0, i;
    snprintf(dir, sizeof(dir), PORT_VITA_HOME "/network/");
    for (i = 0; sHost[i] != '\0' && strlen(dir) < sizeof(dir) - 2; i++) {
        char c = sHost[i];
        size_t n = strlen(dir);
        dir[n] = (char)((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ? c : '_');
        dir[n + 1] = '\0';
    }
    SDL_CreateDirectory(dir);
    setenv("BT4_SIGNAL_SESSION_DIR", dir, 1);
    Bt4Signal_Listen(0, 1);
    Bt4Signal_Listen(4, 1);
    if (!Bt4Signal_Command(0, 1, "get", "{}") || !Bt4Signal_Command(4, 2, "nickname_get", "{}")) {
        say("net: signaling: commands not taken\n");
        return 0;
    }
    while (now_us() - t < 20000000 && (ready == 0 || replies < 2)) {
        for (int c = 0; c < 5; c += 4) {
            int r = Bt4Signal_Read(c, buf, sizeof(buf));
            if (r == 1) {
                if (strstr(buf, "\"bt4.ready\"") != NULL) {
                    ready = 1;
                    say("net: signaling: bt4.ready after %u ms (status %d)\n", (unsigned)((now_us() - t) / 1000), Bt4Signal_Status());
                } else if (strstr(buf, "\"reply\"") != NULL) {
                    replies++;
                    say("net: signaling: consumer %d reply after %u ms: %.160s\n", c, (unsigned)((now_us() - t) / 1000), buf);
                } else {
                    say("net: signaling: consumer %d event: %.120s\n", c, buf);
                }
            }
        }
        sceKernelDelayThread(20000);
    }
    say("net: signaling: %s, %d of 2 replies, status %d, %u ms\n", ready ? "socket ready" : "no bt4.ready (HTTPS fallback, or none)", replies,
        Bt4Signal_Status(), (unsigned)((now_us() - t) / 1000));
    Bt4Signal_Listen(0, 0);
    Bt4Signal_Listen(4, 0);
    Bt4Signal_Close();
    return replies == 2;
}

static int self_test(void *unused) {
    char url[260];
    (void)unused;
    sT0 = now_us();
    say("net: self-test (online_test=%d) against %s\n", gVitaOnlineTest, sServer[0] ? sServer : "(no online_server)");
    result(t_dns(sDns), "dns");
    if (sHostName[0] != '\0' && sHostName[0] >= 'A') {
        t_dns(sHostName);
    }
    if (sUdp[0] == '\0' && sHostName[0] != '\0') {
        snprintf(sUdp, sizeof(sUdp), "%s:26011", sHostName);
    }
    if (sUdp[0] != '\0') {
        result(t_udp(sUdp), "udp echo");
    }
    say_memory("after dns and udp");
    if (sUrl[0] != '\0') {
        snprintf(url, sizeof(url), "%s/m1/get", sUrl);
        result(t_https(url, 1), "https get");
        say_memory("after https");
        snprintf(url, sizeof(url), "%s/m1/echo", sUrl);
        result(t_ws(url), "websocket echo");
        say_memory("after websocket");
        result(t_signal(), "signaling (BT4decomp's client)");
        say_memory("after signaling");
    }
    if (sPublic[0] != '\0') {
        result(t_https(sPublic, 0), "https get, public, package CA list");
        say_memory("after the public get");
    }
    say("net: self-test done in %u ms: %d passed, %d failed\n", (unsigned)((now_us() - sT0) / 1000), sPass, sFail);
    return 0;
}

/* vita_main.c, after the settings and the memory's mapping, before the game's thread. 1: online_test=2, the run ends
   (vita_main.c writes the line the runners wait for, "vita: exitafter:", and quits) */
int Vita_NetBoot(void) {
    if (!gVitaOnline && !gVitaOnlineTest) {
        return 0; /* (off: nothing loaded, nothing allocated) */
    }
    parse_server();
    if (!net_start()) {
        return gVitaOnlineTest == 2;
    }
    if (gVitaOnlineTest) {
        SDL_Thread *th;
        sScreen = 1;
        th = SDL_CreateThread(self_test, "bt4_net_test", NULL); /* (a thread of the kind the network's own run on) */
        if (th != NULL) {
            SDL_WaitThread(th, NULL);
        } else {
            say("net: the self-test's thread did not start\n");
        }
        sScreen = 0;
        if (gVitaOnlineTest == 2) {
            Vita_NetStop();
            return 1;
        }
    }
    if (!gVitaOnline) {
        Vita_NetStop();
    }
    return 0;
}
#endif
