# Design: the baggage-limit mark, and the map scan as a second oracle

**Date:** 2026-09-02
**Status:** Designed, not implemented
**Rests on:** `docs/superpowers/specs/2026-09-01-map-passage-marks-design.md` and
`docs/superpowers/specs/2026-09-02-conditional-exit-destinations-design.md`, which
between them shipped four of Infocom's five legend marks and made conditional
passages drawable at all.

## The legend is a closed set

Page 3 of Infocom's Zork I map carries a legend of exactly five passage symbols:

| Legend entry | Drawn as | Status |
|---|---|---|
| Normal passageway | solid line | shipped |
| One-way passageway | arrowhead at the far end | shipped |
| Passageway requiring special equipment or problem solving | heavy dashes | shipped |
| **Narrow passageway (baggage limit)** | **three cross-bars through the line** | **this design** |
| Passageway returning to room of origin | a circle | shipped |

This matters beyond bookkeeping: the mark designed here is the last one, and no
sixth is waiting. The U and D letters are not legend entries but a note underneath
it -- "Vertical passages are labelled U for UP and D for DOWN" -- which is a fair
summary of why they were derivable from the exit graph while this is not.

The cross-bar cluster repeats along the run. The legend's own sample line carries
two, and the Altar-to-Cave route on page 4 carries four. It decorates a passage
the way the dash does; it is not a single glyph at a midpoint.

## What Zork I's baggage limits actually are

Three passages, established from `cd/Zork I - The Great Underground Empire
(Japan)/zork1/1dungeon.zil` and `1actions.zil`, which are in this repository and
are the only such ground truth on the disc:

| Passage | Gate | Where the gate lives | Drawn as |
|---|---|---|---|
| Studio UP to Kitchen | at most two items, one the lamp | `UP-CHIMNEY-FUNCTION` | cross-page labelled stub pair, one-way |
| Altar DOWN to Cave | `COFFIN-CURE`, set from `<NOT <IN? COFFIN WINNER>>` | a `CEXIT` flag | in-page L-route, one-way |
| Timber Room WEST to Drafty Room, and back EAST and OUT | `EMPTY-HANDED`, set in `NO-OBJS` when nothing carried weighs over 4 | a `CEXIT` flag | straight, two-way, **solid** |

Three passages drawn three different ways, which is what makes them a calibration
set rather than a lucky one.

### The case the drawing settles

`DEFLATE` gates Damp Cave and both White Cliffs Beaches with the refusal *"The
path is too narrow."* Read from the source alone that is a baggage limit and
belongs in the table above. Infocom drew those three lines plain, with no
cross-bars: the gate is the boat's inflation, not the player's load. The scan
excludes it. This is the first question the second oracle was asked, and the
reason it is worth having.

## Why the exit graph cannot supply this

Two independent reasons, and both bear on the design.

**The gate is never in the data.** `UP-CHIMNEY-FUNCTION` is a routine. The two
`CEXIT` cases name a flag, and the flag's meaning lives in `NO-OBJS` and in the
coffin clause -- nothing in the compiled property tells a decoder that
`EMPTY-HANDED` is about weight rather than about a door. A static decoder can see
that a passage is conditional. It can never see what the condition is *about*.

**The chimney is not even a passage the graph can place.** From the shipped
decoder, run over the real story bytes rather than read off the ZIL:

```
Studio  (94)  UP    dest 0    RM_EXIT_MAYBE     routine exit
Kitchen (203) DOWN  dest 94   RM_EXIT_MAYBE     IF FALSE-FLAG ELSE "Only Santa Claus..."
```

`map_model.c:551` drops `dest == 0`, so the Studio side draws nothing. The Kitchen
side survives and draws a one-way dashed arrow **pointing at Studio** -- the one
direction the game never permits. The map is currently backwards on this passage,
and no amount of care with the story file can fix it, because the story file does
not contain the answer.

## The contribution rule

The principle in `1bfbc72` was that the story's own data is exact and a scan can
only contradict it. That principle stands, sharpened rather than abandoned:

