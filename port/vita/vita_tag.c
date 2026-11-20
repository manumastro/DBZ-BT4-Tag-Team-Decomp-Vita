/*
 * Tag Team on the PS Vita: the local room (one player and CPU fighters, offline) and, in a build with the network
 * (BT4_VITA_NETLINK=1), the online rooms between Vitas. GPL-3.0-or-later, as port/src/tag_team.c, whose room model
 * (Tt_*) this drives.
 *
 * BT4decomp's port/src/gs/tag_team_net.c holds the local room together with the online ones (TCP, WebSocket,
 * WebRTC through libdatachannel, SDL threads), so it is not built for the Vita. What follows is its local_room
 * branches, as they are there: Port_TagConnect(NULL) opens a room in this process, every change goes straight to
 * the room model. The battle side (src/battle/tag_team_*.inc, battle_work.c TagBattle_ConfigureLocal) reads the room
 * through these functions. The plan: docs/vita/tagteam.md.
 *
 * Online between Vitas (milestone T1, psvita-agent notes/online/vita-multiplayer-plan.md 7.7, 7.10): the branches of
 * tag_team_net.c's peer mode (the PC's participant-hosted rooms), copied here with "net_room" in place of
 * "peer_mode", over port/vita/vita_tag_net.c (the Vita's TagPeer: the owner's authority, TagPdp's two channels over
 * UDP or Ad Hoc) in place of tag_team_p2p.cpp. Port_TagConnect("vita-udp" / "vita-adhoc") hosts, "vita-udp:IP:PORT" /
 * "vita-adhoc:MAC" joins; the menus' rows: Port_TagEndpoint, Port_TagJoinAddress. Without the network part the online
 * calls answer as no network (Port_TagNetwork 0, in vita_bt4.c).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
#include "tag_team.h"
#include "tag_team_wire.h"

static TtRoom state;
static uint32_t member;
static int local_room, cpu_fill, cpu_difficulty = 2, configured_ffa, configured_quick;
static int offline_mode, offline_custom, offline_beans; /* (BT4decomp 0.1.39.6: its dedicated offline menu's room) */
static char room_code[7], error[128];

#ifdef BT4_VITA_NETLINK
/* ======== online between Vitas: tag_team_net.c's peer mode over vita_tag_net.c ======== */
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <psp2/kernel/threadmgr.h>
#include "tag_team_sync.h"
#include "vita_tag_net.h"
extern void Vita_Log(const char *fmt, ...);
extern int Port_Setting(const char *, int);

#define VITA_TAG_PORT 26200      /* the room's UDP / Ad Hoc port (bt3.txt tagnet_port) */

static int net_room;             /* (tag_team_net.c: peer_mode) */
static int net_adhoc, net_fd = -1;
static Vtn *vnet;
static SceUID net_thread = -1, net_mutex = -1;
static volatile int net_quit;
static TtInputs received;
static unsigned char client_in[64][TT_WIRE_BYTES];
static uint64_t client_at[64];
static unsigned char shared_options_packet[TT_WIRE_BYTES];
static int shared_options_pending, configure_pending, configured_left, configured_right, cancel_pending;
static char chat_lines[6][192];
static int chat_count;
static uint64_t last_heard_ms, ping_at_ms;
static unsigned request_serial;
static struct { unsigned serial, command, epoch; uint64_t at; } request_history[128];
static int render_active, own_camera;
static unsigned net_logged_ms;
/* (tag_team_sync_client.inc) */
static int tag_paced, tag_input_sent, tag_sync_count, tag_received_valid, tag_catchup;
static uint32_t tag_input_epoch, tag_input_latest, tag_input_first, tag_sync_last_frame, tag_pending_buttons;
static unsigned char tag_input_pad[TT_PAD_BYTES], tag_pending_l3, tag_pending_skip;
static unsigned tag_pending_coop;
static unsigned tag_delay_last_adjust_frame, tag_delay_clean_frames;
static int tag_delay_adjusted, tag_delay_margin = 1, tag_delay_feedback_seen;
static uint32_t tag_delay_feedback_epoch, tag_delay_feedback_frame, tag_received_epoch, tag_received_latest;
static unsigned tag_sync_words[TT_MAX_PLAYERS][TT_SYNC_WORDS];

static uint64_t ms_now(void) { return sceKernelGetProcessTimeWide() / 1000; }
static void net_lock(void) { if (net_mutex >= 0) sceKernelLockMutex(net_mutex, 1, NULL); }
static void net_unlock(void) { if (net_mutex >= 0) sceKernelUnlockMutex(net_mutex, 1); }

/* ---- the links under the room: UDP (LAN / internet by address) and Ad Hoc (PDP, no access point) ---- */
static int udp_send(void *ctx, const VtnAddr *to, const void *data, int len) {
    struct sockaddr_in a;
    (void)ctx;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    memcpy(&a.sin_addr.s_addr, to->b, 4);
    memcpy(&a.sin_port, to->b + 4, 2);
    return (int)sendto(net_fd, data, (size_t)len, 0, (struct sockaddr *)&a, sizeof a);
}
static int udp_recv(void *ctx, VtnAddr *from, void *buf, int size) {
    struct sockaddr_in a;
    socklen_t l = sizeof a;
    int n;
    (void)ctx;
    n = (int)recvfrom(net_fd, buf, (size_t)size, MSG_DONTWAIT, (struct sockaddr *)&a, &l);
    if (n <= 0) return 0; /* (would block, or an ICMP error of an earlier send: nothing to read now) */
    memset(from, 0, sizeof *from);
    memcpy(from->b, &a.sin_addr.s_addr, 4);
    memcpy(from->b + 4, &a.sin_port, 2);
    return n;
}
static int ah_send(void *ctx, const VtnAddr *to, const void *data, int len) {
    extern int Vita_AdhocSend(const unsigned char mac[6], int port, const void *data, int len);
    (void)ctx;
    return Vita_AdhocSend(to->b, to->b[6] | (to->b[7] << 8), data, len) ? len : -1;
}
static int ah_recv(void *ctx, VtnAddr *from, void *buf, int size) {
    extern int Vita_AdhocRecv(unsigned char mac[6], int *port, void *buf, int size);
    unsigned char mac[6];
    int port = 0, n = Vita_AdhocRecv(mac, &port, buf, size);
    (void)ctx;
    if (n <= 0) return 0;
    memset(from, 0, sizeof *from);
    memcpy(from->b, mac, 6);
    from->b[6] = (uint8_t)port;
    from->b[7] = (uint8_t)(port >> 8);
    return n;
}
static uint64_t link_now(void *ctx) { (void)ctx; return sceKernelGetProcessTimeWide(); }
static const VtnLink udp_link = { udp_send, udp_recv, link_now };
static const VtnLink adhoc_link = { ah_send, ah_recv, link_now };

/* the room's own thread: one round of vita_tag_net.c every 2 ms (the PC's "BT4 P2P room" thread) */
static int net_thread_main(SceSize args, void *argp) {
    (void)args; (void)argp;
    while (!net_quit) {
        net_lock();
        Vtn_Step(vnet);
        net_unlock();
        sceKernelDelayThread(2000);
    }
    return 0;
}

