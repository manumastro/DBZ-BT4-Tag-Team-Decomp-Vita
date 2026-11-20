/*
 * PS Vita: the online Tag Team room between Vitas (T1). What it is and how it differs from the PC: vita_tag_net.h.
 *
 * The functions below follow BT4decomp's port/src/gs/tag_team_p2p.cpp (R1.6) one for one where the PC has them; the
 * PC's names are kept in the comments (snapshot, notify, admit, command, flushFrames, clearRound, the tick and replay of
 * run()), so a later upstream change can be carried over line by line.
 */
#include <stdio.h>
#include <string.h>
#include "vita_tag_net.h"
#include "tag_team_wire.h"

/* the room's authority (shared/tag_kernel.c, built by vita_tag_authority.c), as p2p.cpp declares it */
unsigned char *Kernel_Input(void), *Kernel_Output(void);
void Kernel_Reset(void), Kernel_Pacing(int), Kernel_Seed(unsigned), Kernel_Snapshot(unsigned, int, const char *),
     Kernel_Leave(unsigned);
int Kernel_Command(unsigned), Kernel_TakeFrame(void), Kernel_Phase(void), Kernel_Seat(unsigned), Kernel_RoundEnded(void);
unsigned Kernel_BattleRules(void), Kernel_OptionsHash(void), Kernel_OptionsVersion(void);
void Kernel_OptionsSnapshot(void);
unsigned Kernel_DeadlineFrame(void), Kernel_Epoch(void), Kernel_PendingFrame(void);

#define PING_COMMAND 107u     /* (p2p.cpp: pingCommand, pongCommand, replayCommand) */
#define PONG_COMMAND 108u
#define REPLAY_COMMAND 110u

/* the handshake's datagrams (not TagPdp packets: their own magic), little-endian:
   0 u16 magic, 2 u8 kind, 3 u8 version, 4 u32 nonce, then
   HELLO   8 u64 content, 16 u32 options version, 20 u32 capable, 24 name[20]      (44 bytes)
   WELCOME 8 u32 session, 12 u32 member, 16 u8 slot, 17 code[6]                    (24 bytes)
   REJECT  8 u32 reason (a TT_* result)                                            (12 bytes)
   BYE     8 u32 session, 12 u32 member                                            (16 bytes) */
#define HS_MAGIC 0x4854u
#define HS_VERSION 1
enum { HS_HELLO = 1, HS_WELCOME, HS_REJECT, HS_BYE };

static void put16(uint8_t *b, unsigned v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *b, uint32_t v) { put16(b, v & 0xffff); put16(b + 2, v >> 16); }
static void put64(uint8_t *b, uint64_t v) { put32(b, (uint32_t)v); put32(b + 4, (uint32_t)(v >> 32)); }
static unsigned get16(const uint8_t *b) { return b[0] | (b[1] << 8); }
static uint32_t get32(const uint8_t *b) { return get16(b) | ((uint32_t)get16(b + 2) << 16); }
static uint64_t get64(const uint8_t *b) { return get32(b) | ((uint64_t)get32(b + 4) << 32); }
static uint64_t read64(const uint8_t *p) { return Ttw_Read(p) | ((uint64_t)Ttw_Read(p + 4) << 32); }
static void write64(uint8_t *p, uint64_t v) { Ttw_Write(p, (uint32_t)v); Ttw_Write(p + 4, (uint32_t)(v >> 32)); }

static uint64_t now_us(Vtn *s) { return s->link->now_us(s->ctx); }
static uint64_t now_ms(Vtn *s) { return now_us(s) / 1000; }
static int same_addr(const VtnAddr *a, const VtnAddr *b) { return !memcmp(a->b, b->b, sizeof a->b); }

static void fail(Vtn *s, const char *why) {
    if (s->status >= 0) {
        s->status = -1;
        s->error = why;
    }
}

/* p2p.cpp admissionError, for the owner's answers */
static const char *reject_text(int reason) {
    if (reason == TT_FULL) return "This Tag Team room is full.";
    if (reason == TT_BAD_PHASE) return "This Tag Team room has already started. Ask the host to return to the lobby.";
    if (reason == TT_MISMATCH) return "Both players need the same game version and game data to join this room.";
    return "The Tag Team room did not accept this player.";
}

/* ---- the game's queue (p2p.cpp emit) ---- */
static void emit(Vtn *s, const uint8_t *p, uint64_t at) {
    int i;
    if (s->queue_count >= VTN_QUEUE) {
        fail(s, "The Tag Team room's messages piled up: the game stopped reading them.");
        return;
    }
    i = (s->queue_head + s->queue_count) % VTN_QUEUE;
    memcpy(s->queue[i].bytes, p, VTN_PACKET);
    s->queue[i].at_ms = at ? at : now_ms(s);
    if (++s->queue_count > (int)s->stats.queue_max) s->stats.queue_max = (uint32_t)s->queue_count;
}

