# Design: recovering conditional exit destinations, and U/D on every vertical link

**Date:** 2026-09-02
**Status:** Implemented on branch `map-passage-marks`, not yet seen on hardware
**Rests on:** `docs/superpowers/specs/2026-09-01-map-passage-marks-design.md`, whose
dashed-conditional mark this makes reachable for the first time.

## The defect

`room_model_refresh_room` reads a destination in exactly one branch:

```c
if (plen == 1) {
    g_model.exits[dir] = RM_EXIT_OPEN;
    g_model.dest[dir]  = g_story[a + 1u];
} else if (plen == 2) {
    g_model.exits[dir] = RM_EXIT_BLOCKED;
} else {
    g_model.exits[dir] = RM_EXIT_MAYBE;
}
```

`map_model_exits` drops any exit with `dest == 0`, so **no conditional or door
exit in any story has ever been able to draw a line.** Three consequences, all
visible on Zork I's map:

- Behind House and the Kitchen are not joined, though the kitchen window is a
  door exit between them.
- The Living Room does not reach the Strange Passage, though the magic flag
  exit names it.
- The trap door does not join the Living Room to the Cellar.

And one that is not visible, because it is the absence of a whole feature: the
dashed-conditional mark the previous design shipped can never appear. `COND` is
set only on `RM_EXIT_MAYBE`, and every `RM_EXIT_MAYBE` has `dest == 0`.

## What the story file actually holds

Direction properties in these v3 images encode the exit kind in their length,
and the two conditional kinds carry the destination in their first byte:

| Length | Kind | Byte 0 |
|---|---|---|
| 1 | plain exit | the room |
| 2 | a message -- "the door is boarded" | a string pointer; no room exists |
| 3 | a routine decides | a routine pointer; no room to recover |
| 4 | conditional on a flag | **the room** |
| 5 | through a door | **the room**, byte 1 the door object |

Measured, not assumed. Zork I:

```
Behind House  west  CB EB 00 00 00   -> 203 Kitchen,      door 235 kitchen window
Kitchen       east  4F EB 00 00 00   -> 79  Behind House, door 235 kitchen window
Cellar        up    C1 B7 00 00 00   -> 193 Living Room,  door 183 trap door
Living Room   west  33 9F 8F B7      -> 51  Strange Passage
Kitchen       down  5E A6 8F 9A      -> 94  Studio
```

The Living Room's own `down` is `52 E8 00` -- a routine exit, with no room to
recover. It does not need one: links are undirected, so the Cellar's `up` draws
the trap door from the other side.

## The fix, and the guard it needs

Read byte 0 as the destination for lengths 4 and 5. **Do not change the state
classification** -- `command_rose.c:64` treats `RM_EXIT_MAYBE` as an available
direction, so reclassifying would silently remove directions from the compass
rose. The change is purely additive: same states, one more field filled.

Byte 0 is trusted only when the object it names is itself a room, meaning it
carries at least one direction property of its own. Direction properties are
identified from the dictionary's `FL_DIR` flag (`0x10`) on a word whose
decoded text matches one of the twelve canonical direction words -- the same
way `room_model.c` itself binds them, and the same way `tools/gen_map_atlas.py:171`
gates on them. Across the 31 shipped stories, 843 of 853 length-4-and-5
direction properties name a room; Zork I is 31 of 31. (An earlier pass at this
count, run with a consensus heuristic instead of the dictionary flag, reported
1217 of 1396 and Zork I as 42 of 42; that heuristic counted property 5 --
object 73's, "stairs" -- as if it were a direction, and missed 20 and 21,
"out" and "in", which are real ones. The numbers above are what
`saturn/tests/test_exit_dests.py` actually measures and asserts.) Without the
guard the remainder would be taken as destinations, and while a non-room can
never be gathered and so can never draw a link, it *would* satisfy the
off-floor stub test and draw a `U` or `D` pointing at nothing.

Lengths 6 to 8 are left alone. They exist in nine of the shipped stories, the
ZIL encoding does not document them as exits, and the guard is not a licence to
guess.

## U/D on every vertical link

The previous design scoped `U`/`D` to exits leaving the shown floor, so a
staircase between two rooms on one floor draws a vertical groove that does not
say which end is up. Kitchen and Attic are the case: both plain exits, both on
the surface, joined by a line that reads the same either way round.

A vertical exit whose destination is on this floor now also places its letter
beside its own mark -- `U` for even direction indices, `D` for odd, the same
rule the off-floor stub uses. Each room labels its own end from its own exit, so
a reciprocal staircase gets both letters and a one-way vertical gets one, which
is the truth in each case.

Placement reuses `map_layout_glyph` unchanged, preferring the direction of the
destination. The link's run already holds the two cells that way, so the helper
falls through to a diagonal on its own, and the letter lands beside the mark
rather than on the line. Where every candidate is taken it declines, as before.

## Testing

| File | Cases |
|---|---|
| `saturn/tests/test_room_model_static.c` | a length-5 door exit yields its room and stays `RM_EXIT_MAYBE`; a length-4 flag exit likewise; a length-2 message exit yields no room and stays `RM_EXIT_BLOCKED`; a length-3 routine exit yields no room and stays `RM_EXIT_MAYBE`; a length-5 exit whose byte 0 is not a room yields no room |
| `saturn/tests/test_exit_dests.py` | over every shipped story, each length-4 and length-5 direction property is decoded and its byte 0 checked against the room set; Zork I must be 31 of 31, the five passages named above must resolve to the rooms named above, and the running total across every shipped story must clear a floor (843 of 853 measured); a separate cross-check builds `saturn/tests/dump_exits.c` against the real `saturn/src/engine/room_model.c` and asserts its `dest` output, read through the public `room_model_bind` / `room_model_refresh_room` / `room_model_get` API, equals what this file's own Python decode derives for the same story bytes -- this is the one assertion here that a reverted fix actually fails, since every other check in this file is a property of the story bytes alone |
| `saturn/tests/test_map_edges.c`, `saturn/tests/test_map_layout.c` | not the placement decision itself -- that lives in `map_view.cxx`'s placement pass, which includes SRL and cannot be built on the host -- but the two primitives it composes: `map_layout_glyph` finding a cell along a preferred direction and declining when every candidate is taken, and `map_edges_glyph` recording a glyph in the accumulated layer so a later `map_layout_glyph` call sees it occupied |

The C/Python cross-check is the one that matters for catching a decoder
regression: it is the only assertion in this file that exercises
`room_model.c` itself rather than a from-scratch reimplementation of the
decode. The Python-only checks (population counts, the five named passages)
still read the shipped images rather than a fixture, so they catch a change
in what the story bytes actually mean -- but they would stay green even if
the fix in `room_model_refresh_room` were reverted, since nothing about them
calls the C code.

## What this does not do

- No change to `RM_EXIT_*` classification, so the compass rose, the command
  view and the console view are untouched.
- No change to `MAP_BLOB_MAGIC` or `MAP_BLOB_MAX`.
- No hamburger mark for a load-limited passage. The chimney is a length-4 flag
  exit, indistinguishable from any other; the limit lives in a routine. That
  mark waits on the atlas scan, which is also where this fix gains a second
  independent oracle.
