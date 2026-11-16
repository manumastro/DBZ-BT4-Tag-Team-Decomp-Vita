# Tag Team offline on the Vita

A study (2026-11-15) of BT4decomp's Tag Team as merged into this tree, for one goal first: **one player on the Vita
with CPU fighters, 2 vs 2**, offline. 3 vs 3 to 5 vs 5 and Free for All only if the console's measurements allow
them. Online Tag Team (between Vitas, or against PCs) comes after, and starts from the same work
([online-bt4.md](online-bt4.md), its plan's step 7 and 9).

## What BT4decomp has

- **Tag Team is BT4decomp's own** (new code in `src/battle/tag_team_*.inc`, `battle_work.c`, `btl_pool.c`,
  `btl_obj.c`, `port/src/tag_team.c`; fighters past the second live in `sTagSides`, `include/battle/tag_team_native.h`).
- **On the PC it has no offline mode a player can open.** Main menu > Tenkaichi Budokai Online > Tag Team > Create
  Room goes through the signaling server and the data check (`src/menu/port_tbo.c:1110`: the local room,
  `Port_TagConnect(NULL)` -> `local_room` in `port/src/gs/tag_team_net.c:552`, is reached only with
  `BT4_RENDER_TEST`). A room of one player and CPUs is still an online room whose input is scheduled locally
  (`solo_cpu_fight`, `tag_team_net.c:429`). BT4decomp's issues #24 and #47 ask for an offline mode.
- **The rules**: health 100%, speed 114% (34.2 steps a second, `Port_SpeedMilli` 1140 while a Tag battle runs,
  `port/src/plat_stub.c:151`), time 60 / 90 / 180 / 240 s or none, CPU difficulty Very easy to Very hard (CPU
  levels 0 / 6 / 13 / 21 / 29, `TagBattle_ConfigureLocal`, `battle_work.c:227`), characters chosen in turns, related
  characters not on the same team, stage and music on the Team Battle's selector.
