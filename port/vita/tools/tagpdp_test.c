/*
 * Host test for port/vita/vita_tagpdp.c (the transport under the online Tag Team, psvita-agent
 * notes/online/vita-multiplayer-plan.md section 7.5). Two or three endpoints in one process, each with its own TagPdp,
 * over an in-memory link on a simulated clock (1 ms steps). Per case: loss %, duplication %, and a random delay per
 * copy (base + jitter ms), so the link reorders too.
 *
 *   gcc -O2 -Wall -Wextra -I port/vita port/vita/vita_tagpdp.c port/vita/tools/tagpdp_test.c -o /tmp/tagpdp_test
 *
 * Checks per case: every control message arrives exactly once, in order, with the right bytes, and every control
 * message is acknowledged in the end (send queues empty); every unreliable message arrives at most as many times as the
 * link copied it, with the right bytes (and every one arrives on a lossless link); a member of another session is never
 * delivered to or from (its control messages stay unacknowledged). One line per case, PASS or FAIL; exit 1 when a case
 * fails.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vita_tagpdp.h"

#define MAX_EP 3
#define PKT_MAX 4096
#define CTL_MAX 5000
#define UNREL_MAX 2000
#define TICK_LIMIT_MS 600000u     /* simulated time per case */
#define TAIL_MS 400u              /* after everything is delivered, let the last unreliable copies arrive */

typedef struct SimPkt {
    int used;
    int src, dst;
    uint64_t at, id;
    int len;
    uint8_t data[TAGPDP_MAX_PACKET];
} SimPkt;

typedef struct Sim {
    uint64_t now, next_id, rng;
    int loss_pct, dup_pct, base_ms, jitter_ms;
    int hint;
    SimPkt pkt[PKT_MAX];
    uint64_t last_id[MAX_EP][MAX_EP];
    uint32_t sent, lost, duped, reordered, overflow;
    uint8_t made[MAX_EP][MAX_EP][UNREL_MAX];   /* copies of each unreliable message the link accepted */
} Sim;

typedef struct Ep {
    Sim *sim;
    int id;
    TagPdp pdp;
    TagPdpOps ops;
} Ep;

typedef struct Case {
    const char *name;
    int n;
    uint32_t session[MAX_EP];
    int active[MAX_EP][MAX_EP];    /* who sends to whom */
    int loss_pct, dup_pct, base_ms, jitter_ms;
    uint32_t ctl_msgs, unrel_msgs;
} Case;

typedef struct Run {
    Sim sim;
    Ep ep[MAX_EP];
    uint32_t ctl_sent[MAX_EP][MAX_EP], ctl_got[MAX_EP][MAX_EP], unrel_sent[MAX_EP][MAX_EP];
    uint8_t unrel_got[MAX_EP][MAX_EP][UNREL_MAX];
    int failed;
    char why[240];
} Run;

typedef struct Result {
    uint32_t resends, dup_drops, rtt_ms, unrel_delivered, unrel_copies, queue_left, session_drops;
    uint32_t pairs;
} Result;

static Run run;

static void fail(const char *fmt, ...) {
    va_list ap;
    if (run.failed)
        return;
    run.failed = 1;
    va_start(ap, fmt);
    vsnprintf(run.why, sizeof run.why, fmt, ap);
    va_end(ap);
}

/* ---- the simulated link ---- */

static uint64_t rnd(Sim *s) {
    s->rng ^= s->rng << 13;
    s->rng ^= s->rng >> 7;
    s->rng ^= s->rng << 17;
    return s->rng;
}

static int chance(Sim *s, int pct) {
    return pct > 0 && (int)(rnd(s) % 100u) < pct;
}

static uint32_t get32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void put32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