/* ---- out through TagPdp (p2p.cpp send): reliable = the control channel, kept in order through the backlog ---- */
static int backlog_has(const Vtn *s, int slot) {
    int i;
    for (i = 0; i < s->backlog_count; ++i)
        if (s->backlog[i].slot == slot) return 1;
    return 0;
}
static int pdp_out(Vtn *s, int slot, const uint8_t *p, int reliable) {
    if (!s->joined || slot < 0 || slot >= TAGPDP_MAX_PEERS || slot == s->slot || !s->peer[slot].used) return 0;
    if (!reliable) {
        if (TagPdp_Send(&s->pdp, slot, TAGPDP_UNRELIABLE, p, VTN_PACKET) == 0) return 1;
        s->stats.send_fail++;
        return 0;
    }
    if (!backlog_has(s, slot) && TagPdp_Send(&s->pdp, slot, TAGPDP_CONTROL, p, VTN_PACKET) == 0) return 1;
    if (s->backlog_count >= VTN_BACKLOG) {
        s->stats.send_fail++;
        return 0;
    }
    s->backlog[s->backlog_count].slot = slot;
    memcpy(s->backlog[s->backlog_count].bytes, p, VTN_PACKET);
    if (++s->backlog_count > (int)s->stats.backlog_max) s->stats.backlog_max = (uint32_t)s->backlog_count;
    return 1;
}
static void backlog_flush(Vtn *s) {
    unsigned blocked = 0;
    int i, kept = 0;
    for (i = 0; i < s->backlog_count; ++i) {
        int slot = s->backlog[i].slot;
        if (!(blocked & (1u << slot)) && TagPdp_Send(&s->pdp, slot, TAGPDP_CONTROL, s->backlog[i].bytes, VTN_PACKET) == 0)
            continue;
        blocked |= 1u << slot;
        if (kept != i) s->backlog[kept] = s->backlog[i];
        ++kept;
    }
    s->backlog_count = kept;
}
static void backlog_drop(Vtn *s, int slot) {
    int i, kept = 0;
    for (i = 0; i < s->backlog_count; ++i) {
        if (s->backlog[i].slot == slot) continue;
        if (kept != i) s->backlog[kept] = s->backlog[i];
        ++kept;
    }
    s->backlog_count = kept;
}

/* ---- TagPdp's link: slots to addresses; handshake datagrams set aside for after the poll ---- */
static int find_addr(const Vtn *s, const VtnAddr *a) {
    int i;
    for (i = 0; i < TAGPDP_MAX_PEERS; ++i)
        if (i != s->slot && s->peer[i].used && same_addr(&s->peer[i].addr, a)) return i;
    return -1;
}
static int find_member(const Vtn *s, uint32_t id) {
    int i;
    for (i = 0; i < TAGPDP_MAX_PEERS; ++i)
        if (i != s->slot && s->peer[i].used && s->peer[i].id == id) return i;
    return -1;
}
static void hs_stash(Vtn *s, const VtnAddr *from, const uint8_t *b, int n) {
    if (n > 64 || s->hs_count >= 8) return;
    s->hs[s->hs_count].from = *from;
    s->hs[s->hs_count].len = n;
    memcpy(s->hs[s->hs_count].bytes, b, (size_t)n);
    s->hs_count++;
}
static int op_send(void *ctx, int slot, const void *data, int len) {
    Vtn *s = (Vtn *)ctx;
    if (slot < 0 || slot >= TAGPDP_MAX_PEERS || !s->peer[slot].used) return -1;
    return s->link->send(s->ctx, &s->peer[slot].addr, data, len);
}
static int op_recv(void *ctx, int *from, void *buf, int size) {
    Vtn *s = (Vtn *)ctx;
    for (;;) {
        VtnAddr a;
        const uint8_t *b = (const uint8_t *)buf;
        int slot, n = s->link->recv(s->ctx, &a, buf, size);
        if (n <= 0) return n;
        if (n >= 8 && get16(b) == HS_MAGIC) {
            hs_stash(s, &a, b, n);
            continue;
        }
        if (n < TAGPDP_HEADER) continue;
        slot = find_addr(s, &a);
        if (slot < 0 || b[6] != slot) continue; /* (a stranger, or a member speaking for another) */
        s->peer[slot].heard_ms = now_ms(s);
        *from = slot;
        return n;
    }
}
static uint64_t op_now(void *ctx) { return now_ms((Vtn *)ctx); }

