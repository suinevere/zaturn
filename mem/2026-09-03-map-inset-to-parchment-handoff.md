---
name: 2026-09-03-map-inset-to-parchment-handoff
description: "Five waves off the owner's own screenshots: the map grid inset onto MAP.TGA's paper with a gutter so edge passages run to the edge, the crosshair moved onto a red accent slot, and the one-way arrowhead withheld where the story never states the far room's exits -- which was 64% of every arrow on the disc; then the atlas regenerated so 90% of its cardinal exits are drawn on their own axis instead of 85%, The Lurking Horror going from 59% to all of them, and floors taken off the story's own up and down exits instead of off the drawn sheet, which gives it ten floors where it had two. Never on a screen."
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

## Wave three -- the owner's Lurking Horror screenshot

Two findings, one fixed and one measured and left to the owner.

**The one-way arrowhead was drawn from absent evidence.** `has_reverse` asks
whether the far room has an exit leading back; `map_model_exits` read "no" as
"one way". But a v3 direction property three bytes long is a routine that
decides at run time and carries no destination at all, so `room_model` records
it as RM_EXIT_MAYBE with a destination of zero. The Lurking Horror's Terminal
Room has exactly two exits, `south` and `out`, and *both* are routines -- so the
story never states that Terminal Room leads anywhere, Second Floor's plain
`north` to it looked unreciprocated, and the map arrowed a passage the player
walks both ways.

Measured across all thirty-one stories: 383 arrows drawn today, 246 of them
(64%) on a room in that state. `reverse_unknown` now vetoes those. A refusal
message -- a two-byte property -- is excluded, because it asserts there is no
passage that way; that is why the conditional bit is tested and not the
destination alone. Zork I loses exactly three arrows and they are the trap door,
the grating and the chimney, all genuinely two-way; it keeps the Altar, the
White Cliffs and the slide. The chimney is the one the previous pass had already
corrected by hand in Zork I's marks table -- this is that correction as a rule.

**The room positions really are off-axis, and the file's own audit was hiding
it.** Terminal Room sits at (1,4) and Second Floor at (3,5): the story's `south`
draws as two columns across and one down, which is what the owner read as
south-east. The generator scores layouts with `HALF`, a half-plane test -- for
`south`, `dy > 0` with `dx` ignored entirely -- which is the right test for
deciding which object a drawn box is, but the emitted header reported its result
as "leave in the direction drawn". The Lurking Horror's table therefore claimed
34 of 34 (100%) while 19 of its 32 plain cardinal exits are on axis.

Across the whole shipped atlas: 664 of 779 (85%). Worst are The Witness (4/8),
Zork I (69/113), The Lurking Horror (19/32). Of the 115 that miss, 57% are off
by exactly one lane and 78% by one or two, which is the signature of the lane
clustering splitting columns the map draws aligned rather than of a genuinely
diagonal map; 12 (10%) point to the wrong side entirely.

`gen_map_atlas.py` now has `AXIS` beside `HALF` and reports both, and the header
wording says which test it is quoting. **The shipped .inc still carries the old
wording**, because regenerating it needs the map scan cache, which is not in the
repo and must not be committed. `saturn/tests/test_atlas_axis.py` measures the
rate from the committed table and the shipped story files instead, records a
per-game baseline and fails on regression, so the number is in the repo and
under review without the scans.

**Open for the owner.** Nothing was changed about where a room is drawn. Three
ways forward, and the choice is not mine: re-run the generator with the cache
and a looser lane tolerance, which the one-lane distribution says would fix most
of it; add a snapping pass that nudges rooms into alignment where a cardinal
exit misses by one lane and keeps only changes that improve the total, which
works on committed data but moves the table away from what the scan measured;
or accept it, since a campus plan drawn square to its buildings is Infocom's and
not an error. Enforcing alignment is not an option -- at PASS_RATE 0.85 it would
drop Zork I, The Lurking Horror and The Witness out of the atlas entirely and
fall all three back to the graph walk.

## Wave four -- the atlas regenerated

The owner chose "rerun generator with looser lane and nudges". The map scans are
not in the repo; `tools/gen_map_atlas.py` downloads them itself from
infodoc.plover.net into a cache that must stay out of the tree. A baseline run
first reproduced the shipped table exactly -- 18 games, every room count
identical -- so every later difference is attributable.

Three things were tried. Only two are in the tool.

**Nudge (kept, and it did the work).** `nudge()` walks the rooms in object order
and tries each at up to `NUDGE_SPAN` lanes either side along each axis, keeping
a move only when it puts more cardinal exits on their own axis and takes none
out of the half-plane its direction names. Scored on the edges incident to the
moved room, since those are the only ones a move changes; cells kept unique per
page, because two rooms in one cell draw one mark and lose the other. 24 moves
across 8 games.