> **The scan may resolve a passage only when every exit on it is
> `RM_EXIT_MAYBE`.** If any exit on the pair is `RM_EXIT_OPEN` or
> `RM_EXIT_BLOCKED`, the graph has asserted something and wins outright, and the
> disagreement is reported rather than applied.

Both chimney exits are `MAYBE`. The graph therefore asserts nothing about the
chimney -- it says *maybe* twice, because `FALSE-FLAG` is a global no static
decoder can evaluate. Resolving it from the drawing overrules no fact. The gate is
one comparison and is directly testable.

One consequence must be stated because it is counter-intuitive. Supplying Studio's
missing destination *without* retracting Kitchen's would make the screen worse,
not better: `map_model_exits` sets `MAP_EXIT_ONEWAY` only when `has_reverse` finds
nothing, so a Studio-side exit would silently delete the arrow and redraw the
chimney as an ordinary two-way stair. Today's map is wrong in one direction; that
would be wrong in both. The two edits are one edit.

## Architecture

`tools/gen_map_atlas.py` is 766 lines carrying five stages, an emitter and a
`main`. The tracer needs eight of its primitives, so they move out rather than
being duplicated or bolted onto it.

- **`tools/mapscan.py`** (moved, not new) -- `page_image`, `find_boxes`,
  `read_boxes`, `read_labels`, `ocr_engine`, `direction_props`, `room_graph`,
  `match_name`. `gen_map_atlas.py` imports them and its behaviour is unchanged,
  which is proved by regenerating `map_atlas_data.inc` and requiring the result
  byte-identical.
- **`tools/trace_edges.py`** (new; all the risk is here) -- `ink_mask` with the
  detected box rules subtracted, so a trace cannot run around a box instead of
  into it; `follow`, which walks the two-pixel stroke, turns at corners and halts
  at a box border, a page edge or a text label; `classify`, which reads one run's
  symbol; and `hamburger_seeds`.
- **`tools/gen_map_marks.py`** (new) -- reconciles scan against graph under the
  rule above, emits `saturn/src/engine/map_marks_data.inc` and
  `docs/ZORK1_MAP_SCAN_AUDIT.md`.
- **`saturn/src/engine/map_marks.{c,h}`** -- bound to a story by release and
  serial exactly as `map_atlas` is.

