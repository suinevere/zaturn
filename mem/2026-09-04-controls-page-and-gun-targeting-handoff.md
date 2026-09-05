---
name: controls-page-and-gun-targeting-handoff
description: Three commits reshaping the Controls page into a device pager over one submenu per controls.xls sheet, moving the mode swap onto L+R, rebuilding the command module, and making the scroll markers and the room picture shootable. Never on a screen.
metadata:
  type: project
---

Three commits on `art-v2`, on top of [[controller-module-handoff]]: `d366a6f`
(L+R and the command module), `50baab5` (gun targeting), `50ea1e8` (the Controls
page). Read `docs/CONTROLS_MATRIX.md` first -- it now carries the page layout and
a "Changes made after the workbook was drawn" section as well as the transcription.

## What the owner decided this session

Asked and answered; treat as settled.

1. **The Interface row stays**, as the persisted preference a game starts in. L+R
   and the command module's Swap row change the live mode for the session only,
   exactly as the old Z/Y tap did.
2. **The command module keeps `invent`**, not `inventory`: the module is eight
   columns wide (`CV_CMD_X` 31 to 38) and widening it would take three columns off
   the word list. It still submits the full verb.
3. **No new scroll arrows.** The existing "more" markers are the shoot targets --
   made symmetric so both are equally easy to hit -- and a held button repeats and
   accelerates the way a held key does.

## What changed, in one list

- **Controls page**: a Device row paging over *connected* devices only, then the
  Interface row, then the Static sheet printed rather than offered, then one
  submenu per sheet that device configures. `controls_sheet_page` is the submenu.
- **Configuration groups**: `chord_assign` now only swaps ties within one
  `chord_group`, so a Scrolling remap cannot move an Actions row off-screen.
- **Editable vs not**: only `DEV_PAD`/`DEV_FLIGHT`/`DEV_ANALOG`. The other four
  list their bindings read-only -- there is no second mapping table for a mouse
  button to point at.
- **L+R** swaps the dashboard's two modes (`mode_combo_fired`). `caps_combo_fired`,
  `mode_toggle_fired` and `toggle_btn_free` are gone; `mode_toggle_reset` kept its
  name and now clears the L+R latch, so all its call sites were untouched.
- **Caps** has no pad binding at all; the Options row is the only way to set it.
- **Command module** reads menu / invent / look / map / swap. `CP_ACT_MENU` and
  `CP_ACT_SWAP` join `CP_ACT_MAP`.
- **Scroll markers** are `"more ^"` and `"more v"`, both six cells at column 34.
  `console_pointer_scroll` scrolls on a click or a shot, repeating through
  `controller_hold_fired`.
- **The room picture's edge** is a move (`cv_pointer_travel`), gated on the exit
  the rose is already showing so Hard cannot be read through the gun.

## What is unverified

**Nothing here has been on a screen.** Syntax-checked only, all four
configurations. 276 host tests pass, with the one pre-existing
`test_lwram_budget::test_every_frame_lies_inside_its_archive` failure that belongs
to the uncommitted art manifest and predates all of this.

The layout arithmetic is the part most likely to be wrong on sight: both new pages
place rows by hand against a `menu_box_fit` height, and neither has been looked at.
`controls_page` draws to `fy + 16` inside a box asked for `CS_N + 12` content rows,
and `controls_sheet_page` puts its Back row at `fy + 4 + CTL_SHEET_MAX + 1`. If
either overflows its frame, that is where.

## Things that will bite

**`controls_page` must stay byte-identical between `menu_pages.cxx` and
`netbin_pages.cxx`.** `test_netbin_lift.py` compares them normalised, and it also
compares the `CS_NAME` and `CTL_DEV` tables -- it used to compare
`FACE_LABEL`/`CHORD_LABEL`, which this session deleted, and the test was updated to
follow. Edit one file, edit the other.

**`g_toggle_btn` is now reserved, not removed.** Nothing reads it. It stays
declared in `app_state.h` and read/written by `options.cxx` so a MOJOOPTS blob
written either side of the change still loads on the other.

**A `*/` inside a doc comment closes it.** `FA_*/CA_*` in a header block cost a
compile and a confusing cascade of "not declared in this scope" errors pointing at
lines nowhere near it. Write `FA_ or CA_`.

**The panel's menu and swap rows are one frame late.** Both are set where the
panel's action is spent, which is below the blocks that act on them, so each is
read on the frame after the row was picked. Deliberate: the menu path at the top of
the loop owes a ramp-down and a music duck that the action site cannot give it.

## Open

- **Non-pad devices cannot be remapped.** The workbook's "they are allowed to swap
  configurations" is honoured for the pad family through face/chord assignment, and
  not for the mouse, gun, twin stick or keyboard, which have no second mapping table
  to store. If those should be remappable, that is a new per-device table and a
  save-format change.
- **`keyboard_controls_page` and the Device row overlap.** `controls_dispatch`
  still auto-swaps pages when the player physically changes device family, and the
  Device row now also reaches the keyboard's sheets. Two ways to the same place.
- The three items still open from [[controller-module-handoff]] stand: the flight
  stick's second stick, the unmeasured cursor speed, and nothing persisting
  `controller_twin_get`/`controller_mouse_mode_get`.

## Suggested skills

- **`superpowers:verification-before-completion`** — three commits, nothing seen.
- **`code-review`** with base `c9e6ebb`, which reaches all four of this branch's
  input commits. `menu_pages.cxx` and `controller.cxx` carry the risk.

## Hard rules

The owner runs every build and every emulator session — [[user-runs-all-builds]],
[[never-edit-mednafen-config]]. Cross-compile with `sh saturn/syntax-check.sh
<file>`, and `NETBIN=1` for anything in the netbin source list.