static void hs_send(Vtn *s, const VtnAddr *to, int kind, uint32_t nonce, const uint8_t *body, int body_len) {
    uint8_t b[64];
    memset(b, 0, sizeof b);
    put16(b, HS_MAGIC);
    b[2] = (uint8_t)kind;
    b[3] = HS_VERSION;
    put32(b + 4, nonce);
    if (body_len > 0) memcpy(b + 8, body, (size_t)body_len);
    s->link->send(s->ctx, to, b, 8 + body_len);
}

/* ---- the owner: snapshots (p2p.cpp snapshot, notify) ---- */
static void deliver(Vtn *s, uint32_t target, const uint8_t *p) {
    if (target == s->self) emit(s, p, 0);
    else {
        int slot = find_member(s, target);
        if (slot >= 0 && !pdp_out(s, slot, p, 1)) s->stats.send_fail++;
    }
}
static void snapshot(Vtn *s, uint32_t target, int result, uint32_t ack) {
    uint8_t p[VTN_PACKET];
    if (Kernel_OptionsHash()) {
        Kernel_OptionsSnapshot();
        memcpy(p, Kernel_Output(), VTN_PACKET);
        deliver(s, target, p);
    }
    Kernel_Snapshot(target, result, s->code);
    memcpy(p, Kernel_Output(), VTN_PACKET);
    Ttw_Write(p + 508, ack);
    deliver(s, target, p);
    s->stats.snapshots++;
}
static void notify(Vtn *s) {
    int i;
    snapshot(s, s->self, TT_OK, 0);
    for (i = 0; i < TAGPDP_MAX_PEERS; ++i)
        if (i != s->slot && s->peer[i].used && s->peer[i].admitted) snapshot(s, s->peer[i].id, TT_OK, 0);
}

/* p2p.cpp clearRound */
static void clear_round(Vtn *s, uint32_t next) {
    int i;
    s->epoch = next;
    s->clock = 0;
    s->expected = 0;
    s->latest = 0;
    s->history_count = 0;
    for (i = 0; i < VTN_HISTORY; ++i) s->history[i].used = 0;
    for (i = 0; i < VTN_REORDER; ++i) s->reorder[i].used = 0;
    s->reorder_count = 0;
    s->last_replay_ms = 0;
}

/* a member is gone (the PC: its transport closed, event kind 4) */
static void leave(Vtn *s, int slot) {
    uint32_t id = s->peer[slot].id;
    int admitted = s->peer[slot].admitted;
    memset(&s->peer[slot], 0, sizeof s->peer[slot]);
    if (s->joined) TagPdp_ResetPeer(&s->pdp, slot);
    backlog_drop(s, slot);
    s->stats.leaves++;
    if (admitted) {
        Kernel_Leave(id);
        notify(s);
    }
}

/* p2p.cpp command: a request of a member (the owner's own included), applied to the authority */
static void command(Vtn *s, uint32_t sender, const uint8_t *packet) {
    unsigned cmd = Ttw_Read(packet + 4), at = Ttw_Read(packet + 8);
    int result, i;
    s->stats.commands++;
    if (cmd != TTW_PING && at != Kernel_Epoch() && (int32_t)(at - Kernel_Epoch()) < 0) {
        if (cmd != TTW_INPUT) snapshot(s, sender, TT_OK, 0);
        return;
    }
    if (cmd == TTW_INPUT) {
        if (at != Kernel_Epoch() || Kernel_Phase() != TT_FIGHT) return;
        memcpy(Kernel_Input(), packet, VTN_PACKET);
        Kernel_Command(sender);
        if (sender == s->self && !s->clock) {
            s->clock = 1;
            s->next_tick_us = now_us(s);
        }
        return;
    }
    if (cmd == TTW_OPEN || cmd == TTW_JOIN || cmd == TTW_STATE || cmd == TTW_FRAME) return;
    if (cmd == TTW_BATTLE_RULES || cmd == TTW_OPTIONS) {
        const unsigned options = cmd == TTW_OPTIONS ? Ttw_Read(packet + 24) : 0;
        for (i = 0; i < TAGPDP_MAX_PEERS; ++i) {
            const VtnPeer *p = &s->peer[i];
            if (i != s->slot && p->used && p->admitted && (!p->capable || (options && p->options < options))) {
                snapshot(s, sender, TT_MISMATCH, 0);
                return;
            }
        }
    }
    if (cmd == TTW_CHAT) {
        uint8_t chat[VTN_PACKET];
        if (Kernel_Seat(sender) < 0) return;
        memcpy(chat, packet, VTN_PACKET);
        Ttw_Write(chat + 12, sender);
        chat[175] = 0;
        emit(s, chat, 0);
        for (i = 0; i < TAGPDP_MAX_PEERS; ++i)
            if (i != s->slot && s->peer[i].used && s->peer[i].admitted) pdp_out(s, i, chat, 1);
        return;
    }
    memcpy(Kernel_Input(), packet, VTN_PACKET);
    result = Kernel_Command(sender);
    if (Kernel_Epoch() != s->epoch) clear_round(s, Kernel_Epoch());
    if (Kernel_RoundEnded()) s->clock = 0;
    if (result == TT_OK && cmd != TTW_PING && cmd != TTW_HASH) notify(s);
    if (cmd == TTW_PING || result != TT_OK) snapshot(s, sender, result, Ttw_Read(packet + 508));
}

