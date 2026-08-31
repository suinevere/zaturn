---
name: map-atlas-handoff
description: The in-game map's layout rule replaced by an authored table measured off Infocom's InvisiClues maps and validated against the story's exit graph, plus the four rendering faults found by actually running it; four commits on input-dashboard, the reveal switch still on and the netbin's size delta still unmeasured.
metadata:
  type: project
---

Branch `input-dashboard`, four commits past `77a5f32`: `8135fc3`, `b5ca960`, `bb40084`, `0ae045a`.
**Nothing pushed.** The commit messages carry the what. This carries what they cannot.

Supersedes the layout half of [[ingame-map-handoff]] — see "What that handoff now gets wrong"
at the end.

## The one thing to do before shipping

`MAP_REVEAL_ALL` in `saturn/src/video/map_view.cxx` is **set to 1**. It places the whole authored
table on open so placements can be checked against the drawing without walking to every room. The
owner asked for it explicitly and said "we'll turn it off later". Later has not happened. Clearing
that one define restores the explored-only map.

## Two hazards a fresh session will otherwise trip on

**`zork1.pdf` is untracked.** It is the only input to `tools/gen_map_atlas.py` and the sole source
of the atlas's geometry. It is 3.4 MB of scanned InvisiClues maps sitting in the repo root,
unstaged. Regenerating `map_atlas_data.inc` without it is impossible. Track it, or accept that the
generated `.inc` is now the primary artifact and the generator is unrunnable.

**The story is release 88, serial 840726** (`saturn/cd/data/Z3/ZORK1.Z3`). The Japanese disc's
reference source under `cd/` is release **119**, serial 880429 — a different build. Object numbers
are assigned by the compiler, so an atlas keyed on the title alone would place rooms plausibly and
wrongly. `map_atlas_bind` matches release *and* serial and `test_map_atlas` asserts release 119 is
refused. Do not "fix" that by loosening the match.

## What the owner has actually seen on screen

The first real runs of this feature happened this session. Three observations, all acted on:

1. Rooms in correct positions, **no connecting lines at all** — the renderer only joined rooms one
   grid step apart, which was true under the graph walk and false the moment the atlas started
   placing rooms where Infocom drew them. 33 of 59 links were silently dropped.
2. Lines drawn, but **rooms appearing connected to rooms they are not** — a single fixed route ran
   14 links straight through an unrelated room. Skipping the paint on the mark does not help; the
   line still arrives and leaves.
3. Requested D-pad scrolling that recentres on the player each open, and a whole-map reveal.

All three are in `bb40084` and `0ae045a`. Everything past that point is still unseen.

## The art question, answered with measurements

`docs/superpowers/specs/2026-08-30-ingame-map-design.md` still lists "Art provenance" as open
decision 1. It can be closed against these findings, which exist nowhere else:

- **There is no VDP1 path in this engine.** `grep -rn 'sprite\|VDP1' saturn/src/` returns nothing.
  Everything is VDP2 scroll layers. The original composites its map from eight VDP1 sprite draws.
- **The original's trail is a VDP1 polyline**, per the command-list parse already in
  `docs/ZORK1_MAP_RECON.md`. A tile layer has no line renderer, which is why ours is an orthogonal
  route rather than the original's wandering squiggle.
- **The canvas pixels are gone.** `analysis/zork_ui/cels/cel28_160x255.png` is noise — stale pool
  data, byte-identical across all four map-open savestates. By contrast `cel29`'s round silhouette
  renders correctly, which proves the cel table's char addresses are right. What is missing from
  the figure and trail is only colour: `rip_cels` in `analysis/zork_ui_rip.py` writes `index*17`
  greyscale and never resolves CRAM.
- **Sixteen colours, shared with the whole dashboard.** The map's tan is not a palette; it is
  `dash_tint(0x2B5E)` bending the marble ramp's hue at runtime and putting it back on exit. Parchment,
  ink and figure cannot be independent colours.
- **The shapes do not fit an 8x8 grid.** The trail stamp is 16x15 — an odd row count, never
  tile-alignable. The compass ring is 88x88, which is 121 tiles against 83 in the entire set.

Reference screenshot of the original map screen: `analysis/zork_ui/_map_screen_ref.png`.

## Reconnaissance that will otherwise be repeated

**Recovering the direction properties from a Z3 dictionary: scan from byte +5, not +4.**
`dir_of_prop`'s rule is the unique data byte in 1..31, and the flags byte at +4 is frequently in
that range — including it counts two candidates and discards the whole entry. Reading from +4
recovered exactly one of twelve directions and looked like a decode failure. `room_model.c`'s
`dir_prop_of` has always been right about this; the Python reimplementation was not. The corrected
version lives in `gen_map_atlas.py:direction_props` and recovers all twelve (properties 20-31).

**Ambiguous room names are resolved by structure, not by name.** Nine short names in Zork I are
shared — four Forests, two Clearings, fifteen Mazes, five Dead Ends. Infocom's parenthetical
numbering is the *map's* disambiguation and does not exist in the game. `CHECKS` in the generator
re-derives each ambiguous choice from the exit graph so a wrong label fails the run. One of those
assertions caught a real mistake mid-session.

