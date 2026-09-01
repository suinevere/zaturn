---
name: inventory-item-pictures-handoff
description: PARTLY STALE on the overlay's geometry and the archive's lifetime, both superseded by the 2026-09-01 handoff; the picture binding, the TVALUE measurement and the bg.bat staging bug still stand. The inventory overlay grew a picture pane showing the Japanese Zork I disc's own paintings of carried treasures, bound to the nineteen objects the story's own TVALUE count proves the picture set to be.
metadata:
  type: project
---

**PARTLY STALE.** [[inventory-overlay-frames-and-caching-handoff]] supersedes two things
here: the overlay is no longer a 34-column printed box at column 2 with a bare pane
(it is the strip itself, split by a tiled divider, with the picture in a frame of its
own), and OITEM.CZ is no longer resident only while the overlay is up (it is read at
game load and held for the session). Everything else below still holds.

Continues [[cgl-only-presentation-handoff]], whose `/BG` injection this rides on, and
[[marbled-menus-handoff]], which is why the pane sits on marble rather than on a
transparent box. Discharges sub-project C of
[`docs/superpowers/specs/2026-08-30-zork1-authentic-backgrounds-and-audio-design.md`](../docs/superpowers/specs/2026-08-30-zork1-authentic-backgrounds-and-audio-design.md).

Design: [`docs/superpowers/specs/2026-08-31-inventory-item-pictures-design.md`](../docs/superpowers/specs/2026-08-31-inventory-item-pictures-design.md).
Plan: [`docs/superpowers/plans/2026-08-31-inventory-item-pictures.md`](../docs/superpowers/plans/2026-08-31-inventory-item-pictures.md).
Both are current; read them for the what. This file carries only what neither they nor
the commit messages say.

## Where the repo is

Branch `inventory-item-pictures`, 19 commits, **not merged and not pushed**. Branched
from `main` at `1c73719`. `main` is untouched.

Code commits in order: `f1ad4b7` (ship OITEM.CZ) → `9e7f7ee` (record table + fixture)
→ `b3a8a1a`/`057f62b`/`1bb9ef6` (decoder) → `546f40e` (binding) →
`e6d1b63`/`cda36fa`/`e26184e`/`2e7e69e` (item_art) →
`98d7b2a`/`7220e64`/`77a935b` (overlay) → `71e3c15`/`65f7772` (wiring) →
`20210da`/`405509f` (final review fixes).

## What was actually hard

**Nothing about the codec.** `OITEM.CZ` turned out to be the same Okumura LZSS the room
archives use, in a flatter container — nineteen 5,120-byte pictures then nineteen
512-byte CLUTs, picture *i* pairing with record *19+i*. `cgl.c`'s decoder was lifted into
a record-agnostic `cgl_lzss` and both formats now share it; `test_cgl` over all 75 room
frames is the gate that the lift changed nothing.

**Identifying the pictures was settled by measurement, not by eye.** `1dungeon.zil` gives
22 objects carrying a `TVALUE`; drop `SWORD` (TVALUE 0) and the two damaged variants and
exactly 19 treasures remain, against exactly 19 pictures. That turned an open
identification problem into a 19↔19 assignment, of which 17 fell out unambiguously.

**Two cells were a judgement call and are recorded as such.** `#00` (a straight white
shaft, blunt butt) and `#13` (a thin tapering rod topped by a brass sphere) are the
sceptre and the bauble in one order or the other. The ZIL's *"an ornamented sceptre,
tapering to a sharp point"* argues for `#13`; the objects themselves argue the other way.
The owner called it on the objects. **If the pane ever shows a bauble that looks like a
staff, `tools/assets/zork1_items.json` rows `"0"` and `"13"` are the swap.**

## The bug that nearly shipped, and why

`bg.bat` mirrors the staged archives into the SDK's CD tree with a `*.CGL` glob, in both
its sh and cmd halves. `OITEM.CZ` does not match. Nothing automated staged it anywhere;
it was in the working tree only because a task was told to copy it by hand.

