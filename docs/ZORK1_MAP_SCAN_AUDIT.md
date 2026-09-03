# ZORK1 map scan audit

This report changes nothing. No file it describes was edited to match it, no
mark was added or withdrawn because of it, and nothing in
`saturn/src/engine/map_marks_data.inc` is derived from it. It is a listing of
where the lines Infocom drew and the exits the compiled story carries do not
say the same thing, produced so that a person can look at them.

A disagreement here is a question for the owner, not a defect to be
auto-corrected. The left-hand side of every row is a tracer reading scanned
print, and the right-hand side is the exit graph, which is exact. When the two
differ the likelier explanation is that the scan is wrong, and the last section
of this report gives the measured rate at which it is.

Only the all-`MAYBE` rows are eligible for resolution at all. The rule the
shipped generator enforces is that the scan may resolve a passage only when
every exit on it is `RM_EXIT_MAYBE` -- conditional, with the story recording
that a condition exists and never what it tests. If any exit on the pair is
`OPEN` (a plain destination) or `BLOCKED` (a refusal message), the graph has
asserted something, the graph wins outright, and the disagreement is reported
rather than applied. Every row below cites the graph's own exit states so that
the rule can be applied to it by hand, and one column says what applying it
gives.

ZORK1, release 88 serial 840726, against pages 3, 4, 5 of the Infocom Documentation Project's scan -- which is not redistributed with this repository and is read out of a local cache.

## How this was read

    python tools/gen_map_marks.py --cache tools/assets/cache --only ZORK1 \
        --audit > docs/ZORK1_MAP_SCAN_AUDIT.md

The generator's ordinary path seeds `trace_edges.follow` at the cross-bar
clusters `trace_edges.hamburger_seeds` detects, which finds only the passages
carrying a narrow-passageway mark. `--audit` seeds the same tracer at every
box's four edge midpoints instead, so that it reads the page's whole line
network, and deduplicates the runs by their resolved endpoint pair. A pair is
drawn here when one run or more resolved a room at both of its ends: a box end
is a box the walk terminated in, and an open end is a labelled stub whose
parenthetical caption named a room, which is how Infocom draws a passage whose
far end is on another page.

The four buckets are:

- **agree** -- the graph asserts an exit between the two rooms and the drawn
  direction is consistent with it.
- **drawn only** -- a run joins the two rooms and the graph asserts no exit
  between them in either direction.
- **graph only** -- the graph asserts an exit between two rooms both of which
  were drawn on a traced page, and no run joined them.
- **direction differs** -- both join them and the drawn arrowhead is not
  consistent with the graph's directions, either because the graph gives no
  exit in the direction the head points or because it gives one in the
  direction the head denies.

An arrowhead is believed only where every trace of a passage found one at the
same end, which is `marks_for`'s policy and is held to for its reason:
`trace_edges.arrow_end` misses a real head about as often as a second trace of
the same wedge finds it. A split reading is reported as split rather than
resolved either way.

## What the tracer actually read

| page | boxes named | seeds | runs ending in the box they left | runs whose far end read as no room | runs resolving two rooms | distinct pairs |
|---|---|---|---|---|---|---|
| 3 | 20 | 80 | 20 | 36 | 24 | 11 |
| 4 | 61 | 244 | 73 | 89 | 82 | 47 |
| 5 | 19 | 76 | 48 | 20 | 8 | 3 |

60 distinct room names appear at one end or other of a traced run, against the 84 rooms `gen_map_atlas` places for this story, the 111 room objects the compiled story carries, and the 79 distinct names those objects go by. The shortfall is this seeding's recall and not Infocom drawing fewer rooms: an edge midpoint only finds a line that leaves a box through the middle of one of its four sides, and plenty of lines on these pages leave nearer a corner.

## Bucket counts

| bucket | pairs |
|---|---|
| agree | 51 |
| drawn only | 4 |
| graph only | 24 |
| direction differs | 0 |
| drawn, not keyable to one object pair | 6 |

The direction-differs count is zero, and that is a check that did not run rather than a check that passed. Every drawn pair here either carried no arrowhead at all or carried one its traces could not agree on, so nothing was ever compared against the graph's own directions. `trace_edges.arrow_end`'s own docstring records that its two shape constants have no measured margin left in either direction and that it misses a real head about as often as a second trace of the same wedge finds one; a zero here is that silence, not evidence that Infocom drew no one-way passage this graph disagrees with.

