/*
 * PS Vita: TagPdp, the transport under the online Tag Team. The packet, the channels and the limits are in
 * vita_tagpdp.h (psvita-agent notes/online/vita-multiplayer-plan.md, section 7.5).
 *
 * No Vita API is called here: the ops given to TagPdp_Init are the only way in and out. So the host test
 * (port/vita/tools/tagpdp_test.c) runs this same code over a simulated lossy link.
 */
#include <string.h>
#include "vita_tagpdp.h"

#define TAGPDP_MAGIC 0x5450u
#define TAGPDP_CH_ACK 2
#define TAGPDP_MASK (TAGPDP_WINDOW - 1)   /* 64 divides 65536, so seq & MASK stays right across the wrap */

static void put16(uint8_t *b, uint16_t v) {
    b[0] = (uint8_t)(v & 0xff);
    b[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *b, uint32_t v) {
    put16(b, (uint16_t)(v & 0xffff));
    put16(b + 2, (uint16_t)(v >> 16));
}

static uint16_t get16(const uint8_t *b) {
    return (uint16_t)(b[0] | (b[1] << 8));
}

static uint32_t get32(const uint8_t *b) {
    return (uint32_t)get16(b) | ((uint32_t)get16(b + 2) << 16);
}

/* signed distance between two sequence numbers (mod 2^16): positive when a comes after b */
static int seq_diff(uint16_t a, uint16_t b) {
    int d = (int)(uint16_t)(a - b);
    return d >= 0x8000 ? d - 0x10000 : d;
}

/* control messages sent and not yet acknowledged */
static int queued(const TagPdpPeer *q) {
    return (uint16_t)(q->snd_next - q->snd_una);
}

/* bit i: control seq (rx_hi - 1 - i) has been received (everything before rx_next has) */
static uint32_t ack_bits(const TagPdpPeer *q) {
    uint32_t bits = 0;
    int i;
    for (i = 0; i < 32; i++) {
        uint16_t t = (uint16_t)(q->rx_hi - 1 - i);
        int e = seq_diff(t, q->rx_next);
        if (e < 0 || (e < TAGPDP_WINDOW && q->rx_have[t & TAGPDP_MASK]))
            bits |= (uint32_t)1 << i;
    }
    return bits;
}

/* builds a packet for one peer (with the ack fields as they are now) and hands it to ops->send */
static int put_packet(TagPdp *p, int pi, uint8_t channel, uint16_t seq, const uint8_t *payload, int len) {
    TagPdpPeer *q = &p->peer[pi];
    uint8_t buf[TAGPDP_MAX_PACKET];
    int n = TAGPDP_HEADER + len;
    int rc;

    put16(buf, TAGPDP_MAGIC);
    put32(buf + 2, p->session);
    buf[6] = (uint8_t)p->self;
    buf[7] = channel;
    put16(buf + 8, seq);
    put16(buf + 10, q->rx_hi);
    put32(buf + 12, ack_bits(q));
    put16(buf + 16, (uint16_t)(q->rx_next - 1));
    put16(buf + 18, (uint16_t)len);
    if (len > 0)
        memcpy(buf + TAGPDP_HEADER, payload, (size_t)len);
    q->ack_pending = 0;   /* this packet carries the ack */

    rc = p->ops->send(p->ctx, pi, buf, n);
    if (rc < 0) {
        p->stats.send_errors++;
        return -1;
    }
    p->stats.packets_out++;
    return 0;
}

/* a control packet arrived from pi: an ack is due within TAGPDP_ACK_MS unless a packet to pi carries it first */
static void want_ack(TagPdp *p, int pi, uint64_t now) {
    TagPdpPeer *q = &p->peer[pi];
    if (!q->ack_pending) {
        q->ack_pending = 1;
        q->ack_due_ms = now + TAGPDP_ACK_MS;
    }
}

static void inbox_push(TagPdp *p, int pi, int channel, const uint8_t *data, int len) {
    TagPdpMsg *m = &p->inbox[(p->inbox_head + p->inbox_count) % TAGPDP_INBOX];
    m->peer = (uint8_t)pi;
    m->channel = (uint8_t)channel;
    m->len = (uint16_t)len;
    if (len > 0)
        memcpy(m->data, data, (size_t)len);
    p->inbox_count++;
}

/* moves the in-order control messages that were held in the reorder buffer into the inbox, as far as there is room */
static void drain_peer(TagPdp *p, int pi) {
    TagPdpPeer *q = &p->peer[pi];
    while (p->inbox_count < TAGPDP_INBOX && q->rx_have[q->rx_next & TAGPDP_MASK]) {
        int idx = q->rx_next & TAGPDP_MASK;
        inbox_push(p, pi, TAGPDP_CONTROL, q->rx_data[idx], q->rx_len[idx]);
        q->rx_have[idx] = 0;
        q->rx_next++;
    }
}

/* one control message of ours is known to be received; the round trip is measured only on a first send (Karn) */
static void slot_acked(TagPdp *p, int pi, uint16_t seq, uint64_t now) {
    TagPdpSlot *s = &p->peer[pi].snd[seq & TAGPDP_MASK];
    uint32_t sample;
    if (s->acked)
        return;
    s->acked = 1;
    if (s->sends != 1)
        return;
    sample = (uint32_t)(now - s->sent_ms);
    if (p->stats.rtt_ms[pi] == 0)
        p->stats.rtt_ms[pi] = sample;
    else
        p->stats.rtt_ms[pi] = (7 * p->stats.rtt_ms[pi] + sample) / 8;
}

/* the ack fields of a packet from pi: cum (all up to it arrived), ack (the highest) and its 32 bits before it */
static void process_acks(TagPdp *p, int pi, uint16_t cum, uint16_t ack, uint32_t bits, uint64_t now) {
    TagPdpPeer *q = &p->peer[pi];
    int count = queued(q);
    int d, i;

    d = seq_diff(cum, q->snd_una);
    if (d >= 0 && d < count) {
        for (i = 0; i <= d; i++)
            slot_acked(p, pi, (uint16_t)(q->snd_una + i), now);
    }
    d = seq_diff(ack, q->snd_una);
    if (d >= 0 && d < count)
        slot_acked(p, pi, ack, now);
    for (i = 0; i < 32; i++) {
        uint16_t t;
        if (!(bits & ((uint32_t)1 << i)))
            continue;
        t = (uint16_t)(ack - 1 - i);
        d = seq_diff(t, q->snd_una);
        if (d >= 0 && d < count)
            slot_acked(p, pi, t, now);
    }
    while (count > 0 && q->snd[q->snd_una & TAGPDP_MASK].acked) {
        q->snd_una++;
        count--;
    }
}

static void on_packet(TagPdp *p, const uint8_t *b, int n, uint64_t now) {
    int sender, channel, len, pi, d, idx;
    uint16_t seq, ack, cum;
    uint32_t bits;
    TagPdpPeer *q;

    if (n < TAGPDP_HEADER || get16(b) != TAGPDP_MAGIC) {
        p->stats.bad_drops++;
        return;
    }
    if (get32(b + 2) != p->session) {
        p->stats.session_drops++;
        return;
    }
    sender = b[6];
    channel = b[7];
    len = get16(b + 18);
    if (sender >= TAGPDP_MAX_PEERS || sender == p->self || channel > TAGPDP_CH_ACK ||
        len > TAGPDP_MAX_PAYLOAD || n != TAGPDP_HEADER + len || (channel == TAGPDP_CH_ACK && len != 0)) {
        p->stats.bad_drops++;
        return;
    }
    p->stats.packets_in++;
    pi = sender;
    q = &p->peer[pi];
    seq = get16(b + 8);
    ack = get16(b + 10);
    bits = get32(b + 12);
    cum = get16(b + 16);
    process_acks(p, pi, cum, ack, bits, now);

    if (channel == TAGPDP_CH_ACK)
        return;
    if (channel == TAGPDP_UNRELIABLE) {
        if (p->inbox_count >= TAGPDP_INBOX) {
            p->stats.inbox_drops++;
            return;
        }
        inbox_push(p, pi, TAGPDP_UNRELIABLE, b + TAGPDP_HEADER, len);
        return;
    }

    /* control */
    d = seq_diff(seq, q->rx_next);
    if (d >= TAGPDP_WINDOW) {
        p->stats.window_drops++;
        want_ack(p, pi, now);
        return;
    }
    if (d < 0 || q->rx_have[seq & TAGPDP_MASK]) {
        p->stats.dup_drops++;
        want_ack(p, pi, now);
        return;
    }
    if (d == 0) {
        /* not accepted (and not acknowledged) when the inbox is full: the sender resends it */
        if (p->inbox_count >= TAGPDP_INBOX) {
            p->stats.inbox_drops++;
            return;
        }
        inbox_push(p, pi, TAGPDP_CONTROL, b + TAGPDP_HEADER, len);
        q->rx_next++;
        drain_peer(p, pi);
    } else {
        idx = seq & TAGPDP_MASK;
        q->rx_have[idx] = 1;
        q->rx_len[idx] = (uint16_t)len;
        memcpy(q->rx_data[idx], b + TAGPDP_HEADER, (size_t)len);
    }
    if (seq_diff(seq, q->rx_hi) > 0)
        q->rx_hi = seq;
    want_ack(p, pi, now);
}

int TagPdp_Init(TagPdp *p, const TagPdpOps *ops, void *ctx, uint32_t session, int self) {
    int i;
    if (!p || !ops || !ops->send || !ops->recv || !ops->now_ms || self < 0 || self >= TAGPDP_MAX_PEERS)
        return -1;
    memset(p, 0, sizeof *p);
    p->ops = ops;
    p->ctx = ctx;
    p->session = session;
    p->self = self;
    for (i = 0; i < TAGPDP_MAX_PEERS; i++)
        p->peer[i].rx_hi = 0xffff;   /* nothing received yet: rx_next - 1 */
    return 0;
}

int TagPdp_Send(TagPdp *p, int peer, int channel, const void *data, int len) {
    TagPdpPeer *q;
    TagPdpSlot *s;
    uint64_t now;
    uint16_t seq;

    if (!p || peer < 0 || peer >= TAGPDP_MAX_PEERS || peer == p->self)
        return -1;
    if (len < 0 || len > TAGPDP_MAX_PAYLOAD || (len > 0 && !data))
        return -1;
    if (channel == TAGPDP_UNRELIABLE)
        return put_packet(p, peer, TAGPDP_UNRELIABLE, 0, (const uint8_t *)data, len) == 0 ? 0 : -1;
    if (channel != TAGPDP_CONTROL)
        return -1;

    q = &p->peer[peer];
    if (queued(q) >= TAGPDP_WINDOW)
        return -1;
    now = p->ops->now_ms(p->ctx);
    seq = q->snd_next;
    s = &q->snd[seq & TAGPDP_MASK];
    s->len = (uint16_t)len;
    if (len > 0)
        memcpy(s->data, data, (size_t)len);
    s->acked = 0;
    s->sends = 1;
    s->sent_ms = now;
    q->snd_next++;
    /* a failed first send is retried by TagPdp_Poll like any lost packet */
    put_packet(p, peer, TAGPDP_CONTROL, seq, s->data, s->len);
    return 0;
}

int TagPdp_Poll(TagPdp *p) {
    uint8_t buf[TAGPDP_RECV_SIZE];
    uint64_t now;
    int got = 0, pi, i;

    if (!p)
        return 0;
    now = p->ops->now_ms(p->ctx);
    for (i = 0; i < 256; i++) {
        int from = -1;
        int n = p->ops->recv(p->ctx, &from, buf, (int)sizeof buf);
        if (n <= 0)
            break;
        if (n > (int)sizeof buf) {
            p->stats.bad_drops++;
            continue;
        }
        on_packet(p, buf, n, now);
        got++;
    }

    for (pi = 0; pi < TAGPDP_MAX_PEERS; pi++) {
        TagPdpPeer *q;
        int count, k;
        if (pi == p->self)
            continue;
        q = &p->peer[pi];
        count = queued(q);
        for (k = 0; k < count; k++) {
            uint16_t seq = (uint16_t)(q->snd_una + k);
            TagPdpSlot *s = &q->snd[seq & TAGPDP_MASK];
            if (s->acked || now - s->sent_ms < TAGPDP_RESEND_MS)
                continue;
            put_packet(p, pi, TAGPDP_CONTROL, seq, s->data, s->len);
            if (s->sends < 255)
                s->sends++;
            s->sent_ms = now;
            p->stats.resends++;
        }
        if (q->ack_pending && now >= q->ack_due_ms) {
            put_packet(p, pi, TAGPDP_CH_ACK, 0, NULL, 0);
            p->stats.acks_sent++;
        }
        drain_peer(p, pi);
    }
    return got;
}

int TagPdp_Read(TagPdp *p, int *peer, int *channel, void *buf, int size) {
    TagPdpMsg *m;
    int n, pi;

    if (!p || p->inbox_count == 0)
        return -1;
    m = &p->inbox[p->inbox_head];
    if ((int)m->len > size)
        return -2;
    if (m->len > 0)
        memcpy(buf, m->data, m->len);
    if (peer)
        *peer = m->peer;
    if (channel)
        *channel = m->channel;
    n = m->len;
    p->inbox_head = (p->inbox_head + 1) % TAGPDP_INBOX;
    p->inbox_count--;
    for (pi = 0; pi < TAGPDP_MAX_PEERS; pi++) {
        if (pi != p->self)
            drain_peer(p, pi);
    }
    return n;
}

int TagPdp_QueueDepth(const TagPdp *p, int peer) {
    if (!p || peer < 0 || peer >= TAGPDP_MAX_PEERS || peer == p->self)
        return 0;
    return queued(&p->peer[peer]);
}

const TagPdpStats *TagPdp_Stats(const TagPdp *p) {
    return p ? &p->stats : NULL;
}