- **Targeting**: L3 moves to the next living enemy (`src/battle/btl_input.c:575-588`). On the Vita, L3 is the rear
  touch pad or a front corner already (`port/vita/vita_touch.c`, the controller page's "Touch Buttons").
- **The HUD** (`src/battle/tag_team_hud.inc`) is drawn by the code: allies' column, enemies' cards, target marks, a
  Dragon Radar. Its boxes and the radar are `Ps_Quad` / `Ps_Rect` (drawn on the Vita); its words are
  `Ps_Text` / `Ps_TextExOuter`, TrueType text the Vita does not draw yet.
- **The menus** (`port_tbo.c`: hub, room setup, room) draw `port/tbo/ui/*.png` pictures (`TBO_PSM`) and TrueType text
  (`tbo_text.c`), neither of which the Vita's renderer has. Some pictures have authors to credit
  (`port/tbo/ui/CREDITS.md` upstream; `btn_*` CC BY 4.0).
- **Upstream is rewriting Tag Team** (the maintainer on BT4decomp's #10 and #47, with TheMufti: "it will change a
  lot"). Keep our side small and separate, so a merge of the new version costs little.
- **Open upstream**: #52, frame drops and crashes in full 5 vs 5 with many ki blasts, offline too, on a desktop PC.

## Where the Vita stands

- The battle side is **already in the Vita build** (`tag_team_*.inc` are included by `battle.c`, `battle_work.c`,
  `btl_char_mgr.c`, `menu_e_b.c`). It is off at run time: `TagBattle_Active()` is `sTagLocal || sTagActors > 2`
  (`battle_work.c:69`), and `TagBattle_ConfigureLocal` gives up because the Vita's answers for `tag_team_net.c`
  (`port/vita/vita_bt4.c`) say capacity 0.
- The main menu has no Tag Team entry on the Vita (`src/menu/menu_a.c:315`, `PORT && !PORT_VITA`).
- **A test that needs no menu**: `BT4_TAG_ACTOR_TEST=4` (6, 8, 10) makes every fight load that many fighters, the
  extra ones CPUs (`TagBattle_Prepare`, `battle_work.c:268`). The development build passes `BT4_` lines of
  `bt3.txt` to the game's environment (`read_settings`), so this measures 2 vs 2 and 5 vs 5 on the console first.

## The costs on the Vita (estimates from the code, to be measured)

| | 2 vs 2 | 5 vs 5 | Where |
|---|---|---|---|
| Memory beyond 1 vs 1 (newlib heap) | about +11 MB | about +41 MB, plus the characters' packs | `btl_pool.c:37-46,117-147`, `btl_obj.c:1189-1327`, `bobj_a.c:2328-2354`; `Port_GameBigAlloc` -> newlib heap, 80 MB (`vita_main.c:47`), 33-44 MB used in 1 vs 1; a failure is fatal (`vita_mem.c:187`) |
| Game thread (10-12 ms of 33 in 1 vs 1) | about 14-19 ms | about 26-41 ms | AI per CPU seat, skeletons, HUD; hits are per pair (linear) |
| Texture table `MAX_TEX 384` | 333-348 used in 1 vs 1 | likely full: rebuilds, untextured draws | `vita_vgl.c:107,2302-2322` |
| Fighters' vertex cache `FT_VERTS 65536` | | likely full: slower path | `vita_vgl.c:226,7921,8030` |
| Upload store `UP_MAX 3072`, 16 MB | 407 uploads, 6.1 MB in 1 vs 1 | evictions | `port/src/gs/gs_core.c:188` |
| Heap slots `BTL_RES_SLOT_MAX 12` | | 8 extra fighters plus the stage's objects: at the limit (`BtlRes_Request` -1) | `btl_obj.h:209` |
| Pools' overflow | | `exit(3)` | `btl_pool.c:68` |

Speed: the Vita paces the game by the display's vblanks (`vita_main.c:487-503`), 30 steps a second at most. Offline
Tag Team runs at 100% on the Vita (`Port_TagRenderActive` 0 there); 114% matters only against PCs online.

## On the console (2026-11-15, PCH-1004, 3.65, development build)

- **Measured** (`BT4_TAG_ACTOR_TEST=4`, the demo fight on stage 4, the Tenkaichi Budokai stadium, against the same
  fight 1 vs 1; samples every 2 s after the first 15 s):

  | | 1 vs 1 | 2 vs 2 |
  |---|---|---|
  | speed, average | 99.1% | 93.9% |
  | pictures a second | 28.9 | 24.5 |
  | 2 s samples under 90% | 1 of 56 | 27 of 119 |
  | newlib heap | 29 MB | 40 MB |
  | texture table, peak | 321 of 384 | 347 of 384 |

- **A hang, fixed**: the first 2 vs 2 runs stopped at 105 s and 77 s, in a special move. A crash dump taken on
  purpose (`hangdump=8`, below) showed the sound thread spinning in the SDL shim's `SDL_LockSpinlock` on a lock the
  game thread held on the same core (BT4decomp's fighters' sounds by distance, `gs/snd_spatial.c`); the game thread,
  of lower priority, never ran again. The shim's lock now gives the processor away after 100 tries
  (`port/vita/sdlshim/SDL3/SDL.h`). Then 4 minutes of the same fight without a hang. It can happen in 1 vs 1 too.
- **The whole way through the menus**: main menu > the new entry > the setup screen > the game's team character
  select (the CPUs' fighters chosen by the player, costumes, options) > the stage > the fight, with the allies'
  column, the enemies' cards and the radar > the results ("GANA EL EQUIPO 2": Rematch, Back to character select,
  exit) > Rematch > the pause's "back to the main menu". No error.
- **Not yet**: the main menu entry has no label (the PC draws "Tenkaichi Budokai Online" in TrueType) and the
  online mode's description under it; the results' third choice still reads "Tenkaichi Budokai Online"; the HUD's
  and the selection's words are not drawn (TrueType); 3 vs 3 and up not run; a whole match played by hand.

## Speed-ups for 2 vs 2 (2026-11-15)

Where the time went (the same 2 vs 2 fight against 1 vs 1, per 2 s): the drawing thread (core 2) was full (idle 99
ms against 189; the average drawn list 40 ms against 33.7), and so was the game's core (game thread and sound, 95%).
Tried, each behind a `bt3.txt` setting, then all together on the console (`BT4_TAG_ACTOR_TEST=4`, stage 4, about
3.5 minutes each; the fights differ from run to run, the CPUs' choices are random):

| Run | Settings | Speed | 2 s samples under 90% | Pictures a second |
|---|---|---|---|---|
| A | all off | 94.3% | 22 of 95 | 25.0 |
| B | palexact, maxtex=768, audiocore=1, vjcore=1, flname=1 | 96.3% | 11 of 95 | 23.9 |
| C | B without vjcore | 98.0% | 3 of 96 | 24.8 |
| E | palexact, maxtex=768, audiocore=1 (4.5 minutes) | 96.2% | 14 of 124 | 24.2 |

- `palexact` (on by default now): the textures made every 2 s went from 180 to 95; the stadium's materials looked
  right in the pictures.
- `audiocore=1` (on by default now): the sound thread on the list reader's core; the game's thread got its core.
- `maxtex=768` (now the default): the table was not full in these runs; room for 3 vs 3 and up.
- Left out: `vjcore` (the strip thread on the fourth core: interrupted there, its work fell back on the drawing
  thread; worse), and a quick way kept across GS memory changes by the textures' names (it never applied: the quick
  way ends for other reasons; removed).
- Found on the way: the program faulted inside SceGxm at every exit (the render thread still at work as the process
  ended: `exitafter`, and START + SELECT): the drawing and render threads now stop first (`quit`); no crash dump in run
  E. One hang in run D (the list reader in the VU1 interpreter for 10 s, inside a DMA chain; a chain is about 4,300
  tags at most): the chain's limit on the Vita is now 20,000 tags, and one that reaches it is logged.
- Then (same day): `upplan` (the large uploads' blocks' places remembered; on by default: uploads 269 -> 244 ms
  per 2 s, 98.4 -> 98.7%, 0 wrong); `dprt` (the kept parts' blocks on the render thread: worse, 14.5 pictures a
  second, the render thread became the limit; off).
- A rare stop in the 2 vs 2 demo fight (pairing 0, about 105 s in, in 4 of 9 runs): the list reader inside the VU1
  interpreter for more than 10 s (normally the interpreter is not used at all in these fights), once a crash in
  vif() copying from a garbage address instead. Measured, not the cause: the effects' packet memory (65 of 460 KB at
  most), the display list (366 of 1024 KB), DMA chains (4,263 tags at most, none came back on itself). Now: a list's
  interpreted VU1 work capped at 20 million instructions (the rest left out, the program logged), and a loop in
  the aura flames fixed (`EftAura_StepFlames`: a flame the full pool could not replace was looked at for ever).
- 2026-11-15, the same stop seen three more times, always at list 2908: once the cap never came (one run of a
  program of 345 instructions with a loop counter of ~50,000, its kicks keeping the interpreter at ~2 million
  instructions a second, so a run alone took seconds), once the cap came after 10 s (too late for the watchdog), once
  a crash in vif() reading 0x4d5b4d5a. So the list's DMA chain itself is bad there: a tag pointing into data that
  is not a tag. Now: every DMA tag and the data it points to are checked to be in the process's memory (the ranges
  of the last memory blocks kept, a system call only on a miss); a list whose chain leaves it ends there, logged
  with its last 8 tags; the interpreter's cap is 2 million instructions a list (no list of a fight or the menus uses
  it at all) and a quarter of a second for one run. The next occurrence names the tags; who builds them is the
  next step.
