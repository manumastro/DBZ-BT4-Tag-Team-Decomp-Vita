# Changelog

The changes of the PS Vita port of BT4. BT4decomp's own, for the PC: [docs/port/CHANGELOG-BT4decomp.md](docs/port/CHANGELOG-BT4decomp.md).

## Unreleased

- **Every Vita-only control can now be changed** (`remap=1`, not yet run on the console): Vita Guide > Vita Buttons,
  or Options > Controller > Vita Settings and START. Choose a SELECT + button pair for L2, R2, L3, R3, the Tag Team
  Senzu Bean, fusion and undo-fusion, quitting the game and Unlock All, and the Tag target method; SQUARE twice puts
  every default back. The guide, the pause page, the reminder line, the Tag Team legend and help lines always show
  your current buttons. A SELECT + button pair no longer sends a stray SELECT press first.
- **Tag Team target: the PC's default** (L3, the rear pad's lower-left quarter) for players who never chose one; pick
  the right stick or the D-pad on Camera mode > Target selection or in Vita Buttons (`tagtarget=1` for the old
  default).
- **Vita Guide in the main menu** (`vitaguide=1`): everything about playing on the Vita,
  in the game, in English or Spanish: the controls and the touch buttons, a full Tag Team guide (how to start, the
  setup, targeting, revive, Senzu, fusion, the HUD), Unlock All, every Vita Settings option, the anime soundtrack, the
  right game files and where they go, mods, what to send when something breaks, and what is not there yet. It is the
  entry just above the first one when the main menu opens.
- **Vita Controls in the pause menu**: the controls of the fight you are in (where L2, R2, L3 and R3 are now, and the
  Tag Team controls in a Tag fight).
- **A controls legend on the Tag Team setup page** ("In the fight": target, revive, Senzu, fusion).
- **A short controls reminder at the bottom of the screen in your first 5 fights** (20 seconds each); pause > Vita
  Controls, SQUARE, turns it on for good or off.
- **The Tag Team fight's HUD drawn like the game's own** (`taghudvita=1`, not yet run on the console): your gauge is
  now exactly the one of a normal 1 vs 1 fight (same size, same corner), the allies' gauges in proportion at the
  bottom left, the game's own timer at the top, a sharp radar and target ring, and readable names and labels (before,
  the gauges sat away from the corner and every word was squeezed to three quarters of its width). It also draws less
  each frame.
- **The Tag Team screens drawn like the game's own menus** (`taguivita=1`, not yet run on the console): the bars,
  windows, buttons and logo are now made for the Vita's screen (sharp edges instead of soft ones), every word is
  written at the exact size it is shown (no more blurry or grainy letters), the words have the PC's proportions again
  (they no longer fill the bars), and the Tag Team screens keep about 4 MB less memory busy, 6 MB less during fights.
- **Multiplayer, the start** (nothing a player sees yet): BT4decomp's online code brought to its latest public release
  (0.1.39.6 R1.6: Ranked abandonment and results, sturdier sign-in); the Vita's network start-up and the PC's
  float arithmetic (built, not run on the console); **Ad Hoc** (Vita to Vita without internet) started: the system's
  group dialog and a first check between Vitas (`adhoc_test`, not run on the console yet); BT4decomp's online menus
  and its 1 vs 1 network code built for the Vita (a separate development build, `online_menu`; matches cannot start yet).
