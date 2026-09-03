---
name: 2026-09-03-map-party-colours-handoff
description: "The map's ink now follows the sheet it is drawn on rather than the player's font colour, every seat stands on it as a figure of its own colour, a shared room draws a quartered mark, the crosshair stays red on all four sheets, the labels moved to the top and bottom edges and the pad legend is gone. Four palette slots are borrowed from the stone ramp for the seat colours and given back when the screen closes. Also: the gamepad's marble strip stopped outstaying a switch to a real keyboard. Never on a screen."
metadata:
  type: project
---

Branch `map-baggage-marks`, commits `1c9d95f` and `83a28f4` on top of 82dbbd2 --
the second is the owner's four corrections after reading the first. Continues
[[2026-09-03-map-inset-to-parchment-handoff]] and
[[2026-09-01-map-floors-crosshair-and-party-handoff]]. One wave, all of it from
the owner's list.

## What was asked, and where each part landed

- ink per sheet: `map_ink` in `video/map_view.cxx`, `pres_map_bg_index` in
  `scene/presentation.c`
- pad legend removed: `MAP_ROW_HELP` is gone from `map_view.cxx`
- floor number moved left one column and up one row: `draw_once`'s tail
- roster to the bottom-left on the floor number's own row, others above:
  `draw_players`
- other seats as figures, colour coded: `DT_KNIGHT_PEER0` in
  `video/dash_map.h`, `knight_tiles`/`paint_knight` in `map_view.cxx`
- shared room quartered: `DT_SHIELD0`, `room_party`/`party_one`

## The palette is the whole difficulty

The map needs **five** colours on NBG2 at once -- the crosshair, the player's,
and one per other seat -- and there is exactly **one** entry nothing on the
stone reaches. That one, `DASH_PAL_ACCENT` 14, is spent on the crosshair and
nothing else: it is the one mark a player looks for rather than reads, so it is
red on every sheet and in every party and `dash_map_ink` does not write it. The
first pass had the player's figure sharing it, which made the cursor change
colour with the sheet.

The other four are **borrowed**: `DASH_PAL_PARTY0..3` are 3, 4, 5 and 6 -- a
groove, a shadow face and two steps of marble body in `dash_palette` -- and
`dash_map_ink` writes colours over them for as long as the map is up.
`dash_tint` on the way out rewrites all sixteen from the ramp and calls the loan
in.

That is safe for one measured reason and not for a comfortable one: outside the
four, the map's own tiles reach palette entries **0, 1, 2, 12, 13, 14 and 15 and
nothing else** -- `dash_map_begin` clears every other cell to `DT_BLANK` and this
screen draws no box -- so 3..11 are unreachable while it is drawn. Both halves are held by
`saturn/tests/test_dash_accent.py`: only the peer figures may name those
entries, and `write_palette` must **not** grow an exemption arm for them, since
an exemption would leave a player's colour on the marble for the rest of the
session.

`test_dash_tiles.c` gained an `is_borrowed_ink` exemption from `escapes_ground`
for the same reason and it should be read as a real weakening: that check says a
mark must be four palette steps clear of the tinted ground, and a figure drawn
wholly in entry 5 is *inside* it. The claim that replaces it is that entry 5 is
not on the ramp while the map is drawn. The shield is not exempted -- its ring is
still on the ramp, so only its quartered core is borrowed and the tile clears the
band on its own. If anything ever paints marble on the
map screen, three seat colours appear in the stone and the exemption is what let
it through.

## Decisions worth knowing