On a clean checkout the feature would have been a no-op, and the failure lands on the one
game meant to work: `tall` keys on `item_art_available()`, true for Zork I whether or not
the archive opened — so Zork I gets the tall box with a permanently dead pane, and
`item_art.cxx` retries the open on every cursor move, a blocking `cd_enter_root` +
`ChangeDir` + `Exists()` per D-pad press. CI was never at risk: `games.bat` `-map`s the
whole staging directory and `full-image.yml` verifies off the built image.

It survived eight tasks and nine reviews because the instruction that kept the build fast
— *don't run `bg.bat`, copy the file by hand* — routed around the exact code path needing
the edit, and `bg.bat`'s behaviour was then read from its docstring rather than its globs.
[[cgl-only-presentation-handoff]] already records this lesson verbatim: **grep the build
system and the CI, not just the scripts.** Having the lesson written down did not prevent
repeating it; only the whole-branch review caught it.

Fixed in `20210da` (`cp BG/*`, and `full-image.yml`'s staging gate now counts
`BG_MANIFEST` rather than `*.CGL`). Proven by deleting the mirrored copy, running the
command read out of `bg.bat` itself, and reading `/BG/OITEM.CZ` back off the finished
image — 40,840 bytes, hash matching.

## Two SRL traps worth keeping

- **`SRL::VDP2::NBG1::LoadBitmap` has two silent early returns** — allocation failure and
  CRAM palette exhaustion. Neither reports anything, and `CellAddress`'s sentinel is
  `(void*)(VDP2_VRAM_A0 - 1)`, one byte *below* bank A0. Writing through it goes straight
  into NBG0's wallpaper. `layer_ensure` now verifies `CellAddress >= VDP2_VRAM_A0` (SRL's
  own internal test) **and** `TilePalette.GetData() != nullptr`, and refuses otherwise.
- **`sgl.h` carries `#define pal COL_32K`**, arriving transitively through `<srl.hpp>`.
  Any field or variable named `pal` in such a translation unit fails to compile, with
  errors nowhere near the declaration. Lowercase only; `Pal` is safe.
- `SRL::Bitmap::IBitmap::GetInfo()` is declared `const`. An override without it does not
  override, and the class is abstract.

## What has and has not been seen on screen

**Confirmed on hardware, Zork I:** the picture is correctly placed, follows the cursor,
gives an empty black plate for an unbound item, and vanishes when the overlay closes; the
box and input line sit right; console text and marble are undisturbed. The NBG1 bring-up
was separately proven by a throwaway checkerboard spike before any of it was built.

**Not yet seen:** the ten-row list paging past ten items, and **anything at all in a
non-Zork-I game** — the other-games regression is unverified on screen. `compile-netbin.bat`
was run once here: 190,592 bytes against a 409,600 limit.

## Owner tasks

1. **Check a non-Zork-I game on screen.** Take something, **change rooms**, then pick
   "invent" from the panel. Expect the old seven-row box, five items, no pane, no stall.
2. **Check the ten-row list** with more than ten items carried.
3. **Decide on the final scoped re-review.** The plan's process calls for one re-review of
   the final fix wave; it has not been run. Everything in that wave was verified by
   executable checks instead (see the ledger), but a re-review is the designed gate.

## Things found that are not this feature's to fix

- **`test_netbin_sources.py` reported success while running nothing.** A bare script with
  no `test_*` functions: `pytest` collected zero items and exited 0. It is the check that
  no CD-only source leaked into the netbin build, and several dispatches invoked it exactly
  that way. Given a pytest entry point in `405509f`; it now passes both ways.
- **Three bare-script tests abort pytest's collector** — `test_ci_boot_music.py`,
  `test_multizork_join.py`, `test_multizork_lobby.py` — against a missing CI workflow step
  and a live multiplayer server. `pytest saturn/tests` cannot run as one command without
  ignoring them. Pre-existing, untouched, worth an issue.
- **The overlay is unreachable until the player changes rooms.** `room_model`'s player
  object is deduced by intersecting two consecutive rooms' children, so `ncarried` is 0
  until the first move, and `cv_cmd_accept` requires `ncarried > 0`. "Open my inventory"
  silently printing instead of opening is confusing the first time and the trigger is
  invisible. Pre-existing; changing it is a design decision about the panel.
