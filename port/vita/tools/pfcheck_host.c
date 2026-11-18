/* The Vita's PS2 float model (port/vita/vita_ps2float.c) against the PC's (port/src/softfloat_ps2.c), both compiled for
   the build machine: every entry point on a few million operands (random ones with exponents close together, where the
   adder's cut acts, random ones over the whole range, and the special patterns). Run by port/vita/build.py in the
   BT4_PS2FLOAT variant; exit status 1 at the first difference. */
#include <stdint.h>
#include <stdio.h>

extern uint32_t PortX_addsf3(uint32_t, uint32_t), PortX_subsf3(uint32_t, uint32_t), PortX_mulsf3(uint32_t, uint32_t),
    PortX_divsf3(uint32_t, uint32_t), PortX_fixunssfsi(uint32_t), PortX_floatunsisf(uint32_t);
extern uint32_t PortPFc_add(uint32_t, uint32_t), PortPFc_sub(uint32_t, uint32_t), PortPFc_mul(uint32_t, uint32_t),
    PortPFc_div(uint32_t, uint32_t), PortPFc_f2u(uint32_t, uint32_t), PortPFc_u2f(uint32_t, uint32_t);

void Port_SeamExact(uint32_t x, uint32_t y, uint32_t z, uint32_t fx, uint32_t fy) { (void)x; (void)y; (void)z; (void)fx; (void)fy; } /* (vu0_a.c: the renderer's) */

static uint64_t s = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return (uint32_t)(s >> 16);
}

static uint32_t operand(int kind) {
    static const uint32_t special[] = {0, 0x80000000u, 0x3F800000u, 0xBF800000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u, 0x7FC00000u,
                                       0x00000001u, 0x007FFFFFu, 0x00800000u, 0x4F000000u, 0x4F7FFFFFu, 0x4F800000u, 0xCF000000u, 0x4EFFFFFFu};
    uint32_t r = rnd();
    switch (kind) {
    case 0: return special[r % 16];
    case 1: return r;                                                        /* anything */
    default: return (r & 0x807FFFFFu) | ((uint32_t)(120 + (rnd() % 16)) << 23); /* exponents close together */
    }
}

int main(void) {
    long n, bad = 0;
    const long total = 4000000;

    for (n = 0; n < total && bad < 10; n++) {
        int ka = n % 7 == 0 ? 0 : n % 3 ? 2 : 1, kb = n % 11 == 0 ? 0 : n % 5 ? 2 : 1;
        uint32_t a = operand(ka), b = operand(kb), u = rnd();
        uint32_t got[6], want[6];
        int k;
        got[0] = PortPFc_add(a, b); want[0] = PortX_addsf3(a, b);
        got[1] = PortPFc_sub(a, b); want[1] = PortX_subsf3(a, b);
        got[2] = PortPFc_mul(a, b); want[2] = PortX_mulsf3(a, b);
        got[3] = PortPFc_div(a, b); want[3] = PortX_divsf3(a, b);
        got[4] = PortPFc_f2u(a, 0); want[4] = PortX_fixunssfsi(a);
        got[5] = PortPFc_u2f(u, 0); want[5] = PortX_floatunsisf(u);
        for (k = 0; k < 6; k++) {
            if (got[k] != want[k]) {
                printf("ps2float: model check: op %d a=%08x b=%08x u=%08x: %08x, the PC's %08x\n", k, a, b, u, got[k], want[k]);
                bad++;
            }
        }
    }
    printf("ps2float: model check against port/src/softfloat_ps2.c: %ld operand pairs, 6 operations each, %ld differ\n", n, bad);
    return bad != 0;
}
