#ifndef VITA_TAG_NET_H
#define VITA_TAG_NET_H

/*
 * PS Vita: the online Tag Team room between Vitas (milestone T1; psvita-agent notes/online/vita-multiplayer-plan.md
 * section 7.7). The Vita's counterpart of BT4decomp's port/src/gs/tag_team_p2p.cpp (the PC's "TagPeer"): one player's
 * game owns the room and runs the room's authority (shared/tag_kernel.c, the Kernel_* calls); the others send it
 * commands and pads and get back states and frames. Same rules as the PC's peer mode, same 512-byte packets (the Tag
 * wire, port/include/tag_team_wire.h), same behaviour (ticks at 34.2 a second, the last three frames sent each tick,
 * replay of missing frames, pings every half second, the snapshots after each command). What differs:
 *   - the transport: TagPdp (vita_tagpdp.h), a reliable "control" channel and an unreliable one over datagrams,
 *     in place of WebRTC's two data channels;
 *   - admission: no signaling service. A joiner sends a HELLO datagram to the owner's address (Ad Hoc: MAC + port;
 *     LAN / internet: IPv4 + port) with its name, its game data's fingerprint and its options version; the owner
 *     compares the fingerprint itself (the PC leaves that to the service), gives it a member number and a TagPdp slot
 *     (WELCOME) and builds the TTW_JOIN the PC's admit() builds; or answers REJECT (full, started, different data);
 *   - leaving: no transport "closed" event, so a member silent for VTN_SILENT_MS counts as gone (BYE datagrams make
 *     it immediate when a side closes the room on purpose);
 *   - no thread of its own here: Vtn_Step does one round of the PC's run() loop. The Vita's glue (vita_tag.c) runs it
 *     on a thread every 2 ms, as the PC's thread; the tests call it directly on a simulated clock.
 *
 * Nothing here calls a Vita API: the link (send / receive one datagram, the clock) is given by the caller, so the same
 * code runs in the host test (port/vita/tools/tagnet_test.c) and on the console.
 *
 * Limits: TAGPDP_MAX_PEERS (4) Vitas in a room: the owner and three others (a room of any size, the other seats CPU).
 * One owner per process (the authority is a single static kernel).
 */

#include <stdint.h>
#include "vita_tagpdp.h"

#define VTN_PACKET 512          /* the Tag wire's packet */
#define VTN_HISTORY 256         /* frames the owner keeps for replay (the PC keeps 512; 256 is 7.5 s at 34.2/s) */
#define VTN_REORDER 128         /* frames a client holds while an earlier one is missing */
#define VTN_QUEUE 256           /* packets waiting for the game (the PC's queue: 1024) */
#define VTN_BACKLOG 64          /* control packets waiting for room in a TagPdp window */
#define VTN_SILENT_MS 10000     /* a member not heard from for this long has left */
#define VTN_HELLO_MS 250        /* a joiner's HELLO is sent again until an answer */
#define VTN_JOIN_MS 15000       /* a joiner gives up after this long without an answer */
#define VTN_RATE_DEFAULT 34200  /* the owner's ticks per 1000 s: the PC's 34.2 a second (Tag Team runs at 114 %) */

typedef struct VtnAddr {        /* a member's address on the link: IPv4 + port, or an Ad Hoc MAC + port */
    uint8_t b[8];
} VtnAddr;

typedef struct VtnLink {
    /* sends one datagram; returns the bytes sent or < 0 */
    int (*send)(void *ctx, const VtnAddr *to, const void *data, int len);
    /* receives one datagram; returns its bytes (and where it came from), 0 when nothing waits, < 0 on error */
    int (*recv)(void *ctx, VtnAddr *from, void *buf, int size);
    uint64_t (*now_us)(void *ctx);
} VtnLink;

typedef struct VtnPeer {        /* (the owner's view of a member; a client keeps its owner in slot 0) */
    int used, admitted;
    VtnAddr addr;
    uint32_t id;                /* member number in the room (the owner is 1) */
    uint32_t nonce;             /* the joiner's HELLO nonce */
    uint64_t heard_ms;
    unsigned probe;
    uint64_t probe_ms, ping_ms;
    int measured;
    int capable;                /* the joiner's HELLO: it knows battle rules (the PC's capability exchange) */
    unsigned options;           /* and which shared options version (TT_OPTIONS_VERSION / TT_OPTION_COMBAT_VERSION) */
    char name[20];
} VtnPeer;