/* ---- the owner: admission (p2p.cpp admit, from a HELLO in place of the signaling service's member list) ---- */
static void owner_hello(Vtn *s, const VtnAddr *from, uint32_t nonce, const uint8_t *b, int n) {
    uint8_t join[VTN_PACKET], body[24];
    uint64_t content;
    unsigned options;
    int capable, slot, i, result;
    char name[20];
    if (n < 44) return;
    s->stats.hello_in++;
    slot = find_addr(s, from);
    if (slot >= 0) {
        if (s->peer[slot].nonce != nonce) leave(s, slot); /* the same address, a new join (its game restarted) */
        else {
            put32(body, s->session);
            put32(body + 4, s->peer[slot].id);
            body[8] = (uint8_t)slot;
            memcpy(body + 9, s->code, 6);
            hs_send(s, from, HS_WELCOME, nonce, body, 16);
            return;
        }
    }
    content = get64(b + 8);
    options = get32(b + 16);
    capable = get32(b + 20) != 0;
    memcpy(name, b + 24, 19);
    name[19] = 0;
    result = TT_OK;
    if (Kernel_Phase() != TT_LOBBY) result = TT_BAD_PHASE;           /* (the PC's room is joinable only in the lobby) */
    else if (content != s->content) result = TT_MISMATCH;            /* (the PC: the service compares the content) */
    else if ((Kernel_BattleRules() && !capable) || (Kernel_OptionsVersion() && !options)) result = TT_MISMATCH;
    slot = -1;
    if (result == TT_OK) {
        for (i = 0; i < TAGPDP_MAX_PEERS; ++i)
            if (i != s->slot && !s->peer[i].used) { slot = i; break; }
        if (slot < 0) result = TT_FULL;
    }
    if (result == TT_OK) {
        VtnPeer *p = &s->peer[slot];
        memset(p, 0, sizeof *p);
        p->used = 1;
        p->addr = *from;
        p->nonce = nonce;
        p->id = s->next_member++;
        p->heard_ms = now_ms(s);
        p->capable = capable;
        p->options = options;
        memcpy(p->name, name, sizeof p->name);
        TagPdp_ResetPeer(&s->pdp, slot);
        /* p2p.cpp admit: the TTW_JOIN the owner builds for the joiner, with the room's own content */
        memset(join, 0, sizeof join);
        Ttw_Request(join, TTW_JOIN, Kernel_Epoch());
        write64(join + 24, s->content);
        Ttw_Write(join + TTW_JOIN_BATTLE_RULES, options >= TT_OPTION_COMBAT_VERSION ? 3 : options == TT_OPTIONS_VERSION ? 2 : (unsigned)capable);
        snprintf((char *)join + 32, 20, "%s", name);
        memcpy(Kernel_Input(), join, VTN_PACKET);
        result = Kernel_Command(p->id);
        if (result != TT_OK) memset(p, 0, sizeof *p);
    }
    if (result != TT_OK) {
        put32(body, (uint32_t)result);
        hs_send(s, from, HS_REJECT, nonce, body, 4);
        s->stats.rejects++;
        return;
    }
    s->peer[slot].admitted = 1;
    put32(body, s->session);
    put32(body + 4, s->peer[slot].id);
    body[8] = (uint8_t)slot;
    memcpy(body + 9, s->code, 6);
    hs_send(s, from, HS_WELCOME, nonce, body, 16);
    s->stats.welcome++;
    notify(s);
}

