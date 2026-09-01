---
name: map-floors-crosshair-and-party-handoff
description: The map became a per-floor screen with a crosshair, a roster, a figure and a parchment to stand them on -- atlas pages, a cursor that pushes the view, pulsing marks, and a multizorkd protocol extension that reports every seat; it has now been on a screen once, which found two faults that every host gate had passed, and two commits sit unpushed on top of the owner's own 7149ca1.
metadata:
  type: project
---

Branch `main`, **two commits unpushed**: `6964911` (the parchment) and `c9e7e52` (the
two faults the first screenshot found). `origin/main` is the owner's own `7149ca1`,
which already carries the floors, crosshair, roster and figure -- that work went up as
`cc429cd` and the owner built `7149ca1` on top of it, which is where TITLE.TGA came
from and where `gen_logo_tga.py` was deleted.

Only the working tree's `saturn/boxart/RAW_ZORK.xcf` is uncommitted, and it is the
owner's, not this work's. Leave it alone -- a rebase will refuse while it is dirty; see
the git note under "Reconnaissance".

Continues [[multigame-atlas-handoff]] and [[map-atlas-handoff]]; see "What those
handoffs now get wrong" at the end. The commit messages carry the what. This carries
what they cannot.

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
7. **A parchment behind it all.** MAP.TGA goes on NBG0, read once and held for the
   session, so opening the map never touches the drive and never stops the track.

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

## Why the parchment is on NBG0 and not anywhere else

All four VDP2 scroll layers are claimed -- NBG0 the wallpaper bitmap, NBG1 the
inventory overlay's item picture, NBG2 the dashboard tiles, NBG3 the text -- so the
question was which to borrow. Three were considered and two were wrong:

* **NBG2, as tiles.** Impossible: it has one plane, so a mark painted into a cell
  replaces the ground there rather than sitting over it. That is also why the map's
  ink had to be redrawn on transparency.
* **NBG1, which the overlay owns.** Tempting, because item_art already claims a
  512x256 8bpp one-bank container and uses only a 64x80 window of it -- the rest sits
  at index 0. The parchment would have fitted in the unused part and stayed resident
  in VRAM for nothing. It does not work: the item window is at ITEM_ART_X 240,
  ITEM_ART_Y 144, which is *inside* a 320x240 parchment, so any inventory visit would
  punch a 64x80 hole in the map. It would also have needed the two modules to agree
  about the layer's priority, and item_art sets its own inside an idempotent
  bring-up that would never have set it back.
* **NBG0.** Already priority 1 against NBG2's 2, so it needs no reordering at all;
  `title_bg_show_raw` and `room_art_reshow` already do the upload and the restore,
  and room_art re-uploads rather than trusting the layer, so the room comes back by
  itself. Nothing in item_art or room_art changed.

The cost is one 320x240 plane held in **High Work RAM** -- 78 KB including the
palette -- which is where every TGA has lived since the cache went, and deliberately
not the Low Work RAM megabyte the jingle, the archives and the trie share: that has
under 90 KB spare at its tightest pairing. `tga_decode` refuses when High Work RAM is
short, so a failure is a map with no parchment rather than a crash.

## What the first screenshot showed

`SaturnRingLib/emulators/mednafen/snaps/Zaturn (USA) (Netlink Edition)-0002.png` is the
first time any of this has been on a screen, across four sessions of work. It found two
faults that every host gate had passed, and both are fixed in `c9e7e52`.

**A nineteen-by-fifteen cell rectangle of black in the middle of the parchment.** It is
the VDP2 window that suppresses NBG0 inside a box -- `console_view.cxx`'s
`image_window_on`. `MenuBacking`'s constructor switches it on and every `menu_frame`
aims it at the box being drawn, so a full-screen page that draws no box silently
inherits whatever rectangle the menu that opened it last used. Nineteen by fifteen at
cell (10,7) is the Options box.

The part worth carrying: **it was not a new fault.** It had been true for as long as
the map existed and was invisible only because the map paved itself with opaque ground.
Taking the ground away exposed it. Expect more of that shape -- anything else that was
hidden behind those tiles is now on screen.

**The back colour came out black instead of tan.** `MAP_GROUND_555` is a *tint target*:
`dash_view.cxx`'s `write_palette` takes it apart into channels and ORs the opaque bit
back itself, so the constant carries no bit 15. Every SRL `HighColor` does --
`HighColor::Colors::Black` is `0x8000`, not `0` -- so handing that word straight to
`SetBackColor` produced black. It is now set with the bit **and only where there is no
parchment**, because black behind the sheet's torn edges is what the picture is drawn
for and tan there would flatten the shape it is cut to.

Two things looked wrong and were not, both confirmed before touching anything:

* **The room name at bottom left was missing.** The crosshair was at cell (36,8), which
  is empty parchment -- nothing to name. The view had followed it to `sx = 4`, which is
  also why the figure and the player's own room sit at the far left rather than centred.
* **The reticle looked absent.** It renders at `(208,192,144)` against parchment
  `(208,152,96)`: present, and subtle. That is the "does the ink read on paper" question
  with a number on it at last. `MARK_XHAIR` in `gen_dash_tiles.py` is the one constant
  to move if it is too faint in play.