## Agree

| pair | page | drawn direction | graph exit states | all-MAYBE, so resolvable | reading |
|---|---|---|---|---|---|
| Altar (212) -- Cave (46) **[ships a mark]** | 4 | one way into Cave (46), on all of 1 trace | Altar (212): down MAYBE(46); Cave (46): none | yes | the graph runs one way into Cave (46) and so does the drawing |
| Temple (220) -- Altar (212) | 4 | no arrowhead, on 2 traces | Temple (220): south OPEN(212); Altar (212): north OPEN(220) | no | the graph joins them and the drawing marks no direction |
| Cave (47) -- Atlantis Room (187) | 4 | no arrowhead, on 2 traces | Cave (47): south OPEN(187), down OPEN(187); Atlantis Room (187): up OPEN(47) | no | the graph joins them and the drawing marks no direction |
| Atlantis Room (187) -- Reservoir North (172) | 4 | no arrowhead, on 2 traces | Atlantis Room (187): south OPEN(172); Reservoir North (172): north OPEN(187) | no | the graph joins them and the drawing marks no direction |
| Attic (201) -- Kitchen (203) | 3 | no arrowhead, on 2 traces | Attic (201): down OPEN(203); Kitchen (203): up OPEN(201) | no | the graph joins them and the drawing marks no direction |
| Bat Room (222) -- Shaft Room (226) | 4 | no arrowhead, on 1 trace | Bat Room (222): east OPEN(226); Shaft Room (226): west OPEN(222), down BLOCKED | no | the graph joins them and the drawing marks no direction |
| Bat Room (222) -- Squeaky Room (23) | 4 | no arrowhead, on 2 traces | Bat Room (222): south OPEN(23); Squeaky Room (23): north OPEN(222) | no | the graph joins them and the drawing marks no direction |
| Behind House (79) -- Clearing (74) | 3 | no arrowhead, on 2 traces | Behind House (79): east OPEN(74); Clearing (74): west OPEN(79), up BLOCKED | no | the graph joins them and the drawing marks no direction |
| Kitchen (203) -- Behind House (79) | 3 | no arrowhead, on 1 trace | Kitchen (203): east MAYBE(79), out MAYBE(79); Behind House (79): west MAYBE(203), in MAYBE(203) | yes | the graph joins them and the drawing marks no direction |
| Behind House (79) -- South of House (80) | 3 | no arrowhead, on 2 traces | Behind House (79): south OPEN(80), sw OPEN(80); South of House (80): north BLOCKED, east OPEN(79), ne OPEN(79) | no | the graph joins them and the drawing marks no direction |
| Twisting Passage (42) -- Cave (47) | 4 | no arrowhead, on 2 traces | Twisting Passage (42): east OPEN(47); Cave (47): west OPEN(42) | no | the graph joins them and the drawing marks no direction |
| Winding Passage (43) -- Cave (46) | 4 | no arrowhead, on 2 traces | Winding Passage (43): east OPEN(46); Cave (46): west OPEN(43) | no | the graph joins them and the drawing marks no direction |
| Cellar (72) -- East of Chasm (71) | 4 | no arrowhead, on 2 traces | Cellar (72): west BLOCKED, south OPEN(71); East of Chasm (71): north OPEN(72), down BLOCKED | no | the graph joins them and the drawing marks no direction |
| Slide Room (15) -- Cellar (72) | 4 | no arrowhead, on 1 trace | Slide Room (15): down OPEN(72); Cellar (72): west BLOCKED | no | the graph joins them and the drawing marks no direction |
| Chasm (37) -- North-South Passage (38) | 4 | no arrowhead, on 2 traces | Chasm (37): south OPEN(38), down BLOCKED; North-South Passage (38): north OPEN(37) | no | the graph joins them and the drawing marks no direction |
| Clearing (143) -- Forest Path (75) | 3 | no arrowhead, on 2 traces | Clearing (143): north BLOCKED, south OPEN(75), down MAYBE; Forest Path (75): north OPEN(143) | no | the graph joins them and the drawing marks no direction |
| Grating Room (57) -- Clearing (143) | 4 | no arrowhead, on 1 trace | Grating Room (57): up MAYBE(143); Clearing (143): north BLOCKED, down MAYBE | no | the graph joins them and the drawing marks no direction |
| Gas Room (124) -- Coal Mine (19) | 4 | no arrowhead, on 2 traces | Gas Room (124): east OPEN(19); Coal Mine (19): north OPEN(124) | no | the graph joins them and the drawing marks no direction |
| Coal Mine (16) -- Ladder Top (21) | 4 | no arrowhead, on 2 traces | Coal Mine (16): down OPEN(21); Ladder Top (21): up OPEN(16) | no | the graph joins them and the drawing marks no direction |
| Cold Passage (45) -- Mirror Room (150) | 4 | no arrowhead, on 2 traces | Cold Passage (45): south OPEN(150); Mirror Room (150): north OPEN(45) | no | the graph joins them and the drawing marks no direction |
| Slide Room (15) -- Cold Passage (45) | 4 | no arrowhead, on 2 traces | Slide Room (15): east OPEN(45); Cold Passage (45): west OPEN(15) | no | the graph joins them and the drawing marks no direction |
| Cyclops Room (185) -- Strange Passage (51) | 4 | no arrowhead, on 2 traces | Cyclops Room (185): east MAYBE(51); Strange Passage (51): west OPEN(185), in OPEN(185) | no | the graph joins them and the drawing marks no direction |
| Cyclops Room (185) -- Treasure Room (190) | 4 | no arrowhead, on 2 traces | Cyclops Room (185): up MAYBE(190); Treasure Room (190): down OPEN(185) | no | the graph joins them and the drawing marks no direction |
| Damp Cave (39) -- Loud Room (138) | 4 | no arrowhead, on 1 trace | Damp Cave (39): west OPEN(138), south BLOCKED; Loud Room (138): east OPEN(39) | no | the graph joins them and the drawing marks no direction |
| Damp Cave (39) -- White Cliffs Beach (33) | 4 | no arrowhead, on 2 traces | Damp Cave (39): east OPEN(33), south BLOCKED; White Cliffs Beach (33): west MAYBE(39) | no | the graph joins them and the drawing marks no direction |
| Ladder Bottom (20) -- Dead End (118) | 4 | no arrowhead, on 2 traces | Ladder Bottom (20): south OPEN(118); Dead End (118): north OPEN(20) | no | the graph joins them and the drawing marks no direction |
| Drafty Room (228) -- Machine Room (157) | 4 | no arrowhead, on 2 traces | Drafty Room (228): south OPEN(157); Machine Room (157): north OPEN(228) | no | the graph joins them and the drawing marks no direction |
| Drafty Room (228) -- Timber Room (206) **[ships a mark]** | 4 | no arrowhead, on 2 traces | Drafty Room (228): east MAYBE(206), out MAYBE(206); Timber Room (206): west MAYBE(228) | yes | the graph joins them and the drawing marks no direction |
| East of Chasm (71) -- Gallery (148) | 4 | no arrowhead, on 2 traces | East of Chasm (71): east OPEN(148), down BLOCKED; Gallery (148): west OPEN(71) | no | the graph joins them and the drawing marks no direction |
| Temple (220) -- Egyptian Room (175) | 4 | no arrowhead, on 2 traces | Temple (220): east OPEN(175), down OPEN(175); Egyptian Room (175): west OPEN(220), up OPEN(220) | no | the graph joins them and the drawing marks no direction |
| Forest Path (75) -- North of House (81) | 3 | no arrowhead, on 2 traces | Forest Path (75): south OPEN(81); North of House (81): north OPEN(75), south BLOCKED | no | the graph joins them and the drawing marks no direction |
| Studio (94) -- Gallery (148) | 4 | no arrowhead, on 2 traces | Studio (94): south OPEN(148), up MAYBE; Gallery (148): north OPEN(94) | no | the graph joins them and the drawing marks no direction |
| Gas Room (124) -- Smelly Room (22) | 4 | no arrowhead, on 2 traces | Gas Room (124): up OPEN(22); Smelly Room (22): down OPEN(124) | no | the graph joins them and the drawing marks no direction |
| Maze (58) -- Grating Room (57) | 5 | no arrowhead, on 1 trace | Maze (58): ne OPEN(57); Grating Room (57): sw OPEN(58) | no | the graph joins them and the drawing marks no direction |
| Living Room (193) -- Kitchen (203) | 3 | no arrowhead, on 2 traces | Living Room (193): east OPEN(203), down MAYBE; Kitchen (203): west OPEN(193) | no | the graph joins them and the drawing marks no direction |
| Studio (94) -- Kitchen (203) **[ships a mark]** | 4 | no arrowhead, on 1 trace | Studio (94): up MAYBE; Kitchen (203): down MAYBE(94) | yes | the graph joins them and the drawing marks no direction |
| Ladder Top (21) -- Ladder Bottom (20) | 4 | no arrowhead, on 1 trace | Ladder Top (21): down OPEN(20); Ladder Bottom (20): up OPEN(21) | no | the graph joins them and the drawing marks no direction |
| Timber Room (206) -- Ladder Bottom (20) | 4 | no arrowhead, on 2 traces | Timber Room (206): east OPEN(20); Ladder Bottom (20): west OPEN(206) | no | the graph joins them and the drawing marks no direction |
| Maze (70) -- The Troll Room (102) | 5 | no arrowhead, on 1 trace | Maze (70): east OPEN(102); The Troll Room (102): west MAYBE(70) | no | the graph joins them and the drawing marks no direction |
| Mine Entrance (24) -- Slide Room (15) | 4 | no arrowhead, on 2 traces | Mine Entrance (24): south OPEN(15); Slide Room (15): north OPEN(24) | no | the graph joins them and the drawing marks no direction |
| Mine Entrance (24) -- Squeaky Room (23) | 4 | no arrowhead, on 2 traces | Mine Entrance (24): west OPEN(23), in OPEN(23); Squeaky Room (23): east OPEN(24) | no | the graph joins them and the drawing marks no direction |
| Narrow Passage (44) -- Mirror Room (152) | 4 | no arrowhead, on 2 traces | Narrow Passage (44): south OPEN(152); Mirror Room (152): north OPEN(44) | no | the graph joins them and the drawing marks no direction |
| Mirror Room (152) -- Winding Passage (43) | 4 | no arrowhead, on 1 trace | Mirror Room (152): west OPEN(43); Winding Passage (43): north OPEN(152) | no | the graph joins them and the drawing marks no direction |
| Reservoir North (172) -- Reservoir (100) | 4 | no arrowhead, on 2 traces | Reservoir North (172): south MAYBE(100); Reservoir (100): north OPEN(172), down BLOCKED | no | the graph joins them and the drawing marks no direction |
| Reservoir (100) -- Reservoir South (50) | 4 | no arrowhead, on 2 traces | Reservoir (100): south OPEN(50), down BLOCKED; Reservoir South (50): north MAYBE(100) | no | the graph joins them and the drawing marks no direction |
| Stream (48) -- Reservoir (100) | 4 | no arrowhead, on 2 traces | Stream (48): east OPEN(100), west BLOCKED, up BLOCKED, down OPEN(100); Reservoir (100): west OPEN(48), up OPEN(48), down BLOCKED | no | the graph joins them and the drawing marks no direction |
| Stream View (49) -- Reservoir South (50) | 4 | no arrowhead, on 2 traces | Stream View (49): east OPEN(50), west BLOCKED; Reservoir South (50): west OPEN(49) | no | the graph joins them and the drawing marks no direction |
| Sandy Beach (120) -- Shore (30) | 4 | no arrowhead, on 2 traces | Sandy Beach (120): south OPEN(30); Shore (30): north OPEN(120) | no | the graph joins them and the drawing marks no direction |
| Smelly Room (22) -- Shaft Room (226) | 4 | no arrowhead, on 2 traces | Smelly Room (22): south OPEN(226); Shaft Room (226): north OPEN(22), down BLOCKED | no | the graph joins them and the drawing marks no direction |
| West of House (180) -- South of House (80) | 3 | no arrowhead, on 2 traces | West of House (180): east BLOCKED, south OPEN(80), se OPEN(80); South of House (80): north BLOCKED, west OPEN(180), nw OPEN(180) | no | the graph joins them and the drawing marks no direction |
| Torch Room (105) -- Temple (220) | 4 | no arrowhead, on 2 traces | Torch Room (105): south OPEN(220), up BLOCKED, down OPEN(220); Temple (220): north OPEN(105), up OPEN(105), out OPEN(105) | no | the graph joins them and the drawing marks no direction |