- 2026-11-15, the guard caught it (list 2909): `Vu1Pkt_LoadProg0`'s REF and CNT, then a model part's CALL
  (`Vu1Pkt_CallProg0`, `ObjDraw_DrawParts`) to 0x88d40eb0, below the game heap, in a block of the C library's heap
  (`Port_GameBigAlloc`: Tag Team's model files), where a file's header lay instead of the part's chain. Tag Team
  reloads a fighter's files in the fight (`BtlRes_Reload`; the old buffers are freed by the next reload,
  `BtlRes_TagDiscardReload`, and the new ones allocated and read at once, often at the same place), while the list
  reader is one list behind the game: the list handed over before the free still called into the old model. On the
  PS2 the DMA was over long before a disc read could end. Now `Port_GameBigFree` waits for the lists handed over and
  the drawing thread's queue (`Port_GsSync`) before it gives a block back; logged ("a block outside the game heap
  ... given back") and counted in the report. On the console: 8 runs of that fight with it, the guard never caught a
  tag again (before: the stop in about 4 runs of 9); one block given back at about 106 s in 2 of the 3 runs that log
  it (the third had no reload); the speed the same (98.1-98.4%, 26.1-26.7 pictures a second, 15-101 s).
- 2026-11-15, measured with the processor's counters (libperf, loaded by the development build itself, below): the
  drawing thread takes ~35 ms a drawn list in 2 vs 2, spread over many parts, none above 3-4 ms (the GIF packets'
  registers ~9 ms in all, the GS primitives' states and vertices ~8, the uploads ~6.5, the model strips ~6, the kept
  parts ~5, the picture's end ~4); it runs 0.74 instructions a cycle with 20 instruction-cache misses and 22
  mispredicted branches per 1000 instructions (the game's thread 0.50). `rtswap` (the swap and the next picture's
  clear made by the render thread) took the drawing thread's waits for it from ~80 to ~10 every 2 s but the picture
  rate only from 26.81 to 26.98 a second: off. A profile-guided build (`BT3_PGO`: counts from the 2 vs 2 demo fight
  and a 1 vs 1 soak over 8 stages, 10 minutes) did not help: 26.67 -> 26.38 pictures a second, the drawing thread
  0.74 -> 0.72 instructions a cycle, its instruction-cache misses 5.7 -> 6.2 per 1000 instructions (the thread's
  whole-stretch figures; one block's 20 above was a heavy moment); not used. Per drawn list the drawing thread runs
  ~12.5 million instructions in ~17 million cycles: data-cache refills ~16% of its time, instruction-cache ~10%,
  mispredicted branches ~6%. Next: the GIF packets decoded ahead by the list reader (busy only ~16 of every 33 ms).

## Tools made for it

- 2026-11-15, tried and dropped: the GIF packets decoded on the list reader (`gifpre`, not kept in the tree). The
  list reader kept a copy of the registers that make the vertices (PRIM, RGBAQ, ST, UV, XYOFFSET, the vertex queue,
  the GIF tag), followed through every record it sent, the kept calls' records too, and sent the drawing thread
  register writes, primitives with their vertices made and runs of triangles instead of the packets. Exact: a check
  mode drew from the packets and compared each primitive and the registers after them, 0 differing in a 200 s run.
  But slower: 22.4 pictures a second against 26.9 (2 vs 2 demo fight, 15-101 s). The drawing thread's time in the
  packets went from 256 to 246 ms every 2 s, while the list reader's went from 388 to 1,106 (its idle 659 -> 138 ms):
  the decoded records were larger than the packets (~1.2 MB a list), and following the kept calls meant reading their
  records again. Even without the registers' tail and the kept calls (a measurement only, not exact) the drawing
  thread gained ~0.6 ms a list (packets 256 -> 172 ms every 2 s, but the primitives 337 -> 372: their vertices no
  longer in the cache) and the list reader lost ~4. The drawing thread's GIF time is bound by memory, not by the
  parsing: moving the parsing does not move the cost.
- 2026-11-15, the GS primitives' states worked out again (the report's new line "states worked out, why their place
  did not give them"), 2 vs 2, every 2 s: ~2,600 "the table's generation changed" and ~950 "empty" are the primitives
  not drawn (~96 a picture, all of them "16-bit or depth target", rejected in the first lines of `state_of_`: cheap,
  a cache of the rejections would save ~0.1 ms a list); ~1,400-1,900 a texture's pages written in the picture
  (needed); ~1,300 their place taken by a state of a picture before; plus ~2,200 on the work targets' way (kept for
  the picture only). Those that cost (5,000-8,000 cycles each) come to ~95 a list, ~1.3 ms, of which perhaps half
  could be saved. The large uploads' writing ("upload: written", ~134 a list, ~2.2 ms) is the pixels themselves,
  already planned (`upplan`) and written lazily. What is left on the drawing thread is a long tail of parts under
  ~1 ms each; the 2 vs 2 fight lacks ~2.5 ms a list for 30 pictures a second.
- 2026-11-15, the 2 vs 2 demo fight on other stages (same build, 30-120 s): stage 4 98% speed, 26.5 pictures a
  second; 24 98%, 25.8; 12 (the island with the sea) 87%, 23.9; 0 (the grassy plain) 83.5%, 21.1. Two limits: the
  drawing thread is full on every stage (~37 ms of work a picture), and on stages 0 and 12 the game's thread too (the
  game slows down). The game's thread by part (`gzones=1`), stage 0, ms a step: 1 vs 1 19.0 (29.2 pictures a second,
  99.6%), 2 vs 2 31.2: the fighters' logic 3.6 -> 7.7, the scene drawn (effects) 4.1 -> 6.5, `snap_make` (the port's
  copies of the list's pieces at the handover) 1.9 -> 2.9, the fighters drawn 1.4 -> 2.4, the scene 1.2 -> 2.3, the
  HUD, objects and computer players 1.5 -> 3.6. Of `snap_make`'s 2.9 ms, 1.4 are its images' sums (~380 images a
  list, read whole each time), 0.9 its walk of the chain, 0.1 its copies.