typedef struct VtnFrame {
    uint32_t frame;
    uint8_t used;
    uint64_t at_ms;
    uint8_t bytes[VTN_PACKET];
} VtnFrame;

typedef struct VtnStats {
    uint32_t requests, commands, snapshots, frames_ticked, frames_sent, replays_asked, replays_sent;
    uint32_t frames_in, frames_dup, frames_far, hello_in, welcome, rejects, leaves;
    uint32_t backlog_max, queue_max, send_fail, late_mask_frames;
} VtnStats;

typedef struct Vtn {
    const VtnLink *link;
    void *ctx;
    int status;                 /* 0 starting, 1 in the room, -1 failed (error says why) */
    const char *error;
    int owner;                  /* 1: this game owns the room (member 1) */
    uint32_t self;              /* member number (0 until admitted) */
    int slot;                   /* TagPdp slot (owner 0) */
    uint32_t session;
    int joined;                 /* TagPdp running (owner at once, client after WELCOME) */
    TagPdp pdp;
    TagPdpOps pdp_ops;
    VtnPeer peer[TAGPDP_MAX_PEERS];
    uint32_t next_member;
    /* the room (the PC's Session) */
    uint64_t content;           /* the game data's fingerprint (owner: its own; client: what it sent) */
    char name[20];
    char code[7];
    int phase;
    uint32_t epoch, expected, latest;
    int reorder_count, hello_started;
    uint32_t nonce;
    int clock;
    uint64_t next_tick_us, rate;
    uint64_t next_ping_ms, last_replay_ms, began_ms, hello_ms;
    unsigned probe_serial;
    unsigned ping_samples[8], ping_count, ping_cursor;
    int ping;
    /* the owner's frames: all kept for replay, the last three sent again each tick */
    VtnFrame history[VTN_HISTORY];
    int history_count;
    /* a client's frames waiting for an earlier one */
    VtnFrame reorder[VTN_REORDER];
    /* what the game reads (TagPeer_Read) */
    struct { uint64_t at_ms; uint8_t bytes[VTN_PACKET]; } queue[VTN_QUEUE];
    int queue_head, queue_count;
    /* control packets waiting for room in a member's TagPdp window */
    struct { int slot; uint8_t bytes[VTN_PACKET]; } backlog[VTN_BACKLOG];
    int backlog_count;
    /* handshake datagrams taken while TagPdp polls, answered after */
    struct { VtnAddr from; int len; uint8_t bytes[64]; } hs[8];
    int hs_count;
    VtnStats stats;
} Vtn;

/* sets the session up: owner NULL hosts a room here, else joins the room at that address. Nothing is sent until the
   game's first request (TTW_OPEN or TTW_JOIN, as the PC's TagPeer). 1 on success. */
int Vtn_Open(Vtn *s, const VtnLink *link, void *ctx, const VtnAddr *owner);
/* the game's request (one 512-byte Tag packet): 1 taken, 0 refused (the session failed or its queues are full) */
int Vtn_Send(Vtn *s, const uint8_t packet[VTN_PACKET]);
/* the next packet for the game: 1 (with the time it arrived, ms), 0 none, -1 the session failed */
int Vtn_Read(Vtn *s, uint8_t packet[VTN_PACKET], uint64_t *at_ms);
/* one round: datagrams in, admissions, the owner's ticks, pings, replay requests, resends */
void Vtn_Step(Vtn *s);
/* says goodbye to the others (BYE datagrams); the session is over */
void Vtn_Close(Vtn *s);
/* the PC's TagPeer_Ping: a client's best recent round trip to the owner; the owner's worst current one; -1 none */
int Vtn_Ping(const Vtn *s);
/* the owner's ticks per 1000 s (VTN_RATE_DEFAULT when 0) */
void Vtn_SetRate(Vtn *s, unsigned per_1000s);
/* a one-line summary for the log */
void Vtn_Describe(const Vtn *s, char *out, int size);

#endif
