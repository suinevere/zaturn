---
name: command-panel-and-dim-handoff
description: Eight commits of hardware-feedback fixes on controller-command-interface2 — wallpaper dim, command panel rework, Space remapping — none of it built or run yet.
metadata:
  type: project
---

> **Partly stale.** The "never compiled or run" claim below and the instruction
> to build first were both overtaken on 2026-08-13: the owner has since run
> builds across the session [[controls-and-panel-interface-handoff]] covers, and
> the branch has moved to `ba370b4`. The eight commits *this* entry describes
> were still not individually confirmed on screen, and its four open decisions
> are still open — those parts stand.

Branch `controller-command-interface2` @ `2a0f550`. Everything below was written
from the owner's hardware/emulator observations and **has never been compiled or
run** — only host tests and `saturn/syntax-check.sh` (DEBUG, release, NETBIN).
The next session's first job is a `./compile.bat` and a look at the screen.

Feature spec and plan:
`docs/superpowers/specs/2026-08-10-controller-command-interface-design.md`,
`docs/superpowers/plans/2026-08-10-controller-command-interface.md`. Task-level
findings live in `../zaturn-cmd-iface/.superpowers/sdd/2026-08-10-controller-command-interface/`
(git-ignored scratch — `git clean -fdx` destroys it). This handoff covers only
what came after that plan closed; supersedes nothing in `mem/`.

## What the eight commits did

`git log --oneline 4ffe983..HEAD` is the list; each message states its own
reason. Grouped:

- **Wallpaper dim** — `9a94ee2`, `380d3eb`, `eee1583`. The dim was being dropped
  by every screen fade and the Dimming row was renumbered twice.
- **Command panel** — `97afe6f`, `bc37a12`, `fe245e8`, `2a0f550`. Seven-row rose
  with word corners, cursor navigation across all three modules, row scrolling,
  full-length words to the parser, letters-only highlight.
- **Controls** — `3348ec6`. Space became a fourth remappable face action.

## Things that will bite

**Save-format sentinels moved twice and the blocks are position-dependent.**
`options_load` measures the sound, gameplay and display blocks by stepping past
the mapping block, so its width is load-bearing. Display block: 6 → 8 (not 7 —
the gameplay block owns 5 and 7). Mapping block: 2 → 3. Both old widths are
still read; see the comments in `saturn/src/menu/options.cxx` and
`DISP_BLOB_BYTES` in `display.h`. Sentinel 8 was **redefined** mid-session after
a five-stop dim row shipped into it for a few hours — if the owner built during
that window, a saved dim reads one stop dark. One nudge right fixes it; not
worth a format branch.

**`CR_ROWS` grew 5 → 7 and the inventory overlay was sized off it.** Caught and
fixed (`CV_OVERLAY_ROWS`), but anything else that measured the rose by
`CR_ROWS` would have the same bug. Nothing else did, at time of writing.

**The rose's movement rule was rewritten three times** — outward search, then an
explicit 12×4 neighbour table the owner dictated, then geometric aiming. Only
the third survives. Do not resurrect the table from the transcript; the owner
rejected it for dead-ending in sparse rooms.

## Open decisions for the owner

1. **Up-and-right from west with north missing lands on `in`, not `ne`.** The
   owner asked for `ne`; the general rule gives `in` because `in` is marginally
   more diagonal (17.5° vs 15° off horizontal). Left as the rule's answer rather
   than special-cased, since removing special cases was the point. One
   comparison to bias if they disagree.
2. **A full rose sends up-from-`up` to `down`** (the vertical wrap). Came from
   the owner's own table and was kept. Most likely press to feel wrong in the
   hand.
3. **The boot logo now carries the wallpaper dim.** `SUINE.TGA` is on the
   picture layer, so it dims with everything else. Flagged when introduced, not
   objected to, but never seen on screen.
4. **`saturn/src/video/category_art.inc` is modified and uncommitted** — the
   owner's art-pipeline regeneration, untouched all session. It breaks
   `test_display`'s `test_category_art` (`named == 12`) in the working tree;
   that test passes against `HEAD`'s copy of the file. Not a regression from any
   of this work.

## Verification state

Host tests all pass: `test_command_rose`, `test_command_panel`,
`test_menu_layout`, `test_display`, `test_bg_dim`, `test_glyph_invert`, plus the
four source-level checks in `test_title_bg_dim.py`. The last of those pin the
colour-offset channel ownership rule that `9a94ee2` established — they have
teeth (verified against reverted source) and will fail loudly if a future fade
takes NBG0 onto channel A again.

`test_display` must be run against `HEAD`'s `category_art.inc`, not the working
tree's — see decision 4.

## Suggested skills

- **`superpowers:verification-before-completion`** first. Nothing here has been
  seen running. Do not let a "the dim is fixed" claim through without a build.
- **`diagnosing-bugs`** if the hardware run misbehaves — and read
  [[verify-before-claiming-root-cause]] before naming a cause. Three of this
  session's fixes were channel-ownership races that looked like arithmetic bugs.
- **`code-review`** with base `4ffe983` if the owner wants the eight commits
  reviewed as a unit before merge.
- Not `superpowers:brainstorming` — the design is settled and the remaining
  work is confirmation, not exploration.

## Hard rules

The owner runs every build and every emulator session; see
[[user-runs-all-builds]] and [[never-edit-mednafen-config]]. Cross-compile
changed units with `sh saturn/syntax-check.sh <file>` (and `NETBIN=1` for
anything in the netbin source list) to check syntax without building.
