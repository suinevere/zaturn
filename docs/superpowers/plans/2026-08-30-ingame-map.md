# In-Game Map Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A map screen reached from the Options menu that draws the rooms the
player has visited, laid out by walking the story's own exits, with the player
fixed at the centre and the map scrolling under them.

**Architecture:** A pure-logic half (`map_model.c`) owns the visited set and a
per-room grid position assigned on first entry and never moved, inferring the
direction travelled from the previous prompt's `dest[]` rather than from input.
A hardware half (`map_view.cxx`) paints the ground on NBG0, the room marks,
links and figure through a new `dash_map` entry point on NBG2, and the labels
through `text_map` on NBG1. Nothing uses VDP1.

**Tech Stack:** C99 (pure half), C++ with SaturnRingLib/SGL (hardware half),
Python 3 (tile generator), `gcc` for host tests, `sh2eb-elf-g++ -fsyntax-only`
via `saturn/syntax-check.sh` for the compile gate.

**Spec:** `docs/superpowers/specs/2026-08-30-ingame-map-design.md`, which rests
on `docs/ZORK1_MAP_RECON.md`. Read both: the 32-pixel step and the decision to
persist positions rather than recompute them are argued there, not here.

## Global Constraints

- **Author of record is `suinevere`.** Every file, function and constant gets the
  project header block (`/*---- | name | Description: | Author: | Dependencies: |
  Globals: | Params: | Returns: ----*/`). Generated files and test files get a
  file header only. `N/A` for fields that do not apply.
- **No comments inside function bodies.** Prose comments are one sentence.
- **Commit after every task.** One sentence, no body, no bullets, no trailers.
  Never mention Claude, AI, or the session.
- **The author runs all builds.** Never run `compile.bat`, `compile-cd.bat`,
  `compile-netbin.bat`, or the emulator. The compile gate available to an
  implementer is `sh syntax-check.sh <files>` from `saturn/`, which is
  `-fsyntax-only` and writes no artifacts.
- **`map_model.c` and `map_model.h` must not include `srl.hpp` or any SRL
  header.** That is what lets `saturn/tests/test_map_model.c` link on the host.
  It may include `room_model.h`, which is equally SRL-free.
- Screen is 320x224 = 40x28 text cells. `TOP_MARGIN` is 1.
- **One grid unit is one room, and one room is 4 text cells** — the original's
  32-pixel step over an 8x8 font. The viewport is 10 x 7 rooms.
- **Do not add any file to the netbin source list** in `saturn/makefile`. See
  Task 12 and "Deviations from the spec" below.
- Backup-RAM file names are truncated to **11 characters**;
  `make_slot_name` uses at most 10, which leaves exactly one byte of suffix.

## Deviations from the spec, and why

Two, both deliberate, recorded here so a reviewer sees them rather than
discovers them:

1. **The spec says the model runs in the netbin. This plan does not put it
   there.** The netbin's source list is size-gated and pinned by
   `saturn/tests/test_netbin_sources.py`, and only the author can run the
   measured clean rebuild that would justify the addition. The dashboard design
   was already reversed once for guessing at exactly this. Task 12 ships
   `#ifdef NETBIN` no-op inlines so the netbin is untouched, and leaves the
   measurement as the follow-up decision.
2. **The spec's three open decisions are taken at their stated defaults:** art
   drawn in the original's idiom rather than ripped from the Japanese disc
   (which keeps a redistribution question closed), UP/DOWN/IN/OUT as a
   one-cell vertical step with a distinct link glyph, and the netbin deferred
   as above.

## File Structure

| File | Responsibility |
|---|---|
| `saturn/src/engine/map_model.h` | the model's interface; SRL-free |
| `saturn/src/engine/map_model.c` | visited set, positions, assignment, collisions, offsets, serialisation |
| `saturn/tests/test_map_model.c` | host test, plain gcc |
| `saturn/src/engine/room_model.h/.c` | gains a public room-name accessor |
| `saturn/src/engine/saturn_glue.cxx` | one call per prompt to feed the model; save/restore hooks |
| `saturn/src/video/dash_map.h/.c` | gains the map tile vocabulary and a paint entry point |
| `tools/gen_dash_tiles.py` | generates the new map tiles |
| `saturn/src/video/dash_tiles.c/.h` | regenerated output |
| `saturn/tests/test_dash_map.c` | extended for the paint entry point |
| `saturn/tests/test_dash_tiles.c` | extended for the new tiles |
| `saturn/src/video/map_view.h/.cxx` | ground, marks, links, figure, labels, the screen loop |
| `saturn/src/menu/menu_pages.cxx` | the Map row |

The CD build globs `src/` for sources (`saturn/makefile:33-34`), so the new
files compile there with no makefile edit. The netbin's list is explicit and
stays untouched.

---

### Task 1: The model's core — first-visit placement

**Files:**
- Create: `saturn/src/engine/map_model.h`
- Create: `saturn/src/engine/map_model.c`
- Test: `saturn/tests/test_map_model.c`

**Interfaces:**
- Consumes: `RoomModel`, `RM_DIR_N`, `RM_N`..`RM_OUT` from `room_model.h`.
- Produces: `map_model_reset(void)`, `map_model_enter(const RoomModel *m)`,
  `map_model_visited(unsigned short room) -> int`,
  `map_model_pos(unsigned short room, int *x, int *y) -> int`,
  `map_model_current(void) -> unsigned short`,
  `MAP_ROOM_MAX` (256), `MAP_DIR_UNKNOWN` (-1).

The direction travelled is **inferred, not observed**: on each prompt the model
compares the new room against the previous snapshot's `dest[]` and takes the
first direction that leads to it. That is what makes the model independent of
how the player moved — typed, panel, or compass rose all land the same.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_map_model.c`:

```c
/* Build:
     gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
         saturn/src/engine/map_model.c && /tmp/tmm
   map_model.c is deliberately free of SRL includes so this links on the host. */
#include "../src/engine/map_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* A snapshot with one exit wired to a destination, which is all the model
   reads: the room it is in, and where each direction would lead. */
static RoomModel mk(unsigned short room) {
    RoomModel m;
    memset(&m, 0, sizeof m);
    m.room = room;
    return m;
}

static void link1(RoomModel *m, int dir, unsigned short dest) {
    m->exits[dir] = RM_EXIT_OPEN;
    m->dest[dir]  = dest;
}

