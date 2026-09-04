---
name: controller-module-handoff
description: A controller module reading every device controls.xls names -- mouse, light gun, twin stick, analogue axes -- shipped in one commit on art-v2, live in both game loops but consumed by no view yet, and never on a screen.
metadata:
  type: project
---

One commit, `29f60e5` on `art-v2`. Continues the mapping work
[[controls-and-panel-interface-handoff]] describes; nothing in that entry is made
stale by this one except the `{A,B,C,X}` face group, which is now `{A,B,C,Y}`.

The workbook is committed at `controls.xls` and transcribed to
`docs/CONTROLS_MATRIX.md`, which also records the three cells that could not be
taken literally and what shipped instead. Read that file rather than reopening the
spreadsheet; `saturn/tests/test_controller_matrix.py` pins the module against it.

## What the owner decided this session

Asked and answered; treat as settled.

1. **Device coverage plus a re-baselined Space, not a new scheme.** The workbook's
   6-pad column wants `L/R` = Scroll, `Z` = Map, `X` = Recall, which between them
   delete the shift-chord system, Autocomplete, Home/End, Cursor and Page, and
   invalidate every chord slot in a saved options blob. Only `Space X -> Y` was
   taken. The chord mechanism and the Controls page are untouched.
2. **A blank cell means unassigned on that device**, not a fallback to the 6-pad
   column. The flight stick's own "(unassigned)" note is the precedent.
3. **The light gun ships unverified.** Implemented in full, with the wedge guard,
   to be checked on real hardware later.

## What is unverified, and how badly

**Nothing here has been on a screen.** Syntax-checked only, both configurations,
both builds, CD and NETBIN. Host tests pass (572, plus 11 new ones), with the one
pre-existing `test_lwram_budget::test_every_frame_lies_inside_its_archive` failure
that belongs to the uncommitted art manifest and predates this work.

Two things are guesses rather than measurements, both flagged in the source:

- **The twin stick's bit table** (`TWIN_*` in `controller.cxx`). A Twin Stick
  reports id `0x02`, identical to a control pad, so it is neither auto-detectable
  nor self-describing. Which ports are Twin Sticks is a player setting
  (`controller_twin_set`); which bits the four triggers sit on is a guess. It is
  one table and nothing else in the module encodes it.
- **The light gun's off-screen test** (`read_gun`). "Shoot off screen" is Accept in
  the workbook, and the test for it is a coordinate-range check. Whether a real gun
  reports out-of-range coordinates or something else entirely is unconfirmed, and
  [[gun-wedges-smpc-peripheral-table]] means Mednafen cannot answer it -- selecting
  a gun there wedges every port to id `0x00` permanently.

## Things that will bite

**The module is live but nothing consumes it.** `controller_tick` runs every frame
in both game loops (`saturn_glue.cxx`, `online.cxx`) and `controller_init` runs at
both boots, so the classification, the action edges and the cursor are all real and
current. No view reads them yet. A mouse plugged in today moves a cursor nobody
draws and clicks nothing. That is the next phase, and it is the larger half: the
console's scroll arrows, the on-screen keyboard's keys and the command panel's Map
entry each have to hit-test `controller_pointer()` and call
`controller_pointer_consume()` when they take the click.

**`controller_tick` must stay after `pad_repeat_update` and `chord_tick`, and must
never call them.** It reads the pad through `pad_fired`/`chord_fired` instead of
re-deriving the mapping, so the pad has exactly one mapping and the Controls page
still owns it -- but those two advance the timers it reads, and calling them twice
in a frame doubles every repeat rate. There is a box at the foot of
`controller.cxx` saying so.

**Space on Y cost the Y option on the Panel/Keyboard toggle.** That toggle claims a
clean tap, which needs a button that types nothing, and Y now types a space.
`toggle_btn_free` in `input.cxx` makes a Y toggle inert rather than let one tap do
both. It still defaults to Z, which is unaffected, so nothing changes out of the
box -- but the Controls page still offers Y, and picking it now silently does
nothing. Either drop Y from that row or accept it; left as-is deliberately because
the owner scoped the Controls page as unchanged.

**Stored face mappings shifted meaning.** `g_face_btn` holds indices into the face
table, and index 3 was X and is now Y. A save written before this commit that had
moved an action onto X will read that action as Y.

**The mouse's fallbacks can double-fire until a view consumes them.** A click
contributes a non-positional fallback (left = Letter, middle = Backspace, right =
Accept) *and* sets `DevPointer.hot`. A view that hit-tests and acts without calling
`controller_pointer_consume()` gets both.

## Open

- The flight stick's second stick. The workbook's Mouse Mode sheet says "Right
  Stick" for the cursor and its Scrolling sheet says "Left Stick", which describes
  a twin Mission Stick; SRL decodes only one stick (`Analog::GetAxis` has a TODO
  for the extended data). `read_sticks` drives both scroll and cursor from the
  primary stick, so on a twin unit they share it.
- `CURSOR_STEP` and `CURSOR_DIV` in `controller.cxx` are unmeasured. Cursor speed
  wants tuning by eye once something draws it.
- Nothing persists `controller_twin_get()` or `controller_mouse_mode_get()`.
  Both are runtime-only until `options.cxx` carries them, and both want a Controls
  page row.

## Suggested skills

- **`superpowers:verification-before-completion`** — one commit, nothing seen. Do
  not let a claim through without the owner running it.
- **`diagnosing-bugs`** if a device misbehaves on hardware, and read
  [[verify-before-claiming-root-cause]] first.
- **`code-review`** with base `c9e6ebb`. `controller.cxx` carries all the risk.

## Hard rules

The owner runs every build and every emulator session — [[user-runs-all-builds]],
[[never-edit-mednafen-config]]. Cross-compile with `sh saturn/syntax-check.sh
<file>`, and `NETBIN=1` for anything in the netbin source list — `controller.cxx`
is in it. If the owner reports a fix did not work, that is ground truth.
