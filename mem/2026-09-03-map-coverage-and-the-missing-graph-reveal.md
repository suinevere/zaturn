---
name: 2026-09-03-map-coverage-and-the-missing-graph-reveal
description: "Why thirteen of the disc's stories -- Starcross among them -- show an empty map on Easy: the reveal reads the authored atlas and nothing else, and the graph walk everyone thinks of as the backup only ever places the room the player is standing in. Measured atlas coverage per story, and then a spike that measured what a synthesised table would actually be worth: 26 of 31 stories clear the generator's own drop rule, and the merge that anchors a walk on the scan's cells reaches 100% coverage for two points of half-plane agreement."
metadata:
  type: reference
---

Started as an owner's question, became a measured spike, and stage one is now
shipped as `77f3796`. The probe scripts were throwaway; what shipped was written
fresh against tests. Companion to [[2026-09-03-map-inset-to-parchment-handoff]] and
[[2026-09-03-map-party-colours-handoff]].

## The two things that decide how much map you see

**The reveal is atlas-only.** `map_view_show` runs `map_model_reveal_atlas()` on
`DIFF_EASY` and `map_model_clear_reveal()` otherwise, and that function's whole
body is a loop over `map_atlas_count()`. A story with no authored table has a
count of zero, so Easy places nothing and behaves exactly like Medium.

**The graph walk is a placement rule, not a reveal.** `map_model_enter` places
the room the player is standing in, one step from the room they came from, and
runs once per turn. There is no pass anywhere that walks the exit graph and lays
out rooms nobody has been to. That is the gap between what the fallback is and
what it sounds like it is.

## Measured coverage, per shipped story

Atlas cells against the room count read off each `.Z3` (an object with any
direction property).

| story | rooms | atlas | | story | rooms | atlas |
|---|---|---|---|---|---|---|
| SORCERER | 83 | 78 (93%) | | BALLYHOO | 37 | 25 (67%) |
| PLNDHRTS | 58 | 52 (89%) | | INFIDEL | 77 | 51 (66%) |
| STATFALL | 105 | 90 (85%) | | WITNESS | 29 | 19 (65%) |
| HOLYWOOD | 67 | 55 (82%) | | LEATHERG | 73 | 42 (57%) |
| ENCHANTR | 66 | 52 (78%) | | SUSPENDD | 62 | 31 (50%) |
| WISHBRNG | 52 | 41 (78%) | | ZORK3 | 86 | 43 (50%) |
| SPLBRKR | 66 | 50 (75%) | | ZORK2 | 80 | 32 (40%) |
| ZORK1 | 111 | 84 (75%) | | CUTHROAT | 86 | 28 (32%) |
| LURKING | 71 | 51 (71%) | | MOONMIST | 65 | 19 (29%) |

**No table at all**: ADVENT, DEADLINE, HITCHHKR, HYPOCOND, INFOSAM5, INFOSAM7,
MZORKI, MZORKI2, MZORKII, PLNTFALL, SEASTLKR, **STARCROS**, SUSPECT. Thirteen
stems. Plundered Hearts is the second-best covered table on the disc; a map that
looked thin there is Medium difficulty, not missing data.

## The spike: what a synthesised table is actually worth

The seam is better than expected. `gen_map_atlas.py` splits cleanly:
`snap`/`assign` turn *scan pixels* into a seed, and `agreement`, `alignment`,
`nudge` and `storeys` never look at the scan at all. So the pipeline runs on a
story with no PDF -- **the transferable thing is the pipeline, not the
coordinates.**

Method: replace the scan seed with a breadth-first walk of the story's own exit
graph, one floor at a time, resolving a taken cell by searching outward the way
`map_model.c`'s `place()` does. Then score it against the eighteen authored
tables, which is ground truth already in the repo.

**A raw walk is much worse than a scan.** Like-for-like on the same rooms:
authored 96% half-plane / 89% on axis, walked 80% / 69%.

