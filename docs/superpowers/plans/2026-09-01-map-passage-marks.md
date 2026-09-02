# Map Passage Marks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Draw four marks the map does not currently draw -- a one-way arrowhead, a `U`/`D` on an exit that leaves the shown floor, a dashed run for a conditional passage or the edge of the drawing, and a circle for an exit that returns you to the room you left.

**Architecture:** Every mark is derived from the story's own exit graph at draw time; nothing is scanned or authored. `map_model` gains a per-room exit enumeration that carries the three flags the graph already implies. The edge accumulator comes out of `map_view.cxx` into an SRL-free `map_edges` so it can be host-tested, then its pair loop is replaced by per-room enumeration -- proven byte-identical first, extended second.

**Tech Stack:** C89-style C for the engine and video units, C++ only in `map_view.cxx`, Python 3 for the tile generator, plain `gcc` for host tests, `compile.bat` for the two Saturn targets.

**Spec:** `docs/superpowers/specs/2026-09-01-map-passage-marks-design.md`

## Global Constraints

- **Author of record is `suinevere`.** Every file, method and constant gets a header comment block in the form CLAUDE.md specifies (`| Description: | Author: | Dependencies: | Globals: | Params: | Returns:`, `N/A` for fields that do not apply). Tests and generated files get a file header only.
- **No comments inside function bodies.** Prose stays in the header block, one sentence, saying the non-obvious thing once.
- **Commit after every task.** One sentence, no body, no bullets, no trailers. Never mention Claude, AI, or the session; no `Claude-Session:` line and no `claude.ai/code` URL.
- **`tools/gen_map_atlas.py`, `map_atlas.c/.h` and `map_atlas_data.inc` are not touched by any task in this plan.**
- **`MAP_BLOB_MAGIC` and `MAP_BLOB_MAX` do not change.** Nothing added here is serialised; `map_model_rebind_exits` refills it after a restore.
- **No existing tile index may move.** All 27 new tiles append after `DT_KNIGHT0 + 6`. `DT_KNIGHT0` stays 109 and `DT_N` goes 115 to 142.
- **Engine and video units stay free of SRL includes** so the host tests link. `map_view.cxx` is the only file in this plan that may include SRL.
- Host tests build with `gcc -O2 -I saturn/src` and each prints `test_<name>: ok` on success.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `saturn/src/engine/map_model.h` | `MapExit`, `MAP_EXIT_*`, `map_model_exits` declaration | 1 |
| `saturn/src/engine/map_model.c` | `g_cond`, conditional-preserving `record_exits`, `has_reverse`, `map_model_exits` | 1 |
| `saturn/src/video/map_layout.h` | `map_layout_cell_free`, `map_layout_glyph` -- pure viewport arithmetic | 2 |
| `tools/gen_dash_tiles.py` | 27 new tiles: dashed links, arrowheads, `U`/`D`, loop | 3 |
| `saturn/src/video/dash_map.h` | tile enum entries and `DT_N` | 3 |
| `saturn/src/video/dash_tiles.c` | regenerated tile data | 3 |
| `saturn/src/video/map_edges.h` / `.c` | **new.** Accumulates lines and marks in viewport cell space and answers which tile each cell wants. SRL-free, host-tested. | 4 |
| `saturn/src/video/map_view.cxx` | gathers rooms, feeds `map_edges`, paints through `dash_map_paint` | 4, 5, 6 |
| `saturn/tests/test_map_model.c` | exit enumeration and flag derivation | 1 |
| `saturn/tests/test_map_layout.c` | glyph placement fallback ladder | 2 |
| `saturn/tests/test_dash_tiles.c` | tile count, no renumbering, stipple correctness | 3 |
| `saturn/tests/test_map_edges.c` | **new.** Route choice, equivalence, tile selection | 4, 5, 6 |

`map_edges` exists because the equivalence test the spec promises is otherwise impossible: `map_view.cxx` includes SRL and cannot be run on the host, so the accumulator it holds cannot be checked. Extracting it is the same split the codebase already made for `map_layout.h`, for the same stated reason.

---

### Task 1: `map_model_exits`

**Files:**
- Modify: `saturn/src/engine/map_model.h` (add after the `MAP_LINK_*` block, around line 164)
- Modify: `saturn/src/engine/map_model.c:106` (add `g_cond`), `:120` (reset it), `:324-333` (`record_exits`), and append `has_reverse` + `map_model_exits`
- Test: `saturn/tests/test_map_model.c`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `MapExit { unsigned short dest; unsigned char dir; unsigned char kind; unsigned char flags; }`, the constants `MAP_EXIT_COND` (1), `MAP_EXIT_ONEWAY` (2), `MAP_EXIT_SELF` (4), and `int map_model_exits(unsigned short room, MapExit *out, int max)` returning the count written. Task 5 and Task 6 call it.

- [ ] **Step 1: Write the failing tests**

Add to `saturn/tests/test_map_model.c`, immediately above the final `printf("test_map_model: ok\n");`. The existing `mk` and `link1` helpers are reused; add `link_kind` beside `link1` near the top of the file:

```c
/* link1 always opens an exit. Three of the four exit states matter to the
   flags map_model_exits derives, so this is the general form. */
static void link_kind(RoomModel *m, int dir, unsigned short dest, int state) {
    m->exits[dir] = (unsigned char) state;
    m->dest[dir]  = dest;
}
```

Then the cases:

```c
    /* One-way, conditional, self-loop and the reverse-blocked exception. */
    {
        MapExit ex[RM_DIR_N];
        int n, i, seen;

        map_model_reset();

        /* 20 leads east to 21 and 21 leads back west: two-way. */
        { RoomModel a = mk(20); link1(&a, RM_E, 21); map_model_enter(&a); }
        { RoomModel b = mk(21); link1(&b, RM_W, 20); map_model_enter(&b); }

        n = map_model_exits(20, ex, RM_DIR_N);
        assert(n == 1);
        assert(ex[0].dest == 21);
        assert(ex[0].dir == RM_E);
        assert(ex[0].kind == MAP_LINK_FLAT);
        assert((ex[0].flags & MAP_EXIT_ONEWAY) == 0);

        /* 30 leads east to 31, and 31 leads nowhere: one-way. */
        map_model_reset();
        { RoomModel a = mk(30); link1(&a, RM_E, 31); map_model_enter(&a); }
        { RoomModel b = mk(31); map_model_enter(&b); }
        n = map_model_exits(30, ex, RM_DIR_N);
        assert(n == 1);
        assert(ex[0].flags & MAP_EXIT_ONEWAY);

        /* A shut door back is still a way back, so this is not one-way. The
           arrow must not appear and vanish as the player opens things. */
        map_model_reset();
        { RoomModel a = mk(40); link1(&a, RM_E, 41); map_model_enter(&a); }
        { RoomModel b = mk(41);
          link_kind(&b, RM_W, 40, RM_EXIT_BLOCKED); map_model_enter(&b); }
        n = map_model_exits(40, ex, RM_DIR_N);
        assert(n == 1);
        assert((ex[0].flags & MAP_EXIT_ONEWAY) == 0);

        /* RM_EXIT_MAYBE survives record_exits and shows as COND. */
        map_model_reset();
        { RoomModel a = mk(50);
          link_kind(&a, RM_E, 51, RM_EXIT_MAYBE); map_model_enter(&a); }
        { RoomModel b = mk(51); link1(&b, RM_W, 50); map_model_enter(&b); }
        n = map_model_exits(50, ex, RM_DIR_N);
        assert(n == 1);
        assert(ex[0].flags & MAP_EXIT_COND);

        /* An open exit is not conditional. */
        map_model_reset();
        { RoomModel a = mk(52); link1(&a, RM_E, 53); map_model_enter(&a); }
        { RoomModel b = mk(53); link1(&b, RM_W, 52); map_model_enter(&b); }
        n = map_model_exits(52, ex, RM_DIR_N);
        assert((ex[0].flags & MAP_EXIT_COND) == 0);

        /* An exit leading back to its own room is a self-loop, and is never
           also reported as one-way. */
        map_model_reset();
        { RoomModel a = mk(60); link1(&a, RM_N, 60); map_model_enter(&a); }
        n = map_model_exits(60, ex, RM_DIR_N);
        assert(n == 1);
        assert(ex[0].dest == 60);
        assert(ex[0].flags & MAP_EXIT_SELF);
        assert((ex[0].flags & MAP_EXIT_ONEWAY) == 0);

        /* A destination nobody has placed has no exits on record, so it must
           not be read as having no way back. Guessing one-way from absent
           evidence would arrow half the map on the first move. */
        map_model_reset();
        { RoomModel a = mk(70); link1(&a, RM_E, 71); map_model_enter(&a); }
        n = map_model_exits(70, ex, RM_DIR_N);
        assert(n == 1);
        assert((ex[0].flags & MAP_EXIT_ONEWAY) == 0);

        /* A vertical exit keeps its kind and its direction, which is what
           lets a caller say U rather than D. */
        map_model_reset();
        { RoomModel a = mk(80); link1(&a, RM_DOWN, 81); map_model_enter(&a); }
        { RoomModel b = mk(81); link1(&b, RM_UP, 80); map_model_enter(&b); }
        n = map_model_exits(80, ex, RM_DIR_N);
        assert(n == 1);
        assert(ex[0].kind == MAP_LINK_VERT);
        assert(ex[0].dir == RM_DOWN);

        /* Every exit is reported, and max is honoured. */
        map_model_reset();
        { RoomModel a = mk(90);
          link1(&a, RM_N, 91); link1(&a, RM_E, 92); link1(&a, RM_S, 93);
          map_model_enter(&a); }
        assert(map_model_exits(90, ex, RM_DIR_N) == 3);
        assert(map_model_exits(90, ex, 2) == 2);

        /* An unvisited room reports nothing rather than reading a stale row. */
        assert(map_model_exits(200, ex, RM_DIR_N) == 0);

        seen = 0;
        n = map_model_exits(90, ex, RM_DIR_N);
        for (i = 0; i < n; i++) if (ex[i].dest == 92) seen = 1;
        assert(seen);

        /* map_model_link is unchanged by any of this. It answers NONE for an
           endpoint nobody has entered, so 91 has to be placed before the pair
           is a pair at all. */
        { RoomModel b = mk(91); link1(&b, RM_S, 90); map_model_enter(&b); }
        assert(map_model_link(90, 91) == MAP_LINK_FLAT);
        assert(map_model_link(90, 200) == MAP_LINK_NONE);
    }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c saturn/src/engine/map_model.c saturn/src/engine/map_atlas.c && /tmp/tmm`