static void link_put(Sim *s, int src, int dst, const uint8_t *data, int len) {
    int i, slot = -1;
    SimPkt *k;
    for (i = 0; i < PKT_MAX; i++) {
        int j = (s->hint + i) % PKT_MAX;
        if (!s->pkt[j].used) {
            slot = j;
            break;
        }
    }
    if (slot < 0) {
        s->overflow++;
        return;
    }
    s->hint = (slot + 1) % PKT_MAX;
    k = &s->pkt[slot];
    k->used = 1;
    k->src = src;
    k->dst = dst;
    k->id = ++s->next_id;
    k->at = s->now + (uint64_t)s->base_ms +
            (s->jitter_ms > 0 ? rnd(s) % (uint64_t)(s->jitter_ms + 1) : 0);
    k->len = len;
    memcpy(k->data, data, (size_t)len);
}

static void link_send(Sim *s, int src, int dst, const uint8_t *data, int len) {
    int copies, c;
    s->sent++;
    if (chance(s, s->loss_pct)) {
        s->lost++;
        return;
    }
    copies = chance(s, s->dup_pct) ? 2 : 1;
    if (copies == 2)
        s->duped++;
    for (c = 0; c < copies; c++) {
        link_put(s, src, dst, data, len);
        if (len >= TAGPDP_HEADER + 8 && data[7] == TAGPDP_UNRELIABLE) {
            uint32_t idx = get32(data + TAGPDP_HEADER + 4);
            if (idx < UNREL_MAX && s->made[src][dst][idx] < 255)
                s->made[src][dst][idx]++;
        }
    }
}

/* the earliest packet due for dst (ties by send order); copies it into buf and removes it */
static int link_recv(Sim *s, int dst, int *src, void *buf, int size) {
    int i, best = -1, n;
    SimPkt *k;
    for (i = 0; i < PKT_MAX; i++) {
        SimPkt *c = &s->pkt[i];
        if (!c->used || c->dst != dst || c->at > s->now)
            continue;
        if (best < 0 || c->at < s->pkt[best].at || (c->at == s->pkt[best].at && c->id < s->pkt[best].id))
            best = i;
    }
    if (best < 0)
        return 0;
    k = &s->pkt[best];
    n = k->len < size ? k->len : size;
    memcpy(buf, k->data, (size_t)n);
    *src = k->src;
    if (k->id < s->last_id[k->src][dst])
        s->reordered++;
    else
        s->last_id[k->src][dst] = k->id;
    k->used = 0;
    return n;
}

/* ---- the endpoints ---- */

static int ep_send(void *ctx, int peer, const void *data, int len) {
    Ep *e = (Ep *)ctx;
    link_send(e->sim, e->id, peer, (const uint8_t *)data, len);
    return len;
}

static int ep_recv(void *ctx, int *peer, void *buf, int size) {
    Ep *e = (Ep *)ctx;
    return link_recv(e->sim, e->id, peer, buf, size);
}

static uint64_t ep_now(void *ctx) {
    return ((Ep *)ctx)->sim->now;
}

/* ---- the messages: length and bytes follow from (src, dst, index), so the receiver can check them ---- */

static uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static int msg_len(int src, int dst, uint32_t idx, int unrel) {
    uint32_t h = idx * 2654435761U + (uint32_t)(src * 1009 + dst * 7919) + (unrel ? 0x5bd1e995U : 0U);
    return 8 + (int)(mix(h) % 521u);
}

static uint8_t fill_byte(int src, int dst, uint32_t idx, int k) {
    return (uint8_t)(idx * 131u + (uint32_t)src * 17u + (uint32_t)dst * 5u + (uint32_t)k * 7u + ((uint32_t)k >> 3));
}

/* the message: sender (4 bytes), index (4 bytes), then fill bytes */
static int build_msg(uint8_t *buf, int src, int dst, uint32_t idx, int unrel) {
    int len = msg_len(src, dst, idx, unrel), k;
    put32(buf, (uint32_t)src);
    put32(buf + 4, idx);
    for (k = 8; k < len; k++)
        buf[k] = fill_byte(src, dst, idx, k);
    return len;
}

static int check_msg(const uint8_t *buf, int n, int src, int dst, uint32_t idx, int unrel) {
    int k;
    if (n != msg_len(src, dst, idx, unrel))
        return 0;
    if (get32(buf) != (uint32_t)src || get32(buf + 4) != idx)
        return 0;
    for (k = 8; k < n; k++) {
        if (buf[k] != fill_byte(src, dst, idx, k))
            return 0;
    }
    return 1;
}

