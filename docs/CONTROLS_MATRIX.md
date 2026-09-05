# Controls matrix

Transcribed from `controls.xls` (4 sheets, 7 device columns). That workbook is the
spec for `saturn/src/input/controller.h`; this file is the diffable copy of it, and
`saturn/tests/test_controller_matrix.py` pins the module against what is written here.

A blank cell means the action is **unassigned on that device**, not that it falls
back to another column. The flight stick's top/bottom-page cell says so out loud
("unassigned") and the rest are read the same way.

## Static (cannot be remapped)

| Action | 6 pad | Flight Stick | Analogue | mouse | twin stick | light gun | Keyboard |
|---|---|---|---|---|---|---|---|
| Menu | Start | Start | Start | Blue button | Start | Button | ESC |

## Actions

| Action | 6 pad | Flight Stick | Analogue | mouse | twin stick | light gun | Keyboard |
|---|---|---|---|---|---|---|---|
| Letter | C | C | C | Click | Gun trigger R | Shoot | — |
| Backspace | B | B | B | Middle Click | Gun Trigger L | Shoot | — |
| Space | Y | Y | Y | Click | Top Trigger R | Shoot | — |
| Accept | A | A | A | Right Click | Top Trigger L | Shoot off screen | — |
| Map | Z | Z | Z | Click "Map" in command menu | — | — | F8 |
| Recall (2) | X (cycles up only) | X (cycles up only) | X (cycles up only) | Click Prompt to go up, Right Click prompt to go down | — | — | — |

## Scrolling

| Action | 6 pad | Flight Stick | Analogue | mouse | twin stick | light gun | Keyboard |
|---|---|---|---|---|---|---|---|
| Scroll (2) | L/R | Left Stick up/down | — | player clicks up/down arrows on screen | — | — | — |
| page up/down (2) | — | Left Stick left/right | — | player Right Clicks up/down arrows on screen | — | — | — |
| top/bottom page (2) | — | Left Stick any direction (unassigned) | — | player Middle Clicks up/down arrows o screen | — | — | — |

## Mouse Mode

| Action | 6 pad | Flight Stick | Analogue | mouse | twin stick | light gun | Keyboard |
|---|---|---|---|---|---|---|---|
| Move selection | Dpad | Right Stick | Dpad | N/A (no mouse on/off) | Left Stick | — | — |
| Mouse cursor move | Dpad | Right Stick | Analogue Stick | Movement | Left Stick | pointed at screen | mouse if available in other controller port |

## How the Controls page is laid out

One page per **connected** device, paged with the Device row; a device nobody has
plugged in gets no page. Under it sits the Interface row (the persisted preference
a game starts in), then the Static sheet printed rather than offered, then one
submenu row per sheet that device configures.

Each sheet is its own configuration group: a swap inside Actions can never move a
Scrolling row, which is what `chord_group` in `input.cxx` enforces.

Only the three pad-family devices have editable sheets, because only they have a
mapping to point somewhere else -- a mouse click is a mouse click and a trigger is
a trigger. The other four list their bindings and are read-only.

| Device | Actions | Scrolling | Mouse Mode | Editable |
|---|---|---|---|---|
| 6 Pad | yes | yes | yes | yes |
| Flight Stick | yes | yes | yes | yes |
| Analogue | yes | yes | yes | yes |
| Mouse | yes | yes | no (always a cursor) | no |
| Twin Stick | yes | no | yes | no |
| Light Gun | yes | no | no | no |
| Keyboard | yes (Map only) | no | no | no |

## Where the shipped build differs from the workbook, and why

Three cells could not be taken literally without deleting a mechanism the workbook
does not mention. Each is listed with what shipped instead.

- **6 pad Map = Z** and **Recall = X**. Z, Y and X are the three shift buttons the
  whole `g_chord_slot` system is built on (`input.h`), and the Controls page edits
  that system. Taking Z and X as plain buttons removes Autocomplete, Home/End,
  Cursor and Page, and invalidates every chord slot in a saved options blob. The
  pad keeps its chords; Map stays on the command panel's Map module entry, and
  Recall stays on the `CA_RECALL` chord (`X+Up/Dn` by default).
- **6 pad Scroll = L/R**. `L/R` is the Autocomplete slot (`SL_LR`). The pad's
  scrolling keeps the `CA_LINE`/`CA_PAGE`/`CA_HOMEEND` chords, which cover all
  three Scrolling rows rather than the one the workbook fills in.
- **Space = Y** *did* ship, as the compiled default: the face group now permutes
  over `{A,B,C,Y}` instead of `{A,B,C,X}`. Two consequences. `SL_XUD` no longer
  overlaps a typing button, and the overlap moves to Y, where the default Page and
  Home/End chords sit. The Panel/Keyboard toggle that Y would also have collided
  with is gone entirely: that swap now rides the fixed L+R combo, so no shift
  button carries it.

## Changes made after the workbook was drawn

- **L+R swaps the dashboard's two modes.** It used to toggle Caps. The shift-button
  toggle (`g_toggle_btn`, Z or Y) is gone with it, which also retires the inert-Y
  case Space created when it moved onto Y.
- **Caps is an Options row only**, with no controller binding at all: a modifier
  whose state the player cannot see was not worth a fixed combo.
- **The command module reads menu / invent / look / map / swap.** Save, Load and
  Quit left it -- all three are one press away on the menu that module now opens.
  `invent` keeps its truncated label because the module is eight columns wide; it
  still submits the full `inventory`.
- **Both scroll markers are the same width at the same column** ("more ^" and
  "more v" at column 34), so the pair reads as a pair and both are equally easy to
  shoot. Clicking or shooting either scrolls, and holding repeats faster the longer
  it is held.
- **A shot at the room picture's edge is a move**, taken only where the compass
  rose is already showing that exit, so Hard cannot be read through the gun.

## Two things in the module that hardware has not confirmed

- **The twin stick's bit table.** A Twin Stick reports id `0x02`, the same as a
  control pad, so it is neither auto-detectable nor self-describing. Which ports
  are Twin Sticks is a player setting (`controller_twin_set`); which bits its four
  triggers sit on is the `TWIN_*` table in `controller.cxx`, and that table is a
  guess until somebody reads a real HSS-0136.
- **The light gun.** Selecting a gun in Mednafen wedges every SMPC port to id
  `0x00` / data `0x0000` permanently, so the gun path cannot be exercised in the
  emulator. It is implemented, and `controller_kind` rejects id `0x00` so the wedge
  is never mistaken for a live pad, but the off-screen test in `read_gun` is by
  coordinate range and wants checking against real hardware.
