#!/usr/bin/env python3
"""PS Vita build of Tenkaichi3Decomp.

    python3 port/vita/build.py [compile] [link] [vpk]       (no step named: all three)

Environment:
    VITASDK            the VitaSDK installation (default /usr/local/vitasdk), with vitaGL built and installed from
                       port/vita/vitagl (build_vitagl.sh)
    VITA_SAMPLES       a checkout of https://github.com/vitasdk/samples (default: next to VITASDK)
    BT3_LIBCLANG       libclang's shared library, when it is not found in the usual places
    BT3_CLANG_PY       the folder that holds clang/cindex.py (libclang's Python bindings), when not installed
    BT3_VITA_DEV=1     the development build: its own title (BTTD00008); it reads every setting of bt3.txt (test
                       fights, statistics in the log), see docs/vita/development.md
    BT3_INCREMENTAL=1  reuse every object newer than its source, the headers its last compile read (<object>.d) and build.py
                       (the film behind the Tag Team screens is always packaged: tbo/ui/tag_ui_{intro,loop}.mp4, 1.3 MB; played with
                       bt3.txt tbomovie=1 (vita_tagmovie.c, the hardware player), else the poster)
    BT4_VITA_TAGMOVIE=1  also the PC's own films, tbo/ui/tag_ui_*.pss (30 MB more), for the software path, bt3.txt
                       tbomovie=2 (vita_tagmovie_sw.c). Default: not packaged
    BT3_VITA_TITLE_ID, BT3_VITA_TITLE, BT3_VITA_VPK
    BT3_JOBS=<n>       compiles at once (default: the processors; fewer where memory is short, a Docker VM)
                       another title ID, name and package file, so that the build is installed beside another one
                       (it reads the same ux0:data/bt3: the disc, the saves, bt3.txt)
    BT3_PGO=gen        a build that counts how the program runs (port/vita/vita_pgo.c: ux0:data/bt3/pgo/); objects
                       and package in their own folders (*_pgogen)
    BT4_VITA_ONLINE=1  the network part (port/vita/vita_net.c: SceNet, curl + OpenSSL, BT4decomp's signaling client, the
                       self-test; bt3.txt online, online_test, docs/vita/online-bt4.md). Default: 1 in the development
                       build, 0 in the release (whose program then has none of it)
    python3 port/vita/build.py netcheck   BT4decomp's network files compiled for the Vita, not linked: what builds
    BT4_VITA_NETLINK=1 BT4decomp's 1 vs 1 network files linked (online M2a; with the network part): net.c, net_lobby.c,
                       net_match.c, net_relay.c, net_chat.c, build_identity.c, plat_fprint.c, match_supabase.cpp, with
                       port/vita/vita_netglue.c; vita_bt4.c's stand-ins renamed away (vita_netlink_rename.h); *_nl folders
    BT4_PS2FLOAT=1     the PS2 float model's variant (online play, docs/vita/online-bt4.md): the game's float instructions whose
                       result can differ from the PC's are replaced by calls of the model (port/vita/ps2float.py), chosen at
                       run time by bt3.txt ps2float=0/1; objects and package in their own folders (*_pf); =mul also the
                       multiplies (*_pfm), a check of the "the float unit's multiply is the PS2's" assumption
    BT3_PGO=use        a build laid out by those counts: port/vita/pgo/<rel or dev>/*.gcda (port/vita/pgo_merge.sh),
                       copied next to the objects (*_pgouse); a file without counts is built as usual

The game's code is compiled as in the PC build's 32-bit variant: the same sources, the PS2 compiler's float
constants (eeconst.py) and call arguments evaluated right to left (ptr32.r2l). The game's built-in data tables come
from port/data as shape only; their values are read from the player's own disc when the program starts.
Objects go to port/build/obj_vita_<variant>, the package to port/build/vita_<variant>/."""
import concurrent.futures, os, pathlib, re, shutil, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/tools"))
sys.path.insert(0, str(ROOT / "port/vita"))
import portsrc, eeconst

SDK = pathlib.Path(os.environ.get("VITASDK", "/usr/local/vitasdk"))
P = str(SDK / "bin/arm-vita-eabi-")
DEV = os.environ.get("BT3_VITA_DEV") == "1"
PGO = os.environ.get("BT3_PGO", "")
ONLINE = os.environ.get("BT4_VITA_ONLINE", "1" if DEV else "0") == "1"
# BT4_VITA_NETLINK=1: BT4decomp's 1 vs 1 network files linked for real (online M2a; needs the network part)
NETLINK = ONLINE and os.environ.get("BT4_VITA_NETLINK") == "1"
if PGO not in ("", "gen", "use"):
    raise SystemExit("BT3_PGO: gen or use")
# BT4_PS2FLOAT: the PS2 float model's variant (port/vita/ps2float.py)
PS2F = os.environ.get("BT4_PS2FLOAT", "")
if PS2F not in ("", "0", "1", "mul"):
    raise SystemExit("BT4_PS2FLOAT: 1 or mul")
PS2F = "" if PS2F == "0" else PS2F
PF_TAG = {"1": "_pf", "mul": "_pfm"}.get(PS2F, "")
TAG = ("_dev" if DEV else "_rel") + ("_pgo" + PGO if PGO else "") + PF_TAG + ("_nl" if NETLINK else "")
PGO_DIR = ROOT / "port/vita/pgo" / ("dev" if DEV else "rel")
OBJ = ROOT / ("port/build/obj_vita" + TAG)
OUT = ROOT / ("port/build/vita" + TAG)
GEN = ROOT / ("port/build/gen/vita" + TAG)
SCE_SYS = ROOT / "port/vita/sce_sys"
SAMPLES = pathlib.Path(os.environ.get("VITA_SAMPLES", SDK.parent / "samples"))  # debugScreen.c (CC0): the start-up text screen
# BT4decomp's Vita build: the Budokai Tenkaichi 4 mod (B14 Rev2), its own title and folder (ux0:data/bt4), so that it
# sits beside the BT3 port and never touches its files or saves
TITLE_ID = os.environ.get("BT3_VITA_TITLE_ID") or ("BTFV00008" if DEV else "BTFV00010")
TITLE = os.environ.get("BT3_VITA_TITLE") or ("BT4 (development)" if DEV else "Dragon Ball Z: BT4")
VPK = os.environ.get("BT3_VITA_VPK") or ("BT4-Vita-dev.vpk" if DEV else "BT4-Vita.vpk").replace(".vpk", ("-pgo" + PGO if PGO else "") + PF_TAG.replace("_", "-") + ".vpk")
APP_VER = "01.41"  # the version the LiveArea and VitaShell show (README "Changes")

