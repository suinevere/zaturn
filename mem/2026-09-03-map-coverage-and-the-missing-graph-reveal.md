---
name: 2026-09-03-map-coverage-and-the-missing-graph-reveal
description: "Why thirteen of the disc's stories -- Starcross among them -- show an empty map on Easy: the reveal reads the authored atlas and nothing else, and the graph walk everyone thinks of as the backup only ever places the room the player is standing in. Measured atlas coverage per story, and then a spike that measured what a synthesised table would actually be worth: 26 of 31 stories clear the generator's own drop rule, and the merge that anchors a walk on the scan's cells reaches 100% coverage for two points of half-plane agreement."
metadata:
  type: reference
---

Started as an owner's question; became a measured spike. **Nothing here was
shipped** -- the probe scripts are throwaway and live in the session scratchpad,
not the tree. Companion to [[2026-09-03-map-inset-to-parchment-handoff]] and
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

## Recommendation

Two stages, smallest first, because the second contains the open problem.

**Stage one -- synthesise whole tables for the tableless stories.** No merge, so
no paging question: their floors come from `storeys()` and are already sane.
Gate on the existing `PASS_RATE`, mark the tables as derived rather than
measured so nobody reads them as scan data and so the axis baselines are not
polluted, and ship the ten that clear it. Adventure, Deadline and Hypochondriac
stay on the walked fallback exactly as today.

**Stage two -- anchored fill for the scanned games.** Worth doing for the 431
rooms the scans missed, but only after the paging rule is designed. Not a
footnote to stage one.
