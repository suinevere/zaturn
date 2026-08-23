---
name: scene-tagged-art-handoff
description: The mood classifier is deleted and replaced by per-room scene tags with per-game art folders, squashed onto main as f381dac; what remains is two owner tasks, a wallpaper-less title screen, and a small parked residue.
metadata:
  type: project
---

**PARTLY STALE as of 2026-08-23.** The art half was rebuilt game-first in
`ecd2808` and the undo/review work landed in `d674a0d`; where this document and
those commits disagree, the commits win. The scene-tagging half still stands.

Landed on `main` as a single squashed commit `f381dac`, +26,584 / −5,740 across
166 files, pushed to `origin/main`. Suite **231 passed, 0 failed**. All eight host
C tests pass, every SH-2 `syntax-check.sh` is clean with zero warnings, and both
generators regenerate byte-identically. **The owner has not built or run it once.**

The unsquashed 31-commit history survives locally as branch `scene-tagged-art`
and tag `pre-squash-scene-tagged-art`, both at `4bc7959`. Its per-task commit
messages are the only record of which change belonged to which task; delete them
once you are sure you will not want that.

Spec `docs/superpowers/specs/2026-08-21-scene-tagged-per-game-art-and-audio-design.md`,
plan `docs/superpowers/plans/2026-08-21-scene-tagged-per-game-art-and-audio.md`.
Supersedes the curation half of [[genre-banded-art-handoff]] — the period-axis
flaw it describes is gone, because genre bands died with moods.

## What replaced what

A room's picture and music no longer come from reading the turn's text. They
come from a scene tag a human assigns to that specific room in that specific
game, looked up by Z-machine object number. Three array reads replace ~1100
lines of keyword tables, tiers, genre masks, memo caches and fallback counters.

Background art moved from shared mood folders to **one flat folder per game** —
`ZORK1/07.TGA` — with each scene owning an index range inside that game's 1..99.
`g_file_buf[2][16]` and the slot encoding are untouched, so saved games stay
valid.

Danger and triumph survive as the one text-driven thing, in
`saturn/src/sound/event_scan.{c,h}`. They carry no picture and are **deliberately
never announced to display subscribers** — `SC_FOREST` and `EV_DANGER` are both
0, so announcing one would swap the background to a forest mid-sting.

## The two things the owner has to do

**Bless Zork I.** `tools/.venv/Scripts/pythonw.exe tools/scene_server.py` on
:8081, then `/game/ZORK1`. 69 of 110 rooms are already tagged by rule; **35
groups** remain, repeated titles collapsed, so the fifteen `Maze` rooms are one
keystroke. 855 old mood judgements survive as hints in
`tools/assets/blessed_moods.json`, scoped per story.

**Source its art.** `tools/fetch_art.py --game ZORK1 --scene FOREST`, curate at
`tools/art_server.py` on :8080. Zork I needs 13 scenes: CAVE, DARKROOM, FOREST,
HOUSE_EXT, KITCHEN, MAZE, MINE, PARLOR, PIT, RIVER, ROCKY, SHORE, TEMPLE.

**It starts from zero.** The half-curated pool this handoff originally
described was fetched under the mood vocabulary into `png/<SCENE>/<noun>/`,
which `make_tga.convert_game_tree` never read -- it walks `png/<GAME>/<SCENE>/`
and skips any top-level directory that is not a game stem, so not one of those
412 pictures could ever have reached a disc. The manifest, the tree and the
rescue migration were deleted in `ecd2808`; see [[art-data-is-disposable]].

## What is deliberately unfinished

**The title screen has no wallpaper.** This is the one visible regression. It
used to show a `TC_HOUSE` picture chosen at boot, but with art per game there is
no game at boot. It now draws from a shared `TITLE/` folder addressed by literal
filename, outside the scene machinery — and `TITLE_ART_N` is 0 because
`tools/assets/png/TITLE/` does not exist. Supply images there and re-run
`make_tga.py`. Until then `title_bg_hide()` runs and the logo sits on plain
background.

**`SCENE_TRACKS` is all zero**, so every scene falls back to the neutral CD-DA
pool and no two scenes sound different yet. Per-game track selection over the
shared 31 tracks is data, not code — art is duplicated per game, audio cannot be,
because those tracks are already ~85% of the disc.

## Traps worth knowing

`art_manifest.json` is **gitignored**, with `art_status.py --snapshot` writing
the committed copy at promote time. Both are empty as of `ecd2808` and the old
backups are worthless: every record in them predates the game-first layout.
Room *tags* are the opposite -- hand-made and irreplaceable. Never conflate the
two.

`.gitattributes` pins `saturn/src/scene/**` to `eol=lf`. Without it the
generators write LF, `core.autocrlf` checks out CRLF, and the byte-identical test
fails on a fresh clone — not here, where the files happen to already be LF.

`saturn/src/scene/scene_map.h` is **partly generated**, between
`GENERATED SCENE ENUM -- BEGIN/END` sentinels. Hand edits go outside them.

`scene_vocab.SCENES` order **is** the C enum value and a column index in three
generated tables. Appending is safe; reordering silently repoints every row.

The rules **refuse rather than guess**. A `(pattern, None)` row is a deliberate
early refusal — `bridge` refuses because 7 of 9 bridges in the library are stone
or foot bridges, not ships'. A guard test proves no rule is shadowed by an
earlier substring, which is how `forecastle` was found dead behind `castle`.

Related: [[genre-banded-art-handoff]], [[user-runs-all-builds]],
[[work-can-vanish-via-clion-rebase]], [[never-print-via-srl-debug]],
[[classify-from-captured-turn-text]]