- **Multiplayer, next steps** (development build, all off by default, not run on the console yet): a session starts
  by restarting the app with a session file, as the PC restarts its program; the online messages ("Connecting to your
  rival...", a desync) drawn in the port screens' own window and letters (`online_line`); **Ad Hoc rooms** in the PC's
  menus: Create Match > Connection "Ad Hoc" and Join > "AD HOC", a list of the rooms nearby, the PC's lobby and match
  carried over Ad Hoc (`adhoc_menu`, needs two Vitas); in a session the Vita Settings that change the pad are neutral
  (`online_neutral`) and a room's speed above 100 % is kept (`online_speed`); the waiting message over the held
  picture (`online_hold`) and the players' names under the health bars (`online_names`).

## v1.4.1 (2026-11-18)

- **No more freeze going back to character select after a fight**: now and then (Duel or Tag Team, with or without
  the anime soundtrack) the game stopped for good on the way from a fight's end to character select. The graphics
  library took a 16 MB block for every picture for a drawing mode the game never uses; after a fight that block
  could be refused, and the library's way out of that never ended. It now takes 64 KB. Run on the console: a long
  loop of fights and returns to character select, Duel 1 vs 1 and Tag Team 2 vs 2, without a freeze.
- **The main menu's Tag Team entry shows on the English disc too**: its lettering is cut from the disc's own menu
  files, and those were kept from the first disc ever played (the Spanish one here), so on the English disc the
  entry had no name. Each disc now has its own copy (`ux0:data/bt4/gamedata/en`, `/es`), and if the disc's lettering
  cannot be made the entry is written in the menu's font. Run on the console with both discs.
- **No more stall entering Tag Team**: its pictures are made ready for the Vita when the package is built (the size
  they are shown at, no PNG to decode), loaded once at start and kept (about 8 MB); the letters are drawn ahead at
  start and the lines at the Vita's size. Run on the console with both discs after two minutes in the main menu:
  entering and re-entering Tag Team show the whole screen at once, no hold-up logged (it was 1.9-2.7 s). The
  package is 1.5 MB smaller. Left: a hold-up of about half a second while scrolling the main menu.

## v1.4 (2026-11-18)

- **The anime soundtrack on the English game** (asked by players): play the English B14 Rev2 with the songs of the
  Spanish B14 Rev2 (the Latino dub's music), as the PC version's "Anime soundtrack". Copy your own Spanish B14 Rev2
  `.iso` to `ux0:data/bt4/es/` (the English one stays where it is) and turn it on in **Options -> Controller -> Vita
  Settings -> Mods**, the lower row (**ON / OFF**, off by default); it applies from the next song. Without the
  Spanish image the English songs play as before. Run on the console: the Spanish image found and mounted next to
  the extracted English disc, a fight's song streamed from it with its loop, no crash.
- **Tag Team lettering like the rest of the game**: the Tag Team screens' and the 2 vs 2 HUD's lines are made when
  the screen asks for them and shown from their first picture (they used to appear a line at a time, and a reused
  line, such as the timer's figures, could show the old letters for a moment), and they are mipmapped as on the PC,
  so they are no longer grainy when drawn small (`tbotext`, `tbomip`, both on). Run on the console (the Tag Team
  screen and a 2 vs 2 fight, on and off: no crash, all lines there).

- **Faster fights, above all 2 vs 2 and the big specials**: the 2 vs 2 demo fight averages 29.2 pictures a second
  (22.7 when this work began, with 10 of 39 stretches under 20; now none), the heavy 1 vs 1 (Super
  Spirit Bomb against Dragon Fist) 29.9. The settings behind it, all on by default: `sprrun`, `rtswap`,
  `dpfreelate` (the Super Spirit Bomb's drops), `bindrange` (the long picture at the Super Spirit Bomb's end, 88 ->
  50 ms), `memsample` and `ftdedupe` (the 2 vs 2 heavy fight's single long pictures: hitch blocks 37 -> 22, no reset
  of the fighters' buffer on the graphics chip). Each is described in [docs/vita/development.md](docs/vita/development.md).
- **The start says which BT4 it found**: Team BT4's B14 Rev2 (English or Spanish) is recognised by its program files'
  sizes and CRC; another build (B13 and older, other mods) stops with a clear message. An archive (`.7z`, `.zip`,
  `.rar`) left in `ux0:data/bt4/`, a cut `.iso` and the 4 GB limit of FAT32 cards are named on the error screen; the
  log says which image and folder are used.
- **Fixes after the first run of this build on a Vita 1000 with the Spanish disc**: choosing the DP battle type in the
  Duel menu no longer crashes (the game's heap had no hole for a 3.5 MB menu file after the Tag Team screens; such a
  block now comes from the system's memory); the main menu and the Tag Team screens no longer stall (their pictures and
  lines are made by a helper thread, the Tag Team set ahead while the main menu is shown; `tbopre`); and Options >
  Controller > Vita Settings, where L2, R2, L3 and R3 are placed on the rear or front touch plates, has its lettering on
  the Spanish disc too (its row kept the disc's own name, with the plates' captions of the disc's own). Built, not yet run on the
  console.
- **README**: a step-by-step installation, more troubleshooting and a FAQ.
- **Tag Team with the PC version's own screens** (run on the console: the screens, the menus by the pad, 2 vs 2 at
  29.4 pictures a second; 5 vs 5 runs but is not playable yet): the main menu's Tag Team entry and the
  Duel menu's Tag type open BT4decomp's own Tag Team menus, drawn on the Vita with their pictures and TrueType
  letters: Local Battle (Singleplayer, Two-player, Spectator Mode, Two-player co-op; 1 to 5 fighters a team, time,
  CPU level, presets, revival, each fighter's health and combat rules, battle rules) and Camera mode (target
  selection with L3, the right stick or the D-pad; the special moves' cameras; joint attacks; fusions; the PC's free
  camera is not there), then the simultaneous character select.
  In the fight the Tag Team HUD's words show (the timer, the names over the target, the gauges' figures, the joint
  attacks' labels). Online Battles is left out. The Vita's earlier Tag Team setup screen stays as `tagteam=2` in
  `bt3.txt`.
- **BT4decomp 0.1.39.6 R1 1.3 merged in**: Tag Team's camera no longer shakes between lock-on and free view; aerial
  clashes no longer leave fighters invisible or unable to move.
- **BT4decomp 0.1.39.6 R1 1.2 merged in** (the PC port this one is built on; run on the console in the 2 vs 2 demo fight and a 1 vs 1 duel: same speed, the beam special faster, no crash):
  Tag Team's crash fixes (the effects of fights with more than two fighters, Ginyu's Body Change), its expanded
  rules (joint attacks and fusions, on by default as on PC, also for the CPU fighters; cooperative revival, off by
  default), the new Tag Team HUD (the game's own gauges for the fighter and the allies, a new radar, the target's
  ring), and the partners' voices from BT4's own ranges. Not on the Vita: online play, the Lua mods' split-screen,
  and the English game with the Spanish install's anime songs (the PC's Tag Team menus and the names over the HUD:
  the entry above).
- **Unlock All is kept**. v1.3 could lose it two ways: chosen at the start with no save on the memory card yet, it was
  given to the save before the title screen, whose New Game then wrote a new save over it; chosen in the menus, it
  stayed in memory until the player saved, and was gone after quitting. Now a choice made at the start, or before the
  title screen is left, is done as the title screen is left (to the save just loaded or just made), and every way
  writes the save file at once (`ux0:data/bt4/saves/card1/BASLUS-21678DBZT3/BASLUS-21678DBZT3`), after copying it
  beside it (`.bak` the first time, never written over; `.last.bak` after). The file is not written when the save in
  use is not the file's (a new game not saved), nor when it is not a whole save.
- Unlock All no longer erases the saved characters of the Data Center (passwords) and no longer lowers the money of
  a save that had more than 4,850,000 Zenni (the game's routine does both); only items not owned before are marked
  new.
- **Unlock All in Vita Settings**: SQUARE on Options > Controller > Vita Settings opens its screen; leaving Options
  then saves the game's own way too.
- SELECT is held for about a second in the menus (it was a second and a half to two).
- The screen says whether the save file was written, and the log says what was done: the characters, stages,
  songs, items, Dragon History slots and money before and after, the copies made, and the file written. Each
  character select screen also logs the characters it shows locked, with the save's character bits.
- Undo's copy is now `unlock_undo.bin`, removed only once the save file has the save as it was. v1.2 to v1.3's
  `unlock_backup.bin` is left as it is and not used (v1.3 could fill it with a new game's defaults).
- **Clashes fixed for BT4**: the Vita Settings' Clashes option came from the BT3 port, where a clash counts turns of
  the stick; BT4 counts presses (Triangle in a beam struggle, Square in a clash of blows). Its **Square** setting
  (the default) took Square off the pad in a clash, so Square presses were lost in a clash of blows; now a press of
  Square counts in both kinds of clash. **Stick** now turns the stick into presses, as BT4decomp's PC option does.
- **Clash assist** (Vita Settings > Clashes, ON / OFF, off by default): Square or Triangle held in a clash is
  pressed for you about ten times a second.
- **Stick** (Vita Settings, below the four plates): a dead zone of 0 to 40% and an even or soft response, for sticks
  that feel too quick. Off by default (the sticks as they were).
- The log says what size the picture is drawn at and what the system shows (`display:` lines), and whether the
  Sharpscale plugin is listed: a picture with black borders in fights only comes from its Integer or Real mode.

## v1.3 (2026-11-14)

- **Unlock All from the menus**: hold SELECT for two seconds in the menus and the same screen shows over the game,
  which waits; CROSS gives the save in use everything at once (save in Options, or after a fight, to keep it), and
  the game goes on where it was.
- **SELECT at the start is seen at any moment**: a thread of its own looks at it every 16 ms from the program's first
  steps, so a short press counts (v1.2.2 still missed a quick press between two of the start's steps).

## v1.2.2 (2026-11-14)

- Unlock All's screen shows when SELECT was down at any step of the start, not only at the moment the screen would
  show: letting SELECT go a little early no longer misses it.

## v1.2.1 (2026-11-14)

- The `.iso` is also found one folder down in `ux0:data/bt4/`, as an archive unpacks it: Team BT4's own archive
  holds the image in `PS2_NTSC/`, so it can be unpacked into `ux0:data/bt4/` as it is.

## v1.2 (2026-11-14)

- **Unlock All**: SELECT held while the game starts shows a screen that gives the save everything (every character
  and form, stage, song and item, 4,850,000 Zenni), with the game's own unlock routine, as arielxflores's Vita port
  does. The save as it was is copied first to `ux0:data/bt4/saves/unlock_backup.bin`, and the same screen can put it
  back.

## v1.1.2 (2026-11-14)

- An `.iso` cut short (a copy that stopped early, or one on a FAT32 card, which holds files up to 4 GB) is told on
  the error screen: its size against the disc's, and what to do, instead of only "archive not found". `log.txt`
  names each file cut off.

## v1.1.1 (2026-11-14)

From arielxflores's BT3 Vita port, v1.2.1:

- BT4's files are also found where the BT3 Vita port keeps them (`ux0:data/bt3/bt4/` and its `es/`), in `ux0:bt4/`
  and in `ux0:bt3/bt4/`, unpacked or as the `.iso`: one copy of the image serves both apps.
- The start's steps and the taiHEN plugins (from `ur0:tai` and `ux0:tai` `config.txt`) in `log.txt`, written line
  by line until the game starts, so that a start that closes the game says where.
- README: the image is over FAT32's 4 GB a file (copy the unpacked files, or use exFAT).
- (His fix for music mods' songs that played once, ADX version 3 loops, was already in BT4decomp's sound code.)

## v1.1 (2026-11-14)

- **Team BT4's Spanish B14 Rev2** ([#1](https://github.com/manumastro/DBZ-BT4-Tag-Team-Decomp-Vita/issues/1)): its
  disc image is used as the English one's, in `ux0:data/bt4/` as it is or unpacked; the game tells the two apart by
  the size of `SLUS_219.78` (BT4decomp's own table of the two releases) and runs in the disc's language, with the
  Spanish release's texts and music (the anime's soundtrack, players say). The Vita's own changes to the menus'
  lettering ("Vita Settings", "Multiplayer") are made for the English disc and are left out on the Spanish one (its
  rows keep their own names and do what the English ones do). Not tested on a Vita yet.

## v1.0 (2026-11-14)

This fork's own:

- Outfit Select: a long outfit name ("Battle Damaged (Topless)") is shrunk sideways to fit its box, as the game's
  own text boxes do, instead of running off the screen's edge.

From the BT3 Vita port (its `optimized` branch), the same as there:

- **The disc image as it is** (arielxflores's reader, from his v1.2): copy your `.iso` to `ux0:data/bt4/`, under
  any name, and the game reads its files from it; no need to unpack it with 7-Zip any more (the unpacked files still
  work, and are used first when both are there).

## v0.2 (2026-11-14)

From the BT3 Vita port (its `optimized` branch, merged in as before), the same as there:

- **arielxflores's 1.2 speed-ups**, taken into this fork (his own code, its commits cherry-picked with `-x`), all
  but the dynamic fight resolution:
  - shaders shipped ready-made (the offline shader bank of Grarak's MH3 port): every shader, the vertex and effect
    ones too, goes through the cache, and the VPK carries them already compiled (`port/vita/shbank.bin`, 87
    programs, the same for BT3 and BT4, made on a Vita with `shbank=1` and checked with `shbank=2`: none missing
    in the menus or the fights), so `libshacccg.suprx` is no longer needed;
  - the outline, the glow and the colour-table passes in half precision (`fxhalf=0` goes back);
  - songs read while they play (no stop when the music changes), and the game's no-wait reads on a reading thread;
  - the game's file unpacker 2 to 5 times faster, byte for byte the same output;
  - new textures read from GS memory while the render thread draws (`texearly=0` goes back); the drawing thread's
    texture keys and state comparisons in line; the C library's thread lookups the short way and a faster memcmp;
  - the opening film no longer reads 2 MB it does not use, and no freeze when it ends or is skipped;
  - palette textures' indices no longer leak in graphics memory (slowdowns after many fights); a line a minute in
    the log with the picture rate, the memory and the texture tables;
  - profile-guided builds (`BT3_PGO=gen` / `use`, [docs/vita/building.md](docs/vita/building.md));
  - `fightres=720` / `960` in `bt3.txt` draws the fights at that size always (640 x 368 by default); 960 ran at 18.9 pictures a second in a 2 vs 2 fight (batch 33), too slow.
- **The outfit's number and name** in the character selects' Outfit Select ("2/6" between the arrows, the name
  under them): BT4decomp writes them with the PC's TrueType line renderer, which the Vita has not; written here
  with the game's own letters.
- The main menu no longer lists the PC's online entry under Duel Mode: its label is a TrueType line (blank on the
  Vita: only its icon showed) and it led to the PC's online screens.
- New LiveArea, laid out as the BT3 port's: the key art as the LiveArea's page with the logo as its start button, the
  cover in the bubble (`port/vita/tools/livearea_from_art.py`, which replaces `livearea_from_screenshot.py`).
- BT4's music no longer hashes the whole song to look for the PC version's lossless copy (none on the Vita): the
  song is read while it plays.

## v0.1 (2026-11-13)

The first version: BT4decomp (BT4 B14 Rev2 on Tenkaichi3Decomp 0.1.39.5) with the BT3 Vita port merged in, as of its
`main` at `3eeb697` (the Vita renderer with all its speed-ups, the fourth core's helper, sound, touch, the controls and
Vita Settings, mods).

- BT4's archives read with the file tables of `BIN/DBZ4.BIN`, as BT4decomp's installer does (they have no table of their
  own); the game's data tables from `SLUS_219.78`.
- Music and voice lines heard (they were played at no volume).
- The large stages: a stage of up to about 20,000 strips a picture kept on the graphics chip (it filled the chip's buffer
  and went down to 5-6 pictures a second); every one of the 35 stages averages 28.8 to 29.5 pictures a second.
- The menus' Vita art made from BT4's own lettering: "Vita Settings" in the controller page, its captions, the Vita in
  the key assignment page.
- Touch: on the controller page a tap on "Vita Settings" chose "Control Type" (the lit row's box covered it).
- Its own title (BTFV00010, "Dragon Ball Z: BT4") and folder (`ux0:data/bt4/`), beside the BT3 port; LiveArea images
  made from the game's title screen (`port/vita/tools/livearea_from_screenshot.py`).
- Screenshots in the README, taken on the console.
- Not available: online play, Tag Team's simultaneous battles, the PC version's added songs and stages, its F1 menu
  and the choice of language.
