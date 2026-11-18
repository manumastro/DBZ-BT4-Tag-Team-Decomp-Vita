#!/usr/bin/env python3
"""The PS2 float model's variant (port/vita/ps2float.py) checked without a console, in an ARM emulator (Unicorn).

    port/vita/tools/ps2float_emu.py         (in WSL / Linux, from the repository root; Docker image of the Vita build;
                                             Python with the unicorn package: python3 -m venv ~/psvita/.venv-unicorn &&
                                             ~/psvita/.venv-unicorn/bin/pip install unicorn)

What it does:
 1. builds, with the Vita's compiler (Docker image bt3-vita-build), a bare program from vita_pftest.c (twice: as game
    code with its instructions replaced, and as the float unit's check), vita_ps2float.c (-mgeneral-regs-only),
    vita_ps2float_a.S and the snippets below (each replaced instruction once, and its original next to it);
 2. runs, with the game thread's float mode (round toward zero, flush to zero, default NaN), with ps2float 0 and 1:
    - the start-up check Vita_PfSelfTest (the replaced instructions against the float unit and the model): 0 differ;
    - each snippet from a state with every register, the flags and the float unit's status set to chosen values: after
      the replaced instruction everything must be as before but its destination; with ps2float=0 the destination must be
      what the original instruction gives.
Exit status 0 when all of it holds.
"""
import os
import pathlib
import random
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "port/vita"))
import ps2float  # noqa: E402

OUT = ROOT / "port/build/pf_emu"
IMAGE = "bt3-vita-build:c9bcd74"
P = "/usr/local/vitasdk/bin/arm-vita-eabi-"
BASE = 0x10000

SNIPPETS = [  # (name, instruction): destination s5 / d5
    ("add", "vadd.f32 s5, s6, s7"), ("sub", "vsub.f32 s5, s6, s7"), ("div", "vdiv.f32 s5, s6, s7"),
    ("mla", "vmla.f32 s5, s6, s7"), ("mls", "vmls.f32 s5, s6, s7"), ("nmla", "vnmla.f32 s5, s6, s7"), ("nmls", "vnmls.f32 s5, s6, s7"),
    ("mla_same", "vmla.f32 s5, s5, s7"), ("f2u", "vcvt.u32.f32 s5, s6"), ("u2f", "vcvt.f32.u32 s5, s6"),
    ("dadd", "vadd.f64 d5, d6, d7"), ("dmla", "vmla.f64 d5, d6, d7"), ("d2f", "vcvt.f32.f64 s5, d7"), ("f2d", "vcvt.f64.f32 d5, s7"),
]

START = r"""
#include <stdint.h>
extern void PortPF_Init(int on);
extern int Vita_PfSelfTest(void (*log)(const char *fmt, ...));
int pf_run(int mode) { PortPF_Init(mode); return Vita_PfSelfTest(0); }
void *memcpy(void *d, const void *s, unsigned n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, unsigned n) { char *a = d; while (n--) *a++ = (char)c; return d; }

/* the entry points' short ways (ps2float=1) against the C model, on n operand pairs of every kind */
extern uint32_t pf_call(void *fn, uint32_t a, uint32_t b); /* (shim.s: keeps r4, r5, which the entry points use) */
extern char PortPF_add[], PortPF_sub[], PortPF_div[];
extern uint32_t PortPFc_add(uint32_t, uint32_t), PortPFc_sub(uint32_t, uint32_t), PortPFc_div(uint32_t, uint32_t);
static uint32_t lcg = 12345;
static uint32_t rnd(void) { lcg = lcg * 1664525u + 1013904223u; return lcg ^ (lcg >> 15); }
static uint32_t opnd(void) {
    static const uint32_t sp[16] = {0, 0x80000000u, 0x3F800000u, 0xBF800000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u, 0x7FC00000u,
                                    0x00000001u, 0x807FFFFFu, 0x00800000u, 0x00FFFFFFu, 0x4F000000u, 0x7F000000u, 0x01000001u, 0x3F7FFFFFu};
    uint32_t r = rnd(), s = rnd();
    switch (r & 7) {
    case 0: return sp[s & 15];
    case 1: return s;                                                        /* anything */
    case 2: return (s & 0x807FFFFFu) | ((1u + (r >> 8) % 30u) << 23);        /* tiny */
    case 3: return (s & 0x807FFFFFu) | ((225u + (r >> 8) % 30u) << 23);      /* huge */
    default: return (s & 0x807FFFFFu) | ((110u + (r >> 8) % 36u) << 23);     /* exponents close together */
    }
}
int pf_entries(int n) {
    int bad = 0, i;
    PortPF_Init(1);
    for (i = 0; i < n; i++) {
        uint32_t a = opnd(), b = opnd();
        bad += pf_call(PortPF_add, a, b) != PortPFc_add(a, b);
        bad += pf_call(PortPF_sub, a, b) != PortPFc_sub(a, b);
        bad += pf_call(PortPF_div, a, b) != PortPFc_div(a, b);
    }
    return bad;
}
"""

