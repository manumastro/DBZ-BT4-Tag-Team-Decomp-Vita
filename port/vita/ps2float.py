#!/usr/bin/env python3
"""The PS2's float arithmetic on the Vita (bt3.txt ps2float, the build variant BT4_PS2FLOAT=1, docs/vita/online-bt4.md).

The PC build compiles every game source with software float: the compiler emits no float instruction and calls
port/src/softfloat_ps2.c for each operation (the PS2's model: the adder's cut of the smaller operand, results truncated,
division rounded to nearest, ...). The Vita's game code runs on the float unit (round toward zero, flush to zero), which
gives the same result for multiplications, conversions to int and comparisons, but not for every addition or subtraction,
division, float <-> unsigned conversion and double operation. Software float for the whole game is not an option on the
Vita (-mfloat-abi=soft changes the calling convention at every boundary with the hard-float code: the vector library,
the C library, the port's own files).

So the closest thing that builds for ARM: the game's objects are compiled to assembly as usual (hard float, the same
register allocation), and here each float instruction whose result can differ from the PS2's is replaced by a branch to
a piece of code at the end of its section, which calls the model with the operands' bits and puts the result back in the
destination register. The replacement is exactly as long as the instruction (4 bytes, `b.w`), so nothing the compiler
laid out moves (literal pools, branch ranges, jump tables). The pieces save and restore every register they use; the
model's entry points (port/vita/vita_ps2float_a.S) keep the flags and every float register. Which model runs is chosen
at run time (gVitaPs2Float, bt3.txt ps2float=0/1): 0 does the float unit's own operation, 1 the PS2's.

Replaced (single precision):  vadd vsub vdiv vmla vmls vnmla vnmls (the product by the float unit, the sum by the model),
                               vcvt.u32.f32, vcvt.f32.u32;  with mul=True also vmul and vnmul.
Replaced (double precision):   every arithmetic or conversion with a .f64 operand (vadd vsub vmul vdiv vmla vmls vnmla
                               vnmls vnmul vsqrt vcvt.f64.f32 vcvt.f32.f64 ...): run with the float unit set to round to
                               nearest without flushing denormals, as the PC's doubles (host IEEE arithmetic, plat_libm.c).
Left alone:                    vmul (truncating on both), vcvt to and from signed integers, vneg, vabs, vcmp, moves,
                               loads and stores; vsqrt.f32 (not the model's: see docs/vita/online-bt4.md).

The gate (bt3.txt pffast, gVitaPfGate in vita_ps2float.c): each piece begins with a look at one word, 0 when pffast=1
and ps2float=0 (offline play): then it does the replaced instruction itself and goes back (push r0, two loads, cbnz,
pop, the instruction, the branch back: some 8 cycles), without the call, the eight registers' save and the float unit
<-> core moves of the model's entry point (some 30-50 cycles: in a Tag 2 vs 2 fight about 60,000 replaced operations a
blank made the game thread ~8 ms a step slower, 29 -> 24 pictures a second). Any other word: the piece as before.
None of push, ldr, cbnz and pop changes the flags, and no replaced instruction reads sp.

The objects are compiled with -mrestrict-it, so that no float instruction sits in an IT block (a 32-bit instruction
is not allowed there under that rule); one that does stops the build.

    ps2float.py in.s out.s [f32|f64] [mul]      (a test: rewrite one file)
"""
import re
import sys

COND = {"eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"}
INSN = re.compile(r"^\s+(v[a-z]+)((?:\.[a-z0-9]+)+)\s+([^@]*?)\s*(@.*)?$")
IT = re.compile(r"^\s+(it[te]{0,3})\s+(\w+)\s*(@.*)?$")
REG = re.compile(r"^[sd]\d+$")
F32_BIN = {"vadd": "add", "vsub": "sub", "vdiv": "div"}
F32_MAC = {"vmla", "vmls", "vnmla", "vnmls"}
F64_OPS = {"vadd", "vsub", "vmul", "vdiv", "vmla", "vmls", "vnmla", "vnmls", "vnmul", "vsqrt", "vcvt"}
# (left alone: vabs / vneg, exact in any mode; vcvt between doubles and integers, which truncates in any mode and is
# exact the other way)
F64_CVT = {".f64.f32", ".f32.f64"}


