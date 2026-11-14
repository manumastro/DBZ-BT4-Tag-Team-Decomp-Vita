# Changelog

The changes of the PS Vita port of BT4. BT4decomp's own, for the PC: [docs/port/CHANGELOG-BT4decomp.md](docs/port/CHANGELOG-BT4decomp.md).

## Unreleased

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
