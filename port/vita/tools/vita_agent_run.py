#!/usr/bin/env python3
"""One test run of the development build on the console through vita-agent-use
(https://github.com/shoui520/vita-agent-use): the same job as vita_console_run.sh (vitacompanion), with what
vita-agent-use adds: real screenshots during the run, the console's CPU / FPS / memory samples, and crash dumps seen
as they are written. CLAUDE.md and docs/vita/testing.md say how to set it up; vita-agent-use's own AGENTS.md is the
law for using it (its `serve` must run for the whole session; a person approves pairing and any change of tai/).

    python3 port/vita/tools/vita_agent_run.py --settings F --seconds N [--eboot] [--shots K] [--out DIR]

    --settings  a bt3.txt for this run, put in ux0:data/bt4/bt3.txt (the console's is downloaded first and put back
                after; if there was none, the run's is moved to the plugin's trash). exitafter=N is added to it, so
                the game ends itself and writes the line the run waits for.
    --seconds   the run's length (default 300)
    --eboot     first send port/build/vita_dev/eboot.bin over the installed one (the run's own "prepare" step). Each
                overwrite leaves a full hidden copy of the old file that no command can free (vita-agent-use's upload
                storage): send it only when it changed, and watch ux0's free space
    --eboot-dir another build folder's eboot.bin for --eboot (port/build/vita_dev_pgogen: the profile-counting build)
    --rotate-log MB  the console's log renamed aside (kept) before the run when larger (default 16; 0 never): the whole log is fetched after each run
    --shots     screenshots taken during the run, evenly spaced (default 6; 0 none)
    --title     default BTFV00008, the development build
    --out       default port/build/runs/agent-<date-time>
    env VITA_AGENT             the vita-agent launcher (default: vita-agent on the PATH)
        VITA_AGENT_CONFIG_DIR  the directory chosen in vita-agent-use's bootstrap

Leaves in --out: log.txt (this run's part of ux0:data/bt4/log-vgl.txt), shots/*.jpg, run.json and run_status.json,
events.jsonl (the run's events: performance samples, crash dumps, dialogs), hang.txt if any, perfsum.txt.
Runs over an hour (vita-agent-use's run timeout) are launched and watched without a run (no run-owned metrics).
Exit codes: 0 done, 1 a crash dump, 2 / 3 not started, 4 the console's bt3.txt holds a test run's lines (exitafter=,
mode=: an earlier run did not put the developer's back; nothing is done), 5 the console's settings could not be put
back (after a crash the console refuses changes while it writes its dump: tried for about three minutes), 6 the game
did not write its `vita: exitafter:` line (not launched, hung or closed). A batch of runs stops at anything but 0.
"""
import argparse
import datetime
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
LOG = "ux0:data/bt4/log-vgl.txt"
SETTINGS = "ux0:data/bt4/bt3.txt"
TERMINAL = {"completed", "cancelled", "failed", "connection_lost"}