int main(void) {
    int x = 99, y = 99;

    map_model_reset();

    /* Nothing is known before the first prompt. */
    assert(!map_model_visited(12));
    assert(!map_model_pos(12, &x, &y));

    /* The first room seen is the origin. */
    RoomModel a = mk(12);
    link1(&a, RM_N, 7);
    map_model_enter(&a);
    assert(map_model_visited(12));
    assert(map_model_pos(12, &x, &y) && x == 0 && y == 0);
    assert(map_model_current() == 12);

    /* North of it lands one cell up. */
    RoomModel b = mk(7);
    link1(&b, RM_S, 12);
    map_model_enter(&b);
    assert(map_model_pos(7, &x, &y) && x == 0 && y == -1);
    assert(map_model_current() == 7);

    /* Going back does not move the room that was already placed. */
    map_model_enter(&a);
    assert(map_model_pos(12, &x, &y) && x == 0 && y == 0);
    assert(map_model_current() == 12);

    /* A room reached with no matching dest[] still gets placed, adjacent to
       where the player came from rather than dropped. */
    RoomModel c = mk(40);
    map_model_enter(&c);
    assert(map_model_visited(40));
    assert(map_model_pos(40, &x, &y));

    printf("test_map_model: ok\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
```

Expected: FAIL — `map_model.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/engine/map_model.h`:

```c
/*----------------------
 | map_model.h
 | Description: The in-game map's model: which rooms have been seen, and where
 |   each one sits on a grid whose unit is one room. A position is assigned the
 |   first time a room is entered and never moved again, so the map does not
 |   rearrange itself between openings. Pure C -- no SRL, no VRAM, no console.
 |   Implemented in map_model.c.
 | Author: suinevere
 | Dependencies: room_model.h
 ----------------------*/
#ifndef MAP_MODEL_H
#define MAP_MODEL_H

#include "room_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | MAP_ROOM_MAX
 | Description: How many object numbers the position table covers. Rooms are
 |   keyed by Z-machine object number, not by the original Saturn game's 0-109
 |   room index, so this is sized for a story's objects rather than for Zork's
 |   room count. Fixed rather than grown because the C heap is already carrying
 |   the story image and the typeahead trie; a room above the cap is dropped
 |   from the map rather than allowed to grow it.
 | Author: suinevere
 ----------------------*/
#define MAP_ROOM_MAX 256

/*----------------------
 | MAP_DIR_UNKNOWN
 | Description: Returned by the direction inference when no exit of the
 |   previous room led to the room now occupied -- a door, a conditional exit,
 |   a teleport, or the first room of all.
 | Author: suinevere
 ----------------------*/
#define MAP_DIR_UNKNOWN (-1)

/*----------------------
 | map_model_reset
 | Description: Forgets every room and position. Call on story load and on any
 |   restore the map cannot be matched to.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_x, g_y, g_cur, g_have_cur, g_prev
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void map_model_reset(void);

/*----------------------
 | map_model_enter
 | Description: Records one prompt. Infers the direction travelled by matching
 |   the new room against the previous snapshot's dest[], places the room if it
 |   is new, and makes it current. Placing is idempotent: a room already on the
 |   grid keeps its cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_x, g_y, g_cur, g_have_cur, g_prev
 | Params: m -- the snapshot room_model_refresh just produced, never null
 | Returns: N/A
 ----------------------*/
void map_model_enter(const RoomModel *m);

/*----------------------
 | map_model_visited
 | Description: Whether a room has been entered and placed.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis
 | Params: room -- object number
 | Returns: 1 when placed, 0 otherwise or when room is out of range
 ----------------------*/
int map_model_visited(unsigned short room);

/*----------------------
 | map_model_pos
 | Description: A placed room's grid cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_x, g_y
 | Params: room -- object number; x, y -- receive the cell, untouched on failure
 | Returns: 1 when the room is placed, 0 otherwise
 ----------------------*/
int map_model_pos(unsigned short room, int *x, int *y);

/*----------------------
 | map_model_current
 | Description: The room the player is in, as of the last map_model_enter.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cur
 | Params: N/A
 | Returns: the object number, or 0 before the first entry
 ----------------------*/
unsigned short map_model_current(void);

#ifdef __cplusplus
}
#endif
#endif /* MAP_MODEL_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/engine/map_model.c`:

```c
/*----------------------
 | map_model.c
 | Description: Implements map_model.h. See docs/ZORK1_MAP_RECON.md for why the
 |   layout is a graph walk rather than a coordinate table: the original Saturn
 |   release has no per-room coordinates and recomputes its map on every open,
 |   which is what made it place a room differently depending on the route
 |   taken to it.
 | Author: suinevere
 ----------------------*/
#include "map_model.h"

/*----------------------
 | DX / DY
 | Description: The grid step for each RM_* direction, in room units. UP, DOWN,
 |   IN and OUT get a vertical step so a staircase reads as a level change
 |   rather than as a north exit; where that collides with a real neighbour the
 |   placement search resolves it.
 | Author: suinevere
 ----------------------*/
static const signed char DX[RM_DIR_N] = { 0, 1,-1, 0, 1,-1, 1,-1, 0, 0, 0, 0 };
static const signed char DY[RM_DIR_N] = {-1, 0, 0, 1,-1,-1, 1, 1,-1, 1, 1,-1 };

/*----------------------
 | g_vis / g_x / g_y / g_cur / g_have_cur / g_prev / g_have_prev
 | Description: The placed set and its coordinates, the current room, and the
 |   previous prompt's snapshot, which is the only thing that can say which way
 |   the player just moved.
 | Author: suinevere
 ----------------------*/
static unsigned char  g_vis[MAP_ROOM_MAX];
static short          g_x[MAP_ROOM_MAX];
static short          g_y[MAP_ROOM_MAX];
static unsigned short g_cur;
static int            g_have_cur;
static RoomModel      g_prev;
static int            g_have_prev;

/*----------------------
 | map_model_reset
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_cur, g_have_cur, g_have_prev
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void map_model_reset(void) {
    int i;
    for (i = 0; i < MAP_ROOM_MAX; i++) { g_vis[i] = 0; g_x[i] = 0; g_y[i] = 0; }
    g_cur = 0;
    g_have_cur = 0;
    g_have_prev = 0;
}

/*----------------------
 | in_range
 | Description: Whether an object number is one the position table covers.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: room -- object number
 | Returns: 1 when addressable, 0 otherwise
 ----------------------*/
static int in_range(unsigned short room) {
    return room != 0 && room < MAP_ROOM_MAX;
}

/*----------------------
 | dir_from_prev
 | Description: Which direction of the previous room leads to `room`, taking the
 |   first match so the result is stable when a room is reachable two ways.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_prev, g_have_prev
 | Params: room -- the room now occupied
 | Returns: an RM_* index, or MAP_DIR_UNKNOWN
 ----------------------*/
static int dir_from_prev(unsigned short room) {
    int d;
    if (!g_have_prev) return MAP_DIR_UNKNOWN;
    for (d = 0; d < RM_DIR_N; d++)
        if (g_prev.exits[d] != RM_EXIT_NONE && g_prev.dest[d] == room) return d;
    return MAP_DIR_UNKNOWN;
}

/*----------------------
 | cell_taken
 | Description: Whether a placed room other than `self` already holds a cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_x, g_y
 | Params: x, y -- the cell; self -- the room allowed to hold it
 | Returns: 1 when occupied, 0 otherwise
 ----------------------*/
static int cell_taken(int x, int y, unsigned short self) {
    int i;
    for (i = 0; i < MAP_ROOM_MAX; i++)
        if (g_vis[i] && i != (int) self && g_x[i] == x && g_y[i] == y) return 1;
    return 0;
}

/*----------------------
 | place
 | Description: Puts a room on the grid at the first free cell at or near the
 |   target, and marks it placed.
 | Author: suinevere
 | Dependencies: cell_taken
 | Globals: g_vis, g_x, g_y
 | Params: room -- object number; tx, ty -- the wanted cell
 | Returns: N/A
 ----------------------*/
static void place(unsigned short room, int tx, int ty) {
    g_x[room] = (short) tx;
    g_y[room] = (short) ty;
    g_vis[room] = 1;
}

/*----------------------
 | map_model_enter
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: dir_from_prev, place
 | Globals: g_vis, g_cur, g_have_cur, g_prev, g_have_prev
 | Params: m -- the snapshot, never null
 | Returns: N/A
 ----------------------*/
void map_model_enter(const RoomModel *m) {
    unsigned short room = m->room;
    if (!in_range(room)) return;

    if (!g_vis[room]) {
        int d = dir_from_prev(room);
        int tx = 0, ty = 0;
        if (g_have_cur && g_vis[g_cur]) {
            tx = g_x[g_cur];
            ty = g_y[g_cur];
            if (d != MAP_DIR_UNKNOWN) { tx += DX[d]; ty += DY[d]; }
            else                      { ty += 1; }
        }
        place(room, tx, ty);
    }

    g_cur = room;
    g_have_cur = 1;
    g_prev = *m;
    g_have_prev = 1;
}

/*----------------------
 | map_model_visited
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: in_range
 | Globals: g_vis
 | Params: room -- object number
 | Returns: 1 when placed, 0 otherwise
 ----------------------*/
int map_model_visited(unsigned short room) {
    return in_range(room) && g_vis[room];
}

/*----------------------
 | map_model_pos
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_visited
 | Globals: g_x, g_y
 | Params: room -- object number; x, y -- receive the cell
 | Returns: 1 when placed, 0 otherwise
 ----------------------*/
int map_model_pos(unsigned short room, int *x, int *y) {
    if (!map_model_visited(room)) return 0;
    *x = g_x[room];
    *y = g_y[room];
    return 1;
}

/*----------------------
 | map_model_current
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cur, g_have_cur
 | Params: N/A
 | Returns: the current object number, or 0
 ----------------------*/
unsigned short map_model_current(void) {
    return g_have_cur ? g_cur : (unsigned short) 0;
}
```

`cell_taken` is unused until Task 2 and will warn under `-Wunused-function`;
that is expected and Task 2 consumes it. If the warning blocks the build, add
`(void) cell_taken;` inside `place` and remove it in Task 2.

- [ ] **Step 5: Run test to verify it passes**

```bash
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
```

Expected: `test_map_model: ok`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/engine/map_model.h saturn/src/engine/map_model.c \
        saturn/tests/test_map_model.c
git commit -m "Place each room on the map grid the first time it is entered, taking the direction from the previous prompt's destinations rather than from input so a typed move, the panel and the compass rose all land the same."
```

---

### Task 2: The collision spiral

**Files:**
- Modify: `saturn/src/engine/map_model.c`
- Test: `saturn/tests/test_map_model.c`

**Interfaces:**
- Consumes: `place`, `cell_taken` from Task 1.
- Produces: no new public symbols; `place` gains the search.

Zork folds over itself, so two rooms will want one cell. The first one there
keeps it. The order is fixed in the spec so it is not invented twice: expanding
radius from the target, and within each radius N, E, S, W, NE, SE, SW, NW.

- [ ] **Step 1: Write the failing test**

Append inside `main()` in `saturn/tests/test_map_model.c`, before the
`printf`:

```c
    /* Two rooms wanting one cell: the first keeps it, the second takes the
       first free cell in the fixed order -- north of the target. */
    map_model_reset();

    RoomModel h = mk(1);
    link1(&h, RM_N, 2);
    link1(&h, RM_E, 3);
    map_model_enter(&h);

    RoomModel n1 = mk(2);
    link1(&n1, RM_S, 1);
    map_model_enter(&n1);
    assert(map_model_pos(2, &x, &y) && x == 0 && y == -1);

    map_model_enter(&h);

    /* Room 3 is east of room 1, so it wants (1,0) -- free, no contest. */
    RoomModel e1 = mk(3);
    link1(&e1, RM_W, 1);
    map_model_enter(&e1);
    assert(map_model_pos(3, &x, &y) && x == 1 && y == 0);

    /* Now a genuine contest. Room 5 is UP from room 1, and UP steps the same
       way north does, so it wants (0,-1) -- which room 2 already holds. The
       spiral's first probe is north, so room 5 must land at (0,-2) and room 2
       must not have moved. */
    map_model_reset();
    RoomModel c1 = mk(1);
    link1(&c1, RM_N, 2);
    link1(&c1, RM_UP, 5);
    map_model_enter(&c1);
    assert(map_model_pos(1, &x, &y) && x == 0 && y == 0);

    RoomModel c2 = mk(2);
    link1(&c2, RM_S, 1);
    map_model_enter(&c2);
    assert(map_model_pos(2, &x, &y) && x == 0 && y == -1);

    map_model_enter(&c1);
    RoomModel c5 = mk(5);
    link1(&c5, RM_DOWN, 1);
    map_model_enter(&c5);
    assert(map_model_pos(5, &x, &y) && x == 0 && y == -2);
    assert(map_model_pos(2, &x, &y) && x == 0 && y == -1);

    /* Whatever the arrangement, no two placed rooms may share a cell. */
    {
        int i, j, xi, yi, xj, yj;
        for (i = 0; i < MAP_ROOM_MAX; i++) {
            if (!map_model_visited((unsigned short) i)) continue;
            for (j = i + 1; j < MAP_ROOM_MAX; j++) {
                if (!map_model_visited((unsigned short) j)) continue;
                assert(map_model_pos((unsigned short) i, &xi, &yi));
                assert(map_model_pos((unsigned short) j, &xj, &yj));
                assert(!(xi == xj && yi == yj));
            }
        }
    }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
```

Expected: FAIL — the no-shared-cell assertion trips, because `place` still
writes the target cell unconditionally.

- [ ] **Step 3: Add the search to `place`**

Replace `place` in `saturn/src/engine/map_model.c`, and add the order table
above it:

```c
/*----------------------
 | SPX / SPY / MAP_SPIRAL_MAX
 | Description: The order a contested placement searches outward in, and how far
 |   it will go before giving up and stacking. Fixed here rather than left to
 |   the implementation so the host tests pin an order that was chosen, not one
 |   that happened.
 | Author: suinevere
 ----------------------*/
static const signed char SPX[8] = { 0, 1, 0,-1, 1, 1,-1,-1 };
static const signed char SPY[8] = {-1, 0, 1, 0,-1, 1, 1,-1 };
#define MAP_SPIRAL_MAX 8

/*----------------------
 | place
 | Description: Puts a room at the target cell, or at the nearest free cell in
 |   the fixed search order when the target is taken, and marks it placed. The
 |   first room to hold a cell keeps it.
 | Author: suinevere
 | Dependencies: cell_taken
 | Globals: g_vis, g_x, g_y
 | Params: room -- object number; tx, ty -- the wanted cell
 | Returns: N/A
 ----------------------*/
static void place(unsigned short room, int tx, int ty) {
    int r, k, x = tx, y = ty;
    if (cell_taken(tx, ty, room)) {
        int done = 0;
        for (r = 1; r <= MAP_SPIRAL_MAX && !done; r++) {
            for (k = 0; k < 8 && !done; k++) {
                int cx = tx + SPX[k] * r, cy = ty + SPY[k] * r;
                if (!cell_taken(cx, cy, room)) { x = cx; y = cy; done = 1; }
            }
        }
    }
    g_x[room] = (short) x;
    g_y[room] = (short) y;
    g_vis[room] = 1;
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
```

Expected: `test_map_model: ok`

- [ ] **Step 5: Commit**

```bash
git add saturn/src/engine/map_model.c saturn/tests/test_map_model.c
git commit -m "Resolve contested map cells by searching outward in a fixed order, since Zork folds over itself and two rooms will want one cell, and let the first room there keep it so no placement ever moves."
```

---

### Task 3: Offsets from the player, and iteration

**Files:**
- Modify: `saturn/src/engine/map_model.h`, `saturn/src/engine/map_model.c`
- Test: `saturn/tests/test_map_model.c`

**Interfaces:**
- Produces: `map_model_offset(unsigned short room, int *dx, int *dy) -> int`,
  `map_model_count(void) -> int`,
  `map_model_room_at(int index, unsigned short *room) -> int`.

The view needs two things: where a room sits relative to the player, and a way
to walk the placed set without scanning 256 slots itself. Offsets are in room
units; multiplying by 4 to reach text cells is the view's job.

- [ ] **Step 1: Write the failing test**

Append inside `main()`, before the `printf`:

```c
    /* Offsets are measured from the player, which is what keeps the figure
       fixed while the map moves under it. */
    map_model_reset();
    RoomModel o1 = mk(12);
    link1(&o1, RM_N, 7);
    map_model_enter(&o1);
    RoomModel o2 = mk(7);
    link1(&o2, RM_N, 8);
    link1(&o2, RM_S, 12);
    map_model_enter(&o2);
    RoomModel o3 = mk(8);
    link1(&o3, RM_S, 7);
    map_model_enter(&o3);

    /* Standing in 8: itself is the origin, 7 is one south, 12 is two south. */
    assert(map_model_current() == 8);
    assert(map_model_offset(8,  &x, &y) && x == 0 && y == 0);
    assert(map_model_offset(7,  &x, &y) && x == 0 && y == 1);
    assert(map_model_offset(12, &x, &y) && x == 0 && y == 2);

    /* Walking back re-centres on the new room without moving anything. */
    map_model_enter(&o2);
    assert(map_model_offset(7,  &x, &y) && x == 0 && y == 0);
    assert(map_model_offset(8,  &x, &y) && x == 0 && y == -1);

    /* An unplaced room has no offset. */
    assert(!map_model_offset(99, &x, &y));

    /* The placed set is walkable, ascending, and holds exactly the three. */
    assert(map_model_count() == 3);
    {
        unsigned short r = 0;
        assert(map_model_room_at(0, &r) && r == 7);
        assert(map_model_room_at(1, &r) && r == 8);
        assert(map_model_room_at(2, &r) && r == 12);
        assert(!map_model_room_at(3, &r));
    }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
```

Expected: FAIL — `implicit declaration of function 'map_model_offset'`.

- [ ] **Step 3: Declare the three in the header**

Add to `saturn/src/engine/map_model.h`, above the `#ifdef __cplusplus` close:

```c
/*----------------------
 | map_model_offset
 | Description: A placed room's cell relative to the room the player is in, in
 |   room units. The player is always the origin, which is what lets the view
 |   nail the figure to the centre of the screen and scroll the map under it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_x, g_y, g_cur, g_have_cur
 | Params: room -- object number; dx, dy -- receive the offset, untouched on
 |   failure
 | Returns: 1 when both the room and the player are placed, 0 otherwise
 ----------------------*/
int map_model_offset(unsigned short room, int *dx, int *dy);

/*----------------------
 | map_model_count
 | Description: How many rooms are placed.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis
 | Params: N/A
 | Returns: the count, 0 or more
 ----------------------*/
int map_model_count(void);

/*----------------------
 | map_model_room_at
 | Description: The index'th placed room in ascending object order, so the view
 |   can walk the set without scanning MAP_ROOM_MAX slots itself.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis
 | Params: index -- 0 to map_model_count()-1; room -- receives the object
 |   number, untouched on failure
 | Returns: 1 on success, 0 when index is out of range
 ----------------------*/
int map_model_room_at(int index, unsigned short *room);
```

- [ ] **Step 4: Implement them**

Append to `saturn/src/engine/map_model.c`:

```c
/*----------------------
 | map_model_offset
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_visited
 | Globals: g_x, g_y, g_cur, g_have_cur
 | Params: room -- object number; dx, dy -- receive the offset
 | Returns: 1 on success, 0 otherwise
 ----------------------*/
int map_model_offset(unsigned short room, int *dx, int *dy) {
    if (!g_have_cur || !map_model_visited(g_cur)) return 0;
    if (!map_model_visited(room)) return 0;
    *dx = g_x[room] - g_x[g_cur];
    *dy = g_y[room] - g_y[g_cur];
    return 1;
}

/*----------------------
 | map_model_count
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis
 | Params: N/A
 | Returns: the number of placed rooms
 ----------------------*/
int map_model_count(void) {
    int i, n = 0;
    for (i = 0; i < MAP_ROOM_MAX; i++) if (g_vis[i]) n++;
    return n;
}

/*----------------------
 | map_model_room_at
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis
 | Params: index -- position in the placed set; room -- receives the object
 | Returns: 1 on success, 0 when index is out of range
 ----------------------*/
int map_model_room_at(int index, unsigned short *room) {
    int i, n = 0;
    if (index < 0) return 0;
    for (i = 0; i < MAP_ROOM_MAX; i++) {
        if (!g_vis[i]) continue;
        if (n == index) { *room = (unsigned short) i; return 1; }
        n++;
    }
    return 0;
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
```

Expected: `test_map_model: ok`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/engine/map_model.h saturn/src/engine/map_model.c \
        saturn/tests/test_map_model.c
git commit -m "Report every placed room's cell relative to the player rather than absolutely, which is what lets the view nail the figure to the centre and scroll the map underneath, and expose the placed set as an ordered walk so the view need not scan the whole table."
```

---

### Task 4: Vertical links

**Files:**
- Modify: `saturn/src/engine/map_model.h`, `saturn/src/engine/map_model.c`
- Test: `saturn/tests/test_map_model.c`

**Interfaces:**
- Produces: `map_model_link(unsigned short a, unsigned short b) -> int`,
  `MAP_LINK_NONE` (0), `MAP_LINK_FLAT` (1), `MAP_LINK_VERT` (2).

This is the least-evidenced part of the design; the spec says so and says why.
A staircase must not read as a north exit, so a link travelled by UP, DOWN, IN
or OUT reports as vertical and the view draws it differently.

- [ ] **Step 1: Write the failing test**

Append inside `main()`, before the `printf`:

```c
    /* A staircase is a link, but not the same kind of link as a road. */
    map_model_reset();
    RoomModel v1 = mk(15);
    link1(&v1, RM_DOWN, 16);
    link1(&v1, RM_N, 14);
    map_model_enter(&v1);
    RoomModel v2 = mk(16);
    link1(&v2, RM_UP, 15);
    map_model_enter(&v2);
    RoomModel v3 = mk(14);
    link1(&v3, RM_S, 15);
    map_model_enter(&v3);

    assert(map_model_link(15, 16) == MAP_LINK_VERT);
    assert(map_model_link(16, 15) == MAP_LINK_VERT);
    assert(map_model_link(15, 14) == MAP_LINK_FLAT);
    assert(map_model_link(14, 16) == MAP_LINK_NONE);
    assert(map_model_link(15, 99) == MAP_LINK_NONE);
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
```

Expected: FAIL — `implicit declaration of function 'map_model_link'`.

- [ ] **Step 3: Record the exits, and declare the query**

A link is a property of the story, not of the walk, so the model must keep each
placed room's exits. Add the store to `saturn/src/engine/map_model.c`, beside
the other globals:

```c
/*----------------------
 | g_dest / g_kind
 | Description: Each placed room's destinations and whether each was a flat or
 |   a vertical exit, kept because a link is a property of the story rather
 |   than of the route walked and the view asks about pairs the player may
 |   never have travelled in that direction.
 | Author: suinevere
 ----------------------*/
static unsigned short g_dest[MAP_ROOM_MAX][RM_DIR_N];
static unsigned char  g_kind[MAP_ROOM_MAX][RM_DIR_N];
```

Add to `map_model_reset`, inside the existing loop body:

```c
    { int d; for (d = 0; d < RM_DIR_N; d++) { g_dest[i][d] = 0; g_kind[i][d] = 0; } }
```

Add to `map_model_enter`, immediately before `g_cur = room;`:

```c
    {
        int d;
        for (d = 0; d < RM_DIR_N; d++) {
            g_dest[room][d] = m->dest[d];
            g_kind[room][d] = (unsigned char)
                (m->exits[d] == RM_EXIT_NONE ? MAP_LINK_NONE
                 : (d >= RM_UP ? MAP_LINK_VERT : MAP_LINK_FLAT));
        }
    }
```

Add to `saturn/src/engine/map_model.h`, above the `#ifdef __cplusplus` close:

```c
/*----------------------
 | MAP_LINK_NONE .. MAP_LINK_VERT
 | Description: How two rooms are joined. VERT covers UP, DOWN, IN and OUT, so
 |   a staircase can be drawn as a level change rather than as a road.
 | Author: suinevere
 ----------------------*/
#define MAP_LINK_NONE 0
#define MAP_LINK_FLAT 1
#define MAP_LINK_VERT 2

/*----------------------
 | map_model_link
 | Description: How room a is joined to room b, if at all. Asked of the story's
 |   exits rather than of the route walked, so a link shows as soon as both
 |   ends are placed.
 | Author: suinevere
 | Dependencies: map_model_visited
 | Globals: g_dest, g_kind
 | Params: a, b -- object numbers
 | Returns: MAP_LINK_VERT, MAP_LINK_FLAT, or MAP_LINK_NONE
 ----------------------*/
int map_model_link(unsigned short a, unsigned short b);
```

- [ ] **Step 4: Implement the query**

Append to `saturn/src/engine/map_model.c`:

```c
/*----------------------
 | map_model_link
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_visited
 | Globals: g_dest, g_kind
 | Params: a, b -- object numbers
 | Returns: the link kind
 ----------------------*/
int map_model_link(unsigned short a, unsigned short b) {
    int d, best = MAP_LINK_NONE;
    if (!map_model_visited(a) || !map_model_visited(b)) return MAP_LINK_NONE;
    for (d = 0; d < RM_DIR_N; d++) {
        if (g_kind[a][d] != MAP_LINK_NONE && g_dest[a][d] == b) {
            if (g_kind[a][d] == MAP_LINK_FLAT) return MAP_LINK_FLAT;
            best = MAP_LINK_VERT;
        }
        if (g_kind[b][d] != MAP_LINK_NONE && g_dest[b][d] == a) {
            if (g_kind[b][d] == MAP_LINK_FLAT) return MAP_LINK_FLAT;
            best = MAP_LINK_VERT;
        }
    }
    return best;
}
```

A pair joined both ways reports flat, because a road that happens also to have
a ladder is still a road to walk along.

- [ ] **Step 5: Run test to verify it passes**

```bash
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
```

Expected: `test_map_model: ok`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/engine/map_model.h saturn/src/engine/map_model.c \
        saturn/tests/test_map_model.c
git commit -m "Distinguish a staircase from a road by recording each placed room's exits and reporting a link travelled by up, down, in or out as vertical, so the view can draw a level change rather than something that reads as a north exit."
```

---

### Task 5: The captured walks as regression fixtures

**Files:**
- Modify: `saturn/tests/test_map_model.c`

**Interfaces:**
- Consumes: everything from Tasks 1-4. No new symbols.

This is the test that earns its keep, but be precise about what it can claim.
The original Saturn release recomputed its layout on every open, so Behind
House **moved** between openings depending on the route walked — measured
across two capture sessions and recorded in `docs/ZORK1_MAP_RECON.md`.

A graph walk cannot make Behind House land on the same cell regardless of
approach; reached through South of House it is south-east of the house, and
through North of House it is north-east. That is the same non-Euclidean
geometry the original tripped on, and no layout rule of this shape escapes it.

What our design guarantees, and the original did not, is that **a placed room
never moves** — including when the player arrives again by another route. That
is the property this test pins. Both walks below are the real room-id sequences
from `saves/zork1-map-underground/` and from the recon document.

- [ ] **Step 1: Write the failing test**

Append inside `main()`, before the `printf`:

```c
    /* Zork I object numbers, from the two captured walks in
       docs/ZORK1_MAP_RECON.md: 12 West of House, 9 South of House,
       1 Behind House, 7 North of House, 8 Forest Path, 15 Living Room,
       16 Cellar, 17 East of Chasm, 18 Gallery, 19 Studio. */
    {
        int ax, ay, bx, by;

        map_model_reset();
        RoomModel woh = mk(12); link1(&woh, RM_S, 9); link1(&woh, RM_N, 7);
        RoomModel soh = mk(9);  link1(&soh, RM_E, 1); link1(&soh, RM_W, 12);
        RoomModel bh  = mk(1);  link1(&bh,  RM_N, 7); link1(&bh, RM_S, 9);
        RoomModel noh = mk(7);  link1(&noh, RM_E, 1); link1(&noh, RM_S, 12);

        /* Reach Behind House the first way: south from the front of the house,
           then east. */
        map_model_enter(&woh);
        map_model_enter(&soh);
        map_model_enter(&bh);
        assert(map_model_pos(1, &ax, &ay));

        /* Now walk out north and come back east into the same room. The
           original recomputed its layout on every open and moved Behind House
           when the route changed; this must not. */
        map_model_enter(&noh);
        map_model_enter(&bh);
        assert(map_model_pos(1, &bx, &by));
        assert(ax == bx && ay == by);

        /* And the room it was first placed against has not moved either. */
        assert(map_model_pos(9, &x, &y) && x == 0 && y == 1);
    }

    /* The underground walk lays out without any room landing on another:
       Living Room -> down -> Cellar -> south -> East of Chasm -> east ->
       Gallery -> north -> Studio. */
    {
        int i, j, xi, yi, xj, yj;
        map_model_reset();
        RoomModel lr = mk(15); link1(&lr, RM_DOWN, 16);
        RoomModel ce = mk(16); link1(&ce, RM_UP, 15); link1(&ce, RM_S, 17);
        RoomModel ec = mk(17); link1(&ec, RM_N, 16);  link1(&ec, RM_E, 18);
        RoomModel ga = mk(18); link1(&ga, RM_W, 17);  link1(&ga, RM_N, 19);
        RoomModel st = mk(19); link1(&st, RM_S, 18);
        map_model_enter(&lr);
        map_model_enter(&ce);
        map_model_enter(&ec);
        map_model_enter(&ga);
        map_model_enter(&st);

        assert(map_model_count() == 5);
        assert(map_model_link(15, 16) == MAP_LINK_VERT);
        assert(map_model_link(17, 18) == MAP_LINK_FLAT);

        for (i = 0; i < MAP_ROOM_MAX; i++) {
            if (!map_model_visited((unsigned short) i)) continue;
            for (j = i + 1; j < MAP_ROOM_MAX; j++) {
                if (!map_model_visited((unsigned short) j)) continue;
                assert(map_model_pos((unsigned short) i, &xi, &yi));
                assert(map_model_pos((unsigned short) j, &xj, &yj));
                assert(!(xi == xj && yi == yj));
            }
        }
    }
```

- [ ] **Step 2: Run the test**

```bash
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
```

Expected: PASS if Tasks 1-4 are correct. **If the Behind House assertion fails,
do not weaken it** — it is the whole point of the task, and a failure means the
placement depends on the route, which is the original's bug. Fix the placement.

- [ ] **Step 3: Commit**

```bash
git add saturn/tests/test_map_model.c
git commit -m "Pin the two captured Zork walks as fixtures, above all the case the whole design exists for: arriving at Behind House again by a different route leaves it exactly where it was first placed, which is what the original -- recomputing its layout on every open -- did not do."
```

---

### Task 6: A public room name

**Files:**
- Modify: `saturn/src/engine/room_model.h`, `saturn/src/engine/room_model.c`
- Test: `saturn/tests/test_room_model.c`

**Interfaces:**
- Produces: `room_model_object_name(unsigned short obj, char *out, int max) -> int`.

`obj_short_name` already decodes an object's short name and is already used by
`room_model_full_word`; it is only `static`. The map needs "West of House" for
its labels, so it becomes public. No decoder is written.

- [ ] **Step 1: Write the failing test**

Read `saturn/tests/test_room_model.c` first to match how it loads a story
fixture, then append a check inside its `main()` before the final `printf`,
using the same story handle that file already sets up:

```c
    /* The room object's short name, which the map draws as its label. */
    {
        char nm[40];
        assert(room_model_object_name(room_model_get()->room, nm, sizeof nm));
        assert(nm[0] != '\0');
    }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -O2 -I saturn/src -o /tmp/trm saturn/tests/test_room_model.c \
    saturn/src/engine/room_model.c && /tmp/trm
```

Expected: FAIL — `implicit declaration of function 'room_model_object_name'`.

- [ ] **Step 3: Declare it**

Add to `saturn/src/engine/room_model.h`, beside `room_model_object_word`:

```c
/*----------------------
 | room_model_object_name
 | Description: An object's full short name, decoded from the Z-string at its
 |   property table -- "West of House" rather than the six-character dictionary
 |   form room_model_object_word returns. The decoder is the one
 |   room_model_full_word already uses; this only makes it reachable.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_story, g_len, g_available
 | Params: obj -- object number; out -- receives the name; max -- its capacity
 | Returns: 1 and fills out on success, 0 and empties out otherwise
 ----------------------*/
int room_model_object_name(unsigned short obj, char *out, int max);
```

- [ ] **Step 4: Add the wrapper**

Append to `saturn/src/engine/room_model.c`:

```c
/*----------------------
 | room_model_object_name
 | Description: See room_model.h.
 | Author: suinevere
 | Dependencies: obj_short_name
 | Globals: g_available
 | Params: obj -- object number; out -- receives the name; max -- its capacity
 | Returns: 1 on success, 0 otherwise
 ----------------------*/
int room_model_object_name(unsigned short obj, char *out, int max) {
    if (max > 0) out[0] = '\0';
    if (!g_available || max <= 0) return 0;
    return obj_short_name(obj, out, max);
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
gcc -O2 -I saturn/src -o /tmp/trm saturn/tests/test_room_model.c \
    saturn/src/engine/room_model.c && /tmp/trm
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
```

Expected: both pass.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/engine/room_model.h saturn/src/engine/room_model.c \
        saturn/tests/test_room_model.c
git commit -m "Expose the object short-name decoder that room_model_full_word already uses, so the map can label a room West of House rather than the six-character dictionary form."
```

---

### Task 7: Feed the model

**Files:**
- Modify: `saturn/src/engine/saturn_glue.cxx:454`

**Interfaces:**
- Consumes: `map_model_enter`, `map_model_reset`, `room_model_get`.
- Produces: nothing new; the model is fed from here on.

One call. `room_model_refresh()` already runs once per prompt and the snapshot
it produces is exactly the model's input.

- [ ] **Step 1: Add the include**

In `saturn/src/engine/saturn_glue.cxx`, beside the existing `room_model.h`
include:

```cpp
#include "map_model.h"
```

- [ ] **Step 2: Feed it, once per prompt**

At `saturn/src/engine/saturn_glue.cxx:454`, immediately after the existing
`room_model_refresh();`:

```cpp
    map_model_enter(room_model_get());
```

- [ ] **Step 3: Reset it on story load**

Find the `room_model_bind(story, len);` call at `saturn_glue.cxx:179` and add
directly after it:

```cpp
    map_model_reset();
```

- [ ] **Step 4: Syntax-check**

```bash
cd saturn && sh syntax-check.sh src/engine/saturn_glue.cxx src/engine/map_model.c
```

Expected: no errors. This is `-fsyntax-only`; it cannot catch a link error, and
the author runs the real build.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/engine/saturn_glue.cxx
git commit -m "Feed the map one snapshot per prompt from the refresh the room model already performs there, and forget every room when a new story is bound."
```

---

### Task 8: The map's tile vocabulary and paint entry point

**Files:**
- Modify: `saturn/src/video/dash_map.h`, `saturn/src/video/dash_map.c`
- Test: `saturn/tests/test_dash_map.c`

**Interfaces:**
- Produces: `dash_map_paint(int x, int y, unsigned char tile)`,
  `dash_map_begin(void)`, `DT_GROUND`, `DT_ROOM`, `DT_ROOM_HERE`, `DT_LINK_H`,
  `DT_LINK_V`, `DT_LINK_STAIR`.

`dash_box` clears the layer and holds exactly one rectangle — "one thing is on
this layer at a time". A map is many things, so it needs its own entry point:
`dash_map_begin` claims and clears the layer, then `dash_map_paint` sets
individual cells.

- [ ] **Step 1: Write the failing test**

Append inside `main()` in `saturn/tests/test_dash_map.c`, before its final
`printf`:

```c
    /* The map paints individual cells rather than one rectangle, so it gets
       its own claim: begin clears, paint sets, and nothing else survives. */
    dash_reset();
    dash_build(DASH_PANEL, 19);
    dash_map_begin();
    for (y = 0; y < DASH_ROWS; y++) assert(row_all(y, DT_BLANK));

    dash_map_paint(4, 8, DT_ROOM);
    dash_map_paint(4, 12, DT_ROOM_HERE);
    dash_map_paint(4, 10, DT_LINK_V);
    assert(dash_cell(4, 8)  == DT_ROOM);
    assert(dash_cell(4, 12) == DT_ROOM_HERE);
    assert(dash_cell(4, 10) == DT_LINK_V);
    assert(dash_cell(5, 8)  == DT_BLANK);

    /* Out-of-range paints are dropped rather than trusted, so a view clipping
       at the screen edge needs no bounds test of its own. */
    dash_map_paint(-1, 5, DT_ROOM);
    dash_map_paint(DASH_COLS, 5, DT_ROOM);
    dash_map_paint(3, DASH_ROWS, DT_ROOM);
    assert(dash_cell(3, 5) == DT_BLANK);
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c \
    saturn/src/video/dash_map.c && /tmp/tdm
```

Expected: FAIL — `implicit declaration of function 'dash_map_begin'`.

- [ ] **Step 3: Add the tiles and the entry points to the header**

In `saturn/src/video/dash_map.h`, add the six names to the `DT_*` enum in the
order `DT_GROUND, DT_ROOM, DT_ROOM_HERE, DT_LINK_H, DT_LINK_V, DT_LINK_STAIR`,
immediately before `DT_N` so the existing tile indices do not move, then
declare:

```c
/*----------------------
 | dash_map_begin
 | Description: Claims the layer for the map and clears it, exactly as
 |   dash_build and dash_box do: one thing is on this layer at a time. Call
 |   once per frame the map is on screen, then paint into it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map, g_variant, g_base, g_box, g_dirty_top, g_dirty_bottom,
 |   g_touched
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void dash_map_begin(void);

/*----------------------
 | dash_map_paint
 | Description: Sets one cell of the shadow. Out-of-range coordinates are
 |   dropped rather than faulting, so a caller clipping at the screen edge
 |   needs no bounds test of its own.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map, g_dirty_top, g_dirty_bottom
 | Params: x -- column; y -- row; tile -- a DT_* index
 | Returns: N/A
 ----------------------*/
void dash_map_paint(int x, int y, unsigned char tile);
```

- [ ] **Step 4: Implement them**

Append to `saturn/src/video/dash_map.c`, following how `dash_box` claims the
layer and marks its dirty range:

```c
/*----------------------
 | dash_map_begin
 | Description: See dash_map.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map, g_variant, g_base, g_box, g_dirty_top, g_dirty_bottom,
 |   g_touched
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void dash_map_begin(void) {
    int x, y;
    for (y = 0; y < DASH_ROWS; y++)
        for (x = 0; x < DASH_COLS; x++) g_map[y][x] = DT_BLANK;
    g_variant = DASH_VARIANT_MAP;
    g_dirty_top = 0;
    g_dirty_bottom = DASH_ROWS - 1;
    g_touched = 1;
}

/*----------------------
 | dash_map_paint
 | Description: See dash_map.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map, g_dirty_top, g_dirty_bottom
 | Params: x -- column; y -- row; tile -- a DT_* index
 | Returns: N/A
 ----------------------*/
void dash_map_paint(int x, int y, unsigned char tile) {
    if (x < 0 || x >= DASH_COLS || y < 0 || y >= DASH_ROWS) return;
    g_map[y][x] = tile;
    if (y < g_dirty_top)    g_dirty_top = y;
    if (y > g_dirty_bottom) g_dirty_bottom = y;
}
```

Add `DASH_VARIANT_MAP` to the variant enum in `dash_map.h` beside the existing
variants, so `dash_box_hold` keyed on the layer's current contents does not
mistake a map for a box.

- [ ] **Step 5: Run tests to verify they pass**

```bash
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c \
    saturn/src/video/dash_map.c && /tmp/tdm
gcc -O2 -I saturn/src -o /tmp/tdt saturn/tests/test_dash_tiles.c \
    saturn/src/video/dash_tiles.c && /tmp/tdt
```

Expected: `test_dash_map: ok`. `test_dash_tiles` will fail until Task 9
generates the new tiles; that is expected and Task 9 closes it.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/video/dash_map.h saturn/src/video/dash_map.c \
        saturn/tests/test_dash_map.c
git commit -m "Give the tile layer a paint entry point for the map, since dash_box holds exactly one rectangle and a map is many cells, and claim the layer under its own variant so a held box is never mistaken for one."
```

---

### Task 9: Generate the map tiles

**Files:**
- Modify: `tools/gen_dash_tiles.py`
- Modify: `saturn/src/video/dash_tiles.c`, `saturn/src/video/dash_tiles.h` (generated)
- Test: `saturn/tests/test_dash_tiles.c`

**Interfaces:**
- Consumes: `DT_ROOM`, `DT_ROOM_HERE`, `DT_LINK_H`, `DT_LINK_V`,
  `DT_LINK_STAIR` from Task 8.
- Produces: pixel data for those five tiles; `DT_N` grows by five.

Drawn in the original's idiom rather than ripped from the Japanese disc — the
spec's stated default, which keeps a redistribution question closed.

A tile in this generator is an 8x8 grid of palette indices 0-15 — `blank()`
returns `[[0] * 8 for _ in range(8)]` — and `build()` appends them in DT order.
The six new ones are plain grids, so they need no new helper.

- [ ] **Step 1: Confirm the shape before editing**

```bash
sed -n '94,112p' tools/gen_dash_tiles.py
```

Expected: `blank()` returning a nibble grid, and `build()` starting
`tiles = [blank()]`. The new tiles append at the end of `build()`, in the same
DT order the header declares.

- [ ] **Step 2: Add the six tiles to the generator**

Insert into `tools/gen_dash_tiles.py` above `build()`:

```python
def solid(idx, x0, y0, x1, y1, base=None):
    """An 8x8 nibble grid with the inclusive box (x0,y0)-(x1,y1) set to idx."""
    t = base if base is not None else blank()
    return [[idx if (x0 <= x <= x1 and y0 <= y <= y1) else t[y][x]
             for x in range(8)] for y in range(8)]
```

Then append at the end of `build()`, immediately before it returns, in DT
order. `EDGE_RP` is the frame ramp the existing tiles already use, so the map
sits in the same palette as the rest of the chrome:

```python
    ground = field(EDGE_RP, 0)                                  # DT_GROUND
    tiles.append(ground)

    room = solid(9, 1, 1, 6, 6, base=ground)
    room = solid(4, 2, 2, 5, 5, base=room)
    tiles.append(room)                                          # DT_ROOM

    here = solid(15, 1, 1, 6, 6, base=ground)
    here = solid(12, 2, 2, 5, 5, base=here)
    tiles.append(here)                                          # DT_ROOM_HERE

    tiles.append(solid(9, 0, 3, 7, 4, base=ground))             # DT_LINK_H
    tiles.append(solid(9, 3, 0, 4, 7, base=ground))             # DT_LINK_V

    stair = solid(9, 3, 0, 4, 1, base=ground)
    stair = solid(9, 3, 3, 4, 4, base=stair)
    stair = solid(9, 3, 6, 4, 7, base=stair)
    tiles.append(stair)                                         # DT_LINK_STAIR
```

Every tile is laid over `ground` rather than over `blank()`, so the field's own
texture runs behind the marks exactly as it runs behind the panel's frame — the
same decision the marble chrome already made.

- [ ] **Step 3: Regenerate and confirm the generator is deterministic**

```bash
python tools/gen_dash_tiles.py
cp saturn/src/video/dash_tiles.c /tmp/tiles_a.c
python tools/gen_dash_tiles.py
diff /tmp/tiles_a.c saturn/src/video/dash_tiles.c && echo "byte-identical"
```

Expected: `byte-identical`. A generator that is not deterministic cannot be
reviewed by diff, which is how every other generated file here is reviewed.

- [ ] **Step 4: Extend the tile test**

Append inside `main()` in `saturn/tests/test_dash_tiles.c`, before its final
`printf`, following how that file already indexes the tile blob:

```c
    /* The six map tiles exist and none is silently blank -- an empty tile
       draws as a hole in the map. dash_tile_data is [DT_N][32]: 8x8 pixels at
       4bpp, two pixels to the byte. */
    {
        int t;
        for (t = DT_GROUND; t <= DT_LINK_STAIR; t++) {
            int i, nonzero = 0;
            for (i = 0; i < 32; i++)
                if (dash_tile_data[t][i] != 0) { nonzero = 1; break; }
            assert(nonzero);
        }
    }

    /* The marks must differ from the ground they sit on, or the map is a
       flat field with nothing drawn on it. */
    {
        int i, differs = 0;
        for (i = 0; i < 32; i++)
            if (dash_tile_data[DT_ROOM][i] != dash_tile_data[DT_GROUND][i])
                { differs = 1; break; }
        assert(differs);
    }
```

- [ ] **Step 5: Run the tests**

```bash
gcc -O2 -I saturn/src -o /tmp/tdt saturn/tests/test_dash_tiles.c \
    saturn/src/video/dash_tiles.c && /tmp/tdt
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c \
    saturn/src/video/dash_map.c && /tmp/tdm
```

Expected: both pass.

- [ ] **Step 6: Commit**

```bash
git add tools/gen_dash_tiles.py saturn/src/video/dash_tiles.c \
        saturn/src/video/dash_tiles.h saturn/tests/test_dash_tiles.c
git commit -m "Generate the five map tiles in the original's idiom rather than ripping them from the Japanese disc, and assert none of them is silently blank since an empty tile draws as a hole in the map."
```

---

### Task 10: The map screen

**Files:**
- Create: `saturn/src/video/map_view.h`, `saturn/src/video/map_view.cxx`

**Interfaces:**
- Consumes: `map_model_*` (Tasks 1-4), `room_model_object_name` (Task 6),
  `dash_map_begin`/`dash_map_paint`/`DT_GROUND` (Tasks 8-9), `text_print_str`,
  `text_clear_line`, `text_flush`, `menu_wait`, `dash_tint`,
  `title_bg_hide`, `title_bg_show`, `title_bg_loaded_file`.
- Produces: `map_view_show(void)`.

One room is 4 text cells. The viewport is 10 x 7 rooms; the player sits at
room-cell (5, 3), which is text cell (20, 12).

- [ ] **Step 1: Write the header**

Create `saturn/src/video/map_view.h`:

```c
/*----------------------
 | map_view.h
 | Description: The in-game map's screen: the ground, the room marks and links
 |   on the tile layer, the labels on the text layer, and the figure fixed at
 |   the centre. Owns every hardware write the map makes; map_model.c owns the
 |   geometry. Implemented in map_view.cxx.
 | Author: suinevere
 | Dependencies: map_model.h, dash_map.h, text_map.h, menu.h
 ----------------------*/
#ifndef MAP_VIEW_H
#define MAP_VIEW_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | map_view_show
 | Description: Draws the map and holds it until the player backs out, then
 |   returns with the screen cleared. Advances the frame through menu_sync so
 |   sound and music do not stall for as long as the map is up.
 | Author: suinevere
 | Dependencies: map_model.h, dash_map.h, text_map.h, menu.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void map_view_show(void);

#ifdef __cplusplus
}
#endif
#endif /* MAP_VIEW_H */
```

- [ ] **Step 2: Write the screen**

Create `saturn/src/video/map_view.cxx`. Read `menu_pages.cxx`'s credits pager
first for how a full-screen page holds itself, and match it:

```cpp
/*----------------------
 | map_view.cxx
 | Description: Implements map_view.h.
 | Author: suinevere
 ----------------------*/
extern "C" {
#include "map_model.h"
#include "dash_map.h"
#include "../engine/room_model.h"
}
#include "text_map.h"
#include "dash_view.h"
#include "title.h"
#include "../menu/menu.h"
#include "map_view.h"

/*----------------------
 | MAP_CELLS / MAP_VIEW_W / MAP_VIEW_H / MAP_CX / MAP_CY / MAP_TOP
 | Description: One room is four text cells -- the original's 32-pixel step
 |   over an 8x8 font -- so a 320x224 screen shows ten rooms by seven, and the
 |   player sits at the middle one. Seven rooms of four rows is exactly the 28
 |   rows the screen has, so MAP_TOP is zero and the map fills it; it exists as
 |   a name rather than a bare 0 because the ground and the marks must agree on
 |   it, and they did not in an earlier draft.
 | Author: suinevere
 ----------------------*/
#define MAP_CELLS   4
#define MAP_VIEW_W  10
#define MAP_VIEW_H  7
#define MAP_CX      5
#define MAP_CY      3
#define MAP_TOP     0

/*----------------------
 | MAP_GROUND_555
 | Description: The tan the ground is tinted to, as a VDP2 BGR555 word. The
 |   tiles are 4bpp indices into palette 1, so the whole ground is these
 |   sixteen CRAM entries and nothing in VRAM moves -- the same arithmetic the
 |   marble chrome already uses to sit on a coloured background.
 | Author: suinevere
 ----------------------*/
#define MAP_GROUND_555 0x2B5Eu

/*----------------------
 | draw_once
 | Description: Paints one frame of the map: every placed room within the
 |   viewport, the links between them, the figure at the centre, and the
 |   current room's name along the bottom.
 | Author: suinevere
 | Dependencies: map_model.h, dash_map.h, text_map.h, room_model.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void draw_once(void) {
    int n = map_model_count(), i, j;

    dash_map_begin();

    for (i = 0; i < MAP_VIEW_H * MAP_CELLS; i++) {
        int c;
        for (c = 0; c < MAP_VIEW_W * MAP_CELLS; c++)
            dash_map_paint(c, MAP_TOP + i, DT_GROUND);
    }

    for (i = 0; i < n; i++) {
        unsigned short r = 0;
        int dx = 0, dy = 0;
        if (!map_model_room_at(i, &r)) continue;
        if (!map_model_offset(r, &dx, &dy)) continue;
        if (dx < -MAP_CX || dx >= MAP_VIEW_W - MAP_CX) continue;
        if (dy < -MAP_CY || dy >= MAP_VIEW_H - MAP_CY) continue;

        int cx = (MAP_CX + dx) * MAP_CELLS;
        int cy = MAP_TOP + (MAP_CY + dy) * MAP_CELLS;
        dash_map_paint(cx, cy,
                       r == map_model_current() ? DT_ROOM_HERE : DT_ROOM);

        for (j = 0; j < n; j++) {
            unsigned short s = 0;
            int ex = 0, ey = 0, kind;
            if (!map_model_room_at(j, &s) || s == r) continue;
            kind = map_model_link(r, s);
            if (kind == MAP_LINK_NONE) continue;
            if (!map_model_offset(s, &ex, &ey)) continue;
            if (ex == dx && ey == dy + 1)
                dash_map_paint(cx, cy + 2,
                               kind == MAP_LINK_VERT ? DT_LINK_STAIR : DT_LINK_V);
            else if (ex == dx + 1 && ey == dy)
                dash_map_paint(cx + 2, cy,
                               kind == MAP_LINK_VERT ? DT_LINK_STAIR : DT_LINK_H);
        }
    }

    {
        char nm[40];
        text_clear_line(1);
        text_clear_line(26);
        text_print_str(2, 1, "MAP");
        if (room_model_object_name(map_model_current(), nm, (int) sizeof nm))
            text_print_str(2, 26, nm);
        text_print_str(2, 27, "B/Esc: back");
        text_flush();
    }
}

/*----------------------
 | map_view_show
 | Description: See map_view.h.
 | Author: suinevere
 | Dependencies: draw_once, menu.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void map_view_show(void) {
    MenuBacking backing;
    char was[64];
    int i = 0;
    const char *cur = title_bg_loaded_file();
    while (cur[i] && i < (int) sizeof was - 1) { was[i] = cur[i]; i++; }
    was[i] = 0;

    title_bg_hide();
    dash_tint(MAP_GROUND_555);
    draw_once();
    menu_wait();

    text_clear_line(1);
    text_clear_line(26);
    text_clear_line(27);
    text_flush();
    if (was[0]) title_bg_show(was);
}
```

The wallpaper is hidden rather than drawn over, because the ground is the tile
layer's own field and a picture behind it would show through wherever the map
paints nothing. Re-showing `title_bg_loaded_file()` on the way out is the one
wallpaper request that cannot touch the disc — `title_bg_show` compares against
that same record and short-circuits — so leaving the map never moves the CD
head.

`menu_wait` already advances the frame through `menu_sync`, which is what keeps
sound and music running while a screen is held. If `MenuBacking` is not the
right holder here, use whatever the credits pager uses — read it and match.

- [ ] **Step 3: Syntax-check**

```bash
cd saturn && sh syntax-check.sh src/video/map_view.cxx
```

Expected: no errors.

- [ ] **Step 4: Confirm the model tests still pass**

```bash
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
```

Expected: `test_map_model: ok`

- [ ] **Step 5: Commit**

```bash
git add saturn/src/video/map_view.h saturn/src/video/map_view.cxx
git commit -m "Draw the map with the player's room at the centre of a ten-by-seven viewport, four text cells to the room so the original's thirty-two-pixel step lands exactly on the text grid, and hold the screen through the wait that keeps sound running."
```

---

### Task 11: The Map row

**Files:**
- Modify: `saturn/src/menu/menu_pages.cxx:1450-1470`

**Interfaces:**
- Consumes: `map_view_show` (Task 10).
- Produces: nothing new.

- [ ] **Step 1: Add the enum entry and its label**

In `saturn/src/menu/menu_pages.cxx`, extend the options enum and its label
table together — they are indexed by the same value, so they must change in one
edit:

```cpp
    enum { OI_RESUME, OI_MAP, OI_SAVE, OI_LOAD, OI_GAMEPLAY, OI_DISPLAY,
           OI_SOUND, OI_CONTROLS, OI_NETWORK, OI_RETURN, OI_N };
    static const char *const OI_LABEL[OI_N] = {
        "Resume", "Map", "Save Game", "Load Game", "Gameplay", "Display",
        "Sound", "Controls", "Network", "Title Screen"
    };
```

- [ ] **Step 2: Offer it in game only**

Directly after the existing `if (g_in_game) items[nitems++] = OI_RESUME;`:

```cpp
    if (g_in_game) items[nitems++] = OI_MAP;
```

- [ ] **Step 3: Handle the selection**

The dispatch at `menu_pages.cxx:1510` is an if/else chain over `item`, not a
switch. Insert a branch between the `OI_RESUME` and `OI_SAVE` arms:

```cpp
            if (item == OI_RESUME) break;   // OM_NONE: exactly what backing out does
            else if (item == OI_MAP) { map_view_show(); continue; }
            else if (item == OI_SAVE) { result = OM_SAVE; break; }
```

`continue` rather than `break`, because the map is a page the Options menu
opens and returns from — unlike Save and Load, which close the menu. Confirm
the enclosing loop makes `continue` re-draw the menu; if it does not, redraw
before continuing.

Add the include at the top of the file, beside the other view includes:

```cpp
#include "../video/map_view.h"
```

- [ ] **Step 4: Syntax-check**

```bash
cd saturn && sh syntax-check.sh src/menu/menu_pages.cxx src/video/map_view.cxx
```

Expected: no errors.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/menu/menu_pages.cxx
git commit -m "Offer the map from the in-game Options menu under the same condition Save and Load already use, since a map of nowhere is what it would be on the title screen."
```

---

### Task 12: Persistence, and the netbin gate

**Files:**
- Modify: `saturn/src/engine/map_model.h`, `saturn/src/engine/map_model.c`
- Modify: `saturn/src/engine/saturn_glue.cxx:747-815`
- Modify: `saturn/src/video/map_view.h`
- Test: `saturn/tests/test_map_model.c`

**Interfaces:**
- Produces: `map_model_serialize(unsigned char *out, unsigned int max) -> unsigned int`,
  `map_model_deserialize(const unsigned char *in, unsigned int len) -> int`.

Z-machine saves do not carry map state, so a restore can land the player in a
room the map has never seen. The map goes in a companion backup file named by
appending `M` to the slot name — `make_slot_name` uses at most 10 of the 11
characters available, which is exactly one byte of headroom.

- [ ] **Step 1: Write the failing test**

Append inside `main()` in `saturn/tests/test_map_model.c`, before the `printf`:

```c
    /* A saved map restores to the same picture, and a truncated or foreign
       blob is refused rather than half-loaded. */
    {
        unsigned char blob[2048];
        unsigned int len;
        int px, py;

        map_model_reset();
        RoomModel s1 = mk(12); link1(&s1, RM_N, 7);
        RoomModel s2 = mk(7);  link1(&s2, RM_S, 12);
        map_model_enter(&s1);
        map_model_enter(&s2);
        assert(map_model_pos(7, &px, &py) && px == 0 && py == -1);

        len = map_model_serialize(blob, sizeof blob);
        assert(len > 0);

        map_model_reset();
        assert(!map_model_visited(7));
        assert(map_model_deserialize(blob, len));
        assert(map_model_visited(7) && map_model_visited(12));
        assert(map_model_pos(7, &px, &py) && px == 0 && py == -1);
        assert(map_model_current() == 7);

        /* Refused, and the model left empty rather than half-filled. */
        map_model_reset();
        assert(!map_model_deserialize(blob, len - 1));
        assert(map_model_count() == 0);
        blob[0] = 0xEE;
        assert(!map_model_deserialize(blob, len));
        assert(map_model_count() == 0);

        /* Too small a buffer is refused rather than overrun. */
        assert(map_model_serialize(blob, 3) == 0);
    }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
```

Expected: FAIL — `implicit declaration of function 'map_model_serialize'`.

- [ ] **Step 3: Declare the pair**

Add to `saturn/src/engine/map_model.h`:

```c
/*----------------------
 | MAP_BLOB_MAGIC / MAP_BLOB_MAX
 | Description: The serialised map's leading byte, so a foreign or stale blob
 |   is refused rather than decoded into nonsense, and the largest blob a full
 |   table produces: four header bytes and six per placed room.
 | Author: suinevere
 ----------------------*/
#define MAP_BLOB_MAGIC 0x4Du
#define MAP_BLOB_MAX   (4u + 6u * MAP_ROOM_MAX)

/*----------------------
 | map_model_serialize
 | Description: Writes the placed set and the current room into out.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_x, g_y, g_cur
 | Params: out -- receives the blob; max -- its capacity
 | Returns: the number of bytes written, or 0 when max is too small
 ----------------------*/
unsigned int map_model_serialize(unsigned char *out, unsigned int max);

/*----------------------
 | map_model_deserialize
 | Description: Replaces the model with a previously serialised one. Refuses a
 |   blob whose magic or length does not match and leaves the model empty
 |   rather than half-filled, because a half-restored map is worse than none.
 | Author: suinevere
 | Dependencies: map_model_reset
 | Globals: g_vis, g_x, g_y, g_cur, g_have_cur
 | Params: in -- the blob; len -- its length
 | Returns: 1 on success, 0 when refused
 ----------------------*/
int map_model_deserialize(const unsigned char *in, unsigned int len);
```

- [ ] **Step 4: Implement the pair**

Append to `saturn/src/engine/map_model.c`:

```c
/*----------------------
 | map_model_serialize
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_count
 | Globals: g_vis, g_x, g_y, g_cur
 | Params: out -- receives the blob; max -- its capacity
 | Returns: bytes written, or 0
 ----------------------*/
unsigned int map_model_serialize(unsigned char *out, unsigned int max) {
    unsigned int n = 0;
    int i, cnt = map_model_count();
    if (max < 4u + 6u * (unsigned int) cnt) return 0;
    out[n++] = (unsigned char) MAP_BLOB_MAGIC;
    out[n++] = (unsigned char) cnt;
    out[n++] = (unsigned char) (g_cur >> 8);
    out[n++] = (unsigned char) (g_cur & 0xFF);
    for (i = 0; i < MAP_ROOM_MAX; i++) {
        if (!g_vis[i]) continue;
        out[n++] = (unsigned char) (i >> 8);
        out[n++] = (unsigned char) (i & 0xFF);
        out[n++] = (unsigned char) ((unsigned short) g_x[i] >> 8);
        out[n++] = (unsigned char) ((unsigned short) g_x[i] & 0xFF);
        out[n++] = (unsigned char) ((unsigned short) g_y[i] >> 8);
        out[n++] = (unsigned char) ((unsigned short) g_y[i] & 0xFF);
    }
    return n;
}

/*----------------------
 | map_model_deserialize
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_reset
 | Globals: g_vis, g_x, g_y, g_cur, g_have_cur
 | Params: in -- the blob; len -- its length
 | Returns: 1 on success, 0 when refused
 ----------------------*/
int map_model_deserialize(const unsigned char *in, unsigned int len) {
    unsigned int n = 4, k;
    int cnt;
    if (len < 4u || in[0] != (unsigned char) MAP_BLOB_MAGIC) return 0;
    cnt = in[1];
    if (len != 4u + 6u * (unsigned int) cnt) return 0;
    map_model_reset();
    g_cur = (unsigned short) ((in[2] << 8) | in[3]);
    g_have_cur = 1;
    for (k = 0; k < (unsigned int) cnt; k++) {
        unsigned short r = (unsigned short) ((in[n] << 8) | in[n + 1]);
        short px = (short) ((in[n + 2] << 8) | in[n + 3]);
        short py = (short) ((in[n + 4] << 8) | in[n + 5]);
        n += 6;
        if (!in_range(r)) { map_model_reset(); return 0; }
        g_vis[r] = 1; g_x[r] = px; g_y[r] = py;
    }
    return 1;
}
```

`map_model_reset` clears `g_have_prev`, so the first prompt after a restore
infers no direction and the room the player is standing in is already placed —
which is correct, and why the restore does not need the previous snapshot.

- [ ] **Step 5: Run test to verify it passes**

```bash
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
```

Expected: `test_map_model: ok`

- [ ] **Step 6: Write the companion file on save**

In `saturn/src/engine/saturn_glue.cxx`, immediately after
`int ok = saturn_bup_write(device, name, comment, data, len);`:

```cpp
    if (ok) {
        char mname[12];
        unsigned char mblob[MAP_BLOB_MAX];
        unsigned int mlen;
        int i = 0;
        while (name[i] && i < 10) { mname[i] = name[i]; i++; }
        mname[i++] = 'M';
        mname[i] = 0;
        mlen = map_model_serialize(mblob, sizeof mblob);
        if (mlen > 0) saturn_bup_write(device, mname, "map", mblob, mlen);
    }
```

A failed map write is deliberately not reported: the game save succeeded, and
telling the player their save failed because a map did not fit would be a lie.

- [ ] **Step 7: Read it back on restore**

In the same file, immediately after
`int ok = saturn_bup_read(device, name, buf);`:

```cpp
    if (ok) {
        char mname[12];
        unsigned char mblob[MAP_BLOB_MAX];
        int i = 0;
        while (name[i] && i < 10) { mname[i] = name[i]; i++; }
        mname[i++] = 'M';
        mname[i] = 0;
        if (!saturn_bup_read(device, mname, mblob) ||
            !map_model_deserialize(mblob, map_model_serialize_len(mblob)))
            map_model_reset();
    }
```

`saturn_bup_read` does not report the stored length, so the blob's own header
must supply it. Declare this in `map_model.h` beside the other two —

```c
unsigned int map_model_serialize_len(const unsigned char *in);
```

— carrying the same header block as its definition, and define it in
`map_model.c`:

```c
/*----------------------
 | map_model_serialize_len
 | Description: The length a serialised map claims, read from its own header,
 |   for a reader that is handed a buffer without a length -- which is what
 |   saturn_bup_read gives back.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: in -- at least four bytes of a blob
 | Returns: the claimed length, or 0 when the magic does not match
 ----------------------*/
unsigned int map_model_serialize_len(const unsigned char *in) {
    if (in[0] != (unsigned char) MAP_BLOB_MAGIC) return 0;
    return 4u + 6u * (unsigned int) in[1];
}
```

- [ ] **Step 8: Keep the netbin untouched**

Add to the bottom of `saturn/src/video/map_view.h`, before the closing
`#endif`, so nothing in the netbin acquires a link edge to the screen:

```c
#ifdef NETBIN
#define map_view_show() ((void) 0)
#endif
```

Then confirm the netbin's pinned source list has not moved:

```bash
python saturn/tests/test_netbin_sources.py
```

Expected: PASS, with the same object count as before this branch. **Do not add
`map_model.c` to the netbin list.** The spec asks for the model there, but only
the author can run the measured clean rebuild that would justify it, and the
dashboard design was reversed once already for guessing at exactly this. Record
the measurement as a follow-up.

- [ ] **Step 9: Syntax-check and run every host test**

```bash
cd saturn && sh syntax-check.sh src/engine/saturn_glue.cxx src/engine/map_model.c \
    src/video/map_view.cxx src/menu/menu_pages.cxx
cd .. && gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c && /tmp/tmm
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c \
    saturn/src/video/dash_map.c && /tmp/tdm
gcc -O2 -I saturn/src -o /tmp/tdt saturn/tests/test_dash_tiles.c \
    saturn/src/video/dash_tiles.c && /tmp/tdt
python saturn/tests/test_netbin_sources.py
```

Expected: all pass.

- [ ] **Step 10: Commit**

```bash
git add saturn/src/engine/map_model.h saturn/src/engine/map_model.c \
        saturn/src/engine/saturn_glue.cxx saturn/src/video/map_view.h \
        saturn/tests/test_map_model.c
git commit -m "Save the map beside each slot in a companion backup file named with the one character of headroom the slot names leave, refuse a blob whose magic or length does not match rather than half-load it, and reset to the current room alone when a restore has no map to match."
```

---

## Follow-ups, not in this plan

- **The netbin measurement.** A clean `compile-netbin.bat clean` rebuild with
  `map_model.c` added, reported in bytes, decides whether the model ships
  there. Until then the netbin is untouched.
- **UP/DOWN/IN/OUT on screen.** The spec calls this the weakest part of the
  design and expects revision once it is visible. Revisit after the author has
  seen it running.
- **Ripped cel art.** If the redistribution question is answered yes, cels
  28-32 can replace the generated tiles without touching the model.
