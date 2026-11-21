/*
 * The PS2's float arithmetic for the game's code on the Vita: the model's C half (bt3.txt ps2float; port/vita/ps2float.py
 * says how the game's instructions reach it, docs/vita/online-bt4.md why).
 *
 * The same arithmetic as the PC build's (port/src/softfloat_ps2.c, the PCSX2 model by default): the add and multiply
 * cores are that file's own inline functions (softfloat_ps2_inl.h); division, float -> unsigned and unsigned -> float are
 * written out here as they are there (softfloat_ps2.c's divide(), __fixunssfsi, __floatunsisf and the vector library's
 * compare and conversion they use), because that file's objects are built for the float unit. build.py checks this file
 * against softfloat_ps2.c on the build machine, on a few million operands, and stops the build at the first difference.
 *
 * Built with -mgeneral-regs-only: this code never touches a float register or the float unit's status, which the game's
 * code around the replaced instruction may hold (the entry points in vita_ps2float_a.S rely on it). Its only call out is
 * getenv, at start (PortPF_Init), for the PC's own experiment switches (BT3_FPU_NEAREST ...: unset, the default).
 */
#include <stdint.h>
#include <stdlib.h>
#ifdef PF_NO_ENV /* (the host check and the emulator test: no environment) */
#define getenv(name) ((char *)0)
#endif
#include "softfloat_ps2_inl.h"

int gVitaPs2Float;            /* bt3.txt ps2float: 0 the float unit's own results (as without the variant), 1 the PS2's
                                 (vita_ps2float_a.S: the short ways where they give the model's bits), 3 the C model always */
unsigned gVitaPfFpscrClear;   /* the float unit's status bits cleared around a double operation: 0, or (ps2float=1) the
                                 rounding mode and flush to zero (bits 22-24): round to nearest with denormals, as the
                                 PC's doubles (host IEEE arithmetic, port/src/plat_libm.c) */
int gVitaPfFast;              /* bt3.txt pffast: 1 the pieces of code do the replaced instruction themselves while
                                 ps2float is 0 (ps2float.py's gate: no call of the entry points, offline play at the
                                 default build's speed); 0 every operation through the entry points as before */
int gVitaPfGate = 1;          /* what the pieces look at: 0 their own instruction, anything else the entry point */
unsigned gVitaPfCount[8];     /* operations done, by entry point: add sub mul div f2u u2f (the report's ps2float line) */

/* RefVu0_LtBits (src/port/vu0_a.c): a < b as sign and magnitude (+0 == -0) */
static int lt_bits(uint32_t a, uint32_t b) {
    int32_t x = (a & SIGN) ? -(int32_t)(a & 0x7FFFFFFF) : (int32_t)(a & 0x7FFFFFFF);
    int32_t y = (b & SIGN) ? -(int32_t)(b & 0x7FFFFFFF) : (int32_t)(b & 0x7FFFFFFF);

    return x < y;
}

/* RefVu0_ItofBits(v, 0) (src/port/vu0_a.c, the integer model): (float)v, truncated */
static uint32_t itof_bits(int32_t v) {
    uint32_t sign = v < 0 ? SIGN : 0;
    uint32_t u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    int32_t e = 31;

    if (u == 0) {
        return 0;
    }
    while (!(u & 0x80000000u)) {
        u <<= 1;
        e--;
    }
    return sign | ((uint32_t)(e + 127) << 23) | ((u >> 8) & 0x7FFFFF);
}

/* softfloat_ps2.c __fixsfsi: cvt.w.s, truncating, out of range the largest / smallest integer */
static int32_t fix_bits(uint32_t a) {
    int32_t e = (int32_t)((a >> 23) & 0xFF) - 127;
    uint32_t m = (a & 0x7FFFFF) | 0x800000;
    uint32_t v;

    if (e < 0) {
        return 0;
    }
    if (e >= 31) {
        return (a & SIGN) ? (int32_t)0x80000000u : 0x7FFFFFFF;
    }
    v = e >= 23 ? m << (e - 23) : m >> (23 - e);
    return (a & SIGN) ? -(int32_t)v : (int32_t)v;
}

/* softfloat_ps2.c divide(): a / b, nearest = 1 the FPU's div.s under PCSX2 (round to nearest even) */
static uint32_t divide(uint32_t a, uint32_t b, int nearest) {
    uint32_t sign = (a ^ b) & SIGN;
    uint64_t n, q, rem;
    uint32_t m;
    int32_t e;

    a = in(a);
    b = in(b);
    if (EXP(b) == 0) {
        return sign | FMAX; /* x / 0 and 0 / 0 */
    }
    if (EXP(a) == 0) {
        return sign;
    }
    n = (uint64_t)MANT(a) << 26;
    q = n / MANT(b);
    rem = n % MANT(b);
    e = (int32_t)EXP(a) - (int32_t)EXP(b) + 127;
    if (q >= ((uint64_t)1 << 26)) {
        rem |= q & 1;
        q >>= 1;
    } else {
        e--;
    }
    rem |= q & 1;
    q >>= 1;
    m = (uint32_t)(q >> 1);
    if (nearest && (q & 1) && (rem != 0 || (m & 1))) {
        m++;
        if (m == 0x1000000) {
            m >>= 1;
            e++;
        }
    }
    return pack(sign, e, m);
}

/* The model, on bit patterns (softfloat_ps2.c: __addsf3, __subsf3, __mulsf3, __divsf3, __fixunssfsi, __floatunsisf). */
uint32_t PortPFc_add(uint32_t a, uint32_t b) { return fpu_add(a, b); }
uint32_t PortPFc_sub(uint32_t a, uint32_t b) { return fpu_add(a, b ^ SIGN); }
uint32_t PortPFc_mul(uint32_t a, uint32_t b) { return mul_core(a, b, mode(1)); }
uint32_t PortPFc_div(uint32_t a, uint32_t b) { return divide(a, b, 1); }
uint32_t PortPFc_f2u(uint32_t a, uint32_t b) {
    (void)b;
    if (!lt_bits(a, 0x4F000000u)) { /* 2^31 and above: through (a - 2^31), the vector unit's subtract, and the top bit */
        return (uint32_t)fix_bits(Sf_AddBits(a, 0x4F000000u ^ SIGN)) | SIGN;
    }
    return (uint32_t)fix_bits(a);
}
uint32_t PortPFc_u2f(uint32_t v, uint32_t b) {
    uint32_t f = itof_bits((int32_t)v);

    (void)b;
    return (v & SIGN) ? Sf_AddBits(f, 0x4F800000u) : f;
}

/* bt3.txt ps2float=<0|1> (vita_main.c). Also reads the PC's experiment switches once, here, so that the model's calls
   never reach the C library while the game runs. */
void PortPF_Init(int on) {
    int k, any = 0;

    for (k = 0; k < 4; k++) {
        any |= mode(k);
    }
    /* 0 the float unit's own results; 1 the model's, the short ways where they give its bits; 3 the C model always (a
       check of the short ways, and what the PC's experiment switches need: the short ways know only the default) */
    gVitaPs2Float = on == 0 ? 0 : on == 3 || any ? 3 : 1;
    gVitaPfFpscrClear = on ? (7u << 22) : 0u;
    gVitaPfGate = gVitaPfFast && gVitaPs2Float == 0 ? 0 : 1; /* (an online session's begin and end come here too) */
}