## Drawn only

| pair | page | drawn direction | graph exit states | all-MAYBE, so resolvable | reading |
|---|---|---|---|---|---|
| South of House (80) -- Cellar (72) | 3 | no arrowhead, on 1 trace | South of House (80): north BLOCKED; Cellar (72): west BLOCKED | no | the graph asserts no exit between these rooms |
| Strange Passage (51) -- Cellar (72) | 4 | no arrowhead, on 1 trace | Strange Passage (51): none; Cellar (72): west BLOCKED | no | the graph asserts no exit between these rooms |
| East-West Passage (41) -- Engravings Cave (96) | 4 | no arrowhead, on 1 trace | East-West Passage (41): none; Engravings Cave (96): none | no | the graph asserts no exit between these rooms |
| Stream (48) -- Stream View (49) | 4 | no arrowhead, on 2 traces | Stream (48): west BLOCKED, up BLOCKED; Stream View (49): west BLOCKED | no | the graph asserts no exit between these rooms |

## Direction differs

None.

Empty because no drawn pair carried an arrowhead every one of its traces agreed on, so no drawn direction was ever put to the graph. Read this section as unexercised, not as clean.

## Graph only

Pairs the graph asserts between two rooms both drawn on a traced page, which no run joined. Nothing is drawn here for the rule to resolve, so the last column says only whether the rule would permit the scan to speak about the pair at all -- and the only thing it could say against a pair the drawing does not show is a retraction.

