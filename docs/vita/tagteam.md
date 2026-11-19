# Tag Team offline on the Vita

A study (2026-11-15) of BT4decomp's Tag Team as merged into this tree, for one goal first: **one player on the Vita
with CPU fighters, 2 vs 2**, offline. 3 vs 3 to 5 vs 5 and Free for All only if the console's measurements allow
them. Online Tag Team (between Vitas, or against PCs) comes after, and starts from the same work
([online-bt4.md](online-bt4.md), its plan's step 7 and 9).

## What BT4decomp has

- **Tag Team is BT4decomp's own** (new code in `src/battle/tag_team_*.inc`, `battle_work.c`, `btl_pool.c`,
  `btl_obj.c`, `port/src/tag_team.c`; fighters past the second live in `sTagSides`, `include/battle/tag_team_native.h`).
- **On the PC (since 0.1.39.6) Tag Team has a dedicated entry and an offline mode**: the main menu's Tag Team (item 4)
  and the Duel menu's Tag type open its own screens (`src/menu/port_tbo.c`, `PortTag_Run` in `src/menu/port_screen.c`):
  Local Battle (Singleplayer, Two-player, Spectator Mode, Two-player co-op; 1 to 5 fighters a team), Online Battles
  (rooms, quick match, through its signaling server) and Camera mode. Local Battle opens a local room
  (`Port_TagStartOffline`, `port/src/gs/tag_team_net.c`) and the simultaneous character select
  (`TeamSel_TagRun`, `src/menu/tag_team_select.inc`). Before 0.1.39.6 there was no offline mode (issues #24, #47).
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

## BT4decomp's own Tag Team screens on the Vita (2026-11-17, branch `wip/tagui`)

The Vita now runs BT4decomp's own Tag Team screens, as the PC does, instead of its own text setup. What made them
undrawable was only the drawing: the screens' code (`src/menu/port_tbo.c`, `port_screen.c`, `tag_team_select.inc`,
`menu_a.c`, `menu_g_c.c`, `src/battle/tag_team_hud.inc`) was in the Vita build all along; it draws each picture and
each line of letters as a GS sprite whose TEX0 has the reserved format `TBO_PSM` and the picture's number in CBP
(`include/menu/tbo_art.h`), and the PC's renderer gives that sprite the picture (`port/src/gs/gs_draw.c`
`tbo_texture`). So the work is on the renderer's side, not an overlay:

- **The letters are upstream's own code, unchanged**: `port/src/gs/tbo_text.c` (the lines' places, measures and
  pictures), `port/src/gs/menu_text.c` (stb_truetype, the fonts of `fonts.tsv`) and `port/src/gs/menu_labels.c` (the
  main menu's "Tag Team" put together from the disc's own letters, `bt4_main_lettering.inc`) are built for the Vita
  (`port/vita/build.py`). The fonts are in the package (`fonts/`, from `port/third_party/fonts`); the list of the
  menus' labels made from the disc (the PC's "Menu font" setting: the game's own menus' labels redrawn in TrueType,
  "Original" by default) is off: `Vita_GsGetenv` answers `BT3_MENU_FONT` empty.
