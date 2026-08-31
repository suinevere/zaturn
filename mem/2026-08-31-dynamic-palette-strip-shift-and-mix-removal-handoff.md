---
name: dynamic-palette-strip-shift-and-mix-removal-handoff
description: Three fixes on top of the Zork I presentation branch -- the Dynamic palette now draws the room's picture the moment it is selected, the wallpaper rises 36 pixels behind the gamepad strip, and the Repeat/Sequential/Random mixes are gone -- then all 27 commits squashed onto main as 02ebc6c, unpushed.
metadata:
  type: project
---

Continues [[zork1-authentic-presentation-handoff]], which still holds everything
about the presentation project itself (spec, plan, ledger, the two alias rows,
the twenty unproven room orderings, Task 9's rejection). This file covers only
what happened after its branch tip, and the merge.

## Where the repo is

`main` is at `02ebc6c`, **4 commits ahead of `origin/main`, unpushed**. That is
the squash plus the three spec/plan doc commits that were already local-only
(`14fe384`, `a5202f9`, `1dbe162`).

- The 27 commits of branch `zork1-authentic-presentation` were squashed into one,
  `02ebc6c`, parented on `14fe384`. Verified lossless: `git diff` between the old
  tip and the squashed commit is empty.
- The pre-squash tip is preserved as branch **`zork1-presentation-presquash`**
  (`515291d`). Delete it once the push is done and the messages are not wanted.
- `origin/zork1-authentic-presentation` still holds the 24 pre-squash commits and
  has diverged from everything. Worth deleting after the push.

The push command is `git push origin main`. It was deliberately not run.

## Still unconfirmed before that push

The `.CGL` tracking decision the earlier handoff flagged is now **inside** the
squash: 2.0 MB of area archives tracked under `saturn/cd/data/BG/`, duplicating
`analysis/zork_bg/raw/`. Still reversible by resetting `main` to `14fe384`; after
the push it needs a history rewrite. See that handoff's own section for the
reasoning, not repeated here.

## What the three fixes were, and why

Read `02ebc6c`'s message and the diff for the what. The parts worth carrying
forward are the reasoning that is not in either:

**The Dynamic palette bug the owner reported.** `room_art_show` is reached only
from the music engine's room subscriber, which fires on a room *change*. Choosing
Dynamic while standing still therefore drew nothing. Fixed by having `room_art`
remember the room on every turn (`room_art_note_room`) whether or not it may draw,
and by `display_apply` calling `room_art_reshow()` on the Dynamic-plus-authored-art
path instead of merely declining to hide NBG0.

**The wallpaper shift is keyed on the strip being PAINTED, not reserved.** This
was the one real design choice. The offset is applied by scrolling NBG0, and a
scrolled bitmap plane (512x256, holding a 320x240 picture) wraps its blank tail
and then the picture's own top rows into the bottom of the screen. The only thing
hiding that wrap is the window `image_window_box` arms — which the two strip
renderers arm, and which a menu box replaces. Keying the offset on
`dash_input_up()` rather than on `g_in_game && g_kbd_visible` makes the wrap
structurally unreachable, at the cost of the picture sliding back down 36 pixels
while an in-game menu is open. The alternative (no slide, wrap visible at the
screen's bottom edge during menus) was considered and rejected.

36 pixels is half the 72 lines the marble covers, chosen by the owner from three
options. It is one constant, `console_strip_shift()` in `console_view.cxx`.

**`image_window_box`'s bottom clamp moved 223 → 239.** Stale from the 224-line
era; the display has been 320x240 since `4ecd749`. Harmless before, load-bearing
now — at 223 the strip's last two rows left NBG0 unsuppressed.

**The mixes.** The owner asked to gut Repeat/Sequential/Random and, when asked
what should replace the Audio Mix row, answered "also drop track, no longer
needed". So Sound Options is Music / PCM / Ok / Cancel, and Music level 0 is the
only off switch — a real one: `music_set_volume(0)` stops the drive and records
the owed track, so coming back off 0 reissues it. `music_start_menu` now opens on
the neutral pool instead of the deleted track selector.

The MOJOOPTS blob is positional, so the sound block's sentinel and its two bytes
are **kept as reserved zeros** rather than reclaimed. Old saves still parse.
Reclaiming them would silently misparse every blob already written.

## Verification reality

Same ceiling as the earlier handoff: **nothing has been built or run**, on
Mednafen or hardware, at any point in this session either.

- `sh saturn/syntax-check.sh` clean on every touched SRL file, DEBUG and release,
  and again under `NETBIN=1`.
- Host tests run and passing: `test_dash_map`, `music_mix_test`,
  `test_music_pause`, `test_music_scene`, `test_music_presentation`,
  `test_music_static`.
- The screen-facing files (`room_art.cxx`, `title.cxx`, `console_view.cxx`,
  `dash_view.cxx`, `options.cxx`, `main.cxx`) are compile-checked only. Every
  claim about what the shift and the redraw look like is derived from the VDP2
  registers and the SRL source, not seen.

## Two pre-existing test defects found, one fixed

- **Fixed.** `test/music_mix_test.c` did not pass at `7107217`: it links `music.c`,
  which calls `pres_of_room`, so it either failed to link or — linked against the
  real `presentation.c` — let Zork I's authored table answer for its object numbers
  and failed six checks. Given the same `pres_of_room` stub `test_music_static.c`
  already uses. Now builds per its own header recipe and passes.
- **Not fixed.** `test/music_test.c` does not compile, before or after: it
  references `EV_DANGER`/`EV_TRIUMPH`, which have moved out of the header it
  includes. Untouched, unrelated, still broken.

## What a next session would do

1. Push, or first reverse the `.CGL` decision. Nothing else blocks the push.
2. Build (`saturn/compile-cd.bat`) and walk Zork I on Mednafen — this is the
   first time any of it would be seen. Specifically worth watching: the Palette
   row landing on Dynamic mid-game (it reads the disc there, up to 408 KB, with
   the in-game Options menu's music pause covering it), the 36-pixel shift, and
   whether the shift's slide on menu open reads as a bug.
3. The Mednafen breakpoint capture at `0x060A597C` that the earlier handoff wants
   for the maze/river ordering and the seven unattributed tracks.

## Suggested skills

- **`superpowers:verification-before-completion`** — the standing hazard on this
  project is claiming screen behaviour that was only compile-checked. Everything
  above is at that ceiling.
- **`diagnosing-bugs`** — if the emulator pass turns up anything, the last two
  bugs here were both "the signal never reaches the drawing code", not drawing
  bugs. Start at the subscriber, not the blit.
- **`superpowers:finishing-a-development-branch`** — for the push/cleanup decision
  and the two stale remote branches.
- **`code-review`** — if reviewing `02ebc6c`, review it against
  `docs/superpowers/specs/2026-08-30-zork1-authentic-backgrounds-and-audio-design.md`;
  the three post-branch fixes are outside that spec and have no spec of their own.
