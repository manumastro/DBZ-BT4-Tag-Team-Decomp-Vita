@ The PS2's float arithmetic for the game's code on the Vita: the model's entry points (bt3.txt ps2float;
@ port/vita/ps2float.py writes the calls, vita_ps2float.c is the model).
@
@ PortPF_<op>: r0, r1 = the operands' bits; the result's bits in r0. Called only from ps2float.py's pieces of code,
@ which keep r0-r5, ip and lr around the call. What the game's code may hold in anything else must survive: the
@ flags (APSR, kept here in r4), every float register (d0, the only one used here, is kept on the stack; the C model
@ is built with -mgeneral-regs-only) and the float unit's status (put back after a division in round to nearest).
@
@ gVitaPs2Float 0: the float unit's own operation (the same result as the instruction that was replaced: this measures
@ what the detour costs); 1: the model's results, the add, subtract and divide by the float unit where that gives the
@ model's bits (below), the C model otherwise; 3: the C model always (a check of the short ways).
@
@ The short ways (they rely on the game thread's float mode, round toward zero, flush to zero, default NaN: vita_main.c
@ game_thread, Vita_FloatMode):
@   add, subtract: the PS2 adder's cut done on the bits (softfloat_ps2_inl.h fpu_add: the smaller operand's d - 1 low
@     bits cleared, d the exponents' difference; 25 or more: only its sign), then the float unit's add, which truncates
@     the exact sum as the model does (zeros, denormals as zero, overflow to the largest number all agree). An operand
@     with exponent 255 (an infinity or NaN to the float unit, a number to the PS2): the C model.
@   divide: the float unit's division at round to nearest (the PC's div.s rounds to nearest even). The C model for a
@     divisor that is zero or denormal, an operand with exponent 255, and a result that overflowed (exponent 255) or
@     is below twice the smallest normal number (exponent 0 or 1: where the float unit's flush and the model's
@     rounding could differ).
@ port/vita/tools/ps2float_emu.py checks the short ways against the C model on random operands.

	.syntax unified
	.thumb
	.text

@ the counter of operation idx, then: gVitaPs2Float in r2 and the flags kept in r4
#define PF_HEAD(name, idx)                                          \
	.global PortPF_##name;                                      \
	.type PortPF_##name, %function;                             \
	.thumb_func;                                                \
	.p2align 2;                                                 \
PortPF_##name:                                                      \
	ldr	r2, .Lfast2;                                        \
	ldr	r2, [r2];                                           \
	cbnz	r2, 1f;                                             \
	ldr	r2, .Lcount;                                        \
	ldr	r3, [r2, (idx) * 4];                                \
	add	r3, r3, #1;                                         \
	str	r3, [r2, (idx) * 4];                                \
1:	ldr	r2, .Lmode;                                         \
	ldr	r2, [r2];                                           \
	mrs	r4, APSR

@ the C model: r0 = PortPFc_<name>(r0, r1)
#define PF_SLOW(name)                                               \
	mov	r5, lr;                                             \
	bl	PortPFc_##name;                                     \
	msr	APSR_nzcvqg, r4;                                    \
	bx	r5

@ the float unit's own operation on s0, s1 (d0 kept)
#define PF_HW(...)                                                  \
	vpush	{d0};                                               \
	vmov	s0, r0;                                             \
	vmov	s1, r1;                                             \
	__VA_ARGS__;                                                \
	vmov	r0, s0;                                             \
	vpop	{d0};                                               \
	msr	APSR_nzcvqg, r4;                                    \
	bx	lr

@ ---- add and subtract: the cut, then the float unit's add (a subtract is an add of -b)
	.type pf_add_cut, %function
	.thumb_func
	.p2align 2
pf_add_cut:                         @ r0 + r1 with the cut; r4 the flags; returns to the entry's caller
	ubfx	r2, r0, #23, #8
	ubfx	r3, r1, #23, #8
	cmp	r2, #255
	beq	9f
	cmp	r3, #255
	beq	9f
	subs	r2, r2, r3          @ d = exp(a) - exp(b)
	beq	4f
	bgt	6f
	rsbs	r2, r2, #0          @ d < 0: the cut on a
	cmp	r2, #25
	bge	7f
	sub	r2, r2, #1
	mvn	r3, #0
	lsl	r3, r3, r2
	and	r0, r0, r3
	b	4f
7:	and	r0, r0, #0x80000000
	b	4f
6:	cmp	r2, #25             @ d > 0: the cut on b
	bge	8f
	sub	r2, r2, #1
	mvn	r3, #0
	lsl	r3, r3, r2
	and	r1, r1, r3
	b	4f
8:	and	r1, r1, #0x80000000
4:	PF_HW(vadd.f32 s0, s0, s1)
9:	PF_SLOW(add)                @ (the C model's add of a and the b given: -b for a subtract)
	.size pf_add_cut, . - pf_add_cut

PF_HEAD(add, 0)
	cbnz	r2, 2f
	PF_HW(vadd.f32 s0, s0, s1)
2:	cmp	r2, #1
	beq	pf_add_cut
	PF_SLOW(add)
	.size PortPF_add, . - PortPF_add

PF_HEAD(sub, 1)
	cbnz	r2, 2f
	PF_HW(vsub.f32 s0, s0, s1)
2:	cmp	r2, #1
	itt	eq
	eoreq	r1, r1, #0x80000000
	beq	pf_add_cut
	PF_SLOW(sub)
	.size PortPF_sub, . - PortPF_sub

@ ---- divide: the float unit at round to nearest
PF_HEAD(div, 3)
	cbnz	r2, 2f
	PF_HW(vdiv.f32 s0, s0, s1)
2:	cmp	r2, #1
	bne	9f
	ubfx	r2, r0, #23, #8
	ubfx	r3, r1, #23, #8
	cmp	r2, #255
	beq	9f
	cmp	r3, #255
	beq	9f
	cmp	r3, #0
	beq	9f
	vpush	{d0}
	vmrs	r2, fpscr
	bic	r3, r2, #0x00C00000     @ round to nearest (flush to zero and default NaN kept)
	vmsr	fpscr, r3
	vmov	s0, r0
	vmov	s1, r1
	vdiv.f32	s0, s0, s1
	vmsr	fpscr, r2
	vmov	r3, s0
	vpop	{d0}
	ubfx	r2, r3, #23, #8
	cmp	r2, #255
	beq	9f
	cmp	r2, #1
	bls	9f
	mov	r0, r3
	msr	APSR_nzcvqg, r4
	bx	lr
9:	PF_SLOW(div)
	.size PortPF_div, . - PortPF_div

@ ---- multiply (only in the BT4_PS2FLOAT=mul build), float -> unsigned, unsigned -> float: the C model
PF_HEAD(mul, 2)
	cbnz	r2, 2f
	PF_HW(vmul.f32 s0, s0, s1)
2:	PF_SLOW(mul)
	.size PortPF_mul, . - PortPF_mul

PF_HEAD(f2u, 4)
	cbnz	r2, 2f
	PF_HW(vcvt.u32.f32 s0, s0)
2:	PF_SLOW(f2u)
	.size PortPF_f2u, . - PortPF_f2u

PF_HEAD(u2f, 5)
	cbnz	r2, 2f
	PF_HW(vcvt.f32.u32 s0, s0)
2:	PF_SLOW(u2f)
	.size PortPF_u2f, . - PortPF_u2f

	.p2align 2
.Lcount:
	.word	gVitaPfCount
.Lmode:
	.word	gVitaPs2Float
.Lfast2:
	.word	gVitaPfFast2
