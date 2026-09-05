---
name: controls-page-and-gun-targeting-handoff
description: Four commits reshaping the Controls page into a device pager over one submenu per controls.xls sheet, moving the mode swap onto L+R, rebuilding the command module, and making the scroll markers and the room picture shootable, and drawing the pointer cursor that three separate faults had kept invisible. Never on a screen.
metadata:
  type: project
---

Twelve commits on `art-v2`, on top of [[controller-module-handoff]]: `d366a6f`
(L+R and the command module), `50baab5` (gun targeting), `50ea1e8` (the Controls
page), `498c909` (the cursor), `315caa2`
(the D-pad gate, per-device stick names, mouse speed and the pointer in menus),
`2d0eb52` (the arrow, acceleration, and any-button prompts), `98e6477` (the
mouse's units and the last two narrow prompts), `6e25537` (menu clicks), `779a048` (the mouse's wrap and scale). Read `docs/CONTROLS_MATRIX.md` first -- it now carries the page layout and
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

## The cursor, and the three reasons it was invisible

`498c909`, after the owner reported seeing no cursor for any mouse mode or for the
gun. It was never one bug:

1. **Nothing drew one.** `text_map` now carries a one-cell overlay
   (`text_cursor_set`/`text_cursor_off`) painted *after* the block copy in
   `text_flush` rather than composed into the shadow, so the character underneath
   is restored by the copy itself when the cursor moves off. `render_pointer` in
   `console_view.cxx` places it every frame in both interfaces. The glyph is a
   blinking `+` because every text palette slot is already spoken for -- a cell
   that moves is easier to find than one more colour among the party inks.
2. **Mouse Mode had no switch.** `controller_mouse_mode_set` was never called from
   anywhere, so `g_mouse_mode` was permanently 0 and `read_sticks` never moved the
   cursor. It is now a `CK_MMODE` row at the head of the Mouse Mode sheet.
   `controller_twin_set` had the same problem and is now a root-page row -- it has
   to be, since it decides whether the Device row can reach a Twin Stick at all.
3. **Two devices never updated their cell.** `DevPointer.col/row` come from
   `clamp_cursor`, and `read_gun` set absolute coordinates without calling it, so a
   gun cursor would have drawn at a stale cell. A control pad reports no axis, so
   it never reached `read_sticks` either and its D-pad moved nothing;
   `read_dpad_cursor` is the control pad's and twin stick's Mouse Mode cell.

The D-pad double-duty this first left open is **closed** in `315caa2`, along with
three things the owner reported from the first run:

- **The gate.** `pad_fired` refuses the four directions while
  `controller_dpad_is_cursor()` -- Mouse Mode on and a digital source chosen -- so
  the D-pad steers the cursor instead of stepping a selection. It lands in
  `pad_fired` because gameplay reads directions through it and menus read the pad
  directly, so menus keep their navigation for free. The map is the one screen that
  wanted an exemption (it owns the display and has its own crosshair) and takes
  `pad_fired_raw`.
- **Which input drives the cursor is now a per-device setting, named per device.**
  `CSRC_NAME` in `controller.cxx`: a 3D Control Pad offers "D-Pad" or "Analogue
  Stick", a Mission Stick "D-Pad" or "Left Stick", a control pad only "D-Pad", a
  Twin Stick only "Left Stick" -- its digital directions *are* its left stick. The
  owner asked this directly and it is the reason the table stores names rather than
  deriving them.
- **The mouse was far too fast and inverted.** The fixed divisor of 2 is now a
  five-step speed on the mouse's own Mouse Mode sheet, defaulting to 12, and Y is
  added rather than subtracted -- the reported sign already runs the way the screen
  does. Measured by the owner on hardware; the 3D pad was correct all along, which
  is what said the fault was in `read_mouse` and not in the cursor.
- **The pointer works in menus.** `menu_sync` ticks the controller and draws the
  cursor, because a menu runs its own loop and never reaches the game loop that
  used to do both. `menu_pointer_act/back/row` let a page take hover and clicks;
  wired into `options_menu` and both Controls pages. Every other page shows the
  cursor but does not yet respond to it.

## The second run's three reports

`2d0eb52`, all three from the owner watching it work:

- **An arrow, not a reticle.** A font tile patched into `TEXT_CURSOR_CH` (control
  code 1) the same way `install_backslash_glyph` patches 0x5C, so the pointer is
  one ordinary character cell riding the text layer's flush and needs no sprite.
  Its tip is the cell's own top-left pixel, which makes the aimed pixel and the
  resolved cell the same place. Two inks, body and outline, so it survives text,
  marble and a picture. The blink is gone -- an arrow is its own signal.
- **Nothing accelerated.** Every source moved at one constant speed until the input
  recentred. A held D-pad direction now starts at one pixel a frame (so a single
  cell can be picked at all) and ramps to seven; an analogue stick scales with
  deflection instead of being collapsed to +-1 by `axis_dir`, which is right for a
  scroll edge and was wrong for a cursor; and the mouse has a quadratic curve.
- **`menu_wait` took four of the pad's thirteen buttons and no other device.** It
  now takes `AnyPressed()` plus `controller_any_fired()`, which is both halves:
  an on-screen gun shot sets a pointer click and an off-screen one sets only
  `DA_ACCEPT`, so a prompt that checked one of the two still stranded the gun.
  `splash_skip_pressed` got the same treatment and ticks the module itself, its
  loops synchronizing directly and reaching nothing that otherwise would.

## The Saturn mouse reports a position, not a delta