**Lane tolerance sweep (kept, and it is marginal).** `LANE_TOLS` is tried in
order and the reading that puts the most cardinal exits on axis wins, ties to
the tightest. Sixteen games kept 60. Spellbreaker took 75 and Stationfall 160,
for +2 and +1 exits. Small, but real, and the sweep costs nothing now that
`_PAGE_CACHE` is at module scope -- before, laying a game out twice meant OCRing
it twice.

**Shear (tried, measured, removed).** The Lurking Horror's compass rose is a
parallelogram: north straight up, the ground's north-south axis leaning right,
every room box on the sheet sheared to match. A room due south of another really
is printed down AND across, which is exactly the (+2,+1) the owner reported, and
no rotation fixes it -- the four the layout tries are all right angles. Undoing
the lean before the columns are read was swept over nine values from -0.6 to
+0.6. **Every one of the eighteen games chose zero.** The nudge reaches the same
placements and reaches further. The sweep is gone and the finding is a comment
in the tool; the emitted tables are byte-identical with and without it, which is
how it was confirmed to have contributed nothing.

Result: **664 of 779 cardinal exits on axis (85%) becomes 702 of 779 (90%)**.
The Lurking Horror 19/32 -> 32/32, The Witness 4/8 -> 8/8, Spellbreaker 17/24 ->
23/24, Zork II 15/20 -> 17/20, Enchanter 55/61 -> 58/61, Zork I 69/113 -> 76/113,
Hollywood 41/42 -> 42/42, Sorcerer and Stationfall +1 each. Every game keeps its
exact room count and floor count, no game gains or loses half-plane agreement,
and the same 18 pass and 4 drop.

The nudge moves a room off where the scan measured it. That is the trade the
owner asked for: at most two lanes, only where the story's own compass says the
drawing is wrong, and never at the cost of a left-of or above-of relation.

One hole closed on the way: the layout clamps coordinates into signed-char range
BEFORE the nudge ran, so a nudge could in principle have pushed a room past 127
and wrapped silently on the console. `CELL_MIN`/`CELL_MAX` now bound the nudge
and an assert guards the emit. No shipped table is near it -- the widest is 24
rooms from centre -- which is why it is asserted rather than trusted.

## Wave three and four -- what was NOT wrong

**The floor count.** The owner reported The Lurking Horror as having "almost 10
floors" while the map shows two. Its map PDF has four pages: page 1 is the
InvisiClues cover (one box, no room names), page 2 reads as blank, and pages 3
and 4 are the two map sheets -- **both kept**, contributing 28 and 33 named
rooms. Zork I is the same shape: five pages, two of them cover and legend, three
maps kept. Infocom drew the game on two sheets and marks underground rooms on
sheet two by shading them rather than by giving them a sheet of their own. A
floor in this port is a drawn sheet, so two is right and nothing is being
discarded. What IS thin there is coverage: 51 of Lurking's 71 rooms get an atlas
cell at all, and the rest fall back to the graph walk.

## Wave five -- floors off the routes, and a regression I shipped

The owner: "Each UP/DOWN should be it's own floor, check the in game routes,
would solve HORROR's 7+ floors".

A page of the published map is not a floor. Infocom drew The Lurking Horror's
levels on two sheets, in oblique projection so the stacking reads on paper, and
marked the underground ones by shading them; paging by sheet offers two floors
for a building you climb. What says which level a room is on is the story:
`storeys()` in the generator makes a floor out of the connected components of
the level-exit graph -- everything but up and down, in and out included, since
walking into a building puts you on its ground floor -- ordered by the vertical
exits between them.

**Paired with the sheet, not replacing it.** Re-paging by level alone breaks
the coordinates: they were measured per sheet and each sheet was then dropped
into its own band of rows, so two rooms off different sheets share no frame.
Measured before implementing: eleven route floors spanned two or three sheets,
Zork I's largest spanning all three with forty-five rows of nothing between.
`(sheet, level)` keeps every floor inside the one drawing it was read from.

Levels are numbered by breadth from the largest component, not by longest path:
a vertical cycle -- Zork I's coal mine has two the ordering cannot honour --
inflates a longest path without bound and put Zork I on a level twenty-five.