Expected: FAIL at compile with `unknown type name 'MapExit'` and `implicit declaration of function 'map_model_exits'`.

- [ ] **Step 3: Declare the interface**

In `saturn/src/engine/map_model.h`, immediately after the `MAP_LINK_NONE`/`FLAT`/`VERT` block:

```c
/*----------------------
 | MAP_EXIT_COND / MAP_EXIT_ONEWAY / MAP_EXIT_SELF
 | Description: What the exit graph already implies about a passage, beside
 |   which way it runs. COND is RM_EXIT_MAYBE and draws dashed; ONEWAY is an
 |   exit with no way back and draws an arrowhead; SELF is an exit whose
 |   destination is the room it left.
 |
 |   A blocked exit back counts as a way back, so a shut door is not one-way.
 |   Counting it would make the arrowhead appear and vanish as the player
 |   opens and closes things, and a mark that flickers teaches nothing.
 | Author: suinevere
 ----------------------*/
#define MAP_EXIT_COND   1
#define MAP_EXIT_ONEWAY 2
#define MAP_EXIT_SELF   4

/*----------------------
 | MapExit
 | Description: One exit out of one room, as the map needs to draw it. The
 |   direction is kept rather than collapsed because a caller has to say U or D,
 |   which map_model_link's three-value answer cannot carry.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned short dest;
    unsigned char  dir;
    unsigned char  kind;
    unsigned char  flags;
} MapExit;

/*----------------------
 | map_model_exits
 | Description: Every exit out of a placed room, with the flags the graph
 |   implies. This is the shape the map draws from: a pairwise question cannot
 |   reach a self-loop or an exit whose far end is on another floor, because
 |   neither has a second room on this one to be a pair with.
 |
 |   ONEWAY is set only when the destination is itself placed. An unplaced room
 |   has no exits on record, so reading its silence as "no way back" would
 |   arrow every passage the moment it was first walked.
 | Author: suinevere
 | Dependencies: map_model_visited
 | Globals: g_dest, g_kind, g_cond
 | Params: room -- object number; out -- receives the exits; max -- its length
 | Returns: how many exits were written, 0 when the room is not placed
 ----------------------*/
int map_model_exits(unsigned short room, MapExit *out, int max);
```

- [ ] **Step 4: Implement**

In `saturn/src/engine/map_model.c`, add the global beside `g_kind` at line 107:

```c
/*----------------------
 | g_cond
 | Description: Which of each placed room's exits are conditional, one bit per
 |   direction. Kept beside g_kind rather than folded into it because g_kind is
 |   the value map_model_link returns and widening it would change that
 |   function's contract. A short and not a char because RM_DIR_N is twelve.
 | Author: suinevere
 ----------------------*/
static unsigned short g_cond[MAP_ROOM_MAX];
```

In `map_model_reset` at line 120, add `g_cond[i] = 0;` to the per-room clear, beside `g_vis[i] = 0;`.

Replace the body of `record_exits`:

```c
static void record_exits(unsigned short room, const RoomModel *m) {
    int d;
    g_cond[room] = 0;
    for (d = 0; d < RM_DIR_N; d++) {
        g_dest[room][d] = m->dest[d];
        g_kind[room][d] = (unsigned char)
            (m->exits[d] == RM_EXIT_NONE ? MAP_LINK_NONE
             : (d >= RM_UP ? MAP_LINK_VERT : MAP_LINK_FLAT));
        if (m->exits[d] == RM_EXIT_MAYBE)
            g_cond[room] |= (unsigned short) (1u << d);
    }
}
```

Append after `map_model_link`:

```c
/*----------------------
 | has_reverse
 | Description: Whether b has any exit at all leading to a, in any state but
 |   RM_EXIT_NONE. g_kind is already RM_EXIT_NONE-filtered, so a blocked exit
 |   answers yes, which is what keeps a shut door off the one-way arrow.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_dest, g_kind
 | Params: a, b -- object numbers
 | Returns: 1 when b leads back to a, 0 otherwise
 ----------------------*/
static int has_reverse(unsigned short a, unsigned short b) {
    int d;
    for (d = 0; d < RM_DIR_N; d++)
        if (g_kind[b][d] != MAP_LINK_NONE && g_dest[b][d] == a) return 1;
    return 0;
}

/*----------------------
 | map_model_exits
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_visited, has_reverse
 | Globals: g_dest, g_kind, g_cond
 | Params: room -- object number; out -- receives the exits; max -- its length
 | Returns: how many exits were written
 ----------------------*/
int map_model_exits(unsigned short room, MapExit *out, int max) {
    int d, n = 0;
    if (room >= MAP_ROOM_MAX || !map_model_visited(room)) return 0;
    for (d = 0; d < RM_DIR_N && n < max; d++) {
        unsigned short dest = g_dest[room][d];
        if (g_kind[room][d] == MAP_LINK_NONE) continue;
        if (dest == 0 || dest >= MAP_ROOM_MAX) continue;
        out[n].dest  = dest;
        out[n].dir   = (unsigned char) d;
        out[n].kind  = g_kind[room][d];
        out[n].flags = 0;
        if (g_cond[room] & (unsigned short) (1u << d))
            out[n].flags |= MAP_EXIT_COND;
        if (dest == room)
            out[n].flags |= MAP_EXIT_SELF;
        else if (map_model_visited(dest) && !has_reverse(room, dest))
            out[n].flags |= MAP_EXIT_ONEWAY;
        n++;
    }
    return n;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c saturn/src/engine/map_model.c saturn/src/engine/map_atlas.c && /tmp/tmm`
Expected: PASS -- `test_map_model: ok`.

- [ ] **Step 6: Confirm the existing suite still passes**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tma saturn/tests/test_map_atlas.c saturn/src/engine/map_atlas.c && /tmp/tma
gcc -O2 -I saturn/src -I saturn/src/video -o /tmp/tml saturn/tests/test_map_layout.c && /tmp/tml
```
Expected: `test_map_atlas: ok` and `test_map_layout: ok`.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/engine/map_model.h saturn/src/engine/map_model.c saturn/tests/test_map_model.c
git commit -m "Report each placed room's exits with the flags its own graph implies -- conditional, one-way and self-loop -- and stop record_exits discarding RM_EXIT_MAYBE, counting a blocked exit back as a way back so a shut door never draws as one-way and never flagging one-way against a destination nobody has placed, whose silence is absent evidence rather than a missing passage."
```

---

### Task 2: `map_layout_glyph`

**Files:**
- Modify: `saturn/src/video/map_layout.h` (append before the closing `#endif`)
- Test: `saturn/tests/test_map_layout.c`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `int map_layout_cell_free(int x, int y, const unsigned short taken[][MAP_ROOMS_W * MAP_CELLS])` and `int map_layout_glyph(int mx, int my, int pdx, int pdy, const unsigned short taken[][MAP_ROOMS_W * MAP_CELLS], int *gx, int *gy)` returning 1 on placement. Task 6 calls `map_layout_glyph`.

The preferred direction is passed as a unit `(pdx, pdy)` rather than a `DT_EDGE_*` bit so this header keeps depending on nothing.

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_map_layout.c`, above its final `printf`:

```c
    /* Glyph placement: two cells out along the preferred direction, then one,
       then a diagonal, then decline. */
    {
        static unsigned short taken[MAP_ROOMS_H * MAP_CELLS][MAP_ROOMS_W * MAP_CELLS];
        int gx = -1, gy = -1, i, c;

        for (i = 0; i < MAP_ROOMS_H * MAP_CELLS; i++)
            for (c = 0; c < MAP_ROOMS_W * MAP_CELLS; c++) taken[i][c] = 0;

        /* Clear board: the far cell wins, so the stub runs its full length and
           stops one cell short of where a neighbouring room would sit. */
        assert(map_layout_glyph(20, 12, 0, -1, taken, &gx, &gy) == 1);
        assert(gx == 20 && gy == 10);

        /* Far cell occupied: fall back to the near one. */
        taken[10][20] = 1;
        assert(map_layout_glyph(20, 12, 0, -1, taken, &gx, &gy) == 1);
        assert(gx == 20 && gy == 11);

        /* Both occupied: fall back to a diagonal, which an orthogonal run
           reaches only by passing through one of the two cells just tried. */
        taken[11][20] = 1;
        assert(map_layout_glyph(20, 12, 0, -1, taken, &gx, &gy) == 1);
        assert(gx == 21 && gy == 11);

        /* Every candidate occupied: decline rather than overwrite a line. A
           missing mark is what gather already does with an off-floor far end. */
        taken[11][21] = 1; taken[13][21] = 1;
        taken[11][19] = 1; taken[13][19] = 1;
        assert(map_layout_glyph(20, 12, 0, -1, taken, &gx, &gy) == 0);

        /* The preferred direction is honoured, not assumed to be north. */
        for (i = 0; i < MAP_ROOMS_H * MAP_CELLS; i++)
            for (c = 0; c < MAP_ROOMS_W * MAP_CELLS; c++) taken[i][c] = 0;
        assert(map_layout_glyph(20, 12, 0, 1, taken, &gx, &gy) == 1);
        assert(gx == 20 && gy == 14);
        assert(map_layout_glyph(20, 12, 1, 0, taken, &gx, &gy) == 1);
        assert(gx == 22 && gy == 12);

        /* A candidate off the viewport is not free. A mark on the top row has
           no room above it and takes a diagonal instead. */
        assert(map_layout_glyph(20, 0, 0, -1, taken, &gx, &gy) == 1);
        assert(gy >= 0);
    }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `gcc -O2 -I saturn/src -I saturn/src/video -o /tmp/tml saturn/tests/test_map_layout.c && /tmp/tml`
Expected: FAIL at compile with `implicit declaration of function 'map_layout_glyph'`.

- [ ] **Step 3: Implement**

Append to `saturn/src/video/map_layout.h` before `#endif /* MAP_LAYOUT_H */`:

