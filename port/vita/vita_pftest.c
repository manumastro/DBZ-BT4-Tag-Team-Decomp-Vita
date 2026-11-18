/*
 * The check of the PS2 float model's build variant (BT4_PS2FLOAT=1, bt3.txt ps2float): the same operations compiled
 * twice from this file, once as game code (its float instructions replaced by port/vita/ps2float.py: Vita_PfOps) and
 * once as the Vita's own code (the float unit: Vita_PfOpsHw, with the check itself, -DPF_CHECK). At start the check
 * runs both on a few hundred operands and compares:
 *   ps2float=0  the replaced instructions must give the float unit's own results, bit for bit;
 *   ps2float=1  the single-precision results must be the model's (vita_ps2float.c PortPFc_*, the PC's arithmetic) and
 *               the double one the float unit's in round to nearest without flush to zero (the PC's doubles).
 * One line in the log: "ps2float: check ... N operations, M differ" (and the first differences). Also built for the
 * emulator test port/vita/tools/ps2float_emu.py, which runs the same check without a console.
 */
#include <stdint.h>

typedef union {
    float f;
    uint32_t u;
} PfBits;

#define PF_NOPS 11

#ifdef PF_CHECK
#define PF_OPS Vita_PfOpsHw
#else
#define PF_OPS Vita_PfOps
#endif

/* out[k * n + i] = operation k on a[i], b[i], c[i] */
void PF_OPS(const uint32_t *a, const uint32_t *b, const uint32_t *c, uint32_t *out, int n) {
    int i;

    for (i = 0; i < n; i++) {
        PfBits x, y, z, r;
        x.u = a[i];
        y.u = b[i];
        z.u = c[i];
        r.f = x.f + y.f; out[0 * n + i] = r.u;
        r.f = x.f - y.f; out[1 * n + i] = r.u;
        r.f = y.u & 0x7F800000u ? x.f / y.f : 0.0f; out[2 * n + i] = r.u; /* (the game's divisions never see a zero: __port_fdiv) */
        r.f = z.f + x.f * y.f; out[3 * n + i] = r.u;
        r.f = z.f - x.f * y.f; out[4 * n + i] = r.u;
        r.f = -z.f - x.f * y.f; out[5 * n + i] = r.u;
        r.f = x.f * y.f - z.f; out[6 * n + i] = r.u;
        r.u = (uint32_t)x.f; out[7 * n + i] = r.u;
        r.f = (float)a[i]; out[8 * n + i] = r.u;
        r.f = (float)((double)x.f * (double)y.f + (double)z.f); out[9 * n + i] = r.u;
        r.f = x.f * y.f; out[10 * n + i] = r.u;
    }
}

#ifdef PF_CHECK
extern int gVitaPs2Float;
extern void Vita_PfOps(const uint32_t *a, const uint32_t *b, const uint32_t *c, uint32_t *out, int n);
extern uint32_t PortPFc_add(uint32_t a, uint32_t b), PortPFc_sub(uint32_t a, uint32_t b), PortPFc_mul(uint32_t a, uint32_t b),
    PortPFc_div(uint32_t a, uint32_t b), PortPFc_f2u(uint32_t a, uint32_t b), PortPFc_u2f(uint32_t a, uint32_t b);

#define PF_N 192
static uint32_t sA[PF_N], sB[PF_N], sC[PF_N], sGot[PF_NOPS * PF_N], sHw[PF_NOPS * PF_N];