**One missing pass closes most of the gap.** `nudge` may only ever RAISE the
axis count and is forbidden from losing a half-plane, so by construction it
cannot repair a half-plane the seed got wrong -- and an outward cell search gets
plenty wrong. Its missing counterpart, the same greedy shape scored on
half-plane first and axis second, takes the walk to **94% / 78%**: two points
behind the scan on the test the drop rule uses, eleven behind on axis.

**Under the generator's own PASS_RATE of 0.85, 26 of 31 stories clear it fully
synthesised**, including STARCROS at 87% and PLNTFALL at 92%. Five do not:
LURKING 85% (which has a scan anyway), DEADLINE 83%, ZORK3 81%, ADVENT 78%,
HYPOCOND 25%.

**Quality is geography, not code.** Drawn out, Starcross reads well -- it is a
spaceship of regular corridors and the walk suits it. Zork I becomes a dense
tangle: 90 rooms on one route floor, sprawling and hand-drawn, 53% on axis. The
numbers say the same thing per game: BALLYHOO, WITNESS, MOONMIST and SUSPENDD
synthesise at or near 100%, ZORK1 at 90/53 and SORCERER at 86/69.

**The merge works and costs almost nothing.** Anchor every authored cell exactly
where the scan measured it, walk the missing rooms outward from their placed
neighbours, repair only the added ones: **1274 of 1274 rooms placed, up from
843, for 96% -> 94% half-plane and 89% -> 80% on axis**, with an assert proving
no anchored room moved. Zork I would go from 84 rooms to all 111.

### What the spike found that was NOT anticipated

- **Floors are not a problem for a tableless story.** The earlier note in this
  file said the floor question "has no answer without a table". Wrong:
  `storeys()` derives floors from the level-exit graph alone and gives sane
  counts -- Starcross 6, Planetfall 6, Suspect 1, Seastalker 1 -- all far under
  `MAP_ATLAS_PAGE_MAX` of 16.
- **Floors ARE the problem for the merge.** Authored pages are drawn sheets;
  route floors are levels; the two do not compose. A newly-placed room has to
  join a page and there is no obvious rule. The quick version -- inherit the
  page of the neighbour it hangs off, spill the unreachable onto new pages --
  gave Cutthroat 37 floors against a ceiling of 16. **The hard part of the merge
  is paging, not placement.**
- Cost is nothing: `MapAtlasCell` is 4 bytes, so the ten shippable tableless
  stories are 603 cells, about 2.4 KB of ROM. All of it offline; no runtime
  change at all.
- One thing to check before implementing: `MapAtlasCell.room` is an
  `unsigned char`, so a story whose room objects exceed 255 cannot be tabled.

## Stage one, shipped (`77f3796`)

`gen_map_atlas.py --walk` adds a table for every story with **no table** -- not
every story with no map, which is the bug I wrote first and which skipped
Starcross, Planetfall, Deadline and Suspect: those four have a scan that was
read and then rejected for disagreeing with their own exits, so keying off
`MAPS` missed exactly the games whose drawing had already been tried.

Three additions to the generator: `walk_seed`, `repair`, and a carry-forward.
The carry is the part worth remembering -- it reproduces each already-emitted
table as **verbatim text**, so a walk run needs neither the map scans nor the
cache they live in, and its diff is 1,000 insertions and **zero deletions**.
That is the proof that no measured coordinate moved, and it is cheaper than any
assertion could be. A second `--walk` run is byte-identical to the first, which
also proves the carry round-trips its own output.

Ten tables shipped: HITCHHKR 93%, INFOSAM5 96%, INFOSAM7 85%, MZORKI 85%,
MZORKI2 85%, MZORKII 96%, PLNTFALL 92%, SEASTLKR 100%, STARCROS 87%, SUSPECT
98%. Three still ship nothing, below the same `PASS_RATE` a scan must clear:
ADVENT 78%, DEADLINE 83%, HYPOCOND 25%. Cells 843 -> 1,446, which is 2.4 KB.
No runtime source changed at all.