```c
/*----------------------
 | map_layout_cell_free
 | Description: Whether a cell is on the viewport and nothing has claimed it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: x, y -- the cell; taken -- the accumulated edge layer, nonzero where
 |   a line or a mark already sits
 | Returns: 1 when the cell is free, 0 otherwise
 ----------------------*/
static inline int map_layout_cell_free(int x, int y,
    const unsigned short taken[][MAP_ROOMS_W * MAP_CELLS])
{
    if (x < 0 || y < 0) return 0;
    if (x >= MAP_ROOMS_W * MAP_CELLS) return 0;
    if (y >= MAP_ROOMS_H * MAP_CELLS) return 0;
    return taken[y][x] == 0;
}

/*----------------------
 | map_layout_glyph
 | Description: Where a glyph annotating a mark goes: two cells out along the
 |   preferred direction, then one, then a diagonal, then nowhere.
 |
 |   Two cells is the first choice because a room is MAP_CELLS wide, so a stub
 |   ending there stops one cell short of anywhere a neighbouring room could
 |   sit -- which is what makes it read as the edge of the drawing rather than
 |   as a passage to something.
 |
 |   Declining is a real outcome and the caller must handle it. It is the same
 |   answer gather gives an exit whose far end is on another floor: a missing
 |   mark is honest where an invented one is not, and the alternative here is
 |   overwriting a line that means something.
 | Author: suinevere
 | Dependencies: map_layout_cell_free
 | Globals: N/A
 | Params: mx, my -- the mark's cell; pdx, pdy -- the preferred direction as a
 |   unit step; taken -- the accumulated edge layer; gx, gy -- receive the cell
 | Returns: 1 when a cell was found, 0 when every candidate was occupied
 ----------------------*/
static inline int map_layout_glyph(int mx, int my, int pdx, int pdy,
    const unsigned short taken[][MAP_ROOMS_W * MAP_CELLS], int *gx, int *gy)
{
    static const int DIAG[4][2] = { { 1, -1 }, { 1, 1 }, { -1, -1 }, { -1, 1 } };
    int i, cx = mx + 2 * pdx, cy = my + 2 * pdy;

    if (map_layout_cell_free(cx, cy, taken)) { *gx = cx; *gy = cy; return 1; }
    cx = mx + pdx; cy = my + pdy;
    if (map_layout_cell_free(cx, cy, taken)) { *gx = cx; *gy = cy; return 1; }
    for (i = 0; i < 4; i++) {
        cx = mx + DIAG[i][0];
        cy = my + DIAG[i][1];
        if (map_layout_cell_free(cx, cy, taken)) { *gx = cx; *gy = cy; return 1; }
    }
    return 0;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `gcc -O2 -I saturn/src -I saturn/src/video -o /tmp/tml saturn/tests/test_map_layout.c && /tmp/tml`
Expected: PASS -- `test_map_layout: ok`.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/video/map_layout.h saturn/tests/test_map_layout.c
git commit -m "Place a glyph annotating a room mark two cells out along its preferred direction, then one, then a diagonal, and decline rather than overwrite a line when every candidate is claimed -- two cells first because a room is four wide, so a stub ending there stops short of anywhere a neighbour could sit and reads as the edge of the drawing rather than as a passage."
```

---

### Task 3: The 27 new tiles

**Files:**
- Modify: `tools/gen_dash_tiles.py:16` (`N`), and `build()` after the `DT_KNIGHT0` block
- Modify: `saturn/src/video/dash_map.h:87-92` (the tail of the tile enum)
- Regenerate: `saturn/src/video/dash_tiles.c`
- Test: `saturn/tests/test_dash_tiles.c`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `DT_DASH0` (115, sixteen tiles indexed by the same `DT_EDGE_*` mask as `DT_LINK0`), `DT_ARROW_N`/`E`/`S`/`W` (131-134), `DT_ARROW_DASH_N`/`E`/`S`/`W` (135-138), `DT_GLYPH_U` (139), `DT_GLYPH_D` (140), `DT_LOOP` (141), `DT_N` (142). Tasks 4 and 6 use these names.

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_dash_tiles.c`, above its final `printf`. If the file has no pixel reader, add this one near the top:

```c
/* dash_tile_data holds two pixels per byte, the left one in the high nibble. */
static int px(int t, int x, int y) {
    unsigned char b = dash_tile_data[t][y * 4 + x / 2];
    return (x & 1) ? (b & 15) : (b >> 4);
}
```

The cases:

```c
    /* The set grew by exactly the 27 new tiles and nothing before them moved.
       Every index after DT_KNIGHT0 is a literal in dash_tiles.c, so a renumber
       here silently repaints the whole map with the wrong glyphs. */
    {
        int mask, lit, x, y;

        assert(DT_KNIGHT0 == 109);
        assert(DT_DASH0 == 115);
        assert(DT_ARROW_N == 131);
        assert(DT_ARROW_DASH_N == 135);
        assert(DT_GLYPH_U == 139);
        assert(DT_GLYPH_D == 140);
        assert(DT_LOOP == 141);
        assert(DT_N == 142);

        /* Mask 0 is never painted and stays blank in both sets. */
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++) assert(px(DT_DASH0, x, y) == 0);

        /* A dashed cell lights both centre rows and both centre columns
           wherever the solid one does, which is what lets a dashed elbow,
           T and crossing join at the middle of the cell. */
        for (mask = 1; mask < 16; mask++) {
            assert(px(DT_DASH0 + mask, 3, 3) == px(DT_LINK0 + mask, 3, 3));
            assert(px(DT_DASH0 + mask, 4, 4) == px(DT_LINK0 + mask, 4, 4));
        }

        /* And it is genuinely dashed: a vertical run lights rows 0, 3, 4 and 7
           and leaves 1, 2, 5 and 6 dark, a two-on two-off stipple whose period
           divides the tile so a long run stays in phase across cell edges. */
        lit = DT_DASH0 + (DT_EDGE_N | DT_EDGE_S);
        assert(px(lit, 3, 0) != 0);
        assert(px(lit, 3, 1) == 0);
        assert(px(lit, 3, 2) == 0);
        assert(px(lit, 3, 3) != 0);
        assert(px(lit, 3, 4) != 0);
        assert(px(lit, 3, 5) == 0);
        assert(px(lit, 3, 6) == 0);
        assert(px(lit, 3, 7) != 0);

        /* The solid run it mirrors is lit all the way down. */
        for (y = 0; y < 8; y++)
            assert(px(DT_LINK0 + (DT_EDGE_N | DT_EDGE_S), 3, y) != 0);

        /* Each arrowhead reaches the edge it points at and not the opposite
           one, which is the whole content of "which way does this go". */
        assert(px(DT_ARROW_E, 7, 3) != 0 && px(DT_ARROW_E, 0, 0) == 0);
        assert(px(DT_ARROW_W, 0, 3) != 0 && px(DT_ARROW_W, 7, 0) == 0);
        assert(px(DT_ARROW_S, 3, 7) != 0 && px(DT_ARROW_S, 0, 0) == 0);
        assert(px(DT_ARROW_N, 3, 0) != 0 && px(DT_ARROW_N, 0, 7) == 0);

        /* The dashed arrowheads keep the head solid and dash only the shaft,
           so a conditional one-way passage still reads as pointing somewhere. */
        assert(px(DT_ARROW_DASH_E, 7, 3) == px(DT_ARROW_E, 7, 3));
        assert(px(DT_ARROW_DASH_E, 1, 3) == 0);

        /* The glyphs and the loop are drawn at all, and differ from each other. */
        {
            int nu = 0, nd = 0, nl = 0;
            for (y = 0; y < 8; y++)
                for (x = 0; x < 8; x++) {
                    if (px(DT_GLYPH_U, x, y)) nu++;
                    if (px(DT_GLYPH_D, x, y)) nd++;
                    if (px(DT_LOOP, x, y))    nl++;
                }
            assert(nu > 4 && nd > 4 && nl > 4);
            assert(memcmp(dash_tile_data[DT_GLYPH_U],
                          dash_tile_data[DT_GLYPH_D], 32) != 0);
        }
    }
```

If `test_dash_tiles.c` does not already `#include <string.h>`, add it.

- [ ] **Step 2: Run test to verify it fails**

Run: `gcc -O2 -I saturn/src -o /tmp/tdt saturn/tests/test_dash_tiles.c saturn/src/video/dash_tiles.c && /tmp/tdt`
Expected: FAIL at compile with `'DT_DASH0' undeclared`.

- [ ] **Step 3: Extend the tile enum**

In `saturn/src/video/dash_map.h`, replace the last two lines of the enum (`DT_KNIGHT0,` and `DT_N = DT_KNIGHT0 + 6`) with:

```c
    DT_KNIGHT0,
    DT_DASH0 = DT_KNIGHT0 + 6,
    DT_ARROW_N = DT_DASH0 + 16, DT_ARROW_E, DT_ARROW_S, DT_ARROW_W,
    DT_ARROW_DASH_N, DT_ARROW_DASH_E, DT_ARROW_DASH_S, DT_ARROW_DASH_W,
    DT_GLYPH_U, DT_GLYPH_D,
    DT_LOOP,
    DT_N
};
```

And add to the enum's own header block, after the paragraph describing `DT_KNIGHT0`:

```
 |   DT_DASH0 mirrors DT_LINK0 exactly -- sixteen tiles on the same four-bit
 |   mask -- but stippled two pixels on and two off. It is a full set rather
 |   than a straight pair because a conditional passage doglegs like any other,
 |   and a conditional link drawing solid at its corners is the one fault a
 |   player cannot tell from a bug. The stipple's period divides the tile, so a
 |   run stays in phase across cell edges and both centre pixels stay lit,
 |   which is what lets a dashed elbow join.
 |
 |   DT_ARROW_* go in the last cell before the mark a one-way passage leads to,
 |   carrying the incoming groove as well as the head so the run does not break
 |   where the arrow starts; the DASH set is the same head over a dashed shaft.
 |   DT_GLYPH_U and DT_GLYPH_D end a stub whose far end is on another floor,
 |   and DT_LOOP marks an exit that returns to the room it left.
```

- [ ] **Step 4: Generate the tiles**

In `tools/gen_dash_tiles.py`, change line 16 to `N = 142`.

Add beside `solid` and `blank`, after the `solid` definition:

