/*
 * Test of port/vita/vita_tag_net.c (the online Tag Team room between Vitas, T1): the owner and up to three joiners,
 * each a Vtn with its own little "game", in one process over an in-memory link on a simulated clock (loss,
 * duplication, delay with jitter, so reordering too). The game side does what the battle and the menus do through the
 * Port_Tag* calls: reads the states and frames, chooses fighters, loads, arms, starts, sends its pads ahead of the
 * frame it is at (lead from the ping, as Port_TagDelay), takes the frames, ends the round and asks for a rematch.
 *
 * Host:  gcc -O2 -Wall -Wextra -DTT_KERNEL_NATIVE=1 -I port/vita -I port/include port/vita/vita_tagpdp.c \
 *          port/vita/vita_tag_net.c port/src/tag_team.c port/src/tag_team_wire.c port/vita/vita_tag_authority.c \
 *          port/vita/tools/tagnet_test.c -o /tmp/tagnet_test && /tmp/tagnet_test
 * Vita:  bt3.txt tagnet_test=1 (2: the run ends after it) in a BT4_VITA_NETLINK=1 build; lines "tagnet:" in the log.
 *
 * Checks per case: every member reaches the fight, every member takes the same frames (all bytes) in the same order;
 * a member's own pad is in the frame when it was not late; the rematch takes everyone back to the character select
 * with the new epoch; admissions refused for the right reasons; a member gone silent is dropped and the fight goes on.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vita_tag_net.h"
#include "tag_team_wire.h"
#ifdef PORT_VITA
extern void Vita_Log(const char *fmt, ...);
#define printf Vita_Log
#define FIGHT_FRAMES 300
#else
#define FIGHT_FRAMES 900
#endif

#define EP_MAX 5
#define PKT_MAX 6000

/* ---- the link ---- */
typedef struct SimPkt { int used, src, dst, len; uint64_t at; uint8_t data[TAGPDP_MAX_PACKET]; } SimPkt;
typedef struct Sim {
    uint64_t now_us, rng;
    int loss_pct, dup_pct, base_ms, jitter_ms;
    unsigned cut;            /* endpoints whose datagrams vanish (gone silent) */
    SimPkt *pkt;
    uint32_t sent, lost;
} Sim;
static Sim sim;
static uint32_t rnd(void) { sim.rng ^= sim.rng << 13; sim.rng ^= sim.rng >> 7; sim.rng ^= sim.rng << 17; return (uint32_t)sim.rng; }

typedef struct Ep { int id; } Ep;
static Ep eps[EP_MAX];
static void addr_of(int id, VtnAddr *a) { memset(a, 0, sizeof *a); a->b[0] = (uint8_t)(id + 1); }
static int queue_pkt(int src, int dst, const void *d, int n, uint64_t at) {
    int i;
    for (i = 0; i < PKT_MAX; ++i)
        if (!sim.pkt[i].used) {
            SimPkt *p = &sim.pkt[i];
            p->used = 1; p->src = src; p->dst = dst; p->len = n; p->at = at;
            memcpy(p->data, d, (size_t)n);
            return 1;
        }
    return 0;
}
static int l_send(void *ctx, const VtnAddr *to, const void *data, int len) {
    Ep *e = (Ep *)ctx;
    int dst = to->b[0] - 1, copies = 1, k;
    if (dst < 0 || dst >= EP_MAX || len > TAGPDP_MAX_PACKET) return -1;
    sim.sent++;
    if ((sim.cut >> e->id) & 1) return len;
    if ((int)(rnd() % 100) < sim.loss_pct) { sim.lost++; return len; }
    if ((int)(rnd() % 100) < sim.dup_pct) copies = 2;
    for (k = 0; k < copies; ++k) {
        uint64_t delay = (uint64_t)sim.base_ms * 1000 + (sim.jitter_ms ? rnd() % ((unsigned)sim.jitter_ms * 1000) : 0);
        queue_pkt(e->id, dst, data, len, sim.now_us + delay);
    }
    return len;
}
static int l_recv(void *ctx, VtnAddr *from, void *buf, int size) {
    Ep *e = (Ep *)ctx;
    int i, best = -1;
    for (i = 0; i < PKT_MAX; ++i)
        if (sim.pkt[i].used && sim.pkt[i].dst == e->id && sim.pkt[i].at <= sim.now_us && (best < 0 || sim.pkt[i].at < sim.pkt[best].at)) best = i;
    if (best < 0) return 0;
    if ((sim.cut >> e->id) & 1) { sim.pkt[best].used = 0; return 0; }
    if (sim.pkt[best].len > size) { sim.pkt[best].used = 0; return 0; }
    memcpy(buf, sim.pkt[best].data, (size_t)sim.pkt[best].len);
    addr_of(sim.pkt[best].src, from);
    sim.pkt[best].used = 0;
    return sim.pkt[best].len;
}
static uint64_t l_now(void *ctx) { (void)ctx; return sim.now_us; }
static const VtnLink link_ops = { l_send, l_recv, l_now };