# A few names exist both in the game's side and in the SDK's libraries with other meanings: the internals of the
# PS2's maths library (port/third_party/newlib_libm), the PS2 C library's character table, and the generic soft-float
# entry points of softfloat_ps2.c. Their definitions are renamed in those objects (objcopy --redefine-syms).
X_DEFS = ["addsf3", "subsf3", "mulsf3", "divsf3", "negsf2", "eqsf2", "nesf2", "ltsf2", "lesf2", "gtsf2", "gesf2", "unordsf2",
          "fixsfsi", "fixunssfsi", "floatsisf", "floatunsisf"]
RENAME = {f"__{n}": f"PortX_{n}" for n in X_DEFS}
RENAME |= {n: "PortNL_" + n.lstrip("_") for n in ["__ieee754_acosf", "__ieee754_asinf", "__ieee754_atan2f", "__ieee754_powf",
           "__ieee754_rem_pio2f", "__ieee754_sqrtf", "__kernel_cosf", "__kernel_rem_pio2f", "__kernel_sinf", "__kernel_tanf"]}
RENAME |= {"_ctype_": "PortPS2_ctype_"}
RENAME_SOFTFLOAT = {"RefVu0_AddBits": "PortX_VuAdd", "RefVu0_MulBits": "PortX_VuMul", "RefVu0_DivBits": "PortX_VuDiv"}

# What the PS2 compiler and 32-bit x86 gcc agree on and the ARM EABI does not: plain char is signed, enums are ints.
ABI = ["-fsigned-char", "-fno-short-enums", "-fsigned-bitfields"]
COMMON = ["-O2", "-g", "-fno-strict-aliasing", "-ffp-contract=off", "-w", "-DPORT_VITA=1", "-DPORT_VITA_FIXED=1",
          '-DPORT_VITA_SFX="-vgl.txt"' if DEV else '-DPORT_VITA_SFX=".txt"', "-DPORT_VITA_GFX=1", "-DPORT_VITA_VGL=1",
          "-DMPEG2_PLAYER=1", "-DPORT_VITA_SOUND=1", "-DPORT_VITA_NATIVE=1",
          "-DPORT_VITA_BT4=1", "-DBT4_TAG_TEAM_EXPANDED=1", '-DPORT_VITA_HOME="ux0:data/bt4"', '-DPORT_VITA_ELF="SLUS_219.78"'] + ([] if DEV else ["-DPORT_VITA_RELEASE=1"]) + \
         (["-DBT4_VITA_ONLINE=1"] if ONLINE else []) + (["-DBT4_VITA_NETLINK=1"] if NETLINK else [])
# Profile-guided builds (BT3_PGO): gen counts with atomic updates (several threads run the same code); use takes the
# counts where there are some (a profile from an older source of a file is used as far as it fits, with a warning)
# counts only (VitaSDK's libgcov has no value profilers, no atomic updates and no file writing): arcs, with the
# objects' records in the section gcov_info, which vita_pgo.c walks and writes out itself
PGO_KIND = ["-fno-profile-values", "-fno-profile-reorder-functions"]
# objects built without their counts: vita_clib.c's code laid out by them does not assemble (a Thumb-2 literal load
# whose offset the assembler cannot represent once its blocks are moved)
PGO_SKIP = {"pc_vita_clib.gcda"}
if PGO == "gen":
    COMMON += ["-fprofile-generate", "-fprofile-update=single", "-fprofile-info-section=gcov_info", "-DBT3_PGO_GEN=1"] + PGO_KIND
elif PGO == "use":
    COMMON += ["-fprofile-use", "-fprofile-partial-training", "-fprofile-correction", "-Wno-missing-profile", "-Wno-coverage-mismatch"] + PGO_KIND
if PS2F:
    COMMON += ["-DBT4_PS2FLOAT_BUILD=1"]
# As in the PC build, no call of the C library is replaced by the compiler's own idea of it; the memory functions are
# allowed (small memcpy / memset calls become inline code).
NOBUILTIN = ["-fno-builtin-" + n for n in "sinf cosf tanf asinf acosf atanf atan2f sqrtf powf floorf ceilf fabsf fmodf expf logf sin cos tan sqrt "
             "pow floor ceil fabs abs labs rand srand printf sprintf snprintf fprintf vsprintf puts putchar fputs fwrite strcpy strncpy strcat "
             "strncat strlen strcmp strncmp strchr strrchr strstr malloc calloc realloc free exit abort atoi memcmp memmove".split()]
# (gnu99 with the C89 meaning of `inline`: BT4decomp's game files declare variables in their for loops, which clang
# takes in C89 as an extension and gcc does not; the decompilation's own files mean the same either way. -fpermissive:
# a library function called without its declaration stays a warning, as C89 had it)
GAME = [P + "gcc", "-std=gnu99", "-fgnu89-inline", "-fpermissive", "-c", "-fcommon"] + COMMON + ABI + NOBUILTIN + \
       ["-include", "port_libm.h", "-Iinclude", "-Iport/include", "-Iport/vita", "-include", "port_compat.h"]

# The game's compiler evaluates call arguments from right to left and the game's code depends on it in places; gcc
# for ARM goes left to right. The preprocessed text is rewritten as in the 64-bit PC build (ptr32.r2l, libclang).
R2L_ARGS = ["-x", "cpp-output", "--target=arm-none-eabi", "-mfloat-abi=soft", "-std=gnu99", "-Wno-error=implicit-function-declaration", "-Wno-error=implicit-int", "-fsigned-char", "-fno-short-enums", "-w",
            "-Wno-error=return-type", "-Wno-error=return-mismatch"]


def r2l(text):
    """The game's preprocessed text with call arguments evaluated right to left (ptr32.r2l), the PS2's rule for shift
    counts and its float division by zero written out (port/vita/srcfix.py). Returns (text, shifts rewritten)."""
    if os.environ.get("BT3_CLANG_PY"):
        sys.path.insert(0, os.environ["BT3_CLANG_PY"])
    import clang.cindex as ci
    if not ci.Config.loaded:
        lib = os.environ.get("BT3_LIBCLANG") or next((str(f) for d in ("/usr/lib/x86_64-linux-gnu", "/usr/lib/llvm-18/lib", "/usr/lib", "/usr/lib64")
                                                     for f in sorted(pathlib.Path(d).glob("libclang-*.so*")) + sorted(pathlib.Path(d).glob("libclang.so*"))), None)
        if lib:
            ci.Config.set_library_file(lib)
    import ptr32, srcfix
    data, n = srcfix.shifts(ptr32.r2l(text.encode("latin-1"), R2L_ARGS), R2L_ARGS)
    data, _ = srcfix.divs(data, R2L_ARGS)
    return data.decode("latin-1"), n


