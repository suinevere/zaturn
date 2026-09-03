---
name: 2026-09-03-map-inset-to-parchment-handoff
description: "The map grid is inset with a gutter so every mark lands on MAP.TGA's paper and a passage to a room scrolled off screen runs to the edge instead of vanishing; the crosshair moves off the stone ramp onto a red accent slot. Measured off the sheet, both targets link, never on a screen."
metadata:
  type: project
---

Branch `map-baggage-marks`, on top of 0f1bc12. Continues
[[2026-09-01-map-floors-crosshair-and-party-handoff]] and
[[2026-09-02-baggage-mark-and-scan-audit-handoff]]. Two waves, both from the
owner looking at the screen.

## Wave one -- the grid was drawn off the paper

`saturn/cd/data/TGA/MAP.TGA` was decoded and scored per 8x8 text cell: its
paper is fully solid only for columns 2..38 and rows 3..25, and everything
outside that is the torn edge or the transparent surround. The old grid was ten
rooms by seven, which at the original's four-cell room step is 40x28 and so
exactly the whole screen, with the status and help lines on rows 26 and 27.
Marks, links, the roster and the right-aligned floor number were all being
drawn on the black beyond the tear.

## Wave two -- the owner's two findings on the first look

**The crosshair was hard to see.** It was drawn in the ramp's brightest entry,
which is a pale near-white -- the one value with nothing to read against on tan
paper carrying dark ink. Palette index 14 turned out to be reachable by nothing:
`marble()` caps its veins at 12 and its body runs 5..9, and no frame, rule or
mark names it, so the emitted tile set used it zero times out of 4608 nibbles.
It is now `PAL_ACCENT` / `DASH_PAL_ACCENT`, holds a red, and `write_palette`
copies it to CRAM untouched instead of bending it toward the background's hue
and brightness like every other entry. Four tiles and one palette word changed;
the marble is byte-identical.

**Links vanished at the boundary instead of running to the edge.** `gather()`
only collects rooms inside the viewport and the link pass can only join two
gathered rooms, so an exit whose far end had scrolled off drew nothing at all.
The marks sat on the viewport boundary itself, so there was nowhere to draw it
even if the pass had tried -- which is why the fix is geometry before it is
code: `MAP_LEFT`/`MAP_TOP` now name where the first *mark* goes, and
`MAP_GUTTER` cells outside them on all four sides belong to the drawing.
`draw_once`'s new `edge_stub` lays a run into that margin.

## Decisions worth knowing

- The `map_edges` layer is indexed in **absolute screen cells**, so the inset is
  part of the index and no call site translates between two spaces.
  `MAP_CELL_W`/`MAP_CELL_H` are exclusive far edges, not extents.
- `map_edges_offview` is a separate entry point from `map_edges_stub` on
  purpose. A stub is forced dashed because its far end is on another floor and
  is not drawn; an off-view run carries the exit's own decoration, because
  drawing an open passage dashed would put the legend's "requires problem
  solving" mark on a passage the story lets you walk. The two share `stub_run`
  and the deco rule is now one `link_deco` rather than a copy in each.
- The direction of an off-view run is the axis the far room actually left the
  viewport by, not the larger of its two offsets: a room one step north-east
  that is off the top but not off the right is reached northward, and a stub
  pointing east at a column still on screen names a passage that is not there.
- Nine by five with a two-cell gutter is the largest whole-room grid that fits
  the solid band with two text rows left below it. Visible rooms fall from 70 to
  45; the view follows the crosshair, so nothing becomes unreachable, only
  sooner-scrolling.
- MAP2/3/4 are full-bleed sheets, so on those genres the inset only adds margin;
  the netbin has no sheet and draws on flat tan, where it is invisible. The red
  accent applies everywhere, since `write_palette` is shared.

## State

Both Saturn targets compile and link. The ISO step fails at Error 127 only
because `xorrisofs` is not on the git-bash PATH -- the documented environment
gap, not a code fault. `test_map_layout`, `test_map_edges`, `test_map_marks`,
`test_map_model` pass on the host; the whole `saturn/tests` python suite passes
(67 passed, 3 skipped), including a new `test_dash_accent.py` that holds
`PAL_ACCENT`, `DASH_PAL_ACCENT` and `write_palette`'s exemption together and
re-proves from the emitted tiles that nothing but the crosshair reaches the slot.

Several `test_map_edges` fixtures sat on rows 0..3 and were shifted down rather
than reinterpreted; the "every step leaves the viewport" case now starts at the
clip corner, since `MAP_LEFT` is inside the drawing.

**Never seen on a screen.** The previews in this session were composited from
the real tiles, the real palette and `write_palette`'s own arithmetic by a
scratch script -- not captured from the console. The owner's original report
came from screenshots that did not reach this session, so the specific rooms
named (West of House, North of House, Forest) were not confirmed; the cause was
established from the code path, not from the images.