/* ---- the game side of one member (what Port_TagPoll / the menus / the battle do) ---- */
typedef struct Game {
    Vtn *s;
    int id, active, done, rejected, dropped;
    uint64_t content;
    TtRoom state;
    uint32_t member;
    char code[7];
    uint8_t options[TT_WIRE_BYTES];
    TtInputs *received;
    uint64_t next_tick_us, tick_us;
    /* what was sent once per epoch */
    uint32_t sent_epoch;
    int sent_select, sent_load, sent_loaded, sent_arm, sent_ack, sent_start, sent_ended, sent_result;
    uint32_t chose_mask;
    /* the fight */
    uint32_t frame, input_latest;
    int input_sent;
    uint32_t hashes[FIGHT_FRAMES];
    uint32_t taken, own_ok, own_late;
    uint32_t fight_epoch, final_epoch;
    int final_phase;
    char error[160];
} Game;

static uint8_t pad_of(int seat, uint32_t frame, int byte) { return (uint8_t)(seat * 37 + frame * 13 + byte * 7 + 1); }

static void request(Game *g, uint8_t *p) { Ttw_Write(p + 508, 0); if (!Vtn_Send(g->s, p)) snprintf(g->error, sizeof g->error, "send refused: %s", g->s->error ? g->s->error : "?"); }
static void command(Game *g, int cmd, uint32_t a, uint32_t b, uint32_t c, int digest) {
    uint8_t p[TT_WIRE_BYTES];
    memset(p, 0, sizeof p);
    Ttw_Request(p, cmd, g->state.epoch);
    Ttw_Write(p + 12, a); Ttw_Write(p + 16, b); Ttw_Write(p + 20, c);
    if (digest) { uint64_t d = Tt_LoadDigest(&g->state); Ttw_Write(p + 24, (uint32_t)d); Ttw_Write(p + 28, (uint32_t)(d >> 32)); }
    request(g, p);
}

static void game_poll(Game *g) {
    uint8_t p[TT_WIRE_BYTES];
    int n;
    while ((n = Vtn_Read(g->s, p, NULL)) > 0) {
        unsigned cmd = Ttw_Read(p + 4);
        int result, seat;
        if (Ttw_Read(p) != TT_PROTOCOL) continue;
        if (cmd == TTW_FRAME) {
            uint32_t epoch = Ttw_Read(p + 8), frame = Ttw_Read(p + 12);
            if (epoch != g->state.epoch || g->state.phase != TT_FIGHT) continue;
            if (g->received->epoch != epoch) Tt_InputsReset(g->received, epoch, Tt_Count(&g->state));
            if ((int32_t)(frame - g->received->next) < 0) continue;
            for (seat = 0; seat < Tt_Count(&g->state); ++seat)
                if (Tt_InputPut(g->received, epoch, seat, frame, p + 16 + seat * TT_PAD_BYTES) != TT_OK)
                    snprintf(g->error, sizeof g->error, "frame %u does not fit (seat %d)", (unsigned)frame, seat);
            continue;
        }
        if (cmd == TTW_OPTIONS_STATE) { memcpy(g->options, p, TT_WIRE_BYTES); continue; }
        if (cmd == TTW_CHAT) continue;
        if (!Ttw_Decode(p, &g->state, &g->member, &result, g->code)) { snprintf(g->error, sizeof g->error, "a packet does not decode"); continue; }
        if (g->state.options_hash && !Ttw_ApplyOptions(g->options, &g->state)) snprintf(g->error, sizeof g->error, "shared options missing");
        if (g->received->epoch != g->state.epoch || (g->state.phase != TT_FIGHT && g->received->seats != Tt_Count(&g->state)))
            Tt_InputsReset(g->received, g->state.epoch, Tt_Count(&g->state));
    }
    if (n < 0 && !g->dropped) { g->dropped = 1; snprintf(g->error, sizeof g->error, "%s", g->s->error ? g->s->error : "failed"); }
}

