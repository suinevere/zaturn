---
name: input-dashboard-handoff
description: The gamepad input strips got a marble panel on the unused VDP2 NBG2 cell layer; branch input-dashboard, now drawing each module as its own mitred box to a mock-up the owner drew, after five wrong reshapes made blind.
metadata:
  type: project
---

Branch `input-dashboard`, tip `2f9a47f`. **Not merged, not pushed.** All feature work is committed; the only dirty paths are the owner's own
(`saturn/src/scene/game_scenes.inc`, the `SaturnRingLib` submodule pointer, and untracked
asset directories) — leave them alone.

Design and plan: `docs/superpowers/specs/2026-08-28-input-dashboard-design.md`,
`docs/superpowers/plans/2026-08-28-input-dashboard.md`. **The spec is only current to
`5214738`.** Everything after that reshaped the panel again and the spec was never brought
forward. Read the code for the real geometry. Task-by-task reports and the SDD ledger are
under `.superpowers/sdd/2026-08-28-input-dashboard/` (git-ignored, still on disk).

Sits on [[controls-and-panel-interface-handoff]] and [[command-panel-and-dim-handoff]],
neither superseded.

## What it is

The ASCII `+---+` chrome around the gamepad input strips is drawn instead as a marble
panel on **NBG2**, which nothing else used. `dash_map.c` is pure logic with no SRL include
— that is what lets `saturn/tests/test_dash_map.c` link with plain gcc, and it is worth
preserving. `tools/gen_dash_tiles.py` generates the committed `dash_tiles.c`; never
hand-edit that file. The printed borders survive behind `dash_ready()` as the fallback for
a failed VRAM allocation and for the whole netbin build, where `dash_view.h` collapses the
feature to `#ifdef NETBIN` no-op inlines.

## The look is settled; do not redesign it

`SaturnRingLib/emulators/mednafen/snaps/fixed.png` is the owner's own mock-up, drawn over a
screenshot in an image editor, and `2f9a47f` implements it. **It is the specification.**
Each module is its own box: a four-pixel frame whose entries are `[7, 3, 2, 13]` by a
pixel's depth from the module's edge, mitred at the corners by taking the minimum depth
over every edge, with the marble field carried underneath out to the highlight. Modules are
separated by two transparent pixels. Ten pixels of frame do not fit one cell, so the
divider cell holds eight and the columns either side carry the overflowing highlight --
that is what `DT_MODRIGHT` and `DT_MODLEFT` are for.

The mock-up is two pixels left and above the cell grid and its two module gaps differ by a
pixel; the implementation snaps to the grid and makes both gaps two. Verified by rendering
the real `dash_map.c` through the committed `dash_tiles.c` and diffing against the approved
raster: 21742 pixels identical, 0 different.

## Identify the build before trusting a screenshot

The owner's ROM is often several commits behind. Decode a snap rather than eyeballing it:
cells are 8x8 at offset (5, 0) in the 330x240 image, a pixel's palette index is `colour >> 3`
against the `PALETTE` table, and the NBG2 layer is displayed one row lower than the map, so
map row *r* appears at screen row *r + 1*. Matching each 8x8 block against every commit's
generated tile table names the build outright -- that is how `f3c79fc` was identified as the
source of `-0001.png` after three sessions of guessing.

## Five wrong reshapes, all made blind

Nothing in the host tests or the compile gate catches appearance. Every one of these was a
design decision taken without seeing it rendered:

1. The input line was inside the panel; the top bevel ran through the glyphs.
2. A framed top row put a flat grey strip across the panel.
3. Two-pixel `[2, 13]` edges, which read as a smear rather than a border.
4. The vertical edges and dividers kept six pixels of flat body, so a grey stripe ran the
   full height of both sides and of every divider.
5. Three-pixel bevels restored from a stale ROM, which was not what the owner wanted either.

`test_dash_tiles.c` now pins the design: the frame entries by depth, the mitre by the
minimum-depth rule, the gap's transparency, and -- against a repeat of defect 4 -- that
every frame tile's stone matches the field tile of its own phase pixel for pixel.

## Traps that cost real time

**A tile's index IS its VDP2 character number.** `dash_view.cxx` uploads in enum order and
its flush writes `g_char_base + dash_cell(...)`. `test_dash_map.c` asserts tile *names*,
which move with their values, so the suite passes even when the generator's append order has
drifted out of step with the enum — the panel just draws wrong shapes. After any renumber,
dump the bytes and compare; do not trust a green run. Building a throwaway harness that
links the real `dash_map.c` and prints `dash_cell()` over the panel is the check that works.