- **The ink is chosen by sheet, not by genre.** What matters is the paper a mark
  has to be found on. Sheets 0 and 1 take black, 2 takes red (which is what the
  previous session's accent already was), 3 takes `display_text_rgb` -- the
  player's own font colour, because that sheet has nothing of its own to read
  against.
- **The crosshair is not part of the ink.** It keeps the accent on all four
  sheets. Everything else the map draws in a colour -- the labels, the figures,
  the shield -- follows the sheet or the seat.
- **The map's labels stop taking the player's font colour on the other three
  sheets.** A bright green chosen to be read on black is barely there on tan.
  `map_view_show` now calls `text_set_color(ink, MAP_GROUND_555)` where it used
  to call `dash_tint(MAP_GROUND_555)` -- `text_set_color` carries that
  `dash_tint` itself, so the labels and the marks cannot be lit by two different
  grounds -- and hands both back with one `text_set_color` on the way out.
  `dash_map_ink` must come **after**, or the tint takes the borrowed slots back.
- **"BLACK if player red" is asked of the colour, not of the sheet.** Sheet 3
  takes a font colour the player picked and that can be red as easily as
  anything, so `map_ink_is_red` tests the channels.
- **A seat's colour is its seat number with ours taken out**, not a running
  index over the occupied seats: the same person keeps the same colour however
  many others join or leave. Before the server has said which seat is ours the
  fourth seat is the one with no colour, which lasts until the first roster
  frame.
- **One occupant keeps the mark the map already drew** (`DT_ROOM_HERE` /
  `DT_ROOM_PEER`) and takes a figure beside it; **two or more** take
  `DT_SHIELD0 + mask`. Two figures do not fit beside one cell. The shield is the
  here-mark with its 4x4 core quartered into 2x2 blocks, upper-left the player's
  and the rest in seat order, and an unclaimed quadrant keeps the core's own
  entry -- which is what makes one filled quadrant read as a colour rather than
  as a pattern.
- The shield set is sixteen tiles although five of them are never painted (mask
  0 and the four single-bit masks), so the mask indexes the set directly. Same
  bargain `DT_LINK0`'s mask 0 makes. Mask 0 is asserted byte-identical to
  `DT_ROOM_HERE`, which is what makes the four quadrant comparisons mean
  anything.
- **The picked room's name is on the drawing's own top row** (`MAP_ROW_ROOM`,
  row 3, the top gutter). It is the label a player reads while moving the cursor
  and the cursor is somewhere in the grid below it, so it belongs where the eye
  is not. It shares the gutter with the edge stubs, which is the cost.
- **The roster sits at the bottom** on the floor number's own row
  (`MAP_ROW_ROSTER`, row 24 -- the last solid row of the sheet) with the others
  climbing above it. The local player's line is the one that is always there, so
  it is the one that never moves. `MAP_ROW_TOP` bounds the climb at four rows,
  which is one short if the server has sent four seats and not yet said which is
  ours. The floor number ends three columns in from the drawing's right edge.
- 34 tiles added at the end of the set (18 figures, 16 shields), so nothing
  before them renumbered: `DT_N` 144 -> 178, 1,088 more bytes of VDP2 VRAM in
  bank B0. The three pinned indices in `test_dash_tiles.c` are updated.

## The gamepad's marble outstayed the keyboard

Reported separately and fixed in the same commit. Switching to a real Saturn
keyboard left the controller's marble strip on screen.

It is **not** the input aggregate reading the keyboard as a pad -- that was the
first theory and it is wrong. SRL screens the keyboard out by peripheral family
(`0x30` against `Digital`'s `0x00`), so `d0.IsConnected()` is already false on a
port holding one.

The real shape is two rules disagreeing about the same silence.
`dash_frame_end` drops the layer on any frame no renderer claimed it, reading
"nobody drew" as "nobody wants it". `dash_hold_any` -- which `menu_sync` runs,
and every loop that ends in `menu_sync` therefore runs every frame -- reads the
same silence as "keep whatever is painted", because it exists for fades and
modal waits that hold a composed screen for many frames. `render_keyboard`'s
real-keyboard branch dropped the image window and then said nothing at all about
the strip, so the two rules met and the second won.

The fix is that the branch now says it: `dash_input_hide()` in `dash_map.c`
clears the layer only when what is painted is one of the four input-strip
variants, leaving a menu box, the map and the hold latch alone. A renderer that
has decided there is no strip states it rather than declining to speak.

`online.cxx`'s terminal loop is where this is worst -- it ends every frame in
`menu_sync` -- but the fix is in `render_keyboard`, which every loop that can be
in keyboard mode goes through, so it covers all of them.

## State

Both Saturn targets compile and link from git-bash with the documented
`SOURCES=` override; the ISO step still fails at Error 127 for want of
`xorrisofs` on PATH, which is the environment gap and not a code fault. The CD
target was built **last**, which is what `test_hwram_budget.py` needs. All five
map host tests pass (`test_map_layout` needs `-I saturn/src/video` as well as
`-I saturn/src`), `test_dash_tiles` passes, and the whole `saturn/tests` python
suite passes at 98 passed, 1 skipped.

**Never seen on a screen.** The colours were checked by compositing the real
tiles through `write_palette`'s own arithmetic and `dash_map_ink`'s overrides in
a scratch script -- black/red/green/blue figures, the sixteen shields and the
reticle on the map's tan, and the same again with the player red to confirm the
first seat falls back to black and the cursor does not. That proves the tiles and
the palette agree; it proves nothing about the sheet, the text rows, the roster
or the marble fix on a television.

## Open for the owner

- **The quadrants are 2x2 pixels.** Four seats on one 8x8 mark is what the tile
  size allows and no more. If they do not read on a television the answer is a
  larger mark, which is a grid change and not a tile change.
- **Two adjacent occupied rooms can overlap their figures.** `map_layout_knight`
  places one cell to the left of a mark and flips right at the viewport edge; it
  knows nothing about a second figure. It has never mattered with one figure and
  now there can be four.
- **The netbin draws on no sheet**, so `g_sheet` stays 0 and its ink is black on
  flat tan. That is the readable choice, but nobody has looked at it.
- **The room name now shares the top gutter with the edge stubs.** A passage
  running off the top edge under a long room name will cross it. The gutter is
  two cells and the stubs are short, so it should be rare rather than never.
