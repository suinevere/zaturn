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
  Home/End chords sit. And the Panel/Keyboard toggle can no longer usefully be set
  to Y, because that toggle needs a button that types nothing — see
  `toggle_btn_free` in `input.cxx`. It still defaults to Z, which is unaffected.

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