Worth knowing: **`nudge` moves nothing on a walked table** -- it reported 0 on
all ten. `repair` breaks ties on axis alignment, so it has already taken every
move `nudge` would have found. `nudge` still runs, as a guard rather than as a
contributor.

`test_atlas_walk.py` holds the algorithm on lattice fixtures (a lattice must lay
out as a lattice; `repair` must fix what `nudge` cannot and must not reach past
`REPAIR_SPAN`; neither may turn a descent over), and holds the shipped file's
provenance marks so a measured table cannot quietly become a derived one.
`test_map_atlas.c` binds Starcross by its real release and serial, which is the
assertion that the runtime cannot tell the two kinds apart and does not have to.

## Stage two, shipped (`a0350a9`)

`gen_map_atlas.py --merge` walks the missing rooms into every measured table:
**843 rooms become 1,274**, and the eighteen scans keep every coordinate they
read. Zork I 84 -> 111, Zork II 32 -> 80, Moonmist 19 -> 65, Cutthroat 28 -> 86.
Nothing was dropped; the whole atlas is now 1,877 cells, 7.5 KB.

**The paging rule is the whole of it, and it is a dissolution rather than a
solution.** The open problem was that authored pages are drawn sheets, route
floors are levels, and an added room has to join one. Trying to inherit a sheet
and re-derive the floors was measured first and is what NOT to do: the shipped
table records the densified (sheet, level) pair and **not the sheet**, so the
sheet cannot be recovered from it at all, and approximating it took one game to
**35 floors against a ceiling of 16** and moved 53 of Zork I's measured rooms to
other pages.

What works instead: an added room joins the page of a placed room it shares a
LEVEL with -- same route-floor component, therefore same sheet and same storey
by definition -- and only a room sharing a level with nothing placed falls back
to its nearest placed neighbour's page. Across the eighteen that was 304 by
level and 127 by neighbour. Because **no page is ever invented**, three things
hold by construction rather than by luck: a game's floor count cannot change, no
measured room can change page, and the ceiling cannot be breached.

Cost: half-plane holds at 93%, axis 78% -> 75%. The losses land where the scan
caught a minority -- Lurking 100 -> 89 half and 100 -> 69 axis, Moonmist 100 ->
87 and 100 -> 58, Zork I 95 -> 88 and 62 -> 52. Nothing fell below `PASS_RATE`,
and a table that did would keep its measured cells alone.

### Two things I predicted wrong

- **I said the diff could not stay additive.** It did: 701 insertions, zero
  deletions. A measured cell's emitted line is unchanged and added cells
  interleave by room number, so git sees pure insertion after all.
- **I planned to lower the alignment baselines.** Wrong instinct -- that would
  have spent the guard to buy the feature. `tables(measured_only=True)` filters
  on the `+` marker each added cell carries, so `BASELINE` now asserts **exact
  equality** on the measured rooms (they must not improve either: a reading that
  changed is a reading that was wrong once), and a separate `WHOLE` holds the
  filled table to a floor. The suite is stronger than before the change.

## The regression stage two shipped, and its root cause (`178058f`)

The owner: "Lurking Horror worked great before this change, now seeing lines to
no where." It did, and it was.

**Root cause: unplaced does not mean missed.** `assign()` refuses to place a
group of more than `AMBIG_MAX` identically-named rooms, because it cannot say
which drawn box is which -- that is what excludes Zork I's fifteen Maze rooms
and it is written in the generator's own header. The fill read every unplaced
room as an OCR failure to repair. Some were not failures, they were **refusals**,
and the reason had not gone away: a maze's drawn positions are an arbitrary
embedding, its exits contradict each other on any plane, and Infocom printed
those pages schematically or not at all for exactly that reason.

