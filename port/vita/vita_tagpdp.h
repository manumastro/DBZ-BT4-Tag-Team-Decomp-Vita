#ifndef VITA_TAGPDP_H
#define VITA_TAGPDP_H

/*
 * PS Vita: TagPdp, the transport under the online Tag Team (psvita-agent notes/online/vita-multiplayer-plan.md,
 * section 7.5). The PC's Tag Team talks over WebRTC data channels: a reliable ordered "control" channel (commands,
 * states, chat, replay requests) and an unordered unreliable "input" channel (pads, frames). The Vita has neither, so
 * this gives the same two channels over a datagram service (Ad Hoc PDP first, internet UDP later).
 *
 * Nothing here calls a Vita API. The caller gives three callbacks (send a datagram to a member, receive one, the
 * clock), so the same code runs in the host test (port/vita/tools/tagpdp_test.c) and on the console.
 *
 * Members are numbered 0 to TAGPDP_MAX_PEERS-1; the callbacks take a member id and the caller maps it to an address.
 * (The PC numbers its members from 1: the caller subtracts one where it matters.)
 *
 * Packet: a 20-byte header and at most 528 bytes of payload, all integers little-endian:
 *   0   u16  magic 0x5450
 *   2   u32  session id (a packet of another session is dropped)
 *   6   u8   sender member
 *   7   u8   channel: 0 control (reliable, ordered), 1 unreliable, 2 ack only (no payload)
 *   8   u16  seq (control: this packet's number; unreliable and ack packets: 0)
 *   10  u16  ack: the highest control seq the sender has received from the receiver
 *   12  u32  ack bits: bit i set when control seq (ack - 1 - i) was received
 *   16  u16  cum: the last control seq the sender has received in order (all before it arrived)
 *   18  u16  payload length
 *   20       payload
 * Every packet carries the ack fields for its peer, so acks ride on any traffic. A bare ack packet goes out only when
 * nothing else went to that peer within 20 ms of a control packet from it.
 *
 * Control channel, per peer: a send window of TAGPDP_WINDOW (32) messages (TagPdp_Send returns -1 when it is full); a message is resent
 * every 150 ms until acknowledged; duplicates are dropped; a reorder buffer of the same size; delivered in order.
 * Unreliable channel: delivered as received, never resent; one inbox shared by all members (TAGPDP_INBOX, 32: TagPdp_Poll stops
 * receiving while it is full, so the rest waits in the socket; a message that still finds it full is dropped and counted).
 *
 * Memory: 154,808 bytes per TagPdp with the defaults (4 members; 5 vs 5 rooms would need TAGPDP_MAX_PEERS 10) (the windows are fixed arrays). No malloc. Keep it static or in a pool.
 * Single-threaded: call TagPdp_Poll once a frame, then TagPdp_Read until it returns -1.
 */

#include <stdint.h>

#define TAGPDP_MAX_PEERS 4
#define TAGPDP_MAX_PAYLOAD 528
#define TAGPDP_HEADER 20
#define TAGPDP_MAX_PACKET (TAGPDP_HEADER + TAGPDP_MAX_PAYLOAD)
#define TAGPDP_RECV_SIZE 1444     /* the buffer the transport receives into (a whole packet is < 1444) */
#ifndef TAGPDP_WINDOW              /* control messages outstanding per peer, and the reorder buffer's size (a power of two) */
#define TAGPDP_WINDOW 32
#endif
#ifndef TAGPDP_INBOX               /* delivered messages waiting for TagPdp_Read (read every frame: a frame's worth is enough) */
#define TAGPDP_INBOX 32
#endif
#define TAGPDP_RESEND_MS 150
#define TAGPDP_ACK_MS 20

#define TAGPDP_CONTROL 0
#define TAGPDP_UNRELIABLE 1

