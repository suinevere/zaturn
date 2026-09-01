---
name: map-floors-crosshair-and-party-handoff
description: The map became a per-floor screen with a crosshair, a roster and a figure -- atlas pages, a cursor that pushes the view, pulsing player marks, a knight, and a multizorkd protocol extension that reports every seat; both targets link, the netbin grew 3.5 KB against a 400 KB limit, and none of it has been seen on a screen.
metadata:
  type: project
---

Working tree on `main`, uncommitted at the time of writing. Continues
[[multigame-atlas-handoff]] and [[map-atlas-handoff]]; see "What those handoffs now
get wrong" at the end. The commit messages carry the what. This carries what they
cannot.

## The story-file trap this walked into

The first regeneration silently produced **seventeen** games instead of eighteen:
`saturn/cd/data/Z3/CUTHROAT.Z3` was missing from the working tree, so
`gen_map_atlas.py` said "not on the disc" and emitted no table for it. Only four files
in that directory are tracked -- `GAME.INF` and the three Zorks -- so a checkout has
almost none of the disc's stories and the generator will quietly narrow the atlas to
whatever happens to be present.

That was **not** the harmless loss it first looked like. `Z3/GAME.INF` is a
thirty-one record manifest and it names CUTHROAT, so the disc has always expected the
file; its absence was a hole in the working tree, not a game the disc does not carry.
The owner restored it and the table is back: eighteen games, 861 rooms, Cutthroats on
two floors at 39/41 exits.

**Check the count before installing a regenerated `.inc`.** The generator reports
"N games pass, M dropped" on stderr and a game absent from `Z3/` never reaches either
number -- it is a third outcome that looks like nothing at all.

**Do not try to recover a dropped table's floors from its coordinates.** That was the
first plan and it is wrong. The generator stacks each drawn page into its own band of
rows with a three-row gap, so "split on a gap of three or more" looks exact -- and
against Zork I it finds five bands where there are three pages. Pages have internal
gaps too. The check is in the session scratchpad and was thrown away; the result is
what matters.

## What was actually built

Six things, all of them in the map screen:

1. **Floors.** Every atlas cell now carries the PDF page it came off, and the map draws
   one page at a time with L/R paging. The page rides in the byte the struct was already
   padding -- `room` went `unsigned short` to `unsigned char`, which a v3 story's 1..255
   object numbering makes free -- so 861 cells carry their floor for zero bytes.
2. **A crosshair.** The D-pad moves a cursor, not the map; the view follows only when
   the cursor would leave it. The picked room's name is bottom-left, the floor number
   bottom-right.
3. **A roster.** Up to four `name: room` lines top-left, or `Player: room` offline.
4. **Pulsing marks.** Every player's mark alternates with `DT_ROOM` every sixteen frames.
5. **A knight**, two cells by three, standing left of the local player's mark.
6. **A protocol extension** so the netbin can know any of that: multizorkd now sends
   `S<seat>` and `P<seat><room><name>` frames beside the existing `R<room>`.

## Judgement calls the owner made, so do not re-open them

Put to the owner and answered before any code was written:

* **Filter to one floor**, rather than keeping the stacked strip and using L/R as a
  scroll shortcut. So `gather` skips other floors and a staircase leaving the floor
  simply has no far end to draw to.
* **Extend the multizorkd protocol**, rather than showing only the local player.
* The knight was **16x24** -- the owner resized the asset mid-question so it divides
  into exactly 2x3 cells. Do not re-scale it; `gen_dash_tiles.py` asserts the size.

## Two things the owner has not been asked

* **The roster names our own seat from the server, not from the model.** The line and
  the mark can disagree for a turn if the `P` frame lags the `R` frame. It has not been
  seen; it is one frame if it happens.
* **The knight covers a link running west out of the player's room.** Unavoidable with a
  figure on a tile layer, and it is why there is a cell of clearance rather than none,
  but it is a visible choice nobody has looked at.