- 2026-11-16, `snapcut` (development build, default 0, kept off): `snap_make` cut down, an image not summed when its
  sum was the same 32 lists running (one list in 16 summed all the same), and the model calls (`CALL` tags) not
  walked (no image was ever found inside one). Stage 0, 2 vs 2: `snap_make` 2.9 -> 1.5 ms a step, but no picture
  gained (22.0 -> 21.4 a second: the drawing thread is the limit). Stage 12 with `snapcut=3` (the sums made all the
  same and compared): of 1,142,842 sums skipped, 17 were of an image that had changed (read in place, not copied:
  a wrong picture possible for one frame), so the skip is not safe as it is. With `snapcut=1` on stage 0 the graphics chip crashed
  (`psp2core-*-GPUCRASH`, no CPU thread at fault) ~97 s in, during a super attack (the battle clock held at ~870
  ticks, then the scene's effects), at clock 1205; the same fight with `snapcut=0` (the demo is deterministic: the same
  health at the same ticks) went past that point. The last log line before the crash read 5 MB of video memory free
  (37 MB a minute before); the run without snapcut had no reading there. One crash, one run each: the cause is not
  known; with the 17 misses and no picture gained, snapcut stays off and is not worth more runs.
- 2026-11-16, the effects on the game's thread (the log's "effect tasks' update / draw callbacks" lines, from
  `port/src/port_fast.c`'s wrapped `BtlTaskList_Update` / `_Draw`; their run-time addresses are the map's plus
  0x10000, plus 1 for Thumb), stage 0, 2 vs 2, us a frame: `EftAuraTask_Draw` (the fighters' auras) 3,800,
  `EftPtcl_Draw` (particles) 1,070, `EftAuraTask_Update` 920, `EftPtcl_Update` 350, `EftAnimPart_Draw` 350,
  `EftSurf_Draw` 320. The aura projects the four corners of each flame (up to 50 a fighter) with
  `Vu0Cur_ProjectPoint`, which had no fast stand-in (~5 us a call in the library's). `port_vufast.c` now has
  `Vu0Cur_ProjectPoint`, `..Points`, `..PointStq`, `..PointsStq` (the particles, ribbons, quads), checked at start
  against the library's on 3,000 operand sets each like the others (all 34 stand-ins used). The same fight (the demo is
  deterministic): the game's thread 30.2 -> 27.4 ms a step, the scene drawn 5.9 -> 3.2, the aura's draw 3.8 -> 2.1;
  the game's speed from 30 s on 93.6% -> 97.4% (stretches under 90%: 11 of 43 -> 5 of 45); pictures 21.3 -> 21.2 a
  second (the drawing thread is the limit). Runs `port/build/runs/tp0`, `pp0`.
- `vita_agent_run.py` after that crash: the console refused to take its settings back while it wrote the dump
  ("stale"), and the runner stopped, leaving the run's `bt3.txt` on the console for hours. It now puts them back in
  a `finally`, tries again for ~3 minutes with a fresh transfer state, reads them back to compare, and exits 5 if it
  could not; it refuses to start (exit 4) when the console's `bt3.txt` holds `exitafter=` or `mode=` (an earlier run's
  left over, which it would otherwise keep as the console's own).
- 2026-11-16, the states of the fighters' and effects' strips (the log's "strips' states (program 0 / 4 / 6)" line):
  in the effect bursts of stage 0's 2 vs 2 the drawing thread's zone 5 ("strip: state") reaches ~15 ms, 6,535 strips
  a list, most of them program 4 strips reading a work target, whose states are worked out each time: the work
  targets' memory (`sWtC`) keeps only states with a texture transform and `pad_[0] != 0`, and these fail on
  `pad_[0] == 0` ("not a picture as texture", the work-targets line's "not:" reasons). Two settings tried, both
  default 0 and kept off:
  - `wtrem=1` (`3` the check): the work targets' memory keeps states without a transform too. As it is it takes
    none (the same `pad_[0]` test); it would need that understood first.
  - `stl4=1` (`3` the check): the "state of the call before" shortcut keeps the last four calls' states, not one.
    It took 13,000-16,000 states a stretch; the check compared ~60,000 at and after the fight's start, none
    different.
  The graphics chip crashed (`psp2core-*-GPUCRASH`, no CPU thread at fault, the console switched itself off after
  the dump) at the fight's start (frame 776, ~35.5 s) in all three runs with either setting on: `wtrem=3`+`stl4=3`,
  `stl4=1`, `wtrem=3` alone; never in the ~16 runs without them (3 short ones made for this, `fs_d1`-`fs_d3`) nor in
  the one with `stl4=3` alone. Video memory was not short (42 MB free). The cause is not known: neither setting
  changes what is drawn before that frame, and no report covers the frame itself; the next step would be a line per
  frame from 770 to 780 (states taken, textures let go, memory stores). Until then neither may be used. Runs
  `port/build/runs/wc0`, `so1`, `fs_*`; dumps `port/build/dumps/gpucrash-1794812820`, `-1794820760`, `-1794825400`.
- The log's "memory free, the lowest since the last report" line: video and main memory sampled every 8 pictures
  (the lowest kept), not only every 60.
- `vita_agent_run.py` after the console switched itself off in a run: every vita-agent call now has a time limit (3
  minutes; 10 for the run's start with its upload and the log's download), and putting the settings back waits at
  most 4 minutes for the console to answer before giving up ("THE CONSOLE IS NOT ANSWERING", exit 5). Before, each
  try hung ~10 minutes, for hours. It exits 6 when the log has no `vita: exitafter:` line (the run ended otherwise).
  Seen working in run `fs_w3`: the console off after a crash, switched on again, the settings put back and read back.
- `zones=1`: the zones of the drawing thread and the list reader (one drawn list in 8) timed by the system's clock
  when libperf is not there (a reading costs ~0.7 us, so the small zones come out too large; libperf's counter costs
  ~95 cycles a mark).
- libperf without touching `tai/config.txt`: the development build loads `libperf.skprx` itself from its app folder
  (`ux0:app/BTFV00008/`, beside `libperf.suprx`) with `taiLoadStartKernelModule`, as VitaShell loads its kernel
  module; it stays loaded until the console restarts (built from https://github.com/shoui520/libperf with the Docker
  image's VitaSDK: `cmake -S . -B build && cmake --build build -j1`; the examples' packages fail, not needed).
  vita-agent-use's `config plan/apply` could not be used: it wants a recovery VitaShell on `uma0:`, which this
  console does not have.
- `vita_agent_run.py --eboot-dir DIR`: another build folder's `eboot.bin` (the profile-counting build);
  `docker_build.sh` passes `BT3_PGO` through.
- `BT4_*` lines of `bt3.txt` go to the game's environment in the development build (`read_settings`).
- `hangdump=<s>` (development build): after that many seconds without a picture the watchdog stops the program on
  purpose, so that the system writes its crash dump (`ux0:data/psp2core-*.psp2dmp`) with every thread's registers;
  read with vita-parse-core and `port/build/vita_dev/bt3.unstripped.elf` (vita-parse-core is Python 2: `util.py`'s
  bytes handling and `elf.py`'s `py3compat` imports need changing for Python 3, pyelftools 0.29).

## The steps

| # | Step | Size |
|---|---|---|
| 0 | `BT4_TAG_ACTOR_TEST=4` and `=10` in a fight on the console against 1 vs 1: the newlib heap's peak, vitaGL's free RAM and VRAM, the texture table's peak and its rebuilds, `FT_VERTS`' peak, the upload store, the game / list reader / drawing thread's ms per frame (`perfsum.py --parts`), the speed %, the loading time | small |
| 1 | `port/vita/vita_tag.c`: the local room only (the `local_room` branches of `tag_team_net.c`), with `port/src/tag_team.c` (plain C) added to `port_sources`; `Port_TagMenuMillis` from the process clock; the network calls stay answered as none (`Port_TagNetwork` 0). `tag_team_net.c` itself stays out (sockets, WebSocket, libdatachannel, SDL threads), so upstream's file merges untouched | small |
| 2 | A Vita setup screen for the room (mode, time, CPU difficulty), then `Port_TagCPUOptions`, `Port_TagConnect(NULL)`, `Port_TagModeOptions`, `Port_TagOpen`, `Port_TagTeams`, `Port_TagSlot` per CPU, `Port_TagCommand(3)`; `src/menu/port_screen.c:684` takes it in place of `PortTbo_Run` under `PORT_VITA`, back to the main menu after | small to medium |
| 3 | The main menu's entry (`menu_a.c:315`) under `PORT_VITA`, its label checked on screen; a `bt3.txt` setting to hide it | small |
| 4 | Fixes for more than two fighters: `btl_input.c:511` (`Save_PortKeys` with `chr->pad` when Tag is active, as the PC's line 522), the clash helper's fighter lookup (`port/vita/vita_gfx.c:175-185`, sockets 0 / 1) | trivial |
| 5 | The HUD's and the selection's words on the Vita (`Ps_Text` through the game's own font, or the TrueType text drawn) | medium |
| 6 | On the console: 2 vs 2 whole matches, results, rematch, back to character select, back to the menu; then 3 vs 3 and up as far as the measurements allow (memory: a larger newlib heap, `MAX_TEX`) | unknown |
