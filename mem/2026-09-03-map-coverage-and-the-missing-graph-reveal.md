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

## The third regression: a room parked at the origin, and the U that never drew

The owner, on The Lurking Horror: "'Concrete Box' at least showing the dashed
lines now, but missing any direction indicator, the U for up to Basement
missing, and it connects to the Steam Tunnels also missing (N). Why are those so
hard to nail down -- guessing there is an algorithm MAYBE attached to them?"

That guess is exactly right, and it is one fact with two consequences. Concrete
Box (object 37) has three direction properties and **not one of them is a plain
destination**: `up` and `out` are three bytes, a routine that decides at run
time and names nothing; `north` is four bytes, a conditional that does name
Steam Tunnel 138.

### Consequence one: the layout could not see the room (`fill_seed`)

Every layout pass in `gen_map_atlas.py` reads OPEN exits and nothing else --
`agreement` scores them, `repair` moves rooms by them, `fill_seed` walks along
them. Concrete Box has none, and nothing anywhere leads back into it, so the
fill could not reach it by any route and dropped it into the island branch:
**seeded at the origin of its own page**. It shipped at (0,0) on page 6 while
the Steam Tunnel it opens onto sat at (-1,10) -- ten cells away. On screen that
is a dashed run leaving the room SOUTHWARD into the gutter, for an exit the
story says is north.

`mapscan.room_graph`'s own header already said the answer: "a door or a flag is
a perfectly good way to walk from one room to the next." The floor pass has
always read conditionals that way. The seeds never did.

**`settle_unstated` is the fix, and WHERE it runs is the whole of it.** Two
earlier shapes were written and measured first, and both were worse:

- *Let a door place any room the fill cannot reach.* Zork II and Zork III gained
  axis alignment; Moonmist lost three exits, fell from 87% to 83%, and was
  dropped by `PASS_RATE` -- 46 filled rooms gone. A door had dragged a whole
  OPEN component off its island seed and in among the measured cells, where
  `free_cell` resolved collisions the metric can see for the sake of a door the
  metric cannot.
- *Narrow it to rooms no plain exit touches, still inside the seed.* Moonmist
  lost two exits instead of three. Same cause one step smaller: a room placed
  mid-sweep takes a cell the stated layout then has to route around, and
  `repair` refuses a move onto an occupied cell.

What works is to leave the seed **byte-for-byte alone** and move these rooms
afterwards, once `repair` and `nudge` have both finished. Then an added room can
push nothing but itself, and the guarantee is not an argument but an
observation: **the whole generator's per-table log is identical before and
after** -- every agreement, every axis count, every floor, every drop. 39 rooms
across the disc leave an origin cluster for the room whose door reaches them,
and Concrete Box lands at (-1,11), one cell south of Steam Tunnel 138.

`unstated()` is the predicate -- a room no OPEN compass exit touches at either
end. Vertical OPEN exits do not count, because `HALF` and `AXIS` have no entry
for up or down and neither pass looks at them either.

Where it still declines: Before the Altar and Third Floor reach their neighbours
only through `up`/`down`, which have no planar step to derive a cell from, and
they keep their origins. Object 49 is not a room at all -- a nameless object
whose only exit is `in` to itself -- and has been in the table all along.

### Consequence two: the runtime dropped the exit entirely (`map_model_exits`)

`map_model_exits` skipped every exit with `dest == 0`, which is what a routine
exit leaves behind. Reasonable-looking -- an exit with no far end has no line to
draw -- and wrong for a staircase, because **U and D annotate the mark and claim
nothing about where the stair comes out**. The glyph pass already draws one for
a staircase whose far end is merely off the viewport; this is the same drawing
with less known about it. Concrete Box showed no U at all, and neither did Zork
I's chimney, grating or trap door: **189 vertical exits across the disc**, every
one of them a passage the player can walk.

Vertical only. A flat exit with no destination has its direction already, no far
end for a run to reach and no glyph of its own; letting one through would hand
the link pass a stub to lay toward a room it cannot find. `test_map_model.c`
asserts both halves of that.

`g_slot[0]` is permanently -1 because `gather()` starts at room 1, so a dest of
zero falls through both passes' "far end not gathered" branch without a special
case -- `map_layout_offview` sends it to the glyph pass and away from
`edge_stub`, which is the rule `724f749` put there for a different reason.

### Regenerating the atlas

