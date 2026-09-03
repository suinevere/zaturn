---
name: 2026-09-03-map-coverage-and-the-missing-graph-reveal
description: "Why thirteen of the disc's stories -- Starcross among them -- show an empty map on Easy: the reveal reads the authored atlas and nothing else, and the graph walk everyone thinks of as the backup only ever places the room the player is standing in. Measured atlas coverage per story, and what a graph-walk reveal would actually cost to build."
metadata:
  type: reference
---

Answering an owner's question rather than recording a change: nothing in this
file was built. Companion to [[2026-09-03-map-inset-to-parchment-handoff]] and
[[2026-09-03-map-party-colours-handoff]].

## The two things that decide how much map you see

**The reveal is atlas-only.** `map_view_show` runs
`map_model_reveal_atlas()` on `DIFF_EASY` and `map_model_clear_reveal()`
otherwise, and that function's whole body is a loop over `map_atlas_count()`.
A story with no authored table has a count of zero, so Easy places nothing and
behaves exactly like Medium.

**The graph walk is a placement rule, not a reveal.** `map_model_enter` places
the room the player is standing in, one step from the room they came from, and
runs once per turn. There is no pass anywhere that walks the exit graph and lays
out rooms nobody has been to. That is the gap between what the fallback is and
what it sounds like it is.

## Measured coverage, per shipped story

Atlas cells against the room count read off each `.Z3` (an object with any
direction property). Reproduce with `tools/zexits.py` against
`saturn/src/engine/map_atlas_data.inc`.

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

**No table at all**, so nothing to reveal on any difficulty: ADVENT, DEADLINE,
HITCHHKR, HYPOCOND, INFOSAM5, INFOSAM7, MZORKI, MZORKI2, MZORKII, PLNTFALL,
SEASTLKR, **STARCROS**, SUSPECT. Thirteen stems.

Plundered Hearts is the second-best covered table on the disc. A map that looked
thin there is Medium difficulty or an unexplored game, not missing data.

## What a graph-walk reveal would take

The parts exist. `room_model_refresh_room(obj)` decodes any room's exits
straight out of the story image without the interpreter having been there --
`map_model_rebind_exits` already calls it for every placed room -- so a
breadth-first walk from the current room could place each unvisited room one
step from its parent and flag it `g_revealed`, which is exactly the shape of
`map_model_reveal_atlas` and would be undone by the same
`map_model_clear_reveal`.

What it would not give is a good map, and that is the reason to think before
building it:

- **Placement quality is the whole point of the atlas.** A graph walk assigns a
  cell from one exit's direction and has no view of the drawing; the authored
  tables exist because Infocom's own layouts resolve the contradictions a story's
  exit graph is full of. Zork I alone has cardinal exits that no planar layout
  can satisfy -- the generator lists them per game in the `.inc` header.
- **Cell contention gets much worse.** `place()` resolves a taken cell by
  searching outward for a free one, and the current fallback only ever does that
  for rooms the player walked into, a handful at a time. A whole-story walk on
  Starcross's 85 or Planetfall's 104 rooms would be hundreds of contested cells
  decided in object order, and a room shoved three cells sideways draws a passage
  that is not there.
- **The floor question has no answer without a table.** `map_atlas_pages` is what
  gives the map its floors, and a tableless story has one page. Every up and down
  exit in the story would land on that one page. `storeys()` in
  `tools/gen_map_atlas.py` derives floors from the level-exit graph and could be
  ported to the runtime, which is a second piece of work, not a footnote to the
  first.

So the honest options are: leave it (an unauthored story is explored, not
surveyed), author more tables from scans the way the eighteen were, or build the
walk and accept that it draws a topologically-correct map that does not look
like the room.