static void handshake(Vtn *s, const VtnAddr *from, const uint8_t *b, int n) {
    int kind = b[2];
    uint32_t nonce = get32(b + 4);
    if (n < 8 || b[3] != HS_VERSION) return;
    if (s->owner) {
        if (!s->self) return;
        if (kind == HS_HELLO) owner_hello(s, from, nonce, b, n);
        else if (kind == HS_BYE && n >= 16 && get32(b + 8) == s->session) {
            int slot = find_addr(s, from);
            if (slot >= 0 && s->peer[slot].id == get32(b + 12)) leave(s, slot);
        }
        return;
    }
    if (!s->hello_started || !same_addr(from, &s->peer[0].addr)) return;
    if (kind == HS_WELCOME && n >= 24 && nonce == s->nonce && !s->joined) {
        int slot = b[16];
        if (slot <= 0 || slot >= TAGPDP_MAX_PEERS) return;
        s->session = get32(b + 8);
        s->self = get32(b + 12);
        s->slot = slot;
        memcpy(s->code, b + 17, 6);
        s->code[6] = 0;
        s->peer[0].heard_ms = now_ms(s);
        if (TagPdp_Init(&s->pdp, &s->pdp_ops, s, s->session, slot) != 0) {
            fail(s, "Cannot start the connection to the room.");
            return;
        }
        s->joined = 1;
        s->stats.welcome++;
    } else if (kind == HS_REJECT && n >= 12 && nonce == s->nonce && !s->joined) {
        s->stats.rejects++;
        fail(s, reject_text((int)get32(b + 8)));
    } else if (kind == HS_BYE && n >= 16 && s->joined && get32(b + 8) == s->session) {
        fail(s, "The host closed the Tag Team room.");
    }
}

/* ---- a client's frames, in order (p2p.cpp flushFrames) ---- */
static void flush_frames(Vtn *s) {
    if (s->phase != TT_FIGHT) return;
    for (;;) {
        VtnFrame *f = &s->reorder[s->expected % VTN_REORDER];
        if (!f->used || f->frame != s->expected) break;
        emit(s, f->bytes, f->at_ms);
        f->used = 0;
        s->reorder_count--;
        s->expected++;
    }
}

/* p2p.cpp event kind 6: one 512-byte packet from a member */
static void on_message(Vtn *s, int slot, int reliable, const uint8_t *packet, int len) {
    VtnPeer *p = &s->peer[slot];
    unsigned cmd, packet_epoch;
    uint64_t at = now_ms(s);
    if (len != VTN_PACKET || Ttw_Read(packet) != TT_PROTOCOL) return;
    p->heard_ms = at;
    cmd = Ttw_Read(packet + 4);
    packet_epoch = Ttw_Read(packet + 8);
    if (cmd == PING_COMMAND) {
        uint8_t pong[VTN_PACKET];
        memcpy(pong, packet, VTN_PACKET);
        Ttw_Write(pong + 4, PONG_COMMAND);
        pdp_out(s, slot, pong, 0);
        return;
    }
    if (cmd == PONG_COMMAND) {
        if (p->probe_ms && Ttw_Read(packet + 12) == p->probe && at >= p->probe_ms && at - p->probe_ms < 3000) {
            unsigned sample = (unsigned)(at - p->probe_ms);
            p->probe_ms = 0;
            p->ping_ms = at;
            p->measured = (int)sample;
            if (!s->owner) { /* p2p.cpp pingSample: the best of the last eight */
                unsigned i, best = sample;
                s->ping_samples[s->ping_cursor++ % 8] = sample;
                if (s->ping_count < 8) s->ping_count++;
                for (i = 0; i < s->ping_count; ++i)
                    if (s->ping_samples[i] < best) best = s->ping_samples[i];
                s->ping = (int)best;
            }
        }
        return;
    }
    if (cmd == REPLAY_COMMAND && reliable && s->owner && packet_epoch == s->epoch) {
        uint32_t wanted = Ttw_Read(packet + 12), f;
        unsigned sent = 0;
        s->stats.replays_asked++;
        for (f = wanted; s->history_count && (int32_t)(s->latest - f) >= 0 && sent < 16; ++f) {
            VtnFrame *h = &s->history[f % VTN_HISTORY];
            if (!h->used || h->frame != f) continue;
            if (pdp_out(s, slot, h->bytes, 1)) { ++sent; s->stats.replays_sent++; }
        }
        return;
    }
    if (s->owner) {
        if (!p->admitted) return;
        if (cmd == TTW_INPUT || reliable) command(s, p->id, packet);
        return;
    }
    if (slot != 0) return;
    if (cmd == TTW_OPTIONS_STATE && reliable) {
        emit(s, packet, at);
        return;
    }
    if (cmd == TTW_STATE && reliable) {
        TtRoom r;
        uint32_t target;
        int result;
        char code[7];
        if (!Ttw_Decode(packet, &r, &target, &result, code) || target != s->self || r.host != 1) return;
        if (s->epoch && (int32_t)(r.epoch - s->epoch) < 0) return;
        if (r.epoch != s->epoch) clear_round(s, r.epoch);
        s->phase = r.phase;
        p->admitted = 1;
        s->status = 1;
        emit(s, packet, at);
        flush_frames(s);
        return;
    }
    if (cmd == TTW_FRAME && packet_epoch == s->epoch) {
        uint32_t frame = Ttw_Read(packet + 12);
        VtnFrame *f;
        s->stats.frames_in++;
        if ((int32_t)(frame - s->expected) < 0) { s->stats.frames_dup++; return; }
        if (frame - s->expected >= TT_INPUT_HISTORY) {
            fail(s, "This client fell too far behind the battle. The other players can continue.");
            return;
        }
        if (frame - s->expected >= VTN_REORDER) { s->stats.frames_far++; return; } /* (asked again by the replay) */
        f = &s->reorder[frame % VTN_REORDER];
        if (f->used && f->frame == frame) { s->stats.frames_dup++; return; }
        if (!f->used) s->reorder_count++;
        f->used = 1;
        f->frame = frame;
        f->at_ms = at;
        memcpy(f->bytes, packet, VTN_PACKET);
        flush_frames(s);
    } else if (cmd == TTW_CHAT && reliable) emit(s, packet, at);
}