`--walk --merge` must run against the last **measured-only** `.inc`, not the
shipped one: `carried()` reads every cell as an anchor, so a second merge over
an already-merged file would freeze the added rooms exactly where they are and
append a second FILLED IN paragraph to each header.

```
git show 82dbbd2:saturn/src/engine/map_atlas_data.inc > /tmp/base.inc
# point gen_map_atlas.INC_PATH at /tmp/base.inc, then
python tools/gen_map_atlas.py --walk --merge > saturn/src/engine/map_atlas_data.inc
```

That reproduces the shipped file byte-for-byte at `86c9751`, which is how the
"log identical" claim above was checked. It is worth knowing before the next
regeneration and is written down nowhere else.

## The fourth: `out` drawn as a staircase going down

The owner, on the same story: "Why does Terminal Room show south and out in
game, and south and a D in the upper right on the map? Is out labelled as down?"

It was. Two expressions of the rule "which exits change floor", each written as
arithmetic on the direction index, and both wrong the same way:

- `record_exits` took the kind from **`d >= RM_UP`**. The enum reads
  `… RM_UP, RM_DOWN, RM_IN, RM_OUT`, in the order the compass rose draws them,
  so IN and OUT fell on the staircase side of that test for no reason but their
  position in a list.
- The glyph pass took the letter from **`(dir & 1) == 0`**. True of RM_UP and
  RM_DOWN by coincidence; `RM_OUT` is 11, odd, so it drew **D**, and `RM_IN` is
  10, even, so it would have drawn **U**.

`gen_map_atlas.py`'s `LEVEL_DIRS` has had the right rule since the floor pass
was written: *"The exits that do not change floor. Everything except up and
down, including in and out -- walking into a building puts you on its ground
floor, not above or below it."* The runtime never agreed with it.

**Scope, and which half is new.** 520 in/out exits across the disc that carry a
destination were already drawing stair bars and a letter; that half is as old as
`record_exits`. The 142 destination-less ones only started drawing when
`map_model_exits` began admitting vertical exits with no far end, which is what
put a bare D on Terminal Room -- whose only two direction properties, `south`
and `out`, are the same routine, the terminal puzzle itself.

`MAP_DIR_VERT` in `map_model.h` is now the one place the question is asked, and
`test_map_model.c` holds both halves: a destination-less OUT is flat and so is
dropped and draws nothing, an OUT that names a room is flat and draws as an
ordinary passage, `map_model_link` agrees, and UP and DOWN are untouched.

Not every `>= RM_UP` is this bug. `map_model_step` asks a different question --
which directions have no planar step -- and up, down, in and out is the right
answer to that one, the same set `STEP` omits in the generator. It was read and
left alone.

**The lesson, which is the third time this shape has appeared.** All four of
these regressions are one rule spelled out in two places that drifted, or spelled
out as arithmetic that happened to be true of the cases it was written for. The
answers have been the same each time: `unresolvable()`, `map_layout_offview`,
`unstated()`, `MAP_DIR_VERT` -- name the rule, put it where a host test can
reach it, and make every caller ask it.

**Still not seen on a screen.** Nothing on this branch has been built for the
Saturn target or run. Terminal Room should now show the link running south to
Second Floor -- which is drawn from Second Floor's end, since its own `south` is
a routine that names nothing -- and no letter at all.

## Paging that keeps your place, and floors that line up under it

Owner request: "Can we have the cursor not reset changing page, and any
connecting rooms above and below each other on same coordinates?" Two halves of
one thing, and the second is what makes the first worth having.

### The cursor

`map_view.cxx` re-centred on a floor change -- `hx = (x0+x1)/2`, `sx = hx` --
which threw away the one thing the reader was holding. It now clamps the
crosshair into the new floor's box and lets the view follow, which is the rule
the cursor step already used; `map_layout_clamp` is that rule named once, in the
header a host test can reach, and `test_map_layout.c` holds it.

### The floors

`align_pages()` gives each floor one offset so that a staircase comes out at the
coordinate it went in at. Only one floor is drawn at a time, so a stair has no
line to follow across a page change and the coordinate has to carry it.

Cross-page vertical exits vote for the difference that would make their two ends
coincide; page pairs are merged heaviest vote first into a spanning **forest**,
so two floors with no staircase between them keep their own origins rather than
being stacked on a guess. Contradictions are the normal case -- two floors joined
by two staircases that disagree can satisfy at most one -- so this is a
plurality, not a solution.