**The access-cycle count nearly killed it silently.** The design asked VDP2 for 4 cycles per
allocation; `VRAM::Allocate` gates on `(bankCycles + cycles) < 8`, so the map allocation
would have failed `8 < 8`, `dash_init` would have returned false on every boot, and the
feature would have been invisible behind its own fallback with nothing failing loudly. SRL's
`AutoAllocateCell` asks 1 for `Paletted16`.

**Bank B0 is load-bearing, for the map not the cells.** The wallpaper's 512x256 8bpp bitmap
owns all of A0. Left to `AutoAllocateMap` the pattern-name table would try A0, fail, and land
in B1 — on top of SRL's own NBG3 font, which the allocator does not track.

**`syntax-check.sh` is `-fsyntax-only` and cannot catch a link error.** The `TOP_MARGIN`
export in `console_view.h` was only proven by compiling to scratch objects and checking `nm`
for `R` versus `U`. Do the same for any linkage change.

## Frame expiry

`dash_frame_end` retires the panel whenever a frame passes with no renderer claiming it —
that is what keeps the marble out from behind menus and the title screen. The model
conflates "stopped drawing" with "stopped being displayed", so any loop calling
`Synchronize()` with the console up but no renderer running blanks the panel under live
text. Five such sites were found and all are handled: the prompt loop's turn boundary,
`run_room_transition`, `confirm_return_to_title` and `saturn_read_story_prefix`'s save-path
retry all call `dash_hold()`; `saturn_die` instead blanks the strip rows. **If a sixth
appears, copy `dash_hold()` in `dash_view.cxx`** — it mirrors the renderers' own variant
dispatch, which is the property that makes it correct.

## Process failures worth not repeating

- **Two subagents committed to this branch concurrently** because a correction was sent to a
  running agent *and* a fresh agent was dispatched for the same work. They happened to split
  cleanly (`2c76b71` tile art, `7ba7cb6` wiring); that was luck. Never do both.
- **A subagent ran `git reset` on its own initiative** after receiving a "do not commit"
  message post-commit, rewriting branch history unprompted. The discarded commits `0627abe`
  and `cdde619` are still in the reflog. Bar `git reset` explicitly in agent briefs.

## Still open

- **The contrast walk has never been done.** Step the Display Options text presets against
  the stone body (palette entries 5-9, roughly 40-48% grey) and confirm none puts dark text
  on it. The remedy is to lighten the body range in the generator, not to constrain the
  presets. This is the last item from the original spec that nobody has closed.
- **`DASH_OVERLAY` has never been seen on hardware.** Open the inventory and check the item
  box; it was added after the first screenshot and changed twice since.
- `DASH_GAMEKB`'s right frame sits at column 38 (phase 2) while the corner and right-edge
  tiles bake phase 3, so its whole right column carries marble one phase out of step.
  Checked in the renderer at 4x and the seam does not read; accepted. Add column-phase
  variants of `DT_EDGE_RIGHT0..3` if it ever shows.
- The spec is stale past `5214738` (see top).
- `saturn_die`'s row clear uses a literal `28` rather than an exported `SCREEN_ROWS`.
- `test_display.c` fails against the working tree because of the owner's own uncommitted
  `game_scenes.inc` / `game_rooms.inc` edits — not a branch defect, verified against the
  committed blobs.

## Gates

```
gcc -O2 -I saturn/src -o /tmp/a saturn/tests/test_dash_map.c   saturn/src/video/dash_map.c   && /tmp/a
gcc -O2 -I saturn/src -o /tmp/b saturn/tests/test_dash_tiles.c saturn/src/video/dash_tiles.c && /tmp/b
python3 saturn/tests/test_netbin_sources.py
python3 tools/gen_dash_tiles.py > /tmp/rg.c && diff -q /tmp/rg.c saturn/src/video/dash_tiles.c
```
and from `saturn/`: `sh syntax-check.sh <files>` plus `NETBIN=1 sh syntax-check.sh <files>`.
**The owner runs all real builds and all hardware testing** — never `compile.bat` or the
emulator.

## Suggested skills for the next session

- **`superpowers:subagent-driven-development`** if resuming multi-task work — but note the
  two process failures above; dispatch one implementer at a time and never resume an agent
  while a replacement is running.
- **`superpowers:brainstorming`** before any visual change, over a render, never over a description.
- **`superpowers:verification-before-completion`** — this branch has repeatedly passed its
  tests while looking wrong. Evidence for a visual claim means a render or a screenshot,
  not a green suite.
- **`superpowers:finishing-a-development-branch`** when the owner is ready to merge; the
  branch is still unmerged and unpushed.