## Reconnaissance worth not repeating

**`gvar_location` in multizorkd is byte-swapped and `get_room_name` does not know.**
The global is loaded and stored through a `uint16*` cast over the story image
(`multizorkd.c:1623,1721`), so on a little-endian host it holds the swap of the real
object id. `write_room_id` already knew this and reads the two bytes big-endian at its
own call site. `get_room_name(inst, loc)` does not: it guards `objid <= 255`, and every
swapped room id is at least 256, so **every "*** X entered Y. ***" broadcast has been
printing an empty room name.** That is pre-existing and was left alone -- fixing it
changes game text -- but `player_room_objid` now exists next to it and is the fix.

**The netbin's out-of-band parser was already forward-compatible and this proved it.**
An old client meets the new `P` frame, buffers to its own cap, sets `oob_len = -1` and
discards to the terminator. `test_party_frames.c` run against `HEAD`'s `term.c` still
passes every console assertion and fails only on the roster ones -- which is the
compatibility claim, checked rather than asserted.

**Do not compare two `term.c` builds without pinning `term.h`.** The first attempt at
that check put the old `term.c` in a directory holding the old header; a quoted include
resolves against the including file's own directory first, so the two translation units
disagreed about `TERM_OOB_MAX` and therefore about `TermState`'s layout, and the failure
landed on `room_id` -- a field the old parser handles perfectly. It looked like a real
defect and was an ABI mismatch.

**A heredoc in this environment eats one level of backslash.** `python - <<'PY'` with
`"a\\nb"` in the body arrives as `"a\nb"`, so a patch script matching C source with
`\0` or `\n` in it silently fails to match, or worse, matches something else. Write the
script to the scratchpad with the editor and run it by path. This is a second instance
of the quirk already recorded for stdin.

## What no gate could prove

Both targets **compile and link**, which is further than the last three map sessions
got: `.\compile-netbin.bat` then `.\compile-cd.bat` from PowerShell in `saturn/`.
Under git-bash the netbin needs `PATH` set by hand -- the `:;` bash line in those `.bat`
files only handles Darwin and Linux -- and the ISO step then fails on a missing
`xorrisofs`, which does not stop `cd/data/0.bin` being produced.

**Nothing has been run.** Specifically unseen:

* Whether four marks -- room, here, peer, picked -- are actually distinguishable on a
  CRT. They are one greyscale ramp bent to a single tan, which is why two of them
  differ in shape as well as value, but that reasoning has never met a television.
* Whether a 1-pixel line-art figure at 16x24 reads at all in the map's dark ink.
* Whether the pulse at sixteen frames a half reads as a beat or as a flicker.
* Whether paging feels like floors or like the map losing half of itself.

**The netbin grew about 3.5 KB**, 191,552 to 195,024, measured by building `HEAD` and
the working tree and comparing the packaged `zaturn.netbin`. The tile set's twelve new
tiles are 384 of that; the rest is code. There *is* a size gate, contrary to what the
earlier handoffs implied was unmeasurable: `post.makefile` prints and enforces
`limit 409600` at the packaging step and `release.yml` asserts the same number, so this
sits at 48% of it with room to spare. `test_netbin_sources.py` is a different check --
it gates the source list, not the size.

## The CI check that was wrong about the workflow

`test_ci_boot_music.py` had been failing with "release.yml never invokes
tools/assets/pvms.bat", and the workflow was fine. `release.yml` stopped calling
`pvms.bat` and `make.sh` itself some time ago in favour of `bash compile.bat release`
-- which runs pvms on both of its halves and is the one place the netbin-first ordering
is written down -- and the check only knew the direct route. It now accepts either, and
for the `compile.bat` route proves the guarantee instead of assuming it: `compile.bat`
must still invoke pvms on both its POSIX line and its Windows half, and the workflow
must still assert `SPLASH.PCM` after the build.