def deps(o):
    """gcc's dependency file next to object o (-MMD: the headers the compile read, system ones left out)."""
    return ["-MMD", "-MF", str(o.with_suffix(".d")), "-MT", str(o)]


def fresh(o, *srcs):
    """BT3_INCREMENTAL: object o can be reused when it is newer than its sources, every header its last compile read
    (o.d) and this file (the flags)."""
    d = o.with_suffix(".d")
    if not os.environ.get("BT3_INCREMENTAL") or not o.exists() or not d.exists():
        return False
    read = [w for w in d.read_text().split()[1:] if w != "\\"]  # "<object>: <source> <header> \ <header> ..."
    paths = [pathlib.Path(s) for s in srcs] + [pathlib.Path(__file__)] + [ROOT / p for p in read]
    t = o.stat().st_mtime
    try:
        return all(p.stat().st_mtime < t for p in paths)
    except FileNotFoundError:  # a header gone: compile again
        return False


def pf_policy(o):
    """BT4_PS2FLOAT: which float instructions of object o are replaced (port/vita/ps2float.py). "f32": the PS2's
    single-precision arithmetic and the PC's doubles (the game's code, the PS2's maths library, the port's game-side
    files; port_fast.c's stand-ins replace game functions, so they count as game code); "f64": the doubles only (the
    vector library's references and port_vufast.c, whose single-precision operations are the vector unit's, with no
    cut and truncating, which the float unit already does); None: left alone (the model itself)."""
    if not PS2F:
        return None
    n = o.name
    if n in ("pc_softfloat_ps2.o",):
        return None
    if n in ("pc_vu0_a.o", "pc_vu0_b.o", "pc_port_vufast.o"):
        return "f64"
    return "f32"