`hamburger_seeds` has one named adversary. Page 4 draws **hatched bars** beside
Temple (North Temple's "west wall is solid granite") and beside Mirror Room. They
are short strokes near a line and a naive detector will claim them. They separate
on fill ratio and width -- a cluster is three one-pixel bars spanning about five
pixels, a wall is a filled hatched rectangle -- and that rejection is a test case,
not a hope.

The cross-page case needs no new machinery: the chimney's two ends are labelled
"(to Kitchen)" and "(from Studio)", and `read_labels` already reads every text
label on a page, boxes or not.

## The shipped data

```c
typedef struct { unsigned char room, dir, dest, flags; } MapMark;
#define MARK_BAGGAGE 0x01   /* annotate this exit */
#define MARK_RETRACT 0x02   /* the drawing shows no such passage */
```

Four bytes an entry, six entries for Zork I. A non-zero `dest` sets a destination
the graph left at zero; object numbers are one byte in v3, so the field is exact
rather than generous, for the same reason `MAP_ROOM_MAX` is 256.

Marks are applied in **`record_exits`** (`map_model.c:334`), the single chokepoint
that both the live path and the restore rebind pass through, so `has_reverse`,
`map_model_link` and `map_model_exits` all read one corrected graph and cannot
disagree about it. The baggage annotation rides a `g_bag[room]` bitmask alongside
the existing `g_cond`, and `map_model_exits` ORs a new `MAP_EXIT_BAGGAGE 8` -- the
flag word currently uses 1, 2 and 4.

What Zork I's table should come out as, recorded here so the scan can be checked
against it and **not** so it can be typed in:

```
Studio (94)  UP    dest=203  BAGGAGE     the graph had dest 0
Kitchen(203) DOWN  ----      RETRACT     the drawing shows no descent
Altar        DOWN  ----      BAGGAGE     dest already correct
Timber       WEST  ----      BAGGAGE
Drafty       EAST  ----      BAGGAGE
Drafty       OUT   ----      BAGGAGE
```

## The mark on screen

`MAP_EDGE_BAGGAGE` takes `0x4000`, the lower of the accumulator's two remaining
free bits, so the edge field stays sixteen bits wide and one bit stays spare.

Two eight-by-eight tiles from one drawing, turned by the `rot_cw` already in
`gen_dash_tiles.py` so the pair cannot drift apart:

```
........        three one-pixel bars across the two-pixel groove,
..#.#.#.        drawn on the east-west run and rotated for the
..#.#.#.        north-south one
########  <- the existing shaft, rows 3 and 4
########
..#.#.#.
..#.#.#.
........
```

Four placement rules:

- **Straight cells only.** Infocom never places a cluster on a corner, and an
  eight-pixel elbow has no room for one.
- **Phased on `(x + y) % 3`**, in map cell coordinates rather than pixels, so a
  run keeps its rhythm across cell edges the way
  the dash's period does, and a three-to-four cell link between adjacent rooms
  receives one cluster, matching the drawn density.
- **Or isolated**: a straight baggage cell whose two run-neighbours do not
  themselves carry the bit takes a cluster whatever the phase says. A stub is
  two cells long and no fixed period can be certain of landing on it, so the
  phase rule alone would ship the chimney -- the passage this feature exists
  for -- unmarked. The clause is not a shorthand for the period check and
  cannot be folded into it.
- **A baggage run draws solid**, even where the graph says `MAYBE`. Infocom makes
  the baggage limit its own legend entry rather than a flavour of "requires
  problem solving", and draws Timber-to-Drafty solid despite its being a `CEXIT`.
  Dashing it as well would report one fact twice. Arrowheads keep priority on
  their own cell, which is also what the Altar route does.

## The scan audit

The second deliverable is the same tracer with a different seeding. Seeded at
hamburger clusters it yields the three passages above; seeded from every box edge
it yields the page's whole line network, which is then compared against the exit
graph. That comparison is written to `docs/ZORK1_MAP_SCAN_AUDIT.md` and **changes
nothing by itself** -- under the contribution rule, any disagreement outside the
all-`MAYBE` case is reported for a ruling, not applied.

The order is deliberate. The hamburger seeding runs first because its output can
be scored against the ZIL, so the tracer has a measured error rate before it
produces anything that ships. The network seeding has no such ground truth and
would otherwise be trusted on the strength of looking plausible.

## Testing

- **`tools/tests/test_trace_edges.py`** scores the tracer against a truth set
  parsed from `1dungeon.zil`: exactly the three passages. The `DEFLATE`
  absence assertion landed in `tools/tests/test_gen_map_marks.py` instead,
  which is where it belongs: `trace_edges` has no notion of an exit graph and
  cannot express "these three exits are absent" at all.
- **The acceptance property**, which `saturn/tests/test_exit_dests.py` established
  and which is the reason that suite is worth anything: disabling the hamburger
  detector must make the calibration test *fail*. A test that passes against its
  own removal is exactly what went wrong in the previous round.
- `saturn/tests/test_map_marks.c` over the emitted table, in the shape of
  `test_map_atlas.c`.
- `saturn/tests/test_map_edges.c` extended for the bit-to-tile choice, including
  that a baggage run picks the solid shaft and not the dashed one.
- `map_atlas_data.inc` regenerated after the refactor and required byte-identical,
  which is the whole guarantee that moving eight functions changed nothing.

## What this does not do

- **The other seventeen mapped games.** Zork I is the only story whose source is
  in this repository, and therefore the only one where a traced edge can be
  checked against something other than the exit graph it is meant to audit.
  Widening is a re-seeding of the same tools, not a rewrite.
- **The hatched wall and mirror symbols.** They are recognised only well enough to
  be rejected. They are not legend entries and nothing reads them.
- **Anything on hardware.** Nothing from the previous round has been seen on a
  screen. This would be the fifth unverified mark and the first whose ink is three
  one-pixel bars on a parchment ground, which makes it the likeliest of the five
  to be illegible on a real television. The four existing marks should be looked
  at before this one is drawn, not after.