The evidence, in order: link spans were fine (only 3 over four cells) and no two
rooms shared a cell, so the first two hypotheses died. Drawing every floor
showed it -- floor 5 went from 6 rooms to 19 and the added thirteen were a knot
around the origin, five to eleven cells from the floor's measured rooms. Dumping
the floor named them: **all thirteen are called "Wet Tunnel"**, with exits
reading `east (+1,-3)`, `west (+3,-2)`, `north (+3,-5)`.

Across the atlas it was 88 of 431 added rooms and 29 of the walked ones: Zork I's
Maze x15, Zork III's Narrow Room x10 and Land of Shadow x8, Infidel's Desert x10
and Cube x8, Spellbreaker's Octagonal Room x9, Zork II's Oddly-angled Room x9,
Enchanter's Courtyard x7, the Mini-Zork mazes.

`unresolvable()` states the rule once and both the walk and the fill honour it.
Those 117 cells go back to the runtime's own placement, a room at a time as the
player walks in, which is the honest answer for geography that has no plan.

**Refusing to draw a maze also stops it dragging the rest of the map down.**
Adventure went 78% -> **94%** and now ships a table where it had none; Mini-Zork
I 85% -> 92%, Mini-Zork II 85% -> 91%, Sampler 5 96% -> 98%. Lurking's
off-viewport runs are back to the five it had before the fill -- the same five.

**What the suite got right and what it missed.** The exact-equality guard on
measured rooms held through the whole regression and the whole fix: not one
scanned baseline moved, which is why the fix could be made confidently. What no
test asked was whether a table places rooms it has no business placing, because
half-plane and axis agreement say nothing about it -- a maze room can satisfy
"east of" perfectly and still be nonsense. That check exists now.

## The second regression: a staircase drawn as a corridor (`724f749`)

The owner again, after the maze fix: "LURKING still wrong, seeing lines to no
where, Floor 7 Renovated Cave North for example." Right again, and a different
bug -- and this one was not in the tables at all.

Renovated Cave (object 201, floor 6, which the map shows as 7) has two exits,
`down` and `south`. There is no north exit, which is what made the report worth
taking literally rather than filing under the first fix.

**`map_view.cxx` had two passes each testing its own condition, and between them
they lost the staircase.** The link pass gives any exit whose far end it has not
gathered to `edge_stub`, which lays a run into the gutter toward wherever the
far room is drawn -- it never asks whether the exit is vertical. The glyph pass
declined any exit whose far room was `map_model_visited` and on this page, on
the assumption that the link pass would draw it properly; true only while both
ends are on screen. So `down` to Before the Altar, placed six cells west and six
north on the same floor, drew a line leaving the room NORTHWARD and no D at all.

**Latent since the glyph pass was written.** It needs the far end of a vertical
exit to be placed and off screen, and before the tables were filled in those
rooms were mostly unplaced -- `map_model_offset` failed inside `edge_stub` and
nothing was drawn, while the glyph pass had the exit to itself and was right.
Filling the tables in is what made it reachable. It stands on **33 exits across
the disc**, eleven of them in Moonmist.

`map_layout_offview` is now the one rule both passes ask, and it lives in
`map_layout.h` -- the header whose stated reason for existing is that a host
test can exercise what a build for the target cannot be run to check.
`test_map_layout.c` holds all four cases and that the two passes can neither
both claim an exit nor both decline it.

**The lesson for the next one.** Both regressions were latent faults that adding
rooms made reachable, not faults in the rooms added. Coverage work is a stress
test of the drawing code, and the drawing code has no host test at all --
`map_view.cxx` cannot be linked without SRL. Moving a decision into
`map_layout.h` is the available answer and is worth doing every time one of
these is found.

`test_atlas_merge.py` holds the frozen set (a frozen room never moves; freezing
nothing is bit-for-bit the old behaviour; a frozen room is still an anchor its
neighbours score against), the paging rule, and the shipped file's own
consistency. Note `REPAIR_SPAN` is 3 -- two test fixtures were written displacing
a room further than that and had to be corrected, not the code; the locality
bound is real and now has a test of its own.