static uint32_t pf_rand(uint32_t *s) {
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

/* One operand: a sign, an exponent near 127 (the cut needs exponents a few apart), a random mantissa; some special
   ones (zeros, values that convert to large unsigned numbers, negative values for float -> unsigned). */
static uint32_t pf_operand(uint32_t *s, int i, int which) {
    uint32_t r = pf_rand(s), e;

    if (i < 8) {
        static const uint32_t special[8][3] = {
            {0x00000000u, 0x3F800000u, 0x80000000u}, {0x4F000000u, 0x3F800000u, 0x4F7FFFFFu}, {0xC0700000u, 0x40000000u, 0x3F800000u},
            {0x4EFFFFFFu, 0x4B000001u, 0x4B800001u}, {0x3F800001u, 0xB3800000u, 0x33FFFFFFu}, {0x7F7FFFFFu, 0x40000000u, 0xFF7FFFFFu},
            {0x00400000u, 0x3F800000u, 0x80000001u}, {0x80000000u, 0x80000000u, 0x00000000u}};
        return special[i][which];
    }
    e = 127 + (r >> 8) % 40 - 20;
    return (r & 0x80000000u) | (e << 23) | (pf_rand(s) & 0x7FFFFFu);
}

/* the double operation in the PC's mode: the float unit at round to nearest, no flush to zero, for the call */
static void pf_ops_nearest(uint32_t *out) {
    uint32_t old, mode;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(old));
    mode = old & ~(7u << 22);
    __asm__ volatile("vmsr fpscr, %0" : : "r"(mode));
    Vita_PfOpsHw(sA, sB, sC, out, PF_N);
    __asm__ volatile("vmsr fpscr, %0" : : "r"(old));
}

/* Returns the number of results that differ; log (may be NULL) gets the summary and the first differences. */
int Vita_PfSelfTest(void (*log)(const char *fmt, ...)) {
    static const char *const name[PF_NOPS] = {"a+b", "a-b", "a/b", "c+a*b", "c-a*b", "-c-a*b", "a*b-c", "(u32)a", "(float)u", "double", "a*b"};
    static uint32_t nearest[PF_NOPS * PF_N];
    uint32_t s = 0x2545F491u, oldmode, mode;
    int i, k, bad = 0, shown = 0;

    /* the game thread's float mode, which the short ways of vita_ps2float_a.S rely on (round toward zero, flush to zero,
       default NaN: vita_float.c Vita_FloatMode); this runs on the main thread at start, whose mode is put back after */
    __asm__ volatile("vmrs %0, fpscr" : "=r"(oldmode));
    mode = (oldmode & ~(7u << 22)) | (3u << 22) | (1u << 24) | (1u << 25);
    __asm__ volatile("vmsr fpscr, %0" : : "r"(mode));

    for (i = 0; i < PF_N; i++) {
        sA[i] = pf_operand(&s, i, 0);
        sB[i] = pf_operand(&s, i, 1);
        sC[i] = pf_operand(&s, i, 2);
    }
    Vita_PfOps(sA, sB, sC, sGot, PF_N);
    Vita_PfOpsHw(sA, sB, sC, sHw, PF_N);
    if (gVitaPs2Float) {
        pf_ops_nearest(nearest);
    }
    for (k = 0; k < PF_NOPS; k++) {
        for (i = 0; i < PF_N; i++) {
            uint32_t want = sHw[k * PF_N + i], a = sA[i], b = sB[i], c = sC[i], p;
            if (gVitaPs2Float) {
                p = PortPFc_mul(a, b);
                switch (k) {
                case 0: want = PortPFc_add(a, b); break;
                case 1: want = PortPFc_sub(a, b); break;
                case 2: want = b & 0x7F800000u ? PortPFc_div(a, b) : 0; break;
                case 3: want = PortPFc_add(c, p); break;
                case 4: want = PortPFc_sub(c, p); break;
                case 5: want = PortPFc_sub(c ^ 0x80000000u, p); break;
                case 6: want = PortPFc_sub(p, c); break;
                case 7: want = PortPFc_f2u(a, 0); break;
                case 8: want = PortPFc_u2f(a, 0); break;
                case 9: want = nearest[k * PF_N + i]; break;
                default: want = p; break; /* the multiply is left to the float unit: its result must be the model's */
                }
            }
            if (sGot[k * PF_N + i] != want) {
                bad++;
                if (log != 0 && shown++ < 6) {
                    log("ps2float: check: %s a=%08x b=%08x c=%08x: %08x, expected %08x (float unit %08x)\n", name[k], a, b, c,
                        sGot[k * PF_N + i], want, sHw[k * PF_N + i]);
                }
            }
        }
    }
    __asm__ volatile("vmsr fpscr, %0" : : "r"(oldmode));
    if (log != 0) {
        log("ps2float: check (ps2float=%d): %d operations, %d differ%s\n", gVitaPs2Float, PF_NOPS * PF_N, bad,
            bad ? "" : gVitaPs2Float ? " (the PS2's results)" : " (the float unit's own results)");
    }
    return bad;
}
#endif