static int all(const TtRoom *r, int what) {
    int i;
    for (i = 0; i < Tt_Count(r); ++i) {
        const TtPlayer *p = &r->players[i];
        if ((what == 0 && !p->loaded) || (what == 1 && !p->armed) || (what == 2 && !p->ended)) return 0;
    }
    return Tt_Count(r) > 0;
}

static int owner_lead; /* the Vita's choice (vita_tag.c Port_TagDelay, bt3.txt tagnet_lead): the owner's pads this many
                          frames ahead too (the PC's owner: 0, its game draws at 60/s) */
static int lead(Game *g) { /* tag_team_net.c Port_TagDelay, peer mode */
    int backlog = 0, ping = Vtn_Ping(g->s), l = 1, full = 1, margin = 1;
    if (g->state.phase == TT_FIGHT && g->received->epoch == g->state.epoch) {
        uint32_t i;
        for (i = g->received->next; i < g->received->next + 64; ++i) {
            TtInputFrame *f = &g->received->ring[i % TT_INPUT_HISTORY];
            if (f->frame != i || f->mask != (1u << g->received->seats) - 1u) break;
            ++backlog;
        }
    }
    if (g->member == g->state.host) return backlog + owner_lead;
    if (ping > 0) {
        l = (ping * 342 + 19999) / 20000; full = (ping * 342 + 9999) / 10000;
        if (l < 1) l = 1;
        if (margin > full - l) margin = full - l;
    }
    return l + margin + backlog;
}

