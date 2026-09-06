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

## Chords (the workbook's Scrolling sheet)

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

## The chord model

Everything that needs a second button is held under **one modifier the player
picks** -- the Actions sheet's `Chord` row, R by default -- and the sheet it
governs is called Chords rather than Scrolling, because Recall and Autocomplete
sit on it beside Line, Page and Home/End. There is no caret chord: moving the
insertion point one character at a time is a keyboard's job, its own Left/Right
do it, and on a pad it cost a gesture to do something a player reaches faster by
rubbing the word out and typing it again. Three gestures exist:
modifier+Up/Down, modifier+Left/Right, and modifier+L/R. A fourth value, Unset,
is not a gesture: it is the action switched off, and any number of actions can be
off at once.

Defaults: Line = R+Up/Down, Home/End = R+Left/Right, and Page, Recall and
Autocomplete start Unset. A **tap of the modifier** -- pressed and
released with no direction taken under it -- scrolls back one line, which is the
one thing the shift button does alone. It is measured on the release: fired on the
press, every chord the player asked for would be preceded by one they did not.

Two consequences of naming the slots after the modifier. Moving the `Chord` row
renames every row on the Chords sheet at once, which is the point. And when the
modifier is itself a trigger -- as R is -- the modifier+L/R gesture has only one
direction, because R+R cannot be held; pick a modifier that is not a trigger to
get both.

Map is **Z** and the Keyboard/Command-Panel swap is **L**, both alone, both off
the workbook. Z is skipped as a map key while it is the chord modifier, or the map
would open on the press that begins every scroll.

## Devices without a D-pad

A racing wheel has a steering axis, two paddles and nothing else. Steering is menu
Left and Right (through the same repeat the analogue sheets use), the right paddle
is Up and the left one Down, and both reach every page because every page's four
directions go through `pad_nav` rather than through the pad directly. The same
readings steer the map's crosshair.

Its Mouse Mode sheet is hidden: a wheel has no second stick and no D-pad, so there
is nothing on it to describe. It shares the Analogue column with
the 3D Control Pad, which does have one, so the test is for the wheel's own id and
not for the column. SRL answers a wheel's second axis with a zero it does not have,
which reads as a stick held hard up, so the cursor is guarded against it too.

## The Mouse row

One row on the Controls page, not a sheet, because it is one setting: what drives
this device's cursor. Its values are that device's own, Off first --

| Device | Mouse row cycles |
|---|---|
| Control Pad | Off, D-Pad |
| 3D Control Pad | Off, D-Pad, 3D Stick |
| Mission Stick | Off, Left Stick, Right Stick |
| Twin Stick | Off, Left Stick, Right Stick |
| Racing wheel | row hidden -- one axis, two paddles, nothing spare |
| Mouse / light gun | row hidden -- always a cursor, or always aimed |

A device with an axis pair starts on it, since nothing else wants it. A device
whose only candidate is the D-pad starts **Off**: that D-pad is also what steps the
selection, and taking it away is the player's call. Switching the row on puts the
cursor on that row -- it has been sitting wherever it was last left, and a cursor
that appears somewhere the player is not looking reads as not having appeared. While it is pointed at the
D-pad, the four directions stop stepping selections in game (`pad_fired` withholds
them) -- menus read `pad_nav` and still navigate, and the map reads the ungated
form because it steers a crosshair of its own.

## One page, not a tree

The Controls page carries everything: the Device name, the Interface row, the
bindings that used to live behind `Actions...`, the Mouse row, Keyboard Caps and
the three exits. `Chords...` is the only submenu left, sitting under the row that
names the modifier it belongs to.

The **Device row is a statement, not a selector**. It names whatever is plugged in,
read fresh every frame, so hot-swapping a pad for a stick renames it and rebuilds
the rows under it where it stands -- and a Twin Stick appears there the moment the
L+R+Z+X chord says it is one. It prefers the first attached device whose bindings
can be edited, since that is what a player opening the page came to configure, and
falls back to the first attached device of any kind so a mouse or a gun on its own
still gets described.