| pair | graph exit states | all-MAYBE |
|---|---|---|
| Aragain Falls (29) -- Shore (30) | Aragain Falls (29): north OPEN(30), down BLOCKED; Shore (30): south OPEN(29) | no |
| Behind House (79) -- North of House (81) | Behind House (79): north OPEN(81), nw OPEN(81); North of House (81): east OPEN(79), south BLOCKED, se OPEN(79) | no |
| Canyon Bottom (27) -- End of Rainbow (136) | Canyon Bottom (27): north OPEN(136); End of Rainbow (136): sw OPEN(27) | no |
| Canyon View (25) -- Clearing (74) | Canyon View (25): south BLOCKED, nw OPEN(74); Clearing (74): east OPEN(25), up BLOCKED | no |
| Canyon View (25) -- Forest (76) | Canyon View (25): west OPEN(76), south BLOCKED; Forest (76): east BLOCKED, south BLOCKED, up BLOCKED | no |
| Canyon View (25) -- Rocky Ledge (26) | Canyon View (25): east OPEN(26), south BLOCKED, down OPEN(26); Rocky Ledge (26): up OPEN(25) | no |
| Cellar (72) -- Living Room (193) | Cellar (72): west BLOCKED, up MAYBE(193); Living Room (193): down MAYBE | no |
| Chasm (37) -- East-West Passage (41) | Chasm (37): sw OPEN(41), up OPEN(41), down BLOCKED; East-West Passage (41): north OPEN(37), down OPEN(37) | no |
| Chasm (37) -- Reservoir South (50) | Chasm (37): ne OPEN(50), down BLOCKED; Reservoir South (50): sw OPEN(37) | no |
| Deep Canyon (40) -- Loud Room (138) | Deep Canyon (40): down OPEN(138); Loud Room (138): up OPEN(40) | no |
| Deep Canyon (40) -- Reservoir South (50) | Deep Canyon (40): nw OPEN(50); Reservoir South (50): se OPEN(40) | no |
| Forest (76) -- South of House (80) | Forest (76): east BLOCKED, south BLOCKED, nw OPEN(80), up BLOCKED; South of House (80): north BLOCKED, south OPEN(76) | no |
| Forest (78) -- West of House (180) | Forest (78): west BLOCKED, up BLOCKED; West of House (180): east BLOCKED, west OPEN(78) | no |
| Forest Path (75) -- Up a Tree (88) | Forest Path (75): up OPEN(88); Up a Tree (88): up BLOCKED, down OPEN(75) | no |
| Maze (52) -- Cyclops Room (185) | Maze (52): se OPEN(185); Cyclops Room (185): nw OPEN(52) | no |
| North of House (81) -- West of House (180) | North of House (81): west OPEN(180), south BLOCKED, sw OPEN(180); West of House (180): north OPEN(81), east BLOCKED, ne OPEN(81) | no |
| North-South Passage (38) -- Deep Canyon (40) | North-South Passage (38): ne OPEN(40); Deep Canyon (40): sw OPEN(38) | no |
| On the Rainbow (28) -- Aragain Falls (29) | On the Rainbow (28): east OPEN(29); Aragain Falls (29): west MAYBE(28), up MAYBE(28), down BLOCKED | no |
| On the Rainbow (28) -- End of Rainbow (136) | On the Rainbow (28): west OPEN(136); End of Rainbow (136): east MAYBE(28), ne MAYBE(28), up MAYBE(28) | no |
| Rocky Ledge (26) -- Canyon Bottom (27) | Rocky Ledge (26): down OPEN(27); Canyon Bottom (27): up OPEN(26) | no |
| Sandy Beach (120) -- Sandy Cave (126) | Sandy Beach (120): ne OPEN(126); Sandy Cave (126): sw OPEN(120) | no |
| Stone Barrow (178) -- West of House (180) | Stone Barrow (178): ne OPEN(180); West of House (180): east BLOCKED, sw MAYBE(178), in MAYBE(178) | no |
| Strange Passage (51) -- Living Room (193) | Strange Passage (51): east OPEN(193); Living Room (193): west MAYBE(51), down MAYBE | no |
| Twisting Passage (42) -- Mirror Room (150) | Twisting Passage (42): north OPEN(150); Mirror Room (150): west OPEN(42) | no |

