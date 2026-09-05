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
- **The D-pad does one job at a time.** With Mouse Mode on and the D-pad chosen as
  the cursor source, `pad_fired` refuses the four directions, so they steer the
  cursor instead of stepping a selection. Menus are outside the gate -- they read
  the pad directly and still need it to navigate -- and so is the map, which owns
  the whole display and steers a crosshair of its own (`pad_fired_raw`).
- **Which input drives the cursor is per device, and so is what it is called.**
  A 3D Control Pad offers "D-Pad" or "Analogue Stick", a Mission Stick "D-Pad" or
  "Left Stick", a control pad only "D-Pad", a Twin Stick only "Left Stick" -- its
  digital directions *are* its left stick. The table is `CSRC_NAME` in
  `controller.cxx`.
- **The mouse reports a running total in a wrapping byte, not a per-frame delta.**
  Movement is the difference against last frame, masked to the counter's width and
  read back signed (`mouse_delta`). Taking the raw value slid the cursor on until
  the mouse was carried back to where it started; taking the difference without
  minding the wrap read -255 for a movement of one and snapped the cursor to
  whichever edge was nearest. Y is added, not subtracted -- the reported sign
  already runs the way the screen does.
- **The mouse has a speed setting** on its own Mouse Mode sheet (that sheet has no
  on/off, which is the cell's "N/A"), five gain steps with 1:1 in the middle. Below
  1:1 the integer division would throw away every movement too small to make a
  whole pixel, so the sub-pixel remainder is carried; above it, one frame's travel
  is capped, because an unbounded acceleration curve turns a flick into half the
  screen in a sixtieth of a second and that reads as jumping, not as speed.
- **The pointer is an arrow whose tip is the cell's own top-left pixel**, so what
  the player aims at and what the program selects are the same place. It is a font
  tile patched into an unused control code (`TEXT_CURSOR_CH`), the same trick the
  backslash glyph uses, so the cursor is one ordinary character cell and needs no
  sprite layer of its own. Two inks -- body and outline -- so it stays readable
  over text, over marble and over a picture.
- **Every cursor source accelerates.** A held D-pad direction starts at one pixel a
  frame, so a single cell can be picked at all, and works up to seven; an analogue
  stick scales with how far it is pushed rather than being collapsed to a
  direction; and the mouse has a quadratic curve so a slow hand is precise and a
  flick crosses the screen.
- **"Press any key" means any key on anything** -- all thirteen pad buttons, any
  keyboard key, any mouse button including the Blue one, and a gun trigger whether
  the shot lands on screen or off it.
- **The cursor works in menus.** `menu_sync` ticks the controller and draws the
  pointer, because a menu runs its own loop and never reaches the game loop's tick.
  Hover highlights, **left or middle click accepts, right click goes back**, wired
  into the generic list picker (which is the mode menu, the game picker and the
  save pickers at once), the Options menu, both Controls pages and the save UI.

  Accept spans two buttons on purpose. SRL calls the digital A, C and B trigger
  bits Left, Right and Middle -- flags bits 2, 1 and 0 -- while the mouse's own
  order for that byte is Left, Right, Middle at bits 0, 1 and 2. The two layouts
  disagree about which of the outer buttons is which and agree about the right one,
  so Right is safe to give a meaning of its own and the other two are safe only
  together.

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