static void net_stop(void) {
    if (net_thread >= 0) {
        net_quit = 1;
        sceKernelWaitThreadEnd(net_thread, NULL, NULL);
        sceKernelDeleteThread(net_thread);
        net_thread = -1;
    }
    if (vnet) {
        char line[600];
        Vtn_Describe(vnet, line, sizeof line);
        Vita_Log("tagnet: room closed: %s\n", line);
        Vtn_Close(vnet);
        free(vnet);
        vnet = NULL;
    }
    if (net_mutex >= 0) { sceKernelDeleteMutex(net_mutex); net_mutex = -1; }
    if (net_fd >= 0) { close(net_fd); net_fd = -1; }
    if (net_adhoc) { extern void Vita_AdhocStop(void); Vita_AdhocStop(); net_adhoc = 0; }
}

/* "vita-udp" / "vita-adhoc" host here; "vita-udp:IP:PORT" / "vita-adhoc:MAC" join that room */
static int net_connect(const char *server) {
    VtnAddr owner;
    int join = 0, port = Port_Setting("tagnet_port", VITA_TAG_PORT);
    memset(&owner, 0, sizeof owner);
    if (!strncmp(server, "vita-adhoc", 10)) {
        extern int Vita_AdhocOpen(int port);
        unsigned m[6];
        if (server[10] == ':') {
            if (sscanf(server + 11, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) {
                snprintf(error, sizeof(error), "Invalid Ad Hoc address.");
                return 0;
            }
            for (join = 0; join < 6; ++join) owner.b[join] = (uint8_t)m[join];
            owner.b[6] = (uint8_t)port; owner.b[7] = (uint8_t)(port >> 8);
            join = 1;
        }
        if (!Vita_AdhocOpen(port)) {
            snprintf(error, sizeof(error), "Join an Ad Hoc group first.");
            return 0;
        }
        net_adhoc = 1;
    } else if (!strncmp(server, "vita-udp", 8)) {
        extern int Vita_NetUp(void);
        struct sockaddr_in a;
        if (server[8] == ':') {
            char ip[64];
            const char *colon = strrchr(server + 9, ':');
            int p = colon ? atoi(colon + 1) : 0;
            struct in_addr in;
            if (!colon || colon - (server + 9) > 63 || p < 1 || p > 65535) {
                snprintf(error, sizeof(error), "Write the room's address as IPv4:port.");
                return 0;
            }
            memcpy(ip, server + 9, (size_t)(colon - (server + 9)));
            ip[colon - (server + 9)] = 0;
            if (inet_pton(AF_INET, ip, &in) != 1) {
                snprintf(error, sizeof(error), "Invalid room address.");
                return 0;
            }
            memcpy(owner.b, &in.s_addr, 4);
            owner.b[4] = (uint8_t)(p >> 8); owner.b[5] = (uint8_t)p; /* (network order, as sin_port) */
            join = 1;
        }
        if (!Vita_NetUp()) {
            snprintf(error, sizeof(error), "The network is not available.");
            return 0;
        }
        net_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (net_fd < 0) {
            snprintf(error, sizeof(error), "Cannot open the room's connection.");
            return 0;
        }
        fcntl(net_fd, F_SETFL, O_NONBLOCK);
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons((unsigned short)(join ? 0 : port));
        if (bind(net_fd, (struct sockaddr *)&a, sizeof a) < 0) {
            snprintf(error, sizeof(error), "The room's port %d is busy.", port);
            close(net_fd);
            net_fd = -1;
            return 0;
        }
    } else {
        snprintf(error, sizeof(error), "Online Tag Team on the PS Vita plays between Vitas: by address or Ad Hoc.");
        return 0;
    }
    vnet = (Vtn *)malloc(sizeof(Vtn));
    if (!vnet) {
        snprintf(error, sizeof(error), "Not enough memory for the room.");
        net_stop();
        return 0;
    }
    Vtn_Open(vnet, net_adhoc ? &adhoc_link : &udp_link, NULL, join ? &owner : NULL);
    Vtn_SetRate(vnet, (unsigned)Port_Setting("tagnet_rate", VTN_RATE_DEFAULT));
    net_mutex = sceKernelCreateMutex("bt4_tagnet", 0, 0, NULL);
    net_quit = 0;
    net_thread = sceKernelCreateThread("bt4_tagnet", net_thread_main, 0x10000100, 0x10000, 0, 0, NULL);
    if (net_mutex < 0 || net_thread < 0 || sceKernelStartThread(net_thread, 0, NULL) < 0) {
        snprintf(error, sizeof(error), "Cannot start the room's connection.");
        net_stop();
        return 0;
    }
    Vita_Log("tagnet: %s %s (%s, port %d, %u bytes, ticks %u per 1000 s)\n", join ? "joining" : "hosting", server,
             net_adhoc ? "Ad Hoc" : "UDP", port, (unsigned)sizeof(Vtn), (unsigned)vnet->rate);
    net_room = 1;
    tag_paced = 1;
    last_heard_ms = ms_now();
    return 1;
}

static int solo_cpu_fight(void) { return net_room && state.phase == TT_FIGHT && state.host == member && Tt_Humans(&state) == 1; }

static void fail(const char *why) {
    Vita_Log("tagnet: %s\n", why);
    tag_catchup = 0;
    net_stop();
    state.phase = TT_ABORTED;
    snprintf(error, sizeof(error), "%s", why);
}

/* tag_team_net.c request */
static void request(unsigned char *p) {
    unsigned serial = ++request_serial, slot = serial % 128;
    int ok;
    request_history[slot].serial = serial;
    request_history[slot].command = Ttw_Read(p + 4);
    request_history[slot].epoch = Ttw_Read(p + 8);
    request_history[slot].at = ms_now();
    Ttw_Write(p + 508, serial);
    if (!vnet) return;
    net_lock();
    ok = Vtn_Send(vnet, p);
    net_unlock();
    if (!ok) fail("The direct connection to the room was lost.");
}

/* (tag_team_sync_client.inc) */
static int tag_backlog(void) {
    int distance;
    if (!tag_paced || state.phase != TT_FIGHT || !tag_received_valid || tag_received_epoch != received.epoch || received.epoch != state.epoch) return 0;
    distance = (int32_t)(tag_received_latest - received.next);
    return distance >= 0 ? distance + 1 : 0;
}
static void tag_update_catchup(void) {
    int backlog = tag_backlog(), old = tag_catchup;
    if (backlog >= 6) tag_catchup = 1;
    else if (backlog <= 2) tag_catchup = 0;
    if (tag_catchup != old && ms_now() - net_logged_ms > 1000) {
        net_logged_ms = (unsigned)ms_now();
        Vita_Log("tagnet: catch-up %s (backlog %d, next %u, latest %u)\n", tag_catchup ? "on" : "off", backlog, received.next, tag_received_latest);
    }
}
static void tag_delay_feedback(unsigned frame, unsigned late_mask) {
    int seat = Tt_Seat(&state, member);
    if (!tag_paced || seat < 0 || !tag_input_sent || tag_input_epoch != state.epoch || frame < tag_input_first || frame > tag_input_latest) return;
    if (tag_delay_feedback_seen && tag_delay_feedback_epoch == state.epoch && (int32_t)(frame - tag_delay_feedback_frame) <= 0) return;
    tag_delay_feedback_epoch = state.epoch; tag_delay_feedback_frame = frame; tag_delay_feedback_seen = 1;
    if (late_mask & (1u << seat)) {
        tag_delay_clean_frames = 0;
        if (tag_delay_margin < 8 && (!tag_delay_adjusted || frame - tag_delay_last_adjust_frame >= 8)) {
            ++tag_delay_margin; tag_delay_last_adjust_frame = frame; tag_delay_adjusted = 1;
        }
    } else if (++tag_delay_clean_frames >= 342) {
        if (tag_delay_margin > 1) --tag_delay_margin;
        tag_delay_clean_frames = 0;
    }
}
static void tag_sync_notice(const unsigned char *p) {
    Vita_Log("tagnet: sync: epoch=%u frame=%u kind=%u streak=%u actor=%d field=%d peer-a=%d value-a=%d peer-b=%d value-b=%d local-last-sent-frame=%u\n",
             Ttw_Read(p + 8), Ttw_Read(p + 12), Ttw_Read(p + 16), Ttw_Read(p + 20), (int)Ttw_Read(p + 24), (int)Ttw_Read(p + 28),
             (int)Ttw_Read(p + 32), (int)Ttw_Read(p + 40), (int)Ttw_Read(p + 36), (int)Ttw_Read(p + 44), tag_sync_last_frame);
}
static int shared_options_save(const TtOptions *options) {
    unsigned char p[512];
    if (configured_quick || state.phase != TT_LOBBY || member != state.host || !state.options.version || shared_options_pending || !Tt_OptionsValid(options)) return 0;
    Ttw_Request(p, TTW_OPTIONS, state.epoch); Ttw_PutOptions(p + 24, options); shared_options_pending = 1; request(p); return 1;
}
static void write64(unsigned char *p, uint64_t v) { Ttw_Write(p, (uint32_t)v); Ttw_Write(p + 4, (uint32_t)(v >> 32)); }

/* the poll of a room between Vitas (tag_team_net.c Port_TagPoll, peer mode: no WebSocket, direct route or voice) */
static void net_poll(void) {
    int n = 0, i, result;
    if (!vnet) return;
    net_lock();
    if (vnet->status < 0) {
        const char *why = vnet->error;
        net_unlock();
        fail(why ? why : "Could not establish or maintain a direct connection to the room.");
        return;
    }
    while (n < 64 && Vtn_Read(vnet, client_in[n], &client_at[n]) > 0) ++n;
    net_unlock();
    for (i = 0; i < n && net_room; ++i) {
        unsigned char *in = client_in[i];
        last_heard_ms = ms_now();
        if (Ttw_Read(in) != TT_PROTOCOL) continue;
        if (Ttw_Read(in + 4) == TT_SYNC_NOTICE) { tag_sync_notice(in); continue; }
        if (Ttw_Read(in + 4) == TTW_FRAME) {
            uint32_t epoch = Ttw_Read(in + 8), frame = Ttw_Read(in + 12);
            int seat;
            if (epoch != state.epoch) { if ((int32_t)(epoch - state.epoch) < 0) continue; fail("Unexpected TAG TEAM input epoch."); return; }
            if (state.phase != TT_FIGHT) continue;
            if (received.epoch != epoch) Tt_InputsReset(&received, epoch, Tt_Count(&state));
            if ((int32_t)(frame - received.next) < 0) continue;
            if (tag_paced && Ttw_Read(in + 492) == 0x32535442u) tag_delay_feedback(frame, Ttw_Read(in + 496));
            if (tag_paced && (int32_t)(frame - received.next) >= TT_INPUT_HISTORY) {
                fail("This client fell too far behind the battle. The other players can continue."); return;
            }
            for (seat = 0; seat < Tt_Count(&state); ++seat)
                if (Tt_InputPut(&received, epoch, seat, frame, in + 16 + seat * TT_PAD_BYTES) != TT_OK) { fail("Invalid TAG TEAM input frame."); return; }
            if (!tag_received_valid || tag_received_epoch != epoch || (int32_t)(frame - tag_received_latest) > 0) tag_received_latest = frame;
            tag_received_epoch = epoch; tag_received_valid = 1; tag_update_catchup();
            continue;
        }
        if (Ttw_Read(in + 4) == TTW_CHAT) {
            int seat = Tt_Seat(&state, Ttw_Read(in + 12)), j;
            char text[160];
            for (j = 0; j < 159 && in[16 + j]; ++j) { unsigned char c = in[16 + j]; text[j] = c >= 32 && c < 127 ? (char)c : '?'; }
            text[j] = 0;
            if (seat >= 0) {
                if (chat_count == 6) { memmove(chat_lines, chat_lines + 1, 5 * sizeof(chat_lines[0])); --chat_count; }
                snprintf(chat_lines[chat_count++], 192, "%s: %s", state.players[seat].name, text);
            }
            continue;
        }
        if (Ttw_Read(in + 4) == TTW_OPTIONS_STATE) { memcpy(shared_options_packet, in, TT_WIRE_BYTES); continue; }
        if (!Ttw_Decode(in, &state, &member, &result, room_code)) { fail("Incompatible TAG TEAM room."); return; }
        if (state.phase != TT_CLOSED && !state.options_hash) { fail("This TAG TEAM room needs the same updated test build on every client."); return; }
        if (state.options_hash && !Ttw_ApplyOptions(shared_options_packet, &state)) { fail("Shared TAG TEAM settings are missing or incompatible."); return; }
        shared_options_pending = 0;
        if (received.epoch != state.epoch || (state.phase != TT_FIGHT && received.seats != Tt_Count(&state))) {
            Tt_InputsReset(&received, state.epoch, Tt_Count(&state));
            tag_received_valid = 0; tag_catchup = 0;
        }
        tag_update_catchup();
        if (state.phase == TT_LOBBY || result) cancel_pending = 0;
        if (result) {
            static const char *messages[] = {"", "Wrong room phase.", "Room or player not found.", "This room is full.", "Only the host can do that.", "Wait for your team's turn.", "Invalid fighter or room settings.", "Waiting for all players.", "The room changed. Try again.", "The room setup or base game data does not match.", "The battle states remained different. A synchronization report was saved in the log."};
            unsigned ack = Ttw_Read(in + 508), slot = ack % 128;
            Vita_Log("tagnet: request rejected: result=%d epoch=%u phase=%d ack=%u command=%u\n", result, state.epoch, state.phase, ack,
                     request_history[slot].serial == ack ? request_history[slot].command : 0);
            snprintf(error, sizeof(error), "%s", result > 0 && result <= TT_DESYNC ? messages[result] : "TAG TEAM request rejected.");
        } else error[0] = 0;
        if (!result && state.phase == TT_LOBBY && member == state.host && configure_pending == 1) {
            extern int Port_TagTeams(int, int);
            configure_pending = 2; Port_TagTeams(configured_left, configured_right);
        } else if (!result && state.phase == TT_LOBBY && state.epoch > 1 && configure_pending == 2) configure_pending = 0;
    }
    if (!net_room) return;
    if (member && ms_now() - last_heard_ms > 10000) { fail("TAG TEAM room stopped responding."); return; }
    if (ms_now() - ping_at_ms >= (uint64_t)(state.phase == TT_FIGHT ? 500 : 1000)) {
        unsigned char b[TT_WIRE_BYTES];
        Ttw_Request(b, TTW_PING, state.epoch); request(b); ping_at_ms = ms_now();
    }
}
#endif /* BT4_VITA_NETLINK */

/* ---- the room ---- */
int Port_TagConnect(const char *server) {
    extern void Port_TagClose(void);
    Port_TagClose();
    if (server && server[0]) {
#ifdef BT4_VITA_NETLINK
        return net_connect(server);
#else
        snprintf(error, sizeof(error), "Online Tag Team is not available on the PS Vita.");
        return 0;
#endif
    }
    local_room = 1;
    member = 1;
    return 1;
}
void Port_TagClose(void) {
#ifdef BT4_VITA_NETLINK
    if (net_room) net_stop();
    net_room = 0;
    memset(&received, 0, sizeof(received));
    memset(shared_options_packet, 0, sizeof(shared_options_packet));
    shared_options_pending = configure_pending = cancel_pending = chat_count = 0;
    tag_paced = tag_input_sent = tag_sync_count = tag_received_valid = tag_catchup = 0;
    tag_delay_margin = 1; tag_delay_clean_frames = 0; tag_delay_feedback_seen = tag_delay_adjusted = 0;
    tag_pending_coop = 0; tag_pending_buttons = tag_pending_l3 = tag_pending_skip = 0;
    ping_at_ms = 0;
    { extern void Port_TagWaitResumed(void), Port_TagWaitClear(void); Port_TagWaitResumed(); Port_TagWaitClear(); }
#endif
    offline_mode = 0;
    memset(&state, 0, sizeof(state));
    member = 0;
    local_room = 0;
    room_code[0] = error[0] = 0;
}
void Port_TagCPUOptions(int fill, int difficulty) { cpu_fill = fill == 1; cpu_difficulty = difficulty >= 0 && difficulty <= 4 ? difficulty : 2; }
void Port_TagModeOptions(int free_for_all, int quick) { configured_ffa = free_for_all == 1; configured_quick = quick == 1; }
void Port_TagOpenWithRules(int size, int limit, int delay, const char *name, uint64_t content, unsigned rules) {
    (void)delay;
    if (configured_ffa && configured_quick) size = 3;
#ifdef BT4_VITA_NETLINK
    if (net_room) { /* tag_team_net.c: the OPEN request, with the shared options of the expanded rules */
        unsigned char b[TT_WIRE_BYTES];
        TtOptions options;
        int i, f;
        static const char *const keys[] = {"tag_off_custom", "tag_off_senzu", "tag_off_joint", "tag_off_fusion", "tag_fusion_seconds",
            "tag_revive_team1", "tag_revive_team2", "tag_revive_seconds", "tag_revive_charge", "tag_revive_health", "tag_revive_radius", "tag_revive_cpu", "tag_revive_opacity"};
        static const char *const fields[] = {"scale", "max", "start", "percent", "freeze"};
        int teamIndex[2] = {0, 0};
        Ttw_Request(b, TTW_OPEN, 0); Ttw_Write(b + 12, size); Ttw_Write(b + 16, limit); Ttw_Write(b + 20, 0);
        write64(b + 24, content); if (name) snprintf((char *)b + 32, 20, "%s", name);
        Ttw_Write(b + 52, cpu_fill); Ttw_Write(b + 56, cpu_difficulty);
        Ttw_Write(b + 60, configured_ffa); Ttw_Write(b + 64, configured_quick);
        Ttw_Write(b + TTW_OPEN_BATTLE_RULES, rules);
        Tt_OptionsDefault(&options);
        for (i = 0; i < TT_OPTION_SCALARS; ++i) options.value[i] = Port_Setting(keys[i], options.value[i]);
        for (i = 0; i < 10; ++i) {
            int team = i < configured_left + configured_right ? (i == 0 ? 0 : i == 1 ? 1 : i < configured_left + 1 ? 0 : 1) : i & 1;
            int slot = teamIndex[team]++;
            if (slot >= 5) slot = 4;
            if (!configured_quick) {
                char key[24];
                options.version = TT_OPTION_COMBAT_VERSION;
                snprintf(key, sizeof(key), "tag_combat%d%d", team + 1, slot + 1);
                options.value[TT_OPTION_LEGACY_WORDS + i] = Port_Setting(key, TT_COMBAT_DEFAULT);
            }
            for (f = 0; f < 5; ++f) {
                char key[24];
                snprintf(key, sizeof(key), "tag_hp%d%d_%s", team + 1, slot + 1, fields[f]);
                options.value[TT_OPTION_SCALARS + i * 5 + f] = Port_Setting(key, options.value[TT_OPTION_SCALARS + i * 5 + f]);
            }
        }
        if (configured_quick) { Tt_OptionsDefault(&options); options.value[TT_OPT_PRESETS] = 0; }
        if (!Tt_OptionsValid(&options)) { fail("Invalid shared TAG TEAM settings."); return; }
        Ttw_PutOptions(b + TTW_OPEN_OPTIONS, &options);
        request(b);
        return;
    }
#endif
    if (!local_room) return;
    Tt_Open(&state, member, name, size, content);
    state.time = limit;
    state.delay = 0;
    state.battle_rules = rules;
    Tt_SetMode(&state, member, state.epoch, configured_ffa);
    if (cpu_fill) Tt_FillCPU(&state, member, state.epoch, cpu_difficulty);
    memcpy(room_code, "LOCAL0", 7);
}
void Port_TagOpen(int size, int limit, int delay, const char *name, uint64_t content) {
    Port_TagOpenWithRules(size, limit, delay, name, content, 0);
}
int Port_TagTeams(int left, int right) {
#ifdef BT4_VITA_NETLINK
    if (net_room) { Port_TagCommand(TTW_TEAMS, left, right); return !error[0]; }
#endif
    return local_room && Tt_SetTeams(&state, member, state.epoch, left, right) == TT_OK;
}
void Port_TagInitialTeams(int left, int right) {
#ifdef BT4_VITA_NETLINK
    if (net_room) { configured_left = left; configured_right = right; configure_pending = 1; return; }
#endif
    Port_TagTeams(left, right);
}
int Port_TagSlot(int seat, int cpu, int difficulty) {
#ifdef BT4_VITA_NETLINK
    if (net_room) {
        unsigned char p[512];
        Ttw_Request(p, TTW_SLOT, state.epoch); Ttw_Write(p + 12, seat); Ttw_Write(p + 16, cpu); Ttw_Write(p + 20, difficulty);
        request(p);
        return !error[0];
    }
#endif
    return local_room && Tt_SetSlot(&state, member, state.epoch, seat, cpu, difficulty) == TT_OK;
}
int Port_TagMovePlayer(int from, int to) {
#ifdef BT4_VITA_NETLINK
    if (net_room) { Port_TagCommand(TTW_MOVE, from, to); return !error[0]; }
#endif
    return local_room && Tt_MovePlayer(&state, member, state.epoch, from, to) == TT_OK;
}

int Port_TagRulesV2(void) { return state.options.version == TT_OPTION_COMBAT_VERSION; }
int Port_TagDraftSeat(void) {
    int team, seat = Tt_Seat(&state, member);
    if (seat < 0) return -1;
    if (state.phase != TT_SELECT) return -1; /* (as BT4decomp 0.1.39.6's) */
    if (Port_TagRulesV2()) { /* (tag_team_net.c: the expanded rules' draft, every player at once; a local room has none) */
        int i;
        if (!state.players[seat].confirmed) return seat;
        if (member == state.host)
            for (i = 0; i < Tt_Count(&state); ++i)
                if (state.players[i].cpu && !state.players[i].confirmed) return i;
        return -1;
    }
    if (Tt_Turn(&state, state.players[seat].team) == seat) return seat;
    if (member == state.host)
        for (team = 0; team < 2; ++team) {
            int turn = Tt_Turn(&state, team);
            if (turn >= 0 && state.players[turn].cpu) return turn;
        }
    return -1;
}
void Port_TagCommand(int command, int a, int b) {
    int rc = TT_BAD_PHASE;
#ifdef BT4_VITA_NETLINK
    if (net_room) { /* tag_team_net.c */
        unsigned char packet[TT_WIRE_BYTES];
        Ttw_Request(packet, command, state.epoch);
        if (command == TTW_CANCEL_SELECT) cancel_pending = 1;
        Ttw_Write(packet + 12, a); Ttw_Write(packet + 16, b);
        write64(packet + 24, (command == TTW_LOAD || command == TTW_LOADED) ? Tt_LoadDigest(&state) : state.load_digest);
        if (command == TTW_CHOOSE) { Ttw_Write(packet + 20, (uint32_t)Port_TagDraftSeat()); memset(packet + 24, 0, 16); }
        request(packet);
        return;
    }
#endif
    if (!local_room) return;
    if (command == TTW_RESULT && a == 1) { /* the results' Rematch: back to the character select, same room */
        int i;
        state.result_choice = 1; ++state.epoch; ++state.revision; state.phase = TT_SELECT;
        for (i = 0; i < Tt_Count(&state); ++i) {
            state.players[i].confirmed = 0; state.players[i].chara = -1;
            state.players[i].ended = state.players[i].loaded = state.players[i].armed = 0;
        }
        rc = TT_OK;
    }
    if (command == TTW_SELECT) rc = Tt_BeginSelect(&state, member, state.epoch);
    if (command == TTW_CANCEL_SELECT) rc = Tt_CancelSelect(&state, member, state.epoch);
    if (command == TTW_CHOOSE) rc = Tt_ChooseSeat(&state, member, state.epoch, Port_TagDraftSeat(), a, b);
    if (rc) snprintf(error, sizeof(error), "Cannot apply room action (%d).", rc);
    else error[0] = 0;
}
int Port_TagReselect(void) {
#ifdef BT4_VITA_NETLINK
    if (net_room) return state.phase == TT_SELECT;
#endif
    if (!local_room || state.host != member || state.phase != TT_SELECT) return 0;
    if (Tt_CancelSelect(&state, member, state.epoch) != TT_OK) return 0;
    error[0] = 0;
    return Tt_BeginSelect(&state, member, state.epoch) == TT_OK;
}
int Port_TagCancelSelect(void) {
    if (state.phase != TT_SELECT || Tt_Seat(&state, member) < 0) return 0;
#ifdef BT4_VITA_NETLINK
    if (net_room && !(member && member == state.host)) return 1; /* (tag_team_net.c: only the host takes the room back) */
#endif
    Port_TagCommand(TTW_CANCEL_SELECT, 0, 0);
    return !error[0];
}

/* ---- BT4decomp 0.1.39.6 R1 1.2: the dedicated offline menu's room (Port_TagStartOffline), a player's choice taken
   back, the room's battle rules, and the private rooms' shared options. A local room has no shared options
   (state.options.version 0): those read as the PC's local room reads them (the player's own settings, the defaults). */
int Port_TagNetwork(void);
int Port_TagOfflineMode(void) { return local_room ? offline_mode : 0; }
int Port_TagOfflineCustom(void) { return Port_TagNetwork() ? Port_TagOption("tag_off_custom", 0) : !Port_TagOfflineMode() || offline_custom; }
int Port_TagOfflineBeans(void) { return Port_TagNetwork() ? Port_TagOption("tag_off_senzu", 0) : Port_TagOfflineMode() ? offline_beans : 0; }
int Port_TagStartOffline(int mode, int left, int right, int time_limit, int difficulty, int custom, int beans) {
    int seat;
    if (mode < 1 || mode > 4 || (mode == 4 && left < 2) || left < 1 || left > 5 || right < 1 || right > 5 ||
        time_limit < 0 || time_limit > 4 || difficulty < 0 || difficulty > 4 || beans < 0 || beans > 5)
        return 0;
    if (!Port_TagConnect(NULL)) return 0;
    offline_mode = 0;
    offline_custom = custom != 0;
    offline_beans = beans;
    Tt_Open(&state, member, "Player 1", 2, 0);
    state.time = time_limit;
    state.delay = 0;
    if (Tt_SetTeams(&state, member, state.epoch, left, right) != TT_OK) goto failed;
    for (seat = 1; seat < left + right; ++seat)
        if (Tt_SetSlot(&state, member, state.epoch, seat, 1, difficulty) != TT_OK) goto failed;
    state.players[0].difficulty = difficulty;
    if (Tt_BeginSelect(&state, member, state.epoch) != TT_OK) goto failed;
    if (mode == 4)
        for (seat = 0; seat < left + right; ++seat)
            state.players[seat].team = seat < 2 || seat >= right + 2 ? 0 : 1;
    offline_mode = mode == 4 ? 2 : mode;
    memcpy(room_code, "LOCAL0", 7);
    return 1;
failed:
    Port_TagClose();
    return 0;
}
int Port_TagWithdrawSeat(int seat) {
    extern int Port_TagHost(void), Port_TagSeat(void);
    if (state.phase != TT_SELECT || seat < 0 || seat >= Tt_Count(&state) || !state.players[seat].confirmed) return 0;
    if (state.players[seat].cpu ? !Port_TagHost() : seat != Port_TagSeat()) return 0;
#ifdef BT4_VITA_NETLINK
    if (net_room) {
        unsigned char packet[TT_WIRE_BYTES];
        if (!Port_TagRulesV2()) return 0;
        Ttw_Request(packet, TTW_CHOOSE, state.epoch); Ttw_Write(packet + 12, UINT32_MAX); Ttw_Write(packet + 20, (uint32_t)seat);
        request(packet);
        return 1;
    }
#endif
    if (!local_room) return 0;
    if (Tt_ChooseSeat(&state, member, state.epoch, seat, -1, 0)) return 0;
    error[0] = 0;
    return 1;
}
int Port_TagWithdrawChoice(void) { return Port_TagWithdrawSeat(Tt_Seat(&state, member)); }
void Port_TagChoiceItems(int chara, int costume, const uint16_t items[8]) {
#ifdef BT4_VITA_NETLINK
    if (net_room) {
        unsigned char p[512];
        int i;
        Ttw_Request(p, TTW_CHOOSE, state.epoch); Ttw_Write(p + 12, chara); Ttw_Write(p + 16, costume); Ttw_Write(p + 20, Port_TagDraftSeat());
        for (i = 0; i < 8; ++i) {
            unsigned v = Port_TagOfflineCustom() && items ? items[i] : 0;
            if (v > 350) return;
            p[24 + i * 2] = (unsigned char)v; p[25 + i * 2] = (unsigned char)(v >> 8);
        }
        request(p);
        return;
    }
#endif
    (void)items;
    Port_TagCommand(TTW_CHOOSE, chara, costume);
}
const uint16_t *Port_TagPlayerItems(int actor) { return actor >= 0 && actor < Tt_Count(&state) && state.options.version ? state.players[actor].items : NULL; }
unsigned Port_TagBattleRules(void) { return state.battle_rules; }
int Port_TagSetBattleRules(unsigned rules) {
    if (configured_quick || rules > TT_RULE_MASK || state.phase != TT_LOBBY || member != state.host) return 0;
#ifdef BT4_VITA_NETLINK
    if (net_room) { Port_TagCommand(TTW_BATTLE_RULES, (int)rules, 0); return !error[0]; }
#endif
    if (!local_room) return 0;
    return Tt_SetBattleRules(&state, member, state.epoch, rules) == TT_OK;
}
#ifdef BT4_VITA_NETLINK
/* tag_team_net.c: the shared options of a room between Vitas (the owner's, changed through the room) */
int Port_TagSharedReady(void) { return state.options.version && !shared_options_pending; }
unsigned Port_TagSharedCombat(int team, int slot, unsigned value, int save) {
    int i, n = 0;
    if (!Port_TagRulesV2() || team < 0 || team > 1 || slot < 0 || slot >= 5) return TT_COMBAT_DEFAULT;
    for (i = 0; i < state.slot_count; ++i)
        if (state.players[i].team == team && n++ == slot) {
            int index = TT_OPTION_LEGACY_WORDS + state.players[i].lobby_slot;
            if (save) {
                TtOptions next = state.options;
                next.value[index] = (int32_t)value;
                return shared_options_save(&next) ? value : (unsigned)state.options.value[index];
            }
            return (unsigned)state.options.value[index];
        }
    return TT_COMBAT_DEFAULT;
}
unsigned Port_TagActorCombat(int actor) {
    if (!Port_TagRulesV2() || actor < 0 || actor >= Tt_Count(&state)) return TT_COMBAT_DEFAULT;
    return (unsigned)state.options.value[TT_OPTION_LEGACY_WORDS + state.players[actor].lobby_slot];
}
int Port_TagSharedCombatTeam(int team, unsigned value) {
    TtOptions next = state.options;
    int i;
    if (!Port_TagRulesV2() || team < 0 || team > 1) return 0;
    for (i = 0; i < state.slot_count; ++i)
        if (state.players[i].team == team) next.value[TT_OPTION_LEGACY_WORDS + state.players[i].lobby_slot] = (int32_t)value;
    return shared_options_save(&next);
}
int Port_TagOption(const char *key, int fallback) {
    int index = Tt_OptionIndex(key);
    if (Port_TagNetwork()) return state.options.version && index >= 0 ? state.options.value[index] : 0;
    return Port_Setting(key, fallback);
}
int Port_TagSharedOption(const char *key, int fallback) { return Port_TagNetwork() && state.phase ? Port_TagOption(key, fallback) : Port_Setting(key, fallback); }
int Port_TagSharedSave(const char *key, int value) {
    extern void Port_SettingSave(const char *, int);
    int index = Tt_OptionIndex(key);
    if (Port_TagNetwork() && state.phase) { TtOptions next = state.options; if (index < 0) return 0; next.value[index] = value; return shared_options_save(&next); }
    Port_SettingSave(key, value);
    return 1;
}
int Port_TagSharedHealth(int team, int slot, int32_t values[5], int save) {
    int i, n = 0, found = -1;
    if (team < 0 || team > 1 || slot < 0 || slot >= 5 || !state.options.version) return 0;
    for (i = 0; i < state.slot_count; ++i) if (state.players[i].team == team && n++ == slot) { found = state.players[i].lobby_slot; break; }
    if (found < 0) return 0;
    if (save) { TtOptions next = state.options; memcpy(next.value + TT_OPTION_SCALARS + found * 5, values, 20); return shared_options_save(&next); }
    memcpy(values, state.options.value + TT_OPTION_SCALARS + found * 5, 20);
    return 1;
}
int Port_TagSharedHealthAll(int32_t values[2][5][5]) {
    TtOptions next = state.options;
    int index[2] = {0, 0}, i;
    for (i = 0; i < state.slot_count; ++i) {
        int team = state.players[i].team, slot = index[team]++;
        if (slot >= 5) return 0;
        memcpy(next.value + TT_OPTION_SCALARS + state.players[i].lobby_slot * 5, values[team][slot], 20);
    }
    return shared_options_save(&next);
}
int Port_TagActorHealth(int actor, int32_t values[5]) {
    if (actor < 0 || actor >= Tt_Count(&state) || !state.options.version) return 0;
    memcpy(values, state.options.value + TT_OPTION_SCALARS + state.players[actor].lobby_slot * 5, 20);
    return 1;
}
#else
int Port_TagSharedReady(void) { return state.options.version != 0; }
unsigned Port_TagSharedCombat(int team, int slot, unsigned value, int save) { (void)team; (void)slot; (void)value; (void)save; return TT_COMBAT_DEFAULT; }
unsigned Port_TagActorCombat(int actor) { (void)actor; return TT_COMBAT_DEFAULT; }
int Port_TagSharedCombatTeam(int team, unsigned value) { (void)team; (void)value; return 0; }
int Port_TagOption(const char *key, int fallback) { extern int Port_Setting(const char *, int); return Port_Setting(key, fallback); }
int Port_TagSharedOption(const char *key, int fallback) { return Port_TagOption(key, fallback); }
int Port_TagSharedSave(const char *key, int value) { extern void Port_SettingSave(const char *, int); Port_SettingSave(key, value); return 1; }
int Port_TagSharedHealth(int team, int slot, int32_t values[5], int save) { (void)team; (void)slot; (void)values; (void)save; return 0; }
int Port_TagSharedHealthAll(int32_t values[2][5][5]) { (void)values; return 0; }
int Port_TagActorHealth(int actor, int32_t values[5]) { (void)actor; (void)values; return 0; }
#endif

/* ---- what the menus and the battle read ---- */
int Port_TagPhase(void) { return state.phase; }
int Port_TagCount(void) { return Tt_Count(&state); }
int Port_TagCapacity(void) { return state.phase >= TT_SELECT && state.phase <= TT_FIGHT ? Tt_Count(&state) : Tt_Capacity(&state); }
int Port_TagSlots(void) { return Tt_Capacity(&state); }
int Port_TagTeam(int seat) { return seat >= 0 && seat < Tt_Capacity(&state) ? state.players[seat].team : seat & 1; }
int Port_TagTeamIndex(int seat) { int i, n = 0; for (i = 0; i < seat; ++i) if (Port_TagTeam(i) == Port_TagTeam(seat)) ++n; return n; }
int Port_TagTeamCount(int team) { return Tt_TeamCount(&state, team, 1); }
int Port_TagTeamSlots(int team) { return Tt_TeamCount(&state, team, 0); }
int Port_TagFreeForAll(void) { return state.free_for_all; }
int Port_TagCanStart(void) {
#ifdef BT4_VITA_NETLINK
    if (net_room)
        return !shared_options_pending && !configure_pending && state.phase == TT_LOBBY && (!configured_quick || Tt_Humans(&state) == Tt_Capacity(&state)) &&
               Tt_TeamCount(&state, 0, 1) > 0 && Tt_TeamCount(&state, 1, 1) > 0;
#endif
    return state.phase == TT_LOBBY && Tt_TeamCount(&state, 0, 1) > 0 && Tt_TeamCount(&state, 1, 1) > 0;
}
int Port_TagTime(void) { return state.time; }
int Port_TagSeat(void) { return Tt_Seat(&state, member); }
int Port_TagTurn(void) { int s = Port_TagSeat(); return s >= 0 ? Tt_Turn(&state, Port_TagTeam(s)) : -1; }
int Port_TagHumans(void) { return Tt_Humans(&state); }
int Port_TagHost(void) { return member && member == state.host; }
int Port_TagCPU(int seat) { return seat >= 0 && seat < TT_MAX_PLAYERS ? state.players[seat].cpu : 0; }
int Port_TagDifficulty(int seat) { return seat >= 0 && seat < TT_MAX_PLAYERS ? state.players[seat].difficulty : 0; }
int Port_TagPlayerChara(int seat) { return seat >= 0 && seat < TT_MAX_PLAYERS ? state.players[seat].chara : -1; }
int Port_TagPlayerCostume(int seat) { return seat >= 0 && seat < TT_MAX_PLAYERS ? state.players[seat].costume : 0; }
int Port_TagConfirmed(int seat) { return seat >= 0 && seat < TT_MAX_PLAYERS ? state.players[seat].confirmed : 0; }
const char *Port_TagPlayerName(int seat) { return seat >= 0 && seat < TT_MAX_PLAYERS ? state.players[seat].name : ""; }
int Port_TagResultChoice(void) { return state.result_choice; }
int Port_TagStage(void) { return state.stage; }
int Port_TagBgm(void) { return state.bgm; }
int Port_TagAttacks(void) { return 1; }
unsigned Port_TagSeed(void) { return state.seed; }
const char *Port_TagCode(void) { return room_code; }
const char *Port_TagError(void) { return error; }
int Port_TagAllLoaded(void) { int i; for (i = 0; i < Tt_Count(&state); ++i) if (!state.players[i].loaded) return 0; return Tt_Count(&state) > 0; }
int Port_TagAllArmed(void) { int i; for (i = 0; i < Tt_Count(&state); ++i) if (!state.players[i].armed) return 0; return Tt_Count(&state) > 0; }
int Port_TagAllEnded(void) { int i; for (i = 0; i < Tt_Count(&state); ++i) if (!state.players[i].ended) return 0; return Tt_Count(&state) > 0; }
/* the character select's 60 s turn clock */
unsigned Port_TagMenuMillis(void) { return (unsigned)(sceKernelGetProcessTimeWide() / 1000); }
void Port_TagMenuTestAdvance(unsigned ms) { (void)ms; }
void Port_TagCapture(void) {}
void Port_TagUiCapture(int index) { (void)index; }
void Port_TagJointRecordMark(int tick) { (void)tick; }
int Port_TagCopyCode(void) { return 0; }
int Port_TagJoinCode(const char *code) { (void)code; snprintf(error, sizeof(error), "Room codes need the online service."); return 0; }
int Port_TagQueueWaiting(int queue) { (void)queue; return 0; }

#ifdef BT4_VITA_NETLINK
/* ---- the network's (tag_team_net.c, peer mode) ---- */
int Port_TagNetwork(void) { return net_room; }
int Port_TagLocalBattle(void) { return local_room || solo_cpu_fight(); }
int Port_TagDraftPending(void) { return cancel_pending; }
int Port_TagCatchup(void) { return tag_catchup; }
int Port_TagPing(void) {
    int ping;
    if (local_room || solo_cpu_fight() || !vnet) return 0;
    net_lock(); ping = Vtn_Ping(vnet); net_unlock();
    return ping;
}
int Port_TagDelay(void) {
    int lead = 1, full = 1, margin = 0, total, ping = Port_TagPing();
    if (local_room || solo_cpu_fight() || !tag_paced) return state.delay;
    /* The PC's owner sends its pads for the frame due next (its game draws at 60/s). The Vita's draws at ~30/s while the
       owner's clock ticks 34.2/s, so its own pads missed the tick about one time in eight (tools/tagnet_test.c: 111 of
       900); one frame ahead they arrive in time (1 of 900). bt3.txt tagnet_lead (default 1; 0 = the PC's). */
    if (member == state.host) return tag_backlog() + Port_Setting("tagnet_lead", 1);
    if (tag_paced && ping > 0) {
        lead = (ping * 342 + 19999) / 20000;
        full = (ping * 342 + 9999) / 10000;
        if (lead < 1) lead = 1;
        margin = tag_delay_margin;
        if (margin > full - lead) margin = full - lead;
    }
    total = lead + margin + tag_backlog();
    return total > 120 ? 120 : total;
}
const char *Port_TagChatLine(int i) { return i >= 0 && i < chat_count ? chat_lines[i] : ""; }
int Port_TagChatCount(void) { return chat_count; }
void Port_TagChatSend(const char *text) {
    unsigned char p[512];
    if (!text || !text[0] || !net_room || Port_TagSeat() < 0) return;
    Ttw_Request(p, TTW_CHAT, state.epoch); snprintf((char *)p + 16, 160, "%s", text); request(p);
}
/* where the menus' Create Room opens a room: the Ad Hoc group when the Vita is in one, else UDP on the LAN / internet */
const char *Port_TagEndpoint(void) { extern int Vita_AdhocMenuOn(void); return Vita_AdhocMenuOn() ? "vita-adhoc" : "vita-udp"; }
int Port_TagDataFingerprint(unsigned long long *out) {
    extern int Port_BuildIdentity(unsigned long long *);
    const int status = Port_BuildIdentity(out);
    if (status == 1 && out) *out ^= 0x54414743414D3033ull; /* (tag_team_net.c, BT4_TAG_TEAM_EXPANDED) */
    return status;
}
void Port_TagJoin(const char *code, const char *name, uint64_t content) {
    unsigned char b[TT_WIRE_BYTES];
    configured_quick = 0;
    if (!code || strlen(code) != 6 || !net_room) return;
    Ttw_Request(b, TTW_JOIN, 0);
    memcpy(b + 12, code, 6); write64(b + 24, content); if (name) snprintf((char *)b + 32, 20, "%s", name);
    Ttw_Write(b + TTW_JOIN_BATTLE_RULES, 3);
    request(b);
}
/* PORT_VITA: the joiner's side of a room between Vitas: "IPv4:port" (UDP) or an Ad Hoc MAC (the menus' rows) */
int Port_TagJoinAddress(const char *address) {
    unsigned long long content = 0;
    char server[96];
    extern const char *Port_NetNameGet(void);
    if (!address || !address[0]) { snprintf(error, sizeof(error), "Write the room's address."); return 0; }
    if (Port_TagDataFingerprint(&content) != 1) { snprintf(error, sizeof(error), "This is an old or incompatible version. Apply the complete BT4 patch."); return 0; }
    snprintf(server, sizeof(server), "%s:%s", strchr(address, '.') ? "vita-udp" : "vita-adhoc", address);
    Port_TagCPUOptions(0, 2);
    if (!Port_TagConnect(server)) return 0;
    Port_TagJoin("VITA00", Port_NetNameGet(), content);
    return !error[0];
}
void Port_TagArena(int stage, int bgm, int attacks) {
    unsigned char p[512];
    (void)attacks;
    if (!net_room) return;
    Ttw_Request(p, TTW_ARENA, state.epoch); Ttw_Write(p + 12, stage); Ttw_Write(p + 16, bgm); Ttw_Write(p + 20, 1); request(p);
}
void Port_TagBattleRenderSet(int active) {
    extern int Port_NetOwnCamera(void);
    if (active && !render_active) own_camera = Port_NetOwnCamera();
    render_active = active != 0;
}
int Port_TagBattleRenderActive(void) { return render_active; }
void Port_TagHashActor(int actor, unsigned x, unsigned y, unsigned z, unsigned hp, unsigned ki, unsigned action, unsigned target, unsigned team, unsigned chara, unsigned costume) {
    unsigned words[TT_SYNC_WORDS] = {x, y, z, hp, ki, action, target, team, chara, costume};
    if (actor == 0) tag_sync_count = 0;
    if (actor < 0 || actor >= TT_MAX_PLAYERS) return;
    memcpy(tag_sync_words[actor], words, sizeof(words));
    tag_sync_count = actor + 1;
}
void Port_TagHash(unsigned frame, unsigned hash) {
    unsigned char p[512];
    if (!net_room || state.phase != TT_FIGHT) return;
    Ttw_Request(p, TTW_HASH, state.epoch); Ttw_Write(p + 12, frame); Ttw_Write(p + 16, hash);
    if (tag_sync_count == Tt_Count(&state)) {
        int i, k;
        Ttw_Write(p + 20, tag_sync_count); Ttw_Write(p + 24, TT_SYNC_MAGIC);
        for (i = 0; i < tag_sync_count; ++i) for (k = 0; k < TT_SYNC_WORDS; ++k) Ttw_Write(p + TT_SYNC_OFFSET + (i * TT_SYNC_WORDS + k) * 4, tag_sync_words[i][k]);
    }
    tag_sync_last_frame = frame;
    request(p);
}
int Port_TagSendInput(uint32_t frame, const unsigned char pad[TT_PAD_BYTES]) {
    unsigned char withCamera[TT_PAD_BYTES], packet[TT_WIRE_BYTES];
    if (!pad) return 0;
    memcpy(withCamera, pad, TT_PAD_BYTES); withCamera[10] = (unsigned char)(0x80 | own_camera); pad = withCamera;
    if (state.phase != TT_FIGHT || !net_room) return 0;
    if (solo_cpu_fight()) {
        unsigned f;
        int i, seat = Tt_Seat(&state, member);
        const unsigned char neutral[TT_PAD_BYTES] = {0, 0, 0, 0, 127, 127};
        if (received.epoch != state.epoch) Tt_InputsReset(&received, state.epoch, Tt_Count(&state));
        if (frame == (unsigned)state.delay && received.next == 0)
            for (f = 0; f < (unsigned)state.delay; ++f) for (i = 0; i < Tt_Count(&state); ++i)
                if (Tt_InputPut(&received, state.epoch, i, f, neutral) != TT_OK) { fail("Invalid CPU practice startup frame."); return 0; }
        for (i = 0; i < Tt_Count(&state); ++i)
            if (Tt_InputPut(&received, state.epoch, i, frame, i == seat ? pad : neutral) != TT_OK) { fail("Invalid CPU practice input frame."); return 0; }
        return 1;
    }
    {
        uint32_t next;
        if (tag_input_epoch != state.epoch) {
            tag_input_sent = 0; tag_input_epoch = state.epoch;
            tag_pending_coop = 0; tag_pending_buttons = tag_pending_l3 = tag_pending_skip = 0; tag_delay_adjusted = 0;
            tag_delay_margin = 1; tag_delay_clean_frames = 0; tag_delay_feedback_seen = 0;
        }
        if (tag_input_sent && frame <= tag_input_latest) {
            tag_pending_buttons |= Ttw_Read(pad); tag_pending_l3 |= pad[6] & 15; tag_pending_skip |= pad[8]; tag_pending_coop |= pad[17] & 13; return 1;
        }
        next = tag_input_sent ? tag_input_latest + 1 : frame;
        if (!tag_input_sent) tag_input_first = frame;
        for (; next <= frame; ++next) {
            Ttw_Request(packet, TTW_INPUT, state.epoch); Ttw_Write(packet + 12, next);
            if (next < frame) { memcpy(packet + 16, tag_input_pad, TT_PAD_BYTES); packet[22] &= 0x30; packet[24] = 0; }
            else {
                memcpy(packet + 16, pad, TT_PAD_BYTES); Ttw_Write(packet + 16, Ttw_Read(pad) | tag_pending_buttons);
                packet[22] |= tag_pending_l3; packet[24] |= tag_pending_skip; packet[33] |= (unsigned char)tag_pending_coop;
            }
            request(packet);
            if (state.phase != TT_FIGHT) return 0;
        }
        memcpy(tag_input_pad, pad, TT_PAD_BYTES);
        tag_pending_coop = 0; tag_pending_buttons = tag_pending_l3 = tag_pending_skip = 0;
        tag_input_latest = frame; tag_input_sent = 1;
        return 1;
    }
}
int Port_TagTakeFrame(unsigned char out[TT_MAX_PLAYERS][TT_PAD_BYTES]) {
    int ok = state.phase == TT_FIGHT && received.epoch == state.epoch && Tt_InputTake(&received, out) == TT_OK;
    tag_update_catchup();
    return ok;
}
void Port_TagPoll(void) { if (net_room) net_poll(); }
void Port_TagYield(void) { sceKernelDelayThread(2000); }
static int tag_wait_banner;
static uint64_t tag_wait_started;
void Port_TagWaitResumed(void) {
    extern void Gs_NetworkWait(int);
    Gs_NetworkWait(0);
    if (tag_wait_started) {
        extern void Port_PaceNetworkResume(unsigned long long);
        uint64_t elapsed = sceKernelGetProcessTimeWide() * 1000 - tag_wait_started;
        tag_wait_started = 0;
        Port_PaceNetworkResume(elapsed);
    }
}
void Port_TagWaitClear(void) { extern void Port_UiBanner(const char *, int); if (tag_wait_banner) { Port_UiBanner(NULL, 0); tag_wait_banner = 0; } }
void Port_TagWait(int phase, int menu, int cursor, int show) {
    extern void Gs_NetworkWait(int), Port_UiBanner(const char *, int), Port_TagMenuSet(int, int);
    const char *text = NULL;
    if (!tag_wait_started) tag_wait_started = sceKernelGetProcessTimeWide() * 1000;
    Port_TagMenuSet(menu, cursor);
    if (!menu && show) text = phase < 5 ? "TAG TEAM ONLINE\nConnecting players..." : "TAG TEAM ONLINE\nWaiting for players...";
    if (text) { Port_UiBanner(text, 0); tag_wait_banner = 1; } else Port_TagWaitClear();
    Gs_NetworkWait(1);
    Port_TagYield();
}
#else
/* ---- the network's, answered as none ---- */
int Port_TagLocalBattle(void) { return local_room; }
int Port_TagDraftPending(void) { return 0; }
int Port_TagDelay(void) { return state.delay; }
const char *Port_TagChatLine(int i) { (void)i; return ""; }
const char *Port_TagEndpoint(void) { return ""; }
int Port_TagChatCount(void) { return 0; }
int Port_TagDataFingerprint(unsigned long long *out) { (void)out; return 0; }
int Port_TagJoinAddress(const char *address) { (void)address; return 0; }
int Port_TagSendInput(uint32_t frame, const unsigned char pad[TT_PAD_BYTES]) { (void)frame; (void)pad; return 0; }
int Port_TagTakeFrame(unsigned char out[TT_MAX_PLAYERS][TT_PAD_BYTES]) { (void)out; return 0; }
int Port_TagCatchup(void) { return 0; }
int Port_TagPing(void) { return -1; }
void Port_TagArena(int stage, int bgm, int attacks) { (void)stage; (void)bgm; (void)attacks; }
void Port_TagBattleRenderSet(int active) { (void)active; }
void Port_TagChatSend(const char *text) { (void)text; }
void Port_TagHash(unsigned frame, unsigned hash) { (void)frame; (void)hash; }
void Port_TagJoin(const char *code, const char *name, uint64_t content) { (void)code; (void)name; (void)content; }
void Port_TagPoll(void) {}
void Port_TagWait(int phase, int menu, int cursor, int show) { (void)phase; (void)menu; (void)cursor; (void)show; }
void Port_TagWaitClear(void) {}
void Port_TagWaitResumed(void) {}
void Port_TagYield(void) {}
#endif