Measured from the owner's report ("still have to move mouse to original position
to get it to stop moving"), fixed in `98e6477`. `Pointer::GetPosition()` hands back
a running total that stays where the hand left it, so reading it as a per-frame
delta re-applies the same offset every frame and the cursor slides until the mouse
is physically carried back to its origin. `read_mouse` now differences against last
frame, seeded on first sight so a fresh device does not jump, and clamped at
`MOUSE_JUMP_MAX` so a wrap or a re-seat is not mistaken for a hand.

This is worth keeping because it is not what the SGL headers suggest, and because
the symptom reads as an acceleration bug rather than a units bug -- the first pass
at it added acceleration and left the real fault in place.

## The mouse took three runs, and each fault had its own signature

Consolidated into [[saturn-mouse-is-a-wrapping-byte-counter]]. Worth reading before
touching `read_mouse` again: the reading is a running total in a wrapping byte, and
the drift, the snap to a screen edge and the dead-feeling resolution were three
different consequences of that one fact, not three bugs. `779a048` is the last of
them.

The methodological note: the first pass took "acceleration not implemented" at face
value and added acceleration, leaving the real fault untouched. The owner's exact
words each time -- "until returned to zero", "snaps to top/bottom" -- were what
identified the mechanism, and each named a different symptom of the same cause.

## Clicking selected nothing, and why that was informative

`6e25537`. `menu_pointer_act` demanded `p->button == DEV_BTN_LEFT`, and a Saturn
mouse's buttons do not reliably land there: SGL defines no mouse-specific bits at
all, so SRL's Left/Right/Middle are its own reading of the digital A/C/B trigger
bits. See [[srl-mouse-button-bits-are-a-guess]].

The diagnostic worth keeping is the asymmetry, not the fix. The same click *did*
dismiss a press-any-key prompt, because that path tests `g_ptr.hot || g_fired[]`
and does not care which button. So the edge was arriving and only its attribution
was wrong -- which ruled out the pointer plumbing, the hit-test coordinates and the
tick ordering in one step, none of which would have produced that split.

Menu clicks now ask for no particular button, and cancelling is a clickable row
rather than a second guess at the same bits. **The gameplay fallbacks still branch
on it** (left = Letter, middle = Backspace, right = Accept) and are still resting on
the unconfirmed mapping.

## The stale repeat table, which is the trap in this design

`59773af`. Making `menu_sync` call `controller_tick` (315caa2) quietly broke the
module's own ordering contract: `controller_tick` reads the pad through
`pad_fired`/`chord_fired`, and **no menu and not the title calls
`pad_repeat_update` or `chord_tick`**. On those screens it reads whatever the last
screen that did call them left behind, so one `fired` flag stuck true makes
`controller_any_fired` true on every frame -- and a title screen whose any-button
test is permanently true never waits for the button at all. It worked from a cold
boot, where the arrays are zeroed, and failed after any gameplay or soft reset.

The pad's four actions read plain `WasPressed` edges now, which need no timer; the
chords are gated on a new `chord_ticked()`. **Anything else added to
`read_pad_family` has to obey the same rule.**

A pointer edge has the mirror problem: it is module state, so it survives the
`Synchronize` that clears the pad's, and `select_at`'s "consume any stale edge"
line did not consume it -- the click that dismissed the title arrived as the mode
menu's first frame and picked whatever the cursor was over.
`controller_pointer_flush()` is paired with every `mode_toggle_reset()` for that.

## What the netbin costs

Measured, not estimated: the changed netbin translation units compiled at `-O2`
with the real netbin defines, at `c9e6ebb` and at `2d0eb52`, section sizes diffed.

| unit | text | data | bss |
|---|---|---|---|
| `input/controller.cxx` | +9,835 | +604 | +1,612 |
| `net/netbin_pages.cxx` | +6,318 | 0 | +4 |
| `menu/menu.cxx` | +1,952 | 0 | 0 |
| `video/text_map.cxx` | +684 | +8 | +4 |
| `video/console_view.cxx` | +360 | 0 | +7 |
| `input/input.cxx` | **-1,072** | 0 | -2 |
| others | -96 | 0 | +4 |
| **total** | **+17,981** | **+612** | **+1,629** |

**That total is an upper bound, not the linked figure.** These are unlinked
objects, and SRL's `Input::Management` statics and its header-only Pointer/Gun/
Analog code are instantiated in every translation unit that touches them and merged
by the linker. The real image growth is lower; how much lower needs a link map,
which only a real build produces. The script is
`scratchpad/measure.sh` in that session and is trivial to re-run against a new base.

Everything is already in the netbin: only `splash.cxx`, `saturn_glue.cxx` and
`menu_pages.cxx` are outside it, and all three are CD-only by design. No part of
this work is behind an `#ifdef NETBIN`.

## What is unverified

**Nothing here has been on a screen** except the absence the cursor fix answers.
Syntax-checked only, all four configurations. 296 host tests pass, with the one pre-existing
`test_lwram_budget::test_every_frame_lies_inside_its_archive` failure that belongs
to the uncommitted art manifest and predates all of this.

The layout arithmetic is the part most likely to be wrong on sight: both new pages
place rows by hand against a `menu_box_fit` height, and neither has been looked at.
`controls_page` draws to `fy + 16` inside a box asked for `CS_N + 13` content rows,
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

- **`superpowers:verification-before-completion`** — six commits; only the
  missing cursor, the mouse's speed and inversion, and the absent acceleration
  have been seen at all.
- **`code-review`** with base `c9e6ebb`, which reaches all four of this branch's
  input commits. `menu_pages.cxx` and `controller.cxx` carry the risk.

## Hard rules

The owner runs every build and every emulator session — [[user-runs-all-builds]],
[[never-edit-mednafen-config]]. Cross-compile with `sh saturn/syntax-check.sh
<file>`, and `NETBIN=1` for anything in the netbin source list.