def pf_compile(cmd, o, policy):
    """cmd (a gcc command with -c and -o o) through assembly: compiled with -S (and -mrestrict-it: no float instruction
    in an IT block), the instructions replaced (ps2float.py), assembled. The counts go to <object>.pf."""
    import ps2float
    s = o.with_suffix(".s")
    c = [a for a in cmd if a != "-c"]
    c[c.index("-o") + 1] = str(s)
    r = subprocess.run(c + ["-S", "-mrestrict-it"], cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        return r
    try:
        text, counts = ps2float.rewrite(s.read_text(encoding="latin-1"), f32=policy == "f32", mul=PS2F == "mul")
    except ps2float.Error as e:
        return subprocess.CompletedProcess(cmd, 1, "", f"error: ps2float: {e}\n")
    s.write_text(text, encoding="latin-1")
    o.with_suffix(".pf").write_text("".join(f"{k} {v}\n" for k, v in sorted(counts.items())))
    inc = [f"-Wa,{a}" for a in cmd if a.startswith("-I")]  # (.include "labels.inc" of inline assembly: include/)
    return subprocess.run([P + "gcc", "-c"] + inc + [str(s), "-o", str(o)], cwd=ROOT, capture_output=True, text=True)


def compile_ee(cmd, src, o, game=False):
    """Preprocess, rewrite the float constants as the PS2 compiler read them (eeconst.py) and, for the game's
    sources, the calls whose arguments have side effects (r2l), compile."""
    r = subprocess.run([a for a in cmd if a != "-c"] + deps(o) + ["-E", str(src)], cwd=ROOT, capture_output=True, text=True, encoding="latin-1")
    if r.returncode:
        return r
    i = pathlib.Path(str(o)[:-2] + ".i")
    text = eeconst.transform(r.stdout)
    if game:
        try:
            text, nshift = r2l(text)
        except Exception as e:  # no libclang, or text it cannot parse
            return subprocess.CompletedProcess(cmd, 1, "", f"error: r2l: {e}\n")
    i.write_text(text, encoding="latin-1")
    policy = pf_policy(o)
    if policy:
        return rename(pf_compile(cmd + ["-fpreprocessed", str(i), "-o", str(o)], o, policy), o)
    return rename(subprocess.run(cmd + ["-fpreprocessed", str(i), "-o", str(o)], cwd=ROOT, capture_output=True, text=True), o)


def rename(r, o):
    """After a compile: the clashing definitions renamed (see RENAME)."""
    if r.returncode == 0:
        syms = RENAME | (RENAME_SOFTFLOAT if o.name == "pc_softfloat_ps2.o" else {})
        m = OBJ / "rename" / (o.name + ".txt")
        m.parent.mkdir(exist_ok=True)
        m.write_text("".join(f"{a} {b}\n" for a, b in syms.items()))
        r2 = subprocess.run([P + "objcopy", f"--redefine-syms={m}", str(o)], capture_output=True, text=True)
        if r2.returncode:
            return r2
    return r


def cc_game(f):
    o = OBJ / (str(f.relative_to(ROOT / "src")).replace("/", "_")[:-2] + ".o")
    src, done, missing = portsrc.prepare(f)
    if fresh(o, src, f):
        return o, f, ""
    r = compile_ee(GAME + [f"-I{f.parent}"], src, o, game=True)
    return (o if r.returncode == 0 else None), f, r.stderr


def first_error(e):
    return next((l for l in e.splitlines() if "rror" in l), e.strip()[:200])[:260]


def data_tables():
    """port/data/*.s assembled for ARM, each with a label at its first byte, and the list of where every run of
    values comes from on the player's disc as a C table (vita_dat.c)."""
    GEN.mkdir(parents=True, exist_ok=True)
    where, checksum = {}, None
    for l in (ROOT / "port/data/index.txt").read_text().splitlines():
        p = l.split()
        if len(p) == 3 and not l.startswith("#"):
            where[p[0]] = (int(p[1]), int(p[2], 16))
        elif len(p) == 2 and p[0] == "checksum":
            checksum = int(p[1], 16)
    objs, decl, rows, total = [], [], [], 0
    names = sorted(where) + sorted(f.name[:-2] for f in (ROOT / "port/data").glob("*.s") if f.name[:-2] not in where)  # the second lot: no values (.bss)
    for name in names:
        which, so = where.get(name, (None, 0))
        label = "PortTbl_" + re.sub(r"\W", "_", name)
        text = (ROOT / "port/data" / (name + ".s")).read_text()
        off, lines = 0, []
        for l in text.splitlines():
            lines.append(l)
            p = l.split()
            if p and p[0] == ".p2align" and label not in "\n".join(lines):
                lines += [f".globl {label}", f"{label}:"]
            if not p or p[0].startswith("#"):
                continue
            if p[0] == ".space":
                if which is None:
                    raise SystemExit(f"{name}: values expected from the disc, but the table is not in port/data/index.txt")
                rows.append(f"    {{{label} + {off}, {which}, 0x{so + off:X}, {int(p[1])}}},")
                total += int(p[1])
                off += int(p[1])
            elif p[0] == ".zero":
                off += int(p[1])
            elif p[0] == ".long":
                off += 4
        decl.append(f"extern unsigned char {label}[];")
        s = GEN / (name + ".s")
        s.write_text("\n".join(lines) + "\n")
        o = OBJ / ("data_" + re.sub(r"\W", "_", name) + ".o")
        r = rename(subprocess.run([P + "as", str(s), "-o", str(o)], capture_output=True, text=True), o)
        if r.returncode:
            print("FAILED", name, r.stderr.splitlines()[0][:200])
        else:
            objs.append(str(o))
    (GEN / "vita_dat.c").write_text(
        "/* generated by port/vita/build.py from port/data: where the game's data tables get their values */\n"
        '#include "vita_dat.h"\n' + "\n".join(decl) + "\nconst PortDatRun gPortDatRuns[] = {\n" + "\n".join(rows) +
        f"\n}};\nconst unsigned gPortDatCount = {len(rows)};\nconst unsigned long long gPortDatChecksum = 0x{checksum:X}ull;\n")
    print(f"data tables: {len(objs)} files, {total} bytes in {len(rows)} runs to read from the disc")
    return objs


def port_sources():
    """(file, kind): 'ee' game-side code (PS2 constants), 'gs' the renderer front end and sound (as on PC), 'hard' the
    Vita's own code, 'vu0' the vector-library references."""
    S, V = ROOT / "port/src", ROOT / "port/vita"
    out = [(ROOT / "src/port/vu0_a.c", "vu0"), (ROOT / "src/port/vu0_b.c", "vu0")]
    # of port/src: everything but the PC's memory, files, crash report and maths bridge (vita_*.c take their place)
    for n in ("gs_marker", "headless", "mathf_pc", "plat_gs", "plat_mc", "plat_movie", "plat_mpeg2",
              "plat_settings", "plat_stub", "plat_sys", "softfloat_ps2", "port_fast", "port_vufast",
              "bt3_gameplay_rules", "bt3_zcounter2", "bt3_bombardment", "plat_sndstate"):  # (the last four: BT4decomp's)
        out.append((S / (n + ".c"), "gs" if n in ("plat_movie", "plat_mpeg2") else "ee"))
    out += [(V / "vita_mem.c", "hard"), (V / "vita_file.c", "hard"), (V / "vita_main.c", "hard"), (V / "vita_float.c", "hard"),
            (GEN / "vita_dat.c", "hard"), (S / "gs/gs_sony.c", "hard"),
            (SAMPLES / "common/debugScreen.c", "hard"),
            (V / "vita_gfx.c", "hard"), (S / "gs/gs_core.c", "gs"), (S / "gs/gs_vu1.c", "gs"),
            (V / "vita_perf.c", "hard"), (V / "vita_vgl.c", "hard"), (V / "vita_touch.c", "hard"), (V / "vita_optart.c", "hard"),
            (V / "vita_text.c", "hard"), (V / "vita_audio.c", "gs"), (S / "gs/snd_adx.c", "gs"), (S / "gs/snd_se.c", "gs"),
            (V / "vita_vufast_wrap.c", "hard"), (V / "vita_tls.c", "hard"), (V / "vita_clib.c", "hard"),
            (V / "vita_read.c", "hard"), (V / "vita_hitch.c", "hard"), (V / "vita_pgo.c", "hard"), (V / "vita_iso.c", "hard"),
            # BT4decomp: the presentation switches and the fighters' sounds by distance as on PC; what its other PC files
            # give the game, answered as the game without them (online play, Tag Team rooms, the F1 window: vita_bt4.c)
            (S / "gs/presentation.c", "gs"), (S / "gs/snd_spatial.c", "gs"), (V / "vita_bt4.c", "hard"),
            # Tag Team offline: the room model and its local room (docs/vita/tagteam.md)
            (S / "tag_team.c", "hard"), (V / "vita_tag.c", "hard"),
            # BT4decomp's own screens (its Tag Team menus, the Tag Team fight's words): their TrueType lines and the main
            # menu's lettering as on PC, the fight's names over the target, and their pictures for the Vita's renderer
            (S / "gs/menu_text.c", "gs"), (S / "gs/tbo_text.c", "gs"), (S / "gs/menu_labels.c", "gs"),
            (S / "fight_panels.c", "ee"), (V / "vita_tbo.c", "hard"),
            # BT4decomp's free / spectator camera and its screens' Spanish, its own files built unchanged
            # (gs/spectator_camera.c, gs/online_language.c; docs/vita/tagteam.md)
            (V / "vita_spectator.c", "gs"), (V / "vita_onlinelang.c", "gs"),
            # the film behind the Tag Team screens (bt3.txt tbomovie): the hardware player (SceAvPlayer)
            (V / "vita_tagmovie.c", "hard"), (V / "vita_tagmovie_sw.c", "hard"),
            # the network (bt3.txt online, online_test; docs/vita/online-bt4.md): without BT4_VITA_ONLINE only its settings
            (V / "vita_net.c", "net"),
            # Ad Hoc, Vita to Vita without an access point (bt3.txt adhoc_test; the transport of online play the Vita adds)
            (V / "vita_adhoc.c", "net"),
            # the online session's banner and notices drawn with the port screens' kit (bt3.txt online_line)
            (V / "vita_netline.c", "hard"),
            # the PS2 float model (bt3.txt ps2float; its calls only in the BT4_PS2FLOAT variant, with its check)
            (V / "vita_ps2float.c", "nofp"), (V / "vita_ps2float_a.S", "asm"),
            # online play, M0: the state log in the PC's format, the session-like start (bt3.txt m0)
            (V / "vita_m0.c", "hard")]
    if ONLINE:  # the SDL calls of BT4decomp's network files, its signaling client (and the pieces of its update code that
                # the client's route checks call), built unchanged but for cloudflare_signal.inc's PORT_VITA lines
        out += [(V / "vita_sdl.c", "net"), (S / "gs/supabase_signal.cpp", "netcpp"), (S / "gs/update_sign.c", "net"),
                (S / "gs/update_crypto.c", "net"), (S / "gs/update_monocypher.c", "net")]
    if NETLINK:  # online M2a: upstream's 1 vs 1 session, chat, TURN, STUN, lobbies, identity, room client, unchanged
        out += [(S / n, "netcpp" if n.endswith(".cpp") else "net") for n in NETLINK_FILES] + [(V / "vita_netglue.c", "net")]
        # Tag Team between Vitas (T1, first step): the room owner's authority (shared/tag_kernel.c) and its wire
        out += [(S / "tag_team_wire.c", "hard"), (V / "vita_tag_authority.c", "hard")]
    if PS2F:
        out += [(V / "vita_pftest.c", "ee"), (V / "vita_pftest.c", "pfcheck")]
    return out


# BT4decomp's network files the Vita does not link yet, compiled by "build.py netcheck" to see what builds (M1 of the
# online plan, docs/vita/online-bt4.md): the 1 vs 1 session, its chat, TURN, STUN, lobbies, the room client, identity
NETCHECK = ["gs/net.c", "gs/net_chat.c", "gs/net_relay.c", "gs/net_match.c", "gs/net_lobby.c", "gs/build_identity.c",
            "plat_fprint.c", "gs/match_supabase.cpp", "gs/tag_team_net.c", "gs/state.c"]
# the ones BT4_VITA_NETLINK=1 links (Tag Team's network and the rollback's state.c not yet)
NETLINK_FILES = [n for n in NETCHECK if n not in ("gs/tag_team_net.c", "gs/state.c")]


def net_cmd(f, o):
    """The compile of a network file (kind net / netcpp): ordinary code, its SDL calls on the shim, getenv the real one."""
    inc = ["-Iinclude", "-Iport/include", "-Iport/src", "-Iport/vita", f"-I{ROOT / 'port/src/gs'}", f"-I{ROOT / 'port/vita/sdlshim'}"]
    if f.suffix == ".cpp":
        return [P + "g++", "-std=gnu++17", "-c"] + COMMON + inc + ["-O2", str(f), "-o", str(o)]
    # gs/net.c: its socket calls through vita_adhoc.c (vita_adhocnet.h: the Ad Hoc transport of milestone A1)
    adhoc = ["-include", "vita_adhocnet.h"] if f.name == "net.c" and f.parent.name == "gs" else []
    return [P + "gcc", "-std=gnu99", "-c"] + COMMON + inc + adhoc + ["-O2", str(f), "-o", str(o)]


def cc_port(item):
    f, kind = item
    o = OBJ / (("gs_" if kind == "gs" else "pc_") + f.stem + ("_check" if kind == "pfcheck" else "") + ".o")
    if fresh(o, f):
        return o, f, ""
    base = [P + "gcc", "-std=gnu99", "-c"] + COMMON + ["-Iinclude", "-Iport/include", "-Iport/src", "-Iport/vita"]
    if kind == "gs":  # the PC's renderer front end and sound: ordinary float code; its environment switches answered at once
        r = subprocess.run(base + deps(o) + ["-O3", "-freorder-blocks-and-partition", "-include", "vita_cold.h", "-Dgetenv=Vita_GsGetenv",
                                   f"-I{ROOT / 'port/src/gs'}", f"-I{ROOT / 'port/vita/sdlshim'}", str(f), "-o", str(o)], cwd=ROOT, capture_output=True, text=True)
    elif kind == "hard":
        r = subprocess.run(base + deps(o) + ["-O3", "-freorder-blocks-and-partition", "-include", "vita_cold.h", f"-I{SAMPLES / 'common'}", f"-I{GEN}",
                                   str(f), "-o", str(o)], cwd=ROOT, capture_output=True, text=True)
    elif kind == "vu0":  # the vector-library references under the game's names, with the three arithmetic primitives inline
        fast = ["-include", "vita_vu0_inline.h", "-DRefVu0_AddBits=VitaVu_AddBits", "-DRefVu0_MulBits=VitaVu_MulBits", "-DRefVu0_DivBits=VitaVu_DivBits"]
        if f.name == "vu0_b.c":
            fast += [f"-DRefVu0_{n}=VitaVuF_{n}" for n in ("Add", "Sub", "Mul", "Div")]
        mode = ["-O3", "--param", "inline-unit-growth=2000", "--param", "max-inline-insns-auto=400", "--param", "max-inline-insns-single=400",
                "--param", "large-function-growth=2000", "--param", "large-unit-insns=100000"]
        vcmd = base + deps(o) + ABI + mode + fast + ["-include", "math.h", "-include", "stdlib.h", "-include", "port_libm.h",
                                                     "-include", "vu0_names.h", "-DREF_VU0_EXTERN_ARITH", str(f), "-o", str(o)]
        policy = pf_policy(o)
        r = rename(pf_compile(vcmd, o, policy) if policy else subprocess.run(vcmd, cwd=ROOT, capture_output=True, text=True), o)
    elif kind in ("net", "netcpp"):
        r = subprocess.run(net_cmd(f, o)[:-3] + deps(o) + net_cmd(f, o)[-3:], cwd=ROOT, capture_output=True, text=True)
    elif kind == "nofp":  # the PS2 float model's C half: no float register touched (vita_ps2float.c)
        r = subprocess.run(base + deps(o) + ["-O2", "-mgeneral-regs-only", str(f), "-o", str(o)], cwd=ROOT, capture_output=True, text=True)
    elif kind == "asm":
        r = subprocess.run([P + "gcc", "-c"] + deps(o) + [str(f), "-o", str(o)], cwd=ROOT, capture_output=True, text=True)
    elif kind == "pfcheck":  # the PS2 float model's check: its float unit half and the check (vita_pftest.c)
        r = subprocess.run(base + deps(o) + ABI + ["-O2", "-DPF_CHECK=1", str(f), "-o", str(o)], cwd=ROOT, capture_output=True, text=True)
    else:
        r = compile_ee(base + ABI + NOBUILTIN + ["-include", "math.h", "-include", "stdlib.h", "-include", "port_libm.h"], f, o)
    return (o if r.returncode == 0 else None), f, r.stderr


def netcheck():
    """NETCHECK compiled for the Vita into <objects>/netcheck (not linked): per file OK or its first errors, and what
    each object needs that neither the Vita's program (its last link) nor the other objects define."""
    d = OBJ / "netcheck"
    d.mkdir(parents=True, exist_ok=True)
    good = []
    for rel in NETCHECK:
        f = ROOT / "port/src" / rel
        o = d / (f.stem + ".o")
        r = subprocess.run(net_cmd(f, o), cwd=ROOT, capture_output=True, text=True)
        errs = [l for l in r.stderr.splitlines() if "error" in l]
        print(f"netcheck {rel}: {'OK' if r.returncode == 0 else 'FAILED'}")
        for l in errs[:12]:
            print("    " + l[-220:])
        if r.returncode == 0:
            good.append(o)
    names = lambda *a: {l.split()[-1] for l in subprocess.run([P + "nm"] + list(a), capture_output=True, text=True).stdout.splitlines() if l.strip()}
    elf = OUT / "bt3.unstripped.elf"
    have = (names("--defined-only", str(elf)) if elf.exists() else set()) | set(PORT_WRAPS + FAST_WRAPS + VUFAST_WRAPS)  # (wrapped: defined as __wrap_<name>)
    mine = set()
    for o in good:
        mine |= names("--defined-only", str(o))
    for o in good:
        need = sorted(names("-u", str(o)) - have - mine)
        strong = {l.split()[-1] for l in subprocess.run([P + "nm", "--defined-only", "--extern-only", str(o)], capture_output=True, text=True).stdout.splitlines()
                  if len(l.split()) == 3 and l.split()[1] in "TDBR"}  # (not C++'s weak copies of inline code)
        clash = sorted(strong & have)
        print(f"netcheck {o.stem}: needs {len(need)} names nothing defines" + (": " + " ".join(need[:60]) if need else ""))
        if clash:
            print(f"netcheck {o.stem}: {len(clash)} names the program defines already (vita_bt4.c's stand-ins): " + " ".join(clash[:60]))


def cc_libm(f):
    o = OBJ / ("nl_" + f.stem + ".o")
    if fresh(o, f):
        return o, f, ""
    r = compile_ee([P + "gcc", "-std=gnu89", "-c"] + COMMON + ABI + NOBUILTIN + ["-Iport/include", f"-I{f.parent}", "-include", "newlib_shim.h"], f, o)
    return (o if r.returncode == 0 else None), f, r.stderr


def float_expected():
    """vita_float_expected.h: the reference sums of the float self-test (vita_float.c), computed on the host."""
    ren = [f"-D__{n}=PortX_{n}" for n in X_DEFS] + [f"-D{a}={b}" for a, b in RENAME_SOFTFLOAT.items()]
    cc = ["gcc", "-O2", "-w", "-Iinclude", "-Iport/include", "-Iport/src", "-Iport/vita"]
    for src, extra, o in (("port/src/softfloat_ps2.c", ren, "ft_soft.o"), ("src/port/vu0_a.c", ["-DREF_VU0_EXTERN_ARITH"], "ft_vu0.o"),
                          ("port/vita/floattest.c", [], "ft_main.o")):
        subprocess.run(cc + extra + ["-c", src, "-o", str(GEN / o)], cwd=ROOT, check=True)
    subprocess.run(["gcc", "-o", str(GEN / "floattest")] + [str(GEN / o) for o in ("ft_main.o", "ft_soft.o", "ft_vu0.o")] + ["-lm"], check=True)
    (GEN / "vita_float_expected.h").write_text(subprocess.run([str(GEN / "floattest")], check=True, capture_output=True, text=True).stdout)


def pf_host_check():
    """BT4_PS2FLOAT: the model's C half (vita_ps2float.c) against the PC's (port/src/softfloat_ps2.c), compiled for this
    computer, on a few million operands (port/vita/tools/pfcheck_host.c). A difference stops the build."""
    ren = [f"-D__{n}=PortX_{n}" for n in X_DEFS] + [f"-D{a}={b}" for a, b in RENAME_SOFTFLOAT.items()]
    cc = ["gcc", "-O2", "-w", "-Iinclude", "-Iport/include", "-Iport/src", "-Iport/vita"]
    for src, extra, o in (("port/src/softfloat_ps2.c", ren, "pf_soft.o"), ("src/port/vu0_a.c", ["-DREF_VU0_EXTERN_ARITH"] + ren, "pf_vu0.o"),
                          ("port/vita/vita_ps2float.c", ["-DPF_NO_ENV"], "pf_model.o"), ("port/vita/tools/pfcheck_host.c", [], "pf_main.o")):
        subprocess.run(cc + extra + ["-c", src, "-o", str(GEN / o)], cwd=ROOT, check=True)
    subprocess.run(["gcc", "-o", str(GEN / "pfcheck")] + [str(GEN / o) for o in ("pf_main.o", "pf_soft.o", "pf_vu0.o", "pf_model.o")] + ["-lm"], check=True)
    r = subprocess.run([str(GEN / "pfcheck")], capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode:
        raise SystemExit("ps2float: the model (port/vita/vita_ps2float.c) differs from port/src/softfloat_ps2.c")


def compile_all():
    OBJ.mkdir(parents=True, exist_ok=True)
    GEN.mkdir(parents=True, exist_ok=True)
    if not (SAMPLES / "common/debugScreen.c").exists():
        raise SystemExit(f"{SAMPLES}: the vitasdk samples are needed (git clone https://github.com/vitasdk/samples; VITA_SAMPLES=<folder>)")
    float_expected()
    if PS2F:
        pf_host_check()
    data = data_tables()
    if PGO == "use":  # the counts next to the objects, where gcc looks for them (<object>.gcda)
        n = 0
        for g in OBJ.glob("*.gcda"):  # (an earlier build's, which this profile may not have)
            g.unlink()
        for g in PGO_DIR.glob("*.gcda"):
            if g.name in PGO_SKIP:
                continue
            shutil.copy(g, OBJ / g.name)
            n += 1
        print(f"profile counts: {n} files from {PGO_DIR}")
        if n == 0:
            raise SystemExit(f"BT3_PGO=use: no counts in {PGO_DIR} (port/vita/pgo_merge.sh)")
    with concurrent.futures.ProcessPoolExecutor(int(os.environ.get("BT3_JOBS") or 0) or os.cpu_count() or 4) as ex:
        res = list(ex.map(cc_game, portsrc.sources()))
        res += list(ex.map(cc_libm, sorted((ROOT / "port/third_party/newlib_libm").glob("*.c"))))
        res += list(ex.map(cc_port, port_sources()))
    bad = [(f, e) for o, f, e in res if o is None]
    for f, e in bad:
        print("FAILED", f.name, first_error(e))
    objs = [str(o) for o, _, _ in res if o] + data
    (OBJ / "objects.txt").write_text("\n".join(objs) + "\n")
    if PS2F:
        total, files = {}, 0
        for p in OBJ.glob("*.pf"):
            files += 1
            for l in p.read_text().splitlines():
                k, v = l.split()
                total[k] = total.get(k, 0) + int(v)
        print(f"ps2float: {sum(total.values())} instructions replaced in {files} objects: " + " ".join(f"{k} {v}" for k, v in sorted(total.items())))
    print(f"{len(objs)} objects, {len(bad)} failed")
    return not bad


LIBS = ["-lvitaGL", "-lpng16", "-lstdc++", "-lSceCommonDialog_stub", "-lSceGxm_stub", "-lmathneon", "-lvitashark", "-lSceShaccCgExt", "-ltaihen_stub",
        "-lSceShaccCg_stub", "-lSceKernelDmacMgr_stub", "-lSceAudio_stub", "-lScePerf_stub_weak",
        "-lSceDisplay_stub", "-lSceCtrl_stub", "-lSceTouch_stub", "-lScePower_stub", "-lSceAppMgr_stub", "-lSceAppUtil_stub",
        "-lSceSysmodule_stub", "-lSceAvPlayer_stub", "-lSceRtc_stub", "-lSceKernelThreadMgr_stub", "-lSceProcessmgr_stub", "-lSceLibKernel_stub",
        "-lSceIofilemgr_stub", "-lSceSysmem_stub", "-lpthread", "-lz", "-lm"]
# the network part (BT4_VITA_ONLINE): the image's libcurl 8.17 (WebSocket) on OpenSSL 1.1.1, SceNet
if ONLINE:
    LIBS = LIBS[:-3] + ["-lcurl", "-lssl", "-lcrypto", "-lzstd", "-lSceNet_stub", "-lSceNetCtl_stub", "-lScePspnetAdhoc_stub", "-lSceKernelModulemgr_stub"] + LIBS[-3:]

# Game functions replaced by faster equivalents (port/src/port_fast.c: the stage's culling, the effects' task lists).
FAST_WRAPS = "StgFrustum_TestBox StgFrustum_TestPart BtlTaskList_Update BtlTaskList_Draw EftGfx_DrawPolyAvgZFront Bpe_Decode".split()
# Vector-library functions with an exact faster equivalent (port/src/port_vufast.c; the jumps: vita_vufast_wrap.c).
VUFAST_WRAPS = """ClipPoly_ClipPlane ClipPoly_ProjectCur Mtx_Mul Mtx_MulVec3 Mtx_MulVec4 Mtx_RotateX Mtx_RotateXYZ Mtx_RotateY Mtx_RotateZ
    Mtx_RotateZXY Vec3_Add Vec3_Cross Vec3_Dist Vec3_DistSq Vec3_Dot Vec3_Length Vec3_LengthSq Vec3_Normalize Vec3_Scale Vec3_Sub Vec4_Add
    Vec4_Mul Vec4_Scale Vec4_Sub Vu0Cur_MulMtx Vu0Cur_MulMtxRev Vu0Cur_MulVec3 Vu0Cur_MulVec4 Vu0Cur_ProjectInt Vu0Cur_ProjectPoint
    Vu0Cur_ProjectPoints Vu0Cur_ProjectPointStq Vu0Cur_ProjectPointsStq Vu0Cur_RotateX
    Vu0Cur_RotateXYZ Vu0Cur_RotateY Vu0Cur_RotateZ Vu0Cur_RotateZXY""".split()
# The port's own entry points around the game's (headless.c, vita_main.c) and the renderer's hooks.
PORT_WRAPS = "Progress_Main main exit Port_GsVif1Chain sceMpegGetPicture GsVu1_Call".split()
# The C library's per-thread lookups the short way (vita_tls.c, as mh3-wii's fast_tls.c) and a faster memcmp (vita_clib.c)
PORT_WRAPS += ["__getreent", "pte_osTlsGetValue", "memcmp"]
# The file calls, the C library's included: the disc's files read from its .iso as it is (vita_iso.c, arielxflores's)
# every request curl makes logged, curl's CA list set (port/vita/vita_net.c)
if ONLINE:
    PORT_WRAPS += ["curl_easy_init", "curl_easy_perform", "curl_multi_add_handle"]
PORT_WRAPS += """sceIoOpen sceIoClose sceIoRead sceIoPread sceIoLseek sceIoLseek32 sceIoGetstat sceIoGetstatByFd sceIoDopen sceIoDread
    sceIoDclose""".split()


def link_cmd(objs, extra):
    # -q keeps the relocations vita-elf-create needs (a Vita program is loaded at an address of the system's choosing)
    wraps = [f"-Wl,--wrap={n}" for n in PORT_WRAPS + FAST_WRAPS + VUFAST_WRAPS]
    pgo = ["-fprofile-generate"] if PGO == "gen" else []  # (libgcov)
    return [P + "gcc", "-Wl,-q", "-Wl,--no-warn-mismatch", "-w", "-o", str(OUT / "bt3.elf")] + pgo + extra + objs + LIBS + wraps + \
           [f"-Wl,-Map={OUT / 'bt3.map'}"]


def link():
    OUT.mkdir(parents=True, exist_ok=True)
    objs = (OBJ / "objects.txt").read_text().split()
    r = subprocess.run(link_cmd(objs, []), capture_output=True, text=True)
    # functions the game's code refers to that do not exist on the Vita (the PS2's hardware libraries): each gets a
    # stub that stops the program with its name, should it ever be called
    need = sorted(set(re.findall(r"undefined reference to `(\w+)'", r.stderr)))
    lines = ["/* Generated by port/vita/build.py: one stub per symbol nothing provides. */", "extern void Port_Missing(const char *name);"]
    lines += [f'void {s}(void) {{ Port_Missing("{s}"); }}' for s in need]
    (GEN / "stubs.c").write_text("\n".join(lines) + "\n")
    so = OBJ / "pc_stubs.o"
    subprocess.run([P + "gcc", "-c", "-O1", str(GEN / "stubs.c"), "-o", str(so)], check=True)
    # vita-elf-create needs room behind the first segment for its own tables; when the segment ends just before a page
    # boundary there is none ("segment 1 overlaps"), and a few constant bytes are added until it fits
    pad = []
    for size in range(0, 4096, 512):
        if size:
            (GEN / "pad.c").write_text(f"const char gVitaPad[{size}] = {{1}};\n")
            subprocess.run([P + "gcc", "-c", str(GEN / "pad.c"), "-o", str(OBJ / "pad.o")], check=True)
            pad = [str(OBJ / "pad.o")]
        r = subprocess.run(link_cmd(objs, [str(so)] + pad), capture_output=True, text=True)
        if r.returncode != 0:
            break
        t = subprocess.run([str(SDK / "bin" / "vita-elf-create"), str(OUT / "bt3.elf"), str(OUT / "probe.velf")], capture_output=True, text=True)
        if t.returncode == 0:
            break
    errs = [l for l in r.stderr.splitlines() if "multiple definition" in l or "undefined reference" in l or "rror" in l]
    print(f"{len(need)} stubs; link {'OK' if r.returncode == 0 else 'FAILED'}")
    for l in errs[:15]:
        print("  " + l[-200:])
    return r.returncode == 0


def vpk():
    run = lambda *a: subprocess.run([str(SDK / "bin" / a[0])] + [str(x) for x in a[1:]], check=True, capture_output=True, text=True)
    shutil.copy(OUT / "bt3.elf", OUT / "bt3.unstripped.elf")
    subprocess.run([P + "strip", "-g", str(OUT / "bt3.elf")], check=True)
    run("vita-elf-create", OUT / "bt3.elf", OUT / "bt3.velf")
    run("vita-make-fself", "-c", OUT / "bt3.velf", OUT / "eboot.bin")
    run("vita-mksfoex", "-s", f"TITLE_ID={TITLE_ID}", "-s", f"APP_VER={APP_VER}", "-d", "ATTRIBUTE2=12", TITLE, OUT / "param.sfo")
    extra = []
    for f in sorted(x for x in SCE_SYS.rglob("*") if x.is_file()):
        extra += ["-a", f"{f}=sce_sys/{f.relative_to(SCE_SYS).as_posix()}"]
    # the shader bank (vita_vgl.c sh_cached: the programs as the Vita's compiler made them, tools/shbank.py)
    bank = ROOT / "port/vita/shbank.bin"
    if bank.exists():
        extra += ["-a", f"{bank}=shbank.bin"]
        print(f"the package carries the shader bank ({bank.stat().st_size} bytes)")
    # BT4decomp's own screens (port/vita/vita_tbo.c): the pictures of port/tbo/ui as tbo/ui/ and the fonts of
    # port/third_party/fonts (with fonts.tsv and their licences) as fonts/, as the PC's release has them. Left out: what
    # only the online hub shows (its radar, hosts, grounds, community button), the unused icon, and the films behind the
    # Tag Team screens (tag_ui_*.pss, 30 MB, packaged only with BT4_VITA_TAGMOVIE=1: the film is the .mp4 files below)
    ui, n = ROOT / "port/tbo/ui", 0
    online_only = {"radar.png", "bg.png", "discord.png", "tag_team_icon.png"}
    # the pictures made ready for the Vita here, not at run time (tools/tbo_vita_assets.py: decoded, touched, sized for
    # the 960 x 544 screen, as the textures the renderer uploads: tbo/ui/<name>.vt), the main menu's emblem with them
    names = [f.stem for f in sorted(ui.iterdir())
             if f.suffix == ".png" and f.name not in online_only and not f.name.startswith("hosts_")] + ["main_tag_emblem"]
    vt = ROOT / "port/build/tbo_vita"
    subprocess.run([sys.executable, str(ROOT / "port/vita/tools/tbo_vita_assets.py"), str(ui), str(vt)] + names, check=True)
    for name in names:
        extra += ["-a", f"{vt / (name + '.vt')}=tbo/ui/{name}.vt"]
        n += 1
    extra += ["-a", f"{ui / 'CREDITS.md'}=tbo/ui/CREDITS.md"]
    n += 1
    fonts = ROOT / "port/third_party/fonts"
    for f in sorted(x for x in fonts.rglob("*") if x.is_file()):
        extra += ["-a", f"{f}=fonts/{f.relative_to(fonts).as_posix()}"]
        n += 1
    print(f"the package carries {n} files of BT4decomp's own screens (tbo/ui, fonts)")
    # the film behind the Tag Team screens (vita_tagmovie.c, bt3.txt tbomovie=1): the two H.264 films made from upstream's
    # .pss by port/vita/tools/make_tagmovie.sh (1.3 MB; the .pss, 30 MB, only with BT4_VITA_TAGMOVIE=1, below)
    for f in ("tag_ui_intro.mp4", "tag_ui_loop.mp4"):
        extra += ["-a", f"{ROOT / 'port/vita/assets' / f}=tbo/ui/{f}"]
    print("the package carries the film behind the Tag Team screens (tbo/ui/tag_ui_{intro,loop}.mp4, about 1.3 MB)")
    # the software path (vita_tagmovie_sw.c, tbomovie=2) plays upstream's own .pss: 30 MB more, so only on request
    if os.environ.get("BT4_VITA_TAGMOVIE") == "1":
        for f in ("tag_ui_intro.pss", "tag_ui_loop.pss"):
            extra += ["-a", f"{ui / f}=tbo/ui/{f}"]
        print("the package carries the PC's films too (tbo/ui/tag_ui_*.pss, 30 MB: BT4_VITA_TAGMOVIE=1)")
    # the network part's CA list (BT4_VITA_ONLINE): the build image's public CA certificates as lib/cacert.pem, where
    # gs/curl_ws_io.h looks for it, as the PC's packages carry it (port/tools/package_native.py)
    if ONLINE:
        ca = next((pathlib.Path(x) for x in ["/etc/ssl/certs/ca-certificates.crt", "/etc/ssl/cert.pem"] if pathlib.Path(x).is_file()), None)
        if ca is None:
            raise SystemExit("BT4_VITA_ONLINE: no CA list in the build image (/etc/ssl/certs/ca-certificates.crt)")
        extra += ["-a", f"{ca}=lib/cacert.pem"]
        print(f"the package carries the CA list lib/cacert.pem ({ca.stat().st_size} bytes)")
    run("vita-pack-vpk", "-s", OUT / "param.sfo", "-b", OUT / "eboot.bin", *extra, OUT / VPK)
    print(f"{OUT / VPK}: {(OUT / VPK).stat().st_size} bytes")


def main():
    steps = [a for a in sys.argv[1:] if not a.startswith("-")] or ["compile", "link", "vpk"]
    if steps == ["netcheck"]:
        netcheck()
        return
    if "compile" in steps and not compile_all():
        sys.exit(1)
    if "link" in steps and not link():
        sys.exit(1)
    if "vpk" in steps:
        vpk()


if __name__ == "__main__":
    main()