class Error(Exception):
    pass


def regs(ops):
    r = [x.strip() for x in ops.split(",")]
    for x in r:
        if not REG.match(x):
            raise Error(f"unexpected operand {x!r}")
    return r


def thunk_f32(n, mn, typ, ops, mul):
    """The piece of code for a single-precision instruction (None: not replaced)."""
    body = []
    if mn in F32_BIN and typ == ".f32":
        d, a, b = regs(ops)
        body = [f"vmov\tr0, {a}", f"vmov\tr1, {b}", f"bl\tPortPF_{F32_BIN[mn]}", f"vmov\t{d}, r0"]
    elif mn in F32_MAC and typ == ".f32":
        d, a, b = regs(ops)
        # d = +-d +- a * b: d's value into r0, the product into r1 (by the float unit into d itself, or by the model:
        # its entry points keep every float register, so d is read after that call), then the model's sum
        body = ([f"vmov\tr0, {a}", f"vmov\tr1, {b}", "bl\tPortPF_mul", "mov\tr1, r0", f"vmov\tr0, {d}"]
                if mul else [f"vmov\tr0, {d}", f"vmul.f32\t{d}, {a}, {b}", f"vmov\tr1, {d}"])
        if mn == "vmla":      # d + p
            body += ["bl\tPortPF_add"]
        elif mn == "vmls":    # d - p
            body += ["bl\tPortPF_sub"]
        elif mn == "vnmla":   # -d - p
            body += ["eor\tr0, r0, #0x80000000", "bl\tPortPF_sub"]
        else:                 # vnmls: -d + p
            body += ["eor\tr0, r0, #0x80000000", "bl\tPortPF_add"]
        body += [f"vmov\t{d}, r0"]
    elif mn == "vcvt" and typ in (".u32.f32", ".f32.u32"):
        d, a = regs(ops)
        body = [f"vmov\tr0, {a}", "bl\tPortPF_f2u" if typ == ".u32.f32" else "bl\tPortPF_u2f", f"vmov\t{d}, r0"]
    elif mul and mn in ("vmul", "vnmul") and typ == ".f32":
        d, a, b = regs(ops)
        body = [f"vmov\tr0, {a}", f"vmov\tr1, {b}", "bl\tPortPF_mul"] + (["eor\tr0, r0, #0x80000000"] if mn == "vnmul" else []) + [f"vmov\t{d}, r0"]
    if not body:
        return None
    # r0-r5, ip and lr: what the model's entry points and the C model may change (r4, r5 are the entry points' own)
    return ["push\t{r0, r1, r2, r3, r4, r5, ip, lr}"] + body + ["pop\t{r0, r1, r2, r3, r4, r5, ip, lr}", f"b.w\t.Lpf{n}r"]


def thunk_f64(n, mn, typ, ops, orig):
    if mn not in F64_OPS or ".f64" not in typ or (mn == "vcvt" and typ not in F64_CVT):
        return None
    # the float unit to round to nearest, no flush to zero (FPSCR bits 22-24 cleared: gVitaPfFpscrClear, 0 with ps2float=0)
    return ["push\t{r0, r1}", "vmrs\tr0, fpscr", f"ldr\tr1, .Lpf{n}k", "ldr\tr1, [r1]", "bic\tr1, r0, r1", "vmsr\tfpscr, r1",
            orig.split("@")[0].strip(), "vmsr\tfpscr, r0", "pop\t{r0, r1}", f"b.w\t.Lpf{n}r", ".p2align 2", f".Lpf{n}k:", ".word\tgVitaPfFpscrClear"]


