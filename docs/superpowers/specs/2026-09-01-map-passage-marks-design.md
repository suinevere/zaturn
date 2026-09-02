# Design: passage marks on the map

**Date:** 2026-09-01
**Status:** Implemented on branch `map-passage-marks`, not yet seen on hardware
**Rests on:** `docs/superpowers/specs/2026-08-30-ingame-map-design.md` for the
map screen itself, and commit `cc429cd` for the floors the marks below are
drawn against. Read neither to follow this; read the first if the 32-pixel step
or the per-room position needs justifying.

## What this builds

Four marks the map does not currently draw, each answering a question a player
standing in a room can otherwise only answer by trying it:

| Mark | Says |
|---|---|
| Arrowhead | this passage goes one way; you cannot come back through it |
| `U` / `D` | this exit leaves the floor you are looking at |
| Dashed line | this passage is conditional, or it runs off the drawing |
| Loop circle | this exit puts you back where you started |

## Where the truth comes from

**From the story's own exit graph, at draw time. Nothing is scanned and nothing
is authored.** `tools/gen_map_atlas.py` is not touched by this work.

That is worth stating plainly because the obvious place to look for these marks
is the drawings the atlas is measured off -- Infocom's maps draw all four, and
the atlas pipeline is already reading those pages. It would be the wrong source.
Three of the four marks are *exact* in data the runtime already holds:

- one-way is `dest[a][d] == b` with no direction out of `b` leading back to `a`
- up and down are `d >= RM_UP`
- a self-loop is `dest[room][d] == room`

Against that, an arrowhead detected in a raster scan is inference from pixels,
and a false positive ships a one-way arrow on a passage the game's own data
says is two-way. There is no version of the scan that is more true than the
graph, so the scan earns nothing and risks something. The fourth mark, the
boundary dash, has no drawn equivalent to copy either -- it is derived below
from the floor the far end sits on.

The one cost of deriving is that these marks appear only for rooms the model has
exits for, which is every placed room: `record_exits` fills them on entry and
`map_model_rebind_exits` refills them after a restore. A revealed room under
Easy is covered too, since `map_model_reveal_atlas` re-derives exits as it
places.

## The four rules

| Mark | Rule |
|---|---|
| Arrow | `g_dest[a][d] == b`, and no `d'` where `g_dest[b][d'] == a` in any state but `RM_EXIT_NONE` |
| U / D | `d >= RM_UP` and `map_model_page(dest) != page`; the letter is `U` for even `d` and `D` for odd, which is `RM_UP`/`RM_IN` up and `RM_DOWN`/`RM_OUT` down |
| Dashed | `exits[a][d] == RM_EXIT_MAYBE`, or the run is an off-floor stub |
| Loop | `g_dest[room][d] == room` |

Two of these deserve their reasoning recorded, because both could defensibly
have gone the other way and a later reader will wonder.

**A blocked reverse exit is not one-way.** `RM_EXIT_BLOCKED` is a real passage
that happens to be shut -- a closed door, a grating not yet unlocked. Counting
it as one-way would make the arrowhead appear and vanish as the player opens and
closes things, and an arrow that flickers teaches nothing. Only
`RM_EXIT_NONE` in every direction back earns the arrow.

**Only `RM_EXIT_MAYBE` draws dashed,** for the same reason: a shut door is a
solid passage. The dash means "there is a condition on this", not "it is closed
right now".

The self-loop rule is the graph form of the behaviour originally asked for --
"previous room matches current room after move". They are the same fact; the
graph knows it one turn earlier, and knows it for exits the player has not
tried.

## What has to change first

`record_exits` (`map_model.c:326`) currently throws the conditional bit away:

```c
g_kind[room][d] = (unsigned char)
    (m->exits[d] == RM_EXIT_NONE ? MAP_LINK_NONE
     : (d >= RM_UP ? MAP_LINK_VERT : MAP_LINK_FLAT));
```

`RM_EXIT_BLOCKED`, `RM_EXIT_OPEN` and `RM_EXIT_MAYBE` all collapse into `FLAT`
or `VERT`. Nothing downstream can draw a dash until `MAYBE` survives this, so a
`unsigned short g_cond[MAP_ROOM_MAX]` bitmask is recorded beside `g_kind`, one
bit per direction. It is a short and not a char because `RM_DIR_N` is twelve.
512 bytes.