Gone with the tree: the Interface description line (both interfaces share one
configuration now, so the row picks a starting surface and nothing else), and the
Static sheet's "Menu (fixed)" row, which said Start on every device that has one.
Caps Lock went to Gameplay, where the other typing settings are -- it says what the
keys produce, not what a button is bound to.

## One button, one action

Accept, Backspace/Cancel, Type, Space and the chord modifier draw from one pool --
**A, B, C, X, Y, R** -- with one action each. Five actions over six buttons, so one
is always spare: moving onto the spare just moves, and moving onto a used button
swaps, handing that action the button the mover left. No two can ever hold the same
button, including the modifier, which is what the Chord row cycling through the
same pool is for.

Z and L are outside the pool. They carry Map and the interface swap, both fixed,
and a binding that could land on either would take away the way out of where the
player is.

## Where the build departs from the workbook

Two cells are answered differently on purpose, both about the buttons a pointing
device has rather than about what the actions mean.

- **The mouse: left and middle accept, right goes back.** The workbook reads the
  right button as Accept and the middle as Backspace. SRL's names for those two
  bits are its own reading of SGL's digital A/C/B bits, and the two candidate
  layouts disagree about which outer button is which -- both agree only about the
  middle bit. Rather than guess, the build gives one button one meaning
  everywhere: `menu_pointer_act` takes left or middle, `menu_pointer_back` takes
  right, and `pointer_fire`'s fallback table matches them.
- **The light gun: a shot off the screen is Back, not Accept.** A gun has one
  trigger and cannot point at a row it is not aiming at, so the shot that misses
  the raster is the only Back it has. It fires as a right click, which is what
  makes every page that honours one honour the other without a second path.

## Every page answers a pointing device

The rule is that nothing is reachable by a pad alone. Rows highlight on hover and
activate on a click; a right click or an off-screen shot is Back; a slider or a
pager is worked by clicking the `<` and `>` it already draws, because a mouse has
no Left and Right of its own; a list's `^ more`/`v more` markers scroll it; the
map's floor number grew a pair of arrows and its paper takes a click as "put the
crosshair on the nearest room"; the save name editor grew a Done row, because a
mouse has no Start to submit with. Yes/no questions are the two words themselves
on the `menu_yesno` widget rather than a legend naming buttons -- a legend is
unanswerable to a device that has none of them.

The one place still pad-only is the in-game command panel's three modules
(compass rose, word page, command list), which is a separate piece of work.

## How the Controls page is laid out

One page per **connected** device, paged with the Device row; a device nobody has
plugged in gets no page. The row names the model the port reports rather than the
column it configures -- "3D Control Pad" and "Racing Wheel" both configure Analogue,
and a page that called them the same thing could not tell the player which one they
were holding -- and it is read every frame (`controller_kind_label`), so a hot-swap
renames it where it stands. Under it sits the Interface row (the persisted preference
a game starts in), then the Static sheet printed rather than offered, then one
submenu row per sheet that device configures.

Each sheet is its own configuration group: a swap inside Actions can never move a
Scrolling row, which is what `chord_group` in `input.cxx` enforces.

Only the three pad-family devices have editable sheets, because only they have a
mapping to point somewhere else -- a mouse click is a mouse click and a trigger is
a trigger. The other four list their bindings and are read-only.

| Device | Actions | Scrolling | Mouse Mode | Editable |
|---|---|---|---|---|
| Control Pad | yes | yes | yes | yes |
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
- **The mouse reports a running total in a sixteen-bit accumulator, not a per-frame
  delta.** SGL's own handler (`_slPerSaturnMouse`, disassembled from `LIBSGL.A`) adds
  each report's signed movement into the `PerPoint` x and y it already holds, and
  zeroes both when the port's id changes. Movement is therefore the difference
  against last frame, taken at that width (`mouse_delta`). Taking the raw value slid
  the cursor on until the mouse was carried back to where it started; narrowing the
  difference to a byte -- which it was -- gave the wrong sign to any frame that moved
  more than 127 counts, which at a modern mouse's resolution is a couple of
  millimetres of hand, so ordinary small movements and every frame of slowing down
  threw the cursor backwards until it pinned against an edge. A difference larger
  than one report can carry is the id-change reset landing between two frames and is
  dropped. Y is added, not subtracted -- the reported sign already runs the way the
  screen does.
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
