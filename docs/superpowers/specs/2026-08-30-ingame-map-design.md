# Design: the in-game map

**Date:** 2026-08-30
**Status:** Designed, not implemented
**Rests on:** `docs/ZORK1_MAP_RECON.md`, which was corrected the same day. The
layout rule below is that document's conclusion; read it first if the 32-pixel
step or the visited bitmap needs justifying.

## What this builds

A map screen for the CD client, reached from the Options menu, in the visual
idiom of the Japanese Saturn release: a tan ground, the player as a figure
nailed to the centre of the screen, the map scrolling under it, room labels
drawn where the player has been, and links drawn between them.

## What the original does, and what we take from it

The original stores **no room coordinates**. Its 110-entry table at `0x06072D38`
is a visited bitmap whose low bits are an unread snapshot of a text-engine
global. Geometry is built on the stack when the map opens and discarded when it
closes, by stepping 32 pixels per exit outward from the room the player is
standing in.

We take the rule and reject the amnesia. Laying out relative to the player is
what makes the figure sit still while the world moves under it, and that we
keep. Recomputing from scratch on every open is what made the original
path-dependent -- Behind House lands in two different places depending on
whether you arrived through South of House or North of House, a difference of
exactly two grid steps, measured across two capture sessions. A map that
rearranges itself between openings is a worse map, so we assign each room a
position the first time it is entered and never move it again.

## Components

Split exactly as `dash_map` / `dash_view` is: pure logic on one side of the
line, every hardware write on the other.

| Unit | Path | Owns |
|---|---|---|
| `map_model` | `saturn/src/engine/map_model.c/.h` | visited set, per-room positions, assignment, collisions, the draw transform |
| `map_view` | `saturn/src/video/map_view.cxx/.h` | ground, marks, links, figure, labels, open/close |

`map_model` takes no SRL, no VRAM and no console, so it links with plain gcc in
the host tests. `map_view` is the only file that touches hardware.

### Feeding the model

One call site. `saturn_glue.cxx:454` already runs `room_model_refresh()` once
per prompt; `map_model_enter()` goes beside it and reads the snapshot that call
just produced. The model needs no other hook, and nothing else in the client
changes to keep it fed.

## The layout rule

```
first entry to room R, arrived from F by direction D:
    target  = pos[F] + delta[D]
    pos[R]  = nearest free cell to target, deterministic spiral search
    visited[R] = 1

thereafter:
    pos[R] is never modified

draw:
    origin = pos[current]
    cell   = (pos[R] - origin) + viewport_centre
```

One grid unit is one room. The eight compass directions take the obvious unit
deltas.

### Collisions

Zork folds over itself, so two rooms will contest a cell. First one there keeps
it; the later arrival takes the nearest free cell by a fixed spiral order, and
keeps that position permanently. Deterministic, so the host tests can pin it.

The order is fixed here rather than left to the implementation, because
otherwise it gets invented twice and the tests pin whichever was written first:
expanding radius from the target, and within each radius N, E, S, W, then NE,
SE, SW, NW.

There is no "trail" separate from the links. The original stamped a trail cel
along the path walked; we draw a link between two adjacent visited rooms
wherever an exit joins them, which produces the same picture from stored state
and needs no movement history.

### The weak point: UP, DOWN, IN and OUT

The original tests a direction index against 2 and 12 and we could not pin down
what that selects, so this part is ours and is the least evidenced thing in the
design. Proposal: treat them as a one-cell vertical step drawn with a distinct
link glyph, so a staircase reads as a level change rather than as a north exit,
and let the collision nudge resolve the overlap with genuine N/S neighbours.

Expect to revise this once it is on screen. It is called out here so that a
future session changes it deliberately rather than discovering it.

## Rendering

**32 pixels is exactly four 8x8 text cells.** The original's step size lands on
our text grid, so labels ride the layer we already drive and no VDP1 bring-up is
needed for any of it.