/* ---- the owner's clock (p2p.cpp run: the FIGHT tick) ---- */
static void owner_tick(Vtn *s) {
    uint64_t step = 1000000000ull / (s->rate ? s->rate : VTN_RATE_DEFAULT), t = now_us(s);
    unsigned burst = 0;
    int i;
    while (t >= s->next_tick_us && burst++ < 4) {
        if (Kernel_DeadlineFrame()) s->stats.late_mask_frames++;
        if (Kernel_TakeFrame()) {
            const uint8_t *out = Kernel_Output();
            uint32_t frame = Ttw_Read(out + 12), k;
            VtnFrame *h = &s->history[frame % VTN_HISTORY];
            h->used = 1;
            h->frame = frame;
            h->at_ms = t / 1000;
            memcpy(h->bytes, out, VTN_PACKET);
            if (s->history_count < VTN_HISTORY) s->history_count++;
            s->latest = frame;
            s->stats.frames_ticked++;
            emit(s, h->bytes, h->at_ms);
            /* the three latest to every member, unreliable (each frame goes three times) */
            for (i = 0; i < TAGPDP_MAX_PEERS; ++i) {
                if (i == s->slot || !s->peer[i].used || !s->peer[i].admitted) continue;
                for (k = frame >= 2 ? frame - 2 : 0; k <= frame; ++k) {
                    VtnFrame *old = &s->history[k % VTN_HISTORY];
                    if (old->used && old->frame == k && pdp_out(s, i, old->bytes, 0)) s->stats.frames_sent++;
                }
            }
        }
        s->next_tick_us += step;
    }
    if (s->next_tick_us + step * 4 < t) s->next_tick_us = t;
}

/* ---- the requests that start a session (p2p.cpp initialize) ---- */
static void owner_open(Vtn *s, const uint8_t *packet) {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    int i, result;
    if (Ttw_Read(packet + 64) == 1) { /* Quick Match: the PC's service queue, which the Vita's rooms do not have */
        fail(s, "Quick Match needs the online service.");
        return;
    }
    s->content = read64(packet + 24);
    memcpy(s->name, packet + 32, 19);
    s->name[19] = 0;
    for (i = 0; i < 6; ++i) s->code[i] = alphabet[(s->session >> (i * 5)) & 31];
    s->code[6] = 0;
    Kernel_Reset();
    Kernel_Pacing(1);
    memcpy(Kernel_Input(), packet, VTN_PACKET);
    result = Kernel_Command(1);
    if (result != TT_OK) {
        fail(s, "The Tag Team room could not be opened with these settings.");
        return;
    }
    Kernel_Seed((unsigned)(now_us(s) ^ s->session));
    s->self = 1;
    s->slot = 0;
    s->next_member = 2;
    s->peer[0].used = 1;
    s->peer[0].id = 1;
    s->epoch = Kernel_Epoch();
    if (TagPdp_Init(&s->pdp, &s->pdp_ops, s, s->session, 0) != 0) {
        fail(s, "Cannot start the room's connection.");
        return;
    }
    s->joined = 1;
    snapshot(s, s->self, TT_OK, 0);
    s->status = 1;
}
static void client_join(Vtn *s, const uint8_t *packet) {
    unsigned level = Ttw_Read(packet + TTW_JOIN_BATTLE_RULES);
    s->content = read64(packet + 24);
    memcpy(s->name, packet + 32, 19);
    s->name[19] = 0;
    /* p2p.cpp: the PC joiner answers the owner's capability offer with these (BT4_TAG_TEAM_EXPANDED writes 3) */
    s->peer[0].capable = level >= 1;
    s->peer[0].options = level >= 3 ? TT_OPTION_COMBAT_VERSION : level >= 2 ? TT_OPTIONS_VERSION : 0;
    s->hello_started = 1;
    s->hello_ms = 0;
    s->began_ms = now_ms(s);
}
static void client_hello(Vtn *s) {
    uint8_t body[36];
    uint64_t t = now_ms(s);
    if (s->joined || !s->hello_started) return;
    if (t - s->began_ms > VTN_JOIN_MS) {
        fail(s, "No Tag Team room answered at this address.");
        return;
    }
    if (s->hello_ms && t - s->hello_ms < VTN_HELLO_MS) return;
    s->hello_ms = t;
    memset(body, 0, sizeof body);
    put64(body, s->content);
    put32(body + 8, s->peer[0].options);
    put32(body + 12, (uint32_t)s->peer[0].capable);
    memcpy(body + 16, s->name, 19);
    hs_send(s, &s->peer[0].addr, HS_HELLO, s->nonce, body, 36);
}