```python
def dash_ok(v):
    """Whether a run's pixel at coordinate v is lit. Two on, two off, on a
    period of four -- which divides the 8-pixel tile, so a long run stays in
    phase across tile boundaries -- and phased so that both centre pixels, 3
    and 4, are lit and a dashed elbow joins at the middle of the cell."""
    return ((v + 1) & 3) < 2


def dashed(idx, x0, y0, x1, y1, axis, base=None):
    """solid(), stippled along 'v' for a vertical arm or 'h' for a horizontal."""
    t = base if base is not None else blank()
    return [[idx if (x0 <= x <= x1 and y0 <= y <= y1
                     and dash_ok(y if axis == "v" else x))
             else t[y][x] for x in range(8)] for y in range(8)]


def rot_cw(t):
    """An 8x8 grid turned a quarter turn clockwise, so one drawn arrowhead
    yields all four without four hand-placed copies to drift apart."""
    return [[t[7 - x][y] for x in range(8)] for y in range(8)]


def bitmap(rows, idx):
    """An 8x8 grid from eight strings of eight characters, '#' for ink."""
    return [[idx if rows[y][x] == "#" else 0 for x in range(8)] for y in range(8)]


GLYPH_U = ["........",
           ".#....#.",
           ".#....#.",
           ".#....#.",
           ".#....#.",
           ".#....#.",
           "..####..",
           "........"]

GLYPH_D = ["........",
           ".####...",
           ".#...#..",
           ".#....#.",
           ".#....#.",
           ".#...#..",
           ".####...",
           "........"]

LOOP = ["..####..",
        ".#....#.",
        "#......#",
        "#......#",
        "#......#",
        ".#....#.",
        "..####..",
        "...##..."]
```

Then in `build()`, immediately before the final `assert len(tiles) == N`:

```python
    # The dashed set, index for index with DT_LINK0. Same arms, stippled along
    # each arm's own axis, so a dashed run and a solid one meet cleanly where
    # one crosses the other.
    for mask in range(16):
        t = blank()
        if mask & 1: t = dashed(MARK_DARK, 3, 0, 4, 4, "v", base=t)
        if mask & 2: t = dashed(MARK_DARK, 3, 3, 7, 4, "h", base=t)
        if mask & 4: t = dashed(MARK_DARK, 3, 3, 4, 7, "v", base=t)
        if mask & 8: t = dashed(MARK_DARK, 0, 3, 4, 4, "h", base=t)
        tiles.append(t)                                         # DT_DASH0+mask

    # One arrowhead is drawn pointing east and turned for the other three. The
    # head stays solid in the dashed variant: a conditional passage still has
    # to say which way it runs.
    def arrowhead(base):
        t = solid(MARK_DARK, 5, 1, 5, 6, base=base)
        t = solid(MARK_DARK, 6, 2, 6, 5, base=t)
        return solid(MARK_DARK, 7, 3, 7, 4, base=t)

    for shaft in (solid(MARK_DARK, 0, 3, 4, 4),
                  dashed(MARK_DARK, 0, 3, 4, 4, "h")):
        east  = arrowhead(shaft)
        south = rot_cw(east)
        west  = rot_cw(south)
        north = rot_cw(west)
        for t in (north, east, south, west):        # DT_ARROW_N, E, S, W
            tiles.append(t)

    tiles.append(bitmap(GLYPH_U, MARK_DARK))                    # DT_GLYPH_U
    tiles.append(bitmap(GLYPH_D, MARK_DARK))                    # DT_GLYPH_D
    tiles.append(bitmap(LOOP, MARK_DARK))                       # DT_LOOP
```

- [ ] **Step 5: Regenerate and eyeball**

Run:
```bash
python3 tools/gen_dash_tiles.py > saturn/src/video/dash_tiles.c
python3 tools/preview_dash.py
```
Expected: the generator exits 0 with its internal `assert len(tiles) == 142` satisfied, and the preview shows dashed runs, four arrowheads, `U`, `D` and a ring with a tip below it. If a glyph reads badly at 8x8, adjust the `GLYPH_*`/`LOOP` strings and regenerate; the tests assert shape and edges, not exact pixels.

- [ ] **Step 6: Run test to verify it passes**

Run: `gcc -O2 -I saturn/src -o /tmp/tdt saturn/tests/test_dash_tiles.c saturn/src/video/dash_tiles.c && /tmp/tdt`
Expected: PASS -- `test_dash_tiles: ok`.

- [ ] **Step 7: Confirm nothing else moved**

Run: `gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdm`
Expected: `test_dash_map: ok`.

- [ ] **Step 8: Commit**

```bash
git add tools/gen_dash_tiles.py saturn/src/video/dash_map.h saturn/src/video/dash_tiles.c saturn/tests/test_dash_tiles.c
git commit -m "Add the twenty-seven tiles the passage marks need -- a dashed set mirroring DT_LINK0 index for index, four arrowheads turned from one drawing and four more over a dashed shaft, a U, a D and a loop -- all appended after the knight so no existing index moves, and stippled on a period that divides the tile so a dashed run stays in phase across cell edges and keeps both centre pixels lit, which is what lets a dashed elbow join."
```

---

### Task 4: Extract the edge accumulator into `map_edges`

This task moves code and changes no behaviour. The equivalence test in Task 5 depends on this unit existing and being runnable on the host.

**Files:**
- Create: `saturn/src/video/map_edges.h`, `saturn/src/video/map_edges.c`
- Modify: `saturn/src/video/map_view.cxx` -- delete `MAP_EDGE_STAIR`, `g_edge`, `mark_step`, `trace`, `paint_link`, `occupied`, `cell_is_mark`; call the new unit instead
- Test: `saturn/tests/test_map_edges.c` (new)

**Interfaces:**
- Consumes: `DT_EDGE_*`, `DT_LINK0`, `DT_LINK_STAIR` from `dash_map.h`; `MAP_LINK_FLAT`/`VERT` from `map_model.h`; `MAP_ROOMS_W`/`H`/`MAP_CELLS` from `map_layout.h`.
- Produces: `void map_edges_reset(void)`, `void map_edges_mark(int cx, int cy)`, `void map_edges_link(int ax, int ay, int bx, int by, int kind)`, `unsigned char map_edges_tile(int x, int y)`. Tasks 5 and 6 call all four.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_map_edges.c`:

```c
/* Build:
     gcc -O2 -I saturn/src -o /tmp/tme saturn/tests/test_map_edges.c \
         saturn/src/video/map_edges.c saturn/src/video/dash_tiles.c && /tmp/tme
   map_edges.c is deliberately free of SRL includes so this links on the host.
   It exists for exactly that reason: the accumulator used to live inside
   map_view.cxx, which cannot be run anywhere but on the console. */
