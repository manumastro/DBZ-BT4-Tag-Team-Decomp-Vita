# The renderer work in progress (handoff)

The whole path for an agent, from a checkout to a change measured on the console: [CLAUDE.md](../../CLAUDE.md).

Written 2026-11-12 so that another person or agent can take the work up without the conversation that produced it;
brought up to date 2026-11-13. Update it when the state changes; delete it once everything here has been merged.

## Where the work is

Everything is in one pull request to `arielxflores/Tenkaichi3Decomp-Vita` (base `vita`, PR #6): the detailed stages'
fixes, the fourth core's helper, vitaGL's state calls, the effects at a lower resolution, the first steps of a native
renderer, and the test tools. No work is held back on another branch.

The renderer's switches, in the order they came, with their defaults (each read from `ux0:data/bt3/bt3.txt` by the
**development build** only; the release reads `disc`, `heap`, `wide`, `report` and runs with the defaults; every
switch is in [development.md](development.md)'s table):

| Switch | What | Default | Run on the console (the Results below) |
|---|---|---|---|
| `ccevict`, `ccsize`, `dppick`, `dpslots`, `dpbank`, `dpframe` | The detailed stages' fixes: the cache of calls emptied, the kept parts' places, the stage buffer's banks, FRAME as "the picture" | on | The 25 stages' survey (the pull request's description) |
| `fehelper` | The uploads' numbers on the fourth core (CapUnlocker) | on | idem |
| `gxmcache` | vitaGL's GXM state calls left out when they change nothing | on | idem |
| `fxlow` | The screen-filling additive effects at a lower resolution | on | `runs/ab-fxlow`, `runs/kame-fx` |
| `dpsplit` | Kept parts' stage strips grouped by their bank too | on | `runs/check-split` |
| `native` | The engine seam: the stage's meshes and animated objects, the fighters' parts named by the game | on | `runs/ab-native`, `runs/gain-native` |
| `nativetex` | The stage's and the fighters' textures named by the game | on | `runs/tex1` |
| `updest` | Each upload's destination remembers what it left there | on | `runs/dest2` |
| `dpfast`, `fxrun` | Kept parts drawn with what does not change worked out once; the effects' particles as runs | on | `runs/fast1` |
| `texbind` | Textures bound by the names of what their uploads left | on | `runs/next1`, `runs/next2` |
| `seavtx` | The sea's corners tested and projected once a frame each | on | `runs/next1`, `runs/next2` |
| `esdirect` | The seam's call packets done as what they are (an experiment: no gain measured) | off | `runs/next1` |
| `upplan` | The large uploads' blocks' places kept for each rectangle; no rectangle's number worked out where another upload's data lies | on | 2026-11-15, console, 2 vs 2 (BT4 Tag Team) 3.5 min A/B: uploads 269 -> 244 ms per 2 s, speed 98.4 -> 98.7%, 0 wrong in ~260 checks |
| `dprt` | Kept parts: the fighters' uniform blocks worked out by the render thread, the groups handed over by their place | off | 2026-11-15, console, 2 vs 2 with upplan: worse, 26.3 -> 14.5 pictures a second (the render thread became the limit: 63 ms a picture to the chip) |
| `texcont` | A texture found by its contents wherever GS memory holds them | on | 2026-11-16, console, 2 vs 2 (BT4 Tag Team), [tagteam.md](tagteam.md): new textures 134 -> 8 every 2 s |
| `dpways`, `dpslots=4096` | Kept parts: a second place when the first is another's; 4096 places (was 2048; ~10 MB more heap) | on | 2026-11-16, 2 vs 2: 22.7 -> 24.9 and 27.5 -> 28.1 pictures a second; parts walked ~1,660 -> ~70 every 2 s |
| `silgate`, `silprobe=64` | The shadow page's strips not sent while the page is not used (2 vs 2: never read) | on | 2026-11-16, 2 vs 2: the list reader 31 -> 20 ms a list; 1 vs 1: the page's clears, reads and shadows the same with and without |
| `wtsig`, `rtkick`, `dptexa`, `texrt`, `fxclamp`, `adfast` | Small cuts on the drawing thread (work targets' signatures, the render thread's wake-ups, TEXA, palette textures without waiting, the animated effects' triangles as runs, A+D tags in one loop) | on | 2026-11-16, 2 vs 2: each inside the noise, together the beam 19.2 -> 22.3 |
| `hudbatch`, `hudrun` | The Tag Team radar's plain shapes in one packet (game side, `PORT_VITA`) and read as a run | on | 2026-11-16, 2 vs 2: 28.2 -> 28.9, the beam 20.4 -> 22.0; the radar the same in the console's screenshots |
| `dpptr`, `dpcrec` | `dprt`'s two halves without the render thread's work: groups by their place, constants from the record | on | 2026-11-16, 2 vs 2: a kept part's match 1,630 -> 1,055 cycles, the beam 22.3 -> 23.3 |
| `dpgen` | A kept part's register changes bump the state's generation (a correctness fix of the quick way) | on | 2026-11-16, 2 vs 2: no cost; ~7,000 such fields every 2 s |
| `skipcc`, `dppf` | Kept calls in lists not drawn sent as registers only; the next kept part prefetched | off | 2026-11-16, 2 vs 2: skipcc worse (27.5 -> 26.6), dppf no gain |
| `rtcap`, `skipasync`, `texrtfill`, `chipkeep`, `esrebind` | The heavy 2 vs 2 fight's single long lists (50-190 ms): the render thread let fall several pictures behind and then waited for at once; the texture pools never filled in a fight; the stage's copies let go with the fighters'; every model's kept calls made again at each bind of an object's model | off | not run yet (branch `wip/hitch`; causes from the runs `hx2`, `hx2off`) |

Removed after a console run: `dpjoin` (never a pair). Also on by default, with all of the above: `runs/final` (10
minutes, duels and three against three, six stages).

## State of verification

Run on a console (PCH-1004, firmware 3.65, CapUnlocker on, the development build, driven over the network through
vita-agent-use): every switch above, A against B and then with everything on by default; the figures and verdicts are
in the Results table. No crash, no hang, no texture found changed, pictures right in the screenshots.

Every file the pull request touches compiles, up to syntax and types, for the Vita's processor
(`port/vita/tools/check_syntax.sh`), and every change to the game's text is under `#ifdef PORT_VITA` or behind a macro
that is the original call without it (the PS2 matching build and the PC port see none of it).

Not run yet:

- **without the fourth core** (CapUnlocker off): `fehelper` then does nothing, and the drawing thread works out the
  uploads' numbers itself; the room the native renderer gives is meant for this case;
- **the release build** (BTTD00010): the same code with the defaults, without the development build's switches and
  log;
- **Vita3K**: stage 1 of [compare.md](compare.md) was skipped, the console runs went straight to stage 2;
- the PS2 matching build and the PC port, for byte identity (the guards were checked by reading);
- the heavy effects' test fight in 2 vs 2 (`demoult` with `BT4_TAG_ACTOR_TEST=4`, `demochara3` / `demochara4`,
  `demoultp=4`, [development.md](development.md)): the Super Spirit Bomb and the Dragon Fist on both teams, every
  fighter in turn; built, not run (the 1 vs 1 one was run: the Results table).

## How to test on the console

Build the development package (`BT3_VITA_DEV=1`, [building.md](building.md)), install it next to the release.

1. **It starts and draws.** No settings: start a fight. The log `ux0:data/bt3/log-vgl.txt` must have
   `vgl: the effects at a lower resolution: their pixel shaders built` (else `... are off: their pixel shaders (N)
   were not built`, and the shader compiler's message above it).
2. **The counters.** Every two seconds (`report=1`): the effects' line, **"draws without their shader" 0**; the
   engine seam's line, **"to the cache (table full)" 0**; the named textures' line, **found changed 0**; texbind's
   `wrong` 0.
3. **A switch against the old way, A/B.** In `bt3.txt` (or `port/vita/tools/settings/soak_ab.txt`,
   `native_next_ab.txt`):

   ```
   mode=demo
   soak=1
   soakduel=1
   soakstages=0-24
   soakab=1
   texbind=2
   ```

   (any switch with the value 2: its B battles have it, its A battles not). `python3 port/vita/tools/perfsum.py --ab
   log-vgl.txt` sets each stage's B battles against its A battles; `--parts` shows where the drawing thread's time
   goes.
4. **Without the fourth core**: the same with CapUnlocker off ([CLAUDE.md](../../CLAUDE.md) section 4, step 5).

When a picture is wrong: `film=` takes pictures into the log, `drawlog=` and `bislist=` find the draw
([development.md](development.md)); each change has its switch to tell whether it is the cause.

## Testing without holding the console

[testing.md](testing.md): the four levels (the syntax check; Vita3K; the console driven over the network; by hand),
what each shows, how to set each up once, and the runners. `port/vita/tools/vita_agent_run.py` (vita-agent-use) ran
every console run in the Results table. `port/vita/tools/vita_console_run.sh` (vitacompanion) and
`port/vita/tools/vita3k_run.sh` were written from the tools' documentation and tried only against stand-ins of the
console and of Vita3K (not on the real ones yet).

## Comparing A and B, and the results

The workflow: [compare.md](compare.md) (stage 1 in Vita3K: does the new way draw what the old one draws; stage 2 on
the console: is it faster and does it look right; stage 3: write it down here, then decide).

### Results

One line per stage run (the newest first). The run's folder is `port/build/runs/...` on the computer that ran it.

| Date | Build (commit) | Where | Setting | Stage | Verdict / figures | Run |
|---|---|---|---|---|---|---|
| 2026-11-16 | `3df72ab` (dev, BT4 fork) | Console, vita-agent-use | the soak (`soakab`, 1 vs 1 duels, stages 0-22 in 45 minutes, 46 battles) with texcont, dpways, silgate, wtsig, rtkick, dptexa, texrt, fxclamp, hudbatch, hudrun, adfast, dpptr, dpcrec, dpgen at 2 (on in the B battles; dpslots 4096 on both sides) | 2 (speed, health) | fps B-A +0.1 on the whole (29.6 / 29.7, at the cap), every stage -0.1..+0.6; the lowest 2 s stretch better on 18 of 23 stages (stage 1 25.3 -> 27.4, stage 4 23.0 -> 25.4); the drawing thread 2-26% less (stage 4 831 -> 658 ms every 2 s, stage 10 780 -> 576); no crash, no watchdog, no check differing; heap at most 56.7 MB of 80, main memory free at least 58 MB | `runs/soak1v1` |
| 2026-11-16 | `69839a0` .. `3df72ab` (dev, BT4 fork) | Console, vita-agent-use | texcont, dpways, silgate, dpslots=4096, wtsig, rtkick, silprobe, dptexa, texrt, fxclamp, hudbatch, hudrun, adfast, dpptr, dpcrec, dpgen (each A/B against a base run on its own build, then the defaults alone), 2 vs 2 stage 0 and 1 vs 1, 120 s each, `film=30` for the pictures | 2 (speed, pictures) | 2 vs 2 22.7 -> 28.8 pictures a second, the beam special 11.5 -> 21.8 (23.3 with dpptr/dpcrec), stretches under 20 a second 10 -> 0-1 of 39; 1 vs 1 29.6 -> 29.8; no crash in ~40 runs; pictures right. [tagteam.md](tagteam.md) | `runs/b0`, `dw1`, `sg1`, `ds1`, `c0`-`c5`, `d0`-`d1f`, `e0`-`e2f`, `f0`-`f1f`, `g0`-`g1v1`, `def`, `defv1`, `h0`-`h3v1` |
| 2026-11-16 | `531a7e0` (dev, BT4 fork) | Console, vita-agent-use | batch 10: sprrun, skipfast, dpgtex + upidmemo at 1 against the defaults (2 vs 2 stage 0, 120 s), the three at 3 with uprepeat=1, all on with `film=30`, all on in 1 vs 1 | 2 (speed, pictures, checks) | defaults 29.0 (beam 23.4); sprrun 29.2 (list reader 18.7 -> 17.0 ms): on by default; skipfast 28.9 (its counter 0 in most stretches: the lists not drawn carry almost nothing in a fight): stays 0; dpgtex + upidmemo 28.9 (beam 23.1): stay 0; the checks 0 differences; uprepeat: of ~17,000 small uploads every 2 s ~13,000 not in the small uploads' memory, ~10,800 landing where the same bytes lay (-> upsame); pictures of all on as before; 1 vs 1 all on 29.9 | `runs/i0`..`i5v1` |
| 2026-11-16 | `162ac02` (dev, BT4 fork) | Console, vita-agent-use | batch 11: upsame=3 with uprepeat, then upsame=1 against the defaults twice interleaved (2 vs 2 stage 0, 120 s), upsame=1 with `film=30`, upsame=1 in 1 vs 1 | 2 (speed, checks) | the check 0 differences (55 stretches; 5,500-11,700 of 9,500-18,000 small uploads every 2 s the same as GS memory); A 28.8 / 29.0, B 28.7 / 29.0; in the beam the drawing thread 19.1 / 20.7 / 19.1 M cycles a list (A, B, B): the small uploads' writes -400 k, the comparison +450 k, nothing saved after them: stays 0; 1 vs 1 29.9 | `runs/u0`..`u1v1` |
| 2026-11-16 | `fea3924` (dev, BT4 fork) | Console, vita-agent-use | batch 12: rtswap=1 against the defaults twice interleaved (2 vs 2 stage 0, 120 s), rtswap=1 in 1 vs 1 | 2 (speed) | A 29.1 / 29.0, B 29.3 / 28.9; beam A 22.8 / 23.2, B 23.6 / 23.2; flame A 29.4 / 28.4, B 29.7 / 29.9; all 60 swaps a 2 s made by the render thread; 1 vs 1 29.3 against 29.9, but its fight went another way at 64-78 s (a screen-filling effect, ~49 blended screens a picture against ~17, the chip ~43 ms a picture: the graphics chip's limit, not the drawing thread's): no clear gain, stays 0 | `runs/r1`..`r1v1` |
| 2026-11-16 | `267c865` (dev, BT4 fork; wip/upstream: BT4decomp 0.1.39.6 R1 1.2 merged) | Console, vita-agent-use | batch 13: the defaults twice (2 vs 2 stage 0, 120 s), `film=30`, 1 vs 1 | 2 (speed, pictures, health) | 28.9 / 29.1 against tagteam's 28.8-29.1; beam 24.2 / 24.6 against 22.8-23.6 (the new Tag HUD has fewer shapes: no full-width enemy gauge, the target's own bar over it); 1 vs 1 29.9; no crash; 16 film pictures and the console's screenshots right (new radar top right, own gauge top left, allies bottom left, target ring and bar); merged into tagteam | `runs/up0`..`upv1` |
| 2026-11-17 | `b7724ee` (dev, BT4 fork, BT4decomp R1 1.2 merged) | Console, vita-agent-use | the soak (`soakab`, 1 vs 1 duels, stages 0-22, 45 minutes, 46 battles) with sprrun and rtswap at 2 (the day's other switches on on both sides) | 2 (speed, health) | fps B-A +0.1 (29.6 / 29.7, at the cap); the lowest 2 s stretch better on 18 of 23 stages (stage 12 23.4 -> 26.4, stage 14 24.4 -> 26.7, stage 8 25.5 -> 28.0); the drawing thread less on 21 of 23 (stage 5 756 -> 602 ms every 2 s); no crash, no watchdog (the fetched hang.txt is an older run's), heap at most 57.0 MB of 80: rtswap on by default | `runs/soak2` |
| 2026-11-13 | `e894c7a` (release, BTTD00011, built with `docker_build.sh`) | Console, by hand (the developer) | the defaults | 2 (play) | The release without the log's samples of the picture's draws (`report=1` brings them back) and its log kept under 1 MB (`log-prev.txt`): played, works. Verdict: in the pull request | (by hand) |
| 2026-11-12 | `3f8b443` (dev) | Console, vita-agent-use | `texbind=1 esdirect=1 seavtx=1 updestlog=1`, soak, stages 12, 22, 4, 210 s | 2 (counters, pictures) | texbind after the fix (a block written pixel by pixel had kept its last owner: a palette sharing its block stayed bound; small destinations now checked by their blocks' writes too): **0 wrong** in ~15,000 checks, 20,000-38,000 textures / 2 s found by their names; no crash; pictures right. Verdict: `texbind`, `seavtx` on by default, `esdirect` off | runs/next2 |
| 2026-11-12 | `99dfa15` (dev) | Console, vita-agent-use | `upprof=1 texbind=2 esdirect=2 seavtx=2 dpjoin=2`, soakab, stages 12, 4, 22, 420 s | 1-2 (counters, cost, pictures) | fps at the cap on both sides (B-A +0.6, 0.0, -0.2). Texture lookups (upprof, ms / 2 s, A then B): 157 -> 118 (4), 226 -> 186 (12), 129 -> 114 (22); drawing thread 890 -> 779 (4), 840 -> 818 (12), 734 -> 757 (22). texbind **0.8-1.5% of its checks wrong** (fixed: the line above). seavtx on Kame House: 63% of the corners' work found done, the surfaces 4.9 -> 4.5 ms a frame. dpjoin: never a pair (removed). esdirect: 6,500 call packets / 2 s, the list reader -34, +21, +87 ms / 2 s (no gain seen) | runs/next1 |
| 2026-11-12 | all on by default (dev) | Console, vita-agent-use | the defaults (native, nativetex, updest, dpfast, fxrun, fxlow, dpsplit on), soak with duels and three against three (changes of fighter), stages 4, 12, 20, 22, 23, 13, 10 minutes | 2 (health, speed, pictures) | 10 battles, no crash, no hang; named textures found changed 0; texture keys from a destination wrong 0 (289 reports); the seam's table never full. fps 29.3-29.6 on every stage (lows 22.2-25.3); the list reader 5.4-8.5 ms a drawn list. 24 screenshots right. Verdict: on by default | `runs/final` |
| 2026-11-12 | dpfast, fxrun (dev) | Console, vita-agent-use | `native=1 nativetex=1 updest=1 dpfast=2 fxrun=2`, soakab, stages 4, 12 | 1-2 (cost, pictures) | Drawing thread 765 -> 700 and 820 -> 763 ms / 2 s; GS primitives on Kame House 236 -> 206, GS packets 127 -> 102; ~1,000 particles / 2 s decoded as runs. A first run (all1) showed the stage's polygons drawn huge: dpfast's stage uniform block cleared three rows of the screen matrix (fixed before fast1; pictures right since). Tried and taken out: each kept group remembering its texture (never found the same: the game writes the stage's materials into one place and a fighter's palettes into one); the triangle runs into the additive groups take none in a fight (they write the alpha the fighters' outline reads, as the full way finds too) | `runs/fast1` |
| 2026-11-12 | updest (dev) | Console, vita-agent-use | `native=1 nativetex=1 upprof=1 updest=2`, soakab, stages 4, 12 | 1-2 (counters, cost, pictures) | upprof (every upload and texture lookup timed, ms / 2 s, A then B): stage 4 uploads by the block 233 -> 175-184, texture lookups 209 -> 126-130, small uploads written 62 -> 70-76, all 551 -> 386-432; stage 12 all 453 -> 433. 9,000-10,600 texture keys / 2 s from a destination, 0 wrong in ~2,400 checks (every 16th). fps the same; pictures right. The rest of the lookups' cost is their number (~30,000 / 2 s: every kept group's and strip's state) | `runs/dest2` |
| 2026-11-12 | nativetex (dev) | Console, vita-agent-use | `native=1 nativetex=2`, soakab, stages 4, 12 | 1-2 (counters, speed, pictures) | ~6,000 uploads / 2 s numbered from their names, 0 found changed in the checks (every 128th), 120 named at a fight's start. The fourth core's helper 383 -> 28, 181 -> 52 ms / 2 s; the drawing thread's uploads (its own clock) 2156 -> 1827, 1505 -> 1345 us a blank; the list reader's GS packets +50 ms / 2 s (the first readings and the checks: it has the room). fps the same. Pictures right (6 B screenshots). Default off for now | `runs/tex1` |
| 2026-11-12 | `a5c4005` (dev) | Console, vita-agent-use | `native=2`, soakab, stages 4, 20, 12, two rounds (A/B, then B/A) | 2 (speed) | The gain over main (fxlow, dpsplit on both sides): fps the same (29.2 / 29.2, at the 30 cap); drawing thread 828 -> 560, 891 -> 687, 778 -> 594 ms / 2 s (-23 to -32%); list reader 314 -> 106, 430 -> 151, 555 -> 212 (-62 to -66%); the seam's table never full (131 reports). With native=1 the drawing thread's time is GS primitives 100-275, uploads 125-180, GS packets 100-180, kept parts drawn 115-200 ms / 2 s; the render thread 830 ms / 2 s on Kame House (20,700 draws) | `runs/gain-native` |
| 2026-11-12 | `513c136` (dev) | Console, vita-agent-use | `fxlow=2`, soakab, stage 12 (Kame House) | 2 (pictures) | Kame House is stage **12** (the island, the pink house; 22 is a canyon with a river, 23 a plain with a lake, 13 Namek at night, 16 space, 15 rocky mountains in rain). Splashes, auras, a ki blast over the sea drawn right with fxlow; an aura cut straight where it meets the sea is there with fxlow=0 too (the game's depth test) | `runs/kame-fx` |
| 2026-11-12 | `f5dccab` (dev) | Console, vita-agent-use | `native=1 dpsplit=1`, one fight each, stages 12, 15 | 2 (speed, parts) | Kame House 29.5 fps (the November 2026 benchmark before the detailed stages' fixes: 24.9). The sea costs the game thread ~8.5 ms a frame (972 triangles looked at, ~900 drawn through the ordering table), the drawing thread's GS primitives ~230 ms / 2 s, the render thread 826 ms / 2 s (20,800 draws): no thread is full, so native.md's step 1 (the sea) would give processor time, not pictures a second | `runs/kame` |
| 2026-11-12 | `513c136` + dpsplit (dev) | Console, vita-agent-use | `native=1 dpsplit=1`, one fight each, stages 20, 13, 16 | 2 (counters) | "not kept for another reason" 0 (before: "indices beyond a bank" in every report of a first fight); kept parts walked 1-20 ms / 2 s in first fights (stage 20: 20 against 272-467); the seam's line: 9,000-25,000 calls / 2 s sent from their copies, "to the cache (table full)" 0. dpsplit on by default | `runs/check-split` |
| 2026-11-12 | `513c136` (dev) | Console, vita-agent-use | `native=2`, soakab, stages 20, 23 | 2 (speed, pictures) | fps B-A: 20: -0.4 (29.4 / 29.0), 23: +1.1 (28.1 / 29.2), all +0.3, near the 30 cap. List reader 500 -> 229 ms / 2 s (20) and 469 -> 189 (23); drawing thread 770 -> 649 and 877 -> 756. Pictures (8 B screenshots): stage, animated objects and fighters whole. The seam's own line was not printed (inside the processor counters' block; fixed in f5dccab). Verdict: works, B no slower; default still off until a fight with a transformation is looked at | `runs/ab-native` |
| 2026-11-12 | `8d19e3f` (dev) | Console (PCH-1004, 3.65, CapUnlocker) through vita-agent-use | `fxlow=2`, soakab, stages 4, 20, 22 | 2 (speed, pictures) | Shaders built; "draws without their shader" 0 in all 110 reports; B drew 1-3 groups smaller per 2 s (3-9 screens). fps B-A: 4: -0.4, 20: -1.0, 22: +0.4, all -0.3, within the order of the battles: the first fight on stage 20 in each round (A in round 1, B in round 2) walked its kept parts 270-470 ms / 2 s against ~30 in the second (`513c136` logs why). Render thread on 22: 778 -> 665 ms / 2 s, draws handed over 17,200 -> 13,800. Pictures (8 B screenshots: blasts, dust, auras): drawn right, in front of and behind the fighters. Run stopped by hand at ~9 of 10 minutes (11 fights). Verdict: stays on | `runs/ab-fxlow` |
| 2026-11-17 | `88740f4` (dev, BT4 fork, rtswap on) | Console, vita-agent-use | batch 15: the defaults twice, fxlowmin=1, fxlowmin=0.5 twice (2 vs 2 stage 0, 120 s), fxlowmin=0.5 with `film=30` and in 1 vs 1 | 2 (speed, pictures) | defaults 29.0 / 29.2 (beam 24.1 / 25.0); fxlowmin=1 29.3 (47 groups drawn smaller in the run), 0.5 29.3 / 29.3 (221 groups); 1 vs 1 29.9 either way (531 groups); the pictures right: the demo fights are not bound by the graphics chip, so fxlowmin stays 2 until a fight that is (a summoned dragon, the Super Spirit Bomb) can be run again and again | `runs/v0`..`l05v1` |
| 2026-11-17 | `a5a7810` (dev, BT4 fork; demoult) | Console, vita-agent-use | batch 16-17: the heavy fight (`demoult=4`, Goku (End) 3 against Goku GT SSJ4 10, the Super Spirit Bomb and the Dragon Fist in turn every 15 s, health 40%), twice, with fxlowmin=0.5, with `film=30` | 2 (speed, pictures) | the six uses started within 2 blanks (one 26); the same run twice gives the same figures stretch by stretch (29.61 / 29.65 in the heavy stretches, at least 27.9 at the Super Spirit Bomb, ~45-49 blended screens a picture, the chip 32-39 ms); fxlowmin=0.5 drew 448 groups smaller and was no better (at least 26.9): fxlow is not the way here; the render thread busy ~1.6 s of 2, the drawing thread waiting for it ~560 ms at dp_free_pending (-> dpfreelate); the film's pictures right | `runs/hx0`, `hx0b`, `hxl`, `hxf` |
| 2026-11-17 | `c55999f` (dev, BT4 fork) | Console, vita-agent-use | batch 18: dpfreelate=1 against 0 in the heavy fight (twice on), 2 vs 2 twice interleaved, 1 vs 1 | 2 (speed) | heavy fight: off at least 28.1 / 28.4 (39 s, 71 s), on 29.9 / 29.9 there, at least 29.2 / 29.4 in the heavy stretches, no wait for the render thread left (every picture had no part to free: the wait was for nothing); 2 vs 2 off 29.3 / 29.3, on 29.2 / 29.0 (the second went another way in the fight), beam even; 1 vs 1 29.9: on by default | `runs/hd0`..`fl1v1` |
| 2026-11-17 | `d67505b` (dev, BT4 fork) | Console, vita-agent-use | the soak (`soakab`, 1 vs 1 duels, stages 0-22, 45 minutes) with dpfreelate at 2 (rtswap, sprrun and the others on on both sides) | 2 (speed, health) | fps B-A +0.0 on the whole (29.7, at the cap; stage 22 29.7 / 29.0 in one stretch, its lowest stretch still better); the lowest 2 s stretch better on 20 of 23 stages (stage 20 24.8 -> 26.7, stage 5 26.2 -> 28.0); no crash, no watchdog, heap at most 57.4 MB of 80: dpfreelate stays on | `runs/soak3` |

## Rules for whoever goes on

- Every change behind a `bt3.txt` switch, `0` the old way, `1` the new, `2` the new in the soak's B battles only
  (`gVitaAbSide == 1`), and a line in [development.md](development.md)'s table; counters for the log's report.
- Changes to the game's text (`src/`, `include/`) only under `#ifdef PORT_VITA`, written so that the PS2 build is
  unchanged (`docs/port/README.md`: the matching build must stay byte-identical).
- Before a commit: `port/vita/tools/check_syntax.sh` clean (and `WARN=1` on the lines changed).
- In the commit message and in [native.md](native.md) / this file: what was checked and how, and what was not.
- The changelog's "Unreleased" section for what players see; default-off development switches under "New".