| Element | Layer | Source |
|---|---|---|
| Tan ground | NBG0 | the wallpaper layer, already hidden under menus |
| Marks, links, figure | NBG2 | tile set extended via `tools/gen_dash_tiles.py` |
| Room labels | NBG1 text | `text_map`, snapped to the 4-cell grid |

Viewport is 10 x 7 rooms at 320x224. Labels come from `obj_short_name()` in
`room_model.c`, which is presently `static` and needs a public wrapper -- the
decoder itself already exists and is used by `room_model_full_word`.

## Where it hangs

A `Map` row in the Options menu beside `Resume`, gated on `g_in_game` exactly as
`Save Game` and `Load Game` already are. Exit returns through the existing
`menu_sync()` path, which the menu border work established must advance the
frame so sound and music do not stall while a screen is held.

## Persistence

Positions are indexed by Z-machine object number, not by the original's 0-109
room index, so the table is sized by the story's object count and 110 is only
Zork I's figure. Cap it at a fixed `MAP_ROOM_MAX` in the manner of
`RM_HERE_MAX`, and drop rooms above the cap rather than grow the heap, which is
already carrying the story image and the typeahead trie. At 256 entries the
table costs `256 * 2 * sizeof(short)` = **1 KB**, so size decides nothing here
either way.

Z-machine saves do not carry map state, so a restore can land the player in a
room the map has never seen. Write the map alongside our own slots through
`saturn_bup_write`; when a restore cannot be matched to a stored map, reset to
the current room alone rather than present a stale one.

## The netbin

The model runs there. `room_model_set_exits_only(1)` still populates `exits[]`
and `dest[]`, the server supplies a room id once a turn, and a kilobyte is
nothing against that build's budget.

The **view** is the gated half. The netbin has a hard size gate and its own
source list, pinned by `saturn/tests/test_netbin_sources.py`. Add the tiles only
if they fit; if they do not, the netbin keeps the model and skips the screen.
Decide this on a measured clean rebuild, not an estimate -- the dashboard design
was reversed once already for guessing at exactly this.

## Testing

`saturn/tests/test_map_model.c`, plain gcc, alongside `test_dash_map.c`.

The two capture walks give real fixtures, and their room-id sequences are
recorded in `ZORK1_MAP_RECON.md`:

| Test | Asserts |
|---|---|
| First-visit assignment | a room entered by direction D from F lands at `pos[F] + delta[D]` |
| Revisit stability | re-entering a room never moves it |
| Collision nudge | deterministic, stable across runs, and the first arrival keeps the cell |
| **Behind House** | arriving again by a different route leaves it exactly where it was first placed -- the exact case where the original, recomputing on every open, moved it |
| Draw transform | the current room maps to the viewport centre |
| Underground walk | the Cellar/Gallery/Studio sequence lays out without collapsing onto the above-ground rooms |

The Behind House case is the one that would catch a regression back into the
original's behaviour, so it earns its place even though it looks redundant.

Be precise about what it can claim. A graph walk cannot place Behind House on
the same cell regardless of approach — through South of House it is south-east
of the house, through North of House north-east — because that is the same
non-Euclidean geometry the original tripped on, and no layout rule of this
shape escapes it. What we guarantee, and the original did not, is that a placed
room never moves.

## Open decisions

1. **Art provenance.** Draw the ground, figure and links ourselves in the
   original's idiom, or rip cels 28-32 from the Japanese disc with
   `analysis/zork_ui_rip.py` and ship them. Drawing our own is the default here;
   ripping is more faithful and raises a redistribution question that should be
   answered deliberately rather than by default.
2. **UP/DOWN/IN/OUT**, as above.
3. **Netbin view**, pending a measured rebuild.

## Not in scope

Free scrolling. The figure is fixed at the centre and the map moves under it,
which is the original's behaviour and removes the need for a scroll model, a
cursor, and the input to drive them.