**How to read the next screenshot, because eyeballing it wasted several passes.** The
screenshot is 330x240 and the picture sits at `+5, +0` inside it. Diff it against
`saturn/cd/data/TGA/MAP.TGA` per 8x8 cell and print one character each for "painted
black", "ink drawn" and "parchment untouched": that renders the whole 40x28 layer as
text and shows exactly which cells NBG2 claimed. It found the window's rectangle to the
cell in one pass, after guessing had failed. Use a *low* difference threshold, around
25 of 765 -- the reticle is 40 counts away from the paper and a threshold tuned to the
dark ink misses it entirely, which is what made it look like it was not being drawn.

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

**An image editor's default TGA export passes none of tga_decode's gates.** MAP.TGA
arrived as 32bpp RLE (imgtype 10, no colormap), which the decoder rejects -- and every
caller reads a rejection as "show nothing", so the failure would have been a blank
screen on hardware and nothing anywhere else. `tools/gen_tga.py` is the encoder that
produces what it wants, recovered from `7149ca1~1` where it had been deleted as
`gen_logo_tga.py` once the boot logo was the only TGA left, and generalised from that
one picture to any. `saturn/tests/test_tga_assets.py` now gates every shipped TGA
against the same six checks the decoder makes, plus the 512x256 one-bank ceiling the
decoder cannot see.

**Index 0 is transparent, and for the parchment that is a feature.** tga_decode builds
its CLUT with `Opaque = 0` for entry 0 alone. The logo's encoder reserved index 0 and
left it unused; the parchment's torn edges *want* it, so gen_tga.py maps a source
alpha below 128 to index 0 and paints those pixels over with the commonest opaque
colour before quantizing, so a transparent region cannot spend palette slots on a
colour nothing draws.

**A heredoc in this environment eats one level of backslash.** `python - <<'PY'` with
`"a\\nb"` in the body arrives as `"a\nb"`, so a patch script matching C source with
`\0` or `\n` in it silently fails to match, or worse, matches something else. Write the
script to the scratchpad with the editor and run it by path. This is a second instance
of the quirk already recorded for stdin.

## What no gate could prove

Both targets **compile and link**: `.\compile-netbin.bat` then `.\compile-cd.bat` from
PowerShell in `saturn/`. Under git-bash the netbin needs `PATH` set by hand -- the `:;`
bash line in those `.bat` files only handles Darwin and Linux -- and the ISO step then
fails on a missing `xorrisofs`, which does not stop `cd/data/0.bin` being produced.

**One frame has been seen, and the fixes it prompted have not.** `c9e7e52` was built
and gated but never displayed, so the first thing to do is open the map again and check
the rectangle is gone and the sheet is whole.

Confirmed working from that one frame: the parchment, the figure, the roster line, the
floor indicator, the marks (dark on tan, light on black), the links, the reticle, the
crosshair clamping and the view following it.

Still unseen:

* Whether the ink reads on the parchment **in motion and on a CRT**. The one frame says
  it reads in an emulator screenshot, which is a weaker claim than it sounds: composite
  blur and a real tube are what the near-white ring has to survive.
* Whether four marks -- room, here, peer, picked -- are distinguishable from each other,
  as opposed to merely visible. Only the ordinary mark and the player's were on screen.
* Whether the pulse at sixteen frames a half reads as a beat or as a flicker.
* Whether paging feels like floors or like the map losing half of itself. Nothing has
  pressed L or R yet.
* Anything on the netbin at all: no drive, so it always takes the no-parchment path and
  its map is flat tan rather than marble. The whole roster, which is the only reason the
  protocol work exists, has never had a second player in front of it.
* Whether anything *else* was hiding behind the old opaque ground. The window was one;
  there is no reason to assume it was the only one.

**The netbin grew about 4 KB**, 191,552 to 195,632, measured by building `HEAD` and
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

```
python tools/gen_tga.py <source image> saturn/cd/data/TGA/<NAME>.TGA
python saturn/tests/test_tga_assets.py
```

`gen_tga.py` needs Pillow and is run by hand only -- the disc's three TGAs are all
committed and the build converts nothing.

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

- **superpowers:systematic-debugging** -- now the governing one, and it has just been
  earned. The two faults arrived as "black screen over image" and were found by
  measuring the screenshot rather than by reading code; every remaining open question is
  the same shape. Measure before hypothesising -- three plausible causes were wrong
  before the per-cell diff named the real one.
- **superpowers:verification-before-completion** -- still true, with the emphasis moved.
  Host gates passed a build with a black rectangle through the middle of it, so "the
  tests are green" says nothing about this screen. `c9e7e52` itself has not been seen.
- **code-review** -- before pushing the two commits. They touch the tile set, a new
  session-held resource in `title.cxx`, and a VDP2 window two other modules share.
- **superpowers:test-driven-development** -- specifically its discipline of watching a
  test fail first. Every new assertion this session was run against the old code and
  required to fail; `test_dash_tiles` caught the transparency change on its own, and the
  page-derivation rule was thrown away *because* it was checked and found wrong.