static void game_tick(Game *g, int expected_humans) {
    TtRoom *r = &g->state;
    int host = g->member && g->member == r->host, i, steps;
    game_poll(g);
    if (g->done || g->dropped || !g->member) return;
    if (r->epoch != g->sent_epoch) {
        g->sent_epoch = r->epoch;
        g->sent_select = g->sent_load = g->sent_loaded = g->sent_arm = g->sent_ack = g->sent_start = g->sent_ended = g->sent_result = 0;
        g->chose_mask = 0;
    }
    switch (r->phase) {
    case TT_LOBBY:
        if (host && !g->sent_select && Tt_Humans(r) >= expected_humans) { command(g, TTW_SELECT, 0, 0, 0, 0); g->sent_select = 1; }
        break;
    case TT_SELECT:
        if (g->fight_epoch) { /* back at the select after the rematch: this member's test is over */
            g->final_epoch = r->epoch; g->final_phase = r->phase; g->done = 1; return;
        }
        for (i = 0; i < Tt_Count(r); ++i) {
            const TtPlayer *p = &r->players[i];
            int mine = p->cpu ? host : p->member == g->member;
            if (!mine || p->confirmed || (g->chose_mask >> i & 1)) continue;
            {   uint8_t q[TT_WIRE_BYTES];
                memset(q, 0, sizeof q);
                Ttw_Request(q, TTW_CHOOSE, r->epoch);
                Ttw_Write(q + 12, (uint32_t)(10 + i)); Ttw_Write(q + 16, 0); Ttw_Write(q + 20, (uint32_t)i);
                request(g, q);
                g->chose_mask |= 1u << i;
            }
        }
        if (host && !g->sent_load && Tt_Turn(r, 0) < 0 && Tt_Turn(r, 1) < 0) { command(g, TTW_LOAD, 0, 0, 0, 1); g->sent_load = 1; }
        break;
    case TT_LOADING:
        if (!g->sent_loaded) { command(g, TTW_LOADED, 0, 0, 0, 1); g->sent_loaded = 1; }
        if (host && !g->sent_arm && all(r, 0)) { command(g, TTW_ARM, 0, 0, 0, 0); g->sent_arm = 1; }
        break;
    case TT_ARMED:
        if (!g->sent_ack) { command(g, TTW_ACK, 0, 0, 0, 0); g->sent_ack = 1; }
        if (host && !g->sent_start && all(r, 1)) { command(g, TTW_START, 0, 0, 0, 0); g->sent_start = 1; }
        break;
    case TT_FIGHT: {
        int seat = Tt_Seat(r, g->member);
        if (!g->fight_epoch) { g->fight_epoch = r->epoch; g->frame = 0; g->input_sent = 0; }
        if (g->frame >= FIGHT_FRAMES) {
            if (!g->sent_ended) { command(g, TTW_RESULT_ENDED, 0, 0, 0, 0); g->sent_ended = 1; }
            if (host && !g->sent_result && all(r, 2)) { command(g, TTW_RESULT, 1, 0, 0, 0); g->sent_result = 1; }
            break;
        }
        /* the pads, ahead by the lead (tag_team_net.c Port_TagSendInput, paced: no hole left) */
        {   uint32_t target = g->frame + (uint32_t)lead(g), next;
            if (!g->input_sent || target > g->input_latest) {
                for (next = g->input_sent ? g->input_latest + 1 : target; next <= target; ++next) {
                    uint8_t q[TT_WIRE_BYTES];
                    int b;
                    memset(q, 0, sizeof q);
                    Ttw_Request(q, TTW_INPUT, r->epoch);
                    Ttw_Write(q + 12, next);
                    for (b = 0; b < TT_PAD_BYTES; ++b) q[16 + b] = pad_of(seat, next, b);
                    request(g, q);
                }
                g->input_latest = target; g->input_sent = 1;
            }
        }
        /* the frames: one a tick, up to five while behind (the game's catch-up) */
        steps = lead(g) >= 6 && g->member != r->host ? 5 : 1;
        if (g->member == r->host && lead(g) >= 6) steps = 5;
        for (i = 0; i < steps && g->frame < FIGHT_FRAMES; ++i) {
            uint8_t pads[TT_MAX_PLAYERS][TT_PAD_BYTES];
            uint32_t h = 2166136261u;
            int a, b;
            if (g->received->epoch != r->epoch || Tt_InputTake(g->received, pads) != TT_OK) break;
            for (a = 0; a < Tt_Count(r); ++a) for (b = 0; b < TT_PAD_BYTES; ++b) h = (h ^ pads[a][b]) * 16777619u;
            g->hashes[g->frame] = h;
            if (pads[seat][0] == pad_of(seat, g->frame, 0) && pads[seat][1] == pad_of(seat, g->frame, 1)) g->own_ok++;
            else g->own_late++;
            g->frame++; g->taken++;
        }
        break; }
    default:
        break;
    }
}

/* ---- one case ---- */
typedef struct Case {
    const char *name;
    int clients;              /* joiners that should get in */
    int loss_pct, dup_pct, base_ms, jitter_ms;
    int game_hz;              /* the games' tick (the Vita's 2 vs 2: ~30) */
    int bad_content;          /* one more joiner with other game data: must be refused (mismatch) */
    int late_joiner;          /* one more joiner during the fight: must be refused (started) */
    int cut_at_frame;         /* > 0: the last joiner goes silent at this frame; the others must finish */
} Case;