**Only pages 3 and 5 of the PDF were examined in depth; only page 3 is in the atlas.** Box
detection already works on all three: page 3 found 23 boxes (21 real, 2 artifacts at the Forest (4)
loop-back), page 4 found 62, page 5 found 20. Page 4's only real misses are Maintenance Room (a
compound shape overlapping Dam Lobby) and Dam (a parallelogram); Frigid River (1)-(5) are bare
labels with no boxes at all. The remaining work on page 4 is labelling, which is the same
name-to-object join the CHECKS mechanism already handles.

**Mazes are deliberately excluded** and fall through to the graph walk. Infocom drew the fifteen
maze rooms in an arbitrary planar embedding; there is no geography to be faithful to. That was the
owner's call and it is documented in `map_atlas.h`.

## What no gate could prove

The owner runs all builds. Gates used: host `gcc` over the SRL-free halves, and
`sh saturn/syntax-check.sh` (which is `-fsyntax-only`) over the rest.

- **The netbin's size gate.** `dash_tiles.c` is on the netbin's source list and the tile set went
  69 to 83, which is **448 bytes** added to that build. The netbin has a hard size gate and the
  dashboard design was reversed once already for guessing at exactly this. If it now fails, the
  five never-painted masks (0 and the four single-side stubs) can come out for 160 bytes at the
  cost of a lookup table.
- **How any of it looks on a CRT.** The routing and mask arithmetic are checked; whether tan
  grooves on tan ground read at four cells to the room is a judgement only the screen makes.
- **One-frame timing on scroll.** `draw_once` now runs again on every scroll step rather than once
  per open. It was measured as safe by reasoning (the O(n^2) scan is bounded by the viewport, and
  the pathological version the earlier handoff warned about was the un-hoisted one), not by a
  stopwatch.

Two host checks worth knowing exist, because they replaced hardware for defects that would
otherwise only show on screen: the line walker was run over all 6561 offsets from -40 to +40 to
prove it terminates, never paints an endpoint, and stays contiguous; and a Python mirror of
`map_view.cxx`'s routing rendered the actual screen as box-drawing characters and counted false
joins. **That simulator was written in the session scratchpad and is gone.** Rebuilding it in
`tools/` is the single highest-value thing available to the next session — it is the only way to
see a map change without a build, and it is what caught the false-join class.

## Practice that caught real weakness

Every new assertion this session was run against the *old* code and required to fail. Three of the
first drafts did not: `ny < wy` ("North of House is north of West of House") passes under the graph
walk too and pins nothing — it was replaced with the exact drawn offset. The ring-search fixture
initially probed a cell the old ray search also reached, so it had to be tightened until it named
the one cell only a ring finds. Assume a new assertion is vacuous until it has been shown to fail.

## Pre-existing breakage, not from this work

`saturn/tests/test_scene_map.c` does not compile: it passes two arguments to `scene_track_mask`,
which takes one. Nothing under `saturn/src/scene/` was touched this session.

## Build and test commands

```
gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/engine -o /tmp/tmm \
    saturn/tests/test_map_model.c saturn/src/engine/map_model.c saturn/src/engine/map_atlas.c
gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/engine -o /tmp/tma \
    saturn/tests/test_map_atlas.c saturn/src/engine/map_atlas.c
gcc -O2 -Wall -I saturn/src -o /tmp/tdt saturn/tests/test_dash_tiles.c saturn/src/video/dash_tiles.c
gcc -O2 -Wall -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c saturn/src/video/dash_map.c
python saturn/tests/test_netbin_sources.py
cd saturn && sh syntax-check.sh src/video/map_view.cxx src/engine/map_model.c src/engine/map_atlas.c

python tools/gen_dash_tiles.py > saturn/src/video/dash_tiles.c
python tools/gen_map_atlas.py zork1.pdf > saturn/src/engine/map_atlas_data.inc
```

`gen_map_atlas.py` needs pymupdf, opencv-python and numpy, and prints its validation result to
stderr (currently `20 rooms, 49/51 exits agree with the drawing`). Both generators are run by hand,
not by the Saturn build.

## What that handoff now gets wrong

[[ingame-map-handoff]] is **PARTLY STALE**. Its account of the reconnaissance, the four criticals
the whole-branch review found, and the lost above-ground savestate walk all still stand. These do
not:

- "assigns each room a position on first entry" is now the *fallback*, not the rule. The atlas is
  the default wherever it covers a room.
- "Diagonal links get a midpoint mark rather than their own glyph" — false. There are sixteen link
  tiles indexed by connection mask, and bends draw as elbows.
- "The first move after a restore infers no direction and places its destination due south" — now
  read backwards out of the room arrived in, and only falls back to due south when that room has no
  way back.
- Its claim that a room's placement is permanent still holds, but the reason it mattered (that a
  graph walk cannot make west mean west) is now routed around rather than lived with.

## Suggested skills

- **superpowers:verification-before-completion** — the governing risk here. Nothing on this branch
  has been compiled for the SH-2 or linked, and three of this session's defects were invisible to
  every host gate. Do not report the map as working on the strength of tests passing.
- **superpowers:systematic-debugging** — the next defect will arrive as an on-screen observation
  ("lines look wrong") with no stack trace, which is exactly the shape the last three had.
- **superpowers:test-driven-development** — specifically its discipline of watching a test fail
  first. See "Practice that caught real weakness" above; three drafts this session were vacuous.
- **code-review** — before any push. Four commits of cross-cutting change to a feature whose
  previous whole-branch review found four criticals that no individual task review could see.