**127 of 231 (55%).** The Lurking Horror 19 of 29, Stationfall 18 of 19,
Starcross 8 of 9, Hitchhiker and Zork II and Infidel all of theirs. In Lurking
that is ten of fifteen passages: the Great Dome, the Brown Building and the
Skyscraper stacks all line up, and so does the Stairway through Aero Lobby,
Subbasement and Basement.

**What I got wrong, and it is worth keeping.** I claimed the slide was free --
"a floor moves as a whole, so every relation is invariant." Every relation
*inside* a page is. But `agreement` and `alignment` score every compass exit
between two placed rooms and **do not care what page either is on**, and a story
whose floors are one drawn sheet has plenty of those. The unguarded version cost
Hollywood four half-plane exits and four axis off a table that had been at 100%
of both, Cutthroat two axis, and Zork I one half-plane. I had written the
invariance into the emitted header before measuring it, which would have shipped
a false claim in the file.

The guard is nudge's, for nudge's reason: each merge is applied, scored, and
reverted unless it loses neither. That costs nine of the 136 unguarded landings
and buys back the claim. Cutthroat goes 6 to 0 -- its three floors are one sheet
with stated exits running between them, and straightening its stairs would have
bent those. Zork I ends one axis exit **better** than before, which the guard
allows because it only refuses a decrease.

**The regeneration is otherwise byte-identical**, which is the check that this
did what it says: one line of the whole per-table log moved, and it moved up.

### The regression the alignment caused, and the older one it uncovered

The owner, on the next build: "Terminal Room now shows as east to Second Floor,
not south. Cursor fix works though."

The atlas was innocent -- floor 7 had moved rigidly by (0,+1) and Terminal Room
was still one cell due north of Second Floor. The runtime moved them.

**`cell_taken` asked whether ANY placed room held a cell, with no reference to
the floor.** So `place()` treated every coincident cell as a contest and flung
the room entered second out to a ring cell. Second Floor (floor 7) and Computer
Center (floor 1) share a cell by design -- that is a staircase landing, the whole
point of `align_pages` -- so Second Floor was displaced, and it landed east of
the Terminal Room.

A cell is owed to be unique **within a floor**, not across the table: `gather()`
and `extent()` both filter on the page, so two rooms on different floors are
never drawn together and cannot hide each other. `cell_taken` now asks the atlas
for both pages and ignores a room on another floor. It asks the TABLE and not
`map_model_page`, which for a room the atlas does not cover runs a
breadth-first walk -- this is called once per ring cell of a contested
placement, and a walk in that loop would be paid hundreds of times over. A room
the atlas does not place keeps the whole-table rule, which is the conservative
half and the one it already had.

**This was already wrong before the alignment.** 135 rooms across the disc
already shared a cell with a room on another floor -- Adventure 23, Starcross 19,
Cutthroat 16 -- and every one of them was being displaced for nothing. What the
alignment did was raise it to 183 and, by design rather than by accident, put 14
of them in the game the owner was looking at.

Verified on the host rather than by argument: a throwaway harness feeds all 59
of The Lurking Horror's placed rooms through `map_model_enter` from the real
exit graph and prints the offsets. Second Floor is dx=0 dy=+1 from Terminal
Room, Computer Center is at the same cell one floor down, and the whole story
has **0 cells shared within a floor and 16 across** -- which is the invariant
stated twice. `test_map_model.c` holds both halves on Zork I's Kitchen, Attic
and Cellar, which sit on one cell across three floors.

**The lesson, again.** The generator has kept cells unique per page since
`fill_seed` was written -- `taken.setdefault(page[r], set())`. The runtime never
did. Two halves of one rule, in two languages, disagreeing quietly; the same
shape as `LEVEL_DIRS` against `record_exits`, and the same shape as the two
drawing passes. What is new here is that I introduced a change whose whole
purpose was to violate the runtime's unstated assumption, without ever going to
look for it.

### Paging that finds something, after two goes at it

Owner: "Lurking Horror not showing Third Floor or Computer Center at all.
Cursor also snapping around to different rooms when only one on screen while
changing pages."

Three faults, and the second and third were mine from the commit before.

**A floor is routinely taller than the viewport.** Five rows; The Lurking
Horror's first floor is eleven and its sixth is thirteen. Clamping the crosshair
into the floor's bounding box can leave it on empty ground with every room off
screen, which reads as a floor holding nothing. `map_model_nearest` lands it on
the closest placed room instead, so something is always drawn -- and where the
floors line up, that room is the one the staircase reaches: page down from
Second Floor and the crosshair is on Computer Center at distance **zero**.