/* ---- the interface ---- */
int Vtn_Open(Vtn *s, const VtnLink *link, void *ctx, const VtnAddr *owner) {
    uint64_t t;
    if (!s || !link || !link->send || !link->recv || !link->now_us) return 0;
    memset(s, 0, sizeof *s);
    s->link = link;
    s->ctx = ctx;
    s->pdp_ops.send = op_send;
    s->pdp_ops.recv = op_recv;
    s->pdp_ops.now_ms = op_now;
    s->rate = VTN_RATE_DEFAULT;
    s->ping = -1;
    s->phase = TT_CLOSED;
    t = now_us(s);
    s->session = (uint32_t)((t * 0x9E3779B97F4A7C15ull) >> 32) ^ (uint32_t)(uintptr_t)s;
    if (!s->session) s->session = 1;
    s->nonce = (uint32_t)((t * 0xC2B2AE3D27D4EB4Full) >> 29) | 1u;
    s->began_ms = t / 1000;
    if (owner) {
        s->peer[0].used = 1;
        s->peer[0].addr = *owner;
        s->peer[0].id = 1;
        s->slot = -1; /* (until WELCOME) */
    } else s->owner = 1;
    return 1;
}

int Vtn_Send(Vtn *s, const uint8_t packet[VTN_PACKET]) {
    unsigned cmd;
    if (!s || s->status < 0) return 0;
    cmd = Ttw_Read(packet + 4);
    s->stats.requests++;
    if (!s->self) {
        if (s->owner && cmd == TTW_OPEN && !s->joined) owner_open(s, packet);
        else if (!s->owner && cmd == TTW_JOIN && !s->hello_started) client_join(s, packet);
        return s->status >= 0;
    }
    if (s->owner) {
        command(s, s->self, packet); /* (applied at once: the PC queues it for its thread's next round) */
        return s->status >= 0;
    }
    if (!pdp_out(s, 0, packet, cmd != TTW_INPUT) && cmd != TTW_INPUT) {
        fail(s, "The connection to the room's host was lost.");
        return 0;
    }
    return 1;
}

int Vtn_Read(Vtn *s, uint8_t packet[VTN_PACKET], uint64_t *at_ms) {
    if (!s) return -1;
    if (!s->queue_count) return s->status < 0 ? -1 : 0;
    memcpy(packet, s->queue[s->queue_head].bytes, VTN_PACKET);
    if (at_ms) *at_ms = s->queue[s->queue_head].at_ms;
    s->queue_head = (s->queue_head + 1) % VTN_QUEUE;
    s->queue_count--;
    return 1;
}