#include "../src/video/map_edges.h"
#include "../src/video/dash_map.h"
#include "../src/video/map_layout.h"
#include "../src/engine/map_model.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* A straight horizontal run between two marks four cells apart lays a
       groove in each of the three cells between them and nothing anywhere
       else. The marks' own cells stay clear so the mark can be painted over. */
    {
        int x, y, n = 0;
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_mark(8, 4);
        map_edges_link(4, 4, 8, 4, MAP_LINK_FLAT);

        assert(map_edges_tile(5, 4) == DT_LINK_H);
        assert(map_edges_tile(6, 4) == DT_LINK_H);
        assert(map_edges_tile(7, 4) == DT_LINK_H);
        assert(map_edges_tile(4, 4) == 0);
        assert(map_edges_tile(8, 4) == 0);

        for (y = 0; y < MAP_ROOMS_H * MAP_CELLS; y++)
            for (x = 0; x < MAP_ROOMS_W * MAP_CELLS; x++)
                if (map_edges_tile(x, y) != 0) n++;
        assert(n == 3);
    }

    /* A vertical run between two marks takes the stair tile, which is what a
       level change looks like as opposed to a road. */
    {
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_mark(4, 8);
        map_edges_link(4, 4, 4, 8, MAP_LINK_VERT);
        assert(map_edges_tile(4, 5) == DT_LINK_STAIR);
        assert(map_edges_tile(4, 7) == DT_LINK_STAIR);
    }

    /* A flat vertical run is an ordinary groove, not a staircase. */
    {
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_mark(4, 8);
        map_edges_link(4, 4, 4, 8, MAP_LINK_FLAT);
        assert(map_edges_tile(4, 6) == DT_LINK_V);
    }

    /* Two runs crossing one cell resolve into a crossing rather than one
       overwriting the other. This is why the layer is accumulated whole and
       swept once, and it is the property a per-link painter cannot have. */
    {
        map_edges_reset();
        map_edges_mark(0, 4); map_edges_mark(8, 4);
        map_edges_mark(4, 0); map_edges_mark(4, 8);
        map_edges_link(0, 4, 8, 4, MAP_LINK_FLAT);
        map_edges_link(4, 0, 4, 8, MAP_LINK_FLAT);
        assert(map_edges_tile(4, 4) == DT_LINK0 + 15);
        assert(map_edges_tile(3, 4) == DT_LINK_H);
        assert(map_edges_tile(4, 3) == DT_LINK_V);
    }

    /* An L route turns, so the corner cell is an elbow. */
    {
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_mark(8, 8);
        map_edges_link(4, 4, 8, 8, MAP_LINK_FLAT);
        assert(map_edges_tile(6, 4) != 0);
        assert(map_edges_tile(8, 6) != 0);
    }

    /* A route is chosen that passes through no other mark. The first L route
       from (4,4) to (12,12) turns at (12,4), so a mark sitting there rules it
       out and the second L -- turning at (4,12) -- is taken instead.

       The blocking mark has to be off the straight line between the two ends:
       every candidate route stays inside their bounding box, so a mark lying
       collinear between them blocks all four and paint_link draws through it
       anyway, on the grounds that a missing link is worse than an ambiguous
       one. This case tests the choice, not that fallback. */
    {
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_mark(12, 12);
        map_edges_mark(12, 4);
        map_edges_link(4, 4, 12, 12, MAP_LINK_FLAT);
        assert(map_edges_tile(4, 8) != 0);
        assert(map_edges_tile(8, 4) == 0);
    }

    /* Cells off the viewport are refused rather than written past the end of
       the layer, which would corrupt whatever the linker put next to it. */
    {
        map_edges_reset();
        map_edges_mark(0, 0);
        map_edges_link(0, 0, -8, 0, MAP_LINK_FLAT);
        assert(map_edges_tile(0, 0) == 0);
    }

    printf("test_map_edges: ok\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `gcc -O2 -I saturn/src -o /tmp/tme saturn/tests/test_map_edges.c saturn/src/video/map_edges.c saturn/src/video/dash_tiles.c && /tmp/tme`
Expected: FAIL -- `saturn/src/video/map_edges.c: No such file or directory`.

- [ ] **Step 3: Create the header**

Create `saturn/src/video/map_edges.h`:

```c
/*----------------------
 | map_edges.h
 | Description: The map's line layer: accumulates every link between room marks
 |   in viewport cell space, then answers which tile each cell wants. Pure
 |   arithmetic over a byte grid, free of SRL, so a host test can exercise the
 |   route choice and the tile choice that a build for the target cannot be run
 |   to check.
 |
 |   This used to live inside map_view.cxx, where it could not be tested at all
 |   -- the same reason map_layout.h was split out before it. What stays in
 |   map_view.cxx is gathering the rooms and calling dash_map_paint; every
 |   decision about what a cell should look like is here.
 |
 |   Lines are accumulated whole and swept once rather than painted as they are
 |   walked. That is what lets a cell two runs cross come out as a crossing: a
 |   renderer painting each link as it went would only ever see one line at a
 |   time and would overwrite the first with the second.
 | Author: suinevere
 | Dependencies: dash_map.h, map_model.h, map_layout.h
 ----------------------*/
#ifndef MAP_EDGES_H
#define MAP_EDGES_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | map_edges_reset
 | Description: Forgets every line and mark, ready for a fresh frame.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_edge, g_mark
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void map_edges_reset(void);

/*----------------------
 | map_edges_mark
 | Description: Records that a room mark holds a cell. Two things follow: a
 |   route will not be drawn through it, and the sweep will not offer a tile
 |   for it, so the caller's own mark is not painted over by a groove.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_mark
 | Params: cx, cy -- the mark's cell
 | Returns: N/A
 ----------------------*/
void map_edges_mark(int cx, int cy);

/*----------------------
 | map_edges_link
 | Description: Records the route joining two room marks, choosing one that
 |   passes through no other mark. Four candidate routes are tried in order --
 |   the two L shapes, then the two doglegs through the midpoint -- and the
 |   first that is clear is taken. If none is clear the first is drawn anyway,
 |   on the grounds that a map missing a link is worse than one drawing an
 |   ambiguous link.
 |
 |   Every candidate stays inside the bounding box of the two marks, so no
 |   route can wander off the viewport.
 | Author: suinevere
 | Dependencies: map_edges_mark
 | Globals: g_edge
 | Params: ax, ay -- the source mark's cell; bx, by -- the destination's; kind
 |   -- MAP_LINK_FLAT or MAP_LINK_VERT
 | Returns: N/A
 ----------------------*/
void map_edges_link(int ax, int ay, int bx, int by, int kind);

/*----------------------
 | map_edges_tile
 | Description: Which tile a cell wants, or 0 for none. Zero is DT_BLANK and is
 |   also what a cell holding a room mark answers, so a caller can sweep the
 |   whole viewport and paint whatever is nonzero without a second test.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_edge, g_mark
 | Params: x, y -- the cell
 | Returns: the tile index, or 0
 ----------------------*/
unsigned char map_edges_tile(int x, int y);

#ifdef __cplusplus
}
#endif
#endif /* MAP_EDGES_H */
```

- [ ] **Step 4: Move the implementation**

Create `saturn/src/video/map_edges.c` by moving `mark_step`, `trace` and `paint_link` out of `map_view.cxx` unchanged apart from the two substitutions noted below, and adding the two entry points.

```c
/*----------------------
 | map_edges.c
 | Description: See map_edges.h.
 | Author: suinevere
 | Dependencies: map_edges.h, dash_map.h, map_model.h, map_layout.h
 ----------------------*/
#include "map_edges.h"
#include "dash_map.h"
#include "map_layout.h"
#include "../engine/map_model.h"

/*----------------------
 | MAP_EDGE_STAIR
 | Description: The fifth bit of an edge cell, beside the four DT_EDGE_* sides:
 |   set when a vertical exit -- a staircase rather than a walk -- laid the
 |   line. Kept out of the low nibble so the nibble still indexes the tile set.
 | Author: suinevere
 ----------------------*/
#define MAP_EDGE_STAIR 16

/*----------------------
 | g_edge / g_mark
 | Description: Which sides of each cell a line leaves through, and which cells
 |   a room mark holds. A whole viewport of bytes rather than a sparse list
 |   because the sweep then costs one pass with no lookup, and two kilobytes is
 |   nothing beside the story image the heap is already carrying.
 | Author: suinevere
 ----------------------*/
static unsigned char g_edge[MAP_ROOMS_H * MAP_CELLS][MAP_ROOMS_W * MAP_CELLS];
static unsigned char g_mark[MAP_ROOMS_H * MAP_CELLS][MAP_ROOMS_W * MAP_CELLS];

/*----------------------
 | in_view
 | Description: Whether a cell is on the viewport.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: x, y -- the cell
 | Returns: 1 when the cell is inside the layer, 0 otherwise
 ----------------------*/
static int in_view(int x, int y)
{
    return x >= 0 && y >= 0 &&
           x < MAP_ROOMS_W * MAP_CELLS && y < MAP_ROOMS_H * MAP_CELLS;
}

/*----------------------
 | map_edges_reset
 | Description: See map_edges.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_edge, g_mark
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void map_edges_reset(void)
{
    int y, x;
    for (y = 0; y < MAP_ROOMS_H * MAP_CELLS; y++)
        for (x = 0; x < MAP_ROOMS_W * MAP_CELLS; x++) {
            g_edge[y][x] = 0;
            g_mark[y][x] = 0;
        }
}

/*----------------------
 | map_edges_mark
 | Description: See map_edges.h.
 | Author: suinevere
 | Dependencies: in_view
 | Globals: g_mark
 | Params: cx, cy -- the mark's cell
 | Returns: N/A
 ----------------------*/
void map_edges_mark(int cx, int cy)
{
    if (in_view(cx, cy)) g_mark[cy][cx] = 1;
}
```

Then `mark_step`, `trace` and `paint_link` follow, copied from `map_view.cxx:214-345` with their header blocks, and with exactly two changes:

1. `mark_step`'s three bounds tests collapse to `if (!in_view(x, y) || !in_view(nx, ny)) return;`
2. `trace`'s test-mode line `else if (!(x == ex && y == ey) && cell_is_mark(x, y, n)) return 0;` becomes `else if (!(x == ex && y == ey) && g_mark[y][x]) return 0;`, and the `int n` parameter is dropped from both `trace` and `paint_link`.

`paint_link` is renamed `map_edges_link` and loses its `n` argument; its body is otherwise the one already in `map_view.cxx`. Finally:

```c
/*----------------------
 | map_edges_tile
 | Description: See map_edges.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_edge, g_mark
 | Params: x, y -- the cell
 | Returns: the tile index, or 0
 ----------------------*/
unsigned char map_edges_tile(int x, int y)
{
    unsigned char e;
    int mask;
    if (!in_view(x, y)) return 0;
    if (g_mark[y][x]) return 0;
    e = g_edge[y][x];
    mask = e & 15;
    if (mask == 0) return 0;
    if ((e & MAP_EDGE_STAIR) && mask == (DT_EDGE_N | DT_EDGE_S))
        return DT_LINK_STAIR;
    return (unsigned char) (DT_LINK0 + mask);
}
```

- [ ] **Step 5: Rewire `map_view.cxx`**

Delete `MAP_EDGE_STAIR`, `g_edge`, `occupied`, `cell_is_mark`, `mark_step`, `trace` and `paint_link` from `map_view.cxx`, and add `#include "map_edges.h"` beside its other includes.

In `draw_once`, replace the `g_edge` clear with `map_edges_reset();` placed immediately after `n = gather(sx, sy, page);`, then register the marks before any link is laid:

```c
    map_edges_reset();
    for (i = 0; i < n; i++)
        map_edges_mark(map_layout_cell(g_dxs[i], 0, MAP_CX, 0),
                       map_layout_cell(g_dys[i], 0, MAP_CY, MAP_TOP));
```

The pair loop keeps its shape and drops the `n` argument:

```c
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            int kind = map_model_link(g_ids[i], g_ids[j]);
            if (kind == MAP_LINK_NONE) continue;
            map_edges_link(map_layout_cell(g_dxs[i], 0, MAP_CX, 0),
                           map_layout_cell(g_dys[i], 0, MAP_CY, MAP_TOP),
                           map_layout_cell(g_dxs[j], 0, MAP_CX, 0),
                           map_layout_cell(g_dys[j], 0, MAP_CY, MAP_TOP),
                           kind);
        }
    }
```

And the sweep becomes:

```c
    for (i = 0; i < MAP_ROOMS_H * MAP_CELLS; i++) {
        int c;
        for (c = 0; c < MAP_ROOMS_W * MAP_CELLS; c++) {
            unsigned char t = map_edges_tile(c, i);
            if (t) dash_map_paint(c, i, t);
        }
    }
```

Add `saturn/src/video/map_edges.c` to whatever source list `saturn/compile.bat` builds from, if it enumerates files rather than globbing.

- [ ] **Step 6: Run test to verify it passes**

Run: `gcc -O2 -I saturn/src -o /tmp/tme saturn/tests/test_map_edges.c saturn/src/video/map_edges.c saturn/src/video/dash_tiles.c && /tmp/tme`
Expected: PASS -- `test_map_edges: ok`.

- [ ] **Step 7: Build both targets**

Run: `cd saturn && cmd //c compile.bat`
Expected: both the CD and NETBIN halves link with no new warnings. Do not use `make` from git-bash -- its `find src/ -name '*.c'` returns nothing under this shell and the link fails for reasons unrelated to this change.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/video/map_edges.h saturn/src/video/map_edges.c saturn/src/video/map_view.cxx saturn/tests/test_map_edges.c
git commit -m "Move the map's line layer out of map_view.cxx into an SRL-free map_edges so the route choice and the tile choice can be exercised on the host, which they never could while they sat in a file that only runs on the console, replacing the mark test that inferred a room's cell from its coordinates with an explicit register of the cells the marks hold; no behaviour changes."
```

---

### Task 5: Replace the pair loop with per-room enumeration

Still no visual change. The point of doing this separately is that the drawn layer must come out identical, and that is only checkable while nothing else is moving.

**Files:**
- Modify: `saturn/src/video/map_view.cxx` -- `gather` fills a reverse index; the `i`/`j` loop becomes an exit walk
- Test: `saturn/tests/test_map_edges.c`

**Interfaces:**
- Consumes: `map_model_exits`, `MapExit`, `MAP_EXIT_ONEWAY` from Task 1; `map_edges_link`, `map_edges_mark`, `map_edges_reset`, `map_edges_tile` from Task 4.
- Produces: nothing new. Task 6 extends the same loop.

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_map_edges.c`, above its final `printf`. This is the equivalence check: the same set of pairs, laid the same way, whichever loop produced them.

```c
    /* The canonical-order rule draws each two-way pair exactly once, and a
       one-way pair once from its source. Feeding the same four marks through
       both orders must leave an identical layer -- this is what makes the loop
       swap provably behaviour-preserving rather than merely faster. */
    {
        static unsigned char before[MAP_ROOMS_H * MAP_CELLS][MAP_ROOMS_W * MAP_CELLS];
        int x, y;

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(12, 4);
        map_edges_link(4, 4, 12, 4, MAP_LINK_FLAT);
        for (y = 0; y < MAP_ROOMS_H * MAP_CELLS; y++)
            for (x = 0; x < MAP_ROOMS_W * MAP_CELLS; x++)
                before[y][x] = map_edges_tile(x, y);

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(12, 4);
        map_edges_link(12, 4, 4, 4, MAP_LINK_FLAT);
        for (y = 0; y < MAP_ROOMS_H * MAP_CELLS; y++)
            for (x = 0; x < MAP_ROOMS_W * MAP_CELLS; x++)
                assert(before[y][x] == map_edges_tile(x, y));
    }

    /* Laying the same link twice, which the enumeration does not do but which
       a bug in the canonical-order rule would cause, must not change the
       layer either. The accumulation is idempotent by construction and this
       pins it, so a double-draw shows up as a test failure rather than as a
       map that looks right. */
    {
        static unsigned char once[MAP_ROOMS_H * MAP_CELLS][MAP_ROOMS_W * MAP_CELLS];
        int x, y;

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(4, 12);
        map_edges_link(4, 4, 4, 12, MAP_LINK_VERT);
        for (y = 0; y < MAP_ROOMS_H * MAP_CELLS; y++)
            for (x = 0; x < MAP_ROOMS_W * MAP_CELLS; x++)
                once[y][x] = map_edges_tile(x, y);

        map_edges_link(4, 4, 4, 12, MAP_LINK_VERT);
        for (y = 0; y < MAP_ROOMS_H * MAP_CELLS; y++)
            for (x = 0; x < MAP_ROOMS_W * MAP_CELLS; x++)
                assert(once[y][x] == map_edges_tile(x, y));
    }
```

- [ ] **Step 2: Run test to verify it passes already**

Run: `gcc -O2 -I saturn/src -o /tmp/tme saturn/tests/test_map_edges.c saturn/src/video/map_edges.c saturn/src/video/dash_tiles.c && /tmp/tme`
Expected: PASS. These two properties are what the loop swap will rely on, so they are pinned before the swap rather than after. If either fails, `map_edges_link` is order-dependent and Task 5 cannot proceed -- stop and report.

- [ ] **Step 3: Give `gather` a reverse index**

In `map_view.cxx`, beside `g_ids`/`g_dxs`/`g_dys` at line 116:

```c
/*----------------------
 | g_slot
 | Description: Which gathered entry each object number landed in, or -1. The
 |   exit walk asks this once per exit; a scan of g_ids instead would put an
 |   O(n) search inside a loop that already runs n times, which is the shape
 |   that cost this screen a dozen frames a redraw once before.
 | Author: suinevere
 ----------------------*/
static short g_slot[MAP_ROOM_MAX];
```

At the top of `gather`, clear it, and set it as each room is accepted:

```c
static int gather(int sx, int sy, int page)
{
    int r, n = 0;
    for (r = 0; r < MAP_ROOM_MAX; r++) g_slot[r] = -1;
    for (r = 1; r < MAP_ROOM_MAX && n < MAP_VIS_MAX; r++) {
```

and after the existing `g_ids[n] = (unsigned short) r;`, add `g_slot[r] = (short) n;`.

- [ ] **Step 4: Replace the loop**

Replace the whole `i`/`j` pair loop in `draw_once` with:

```c
    for (i = 0; i < n; i++) {
        MapExit ex[RM_DIR_N];
        int k, ne = map_model_exits(g_ids[i], ex, RM_DIR_N);
        for (k = 0; k < ne; k++) {
            int j, kind;
            if (ex[k].dest == g_ids[i]) continue;
            j = g_slot[ex[k].dest];
            if (j < 0) continue;
            if (!(ex[k].flags & MAP_EXIT_ONEWAY) && ex[k].dest < g_ids[i])
                continue;
            kind = map_model_link(g_ids[i], ex[k].dest);
            map_edges_link(map_layout_cell(g_dxs[i], 0, MAP_CX, 0),
                           map_layout_cell(g_dys[i], 0, MAP_CY, MAP_TOP),
                           map_layout_cell(g_dxs[j], 0, MAP_CX, 0),
                           map_layout_cell(g_dys[j], 0, MAP_CY, MAP_TOP),
                           kind);
        }
    }
```

The `kind` still comes from `map_model_link` rather than from `ex[k].kind`. Those differ when a pair is joined by a flat exit one way and a vertical exit the other: `map_model_link` prefers flat, and taking the directed answer here would change what is drawn. Task 6 is where the directed kind starts to matter; this task must not change a pixel.

Add `#include "../engine/room_model.h"` to `map_view.cxx` if `RM_DIR_N` is not already reachable from its includes.

- [ ] **Step 5: Verify the layer is unchanged on the target**

Run: `cd saturn && cmd //c compile.bat`
Expected: both halves link. Then open the map in an emulator on a save with a dozen or more rooms explored and compare against a screenshot taken before Task 5. Expected: identical.

- [ ] **Step 6: Run the host suite**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tme saturn/tests/test_map_edges.c saturn/src/video/map_edges.c saturn/src/video/dash_tiles.c && /tmp/tme
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c saturn/src/engine/map_model.c saturn/src/engine/map_atlas.c && /tmp/tmm
gcc -O2 -I saturn/src -I saturn/src/video -o /tmp/tml saturn/tests/test_map_layout.c && /tmp/tml
```
Expected: three `ok` lines.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/video/map_view.cxx saturn/tests/test_map_edges.c
git commit -m "Draw the map's links by walking each gathered room's own exits rather than by testing all two and a half thousand pairs of them, which costs eight hundred and forty iterations a redraw against about twenty-nine thousand on the same loop whose earlier form starved the looping PCM hand-off, keeping each two-way pair drawn once from its lower object number and each one-way pair from its source, and still taking the link kind from map_model_link so the drawn layer comes out identical."
```

---

### Task 6: Draw the four marks

**Files:**
- Modify: `saturn/src/video/map_edges.h` / `.c` -- widen the layer, add the flag bits, the stub and glyph entry points, extend `map_edges_tile`
- Modify: `saturn/src/video/map_view.cxx` -- pass flags, second placement pass
- Test: `saturn/tests/test_map_edges.c`

**Interfaces:**
- Consumes: everything from Tasks 1-5.
- Produces: `MAP_EDGE_DASH`, `MAP_EDGE_SOLID`, `MAP_EDGE_ARROW_N/E/S/W`, `MAP_EDGE_UP`, `MAP_EDGE_DOWN`, `MAP_EDGE_LOOP`; `map_edges_link` gains a `flags` parameter and an `arrow` parameter (`0` none, `1` head at the `(bx,by)` end, `2` head at the `(ax,ay)` end); new `void map_edges_stub(int mx, int my, int dx, int dy)`, `void map_edges_glyph(int x, int y, unsigned int bit)` and `const unsigned short *map_edges_layer(void)`.

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_map_edges.c`, above its final `printf`:

```c
    /* A conditional link draws dashed end to end. */
    {
        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(8, 4);
        map_edges_link(4, 4, 8, 4, MAP_LINK_FLAT, MAP_EXIT_COND, 0);
        assert(map_edges_tile(5, 4) == DT_DASH0 + (DT_EDGE_E | DT_EDGE_W));
        assert(map_edges_tile(6, 4) == DT_DASH0 + (DT_EDGE_E | DT_EDGE_W));
    }

    /* An open link is solid, and where an open line crosses a conditional one
       the shared cell draws solid: a cell carrying a real passage must not
       read as conditional. */
    {
        map_edges_reset();
        map_edges_mark(0, 4); map_edges_mark(8, 4);
        map_edges_mark(4, 0); map_edges_mark(4, 8);
        map_edges_link(0, 4, 8, 4, MAP_LINK_FLAT, MAP_EXIT_COND, 0);
        map_edges_link(4, 0, 4, 8, MAP_LINK_FLAT, 0, 0);
        assert(map_edges_tile(3, 4) == DT_DASH0 + (DT_EDGE_E | DT_EDGE_W));
        assert(map_edges_tile(4, 3) == DT_LINK_V);
        assert(map_edges_tile(2, 4) == DT_DASH0 + (DT_EDGE_E | DT_EDGE_W));
        /* The shared cell itself, which is the whole point of the case. */
        assert(map_edges_tile(4, 4) == DT_LINK0 + 15);
    }

    /* arrow == 1 puts the head in the last cell before the (bx,by) end,
       pointing at it, and nothing at the end it left. */
    {
        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(8, 4);
        map_edges_link(4, 4, 8, 4, MAP_LINK_FLAT, 0, 1);
        assert(map_edges_tile(7, 4) == DT_ARROW_E);
        assert(map_edges_tile(5, 4) == DT_LINK_H);
    }

    /* arrow == 2 is the same link with the head at the other end. The argument
       order is identical -- only which end the passage leads to differs, which
       is exactly the case canonical ordering created: the route always runs
       from the lower object number, so the destination is not always (bx,by). */
    {
        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(8, 4);
        map_edges_link(4, 4, 8, 4, MAP_LINK_FLAT, 0, 2);
        assert(map_edges_tile(5, 4) == DT_ARROW_W);
        assert(map_edges_tile(7, 4) == DT_LINK_H);
    }

    /* A conditional one-way passage keeps its head and dashes its shaft. */
    {
        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(8, 4);
        map_edges_link(4, 4, 8, 4, MAP_LINK_FLAT, MAP_EXIT_COND, 1);
        assert(map_edges_tile(7, 4) == DT_ARROW_DASH_E);
    }

    /* An arrow on a route that turns points along its final leg, not along the
       straight line between the two marks. */
    {
        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(8, 8);
        map_edges_link(4, 4, 8, 8, MAP_LINK_FLAT, 0, 1);
        assert(map_edges_tile(8, 7) == DT_ARROW_S);
    }

    /* The glyphs and the loop win over whatever line shares their cell, since
       placement only ever puts them in cells nothing else claimed. */
    {
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_glyph(4, 2, MAP_EDGE_UP);
        map_edges_glyph(6, 4, MAP_EDGE_DOWN);
        map_edges_glyph(4, 6, MAP_EDGE_LOOP);
        assert(map_edges_tile(4, 2) == DT_GLYPH_U);
        assert(map_edges_tile(6, 4) == DT_GLYPH_D);
        assert(map_edges_tile(4, 6) == DT_LOOP);
    }

    /* A stub is a dashed run of its own, so an exit leaving the floor shows as
       something rather than as nothing -- which is what it showed before. */
    {
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_stub(4, 4, 0, -1);
        assert(map_edges_tile(4, 3) == DT_DASH0 + (DT_EDGE_N | DT_EDGE_S));
    }

    /* The layer is readable, which is how the placement pass finds free cells. */
    {
        const unsigned short *layer;
        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(8, 4);
        map_edges_link(4, 4, 8, 4, MAP_LINK_FLAT, 0, 0);
        layer = map_edges_layer();
        assert(layer[4 * (MAP_ROOMS_W * MAP_CELLS) + 5] != 0);
        assert(layer[0] == 0);
    }
```

Every earlier `map_edges_link` call in this file gains two trailing arguments, `0, 0` — no conditional flag and no arrowhead — which leaves their existing assertions unchanged.

- [ ] **Step 2: Run test to verify it fails**

Run: `gcc -O2 -I saturn/src -o /tmp/tme saturn/tests/test_map_edges.c saturn/src/video/map_edges.c saturn/src/video/dash_tiles.c && /tmp/tme`
Expected: FAIL at compile -- `too many arguments to function 'map_edges_link'` and `implicit declaration of 'map_edges_glyph'`.

- [ ] **Step 3: Widen the layer and add the bits**

In `map_edges.c`, change `g_edge` to `static unsigned short g_edge[...][...]`, and replace the `MAP_EDGE_STAIR` define with the full plan:

```c
/*----------------------
 | MAP_EDGE_STAIR .. MAP_EDGE_LOOP
 | Description: What a cell carries beside the four sides a line leaves through.
 |   The low nibble still indexes the tile set, so everything else lives above
 |   it.
 |
 |   SOLID exists so that a conditional line crossing an open one comes out
 |   solid at the shared cell: DASH alone could not tell "this cell is only on a
 |   conditional run" from "this cell is on both", and a cell carrying a real
 |   passage must not read as conditional.
 | Author: suinevere
 ----------------------*/
#define MAP_EDGE_STAIR    0x0010
#define MAP_EDGE_DASH     0x0020
#define MAP_EDGE_ARROW_N  0x0040
#define MAP_EDGE_ARROW_E  0x0080
#define MAP_EDGE_ARROW_S  0x0100
#define MAP_EDGE_ARROW_W  0x0200
#define MAP_EDGE_UP       0x0400
#define MAP_EDGE_DOWN     0x0800
#define MAP_EDGE_LOOP     0x1000
#define MAP_EDGE_SOLID    0x2000
```

`MAP_EDGE_UP`, `MAP_EDGE_DOWN` and `MAP_EDGE_LOOP` also go in `map_edges.h`, since `map_edges_glyph` takes one of them.

`mark_step` gains a `flags` argument carrying `MAP_EDGE_DASH` or `MAP_EDGE_SOLID`, and ORs it into both cells alongside `out` and `in`. `trace` and `map_edges_link` thread it through, along with the `arrow` argument.

**The arrow's end is told to this code, not inferred from it.** Task 5 canonicalised every call so the route runs from the lower object number to the higher, which means the far end of the route is the higher-numbered room and *not* necessarily where the passage leads. Placing the head at the route's end would point half the one-way arrows at the wrong room. So `arrow` is a three-valued argument: `0` no head, `1` head at the `(bx, by)` end, `2` head at the `(ax, ay)` end.

In `trace`'s record pass:

```c
            if (record) {
                mark_step(px, py, x, y, stair, deco);
                if (arrow == 1 && x == ex && y == ey) {
                    unsigned short head = (unsigned short)
                        (x > px ? MAP_EDGE_ARROW_E : x < px ? MAP_EDGE_ARROW_W
                         : y > py ? MAP_EDGE_ARROW_S : MAP_EDGE_ARROW_N);
                    if (in_view(px, py)) g_edge[py][px] |= head;
                }
                if (arrow == 2 && first) {
                    unsigned short head = (unsigned short)
                        (x > px ? MAP_EDGE_ARROW_W : x < px ? MAP_EDGE_ARROW_E
                         : y > py ? MAP_EDGE_ARROW_N : MAP_EDGE_ARROW_S);
                    if (in_view(x, y)) g_edge[y][x] |= head;
                    first = 0;
                }
            }
```

`first` is an `int` initialised to 1 before the walk. The two cases are mirror images: for `arrow == 1` the head goes on `(px, py)`, the cell stepped *from* on the last step, pointing the way travel went; for `arrow == 2` it goes on `(x, y)`, the cell stepped *to* on the first step, pointing back the way it came. Either way the head lands in the cell adjacent to the destination mark, pointing at it, and never in a mark's own cell — the sweep offers no tile for those.

`map_edges_link` takes both:

```c
void map_edges_link(int ax, int ay, int bx, int by, int kind,
                    unsigned int flags, int arrow)
{
    unsigned short deco = (unsigned short)
        ((flags & MAP_EXIT_COND) ? MAP_EDGE_DASH : MAP_EDGE_SOLID);
```

with the rest of the body unchanged apart from passing `deco` and `arrow` down. `MAP_EXIT_ONEWAY` is deliberately **not** read here — only the caller knows which end the passage leads to, and inferring it inside this function is the bug this design avoids.

New entry points:

```c
/*----------------------
 | map_edges_glyph
 | Description: Puts one of MAP_EDGE_UP, MAP_EDGE_DOWN or MAP_EDGE_LOOP in a
 |   cell. The caller has already established the cell is free, which is why
 |   this does not check: glyph placement is a pass of its own that runs after
 |   every line is in, precisely so it can see what is free.
 | Author: suinevere
 | Dependencies: in_view
 | Globals: g_edge
 | Params: x, y -- the cell; bit -- the glyph
 | Returns: N/A
 ----------------------*/
void map_edges_glyph(int x, int y, unsigned int bit)
{
    if (in_view(x, y)) g_edge[y][x] |= (unsigned short) bit;
}

/*----------------------
 | map_edges_stub
 | Description: A two-cell dashed run out of a mark, for an exit whose far end
 |   is on another floor. Before this, such an exit drew nothing at all -- the
 |   gather drops it for having no far end on this page -- so the map never
 |   said that a floor connects to another floor.
 |
 |   Two cells and not one because the glyph goes at the far end: one cell
 |   would set only the side facing the mark and draw as a half-tick with a
 |   letter floating past it, where two make a run that reaches what it
 |   labels. It stops there, one cell short of where a neighbouring room's mark
 |   would sit, which is what makes it read as an edge rather than a passage.
 | Author: suinevere
 | Dependencies: mark_step
 | Globals: g_edge
 | Params: mx, my -- the mark's cell; dx, dy -- the direction as a unit step
 | Returns: N/A
 ----------------------*/
void map_edges_stub(int mx, int my, int dx, int dy)
{
    mark_step(mx, my, mx + dx, my + dy, 0, MAP_EDGE_DASH);
    mark_step(mx + dx, my + dy, mx + 2 * dx, my + 2 * dy, 0, MAP_EDGE_DASH);
}

/*----------------------
 | map_edges_layer
 | Description: The accumulated layer, so the placement pass can ask which
 |   cells are free without a second copy of the grid.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_edge
 | Params: N/A
 | Returns: the layer, MAP_ROOMS_H*MAP_CELLS rows of MAP_ROOMS_W*MAP_CELLS
 ----------------------*/
const unsigned short *map_edges_layer(void)
{
    return &g_edge[0][0];
}
```

`map_edges_mark` also sets the mark's own cell nonzero in `g_edge` so the placement pass does not offer it:

```c
void map_edges_mark(int cx, int cy)
{
    if (!in_view(cx, cy)) return;
    g_mark[cy][cx] = 1;
    g_edge[cy][cx] |= MAP_EDGE_SOLID;
}
```

And `map_edges_tile` grows the precedence ladder:

```c
unsigned char map_edges_tile(int x, int y)
{
    unsigned short e;
    int mask;
    if (!in_view(x, y)) return 0;
    if (g_mark[y][x]) return 0;
    e = g_edge[y][x];
    if (e & MAP_EDGE_LOOP) return DT_LOOP;
    if (e & MAP_EDGE_UP)   return DT_GLYPH_U;
    if (e & MAP_EDGE_DOWN) return DT_GLYPH_D;
    if (e & (MAP_EDGE_ARROW_N | MAP_EDGE_ARROW_E |
             MAP_EDGE_ARROW_S | MAP_EDGE_ARROW_W)) {
        int dash = (e & MAP_EDGE_DASH) && !(e & MAP_EDGE_SOLID);
        if (e & MAP_EDGE_ARROW_N) return dash ? DT_ARROW_DASH_N : DT_ARROW_N;
        if (e & MAP_EDGE_ARROW_E) return dash ? DT_ARROW_DASH_E : DT_ARROW_E;
        if (e & MAP_EDGE_ARROW_S) return dash ? DT_ARROW_DASH_S : DT_ARROW_S;
        return dash ? DT_ARROW_DASH_W : DT_ARROW_W;
    }
    mask = e & 15;
    if (mask == 0) return 0;
    if ((e & MAP_EDGE_STAIR) && mask == (DT_EDGE_N | DT_EDGE_S))
        return DT_LINK_STAIR;
    if ((e & MAP_EDGE_DASH) && !(e & MAP_EDGE_SOLID))
        return (unsigned char) (DT_DASH0 + mask);
    return (unsigned char) (DT_LINK0 + mask);
}
```

Declare `map_edges_stub`, `map_edges_glyph` and `map_edges_layer` in `map_edges.h` with header blocks matching the ones above, and add the `flags` parameter to `map_edges_link`'s declaration and header block.

- [ ] **Step 4: Run test to verify it passes**

Run: `gcc -O2 -I saturn/src -o /tmp/tme saturn/tests/test_map_edges.c saturn/src/video/map_edges.c saturn/src/video/dash_tiles.c && /tmp/tme`
Expected: PASS -- `test_map_edges: ok`.

- [ ] **Step 5: Feed the flags and add the placement pass**

In `map_view.cxx`, the exit walk now passes the conditional flag and resolves which end the arrow belongs at. Self-loops and off-floor exits are collected for the second pass instead of being skipped.

**Keep Task 5's canonical ordering exactly as it stands** — `lo` and `hi` chosen by comparing object numbers, the lower always passed first. The arrow argument is what carries the direction now, so canonical ordering and correct arrowheads are no longer in tension.

```c
    for (i = 0; i < n; i++) {
        MapExit ex[RM_DIR_N];
        int k, ne = map_model_exits(g_ids[i], ex, RM_DIR_N);
        for (k = 0; k < ne; k++) {
            int j, lo, hi, arrow;
            if (ex[k].flags & MAP_EXIT_SELF) continue;
            j = g_slot[ex[k].dest];
            if (j < 0) continue;
            if (!(ex[k].flags & MAP_EXIT_ONEWAY) && ex[k].dest < g_ids[i])
                continue;
            if (g_ids[i] < ex[k].dest) { lo = i; hi = j; } else { lo = j; hi = i; }
            arrow = !(ex[k].flags & MAP_EXIT_ONEWAY) ? 0
                    : (ex[k].dest == g_ids[hi]) ? 1 : 2;
            map_edges_link(map_layout_cell(g_dxs[lo], 0, MAP_CX, 0),
                           map_layout_cell(g_dys[lo], 0, MAP_CY, MAP_TOP),
                           map_layout_cell(g_dxs[hi], 0, MAP_CX, 0),
                           map_layout_cell(g_dys[hi], 0, MAP_CY, MAP_TOP),
                           map_model_link(g_ids[i], ex[k].dest),
                           ex[k].flags, arrow);
        }
    }
```

Then, after that loop and before the sweep, the placement pass:

```c
    for (i = 0; i < n; i++) {
        MapExit ex[RM_DIR_N];
        int cx = map_layout_cell(g_dxs[i], 0, MAP_CX, 0);
        int cy = map_layout_cell(g_dys[i], 0, MAP_CY, MAP_TOP);
        int k, gx, gy, ne = map_model_exits(g_ids[i], ex, RM_DIR_N);
        const unsigned short (*layer)[MAP_ROOMS_W * MAP_CELLS] =
            (const unsigned short (*)[MAP_ROOMS_W * MAP_CELLS]) map_edges_layer();

        for (k = 0; k < ne; k++) {
            int up, dy;
            if (ex[k].flags & MAP_EXIT_SELF) {
                int sdx, sdy;
                map_model_step(ex[k].dir, &sdx, &sdy);
                if (map_layout_glyph(cx, cy, sdx, sdy, layer, &gx, &gy))
                    map_edges_glyph(gx, gy, MAP_EDGE_LOOP);
                continue;
            }
            if (ex[k].kind != MAP_LINK_VERT) continue;
            if (g_slot[ex[k].dest] >= 0) continue;
            up = (ex[k].dir & 1) == 0;
            dy = up ? -1 : 1;
            if (!map_layout_glyph(cx, cy, 0, dy, layer, &gx, &gy)) continue;
            if (gx == cx) map_edges_stub(cx, cy, 0, dy);
            map_edges_glyph(gx, gy, up ? MAP_EDGE_UP : MAP_EDGE_DOWN);
        }
    }
```

The glyph is placed **before** the stub is laid, and the stub is laid only when the glyph landed on the vertical. Laying the stub first would mark its own two cells occupied and push its own letter onto a diagonal, and a stub whose letter ended up somewhere else is a stray dash that means nothing. Where the vertical is congested this draws neither, which is the same declining-is-honest rule the rest of the placement follows.

`map_model_step` does not exist yet and must be added to `map_model.c`/`.h` as part of this step. It cannot simply expose `DX`/`DY`: those are file-static at `map_model.c:33-34` and hold **placement** steps in room units, not unit steps -- `RM_UP` is `(0,-2)`, `RM_DOWN` `(0,2)`, `RM_IN` `(2,0)`, `RM_OUT` `(-2,0)`. Passing them to `map_layout_glyph`, which takes a unit step, would put the glyph two cells past where it belongs.

```c
/*----------------------
 | map_model_step
 | Description: The unit step a direction moves in, for a caller working in
 |   cells rather than in rooms. Not DX/DY themselves: those are placement
 |   steps in room units and the vertical four are two apart, which is the
 |   spacing the layout wants and not a direction.
 |
 |   Up, down, in and out have no direction on a flat drawing and answer with
 |   the step for north, so a mark annotating one of them lands above the room
 |   rather than nowhere.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: DX, DY
 | Params: dir -- an RM_* index; dx, dy -- receive the step, each -1, 0 or 1
 | Returns: N/A
 ----------------------*/
void map_model_step(int dir, int *dx, int *dy) {
    if (dir < 0 || dir >= RM_DIR_N || dir >= RM_UP) { *dx = 0; *dy = -1; return; }
    *dx = DX[dir];
    *dy = DY[dir];
}
```

Declare it in `map_model.h` with the same block. `test_map_model.c` gains two assertions for it:

```c
    /* Unit steps, and the four with no direction on a flat drawing take north
       so a glyph annotating one of them lands somewhere. */
    {
        int dx, dy;
        map_model_step(RM_E, &dx, &dy);    assert(dx == 1 && dy == 0);
        map_model_step(RM_NE, &dx, &dy);   assert(dx == 1 && dy == -1);
        map_model_step(RM_UP, &dx, &dy);   assert(dx == 0 && dy == -1);
        map_model_step(RM_IN, &dx, &dy);   assert(dx == 0 && dy == -1);
        map_model_step(99, &dx, &dy);      assert(dx == 0 && dy == -1);
    }
```

`ex[k].dir & 1` reads even as up and odd as down, which is `RM_UP`/`RM_IN` up and `RM_DOWN`/`RM_OUT` down, matching the enum at `room_model.h:37`.

- [ ] **Step 6: Build both targets**

Run: `cd saturn && cmd //c compile.bat`
Expected: both halves link with no new warnings.

- [ ] **Step 7: Run the whole host suite**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tme saturn/tests/test_map_edges.c saturn/src/video/map_edges.c saturn/src/video/dash_tiles.c && /tmp/tme
gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c saturn/src/engine/map_model.c saturn/src/engine/map_atlas.c && /tmp/tmm
gcc -O2 -I saturn/src -I saturn/src/video -o /tmp/tml saturn/tests/test_map_layout.c && /tmp/tml
gcc -O2 -I saturn/src -o /tmp/tma saturn/tests/test_map_atlas.c saturn/src/engine/map_atlas.c && /tmp/tma
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdm
gcc -O2 -I saturn/src -o /tmp/tdt saturn/tests/test_dash_tiles.c saturn/src/video/dash_tiles.c && /tmp/tdt
python3 -m pytest saturn/tests -q
```
Expected: six `ok` lines and a clean pytest run.

- [ ] **Step 8: Check it on screen**

Open the map in an emulator on a Zork I save with the cellar reached. Expected: the trap door from the Living Room shows a dashed stub south ending in `D`; the cellar floor shows one north ending in `U`; the one-way drop into the cellar carries an arrowhead; no arrowhead appears on ordinary two-way passages. Page with L/R to confirm the stubs follow the floor being shown.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/video/map_edges.h saturn/src/video/map_edges.c saturn/src/video/map_view.cxx saturn/src/engine/map_model.h saturn/src/engine/map_model.c saturn/tests/test_map_edges.c
git commit -m "Draw the four marks the map was missing -- an arrowhead in the last cell before a room a passage only leads one way into, a dashed run for a conditional exit, a dashed stub carrying U or D for an exit whose far end is on another floor and which until now drew nothing at all, and a circle for an exit that returns to the room it left -- placing every glyph in a pass that runs after the lines are in so it can see which cells are free, declining rather than overwriting when none is, and drawing a cell solid wherever an open line crosses a conditional one so a real passage never reads as conditional."
```

---

## Self-Review

**Spec coverage.** Every section of the spec maps to a task: the four rules and the `g_cond` change to Task 1; `map_layout_glyph` to Task 2; the tile table to Task 3; the three-pass rendering and the bit plan to Tasks 4 and 6; the interface change and the 35x claim to Task 5; every test named in the spec's testing table to the task that adds it.

**Two deviations from the spec, both deliberate:**

1. The spec's testing table put the equivalence test in `test_dash_map.c`. It cannot go there -- the accumulator lived in `map_view.cxx`, which includes SRL and does not run on the host. Task 4 extracts it into `map_edges` first, which is why this plan has six tasks where the spec implies four. The extraction is a pure move with its own test.
2. The spec says "solid beats dashed" without saying how. This plan adds a `MAP_EDGE_SOLID` bit, because `MAP_EDGE_DASH` alone cannot distinguish "only a conditional run crosses here" from "both do".

**Type consistency.** `map_edges_link` takes `(ax, ay, bx, by, kind, flags, arrow)` from Task 6 onward and `(ax, ay, bx, by, kind)` in Tasks 4-5; Task 6 Step 1 states that the earlier calls in the test gain two trailing `0` arguments. `arrow` is three-valued (`0` none, `1` head at `(bx,by)`, `2` head at `(ax,ay)`) and is resolved by the caller, never inferred from `MAP_EXIT_ONEWAY` inside `map_edges_link` — Task 5's canonical ordering means the route's far end is the higher-numbered room rather than the passage's destination. `map_layout_glyph` is `(mx, my, pdx, pdy, taken, gx, gy)` in both its definition and its call site. `MAP_EXIT_*` are the model's flags and `MAP_EDGE_*` the layer's bits; `map_edges_link` is the single place they are mapped across.

**One trap this plan closes rather than leaves open.** `DX`/`DY` at `map_model.c:33-34` are file-static *and* are placement steps in room units, not unit steps: the vertical four are two apart. Reaching for them from the glyph placement would put every self-loop mark two cells off. Task 6 Step 5 adds `map_model_step`, which clamps to a unit step and answers north for the four directions that have none on a flat drawing, with its own assertions in `test_map_model.c`.