**Clamping was eating the reader's place.** A floor holding one room drags the
crosshair onto it, and the clamped value was being remembered as the request, so
paging through Lurking's three single-room floors erased where you had been.
`wx, wy` now holds what the reader last asked for; the D-pad writes it and a
floor change only reads it.

**`map_layout_follow` is wrong for a floor change.** It moves the view the least
it can, which is right for a cursor step -- the map should not lurch under a
D-pad press -- and wrong on landing, because it leaves the landing on whichever
edge it entered by with the rest of the floor beyond it off screen. That is
exactly what hid Third Floor: the crosshair landed on the Roof and Third Floor
sat two rows above the top of the viewport. A floor change centres now.

**And a room reached only by stairs had no coordinate at all.** Third Floor's
exits are `up` to the Roof, `down` to Second Floor and an `out` the story
decides by running code -- no planar exit at either end, so neither the fill nor
`conditional_cell` had anything to say and it took its page's origin ten rows
from every other room on floor 8. `staircase_cell` gives it the far room's own
cell, which is the convention `align_pages` already established.

**The preference inside that rule was measured the wrong way round first.**
Sending Third Floor to Second Floor's cell one floor down did make paging land
on it -- and left it eleven rows from the Roof it opens onto, a stair drawn as a
line across the whole sheet, which is the fault this branch has spent three
commits removing. A room belongs beside its own floor's neighbours; getting to
it from the floor below is the crosshair's job. With the preference flipped,
floor 8 draws Third Floor and the Roof one cell apart and paging up from Second
Floor shows both.

`align_pages` had to stop counting votes from rooms in `unstated()` at the same
time: `settle_unstated` now runs after it and cannot run before, since it reads
the finished positions, so an island's origin would otherwise have been deciding
where a whole floor goes.

Checked by simulation rather than by argument -- a throwaway harness feeds all
59 of Lurking's placed rooms through `map_model_enter` from the real exit graph
and prints, for each floor in turn, what `gather()` would collect. Floor 8 comes
out "Third Floor, Roof"; floor 1 "Computer Center, Smith Street, Roof of Great
Dome, Elevator, Temporary Lab"; floor 7 unchanged.

**Still open.** Object 49 is in the table and is not a room -- a nameless object
whose only exit is `in` to itself -- so Lurking's floor 0 pages to a blank
crosshair sitting on it. Five more like it across the disc (Zork I 82, Zork II
230, Wishbringer 34, Spellbreaker 41, Stationfall 40 "it"). `room_graph` admits
any object carrying a direction property, which is the same test the runtime
applies, so dropping them needs a rule better than "the name is empty".

### U and D go where the rose puts them

Owner: "Random U upper left of roof. Put them in respective position they are on
the rose, U upper left D lower left. Crosshairs cover any U D."

The stray U was Third Floor's own `up` glyph. `map_layout_glyph` aims two cells
along a preferred direction, which for a staircase was the direction of the far
room; Third Floor and the Roof are one cell apart, so the letter landed nearer
the Roof than its own room and read as the Roof's exit.

`map_layout_updown` puts it in the same two places on every room instead: two
cells left, one row up for U and one down for D -- the compass rose's own left
column, `CR_CELL` having UP at its top and DOWN at its bottom. Two constraints
pick those cells and both are in the test:

- **The column is two out, not one.** The crosshair is four brackets on the
  mark's diagonals, so a letter at a diagonal is covered exactly when the room
  is picked, which is the room the reader is asking about.
- **The row is one off, not two.** `MAP_ROOM_DROP` prints the picked room's name
  two rows under the mark. That constant moved into `map_layout.h` so the test
  can state the relationship rather than the two drifting apart; the rest of the
  text-row constants stayed in `map_view.cxx`, since `MAP_ROW_TOP` depends on
  `party.h`.

A fallback steps further out along the same side rather than crossing to the
other one: a letter that jumped to the right of the mark would read as a
different room's.

**`map_edges_stub` now has no caller.** The letter used to be paired with a
short stub when it sat two cells straight out; at a fixed diagonal-ish cell a
stub would be a second mark for one exit, which is what the drawing rules
already forbid. Left in place rather than deleted, with this note: if the bare
letter reads badly on a television the stub comes straight back, and that is a
decision to take from a screenshot and not from here.