/* ---- one case ---- */

static int run_case(const Case *c, int seed, Result *res) {
    static const TagPdpOps ops_template = { ep_send, ep_recv, ep_now };
    int e, a, b, same[MAX_EP][MAX_EP];
    uint64_t start;
    uint64_t tail = 0;
    int done = 0;

    memset(&run, 0, sizeof run);
    memset(res, 0, sizeof *res);
    run.sim.rng = 0x9E3779B97F4A7C15ULL ^ ((uint64_t)seed * 0x2545F4914F6CDD1DULL);
    run.sim.loss_pct = c->loss_pct;
    run.sim.dup_pct = c->dup_pct;
    run.sim.base_ms = c->base_ms;
    run.sim.jitter_ms = c->jitter_ms;
    run.sim.now = 1000;
    start = run.sim.now;

    for (e = 0; e < c->n; e++) {
        run.ep[e].sim = &run.sim;
        run.ep[e].id = e;
        run.ep[e].ops = ops_template;
        if (TagPdp_Init(&run.ep[e].pdp, &run.ep[e].ops, &run.ep[e], c->session[e], e) != 0)
            fail("TagPdp_Init failed for member %d", e);
    }
    for (a = 0; a < c->n; a++) {
        for (b = 0; b < c->n; b++) {
            same[a][b] = c->session[a] == c->session[b];
            if (a != b && c->active[a][b])
                res->pairs++;
        }
    }

    for (;;) {
        int tick_odd;
        if (run.failed)
            break;
        if (run.sim.now - start > TICK_LIMIT_MS) {
            fail("timeout: control not finished after %u simulated ms", (unsigned)TICK_LIMIT_MS);
            break;
        }
        run.sim.now += 1;
        tick_odd = (int)(run.sim.now & 1);

        for (e = 0; e < c->n && !run.failed; e++) {
            TagPdp *p = &run.ep[e].pdp;
            uint8_t buf[TAGPDP_MAX_PAYLOAD];
            TagPdp_Poll(p);
            for (;;) {
                int peer = -1, ch = -1;
                int r = TagPdp_Read(p, &peer, &ch, buf, (int)sizeof buf);
                if (r == -1)
                    break;
                if (r < 0 || peer < 0 || peer >= c->n || peer == e) {
                    fail("read error at member %d (code %d, from %d)", e, r, peer);
                    break;
                }
                if (!c->active[peer][e] || !same[peer][e]) {
                    fail("a message from member %d reached member %d, which must not see it", peer, e);
                    break;
                }
                if (ch == TAGPDP_CONTROL) {
                    uint32_t want = run.ctl_got[peer][e];
                    if (!check_msg(buf, r, peer, e, want, 0)) {
                        fail("control %d -> %d: message %u is wrong or out of order", peer, e, (unsigned)want);
                        break;
                    }
                    run.ctl_got[peer][e]++;
                } else if (ch == TAGPDP_UNRELIABLE) {
                    uint32_t idx = get32(buf + 4);
                    if (idx >= UNREL_MAX || !check_msg(buf, r, peer, e, idx, 1)) {
                        fail("unreliable %d -> %d: message %u is corrupt", peer, e, (unsigned)idx);
                        break;
                    }
                    if (run.unrel_got[peer][e][idx] < 255)
                        run.unrel_got[peer][e][idx]++;
                } else {
                    fail("unknown channel %d at member %d", ch, e);
                    break;
                }
            }

            for (b = 0; b < c->n && !run.failed; b++) {
                if (b == e || !c->active[e][b])
                    continue;
                while (run.ctl_sent[e][b] < c->ctl_msgs) {
                    int len = build_msg(buf, e, b, run.ctl_sent[e][b], 0);
                    if (TagPdp_Send(p, b, TAGPDP_CONTROL, buf, len) != 0)
                        break;
                    run.ctl_sent[e][b]++;
                }
                if (!tick_odd && run.unrel_sent[e][b] < c->unrel_msgs) {
                    int len = build_msg(buf, e, b, run.unrel_sent[e][b], 1);
                    if (TagPdp_Send(p, b, TAGPDP_UNRELIABLE, buf, len) == 0)
                        run.unrel_sent[e][b]++;
                }
            }
        }
        if (run.failed)
            break;

        /* done: every same-session control message delivered and acknowledged, every unreliable one sent */
        done = 1;
        for (a = 0; a < c->n && done; a++) {
            for (b = 0; b < c->n && done; b++) {
                if (a == b || !c->active[a][b] || !same[a][b])
                    continue;
                if (run.ctl_got[a][b] < c->ctl_msgs || TagPdp_QueueDepth(&run.ep[a].pdp, b) > 0 ||
                    run.unrel_sent[a][b] < c->unrel_msgs)
                    done = 0;
            }
        }
        if (done) {
            tail++;
            if (tail >= TAIL_MS)
                break;
        }
    }

    if (!run.failed && run.sim.overflow)
        fail("the test's link pool overflowed (%u packets): raise PKT_MAX", (unsigned)run.sim.overflow);

    /* the checks that need the whole run */
    for (a = 0; a < c->n && !run.failed; a++) {
        for (b = 0; b < c->n && !run.failed; b++) {
            uint32_t i;
            int held;
            if (a == b || !c->active[a][b])
                continue;
            if (!same[a][b]) {
                /* another session: nothing delivered; the messages stay in a's queue, unacknowledged */
                held = TagPdp_QueueDepth(&run.ep[a].pdp, b);
                if (run.ctl_got[a][b] != 0)
                    fail("member %d (other session) reached member %d", a, b);
                else if (held != (run.ctl_sent[a][b] < TAGPDP_WINDOW ? (int)run.ctl_sent[a][b] : TAGPDP_WINDOW))
                    fail("member %d (other session): its queue to %d has %d messages", a, b, held);
                for (i = 0; i < UNREL_MAX && !run.failed; i++) {
                    if (run.unrel_got[a][b][i])
                        fail("member %d (other session): unreliable message %u reached %d", a, b, (unsigned)i);
                }
                res->queue_left += (uint32_t)held;
                continue;
            }
            if (run.ctl_got[a][b] != c->ctl_msgs) {
                fail("control %d -> %d: %u of %u delivered", a, b, (unsigned)run.ctl_got[a][b], (unsigned)c->ctl_msgs);
                break;
            }
            if (TagPdp_QueueDepth(&run.ep[a].pdp, b) != 0) {
                fail("control %d -> %d: %d messages never acknowledged", a, b, TagPdp_QueueDepth(&run.ep[a].pdp, b));
                break;
            }
            for (i = 0; i < c->unrel_msgs && !run.failed; i++) {
                res->unrel_copies += run.sim.made[a][b][i];
                res->unrel_delivered += run.unrel_got[a][b][i];
                if (run.unrel_got[a][b][i] > run.sim.made[a][b][i])
                    fail("unreliable %d -> %d: message %u arrived more often than the link copied it", a, b, (unsigned)i);
                else if (c->loss_pct == 0 && run.unrel_got[a][b][i] == 0)
                    fail("unreliable %d -> %d: message %u lost on a lossless link", a, b, (unsigned)i);
            }
        }
    }

    for (e = 0; e < c->n; e++) {
        const TagPdpStats *st = TagPdp_Stats(&run.ep[e].pdp);
        int pi;
        res->resends += st->resends;
        res->dup_drops += st->dup_drops;
        res->session_drops += st->session_drops;
        if (st->bad_drops && !run.failed)
            fail("member %d dropped %u malformed packets", e, (unsigned)st->bad_drops);
        for (pi = 0; pi < TAGPDP_MAX_PEERS; pi++) {
            if (st->rtt_ms[pi] > res->rtt_ms)
                res->rtt_ms = st->rtt_ms[pi];
        }
    }
    for (a = 0; a < c->n && !run.failed; a++) {
        for (b = 0; b < c->n && !run.failed; b++) {
            if (a != b && c->active[a][b] && !same[a][b] && res->session_drops == 0)
                fail("no foreign-session packet was seen and dropped");
        }
    }
    return !run.failed;
}