It was checked by mutation rather than by passing: six deliberate breakages -- sox
dropped, the PCM assertion dropped, the build step removed, either half of
`compile.bat` losing its pvms call, and `full-image.yml`'s pvms moved after `make.sh`
-- and all six are caught. The mutation driver lives in the session scratchpad and is
gone; rebuild it before trusting a future edit to that file.

It also no longer calls `sys.exit` at import, which is what let one failure abort
pytest collection for the whole directory. `python -m pytest saturn/tests` now
collects and passes 30.

## Build and test commands

```
python tools/gen_map_atlas.py --cache <dir outside the tree> > saturn/src/engine/map_atlas_data.inc
python tools/gen_dash_tiles.py > saturn/src/video/dash_tiles.c

gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/engine -o /tmp/tma \
    saturn/tests/test_map_atlas.c saturn/src/engine/map_atlas.c
gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/engine -o /tmp/tmm \
    saturn/tests/test_map_model.c saturn/src/engine/map_model.c saturn/src/engine/map_atlas.c
gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/video -o /tmp/tmlay saturn/tests/test_map_layout.c
gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/net -I saturn/src/input -I saturn/src/video \
    -o /tmp/tpf saturn/tests/test_party_frames.c saturn/src/net/term.c saturn/src/net/party.c
gcc -std=c11 -O2 -Wall -o /tmp/tt.exe saturn/tests/test_term.c saturn/src/net/term.c \
    saturn/src/net/party.c saturn/src/video/console.c saturn/src/input/keyboard.c \
    saturn/tests/net/mock_transport.c -I saturn/src -I saturn/src/net -I saturn/src/video \
    -I saturn/src/input -I saturn/tests

cd saturn && sh syntax-check.sh <files>          # CD build
cd saturn && NETBIN=1 sh syntax-check.sh <files> # netbin build
```

`gen_map_atlas.py` needs pymupdf, opencv-python, numpy and rapidocr-onnxruntime, fetches
its PDFs with `curl`, and takes about forty seconds a game -- fifteen minutes for all
twenty-two. `gen_dash_tiles.py` now needs Pillow, and only for the knight: it reads
`tools/assets/png/KNIGHT.PNG` rather than carrying a transcribed copy, so the drawing
and the tiles cannot drift.

`multizorkd.c` **was not compiled.** It needs POSIX headers this machine's git-bash gcc
does not have, Docker's daemon was not running and the only WSL distro has no compiler.
The client half of the protocol is covered by `test_party_frames.c`, whose frames are
written as literal bytes and are the contract the server must meet; the server half has
been read and not built.

## What those handoffs now get wrong

[[multigame-atlas-handoff]] is **PARTLY STALE**. Its account of the OCR, the three bugs
and the two open ship-or-wait calls all stand. These do not:

- "about 3.4 KB" for the table -- the cell struct is unchanged at four bytes and the
  room count is unchanged at 861, so the size is what it was; it now also carries a
  floor per room, for nothing, in the byte the struct was already padding.

[[map-atlas-handoff]] is **PARTLY STALE** on top of what that handoff already corrected:

- "D-pad: scroll" -- the D-pad is a crosshair now and the view follows it.
- Its worry that `draw_once` runs on every scroll step is unchanged in kind but the
  screen now also repaints two to five cells per frame for the pulse.
- The Python mirror of `map_view.cxx`'s routing that it called "the single highest-value
  thing available to the next session" still does not exist. `map_layout.h` and
  `test_map_layout.c` are a first piece of it -- the viewport arithmetic is now out of
  the SRL translation unit and checked exhaustively on the host -- but the link router
  is not.

## Suggested skills

- **superpowers:verification-before-completion** -- unchanged and still governing. Both
  targets link and every host gate is green, and not one pixel of this has been seen.
- **superpowers:systematic-debugging** -- the four open questions are all "does this look
  right", which arrives as an observation and not a stack trace.
- **code-review** -- before pushing. This touches the atlas format, the tile set, the
  wire protocol and the server in one change.