SHIM = """
	.syntax unified
	.thumb
	.text
	.global pf_call
	.type pf_call, %function
	.thumb_func
pf_call:
	push	{r4, r5, r6, lr}
	mov	ip, r0
	mov	r0, r1
	mov	r1, r2
	blx	ip
	pop	{r4, r5, r6, pc}
"""


def sh(cmd):
    # (umask 000: what the container writes stays writable for this user)
    r = subprocess.run(["docker", "run", "--rm", "-v", f"{ROOT}:/src", "-w", "/src", IMAGE, "sh", "-c", "umask 000 && " + cmd],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"failed: {cmd}\n{r.stdout}{r.stderr}")
    return r.stdout


def build():
    o = "port/build/pf_emu"
    if not OUT.is_dir():  # (port/build belongs to the build container: the folder is made there, open to this user)
        sh(f"mkdir -p {o} && chmod 777 {o}")
    flags = "-O2 -fno-strict-aliasing -ffp-contract=off -fsigned-char -fno-short-enums -Iport/src -Iport/vita -Iport/include"
    for kind in ("pf", "ref"):  # each instruction replaced (snip_pf_*) and as it is (snip_ref_*)
        snip = ["\t.syntax unified", "\t.thumb", "\t.text"]
        for name, insn in SNIPPETS:
            snip += [f"\t.global snip_{kind}_{name}", f"\t.type snip_{kind}_{name}, %function", "\t.thumb_func", "\t.p2align 2",
                     f"snip_{kind}_{name}:", "\t" + insn.replace(" ", "\t", 1), "\tnop", "\tnop"]
        text = "\n".join(snip) + "\n"
        if kind == "pf":
            text, _ = ps2float.rewrite(text)
        (OUT / f"snip_{kind}.s").write_text(text)
    (OUT / "start.c").write_text(START)
    (OUT / "shim.s").write_text(SHIM)
    cmds = [
        f"{P}gcc {flags} -S -mrestrict-it port/vita/vita_pftest.c -o {o}/pftest.s",
        f"{P}gcc {flags} -DPF_CHECK=1 -c port/vita/vita_pftest.c -o {o}/pfcheck.o",
        f"{P}gcc {flags} -DPF_NO_ENV -mgeneral-regs-only -c port/vita/vita_ps2float.c -o {o}/model.o",
        f"{P}gcc -c port/vita/vita_ps2float_a.S -o {o}/entry.o",
        f"{P}gcc {flags} -c {o}/start.c -o {o}/start.o",
        f"{P}gcc -c {o}/shim.s -o {o}/shim.o",
    ]
    sh(" && ".join(cmds))
    s = OUT / "pftest.s"
    text, counts = ps2float.rewrite(s.read_text())
    s.write_text(text)
    print("vita_pftest.c as game code:", " ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    sh(" && ".join([f"{P}gcc -c {o}/pftest.s -o {o}/pftest.o", f"{P}gcc -c {o}/snip_pf.s -o {o}/snip_pf.o", f"{P}gcc -c {o}/snip_ref.s -o {o}/snip_ref.o",
                    f"{P}gcc -nostdlib -nostartfiles -static -Wl,-Ttext={BASE:#x} -Wl,-e,pf_run -o {o}/emu.elf "
                    f"{o}/start.o {o}/pftest.o {o}/pfcheck.o {o}/model.o {o}/entry.o {o}/snip_pf.o {o}/snip_ref.o {o}/shim.o -lgcc",
                    f"{P}objcopy -O binary {o}/emu.elf {o}/emu.bin"]))
    syms = {}
    for l in sh(f"{P}nm {o}/emu.elf").splitlines():
        p = l.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    return (OUT / "emu.bin").read_bytes(), syms