It stays out of the save blob. `MAP_BLOB_MAX` is unchanged and
`map_model_rebind_exits` refills `g_cond` along with everything else, for the
reason that header already gives: positions are worth six bytes a room in every
backup slot, derived exit state is not.

## Interface

The draw loop is replaced rather than extended. Today it is a pair loop:

```c
for (i = 0; i < n; i++)
    for (j = i + 1; j < n; j++)
        if (map_model_link(g_ids[i], g_ids[j]) != MAP_LINK_NONE) ...
```

Two of the four marks cannot be expressed in that shape at all. A stub and a
self-loop have no second room, so there is no pair for the loop to reach them
by. Bolting a third loop on beside it would leave two ways of asking the model
about exits, which is one too many.

```c
/* map_model.h */
#define MAP_EXIT_COND   1   /* RM_EXIT_MAYBE   -> dashed       */
#define MAP_EXIT_ONEWAY 2   /* no reverse exit -> arrowhead    */
#define MAP_EXIT_SELF   4   /* dest == room    -> loop circle  */

typedef struct {
    unsigned short dest;  /* the far room, or `room` itself for a self-loop */
    unsigned char  dir;   /* RM_* index, so the caller can say U or D       */
    unsigned char  kind;  /* MAP_LINK_FLAT or MAP_LINK_VERT                 */
    unsigned char  flags; /* MAP_EXIT_*                                     */
} MapExit;

int map_model_exits(unsigned short room, MapExit *out, int max);
```

`map_model_link` keeps its signature and becomes a thin read of the same table,
so its existing callers and tests are untouched.

The cost falls, but not by the 35x this section once claimed: that count was
the pair loop alone, against a per-room walk that also runs `map_model_link`
and (in the placement pass) a second `map_model_exits` and `has_reverse` scan.
`MAP_VIS_MAX` is 70, so the old pair loop ran 2415 pairs at twelve directions
scanned twice each: about 29,000 iterations per redraw. The shipped path costs
roughly 840 for the exit walk, plus `has_reverse`'s own twelve-direction scan
per exit (~2500), plus `map_model_link` for each link's `kind` (~350), plus the
placement pass's second `map_model_exits` and the `has_reverse` inside it again
(~840 + ~2500) -- about 7,000 iterations per redraw, roughly a fourfold cut on
the exact loop whose earlier form, by `map_view.cxx`'s own record, "spent about
a dozen frames between one `menu_sync` and the next, long enough to starve the
looping PCM hand-off". Still a real win on a redraw that only runs on open, on
a cursor step, and on a floor change -- never per frame.

## Rendering

`g_edge` widens from `unsigned char` to `unsigned short`, 1120 to 2240 bytes.
The accumulate-then-sweep model is kept, because it is what lets two lines
crossing one cell resolve into a T rather than one overwriting the other; the
new marks join it as higher bits rather than as a second painting pass.

```
bits 0-3   sides            unchanged, still indexes DT_LINK0
bit  4     STAIR            unchanged
bit  5     DASH             conditional run, or off-floor stub
bits 6-9   ARROW_N/E/S/W    arrowhead pointing that way
bit  10    UP               glyph U
bit  11    DOWN             glyph D
bit  12    LOOP             loop circle
```

Sweep precedence is glyph, then arrow, then link. **Solid beats dashed** where a
conditional line and an open one cross the same cell: a cell carrying a real
passage must not read as conditional.

### Three passes

Placement has to know which cells are free, and `g_edge` is not complete until
every link is in it. So:

1. accumulate every room-to-room link
2. place stubs, glyphs and loops into cells pass 1 left empty
3. paint the layer in one sweep, as today

### Placement

**Arrowhead.** `trace` already walks a route cell by cell; the last step before
the destination mark sets `ARROW_<dir>`. The direction is available now only
because `map_model_exits` yields *directed* edges -- the old `i < j` pair loop
had no forward.

**Drawing each pair once.** A two-way link is enumerated from both ends. It is
drawn from the lower object number; a one-way edge always draws from its source.
Exactly one line per pair, and an arrowhead only where one is earned.

**Stub.** North for `U`, south for `D`. Cell+1 takes `DASH` and the straight
mask, cell+2 the glyph. Since a room is four cells and a neighbour's mark would
sit at cell+4, the stub visibly stops short of anywhere a room could be, which
is what makes it read as an edge of the drawing rather than as a passage.

