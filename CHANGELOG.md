# Changelog

The changes of the PS Vita port of BT4. BT4decomp's own, for the PC: [docs/port/CHANGELOG-BT4decomp.md](docs/port/CHANGELOG-BT4decomp.md).

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
  of its own.
- Not available: online play, Tag Team's simultaneous battles, the PC version's added songs and stages, its F1 menu
  and the choice of language.
