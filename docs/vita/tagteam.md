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
- Next, larger: the kept parts' uniform blocks worked out on the render thread; textures made through the render
  thread's ring without waiting for it; the GIF packets decoded ahead by the list reader (studied, not done).

## Tools made for it

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