static int run_case(const Case *c) {
    Vtn *v[EP_MAX];
    Game *g[EP_MAX];
    int n = 1 + c->clients + (c->bad_content ? 1 : 0) + (c->late_joiner ? 1 : 0), i, failed = 0, late_started = 0;
    int humans = 1 + c->clients;
    uint64_t end_us = 600ull * 1000000, step_us = 1000;
    char why[260] = "";
    VtnAddr owner_addr;
    memset(&sim, 0, sizeof sim);
    sim.pkt = (SimPkt *)calloc(PKT_MAX, sizeof(SimPkt));
    sim.rng = 0x2545F4914F6CDD1Dull;
    sim.loss_pct = c->loss_pct; sim.dup_pct = c->dup_pct; sim.base_ms = c->base_ms; sim.jitter_ms = c->jitter_ms;
    sim.now_us = 1000000;
    for (i = 0; i < n; ++i) { v[i] = (Vtn *)calloc(1, sizeof(Vtn)); g[i] = (Game *)calloc(1, sizeof(Game)); if (g[i]) g[i]->received = (TtInputs *)calloc(1, sizeof(TtInputs)); }
    for (i = 0; i < n; ++i)
        if (!sim.pkt || !v[i] || !g[i] || !g[i]->received) { printf("tagnet: %s: not run (no memory)\n", c->name); failed = 2; goto out; }
    addr_of(0, &owner_addr);
    for (i = 0; i < n; ++i) {
        eps[i].id = i;
        g[i]->s = v[i]; g[i]->id = i; g[i]->content = 0x1122334455667788ull;
        g[i]->tick_us = 1000000u / (unsigned)c->game_hz; g[i]->next_tick_us = sim.now_us + (uint64_t)i * 7000;
        Vtn_Open(v[i], &link_ops, &eps[i], i ? &owner_addr : NULL);
    }
    if (c->bad_content) g[1 + c->clients]->content ^= 1;
    /* the owner opens a 2 vs 2 room with CPU fill (the menus' Create Room), the joiners join */
    {   uint8_t p[TT_WIRE_BYTES];
        TtOptions o;
        memset(p, 0, sizeof p);
        Ttw_Request(p, TTW_OPEN, 0); Ttw_Write(p + 12, 2); Ttw_Write(p + 16, 0); Ttw_Write(p + 20, 0);
        Ttw_Write(p + 24, (uint32_t)g[0]->content); Ttw_Write(p + 28, (uint32_t)(g[0]->content >> 32));
        snprintf((char *)p + 32, 20, "Owner");
        Ttw_Write(p + 52, 1); Ttw_Write(p + 56, 2);
        Tt_OptionsDefault(&o); o.version = TT_OPTION_COMBAT_VERSION;
        for (i = 0; i < TT_MAX_PLAYERS; ++i) o.value[TT_OPTION_LEGACY_WORDS + i] = (int32_t)TT_COMBAT_DEFAULT;
        Ttw_PutOptions(p + TTW_OPEN_OPTIONS, &o);
        request(g[0], p);
    }
    for (i = 1; i < n; ++i) {
        uint8_t p[TT_WIRE_BYTES];
        if (c->late_joiner && i == n - 1) continue; /* (later) */
        memset(p, 0, sizeof p);
        Ttw_Request(p, TTW_JOIN, 0);
        memcpy(p + 12, "VITA00", 6);
        Ttw_Write(p + 24, (uint32_t)g[i]->content); Ttw_Write(p + 28, (uint32_t)(g[i]->content >> 32));
        snprintf((char *)p + 32, 20, "Vita %d", i);
        Ttw_Write(p + TTW_JOIN_BATTLE_RULES, 3);
        request(g[i], p);
    }
    for (; sim.now_us < end_us; sim.now_us += step_us) {
        int busy = 0;
        if (sim.now_us % 2000 == 0)
            for (i = 0; i < n; ++i) if (!((sim.cut >> i) & 1)) Vtn_Step(v[i]);
        for (i = 0; i < n; ++i) {
            if ((sim.cut >> i) & 1) continue;
            if (sim.now_us >= g[i]->next_tick_us) { g[i]->next_tick_us += g[i]->tick_us; game_tick(g[i], humans); }
        }
        if (c->late_joiner && !late_started && g[0]->state.phase == TT_FIGHT && g[0]->frame > 30) {
            uint8_t p[TT_WIRE_BYTES];
            int k = n - 1;
            late_started = 1;
            memset(p, 0, sizeof p);
            Ttw_Request(p, TTW_JOIN, 0);
            Ttw_Write(p + 24, (uint32_t)g[k]->content); Ttw_Write(p + 28, (uint32_t)(g[k]->content >> 32));
            snprintf((char *)p + 32, 20, "Late");
            Ttw_Write(p + TTW_JOIN_BATTLE_RULES, 3);
            request(g[k], p);
        }
        if (c->cut_at_frame && !sim.cut && g[c->clients]->frame >= (uint32_t)c->cut_at_frame) sim.cut = 1u << c->clients;
        for (i = 0; i <= c->clients; ++i) {
            if (c->cut_at_frame && i == c->clients) continue;
            if (!g[i]->done && !g[i]->dropped) busy = 1;
        }
        if (!busy) break;
    }
    /* the checks */
    for (i = 0; i <= c->clients && !failed; ++i) {
        int k;
        if (c->cut_at_frame && i == c->clients) continue;
        if (g[i]->error[0]) { snprintf(why, sizeof why, "member %d: %s", i, g[i]->error); failed = 1; break; }
        if (!g[i]->done) { snprintf(why, sizeof why, "member %d stopped at phase %d frame %u (epoch %u)", i, g[i]->state.phase, (unsigned)g[i]->frame, (unsigned)g[i]->state.epoch); failed = 1; break; }
        if (g[i]->final_epoch != g[0]->final_epoch || g[i]->final_phase != TT_SELECT) { snprintf(why, sizeof why, "member %d ended at epoch %u phase %d", i, (unsigned)g[i]->final_epoch, g[i]->final_phase); failed = 1; break; }
        for (k = 0; k < FIGHT_FRAMES; ++k)
            if (g[i]->hashes[k] != g[0]->hashes[k]) { snprintf(why, sizeof why, "member %d frame %d differs from the owner's", i, k); failed = 1; break; }
    }
    if (!failed && c->bad_content) {
        Game *b = g[1 + c->clients];
        if (!b->dropped || !strstr(b->error, "same game")) { snprintf(why, sizeof why, "the joiner with other data was not refused (%s)", b->error); failed = 1; }
    }
    if (!failed && c->late_joiner) {
        Game *b = g[n - 1];
        if (!b->dropped || !strstr(b->error, "already started")) { snprintf(why, sizeof why, "the late joiner was not refused (%s)", b->error); failed = 1; }
    }
    if (!failed && c->cut_at_frame && v[0]->stats.leaves < 1) { snprintf(why, sizeof why, "the silent member was not dropped"); failed = 1; }
    {   char line[600];
        uint32_t late = 0, ok = 0;
        for (i = 1; i <= c->clients; ++i) { late += g[i]->own_late; ok += g[i]->own_ok; }
        printf("tagnet: %s, owner lead %d: %s%s%s (%.1f s simulated, %u datagrams, %u lost; pads late: owner %u of %u, joiners %u of %u)\n",
               c->name, owner_lead, failed ? "FAIL" : "PASS", failed ? ": " : "", why, (double)(sim.now_us - 1000000) / 1e6,
               sim.sent, sim.lost, g[0]->own_late, g[0]->own_late + g[0]->own_ok, late, late + ok);
        Vtn_Describe(v[0], line, sizeof line);
        printf("tagnet:   %s\n", line);
        if (c->clients) { Vtn_Describe(v[1], line, sizeof line); printf("tagnet:   %s\n", line); }
    }
out:
    for (i = 0; i < n; ++i) { if (g[i]) free(g[i]->received); free(g[i]); free(v[i]); }
    free(sim.pkt);
    return failed;
}