def agent(*args, check=False, timeout=180):
    """Runs the vita-agent CLI; returns (exit code, the JSON objects it printed)."""
    exe = os.environ.get("VITA_AGENT") or shutil.which("vita-agent") or "vita-agent"
    try:
        p = subprocess.run([exe, *[str(a) for a in args]], capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        if check:
            raise RuntimeError("vita-agent %s: no answer in %d s" % (" ".join(map(str, args)), timeout))
        return 124, []
    objs = []
    for line in p.stdout.splitlines():
        line = line.strip()
        if line.startswith("{"):
            try:
                objs.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    if check and p.returncode != 0:
        raise RuntimeError("vita-agent %s failed (%d): %s %s" % (" ".join(map(str, args)), p.returncode, p.stdout[-600:], p.stderr[-300:]))
    return p.returncode, objs


def last(objs):
    finals = [o for o in objs if o.get("status") != "progress"]
    return finals[-1] if finals else (objs[-1] if objs else {})


def upload(src, dest, out, overwrite=False):
    """fs upload with a transfer state of its own: vita-agent-use keeps one per source and destination by default and
    takes a finished one with the same bytes as done again, without sending them (a file removed since stays gone)."""
    state = out / ("xfer-%d.json" % time.monotonic_ns())
    args = ["fs", "upload", src, dest, "--transfer-state", state] + (["--overwrite", "--yes"] if overwrite else [])
    rc, objs = agent(*args, check=True)
    return rc, objs


def exists(path):
    rc, objs = agent("fs", "stat", path)
    return rc == 0 and last(objs).get("status") == "ok"


def connected():
    rc, objs = agent("server", "status", timeout=60)
    st = last(objs)
    return bool(st.get("connected") or st.get("result", {}).get("connected"))


def wait_connected(note, seconds=240):
    """True as soon as the console answers again; False after `seconds` without it (switched off, asleep, crashed
    hard): then there is no point in trying for longer, a person has to look at it."""
    end = time.monotonic() + seconds
    while True:
        if connected():
            return True
        if time.monotonic() > end:
            note("THE CONSOLE IS NOT ANSWERING (for %d s): is it on and awake?" % seconds)
            return False
        time.sleep(20)


def put_back(src, out, note):
    """The console's own settings put back, and read back to be sure. Right after a crash the console is busy writing
    its dump and refuses changes for a while ("stale"): tried again, each time with a fresh transfer state, for up to
    about three minutes. False (and a loud line) if they could not be put back."""
    want = src.read_bytes()
    for attempt in range(12):
        if not wait_connected(note):
            break
        try:
            upload(src, SETTINGS, out, overwrite=True)
        except RuntimeError as e:
            note("putting the settings back failed (try %d): %s" % (attempt + 1, str(e)[:200]))
        got = out / "bt3.readback.txt"
        if got.exists():
            got.unlink()
        agent("fs", "download", SETTINGS, got)
        if got.is_file() and got.read_bytes() == want:
            return True
        time.sleep(15)
    note("THE CONSOLE'S SETTINGS WERE NOT PUT BACK:", SETTINGS, "still holds this run's; the console's own are in", src)
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--settings", required=True, type=pathlib.Path)
    ap.add_argument("--seconds", type=int, default=300)
    ap.add_argument("--eboot", action="store_true")
    ap.add_argument("--eboot-dir", type=pathlib.Path, help="with --eboot: the eboot.bin of this build folder instead of vita_dev's (port/build/vita_dev_pgogen)")
    ap.add_argument("--shots", type=int, default=6)
    ap.add_argument("--title", default="BTFV00008")
    ap.add_argument("--rotate-log", type=int, default=16, metavar="MB", help="the console's log moved aside (renamed, kept) before the run when larger than this (0: never)")
    ap.add_argument("--out", type=pathlib.Path)
    o = ap.parse_args()
    out = o.out or ROOT / "port/build/runs" / ("agent-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    (out / "shots").mkdir(parents=True, exist_ok=True)
    note = lambda *a: print("vita_agent_run:", *a, flush=True)

    rc, objs = agent("server", "status")
    st = last(objs)
    if not (st.get("connected") or st.get("result", {}).get("connected")):
        note("vita-agent-use's serve is not connected (start `vita-agent serve` in the background first):", json.dumps(st)[:300])
        return 3
    agent("app", "close", o.title)

    # the settings: the console's kept, this run's sent (with exitafter, the end the run waits for)
    had = exists(SETTINGS)
    if had:
        agent("fs", "download", SETTINGS, out / "bt3.console.txt", check=True)
        left = [l for l in (out / "bt3.console.txt").read_text(errors="replace").splitlines() if l.startswith(("exitafter=", "mode="))]
        if left:  # (a run before this one did not put the console's own back: they would be kept as the console's)
            note("the console's", SETTINGS, "holds a test run's lines (%s): put the developer's own back first" % ", ".join(left))
            return 4
    run_settings = out / "bt3.run.txt"
    run_settings.write_text(o.settings.read_text() + "\nexitafter=%d\n" % o.seconds)
    restored = False
    try:
        upload(run_settings, SETTINGS, out, overwrite=had)
        if not exists(SETTINGS):
            note("the settings did not reach", SETTINGS)
            return 3
        # the log, once large, moved aside (kept, not deleted): the whole of it is fetched after every run
        rc, objs = agent("fs", "stat", LOG)
        size = int(last(objs).get("result", {}).get("bytes") or 0) if rc == 0 else 0
        if o.rotate_log > 0 and size > o.rotate_log << 20:
            kept = LOG.replace(".txt", "-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + ".txt")
            agent("fs", "move", LOG, kept, "--yes", check=True)
            note("the console's log (%d MB) moved to %s" % (size >> 20, kept))

        eboot = (o.eboot_dir if o.eboot_dir else ROOT / "port/build/vita_dev") / "eboot.bin"
        if o.eboot and not eboot.is_file():
            note(str(eboot), "not found (BT3_VITA_DEV=1 python3 port/vita/build.py)")
            return 2
        long_run = o.seconds + 180 > 3600
        t0 = time.time()
        if not long_run:
            spec = {"title_id": o.title, "timeout_s": o.seconds + 180, "performance_interval_ms": 2000, "close_on_completion": True,
                    "completion": {"log": {"path": LOG, "literal": "vita: exitafter:"}}}
            if o.eboot:
                spec["prepare"] = {"upload": {"source": str(eboot), "destination": "ux0:app/%s/eboot.bin" % o.title, "overwrite": True}}
            (out / "run.json").write_text(json.dumps(spec, indent=2))
            rc, objs = agent("run", "start", out / "run.json", *(["--yes"] if o.eboot else []), timeout=600)
            run_id = last(objs).get("run_id") or last(objs).get("result", {}).get("run_id")
            if not run_id:
                note("the run did not start:", json.dumps(last(objs))[:400])
                return 3
            note("run", run_id, "for about", o.seconds, "s")
        else:
            if o.eboot:
                upload(eboot, "ux0:app/%s/eboot.bin" % o.title, out, overwrite=True)
            agent("server", "performance-start", "--seconds", 3600, "--interval-ms", 2000)
            agent("app", "launch", o.title, check=True)
            run_id = None
            note("launched for", o.seconds, "s (no run: longer than vita-agent-use's hour)")

        shots_at = [o.seconds * (k + 1) / (o.shots + 1) for k in range(o.shots)]
        status = {}
        while True:
            time.sleep(5)
            el = time.time() - t0
            while shots_at and el >= shots_at[0]:
                shots_at.pop(0)
                agent("screen", "capture", "--output", out / "shots" / ("%05d.jpg" % int(el)))
            if run_id:
                rc, objs = agent("run", "status")
                status = last(objs)
                phase = status.get("phase") or status.get("result", {}).get("phase")
                if phase in TERMINAL:
                    break
            else:
                rc, objs = agent("app", "running")
                if el > 60 and o.title not in json.dumps(last(objs)):
                    break
            if el > o.seconds + 600:
                note("no end after", int(el), "s: cancelling")
                agent("run", "cancel") if run_id else agent("app", "close", o.title)
                break
        (out / "run_status.json").write_text(json.dumps(status, indent=2))
        if not run_id:
            agent("server", "performance-stop")

        # the run's events (performance samples, crash dumps, dialogs)
        after, events = 0, []
        for _ in range(64):
            rc, objs = agent("server", "events", "--after", after, "--limit", 1024)
            r = last(objs)
            page = r.get("events") or r.get("result", {}).get("events") or []
            events += [e for e in page if run_id is None or e.get("run_id") in (run_id, None)]
            nxt = r.get("next") or r.get("result", {}).get("next")
            if not page or nxt is None or nxt == after:
                break
            after = nxt
        with open(out / "events.jsonl", "w") as f:
            for e in events:
                f.write(json.dumps(e) + "\n")

        # the log, the watchdog's file, the settings put back
        agent("fs", "download", LOG, out / "log-all.txt", timeout=600)
        if exists("ux0:data/bt4/hang-vgl.txt"):
            agent("fs", "download", "ux0:data/bt4/hang-vgl.txt", out / "hang.txt")
    finally:
        if had:
            restored = put_back(out / "bt3.console.txt", out, note)
        else:
            restored = agent("fs", "trash", SETTINGS, "--yes")[0] == 0
    if (out / "log-all.txt").is_file():
        lines = (out / "log-all.txt").read_text(encoding="utf-8", errors="replace").splitlines(True)
        start = max([i for i, l in enumerate(lines) if l.startswith("vita: Tenkaichi3Decomp")] or [0])
        (out / "log.txt").write_text("".join(lines[start:]))
        if any(l.startswith("soak: battle") for l in lines[start:]):
            for name, extra in (("perfsum.txt", []), ("perfsum_ab.txt", ["--ab"])):
                p = subprocess.run([sys.executable, str(ROOT / "port/vita/tools/perfsum.py"), *extra, str(out / "log.txt")], capture_output=True, text=True)
                (out / name).write_text(p.stdout + p.stderr)
    dumps = [e for e in events if str(e.get("type", "")).startswith("coredump")]
    note("phase:", status.get("phase") or status.get("result", {}).get("phase") or "(no run)",
         "| crash dumps:", len(dumps), "| shots:", len(list((out / "shots").glob("*.jpg"))), "| files in", out)
    if not restored:
        return 5
    if dumps:
        return 1
    ended = (out / "log.txt").is_file() and "vita: exitafter:" in (out / "log.txt").read_text(encoding="utf-8", errors="replace")
    if not ended:  # (no crash dump, but the game did not reach its own end either: not launched, hung, closed)
        note("the log has no `vita: exitafter:` line: the game did not end the run itself; run error:",
             json.dumps(status.get("error") or status.get("result", {}).get("error"))[:300])
        return 6
    return 0


if __name__ == "__main__":
    sys.exit(main())
