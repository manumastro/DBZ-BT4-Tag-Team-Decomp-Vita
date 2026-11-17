# Batch 33: the build that carries the pending branches (2026-11-17, branch `wip/batch33`)

One dev build with `tagteam` (f1b9cc2) plus `wip/tagmovie` (the film, `tbomovie`, default 0), `wip/unlock`, `wip/disc`,
`wip/screen`, `wip/docs` (the Reddit fixes of 2026-11-16). Not merged into `tagteam`. Build `port/build/vita_dev_b33`
(copy also in `bt4-vita/port/build/vita_dev_b33` for the runner), built 2026-11-17 in 246 s from scratch (8 jobs).
Defaults are those of the branches: `tbomovie=0`, `tbomoviecopy=1`, `tbomoviecsc=0`, `tbomoviemem=0`; the Reddit fixes have no
`bt3.txt` switch (the clash assist and the stick shaping are off in `bt3_settings.txt` until set in Vita Settings; Unlock All is
only when asked; the disc checks and the log lines are always on).

How the merge was done: conflicts kept both sides (settings tables, `Port_PadRead`, the Vita Settings page, README, CHANGELOG).
Where the two sides met in code:
- `Port_PadRead` (`vita_gfx.c`): `clash_pad` (wip/screen) and `stick_shape` (both sticks) first, then the free camera's copy of the
  pad (`sSpecPad`, tagpc) and its filter: the camera sees the shaped sticks, as the game does.
- Vita Settings guide (`menu_x_c.c`): wip/unlock's "SQUARE: Unlock All." line is added only when the text has fewer than three
  lines (wip/screen's lower-row texts have three already: "Down: the sticks.").
- README: wip/docs' "Tag Team comes later, no label, HUD words not drawn" was out of date and dropped; its Sharpscale line gives
  way to wip/screen's picture-size entry (`fightres=960`).

## Runs (sets in `psvita-agent/sets/bt4/`)

Film batch, 70 s a run (`SECONDS_RUN=70`), the first run sends the eboot:
`SECONDS_RUN=70 ~/psvita/agent-tools/run_batch.sh b33a vita_dev_b33 tagmovie_on tagmovie_grid tagmovie_back tagmovie_nocopy tagmovie_csc1`

| run | what it checks | passed when (in `log.txt`, `shots/`) |
|---|---|---|
| `tagmovie_on` | handoff step 1: the pool of vitaGL given back, the film plays | `tagmovie: vitaGL's physically contiguous pool ... given back` (after `the pool: 26623 of 26624 KB free`; a pool with something in it gives the poster, the line says how much); `tagmovie: first picture N ms after the source`; the 2 s lines `N frames from the player, N shown ... N late` with frames shown; the 3 pictures in `shots/` red and purple, no magenta / green blobs, no hard edge near x=710. Failed as batch 32 if `IsActive=0` and no picture after 1 s, or an error code of the player (0x80024302...) |
| `tagmovie_grid` | the pool taken again after the screens close; wip/unlock's character grid log | START at 30 s (to the character select): `tagmovie: ... taken again: 26624 of 26624 KB` after it; `vita: character grid (menu mode N): X of its characters and forms locked` lines; `shots/in00030.jpg` / `in00048.jpg` show the character select; no crash |
| `tagmovie_back` | the same by CIRCLE at 30 s | `taken again: N of N KB` after 30 s; `in00030.jpg` shows where CIRCLE went (if the screens did not close there is no such line: not a fault of the film) |
| `tagmovie_nocopy`, `tagmovie_csc1` | handoff step 2, the colours: copy off; CSC1 (only worth running if `tagmovie_on` showed pictures) | as `tagmovie_on`; compare the 3 shots with `tagmovie_on`'s: a variant whose pictures are all right names the cause (compare the `picture N: ...` addresses of the wrong ones) |

Regression batch, 130 s a run (`SECONDS_RUN=130`), no eboot to send:
`SECONDS_RUN=130 ~/psvita/agent-tools/run_batch.sh b33b - u0 hx0 u0_movie u0_fr960`

| run | what it checks | passed when |
|---|---|---|
| `u0` (2 vs 2, defaults) | the merge changed nothing in fights; wip/disc and wip/screen log lines | `sumrun.py`: 29.4 to 29.6 pictures/s (40-118 s), `<15:0 <20:0`; `vita: minute 2: ... heap 48.1 MB` (48147 KB in batch 31; 64 MB or more = the vitaGL pool is lost); no `tagmovie:` line; rc=0, no crash dump. Start lines (wip/disc): `vita: BT4 B14 Rev2, the Spanish release (SLUS_219.78 of 2115688 bytes)`, three `vita: <file>: B14 Rev2 Spanish's (CRC-32 ...)` (a `not B14 Rev2` line or a FATAL is a false alarm of the check: report it), `vita: the game's files: the disc image ...`, `vita: data folder ux0:data/bt4 (there already)`, `vita: the port's own files ...`. Display lines (wip/screen): `display: Sharpscale is not in ur0:tai/config.txt` (and ux0), `vgl: the picture is drawn at 640 x 368 from list N (...)`, `display: the system shows a buffer of 960 x 544` |
| `hx0` (1 vs 1 heavy, defaults) | the same for the heavy 1 vs 1 | 29.8 pictures/s, `<15:0 <20:0` (batch 27's), no crash; the same start lines |
| `u0_movie` | `tbomovie=1` with the pool kept: the fights' memory unchanged | as `u0`, heap 48.1 MB; `vgl: 0 MB ... left to the film's player ... the pool: N of N KB free` line |
| `u0_fr960` | wip/screen: `fightres=960` (the README's workaround for the black borders) | `vgl: the picture is drawn at 960 x 544` and the buffer line; speed noted (it may be slower: not a pass / fail) |

Not run automatically (they would touch the developer's saves or need other files): the manual checks in the report of the
batch's author: Unlock All (three ways, Undo, the `.bak` files), wip/disc's messages with a cut `.iso`, an archive or another build,
Vita Settings (Clashes, Stick pages) and the clashes themselves.