**31 floors become 76.** The Lurking Horror 2 -> 10, Stationfall 3 -> 12, Zork I
3 -> 9, Leather Goddesses 3 -> 7, Hollywood and Plundered Hearts to 5 each. Every
coordinate unchanged, every game's floors dense from zero with none empty. Zork
I reads convincingly: floor 1 is Attic and Up a Tree, floor 6 the Temple level
above the dungeon, floors 2 to 4 the coal mine's three levels.

Two consequences that had to be handled rather than ignored:

- **Floors now overlap, on purpose** -- the storeys of a building stand on the
  building's footprint. `map_atlas_pages_overlap` was an invariant asserted as
  always false; it is now the question a caller must ask before drawing two
  floors at once and the answer is normally yes. Nothing in the port draws more
  than one, so it costs nothing, but the contract and its test said otherwise.
- **The fallback for unplaced rooms broke on that.** It gave a room the floor
  whose box it sat nearest, unambiguous only while floors had disjoint bands.
  Inside a shared footprint every candidate is distance zero and the first index
  wins, so all twenty of The Lurking Horror's unplaced rooms would have piled
  onto one floor. `page_via_routes` in map_model.c now walks level exits to the
  nearest room the atlas does place and takes its floor.

Also fixed: `room_graph` was discarding the destination byte on a conditional
exit, which cut floors apart at every locked door. Only OPEN exits are scored,
so it moves no room; it only joins floors that a door already joined.

### The ceiling that would have capped it anyway

`MAP_ATLAS_PAGE_MAX` was **8**, and `map_atlas_bind` clamps a table declaring
more -- folding every floor past the ceiling onto the top one. Nothing fails to
compile and nothing says so at run time; the map pages to the ceiling and stops,
with several storeys piled on the last page. Eight was ample while a floor was a
drawn sheet and no publisher printed more than four. Route floors take
Stationfall to twelve and The Lurking Horror to ten.

Raised to 16 (twelve used, `g_box` is signed chars so the cost is 64 bytes), and
`saturn/tests/test_atlas_floors.py` now holds the shipped tables under it. That
test also checks the trailing count in `MAP_ATLAS_STORIES` against the floors
the cells actually carry -- the two are written by different lines of the
generator, and it is the entry that `map_atlas_bind` reads -- and pins that The
Lurking Horror and Stationfall keep at least seven floors, so a regeneration
that quietly went back to paging by sheet fails there rather than on a console.

Worth knowing for the next debugging session: the C snippet I first used to read
the count printed `pages=0` for Zork I and `8` for The Lurking Horror, which
looked like a table fault. It was my snippet -- `printf("%d %d", bind(), pages())`
has unspecified argument evaluation order, so `pages()` ran before `bind()`.
Both were in fact clamped to 8.

## The regression, and how it got past me

**Wave four shipped a defect.** The nudge broke Zork I's canyon: Canyon View,
Rocky Ledge and Canyon Bottom descend by three down exits, and the nudge scored
only cardinal alignment, so it was free to reorder them vertically while
straightening something else. `test_map_atlas.c` asserts that descent and it
failed. Bisected to 1ea507a -- mine, this session -- not to the branch base.

The cause of the miss: **there are five map host tests and I had been running
four.** test_map_layout, test_map_edges, test_map_marks and test_map_model were
in my loop; test_map_atlas was not, and it is the only one that reads the
generated table. Anything that regenerates map_atlas_data.inc must run it.

The fix generalises the guard the nudge already had: it refused to lose a
half-plane relation, and now also refuses to reverse an up or down exit drawn
between two rooms of one sheet (`VERT`). Cost 692 -> 690 axis exits and 23 -> 21
moves. The Lurking Horror still reaches 32 of 32; the canyon reads correctly.

**And a second, dumber mistake worth recording.** After installing the ten-floor
table I hit that test failure and bisected by writing `git show <rev>:...inc`
over `saturn/src/engine/map_atlas_data.inc`, ending with
`git checkout -- map_atlas_data.inc`. That restored the committed two-floor
table and silently discarded the install. The owner then built and tested and
reported "still only 2 pages" -- correctly, because the change was not in the
tree. Never bisect a generated file in place while an uncommitted regeneration
of it is installed; copy it aside first.

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

`test_hwram_budget.py` fails whenever BuildDrop's link map is the netbin's
rather than the CD's -- the two configs share the BuildDrop names and it reads
the heap size off whichever built last. Build the CD target last before running
the suite.

**Never seen on a screen.** The previews in this session were composited from
the real tiles, the real palette and `write_palette`'s own arithmetic by a
scratch script -- not captured from the console. The owner's original report
came from screenshots that did not reach this session, so the specific rooms
named (West of House, North of House, Forest) were not confirmed; the cause was
established from the code path, not from the images.