3 of these 24 pairs are joined by an exit in one direction only. That number is worth stating because an earlier version of this enumeration deduplicated pairs by requiring an exit's destination to outrank its source by object number, which drops a one-way exit drawn from the higher-numbered room from both loop iterations at once and reported it nowhere at all. The bucket is now collected as unordered pairs and only then asked which way round the graph asserts it.

## Drawn, but not keyable to one object pair

Runs whose two endpoint names do not settle on a single pair of objects, because Zork I gives several rooms the same short name -- fifteen of them are called Maze. These are readings the audit cannot bucket, not readings it rejects.

| names | page | traces | why |
|---|---|---|---|
| land of the dead -- cave | 4 | 1 | 1 and 2 objects carry these two names |
| mirror room -- cave | 4 | 2 | 2 and 2 objects carry these two names |
| mirror room -- cellar | 4 | 1 | 2 and 1 objects carry these two names |
| clearing -- forest | 3 | 4 | 2 and 4 objects carry these two names |
| maze -- dead end | 5 | 6 | 15 and 5 objects carry these two names |
| forest -- forest path | 3 | 4 | 4 and 1 objects carry these two names |

## The passages that ship

`saturn/src/engine/map_marks_data.inc` carries 6 marks for this story, from 3 passages the cross-bar seeding resolved. Whether this report's independent edge seeding found each of them again is a check on the seeding, not on the marks; the rows are tagged **[ships a mark]** in the buckets above.