def rewrite(text, f32=True, mul=False, tag="pf"):
    """Returns (new text, {kind: count})."""
    out, thunks, counts = [], {}, {}
    section = ".text"
    stack = []
    it_left = 0
    in_asm = False
    n = 0
    for line in text.splitlines():
        s = line.strip()
        # inline assembly the source wrote itself (gcc marks it "@ <line> <file> 1" ... "@ 0 "" 2"): left as written
        if s.startswith("@ ") and s.endswith(" 1") and '"' in s:
            in_asm = True
        elif s.startswith('@ 0 "" 2'):
            in_asm = False
        if s.startswith(".section"):
            section = s
        elif s in (".text", ".data", ".bss") or s.startswith((".text\t", ".data\t", ".bss\t")):
            section = s
        elif s.startswith(".pushsection"):
            stack.append(section)
            section = ".section" + s[len(".pushsection"):]
        elif s.startswith(".popsection"):
            section = stack.pop() if stack else ".text"
        elif s.startswith(".previous"):
            raise Error(".previous is not followed")
        m = IT.match(line)
        if m and not in_asm:
            it_left = len(m.group(1)) - 1
            out.append(line)
            continue
        m = INSN.match(line)
        if m and not in_asm:
            mn, typ, ops = m.group(1), m.group(2), m.group(3)
            base = mn
            if mn[-2:] in COND and mn[:-2] in F32_BIN.keys() | F32_MAC | F64_OPS | {"vmul", "vnmul"}:
                base = mn[:-2]
            code = None
            if f32:
                code = thunk_f32(n, base, typ, ops, mul)
            if code is None:
                code = thunk_f64(n, base, typ, ops, line)
            if code is not None:
                # the gate (see the top): the instruction itself when gVitaPfGate is 0, the piece otherwise
                code = (["push\t{r0}", f"ldr\tr0, .Lpf{n}g", "ldr\tr0, [r0]", f"cbnz\tr0, .Lpf{n}s", "pop\t{r0}",
                         line.split("@")[0].strip(), f"b.w\t.Lpf{n}r", f".Lpf{n}s:", "pop\t{r0}"] + code +
                        [".p2align 2", f".Lpf{n}g:", ".word\tgVitaPfGate"])
                if base != mn or it_left > 0:
                    raise Error(f"a replaced instruction in an IT block or conditional: {line.strip()} (build with -mrestrict-it)")
                if "text" not in section and '"ax"' not in section:
                    raise Error(f"float instruction outside code: {section}")
                out.append(f"\tb.w\t.Lpf{n}")
                out.append(f".Lpf{n}r:")
                thunks.setdefault(section, []).append([f".Lpf{n}:"] + ["\t" + c if not c.startswith(".L") else c for c in code])
                key = base + typ
                counts[key] = counts.get(key, 0) + 1
                n += 1
                continue
        if it_left > 0 and s and not s.startswith((".", "@")) and not s.endswith(":"):
            it_left -= 1
        out.append(line)
    if thunks:
        out.append("@ ps2float.py: the replaced instructions' code")
        for sec, items in thunks.items():
            out.append("\t" + sec)
            out.append("\t.syntax unified")
            out.append("\t.thumb")
            out.append("\t.p2align 1")
            for t in items:
                out.extend(t)
    return "\n".join(out) + "\n", counts


def main():
    src, dst = sys.argv[1], sys.argv[2]
    mode = sys.argv[3] if len(sys.argv) > 3 else "f32"
    text, counts = rewrite(open(src, encoding="latin-1").read(), f32=mode == "f32", mul="mul" in sys.argv[4:])
    open(dst, "w", encoding="latin-1").write(text)
    print(" ".join(f"{k}:{v}" for k, v in sorted(counts.items())))


if __name__ == "__main__":
    main()