def main():
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UcError
    from unicorn import arm_const as A
    image, syms = build()
    mem_size = 0x400000
    stack_top = BASE + mem_size - 0x1000
    stop = BASE + mem_size - 0x100

    def fresh():
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        try:
            from unicorn import UC_CPU_ARM_CORTEX_A9
            uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
        except Exception:
            pass
        uc.mem_map(BASE, mem_size)
        uc.mem_write(BASE, image)
        uc.mem_write(stop, b"\x00\xbf" * 8)  # nops: where the calls return to
        uc.reg_write(A.UC_ARM_REG_C1_C0_2, uc.reg_read(A.UC_ARM_REG_C1_C0_2) | (0xF << 20))
        uc.reg_write(A.UC_ARM_REG_FPEXC, 0x40000000)
        uc.reg_write(A.UC_ARM_REG_FPSCR, (3 << 22) | (1 << 24) | (1 << 25))  # the game thread's mode (vita_float.c)
        uc.reg_write(A.UC_ARM_REG_SP, stack_top)
        return uc

    bad = 0
    for mode in (0, 1, 3):
        uc = fresh()
        uc.reg_write(A.UC_ARM_REG_R0, mode)
        uc.reg_write(A.UC_ARM_REG_LR, stop | 1)
        try:
            uc.emu_start(syms["pf_run"] | 1, stop, count=200_000_000)
        except UcError as e:
            print(f"ps2float={mode}: the check stopped: {e} at {uc.reg_read(A.UC_ARM_REG_PC):#x}")
            bad += 1
            continue
        n = uc.reg_read(A.UC_ARM_REG_R0)
        print(f"ps2float={mode}: Vita_PfSelfTest: {n} differ")
        bad += n != 0

    uc = fresh()  # the short ways of ps2float=1 against the C model
    n_pairs = 60000
    uc.reg_write(A.UC_ARM_REG_R0, n_pairs)
    uc.reg_write(A.UC_ARM_REG_LR, stop | 1)
    uc.emu_start(syms["pf_entries"] | 1, stop, count=2_000_000_000)
    n = uc.reg_read(A.UC_ARM_REG_R0)
    print(f"ps2float=1: the add, subtract and divide entry points against the C model: {n_pairs} operand pairs (special,"
          f" tiny, huge, close exponents, any): {n} differ")
    bad += n != 0

    regs_core = [getattr(A, f"UC_ARM_REG_R{i}") for i in range(13)] + [A.UC_ARM_REG_LR, A.UC_ARM_REG_SP]
    regs_d = [getattr(A, f"UC_ARM_REG_D{i}") for i in range(32)]
    rnd = random.Random(7)
    for mode in (0, 1, 3):
        for name, insn in SNIPPETS:
            for trial in range(40):
                uc = fresh()
                gaddr = syms["gVitaPs2Float"]
                uc.mem_write(gaddr, struct.pack("<I", mode))
                uc.mem_write(syms["gVitaPfFpscrClear"], struct.pack("<I", (7 << 22) if mode else 0))
                state = {}
                for r in regs_core[:13] + [A.UC_ARM_REG_LR]:
                    v = rnd.getrandbits(32)
                    uc.reg_write(r, v)
                for i, r in enumerate(regs_d):
                    hi = (rnd.getrandbits(1) << 31) | ((120 + rnd.randrange(16)) << 23) | rnd.getrandbits(23)
                    lo = (rnd.getrandbits(1) << 31) | ((120 + rnd.randrange(16)) << 23) | rnd.getrandbits(23)
                    if name in ("u2f",) and i == 3:
                        lo = rnd.getrandbits(32)  # s6: an unsigned integer
                    if name.startswith("d") or name == "f2d":
                        if i in (5, 6, 7) and name != "f2d":
                            hi = (rnd.getrandbits(1) << 31) | ((1020 + rnd.randrange(8)) << 20) | rnd.getrandbits(20)
                            lo = rnd.getrandbits(32)
                    uc.reg_write(r, (hi << 32) | lo)
                flags = rnd.choice([0, 0x80000000, 0x40000000, 0x20000000, 0x10000000, 0xF0000000, 0x08000000])
                uc.reg_write(A.UC_ARM_REG_CPSR, (uc.reg_read(A.UC_ARM_REG_CPSR) & 0x07FFFFFF) | flags)
                fpscr = (3 << 22) | (1 << 24) | (1 << 25) | (rnd.choice([0, 0x80000000, 0x60000000]))
                uc.reg_write(A.UC_ARM_REG_FPSCR, fpscr)
                before = {r: uc.reg_read(r) for r in regs_core + regs_d}
                cpsr0 = uc.reg_read(A.UC_ARM_REG_CPSR)
                double = name in ("dadd", "dmla", "f2d", "d2f")
                # what the destination must hold: ps2float=0 the original instruction's result; 1 the model's (the C
                # model's entry points called in the emulator), for doubles the original's in round to nearest, no flush
                uc2 = fresh()
                for r, v in before.items():
                    uc2.reg_write(r, v)
                uc2.reg_write(A.UC_ARM_REG_CPSR, cpsr0)
                uc2.reg_write(A.UC_ARM_REG_FPSCR, fpscr & ~(7 << 22) if mode != 0 and double else fpscr)
                a = syms[f"snip_ref_{name}"]
                uc2.emu_start(a | 1, a + 4)
                ref = {r: uc2.reg_read(r) for r in regs_d}
                if mode != 0 and not double:
                    def call(fn, x, y=0):
                        u3 = fresh()
                        u3.reg_write(A.UC_ARM_REG_R0, x)
                        u3.reg_write(A.UC_ARM_REG_R1, y)
                        u3.reg_write(A.UC_ARM_REG_LR, stop | 1)
                        u3.emu_start(syms["PortPFc_" + fn] | 1, stop)
                        return u3.reg_read(A.UC_ARM_REG_R0)
                    s5, s6, s7 = before[A.UC_ARM_REG_D2] >> 32, before[A.UC_ARM_REG_D3] & 0xFFFFFFFF, before[A.UC_ARM_REG_D3] >> 32
                    S = 0x80000000
                    p = call("mul", s5 if name == "mla_same" else s6, s7)
                    want = {"add": lambda: call("add", s6, s7), "sub": lambda: call("sub", s6, s7), "div": lambda: call("div", s6, s7),
                            "mla": lambda: call("add", s5, p), "mls": lambda: call("sub", s5, p), "nmla": lambda: call("sub", s5 ^ S, p),
                            "nmls": lambda: call("add", s5 ^ S, p), "mla_same": lambda: call("add", s5, p),
                            "f2u": lambda: call("f2u", s6), "u2f": lambda: call("u2f", s6)}[name]()
                    ref[A.UC_ARM_REG_D2] = (want << 32) | (before[A.UC_ARM_REG_D2] & 0xFFFFFFFF)
                a = syms[f"snip_pf_{name}"]
                try:
                    uc.emu_start(a | 1, a + 4, count=2_000_000)
                except UcError as e:
                    print(f"ps2float={mode} {name}: stopped: {e} at {uc.reg_read(A.UC_ARM_REG_PC):#x}")
                    bad += 1
                    break
                after = {r: uc.reg_read(r) for r in regs_core + regs_d}
                dest = A.UC_ARM_REG_D2 if name not in ("dadd", "dmla", "f2d") else A.UC_ARM_REG_D5  # s5 is the top half of d2
                problems = []
                for r in regs_core + regs_d:
                    if r == dest:
                        x, y = before[r], after[r]
                        if name not in ("dadd", "dmla", "f2d"):  # s4 (the low half of d2) must be kept
                            if (x & 0xFFFFFFFF) != (y & 0xFFFFFFFF):
                                problems.append("s4 changed")
                        continue
                    if before[r] != after[r]:
                        problems.append(f"register {r} {before[r]:#x} -> {after[r]:#x}")
                if (uc.reg_read(A.UC_ARM_REG_CPSR) & 0xF80F0000) != (cpsr0 & 0xF80F0000):
                    problems.append(f"flags {cpsr0:#x} -> {uc.reg_read(A.UC_ARM_REG_CPSR):#x}")
                if (uc.reg_read(A.UC_ARM_REG_FPSCR) ^ fpscr) & ~0x9F:  # (bits 0-4, 7: the operations' sticky exception flags)
                    problems.append(f"FPSCR {fpscr:#x} -> {uc.reg_read(A.UC_ARM_REG_FPSCR):#x}")
                if after[dest] != ref[dest]:
                    problems.append(f"result {after[dest]:#x}, expected {ref[dest]:#x}")
                if problems:
                    bad += 1
                    print(f"ps2float={mode} {insn}: " + "; ".join(problems[:4]))
                    break
        print(f"ps2float={mode}: {len(SNIPPETS)} replaced instructions run 40 times each from random states"
              f" (registers, flags and status kept, the results the expected ones)")
    print("ps2float_emu: " + ("OK" if bad == 0 else f"{bad} problems"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