| passage | bucket it appears in |
|---|---|
| Altar (212) -- Cave (46) **[ships a mark]** | agree |
| Studio (94) -- Kitchen (203) **[ships a mark]** | agree |
| Drafty Room (228) -- Timber Room (206) **[ships a mark]** | agree |

## What this audit is worth

The cross-bar seeding this repository already trusts yields about thirteen seeds on the underground page of which four or five resolve cleanly, and it was scored against Zork I's own ZIL source before anything shipped from it. The edge seeding here has no such oracle, so its own rate has to stand in its place: 400 seeds across 3 pages produced 114 runs that resolved a room at both ends, while 141 ended back in the box they left and 145 reached something that read as no room at all -- 36% of every seed laid down.

**Not one of the 4 drawn-only pairs is eligible under the all-MAYBE rule.** 3 are vetoed by an exit the story has already asserted, a plain destination or a refusal message; on the remaining 1 the graph offers no exit the drawing could be about in either direction, so there is nothing to annotate at all. Read across the whole drawn network, that is this report's single most useful finding: the contribution rule permits the scan to add nothing beyond the 3 passages already shipped. The edge seeding turned up no new passage the rule would let anyone act on, so there is no pending change here waiting to be approved -- only the rejected readings listed above, kept because a scan that dropped them silently would be indistinguishable from a scan that saw nothing.

The buckets are not equally strong and should not be read as though they were. A **drawn only** row is the tracer claiming a passage the story does not carry, which is either a real conditional passage or a misread line, and the all-MAYBE column is what separates the rows worth looking at from the ones the graph already forbids. A **graph only** row is much weaker evidence of anything, because this seeding is known not to find every line: that count is dominated by lines leaving a box away from an edge midpoint, by exits drawn across a page boundary, and by pairs whose names could not be keyed to objects. **Direction differs** rests entirely on `trace_edges.arrow_end`, whose two shape constants its own docstring records as having no measured margin left in either direction, so a row there is a prompt to look at the page rather than a finding -- and its count of 0 says how much of that check actually ran, not how much of it passed.
