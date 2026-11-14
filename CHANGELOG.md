# Changelog

The changes of the PS Vita port of BT4. BT4decomp's own, for the PC: [docs/port/CHANGELOG-BT4decomp.md](docs/port/CHANGELOG-BT4decomp.md).

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
  - `fightres=720` / `960` in `bt3.txt` draws the fights at that size always (640 x 368 by default).
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