void Vtn_Step(Vtn *s) {
    uint8_t buf[TAGPDP_MAX_PAYLOAD];
    uint64_t t;
    int i, round;
    if (!s || s->status < 0) return;
    if (!s->joined) {
        /* a joiner before WELCOME: only handshake datagrams count */
        for (i = 0; i < 64; ++i) {
            VtnAddr a;
            uint8_t raw[TAGPDP_RECV_SIZE];
            int n = s->link->recv(s->ctx, &a, raw, (int)sizeof raw);
            if (n <= 0) break;
            if (n >= 8 && get16(raw) == HS_MAGIC) handshake(s, &a, raw, n);
            if (s->status < 0) return;
            if (s->joined) break; /* (what follows WELCOME goes through TagPdp) */
        }
        client_hello(s);
        if (!s->joined) return;
    }
    for (round = 0; round < 4; ++round) {
        int got = TagPdp_Poll(&s->pdp), peer, channel, n;
        for (i = 0; i < s->hs_count; ++i) handshake(s, &s->hs[i].from, s->hs[i].bytes, s->hs[i].len);
        s->hs_count = 0;
        while ((n = TagPdp_Read(&s->pdp, &peer, &channel, buf, (int)sizeof buf)) >= 0) {
            on_message(s, peer, channel == TAGPDP_CONTROL, buf, n);
            if (s->status < 0) return;
        }
        if (n == -2) { /* (a message larger than any Tag packet: drop it) */
            uint8_t big[TAGPDP_MAX_PAYLOAD];
            TagPdp_Read(&s->pdp, &peer, &channel, big, (int)sizeof big);
        }
        if (!got) break;
    }
    backlog_flush(s);
    if (s->owner && s->self && s->clock && Kernel_Phase() == TT_FIGHT) owner_tick(s);
    t = now_ms(s);
    if (s->self && t >= s->next_ping_ms) { /* p2p.cpp run: pings every half second, the silent ones gone */
        int host_ping = -1;
        s->next_ping_ms = t + 500;
        for (i = 0; i < TAGPDP_MAX_PEERS; ++i) {
            VtnPeer *p = &s->peer[i];
            if (i == s->slot || !p->used) continue;
            if (t > p->heard_ms && t - p->heard_ms > VTN_SILENT_MS) {
                if (s->owner) { leave(s, i); continue; }
                fail(s, "The room's host stopped answering.");
                return;
            }
            if (!p->probe_ms || t - p->probe_ms >= 3000) {
                uint8_t packet[VTN_PACKET];
                memset(packet, 0, sizeof packet);
                Ttw_Request(packet, PING_COMMAND, s->epoch);
                p->probe = ++s->probe_serial;
                Ttw_Write(packet + 12, p->probe);
                p->probe_ms = pdp_out(s, i, packet, 0) ? t : 0;
            }
            if (p->ping_ms && t - p->ping_ms < 4000 && p->measured > host_ping) host_ping = p->measured;
        }
        if (s->owner) s->ping = host_ping;
    }
    if (!s->owner && s->reorder_count > 0 && t - s->last_replay_ms >= 100) { /* p2p.cpp: replay what is missing */
        uint8_t packet[VTN_PACKET];
        s->last_replay_ms = t;
        memset(packet, 0, sizeof packet);
        Ttw_Request(packet, REPLAY_COMMAND, s->epoch);
        Ttw_Write(packet + 12, s->expected);
        pdp_out(s, 0, packet, 1);
    }
}

void Vtn_Close(Vtn *s) {
    uint8_t body[8];
    int i, k;
    if (!s || !s->link) return;
    if (s->self) {
        put32(body, s->session);
        put32(body + 4, s->self);
        for (k = 0; k < 3; ++k)
            for (i = 0; i < TAGPDP_MAX_PEERS; ++i)
                if (i != s->slot && s->peer[i].used) hs_send(s, &s->peer[i].addr, HS_BYE, 0, body, 8);
    }
    fail(s, "The Tag Team room was closed.");
}

int Vtn_Ping(const Vtn *s) { return s ? s->ping : -1; }

void Vtn_SetRate(Vtn *s, unsigned per_1000s) {
    if (s) s->rate = per_1000s ? per_1000s : VTN_RATE_DEFAULT;
}

void Vtn_Describe(const Vtn *s, char *out, int size) {
    const TagPdpStats *t;
    if (!s || !out || size <= 0) return;
    t = s->joined ? TagPdp_Stats(&s->pdp) : NULL;
    snprintf(out, (size_t)size,
             "%s member=%u status=%d phase=%d epoch=%u ping=%d frames ticked=%u sent=%u in=%u dup=%u far=%u expected=%u "
             "replay asked=%u sent=%u late=%u queue_max=%u backlog_max=%u send_fail=%u hello=%u welcome=%u reject=%u "
             "leaves=%u pdp out=%u in=%u resends=%u drops=%u/%u/%u",
             s->owner ? "owner" : "client", (unsigned)s->self, s->status, s->owner ? Kernel_Phase() : s->phase,
             (unsigned)s->epoch, s->ping, s->stats.frames_ticked, s->stats.frames_sent, s->stats.frames_in,
             s->stats.frames_dup, s->stats.frames_far, (unsigned)s->expected, s->stats.replays_asked,
             s->stats.replays_sent, s->stats.late_mask_frames, s->stats.queue_max, s->stats.backlog_max,
             s->stats.send_fail, s->stats.hello_in, s->stats.welcome, s->stats.rejects, s->stats.leaves,
             t ? t->packets_out : 0, t ? t->packets_in : 0, t ? t->resends : 0, t ? t->inbox_drops : 0,
             t ? t->window_drops : 0, t ? t->bad_drops : 0);
}