/* ---- the cases ---- */

static void all_pairs(Case *c, int n, uint32_t session) {
    int a, b;
    c->n = n;
    for (a = 0; a < n; a++) {
        c->session[a] = session;
        for (b = 0; b < n; b++)
            c->active[a][b] = a != b;
    }
}

static void lossy(Case *c, int loss_pct) {
    c->loss_pct = loss_pct;
    c->dup_pct = 5;
    c->base_ms = 10;
    c->jitter_ms = 30;
    c->ctl_msgs = CTL_MAX;
    c->unrel_msgs = UNREL_MAX;
}

int main(void) {
    Case cases[8];
    const char *names[8] = {
        "2 members, loss 0%", "2 members, loss 10%", "2 members, loss 30%",
        "3 members, loss 0%", "3 members, loss 10%", "3 members, loss 30%",
        "foreign session ignored", "2 members, 70000 msgs (seq wraps), loss 2%"
    };
    int nc = 0, i, failures = 0;

    memset(cases, 0, sizeof cases);
    all_pairs(&cases[0], 2, 0x51A7E000u); lossy(&cases[0], 0);
    all_pairs(&cases[1], 2, 0x51A7E000u); lossy(&cases[1], 10);
    all_pairs(&cases[2], 2, 0x51A7E000u); lossy(&cases[2], 30);
    all_pairs(&cases[3], 3, 0x51A7E000u); lossy(&cases[3], 0);
    all_pairs(&cases[4], 3, 0x51A7E000u); lossy(&cases[4], 10);
    all_pairs(&cases[5], 3, 0x51A7E000u); lossy(&cases[5], 30);

    /* member 2 is in another session: 0 and 1 talk; 2 sends to both and must reach neither */
    all_pairs(&cases[6], 3, 0x51A7E000u);
    cases[6].session[2] = 0x0BADBEEFu;
    cases[6].active[0][2] = 0;
    cases[6].active[1][2] = 0;
    lossy(&cases[6], 10);
    cases[6].ctl_msgs = 300;
    cases[6].unrel_msgs = 300;

    /* more than 65536 messages: the 16-bit seq wraps (the ack and window math must hold across it) */
    all_pairs(&cases[7], 2, 0x51A7E000u); lossy(&cases[7], 2);
    cases[7].ctl_msgs = 70000;
    cases[7].unrel_msgs = 0;
    nc = 8;

    for (i = 0; i < nc; i++) {
        Result res;
        int ok = run_case(&cases[i], i + 1, &res);
        double sim_s = (double)(run.sim.now - 1000) / 1000.0;
        if (ok) {
            printf("PASS %s: control %u msgs per pair in order (%u pairs), resends %u, dup drops %u, rtt %u ms; "
                   "unreliable %u delivered of %u copies, none more than copied; link lost %u, dup %u, reordered %u; "
                   "queue left %u, foreign-session drops %u; sim %.1f s\n",
                   names[i], (unsigned)cases[i].ctl_msgs, (unsigned)res.pairs, (unsigned)res.resends,
                   (unsigned)res.dup_drops, (unsigned)res.rtt_ms, (unsigned)res.unrel_delivered,
                   (unsigned)res.unrel_copies, (unsigned)run.sim.lost, (unsigned)run.sim.duped,
                   (unsigned)run.sim.reordered, (unsigned)res.queue_left, (unsigned)res.session_drops, sim_s);
        } else {
            failures++;
            printf("FAIL %s: %s (resends %u, dup drops %u, sim %.1f s)\n", names[i], run.why,
                   (unsigned)res.resends, (unsigned)res.dup_drops, sim_s);
        }
    }
    printf("%d of %d cases passed\n", nc - failures, nc);
    return failures ? 1 : 0;
}