### The vote exclusion, backed out one commit later

Excluding `unstated()` rooms from `align_pages`' votes was wrong, and the owner
found it in the next build: Third Floor is the ONLY room joining The Lurking
Horror's floor 7 and floor 8, so excluding it left the two floors with no edge
between them at all and paging up from the Terminal Room landed eleven cells
away. `settle_unstated` runs before `align_pages` again -- which is the only
window that works, since it has already put such a room on the stair that
reaches it, and running it after leaves the room at its page's origin while the
votes are counted.

Floor 8 now lands two cells from Second Floor showing Third Floor and the Roof
together; floor 1 lands on Computer Center at distance zero.

### Open, and blocked on a decision

Infocom's own drawing and the compiled story disagree about which storey the
Terminal Room is on, and the flat map at
`highprogrammer.com/alan/games/video/ifmaps/lurking.pdf` is the evidence.
Extracting its text with positions -- `zlib.decompress` over each FlateDecode
stream, then tracking `Tm`/`Td` before each `Tj` -- gives the drawn rows:

```
y 618   Roof
y 563   Third Floor        Terminal Room (y 571)
y 503   Kitchen            Second Floor        Smith Street  Smith Street
y 443   Elevator           Computer Center
```

Terminal Room is drawn on the Third Floor's row. The story says
`Second Floor --north--> Terminal Room`, a plain one-byte compass exit, so
`storeys()` puts them on one level and our page 7 holds Terminal Room, Second
Floor and Kitchen together.

Two further facts:

- **Roof and Third Floor share page 8 although a staircase joins them**, because
  Third Floor's `out` names the same room as its `up` and `LEVEL_DIRS` counts
  `out` as level-preserving. **63 in/out exits across the disc duplicate a
  vertical exit of the same room** -- Zork I's Temple `out` to the Torch Room,
  Starcross's eighteen airlocks -- so "an in or out naming the room an up or
  down already names is a synonym for the stair" is a well-attested rule and the
  same shape as the `record_exits` fix.
- **Neither can be changed without re-running the scan.** A merged table's pages
  are carried from the shipped `.inc` as text; `storeys()` only runs on a fresh
  scan, which needs the map PDFs from infodoc.plover.net and pymupdf, opencv and
  rapidocr. `build_merged` also asserts a merge never changes a floor count, so
  this cannot be smuggled in through the merge path.

### IN and OUT as a second name for the staircase (owner's call: story wins)

`LEVEL_DIRS` counts IN and OUT as level-preserving, on the reasoning that
walking into a building puts you on its ground floor. That holds until a story
uses one as a second name for a staircase it already has: The Lurking Horror's
Third Floor has `up` to the Roof AND `out` to the Roof, both one-byte properties
naming the same object, so the level pass joined the Roof to the storey below it
and the map drew a building whose top two floors were one page.

`level_exits()` states the test once -- an IN or OUT naming a room this room's
own UP or DOWN already names is that same passage under another word -- and both
`storeys()` and `merge_pages()` ask it. **63 exits across the disc**: Zork I's
Temple `out` to the Torch Room, Starcross's eighteen airlocks, Moonmist's ten,
Lurking's four.

**This needed a full re-scan**, which is the part worth writing down. A merged
table's pages are carried from the shipped `.inc` as text, so `storeys()` only
runs on a fresh scan; and `build_merged` asserts a merge never changes a floor
count, so it cannot be smuggled in through the merge path either. The recipe:

```
python tools/gen_map_atlas.py --cache tools/assets/cache > measured.inc
# then point gen_map_atlas.INC_PATH at measured.inc and
python tools/gen_map_atlas.py --walk --merge > saturn/src/engine/map_atlas_data.inc
```

The cache is gitignored but was already populated with 23 PDFs, and pymupdf,
opencv, numpy and rapidocr are all installed, so it ran offline. **A scan with
the rule reverted reproduces every measured cell of `82dbbd2` byte for byte** --
only a file-header paragraph added later differs -- which is what made it
possible to tell the rule's effect from OCR noise.

Result: Leather Goddesses 7 floors -> 9, Spellbreaker 4 -> 5, Stationfall 12 ->
14 against a ceiling of 16, Adventure 4 -> 5 and its axis rate 51% -> 53%,
Starcross 6 -> 8 and 76% -> 79%. Nothing dropped and nothing fell below
`PASS_RATE`. In Lurking the Roof moved from page 8 to a page of its own.