static const Case cases[] = {
    { "2 Vitas, clean link", 1, 0, 0, 2, 2, 30, 0, 0, 0 },
    { "2 Vitas, 5% loss, 2-14 ms, duplicates", 1, 5, 3, 2, 12, 30, 0, 0, 0 },
    { "2 Vitas, games at 29/s (owner 34.2/s)", 1, 1, 0, 3, 3, 29, 0, 0, 0 },
    { "refusals: other game data, a join after the start", 1, 0, 0, 2, 2, 30, 1, 1, 0 },
#ifndef PORT_VITA
    { "4 Vitas, 5% loss, 2-20 ms", 3, 5, 2, 2, 18, 30, 0, 0, 0 },
    { "4 Vitas, one goes silent in the fight", 3, 2, 0, 2, 4, 30, 0, 0, 200 },
#endif
};

#ifdef PORT_VITA
int Vita_TagNetTest(void)
#else
int main(void)
#endif
{
    int i, failed = 0, nomem = 0;
    for (owner_lead = 0; owner_lead <= 1; ++owner_lead)
        for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); ++i) {
            int r;
#ifdef PORT_VITA
            if (owner_lead == 0 && i) continue; /* (the Vita: the comparison on the first case only) */
#endif
            r = run_case(&cases[i]);
            if (r == 1) failed = 1;
            if (r == 2) nomem = 1;
        }
    printf("tagnet: %s\n", failed ? "FAILED" : nomem ? "not all run (no memory)" : "all passed");
    return failed ? 1 : nomem ? 2 : 0;
}