typedef struct TagPdpOps {
    /* sends one datagram to a member; returns the bytes sent or < 0 on error */
    int (*send)(void *ctx, int peer, const void *data, int len);
    /* receives one datagram into buf (at most size bytes); sets *peer to where it came from; returns the bytes,
       0 when nothing is waiting, < 0 on error */
    int (*recv)(void *ctx, int *peer, void *buf, int size);
    uint64_t (*now_ms)(void *ctx);
} TagPdpOps;

typedef struct TagPdpStats {
    uint32_t packets_out;       /* packets handed to ops->send */
    uint32_t packets_in;        /* packets accepted (magic, session and sender checked) */
    uint32_t resends;           /* control messages sent again */
    uint32_t acks_sent;         /* bare ack packets */
    uint32_t dup_drops;         /* control packets already received */
    uint32_t window_drops;      /* control packets beyond the 64-message window */
    uint32_t inbox_drops;       /* messages dropped because the inbox was full */
    uint32_t session_drops;     /* packets of another session */
    uint32_t bad_drops;         /* malformed packets, or a sender that is not a member */
    uint32_t send_errors;       /* ops->send returned < 0 */
    uint32_t rtt_ms[TAGPDP_MAX_PEERS];  /* smoothed round trip per peer (first messages only); 0 until a sample */
} TagPdpStats;

/* the rest of this header is the transport's state: read it through the functions below */

typedef struct TagPdpSlot {
    uint64_t sent_ms;
    uint16_t len;
    uint8_t acked;
    uint8_t sends;
    uint8_t data[TAGPDP_MAX_PAYLOAD];
} TagPdpSlot;

typedef struct TagPdpPeer {
    uint16_t snd_una;           /* oldest control message not yet acknowledged */
    uint16_t snd_next;          /* seq of the next control message */
    TagPdpSlot snd[TAGPDP_WINDOW];
    uint16_t rx_next;           /* next control seq to deliver in order (all before it received) */
    uint16_t rx_hi;             /* highest control seq received */
    uint8_t rx_have[TAGPDP_WINDOW];
    uint16_t rx_len[TAGPDP_WINDOW];
    uint8_t rx_data[TAGPDP_WINDOW][TAGPDP_MAX_PAYLOAD];
    uint8_t ack_pending;
    uint64_t ack_due_ms;
} TagPdpPeer;

typedef struct TagPdpMsg {
    uint8_t peer;
    uint8_t channel;
    uint16_t len;
    uint8_t data[TAGPDP_MAX_PAYLOAD];
} TagPdpMsg;

typedef struct TagPdp {
    const TagPdpOps *ops;
    void *ctx;
    uint32_t session;
    int self;
    int inbox_head;
    int inbox_count;
    TagPdpPeer peer[TAGPDP_MAX_PEERS];
    TagPdpMsg inbox[TAGPDP_INBOX];
    TagPdpStats stats;
} TagPdp;

/* 0 on success; -1 on bad arguments (self out of range, missing callbacks) */
int TagPdp_Init(TagPdp *p, const TagPdpOps *ops, void *ctx, uint32_t session, int self);

/* channel TAGPDP_CONTROL or TAGPDP_UNRELIABLE; 0 when queued (control) or sent (unreliable), -1 when the window is full,
   the arguments are bad, or the send failed (unreliable only) */
int TagPdp_Send(TagPdp *p, int peer, int channel, const void *data, int len);

/* receives every waiting datagram, resends overdue control messages, sends due bare acks; returns the datagrams
   received */
int TagPdp_Poll(TagPdp *p);

/* the next delivered message: its length (0 or more) with *peer, *channel set and the payload copied to buf;
   -1 when there is none; -2 when buf is smaller than the message (the message stays) */
int TagPdp_Read(TagPdp *p, int *peer, int *channel, void *buf, int size);

/* forgets everything about one member (its windows, sequence numbers, its waiting messages): for a slot given to a
   new member, whose numbering starts again */
void TagPdp_ResetPeer(TagPdp *p, int peer);

/* control messages to a peer not yet acknowledged */
int TagPdp_QueueDepth(const TagPdp *p, int peer);

const TagPdpStats *TagPdp_Stats(const TagPdp *p);

#endif