- **The language (`taglang=1`, the default; branch `wip/tagpc`)**: as the PC. BT4decomp's screens are written in
  English, and its `port/src/gs/online_language.c` (`PortOnline_Text`, called by `tbo_text.c` for every line, by the
  HUD, the character select's lines and the main menu's Tag Team description) gives each line in Spanish when the disc
  is the Spanish B14 Rev2 (`Port_GameProfileEnglish`). It is built unchanged for the Vita
  (`port/vita/vita_onlinelang.c`), so the Spanish disc (the developer's, `ux0:data/bt4/es`) shows the screens in
  Spanish, the English disc in English. The PC chooses no differently: its first-start picker
  (`src/menu/port_language.c`, `port/src/gs/game_profile.c`) chooses the disc and saves the menus' catalog as "en"
  ("the disc's native text; do not overlay a translation pack"); the catalogs of `menu_translation.c`
  (`port/translations`) are used only by its Brazilian Portuguese pack (a third profile over the English disc, a PC
  download), which the Vita does not have. The results' lines and the HUD editor's were already in the disc's
  language (`Port_GameProfileEnglish` read directly).
- **The pictures and lines as textures: `port/vita/vita_tbo.c`**, the Vita's `tbo_texture`: `tbo/ui/<name>.png`
  (libpng; `port/tbo/ui`, in the package as `tbo/ui/`) with the PC's touches (the window's teal, the logo's halo, the
  emblem cut from the logo), a line's picture from `TboText_Pixels` made again when its line changes. The renderer
  (`port/vita/vita_vgl.c` `state_of_`) asks for it when TEX0's format is `TBO_PSM`. The Vita's renderer has no
  mipmaps, so each picture is made smaller once (by halves, the alpha-weighted average the PC's smaller copies use):
  backdrops to 1280 across, other pictures to 640, lines to 128 rows (their capitals 20-24 pixels; they are shown at
  7-20). A texture not drawn for 600 pictures is given back. Setting `tboui` (`docs/vita/development.md`).
- **The main menu's entry is upstream's** (item 4, its emblem and lettering; `bt3.txt tagteam=1`, the default), and
  so is the Duel menu's Tag type (`DuelMenu_TagAvailable`). Online Battles is left out of the screens' sections on
  the Vita (`Tb_TagShellUpdate`, `PortTag_Run`, under `PORT_VITA`): R1 / L1 / CIRCLE go between Local Battle and
  Camera mode. `tagteam=2` keeps the Vita's earlier text setup as a fallback; `tagteam=0` hides Tag Team.
- **In the fight**: the words of the Tag Team HUD (`TagHud_Label`: the timer, "KO", the gauges' figures, the joint
  attacks' and fusions' labels) are drawn now; the fighters' names over the target come from upstream's
  `port/src/fight_panels.c` (built for the Vita: `Port_TagActorName`, `Port_TagAllyHudFeed`) and a name cache filled
  by the character selects (`Port_CacheFighterNames`, a copy of `a11y_game.c`'s in `port/vita/vita_bt4.c`, without
  the screen reader). The floating allies' gauges over their fighters are drawn by the game's own code in
  split-screen only (`TagHud_FloatingAllies`, called from the split branch of `TagHud_DevLayout`: the two-player
  modes, a second controller needed, the game's own split of the screen); `Port_TagAllyHudFeed` only hands them to
  the PC's side panels. On one screen the PC and the Vita both show the allies' column.
- **Camera mode**: the special moves' cinematic cameras (on / off, `Port_SpecialMoveCameras`, now as the PC's
  `mods_game.c`: saved, read by `src/battle/btl_cam.c`), the target selection (below), joint attacks and fusions
  work; so does the free / spectator camera (`tagcam=1`, the default; branch `wip/tagpc`): upstream's
  `port/src/gs/spectator_camera.c` built unchanged (`port/vita/vita_spectator.c`). Its rows: Camera mode (Game camera,
  Free camera, Follow fighter, Fight overview), Fighter (the one Follow fighter follows, 1-10), Camera speed (25-400%),
  Camera zoom (100-300%), Reset camera; saved as the PC saves them. The fight's camera asks it every step
  (`src/battle/btl_cam.c` `BtlCam_PortFreeCamera`, under `PORT_VITA` with the Vita's screen shape instead of the PC's
  saved window shape) and draws from there, the game's own camera untouched. As on the PC, while a camera mode other
  than Game camera is chosen, player 1's pad is the camera's in every fight (START and SELECT still reach the game, the
  pause menu gives the pad back): left stick moves (Free camera) or turns and comes closer / goes back (Follow fighter,
  Fight overview), right stick turns, R / L up and down, R2 / L2 zoom while held (SELECT + R / L, or the touch
  panels), L3 (the touch panels) three times as fast (`port/vita/vita_gfx.c` `PortInput_Spectator`, the pad filtered
  in `Port_PadRead` as `gs_input.c` does). It is meant for Spectator Mode (all CPUs), where the game's own camera
  is used with Game camera. `tagcam=0`: the game's camera always, as before.
- **Target selection** (Camera mode > Target selection: L3, right stick, D-pad): the PC's pad code
  (`gs_input.c`) puts the method and the direction in byte 17 of the pad's report, which `BtlInput_Sample` reads; the
  Vita's pad code does the same (`port/vita/vita_gfx.c` `tag_target`, `PortInput_TagTargetMethod`), saved as the PC
  saves it (`tag_target_method_p1`).
- **The options reach the fight as on PC**: offline, the menus write the settings file (`Port_SettingSave`) and the
  fight reads it (`TagBattle_ConfigureLocal`: `tag_combat<team><slot>`; `Port_TagOption`: joint attacks, fusions,
  revival; `TagBattle_SetLocalHealth`: per-fighter health). `port/vita/vita_tag.c` answers those calls as
  `tag_team_net.c`'s local room does.
- **Not the PC's**: the film behind the screens (`tbo/ui/tag_ui_*.pss`, 30 MB: the Vita shows its poster unless
  `tbomovie` is 1 or 2, see below), the Brazilian Portuguese pack, the online section, the Lua mods'
  split-screen presentation (`Mods_SplitScreen`; the two-player modes' own split is the game's and is there), the
  Lua Modern HUD (BT4decomp's bundled mod: it would need the Lua runtime and the mods' code, `port/src/gs/mods*.c`,
  `port/third_party/lua`, built for the Vita, and their drawing calls answered by the Vita's renderer), touch in
  these screens (pad only; the PC's mouse is only for the online hub's community button).
- **The film behind the screens** (`tbomovie`, `port/vita/vita_tagmovie.c`): the PC's `port/src/gs/tag_menu_movie.c`
  plays `tbo/ui/tag_ui_intro.pss` (6.7 s) once, then `tag_ui_loop.pss` (3.3 s) repeated, 1280 x 720 MPEG-2, decoded in
  software into one texture. The software decoder on one Vita core (batch 29, `tbomovie=2`, still in the code as
  `vita_tagmovie_sw.c`) took 180 ms a picture (about 4 pictures a second against the PC's 23.976) and 37 ms to upload
  one, and the menu ran at 55.4 pictures a second against 59.0 with the poster. So the film is played by the console's
  hardware H.264 decoder (`SceAvPlayer`): `port/vita/tools/make_tagmovie.sh` makes `tag_ui_intro.mp4` and
  `tag_ui_loop.mp4` (960 x 544, H.264 Main, 0.86 + 0.45 MB) from upstream's tracked `.pss` with ffmpeg; the files are
  committed under `port/vita/assets/` (derived from upstream's assets, not game data) and always packaged as
  `tbo/ui/tag_ui_{intro,loop}.mp4` (1.3 MB). The player's frames are physically contiguous memory mapped for the GPU
  (its allocator hook, as vitaGL's `samples/video_playback`); the drawing thread points the film's texture at the newest
  frame (`Vita_VglTexYuv`: a linear YVU 4:2:0 texture on the frame itself) and the chip converts the colour when it
  samples: no copy, no conversion on the CPU. With `tbomovie=1` vitaGL's physically contiguous pool (26 MB, all the console has) is given back to the system while the film plays
  and taken again after (`tbomoviemem=0`, by a function in the port's vitaGL patch; `tbomoviemem=N` leaves N MB aside at start instead, which costs fights memory). `tbomovie=0` the poster (as before, the default); `1` the hardware
  film (the intro once, then the loop, a second player source for each); `2` the software MPEG-2 film (needs a package
  built with `BT4_VITA_TAGMOVIE=1`: the two `.pss`, 30 MB more). Built on `wip/tagmovie`, not run on a console yet. To
  look at: the poster shows for an instant between the intro and the loop (the player's source is changed), the colour
  (BT.601 as the PS2's film; a swap to `_CSC1` is one word in `Vita_VglTexYuv`), the player's start-up delay, the
  memory left to vitaGL, and the log's `tagmovie:` line (every 2 s, see `development.md`).
- **To measure on the console** (memory, speed): the textures held (the report's `tboui` line; the log's `tbo:`
  lines), the cost of a line of letters made on the drawing thread in a fight (the timer's every second; the
  gauges' figures when they change), 3 vs 3 to 5 vs 5 and Free for All 10 (the PC's limits: 5 a team, 10 in all; the
  estimates above), the first opening of the screens (pictures read from the package), the main menu's lettering
  made at its first showing.

### The console test of these screens (to do)

The pictures and fonts are in the package only: install the `.vpk` (vita-agent-use `app install`), not only
`eboot.bin`. The settings (`port/build/sets/`, the agent's `sets/bt4/`):

| Run | Settings | What it shows |
|---|---|---|
| 1 | `tagui_main.txt` (`mode=play`) | The main menu: the Tag Team entry after Duel, its emblem and "Tag Team" in the disc's lettering (the log's `tbo: main menu lettering` lines; `ux0:data/bt4/gamedata/pzs4us1/00448.bin` and `00449.bin` written once), its description in the box. Screenshots with `--shots` |
| 2 | `tagui_menu.txt` (`startmode=80`, `tagscreen=1`) | The Tag Team screens straight away. With vita-agent-use's input: R1 to Camera mode and back; each Local Battle row; Health, Recovery, Battle rules pages; START to the character select; a fight; the results. The report's `tboui` line (textures held, KB), the `tbo:` lines, the watchdog |
| 3 | `tagui_menu_off.txt` (`tboui=0`) | The same screens not drawn (as before): that the setting turns them off |
| 4 | `tagui_2v2_on.txt` against `tagui_2v2_off.txt` | The 2 vs 2 demo fight with and without the HUD's words: pictures a second (`perfsum.py --ab`), the drawing thread's ms, the lines made (`tboui` line) |
| 5 | `tagui_5v5.txt` (`BT4_TAG_ACTOR_TEST=10`) | 5 vs 5 with the HUD's words: the newlib heap's peak, vitaGL's free memory, the texture table, the speed (as step 0 below) |

The free camera and the language (branch `wip/tagpc`, to do; the `.vpk` installed as above, the Spanish disc):

| Run | Settings | What it shows |
|---|---|---|
| 6 | `tagui_menu.txt` | The Tag Team screens in Spanish ("Combate local", "Modo de cámara", the help lines under each row), accents drawn; then `taglang=0` added: in English, as before. The main menu (`tagui_main.txt`, START past the film): the Tag Team description in Spanish in the box |
| 7 | `tagui_menu.txt`, by hand or with input | R1 to Camera mode; Camera mode row to "Fight overview" ("Vista general") (CROSS or right); L1 back to Local Battle, its first row to Spectator Mode, START, the fighters chosen, the fight: the camera holds all four fighters in view and moves on its own; right stick turns, SELECT + R / L zoom (the zoom row's value follows). Then "Free camera": left stick flies, R / L up / down, the fighters' pad does nothing but START (pause gives the pad back). "Follow fighter" with Fighter 2: follows the second fighter. Reset camera: back to Game camera. `tagcam=0`: the game's camera whatever the row says. The log: no watchdog, the speed as with Game camera |

By hand (a person with the console): the screens' look against the PC's (the backdrop, the window's corners, the
bars' round ends, the letters' outline, sharpness), every option changed and seen in the fight that follows (time,
CPU level, 5 vs 5, Free for All, per-fighter health and Boss preset, revival, joint attacks, fusions, the referee,
stage destruction), Two-player and Two-player co-op with a second controller (PS TV or a paired pad: the game's
split screen, the floating allies' gauges), the target selection's three methods in a fight, the names over the
target, the results' Rematch / Back to character select / exit, the Duel menu's Tag type, `tagteam=2` (the earlier
text setup) and `tagteam=0`.

### Tag Team readable on the 5-inch screen (2026-11-18, branch `wip/v14rel`, after v1.4.1)

The developer played Tag Team on v1.4.1: the HUD very small and unclear, the menus very small, and no way found to
pick one enemy over the other. Measured on the console's shots (960 x 544): the main menu's rows have capitals of
18-20 pixels, the talk box 16; the Tag Team setup's rows and its talk box 12-13. So: `tagui_scale=130` (the rows'
capitals about 16-17) and `taghud_scale=135` (the fight's letters, the HUD's gauge, the allies' column, the target's
rings and bar). The layout is upstream's: only the letters and the HUD's pieces grow, each around where it was; the
allies' column is laid out from the screen's foot so it stays on it. Targeting: the Vita's default was the right
stick (`tagtarget=1`) until `wip/remap`, which went back to the PC's L3 (the developer: every default the PC's;
the player picks the method on Camera mode > Target selection or Vita Guide > Vita Buttons, and the help lines
say the current one, `docs/vita/development.md` "remap"); the Select Fighters row's help says the fight's
controls and where L2 / R2 / L3 / R3 are (English, Spanish, Portuguese: `online_language.c`, PORT_VITA). Console
check: `psvita-agent/sets/bt4/tagscale_menu_{on,off}.txt` (with `.inputs.json`) and `tagscale_2v2_{on,off}.txt`.

### Entering Tag Team and the English disc's entry (2026-11-18, branch `wip/v14rel`)

The developer's run on the English B14 Rev2 (extracted folder) after v1.4:

- **No Tag Team entry in the main menu**: `tbo: the main menu's Tag Team lettering could not be put together`. The
  members 448 / 449 the lettering is cut from were written once to `ux0:data/bt4/gamedata/pzs4us1` and never again
  (`Vita_MemberFile` keeps a file that is there): written from the Spanish disc in an earlier run, they were put
  together with the English menu's names (`bt4_main_lettering.inc` takes 11 English rows, 6 Spanish ones), which fails.
  The folder form of the disc was not the cause (its archives are opened the same way). Now the members go to
  `gamedata/en` or `gamedata/es` by the disc played (the old folder left as it is), and if the disc's letters cannot be
  put together the entry is written in the Menu font (`letter_fallback`), so it always shows.
- **Entering Tag Team held the game's thread 1.9-2.7 s** (at 37.5 s, the first entry, and 151 s; both at the Tag Team
  screens' music starting, afs:1:2071: that is the entry, not the music): the screens' some forty lines were written
  on the game's thread at 80 pixels with their outlines (`tbotext=1` of `wip/tagtext`), and the pictures, given back
  after 600 pictures not drawn, were read and uploaded again. Now: the pictures made at build time and resident, the
  lines written at the Vita's size from kept glyphs, at most 4 ms a picture on the game's thread, the rest by the
  maker with the screen held until they are made (`tbotextsize`, `docs/vita/development.md`). To measure: the log's
  `held up` lines when entering (target under 100 ms), `tbo: the Tag Team set resident`, the report's `tbotext` lines.

Console check: `psvita-agent/sets/bt4/tagentry_{en,es}.txt` with `.inputs.json` (boot to the main menu, 2 minutes
there, down to Tag Team, enter, back, enter again; screenshots after each step; the DOWN count is a guess to check on
the first shot).

### The Tag Team text after v1.4 (2026-11-18, branch `wip/tagtext`)

The developer, playing v1.4 with its fixes on the Spanish disc: the Tag Team strings are "strange" (too small,
grainy) and "loaded little by little, also inside the fights". What the code did:

- **Grainy**: a line is written with capitals of 80 (talk box 48) pixels, halved to fit 128 rows (capitals ~40 / 24
  pixels) and drawn with capitals of 7-20 pixels, sampled bilinearly from that one size: 2-6 texels a pixel, letters
  broken and shimmering. The PC samples them with mipmaps (`gs_draw.c` `tbo_upload`). Now `tbomip=1`: the lines and the
  power-of-two pictures with mipmaps made by the chip, sampled trilinearly. (In a fight the whole picture, HUD included,
  is drawn at 640 x 368 and stretched to 960 x 544, as the game's own HUD; `fightres=960` is the existing way out.)
- **Little by little**: with `tbopre=1` the maker thread wrote the lines one at a time (1.5 ms pause between two, at a
  priority below the game's) after the drawing thread first asked for them, and a line was not drawn until made; a
  place reused by a new line (the timer's figures every second) showed the old line's letters until then. Now
  `tbotext=1`: a line's picture is made when the screen's code asks for it (the game's thread, before the picture is
  handed over) and uploaded in the picture that first draws it.
- **Small**: the layout and sizes are the PC's (fork rule); `tbotextpct` (default 100) draws the lines larger for a
  comparison on the console (`tagtext_*_big.txt` at 120).
- The strings themselves (Spanish, accents) come from upstream's catalogs (`PortOnline_Text`, `menu_translation.c`)
  and the TrueType fonts of `fonts.tsv`, which have every Latin-1 letter: nothing was found missing in the code; the
  console screenshots should confirm it.

Console check (settings in `psvita-agent/sets/bt4/`): `tagtext_menu_{new,old,big}.txt` (the Tag Team screens,
`startmode=80`, `tagscreen=1`) and `tagtext_2v2_{new,old,big}.txt` (the 2 vs 2 demo fight with the HUD,
`BT4_TAG_ACTOR_TEST=4`).

### The Vita's Tag Team HUD (`taghudvita`, 2026-11-19, branch `wip/taghud-vita`)

The developer: "we must make it all optimized for the PS Vita as the normal fights' HUD is, which runs great, in
proportions and resolution". The Tag Team fight's HUD is the PC port's (`src/battle/tag_team_hud.inc`): vector strokes,
rings and TrueType lines laid out for a big monitor. The game's own 1 vs 1 HUD is textured sprites authored for its
screen, resident, drawn 1:1 into the 640 x 368 fight picture. `taghudvita=1` (default 1, not compared on the console
yet; `0` the way before; `2` the soak's B battles) draws the Tag HUD the second way, with upstream's logic (what is
shown, when, where on the page) unchanged:

- **Not narrowed twice** (`vita_vgl.c`): in a fight the Vita's renderer narrows each 2D sprite to 3/4 about a fixed
  point (the 4:3 HUD on the 16:9 picture). It did that to the Tag HUD's gauges (marker 0x19) and to every BT4decomp
  line of letters (TBO_PSM) too, which are laid out on the page for the screen's shape already (the PC's `gs_draw.c`
  leaves both alone: `gsAnchor 20`, `!tbo`). That is why on v1.4.1 the player's gauge sat 85 pixels in from the left
  (the 1 vs 1 one at 5), the allies' column too, the portraits came off their frames, and every word was 3/4 as wide.
- **The gauges in the 1 vs 1 HUD's proportions**: the player's gauge is drawn with the 1 vs 1 HUD's own transform
  (scale 1, at the page's top-left corner: the same pixels as the 1 vs 1 player's gauge), the allies' column at 0.6 of
  it against the left edge from the foot, the floating allies' gauges at 0.34. The PC's gauge unit was 0.857 of the
  page's across on top of the renderer's 3/4: its gauges came out narrower than tall.
- **The game's own timer** (`hud_a.c`: its frame and digit sprites, top centre as in 1 vs 1) instead of the PC's
  TrueType line.
- **Shapes from one atlas** (`port/vita/tools/vita_hud_atlas.py`, built into the package as `tbo/ui/tag_hud_vita.vt`,
  128 x 112 RGBA, 56 KB, read once and kept; its boxes in `port/vita/vita_hud_atlas.h`, picture number 0x240): the
  radar's face (dark green disc, grid, rim), the target's rings in three sizes (the nearest drawn, scaled; a dark edge
  under them for any ground), the radar's contacts, the stick and shoulder glyphs. Each is one textured quad at its
  pixel size, its corner on a whole pixel, tinted with the HUD's colours as the PC colours its strokes. The off-screen
  arrow stays a filled triangle (it turns) with a dark one under it; bars stay rectangles.
- **Words at the game's HUD text size**: the lines (names, figures, KO, the assist and fusion labels) written with
  capitals of 11 pixels without smaller copies (as `taghudfine`) and drawn 1.6 times the PC's size (capitals 7-10
  pixels, as the 1 vs 1 HUD's "Hits" and damage figures), each as two sprites (the outline, one fill) instead of 12
  (the 8 copies of the outer border were 0.3 pixels out; three fill bands of one colour). `taghud_scale` no longer
  applies with it; strokes that remain (the revival cylinder's rim) at least 1.25 pixels.

Sizes in the 640 x 368 fight picture (PC = upstream's layout as v1.4.1 drew it on the Vita, `taghud_scale=100`):

| Element | PC (v1.4.1 on the Vita) | Vita (`taghudvita=1`) | Source |
|---|---|---|---|
| Player's gauge (face, health, ki, blast stock) | 0.86 of the game's gauge, narrowed twice: ~123 x 45 px from x 85 | the 1 vs 1 gauge: ~215 x 53 px from x 5 | the game's gauge sprites (`HudGauge_TagDraw`) |
| Allies' column (one gauge each) | 0.48: ~68 x 25 px from x 85 | 0.6: ~129 x 32 px against the left edge, 40 units apart from the foot | the game's gauge sprites |
| Floating allies' gauges | 0.28: ~39 x 15 px | 0.34: ~73 x 18 px | the game's gauge sprites |
| Timer | TrueType line, capitals ~9 px, narrowed, top right | the game's timer (frame, digits), top centre | the game's timer node |
| Radar | ~150 triangles: rim 0.7 px, grid 0.3 px, contacts 2-3 px | one 48 x 48 quad; contacts 7-9 px quads | atlas |
| Target's rings | 64 strokes 1.2 / 0.45 px, radius 8-26 px | one quad: rings 1.7 / 1.0 px with a dark edge, radius 10-31 px (3 sizes) | atlas |
| Off-screen arrow | filled triangle 6 units (~5 px) | filled triangle 8 units over a dark one of 10.5 | shapes |
| Target's health bar | 58 x 3 units (~54 x 2.5 px) | x 1.2 (~65 x 3 px) | rectangles |
| Target's name | capitals ~5.3 px, 3/4 wide | capitals ~8.5 px | TrueType line (cap 11) |
| Health figures (> 70,000), KO, assist / fusion labels | capitals 4-6.5 px, 3/4 wide | capitals 7-10 px | TrueType lines |
| Stick / shoulder glyphs | 16 strokes 0.7 px / 2 strokes | 16 x 16 / 15 x 13 px quads | atlas |

Cost, per picture of a steady 2 vs 2 fight (estimates from the code, to be measured on the console; the 1 vs 1 HUD
for reference):

| | Normal 1 vs 1 HUD | Tag HUD now (v1.4.1) | Tag HUD `taghudvita=1` |
|---|---|---|---|
| Gauges | 2 x ~30 sprites | ~30 + 2 x ~22 sprites (player, one ally in the column, one floating) | the same |
| Timer | ~4 sprites | 12 sprites (a line) | ~4 sprites (the game's) |
| Radar | | 1 packet of ~120 triangles (hudbatch) | 5 sprites |
| Target | | 1 packet of ~128 triangles, 3 rectangles, 12 sprites (name) | 1 sprite, 3 rectangles, 2 sprites |
| Lines of letters drawn | 0 | 2 (timer, name; +2 above 70,000 health): 24-48 sprites | 1 (name; +2): 2-6 sprites |
| Textures | the game's HUD sheets, resident | + the lines' textures (written at capitals 40) | + the atlas 56 KB, resident; the lines written at capitals 11 (about a thirteenth of the pixels) |
| Uploads a picture | the faces (the game's) | the faces, once per gauge (upstream) | the same; the atlas once in a fight |

The drawing thread reads each sprite and packet register by register: about 60-70 fewer sprites and 250 fewer
triangles a picture than now. Not yet measured; the batch below gives the figures (the report's `tboui`/`tbotext` lines,
the HUD's ms in the list reader).

Preview without the console: `port/vita/tools/taghud_preview.py` (Pillow) composes the atlas's pieces at their places
over a 1 vs 1 screenshot of the runs and puts it under a v1.4.1 2 vs 2 screenshot, at 960 x 544
(`port/build/taghud_vita_preview.png`).

Console check (settings in `psvita-agent/sets/bt4/`): `taghudvita_2v2_on.txt` / `_off.txt` (the 2 vs 2 demo fight,
`BT4_TAG_ACTOR_TEST=4`, screenshots) and `taghudvita_1v1.txt` (a 1 vs 1 fight: nothing must change). To look at: the
player's gauge where the 1 vs 1 one is; the portraits in their frames; the words' width; the radar and the rings
sharp; the allies' column at the foot; the timer at the top centre; 2 vs 2 at ~30 pictures a second.

#### Not Vita-native yet: the other PC-port screens (the next agent's brief)

The developer's rule (2026-11-19): everything the PC port adds must look and cost like the game's own HUD and menus
on the Vita: pre-made graphics at their Vita size, the game's fonts, the right proportions; upstream's logic kept. The
atlas (`vita_hud_atlas.py`: an element is a name, a box, a draw function; `TagHud_Atlas` draws one) and the
cheap lines (`Ps_TextExOuterXY`: one fill band when the colour is one; lines written at the capitals shown) are meant
to be reused. What remains, with what each needs:

| Screen / overlay | Where (upstream) | Today on the Vita | Needs |
|---|---|---|---|
| Tag Team setup pages: mode, rules, health, recovery, fusion, settings, shared (`Tb_DrawTag*`) | `src/menu/port_tbo.c` 2724-2990 | **done with `taguivita` (above), not run on the console**; before: PC kit pictures (`kit_*.png`) halved to ~640, TrueType lines at `tagui_scale` 130 | the kit's bars, windows and capsules made at their pixel size on 960 x 544 (9-slice in an atlas), rows' lines at their capitals; count sprites per page (each `Ps_Panel` is 9) |
| Tag Team character select (header, team rows, cursor, hexagon cells, "Goku (Mid)" bars, OK) | `src/menu/tag_team_select.inc` | **captions, windows, prompts done with `taguivita`**; the cells, bars, OK and names are the game's own team select | the hexagon cells and bars as atlas pieces at their size; the header in the game's menu font size; the faces from the game's sheet (already) |
| The talk box (hosts' lines) and headings (`Ps_Msg*`, `Ps_Heading`) | `src/menu/port_screen.c` | **done with `taguivita`** (the box under the page is the game's MsgWin) | written at the capitals shown; one band where one colour |
| Button prompts (`btn_*.png`, `Ps_Prompt*`) | `port/tbo/ui`, `port_screen.c` 718-790 | **done with `taguivita`** (atlas, 24 px); in a fight still the mipmapped picture (`tag_team_hud.inc` 192) | the Vita's own buttons (the game's prompt sprites, or an atlas at ~20 px) |
| Backdrop / poster behind the screens (`tag_team_art`) | `port/tbo/ui` | **`taguivita`: given back in fights**; 960 x 540 picture (the film off: `tbomovie=0`) | fine as a full-screen picture; check its VRAM (~2 MB) is freed in a fight |
| Main menu's Tag Team entry, emblem | `menu_a.c`, `bt4_main_lettering.inc` | the disc's own lettering (done), **emblem from the atlas at 47 x 48 with `taguivita`** | emblem at its pixel size (kept, ~600 KB with the lettering) |
| The Tag Team pause menu (`sTagPauseMenu`) | `src/battle/hud_0.c` 138 | **the game's own** (rows of `gPauseMenuVersus`): nothing to do | check its rows are the game's font (not TrueType); else as the setup pages |
| Names over fighters (`fight_panels.c`) | `port/src/fight_panels.c` | TrueType lines, now not narrowed (`taghudvita`) | sized as the target's name; one line per fighter cached |
| The assist / fusion / relay panel (`TagHud_Assist`) | `tag_team_hud.inc` 115-171 | gradient quads, lines, glyphs from the atlas (`taghudvita`); **reviewed: kept** (a plain quad costs no more than a piece) | its panel as an atlas piece (gradient 146 x 34 units), the relay's three links as pieces |
| Revival cylinder and its label (`TagHud_Revival`) | `tag_team_hud.inc` 466-517 | 24 projected quads + rim strokes (min 1.25 px), a label | keep the projection (3D); the label box and plus sign as atlas pieces |
| Spectator labels ("Switch fighter", L1/R1) and split-screen labels | `tag_team_hud.inc` 415-450, 554 | lines and atlas glyphs | sizes checked on the console |
| The HUD editor (`tag_team_hud_editor.inc`) | `src/menu/tag_team_hud_editor.inc` | PC only? (to check it is reachable on the Vita) | if reachable: as the setup pages |
| Online screens (lobby, ranked, join, profile: `Tb_Draw*` 2398-3270) | `src/menu/port_tbo.c` | not reachable offline | with the online work (`docs/vita/online-bt4.md`) |

### The Vita's Tag Team screens (`taguivita`, 2026-11-19, branch `wip/tagui-vita`)

The same rule for the screens the PC port adds (the developer, 2026-11-19: everything must be optimized for the Vita
like the game's own HUD and menus; look first at how the original makes its equivalent). How the game makes its own
menus: its pieces (windows, bars, icons) are small sheets made for its screen and resident while the menu is up; its
words are one sprite a letter from a resident font sheet (`src/battle/col_c.c` `Font_PutSprite`), drawn at the size
the sheet has them. How v1.4.1 made the Tag Team screens: 19 textures of the PC's pictures (100-2168 pixels across)
stretched to powers of two and sampled down through their mipmaps (a bar 512 x 64 shown 28 pixels tall, a button
256 x 256 shown 23), 7.3 MB with the mipmaps and the backdrop, resident for the whole session, fights included; each
line of letters written with capitals of 40 (the talk box 24), halved to 128 rows at most and sampled down through
mipmaps to the 11-18 pixels it shows, at 0.755 of its width (the 4:3 page's letters, `Port_MenuTextNarrow`): soft,
grainy letters. `taguivita=1` (default 1, not compared on the console yet; `0` the way before; `2` the soak's B
battles) keeps upstream's logic and layout (what is shown, where, the options) and makes the look the game's way:

- **The kit from one atlas at its pixel size** (`port/vita/tools/vita_ui_atlas.py` -> `tbo/ui/tag_ui_vita.vt`, 512 x 248,
  496 KB, read once in the first seconds and kept; boxes in `port/vita/vita_ui_atlas.h`, picture 0x241): every piece made
  from the PC's picture with an area filter at the size it shows on 960 x 544, with 2 pixels of its own edge round it.
  `Ps_SpriteScreenFiltered` (`src/menu/port_screen.c`, `PORT_VITA`) draws a piece by its box in the atlas: the same
  sprites, the same layout (the screens place pictures by their sizes in units), one texture instead of nine.
- **Lines written with the capitals they show** (`tbo_text.c` `gVitaTboWantCap/Sx/Sy`, `vita_tbo.c` `line_pixels`): the
  capitals' pixels are rounded (the line drawn up to 4 % larger or smaller to match), the line written at that size with
  a 1-pixel outline and made 0.755 as wide with an area filter (what the page does to it when it is drawn), so a texel
  is a pixel; its corner is put on a whole pixel; no mipmaps. A line of the same words at another size is another line.
  The headings (`Ps_Heading`) are written at their row's own size (capitals 22, `menu_text.c` `HEADING_RES` 1, not 88).
  A line of one colour (the chosen row's) is 2 sprites, not 4.
- **The PC's proportions** (`tagui_scale` 100 by default with it; the 130 of v1.4.1 was a pure enlargement against the
  blur: the rows' words filled their bars, the talk box ran to three lines). The character select's captions keep their
  v1.4.1 size (header capitals ~15 pixels, as the game's own names on that screen; teams' ~12). `tagui_scale=115` is
  the knob if 100 reads too small on the console.
- **The backdrop** (`tag_team_art`, 960 x 540, 2 MB) given back two seconds into a fight, read again by the maker as soon
  as the fight is over (the game's own screens load their backdrops when they open).

Sizes on the 960 x 544 screen (the page's unit 1.416 pixels across, 1.214 down):

| Element | Shown | v1.4.1 texture | `taguivita=1` |
|---|---|---|---|
| Row bar / selection (`kit_bar`, `kit_sel`; setup, options, rules, recovery, fighter pages) | 28-30 px tall | 512 x 64 + mips, sampled at ~0.45 | 356 x 28 / 335 x 28, 1:1 |
| Compact keys, gold / steel badges | 28 px | 128 x 128, 256 x 64 + mips | 45 x 28, 42 x 28, 106 x 28, 114 x 28 |
| Window (`kit_window`), its corner | corner 13 px | 512 x 256 + mips (corner 54 texels) | 124 x 60 (corner 13) |
| Talk / dialog box (`kit_dialog`) | corner 19.5 px | 512 x 128 + mips | 340 x 54 |
| Pad buttons (prompts, rows' arrows) | 22-24 px | 256 x 256 (arrows 512 x 256, START 256 x 128) + mips, each | 24 x 24 (40 x 24, 58 x 24) |
| Tag Team logo (title) | 262 x 87 px | 512 x 128 + mips, twice (on / off) | 262 x 88, once |
| Main menu's emblem | ~44 x 41 px | 256 x 256 + mips | 47 x 48 |
| Rows' words (`Ps_Text` .68-.70) | capitals 11 px (14-15 at `tagui_scale` 130) | written at 40, halved, mips, 0.755 wide | written at 11, 0.755 made at write time, 1:1 |
| Talk box (`Ps_MsgLiteral` 1.08) / prompts | capitals 15 / 14 px | written at 24, mips | written at 15 / 14, 1:1 |
| Headings (`Ps_Heading`) | capitals ~21 px | written at 88, halved, stretched to powers of two, mips | written at 22, 1:1 |
| Select header / teams' captions | capitals ~15 / ~12 px | written at 40, mips | written at 15 / 12, 1:1 |

Cost (the Tag Team "Local Battle" page, 10 rows, the cursor on a row with a value; counted from the code and the
build's texture sizes; the game's own menus for reference):

| | The game's own menus | v1.4.1 | `taguivita=1` |
|---|---|---|---|
| Sprites a picture | one a letter (a 30-letter line = 30) plus its pieces | ~125: backdrop 1, logo 1, window 9, rows 30, lines 22 (4 each, the talk box's 2), prompts' buttons 4 | ~121 (the chosen row's two lines 2 sprites each) |
| Textures drawn from | its sheets | 9 pictures + 22 lines | 2 pictures (atlas, backdrop) + 22 lines |
| Kept for the screens | small sheets while the menu is up | 7.3 MB (19 textures with mipmaps, backdrop 2 MB), the whole session | atlas 496 KB + the Duel menu's Tag lettering 341 KB; + the backdrop 2 MB outside fights |
| The page's lines | the font sheet | ~3.6 MB (estimate from the fonts' metrics: 22 lines at 40 / 24 with mipmaps) | ~1.3 MB (written at 11-15, no mipmaps) |
| Sampling | 1:1 | trilinear from 2-4 times larger | bilinear 1:1, whole pixels |

So the memory the Tag Team screens keep goes from 7.3 MB (fights included) to 2.9 MB on the screens and 0.8 MB in a
fight, and the drawing is no heavier (four sprites fewer; the kit's pieces from one texture). Not measured on the
console yet: the report's `tboui` line gives the textures held and their KB.

Reviewed, nothing to change: the Tag Team pause menu (`sTagPauseMenu`, `hud_0.c`) and the results menu ("Team 1 wins",
Rematch, ...) are the game's own menus with some rows taken out; the assist / relay panel in a fight (`TagHud_Assist`)
is one plain gradient quad (as cheap as an atlas piece and without edges to alias), a 1-unit bar, the portrait, the
label and the keys (already `taghudvita`'s); the revival cylinder is projected in 3D (kept).

Preview without the console: `port/vita/tools/tagui_preview.py` (Pillow): the setup page drawn from the atlas and lines
written as the Vita writes them (the menu font DejaVu Sans Condensed Bold, the talk box's Fira Sans Condensed Bold,
both narrowed to 0.755: measured on the console's screenshots against the fonts' advances), under the console's
screenshot of today's page; the character select's captions over its screenshot.

Console check (settings in `psvita-agent/sets/bt4/`, with the `.inputs.json` of the same name): `taguivita_menu_on` /
`taguivita_menu_off` (the setup pages, as `tagscale_menu_*`), `taguivita_menu_115` (the letters 15 % larger),
`taguivita_select_on` (character select), `taguivita_entry_en` (the main menu's emblem and the screens in English),
`taguivita_2v2_on` (a 2 vs 2 fight: nothing must change; the report's `tboui` KB lower). To look at: the bars' rims
and the window's corners sharp, the words sharp and not touching their bars' edges, the prompts' buttons sharp, the
logo, the emblem; the log's `tag_ui_vita.vt: 512 x 248, kept`.

Not done yet (next): the HUD editor (`tag_team_hud_editor.inc`: reachable on the Vita? it draws with the same kit, so
it gets the atlas and the lines already), the online screens (with the online work), the button glyph of the fight's
relay panel (`tag_team_hud.inc` 192, still the mipmapped picture), the hosts' pictures (online only).

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
  and the selection's words are not drawn (TrueType); 3 vs 3 and up not run; a whole match played by hand. (Since
  then: the entry, its description and the words are upstream's, 2026-11-17 `wip/tagui`; the results' third choice is
  "Back to Tag Team" / "Volver a Tag Team" from the Tag Team screens as on the PC, and under `PORT_VITA` also from the
  earlier text setup, `tagteam=2`, which has no Tenkaichi Budokai Online to go back to, `wip/tagpc`.)

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
- 2026-11-16, where the drawing thread's time goes in 2 vs 2 on stage 0 (zones 48-50 inside the state lookup, the
  render-thread waits by line, the textures made the long way; runs `zs0`, `rs0`, `tl0`, `tm0`): the stretches under
  14 pictures a second take ~75 ms a drawn list on the drawing thread and ~40 ms on the list reader (~42 and ~25 in
  the stretches at 20-26), spread over many zones. The waits for the render thread are 3-5 ms a list there, most of
  them before a palette texture's indices and palette are put in the chip's memory (vitaGL's allocator is not for two
  threads) and at a picture's end (`rtswap` off). The spikes were the log: the first 24 textures made the long way
  (8-bit, their palette's way refused) each logged vitaGL's free memory, and `vglMemFree` walks vitaGL's whole heap
  (`sceClibMspaceMallocStats`, no custom heap in our vitaGL build): 40-66 ms each, in the release too (`Vita_Log` is
  not compiled out), 330-730 ms in a 2 s stretch. The line now shows the memory as last read: texture making 545-733
  -> 90 ms in the worst stretch, 2,451-3,563 -> 1,493 ms over the run, 20.2-21.5 -> 22.3 pictures a second, stretches
  under 15 a second 5-7 -> 3 of 39 (one run each). The reading itself grows with the heap's pieces, 3 ms at the
  fight's start to 46 ms two minutes in; it was every 60th picture (the watchdog's, release too), every 8th for a few
  runs today, and is now every 1024th in a fight.
- 2026-11-16, the special moves (screenshots of run `sh0`: a beam with an explosion dome at 38-44 s runs at 10-13
  pictures a second, a giant flame at 63-67 s at 14-18, the intro with as many models at 26-27): the drawing thread
  is the wall there (2.0 s of processor in every 2 s, never waiting for the list reader, which is at ~82%). Against
  the intro, the extra is the effects' GS packets (~1,430 against 714 a list, 24-32 ms against 8 by the drawing
  thread's records), the kept parts that alternate with them (25-36 ms against 10) and the state lookups. The effects'
  images and palettes are given places in GS memory a frame at a time, so the texture table made the same images
  again at each new place. `texcont=1` keys the table by contents (runs `tc0`, `tc1`, A/B, 120 s, `film=30` both):
  new textures 134 -> 8 every 2 s, their making 56 -> 2 ms; the beam 10.4 -> 11.3 pictures a second, the drawing
  thread 82 -> 76 ms a list there; the whole fight 19.5 -> 19.8 (inside the noise; `film` costs both runs ~3 a
  second). The waits for the render thread rose in the bursts (103 -> 152 ms every 2 s): what the textures no longer
  cost was waited for at the picture's end. The film's pictures of B during the beam, the explosion and the flame
  show the effects' textures right; only one pair of A and B fell on the same moment, and it is the same picture.
  Default 0 until a 1 vs 1 run.
- 2026-11-16, the kept parts walked and the shadow page's strips (one batch of runs, 120 s each, 2 vs 2 on stage 0,
  `texcont=1` in all; the means over 40-118 s; one run each):

  | Run | Settings | Pictures a second | Stretches under 20 | Beam (38-44 s) | Drawing thread / list reader, ms a drawn list |
  |---|---|---|---|---|---|
  | `b0` | `texcont=1` | 22.7 | 10 of 39 | 11.5 | 43.9 / 31.0 |
  | `dw1` | + `dpways=1` | 24.9 | 4 | 11.8 | 40.7 / 27.4 |
  | `sg1` | + `silgate=1` | 27.2 | 1 | 19.0 | 36.3 / 19.8 |
  | `ds1` | + both | 27.6 | 1 | 19.3 | 35.8 / 19.7 |
  | `da1` | + both, `audiocore=0` | 27.2 | 1 | 20.3 | 36.6 / 18.9 |

  `dpways`: a kept part whose place is another's in use takes a second place; the parts walked record by record
  1,659 -> 826 every 2 s (busy places 1,543 -> 623). `silgate`: in 2 vs 2 the shadow page is spoiled every picture
  and never read ("0 given the page"), yet ~2,500 program-2 strips a list were read, sent and drawn: held back
  ~80,000 every 2 s, the list reader 31 -> 20 ms a list. With both, the stretches at 30 pictures a second are most of
  the fight; what is left is the beam (17-22 a second, the drawing thread 43-53 ms a list: the effects' GS packets
  ~31 ms of it, the kept parts ~20) and the stretch after it. `audiocore=0` changes nothing measurable. The film
  (`dsf`, `film=30`, all three on) shows the fighters, the auras, the beams and the HUD right. A first try of `b0`
  hung at 77 s (the watchdog's dump: the list reader in its delay loop with the 8 MB ring full, the drawing thread in
  `GR_FRAMEEND` waiting; dump `crash-1794837838`, run `b0_hang`); not seen again in the 7 runs after it.
  In 1 vs 1 (`ds1v1`, all three on) the shadow page is drawn and read (~100 reads every 2 s), `silgate` holds back
  ~50 strips every 2 s and the fight runs as before: 29.7 pictures a second against 29.6 (`s1v1`). The three are on by
  default since.
- 2026-11-16, the batches after (same fight, 120 s, one run each, means over 40-118 s; each batch began with a run of
  its base on its own build, which repeated the batch before's figure within 0.3 a second):

  | Change | Run against base | Pictures a second | Beam (38-44 s) | What it did |
  |---|---|---|---|---|
  | `skipcc=1` | `n1` / `b2base` | 26.6 / 27.5 | 14.8 / 18.5 | the list reader 24 -> 17 ms a list not drawn, but the drawing thread 7.5 -> 21 ms on them: off |
  | `dpslots=4096` | `c0` / `b2base` | 28.1 / 27.5 | 19.2 / 18.5 | parts walked as their place was another's ~800 -> ~3 every 2 s; heap 38 -> 48 MB |
  | `wtsig`, `rtkick`, `silprobe=64` | `sk1` / `b2base` | 27.9 / 27.5 | 19.6 / 18.5 | |
  | `dptexa=1` | `c1` / `c0` | 28.2 / 28.1 | 20.7 / 19.2 | parts walked 164 -> 77 |
  | `texrt=1` | `c2` / `c0` | 27.8 / 28.1 | 20.1 / 19.2 | the waits for the render thread before a palette texture gone; the picture's end waits more |
  | `dppf=1` | `c3` / `c0` | 27.5 / 28.1 | 20.1 / 19.2 | nothing: off |
  | `fxclamp=1` | `d1` / `d0` | 28.0 / 27.9 | 20.5 / 20.3 | up to ~140 triangles a list the runs' way |
  | `hudbatch=1` | `e2` / `e2b` | 28.5 / 28.2 | 21.5 / 20.4 | the radar one packet a picture (292 triangles) instead of ~280 |
  | `hudrun=1` | `f1` / `f0` | 28.9 / 28.6 | 22.0 / 21.2 | the radar's triangles the runs' way |
  | `adfast=1` | `g1` / `g0` | 28.7 / 28.6 | 22.3 / 21.9 | no stretch under 20 a second |
  | `dpptr=1`, `dpcrec=1` | `h1` / `h0` | 28.6 / 28.6 | 23.3 / 22.3 | a kept part's match (zone 32) 1,630 -> 1,055 cycles, ~1,400 parts a list; the flame 28.1 -> 29.5 |
  | `dpgen=1` | `h2` / `h0` | 28.7 / 28.6 | 22.0 / 22.3 | a correctness fix: ~7,000 state fields every 2 s changed by kept parts without the state's generation bumped |

  One at a time most are inside the noise between two runs (~0.3 a second); together, from `c0` to `g1`, 28.1 -> 28.7
  and the beam 19.2 -> 22.3. Where the drawing thread's time went in the beam was found with `gifshapes=1` (the GIF tags
  outside the runs, by shape, with the PRIM they draw with): the Tag Team radar (`Ps_ScreenQuad`, port code) was ~280
  packets of one quad each, ~6,300 A+D register writes a list were walked register by register, the effects' animated
  layers (`EftSprAnim_QueueTri`, CLAMP before and after each triangle) missed the runs. The films (`film=30`) of each
  batch show the effects, the auras, the HUD and the radar right; the console's screenshots of the radar with and
  without `hudbatch` are the same. 1 vs 1 with all of them: 29.8 (`g1v1`). All on by default since, but `skipcc` and
  `dppf`. What is left in the beam (~45 ms a drawn list on the drawing thread): the effects' image uploads (~9 ms),
  the kept parts (~15 ms), the GS primitives' states and the sprites of the post passes (glow, outline, pan blur),
  which the Vita draws natively but are still read.
- 2026-11-16, the 25-stage soak in 1 vs 1 (`runs/soak1v1`: stages 0-22 reached in 45 minutes, 46 duels, side A with
  the day's switches off, side B on; dpslots 4096 on both): pictures a second 29.6 / 29.7 (at the cap), every stage
  within -0.1..+0.6; the lowest 2 s stretch better on 18 of 23 stages; the drawing thread 2-26% less on every stage;
  no crash, no watchdog stop; the heap at most 56.7 MB of the 80 MB the C library has.
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
| 2 | (done, then replaced 2026-11-17 by BT4decomp's own screens; kept as `tagteam=2`) A Vita setup screen for the room (mode, time, CPU difficulty), then `Port_TagCPUOptions`, `Port_TagConnect(NULL)`, `Port_TagModeOptions`, `Port_TagOpen`, `Port_TagTeams`, `Port_TagSlot` per CPU, `Port_TagCommand(3)`; `src/menu/port_screen.c:684` takes it in place of `PortTbo_Run` under `PORT_VITA`, back to the main menu after | small to medium |
| 3 | (done 2026-11-17) The main menu's entry: BT4decomp's own (item 4, its emblem and the disc's lettering), `tagteam=0` hides it | small |
| 4 | Fixes for more than two fighters: `btl_input.c:511` (`Save_PortKeys` with `chr->pad` when Tag is active, as the PC's line 522), the clash helper's fighter lookup (`port/vita/vita_gfx.c:175-185`, sockets 0 / 1) | trivial |
| 5 | (done 2026-11-17, not run on the console) The HUD's and the selection's words on the Vita: the TrueType text drawn, as on PC (above) | medium |
| 6 | On the console: 2 vs 2 whole matches, results, rematch, back to character select, back to the menu; then 3 vs 3 and up as far as the measurements allow (memory: a larger newlib heap, `MAX_TEX`) | unknown |

## Handoff: the film behind the Tag Team screens (`tbomovie`, written 2026-11-17 on `wip/tagmovie`)

Where it stands: `tbomovie=1` plays the two H.264 films (`port/vita/assets/tag_ui_{intro,loop}.mp4`, made by
`port/vita/tools/make_tagmovie.sh`, `ffmpeg` from the PATH) on the hardware decoder through `SceAvPlayer`
(`port/vita/vita_tagmovie.c`); the default stays 0 (the poster) until the points below are settled.

What the console showed, in order (batches 30 to 32):
1. 16 MB of physically contiguous memory left aside at vitaGL's start: no picture. The player's `allocateTexture` asks for one
   3825 KB block (its 5 output frames, 765 KB each, 128 byte aligned) and one 14336 KB block (1 MB aligned, the decoder's); with 5 MB of
   frames taken first, 11 MB was left: `0x80024302`.
2. 32 MB left aside (`tbomoviemem=32`): the film plays (intro then loop, texture set 5 us, 22 MB held), but the console has only 26 MB of
   physically contiguous memory in all, so vitaGL got no pool: a 2 vs 2 fight's newlib heap peaked at 64.5 MB instead of 48.2 (5v5 was
   already at 74.4 of 80 MB). Not usable as the default.
3. The colours of that run: some pictures right (the film is red and purple), some with saturated magenta and green blobs and a hard
   vertical edge near x=710. Not understood. Seen: right pictures and wrong ones in the same screens, the luma always right; the player's
   frames sit at 765 KB steps in one block (1 KB aligned only) and the decoder rewrites them while the chip may still read them.
4. Memory carved out of a block of vitaGL's own pool (`vglAlloc(VGL_MEM_PHYCONT)`, batch 32, cd72f17): the player never gave a picture
   (IsActive drops after ~500 ms, the 14 MB block never freed), with or without copies, with CSC0 or CSC1. The decoder wants memory of its own
   memblocks (the same addresses worked when each block was its own `sceKernelAllocMemBlock`), but fights cost nothing that way (heap peak 48.0 MB).

What is in the last commit (built, not run): the pool of vitaGL given back to the system while the film plays and taken again after, instead of
carving it. The port's vitaGL patch gained `vglPhycontMemRelease()` (only when nothing is allocated in the pool: it is nearly empty at the Tag
screens, `26623 of 26624 KB free`) and `vglPhycontMemReacquire(size)`; the player's blocks are plain `sceKernelAllocMemBlock`
PHYCONT_NC ones again (`tbomoviemem=0`), the drawing thread gives the pool back before starting the playing thread and takes it again 1 s
after the player closed (`tagmovie: vitaGL's physically contiguous pool ... given back` / `taken again: N of N KB`). Also in it:
`tbomoviecopy=1` (each picture copied into three 1 MB aligned blocks, `copies on:` line), `tbomoviecsc=0/1`, the playing thread kept 3 s
(not 0.5 s: batch 31 made the player 26 times in 70 s), and the log of every player call and the first 12 pictures' addresses.

Not finished, to do next, in this order:
1. Console: `tagmovie_on` with this build. Look for the release line (a pool with something in it gives the poster: the log says how
   much was free), the first picture, `taken again: 26624 of 26624 KB`, and that a fight after the Tag screens has its heap peak
   unchanged (`u0_movie`: 48.0 MB against 48.2).
2. The colours (point 3) are open. With copy on and off and CSC0 and CSC1 the runs `tagmovie_on`, `tagmovie_nocopy`, `tagmovie_csc1`
   did not reach a picture in batch 32, so nothing was learnt; repeat them after step 1. Compare which pictures are wrong with their
   addresses (`picture N: ...` lines). Ideas: the player's frame buffers are not 4 KB aligned (copy fixes that if it is the cause);
   the decoder writes the frame while the chip reads it (needs the copy, or more output frames); the chroma plane's place
   (`YVU420P2` expects it right after the luma, here w x h bytes on); sampling with half precision in the port's pixel shaders.
3. 55 of the first 211 pictures late in batch 31: mostly the player's 650 ms to open (lateness counts from the source's start); the
   intro could be opened before the screens draw (a longer idle time, or starting at the menu before).
4. The gap between the intro and the loop (a second source on the same player: the poster may show for an instant) and the sound
   (the films have none).
5. Before the default can be 1: the 5v5 and 2v2 heap peaks unchanged, no leak of the pool over many openings of the Tag screens.

## The hang back to character select (v1.4.1, found and fixed 2026-11-18, branch `wip/v14rel`)

**Found** (the overnight sweep, a crash dump with `hangdump=8`): the drawing thread was in an endless recursion inside
vitaGL (`scene_reset` -> the immediate-mode pool, 16 MB, asked of the mapped memory -> refused -> the unsafe allocator ->
`glFinish` -> `scene_reset` -> ...), sleeping a second at a time; every other thread waited behind it. The pool is 64 KB
now (`vglpool`, `docs/vita/development.md`). What follows is how it was looked for.

After a fight, at the switch from the fight size (640 x 368) to the menus (960 x 544), about 15 pictures later, the
game stops for good (not every time; seen with and without anime music, and in a build from before the screens'
pictures were made at build time). Every watchdog of it says the same: the drawing thread asleep (wait type 1, a
sleep) in a picture's end (back end record 7, not in `rt_sync`, the swap or the ring), its run time frozen; the back
end's queue full (8 MB); the list reader sleeping for room in it; the game's thread on its condition variable; the
render thread idle; **the pictures shown counter frozen too** (the display callback no longer runs: the chip has not
finished a picture). The likeliest reading is the chip stalled and the drawing thread waiting in a system call
(`sceGxmBeginScene` for a free scene of the display's target, at the next picture's `glClear`, is outside every
marker the watchdog had).

What the watchdog now says (commit "Hang after a fight: name what every thread waits on"):
- the drawing thread's step in `GsGpu_FrameEnd` (`gVitaFePhase`: 1 last batches ... 9 `vglSwapResolution`, 10 swap,
  11 `scr_resized`, 13 the next picture's `glClear`) and in `Vita_TboFrameEnd` (`gVitaTboPhase`);
- for every watched thread its last sleep of 1 ms or more and who asked for it (`sceKernelDelayThread` is wrapped:
  the caller's address against `Vita_Log`'s; `bt3.map` turns it into a function). No sleep noted for a thread that
  is asleep = asleep inside a system module (the chip's or the display's calls);
- what the game's thread and the list reader wait for, and for how long; a wait over 1 s is also logged when it ends.

The next step: a run with `hangdump=8` (the program stops on purpose after 8 s without a picture, so that the system
writes a crash dump with every thread's stack: `vita-parse-core` with `bt3.unstripped.elf`). Sets:
`sets/bt4/hang_tag2v2_am1.txt`, `hang_tag2v2_am0.txt`, `hang_1v1.txt` (psvita-agent).