**One helper serves stubs and loops.**

```c
/* map_layout.h */
int map_layout_glyph(int mx, int my, int prefer,
                     const unsigned short taken[][MAP_ROOMS_W * MAP_CELLS],
                     int *gx, int *gy);
```

First free cell along the preferred direction (cell+2, then cell+1), then the
diagonal, then **not drawn**. Declining is the honest failure and it is what
`gather` already does when a far end is off-floor: a missing mark is better than
an invented one.

It goes in `map_layout.h` rather than `map_view.cxx` because it is pure
viewport arithmetic, and that is the split the codebase already made for exactly
this reason -- "header-only and free of SRL, so a host test can exercise the
arithmetic that a build for the target cannot be run to check". `map_view.cxx`
is at 931 lines; this keeps the testable half out of it.

## Tiles

27 new, generated by `tools/gen_dash_tiles.py`, all **appended after
`DT_KNIGHT0 + 6` so no existing index moves** -- the reason `dash_map.h` already
gives for keeping the dead `DT_BOX_*` set rather than cutting it.

| Tiles | Count |
|---|---|
| `DT_DASH0..15` -- dashed links, same 4-bit mask indexing as `DT_LINK0` | 16 |
| `DT_ARROW_N/E/S/W` and `DT_ARROW_DASH_N/E/S/W` | 8 |
| `DT_GLYPH_U`, `DT_GLYPH_D` | 2 |
| `DT_LOOP` -- circle with an arrowhead | 1 |

`DT_N` goes 115 to 142, so 4544 bytes of pattern data, well inside the
512x256 one-bank ceiling the TGA gate already checks against. Every one is drawn
on transparency like the rest of the map set, so the parchment shows through.

**Sixteen dashed tiles is deliberate.** Two -- a dashed horizontal and vertical
-- would cover stubs, which are always straight, and most conditional links.
The remainder would fall back to a solid elbow, and a conditional passage that
draws solid at its corners is the one failure a player cannot tell from a bug.
448 bytes is not a budget worth defending here.

## Testing

Host tests, TDD, against the existing suite.

| File | Cases |
|---|---|
| `test_map_model.c` | one-way detected; reverse-`BLOCKED` is not one-way; `MAYBE` survives `record_exits`; self-loop flagged; off-floor destination reported; `map_model_link` unchanged for every existing case |
| `test_map_layout.c` | `map_layout_glyph` prefers cell+2, falls back to cell+1, then the diagonal, then declines |
| `test_dash_tiles.c` | 142 tiles; every index below `DT_DASH0` byte-identical to today, guarding against renumbering; dashed tiles differ from solid only at the stipple phase |
| `test_map_edges.c` | the line accumulator's own route choice, tile precedence and layer readability, extracted so it can be exercised on the host at all |

The pair loop's replacement by per-room enumeration is **not** covered by an
automated before/after equivalence test. The spec originally called for one in
`test_dash_map.c`, asserting a fixture map's drawn edge set is byte-identical
before and after the swap; that test does not exist, because the predicate
that would have to be proven equivalent -- which pair gets drawn once, and
from which end -- lives in `map_view.cxx`'s own exit walk, which includes SRL
and does not run on the host. The equivalence instead rests on argument and on
three independent hand-proofs: every two-way link is still drawn exactly once,
each one-way link is still drawn from its source, and the arrowhead's placement
does not depend on which end of the canonicalised call happens to be the
destination. `test_map_edges.c` pins the pieces that argument depends on --
route choice, argument-order dependence, and idempotence -- so a violation of
any one of them fails a real assertion even though the equivalence claim as a
whole is not itself an automated test.

## What this does not do

- No change to `tools/gen_map_atlas.py`, the atlas format, or `map_atlas.h`.
- No change to `MAP_BLOB_MAGIC`, `MAP_BLOB_MAX`, or anything a save slot holds.
- No diagonal links. `trace` walks orthogonally and continues to; NE/NW/SE/SW
  exits contribute a one-way or conditional flag to a link drawn as a dogleg,
  and are not given a diagonal groove of their own.
- No legend on screen. Four marks that each look like what they mean, against a
  screen that is already carrying a floor number, a room name and a roster.