**What it could not do, and why.** Third Floor is not in the scan at all -- the
OCR never read it -- so `merge_pages` places it, and `merge_pages` may not
invent a floor. It joins the Roof's. So the two are still one page, reached by
the short stair link the map draws for a same-page staircase, while Third Floor
now sits on exactly Second Floor's cell one page down. Paging up from the
Terminal Room lands on it. Splitting them properly would mean letting the merge
create a floor, which is the rule that took one game to thirty-five floors and
is not worth revisiting for one room.

### A guard that was over-applied

`test_atlas_axis.py` held every table in `BASELINE` to EXACT equality, walked
tables included, and the argument for exact equality does not reach them: it is
that a coordinate read off a drawing is not supposed to improve, because a
reading that changed is a reading that was wrong once. A walked layout is an
inference, and a better inference is a better map. The floor split improved
Adventure 65/127 -> 67 and Starcross 131/170 -> 136 and the suite called both a
regression. Derived tables are held to a floor now, like `WHOLE`; measured ones
are still held to the letter.

## Paging follows the staircase, not the page number

Owner, on the fourth report of the same thing: "Below Second Floor is steam
tunnels and above it is Third Floor... Are you even checking the map after you
make a change?"

**No, not the thing being reported.** Three sessions of work went into where
rooms sit on a floor and which room the crosshair lands on, and never once into
what L and R actually walk through. Every one of those reports was about page
ORDER. The first thing built this round was the check that was missing, and it
said so immediately: **15 of the disc's staircases run backwards in page order
and 9 skip a page.** The Lurking Horror's Basement goes UP to the Computer
Center and the page number goes DOWN.

### Three attempts at fixing the ordering, all abandoned

1. **SCC-condensed longest-path layering** in place of the breadth-first height
   numbering. A prototype said 15 backwards -> 0. In the generator it gave 9,
   because the prototype ignored `sheet` and the real key is `(sheet, height)`
   -- the sheet sorts FIRST, so a staircase crossing sheets is ordered by which
   page of the book it was printed on. It also cost Zork III a floor and with it
   the cell room its fill needed to clear `PASS_RATE`: 68 rooms back to 43.
2. **Stacking independent groups** so unrelated stacks stop interleaving. Every
   ceiling blown: Lurking 21 floors, Spellbreaker 27, Wishbringer 27, against
   `MAP_ATLAS_PAGE_MAX` of 16.
3. **Keying by `(height, sheet)`** so the staircases sort before the book. Zero
   backwards for Lurking, Spellbreaker and Stationfall -- and Lurking's skips go
   3 -> 10 and Stationfall's 3 -> 16. Trading a wrong direction for two presses
   in the right one is not obviously a win, and Zork I still had 3 backwards.

### What was actually wrong

**Page order cannot answer this and no numbering of the pages ever could.** A
page index is one line; a story's floors are a tree. The Lurking Horror's ground
level has three floors above it and three below, so at most one of each can be
the page next door however the pages are numbered. Every attempt above was
tuning a metric that has no good value.

So the question moved off the atlas and onto the room. `map_model_climb` reads
the staircase out of the room under the crosshair and L/R go to ITS far end,
with the crosshair placed on it. The story states that outright; no layout has
to infer it. Checked by walking the whole disc through it: **277 cross-floor
staircases, 277 land on the room the stair reaches, 0 do not.** Standing on
Second Floor, R is Third Floor and L is the Computer Center.

The page index survives as the fallback for a room with no staircase that way,
which is all it was ever able to do.

### The guard that should have existed three sessions ago

`test_atlas_stairs.py` scores the shipped table against every up and down exit
the stories carry: how many staircases have both ends placed, so the climb rule
can fire (519 of 526, a floor), and how the fallback's page order does per
story (a floor on the ones that land, a ceiling on the ones that run backwards).
Neither number is a target. The point is that a regeneration cannot quietly make
either worse, which is exactly what happened four times without anybody but the
owner noticing.

**The lesson, and it is not a subtle one.** Every fix this branch shipped was
verified against the thing I had just changed and not against the thing being
reported. Coordinates were checked when the complaint was about ordering;
the crosshair's landing was checked when the complaint was about what was
drawn next to it. Build the check for the REPORT first, run it before the fix,
and let it say whether the fix was needed at all.
