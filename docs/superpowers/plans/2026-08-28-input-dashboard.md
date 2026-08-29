# Input Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Draw a dark grey stone-marble panel behind the three on-screen input
strips, on the unused VDP2 NBG2 cell layer, sized per input configuration.

**Architecture:** A pure-logic half (`dash_map.c`) owns a work-RAM shadow of the
NBG2 pattern-name map and paints it from a per-variant geometry table; a hardware
half (`dash_view.cxx`) brings up NBG2 in VRAM bank B0 and copies the dirty rows
into VRAM during vblank on the existing `OnAfterSync` hook. The renderers stop
printing their ASCII borders when the layer is up and keep printing them when it
is not.

**Tech Stack:** C99 (pure half), C++ with SaturnRingLib/SGL (hardware half),
Python 3 (tile generator), `gcc` for host tests, `sh2eb-elf-g++ -fsyntax-only`
via `saturn/syntax-check.sh` for the compile gate.

**Spec:** `docs/superpowers/specs/2026-08-28-input-dashboard-design.md`

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
- **`dash_map.c` and `dash_map.h` must not include `srl.hpp` or any SRL header.**
  That is what lets `saturn/tests/test_dash_map.c` link on the host.
- Screen is 320x224 = 40x28 text cells. `TOP_MARGIN` is 1. The ten-row variants
  occupy screen rows 18..27 inclusive; row 27 is the last row and column 39 the
  last column, so nothing may be painted outside the panel rectangle.
- NBG2 lives in VRAM **bank B0**, allocated explicitly. Never let SRL
  auto-allocate it — the auto path tries bank A0 first and A0 holds the NBG0
  wallpaper bitmap.
- NBG2 uses CRAM **palette number 1**, entries 16..31. Palette 0 (entries 0..15)
  belongs to the NBG3 font; entries 256+ belong to the wallpaper.
- Pattern names are **2-word** (`PNB_2WORD` == 0), cells `CHAR_SIZE_1x1` == 0,
  plane `PL_SIZE_1x1` == 0, colour `Paletted16`.
- The netbin build's source list is explicit in `saturn/makefile` and gated by
  `saturn/tests/test_netbin_sources.py` against exactly 27 objects. **Do not add
  any new file to it.** `dash_view.h` supplies `#ifdef NETBIN` no-op inlines so
  `console_view.cxx` still compiles there with no link edge.

---

### Task 1: `dash_map` infrastructure and the one-row bar

**Files:**
- Create: `saturn/src/video/dash_map.h`
- Create: `saturn/src/video/dash_map.c`
- Test: `saturn/tests/test_dash_map.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `void dash_build(int variant, int base_row)`;
  `unsigned char dash_cell(int x, int y)`; `int dash_dirty_top(void)`;
  `int dash_dirty_bottom(void)`; `void dash_dirty_clear(void)`;
  `void dash_reset(void)`; the `DT_*` tile enum; the `DASH_*` variant enum;
  `DASH_COLS` (40) and `DASH_ROWS` (32).
  `dash_dirty_bottom()` returns a value below `dash_dirty_top()` when clean.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_dash_map.c`:

```c
/* Build:
     gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c \
         saturn/src/video/dash_map.c && /tmp/tdm
   dash_map.c is deliberately free of SRL includes so this links on the host. */
#include "../src/video/dash_map.h"
#include <assert.h>
#include <stdio.h>

static int row_all(int y, unsigned char t) {
    int x;
    for (x = 0; x < DASH_COLS; x++) if (dash_cell(x, y) != t) return 0;
    return 1;
}

int main(void) {
    int x, y;

    dash_reset();

    /* Nothing is painted before the first build. */
    for (y = 0; y < DASH_ROWS; y++) assert(row_all(y, DT_BLANK));
    assert(dash_dirty_bottom() < dash_dirty_top());

    /* The one-row bar carries both bevels and two end caps. */
    dash_build(DASH_LINE, 27);
    assert(dash_cell(0, 27)  == DT_BAR_L);
    assert(dash_cell(39, 27) == DT_BAR_R);
    for (x = 1; x <= 38; x++) assert(dash_cell(x, 27) == DT_BAR);

    /* It uses none of the frame tiles -- a one-row panel cannot borrow a
       corner or an edge, because each of those carries only one bevel. */
    for (x = 0; x < DASH_COLS; x++) {
        unsigned char t = dash_cell(x, 27);
        assert(t == DT_BAR_L || t == DT_BAR || t == DT_BAR_R);
    }

    /* Nothing lands on any other row. */
    for (y = 0; y < DASH_ROWS; y++) if (y != 27) assert(row_all(y, DT_BLANK));

    /* One row changed, so the dirty span is that row alone. */
    assert(dash_dirty_top() == 27);
    assert(dash_dirty_bottom() == 27);

    /* A repeat build with the same variant and base changes nothing. */
    dash_dirty_clear();
    assert(dash_dirty_bottom() < dash_dirty_top());
    dash_build(DASH_LINE, 27);
    assert(dash_dirty_bottom() < dash_dirty_top());
    assert(dash_cell(0, 27) == DT_BAR_L);

    /* Moving the bar clears the row it left. */
    dash_build(DASH_LINE, 20);
    assert(row_all(27, DT_BLANK));
    assert(dash_cell(0, 20) == DT_BAR_L);
    assert(dash_dirty_top() == 20);
    assert(dash_dirty_bottom() == 27);

    /* An out-of-range variant is ignored rather than trusted. */
    dash_build(99, 20);
    assert(dash_cell(0, 20) == DT_BAR_L);

    printf("test_dash_map: ok\n");
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdm
```
Expected: FAIL — `dash_map.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/video/dash_map.h`:

```c
/*----------------------
 | dash_map.h
 | Description: The input dashboard's tile vocabulary, its per-variant geometry,
 |   and the work-RAM shadow of the NBG2 pattern-name map. Pure logic -- no SRL
 |   and no VRAM; dash_view.cxx owns every write to hardware. Implemented in
 |   dash_map.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef DASH_MAP_H
#define DASH_MAP_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | DASH_COLS / DASH_ROWS
 | Description: The shadow's shape: the 40 columns a 320-pixel screen shows, and
 |   32 rows covering the 28 the program draws on with room to spare, matching
 |   text_map's TEXT_ROWS. The hardware map's pitch is 64 cells; dash_view
 |   supplies that when it flushes.
 | Author: suinevere
 ----------------------*/
#define DASH_COLS 40
#define DASH_ROWS 32

/*----------------------
 | DT_BLANK .. DT_N
 | Description: The tile set, in the order dash_tiles.c stores it. DT_BLANK is
 |   fully transparent and is what every cell outside the panel holds, so the
 |   wallpaper shows through. DT_FIELD0 begins the 4x4 marble patch, addressed
 |   by the low two bits of the cell's screen coordinates, which is what gives
 |   the stone a 32-pixel repeat instead of an 8-pixel one.
 | Author: suinevere
 ----------------------*/
enum {
    DT_BLANK = 0,
    DT_FIELD0 = 1,
    DT_CORNER_TL = 17, DT_CORNER_TR, DT_CORNER_BL, DT_CORNER_BR,
    DT_EDGE_TOP = 21, DT_EDGE_BOTTOM, DT_EDGE_LEFT, DT_EDGE_RIGHT,
    DT_GROOVE = 25, DT_GROOVE_L, DT_GROOVE_R,
    DT_DIVIDER = 28, DT_DIV_TOP, DT_DIV_BOTTOM, DT_DIV_CROSS,
    DT_BAR_L = 32, DT_BAR, DT_BAR_R,
    DT_N = 35
};

/*----------------------
 | DASH_NONE .. DASH_VARIANT_N
 | Description: The panel's three shapes, plus the nothing-painted state the
 |   shadow starts in. PANEL and GAMEKB are the two in-game gamepad strips; LINE
 |   is the single bevelled row a real keyboard's prompt sits in.
 | Author: suinevere
 ----------------------*/
enum { DASH_NONE = 0, DASH_PANEL, DASH_GAMEKB, DASH_LINE, DASH_VARIANT_N };

/*----------------------
 | dash_build
 | Description: Repaints the shadow for `variant` with its top row at
 |   `base_row`, clearing whatever rectangle was painted before. Idempotent: a
 |   call naming the variant and base already showing returns without touching
 |   the shadow or the dirty span, so a renderer may call it every frame. An
 |   out-of-range variant is ignored.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map, g_variant, g_base, g_dirty_top, g_dirty_bottom
 | Params: variant -- one of the DASH_* values; base_row -- screen row of the
 |   panel's first row
 | Returns: N/A
 ----------------------*/
void dash_build(int variant, int base_row);

/*----------------------
 | dash_cell
 | Description: The tile index the shadow holds at (x, y). Out-of-range
 |   coordinates read as DT_BLANK rather than faulting, so a caller clipping at
 |   the screen edge needs no bounds test of its own.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map
 | Params: x -- cell column; y -- cell row
 | Returns: the tile index, or DT_BLANK when off the shadow
 ----------------------*/
unsigned char dash_cell(int x, int y);

/*----------------------
 | dash_dirty_top / dash_dirty_bottom / dash_dirty_clear
 | Description: The span of rows changed since the last clear, and the call that
 |   closes it. The span is empty when bottom is below top, which is the state a
 |   fresh shadow and a just-flushed one both hold.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_dirty_top, g_dirty_bottom
 | Params: N/A
 | Returns: top and bottom return row numbers; clear returns N/A
 ----------------------*/
int  dash_dirty_top(void);
int  dash_dirty_bottom(void);
void dash_dirty_clear(void);

/*----------------------
 | dash_reset
 | Description: Blanks the shadow, forgets the painted variant, and empties the
 |   dirty span. Used at init and by tests.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map, g_variant, g_base, g_dirty_top, g_dirty_bottom
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void dash_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* DASH_MAP_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/video/dash_map.c`:

```c
/*----------------------
 | dash_map.c
 | Description: Implements the dashboard's geometry table and the painting of
 |   the NBG2 map shadow. Nothing here touches hardware; see dash_map.h.
 | Author: suinevere
 | Dependencies: dash_map.h
 ----------------------*/
#include "dash_map.h"

/*----------------------
 | DashGeom
 | Description: One variant's shape. Columns are absolute screen cells and both
 |   ends are inclusive, so x0 and x1 are the frame itself rather than the space
 |   inside it. rule_row is the content row carrying a horizontal rule inside the
 |   rightmost module, counted from the first content row, or -1 for none.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned char rows;
    unsigned char x0;
    unsigned char x1;
    unsigned char ndiv;
    unsigned char div[2];
    signed char   rule_row;
} DashGeom;

/*----------------------
 | g_geom
 | Description: The three variants, and the empty one the shadow starts in.
 |   PANEL closes at column 39 and GAMEKB at 38 because their printed borders do;
 |   reproducing that keeps every existing text position exactly where it is.
 | Author: suinevere
 ----------------------*/
static const DashGeom g_geom[DASH_VARIANT_N] = {
    {  0, 0,  0, 0, {  0, 0 }, -1 },
    { 10, 0, 39, 2, { 14, 30 }, -1 },
    { 10, 0, 38, 1, { 14,  0 },  2 },
    {  1, 0, 39, 0, {  0, 0 }, -1 }
};

/*----------------------
 | g_map / g_variant / g_base / g_dirty_top / g_dirty_bottom
 | Description: The shadow, what is painted in it, and the row span changed
 |   since the last flush.
 | Author: suinevere
 ----------------------*/
static unsigned char g_map[DASH_ROWS][DASH_COLS];
static int g_variant = DASH_NONE;
static int g_base = 0;
static int g_dirty_top = DASH_ROWS;
static int g_dirty_bottom = -1;

/*----------------------
 | put
 | Description: Writes one cell, widening the dirty span only when the value
 |   actually changes, so a repaint that lands on identical tiles costs no VRAM
 |   traffic.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map, g_dirty_top, g_dirty_bottom
 | Params: x -- column; y -- row; t -- tile index
 | Returns: N/A
 ----------------------*/
static void put(int x, int y, unsigned char t)
{
    if (x < 0 || x >= DASH_COLS || y < 0 || y >= DASH_ROWS) return;
    if (g_map[y][x] == t) return;
    g_map[y][x] = t;
    if (y < g_dirty_top)    g_dirty_top = y;
    if (y > g_dirty_bottom) g_dirty_bottom = y;
}

/*----------------------
 | cell_at
 | Description: The tile one cell of a variant wants. Only the one-row bar is
 |   handled here; the ten-row frame arrives in a later task.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: g -- the variant's geometry; r -- row within the panel; x -- absolute
 |   column; y -- absolute row
 | Returns: the tile index
 ----------------------*/
static unsigned char cell_at(const DashGeom *g, int r, int x, int y)
{
    (void) r;
    (void) y;
    if (x == g->x0) return DT_BAR_L;
    if (x == g->x1) return DT_BAR_R;
    return DT_BAR;
}

/*----------------------
 | clear_painted
 | Description: Blanks the rectangle the current variant occupies. A no-op at
 |   boot, where the painted variant is DASH_NONE and its height is zero.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_geom, g_variant, g_base
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void clear_painted(void)
{
    const DashGeom *g = &g_geom[g_variant];
    int r, x;
    for (r = 0; r < g->rows; r++)
        for (x = g->x0; x <= g->x1; x++) put(x, g_base + r, DT_BLANK);
}

void dash_build(int variant, int base_row)
{
    const DashGeom *g;
    int r, x;

    if (variant < 0 || variant >= DASH_VARIANT_N) return;
    if (variant == g_variant && base_row == g_base) return;

    clear_painted();
    g_variant = variant;
    g_base = base_row;
    g = &g_geom[variant];

    for (r = 0; r < g->rows; r++)
        for (x = g->x0; x <= g->x1; x++)
            put(x, base_row + r, cell_at(g, r, x, base_row + r));
}

unsigned char dash_cell(int x, int y)
{
    if (x < 0 || x >= DASH_COLS || y < 0 || y >= DASH_ROWS) return DT_BLANK;
    return g_map[y][x];
}

int  dash_dirty_top(void)    { return g_dirty_top; }
int  dash_dirty_bottom(void) { return g_dirty_bottom; }

void dash_dirty_clear(void)
{
    g_dirty_top = DASH_ROWS;
    g_dirty_bottom = -1;
}

void dash_reset(void)
{
    int y, x;
    for (y = 0; y < DASH_ROWS; y++)
        for (x = 0; x < DASH_COLS; x++) g_map[y][x] = DT_BLANK;
    g_variant = DASH_NONE;
    g_base = 0;
    dash_dirty_clear();
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdm
```
Expected: PASS — `test_dash_map: ok`.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/video/dash_map.h saturn/src/video/dash_map.c saturn/tests/test_dash_map.c
git commit -m "Add the input dashboard's map shadow, its variant geometry table and the one-row bevelled bar a real keyboard's prompt sits in."
```

---

### Task 2: the ten-row frame

**Files:**
- Modify: `saturn/src/video/dash_map.c` (`cell_at`)
- Test: `saturn/tests/test_dash_map.c` (extend)

**Interfaces:**
- Consumes: `DashGeom`, `cell_at`, `put`, the `DT_*` enum from Task 1.
- Produces: no new symbols. `cell_at` now handles `rows > 1` — the outer frame,
  the groove row at panel row 1, and the marble field.

The row anatomy this implements, for a panel whose first row is `base`:

| Panel row | Contents |
| ---: | --- |
| 0 | outer top bevel, in the same cells the input line prints over |
| 1 | horizontal groove: floor of the input well, ceiling of the modules |
| 2..rows-2 | content: left edge, marble field, right edge |
| rows-1 | outer bottom edge |

- [ ] **Step 1: Write the failing test**

Insert into `saturn/tests/test_dash_map.c`, immediately before the
`printf("test_dash_map: ok\n");` line:

```c
    /* The command panel: ten rows starting at 18, closing at column 39. */
    dash_build(DASH_PANEL, 18);
    assert(dash_cell(0, 18)  == DT_CORNER_TL);
    assert(dash_cell(39, 18) == DT_CORNER_TR);
    assert(dash_cell(20, 18) == DT_EDGE_TOP);
    assert(dash_cell(0, 27)  == DT_CORNER_BL);
    assert(dash_cell(39, 27) == DT_CORNER_BR);
    assert(dash_cell(20, 27) == DT_EDGE_BOTTOM);
    assert(dash_cell(0, 19)  == DT_GROOVE_L);
    assert(dash_cell(39, 19) == DT_GROOVE_R);
    assert(dash_cell(20, 19) == DT_GROOVE);
    assert(dash_cell(0, 22)  == DT_EDGE_LEFT);
    assert(dash_cell(39, 22) == DT_EDGE_RIGHT);

    /* The field is addressed by the cell's own screen coordinates, so the
       stone is continuous across the panel and repeats every four cells. */
    assert(dash_cell(20, 22) == DT_FIELD0 + ((22 & 3) << 2) + (20 & 3));
    assert(dash_cell(24, 26) == DT_FIELD0 + ((26 & 3) << 2) + (24 & 3));
    assert(dash_cell(20, 22) == dash_cell(24, 26));

    /* Nothing above the panel and nothing past its right edge. */
    assert(row_all(17, DT_BLANK));

    /* The keyboard strip closes one column earlier, and column 39 stays clear. */
    dash_build(DASH_GAMEKB, 18);
    assert(dash_cell(38, 18) == DT_CORNER_TR);
    assert(dash_cell(38, 27) == DT_CORNER_BR);
    /* Row 21, not 22: screen row 22 is this variant's inner-rule row, which
       Task 3 turns into a groove. */
    assert(dash_cell(38, 21) == DT_EDGE_RIGHT);
    for (y = 18; y <= 27; y++) assert(dash_cell(39, y) == DT_BLANK);

    /* Shrinking to the one-row bar clears all nine rows the panel left. */
    dash_build(DASH_LINE, 27);
    for (y = 18; y <= 26; y++) assert(row_all(y, DT_BLANK));
    assert(dash_cell(0, 27) == DT_BAR_L);
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdm
```
Expected: FAIL — assertion at the `DT_CORNER_TL` line, because `cell_at`
currently returns `DT_BAR_L` for every variant's first column.

- [ ] **Step 3: Replace `cell_at`**

In `saturn/src/video/dash_map.c`, replace the whole `cell_at` function and its
header block with:

```c
/*----------------------
 | cell_at
 | Description: The tile one cell of a variant wants. A one-row variant is all
 |   bar; a taller one is a top bevel, a groove row under it, content rows, and
 |   a bottom edge. The top bevel shares its cells with the input line because
 |   the panel is one row taller than the box it replaces and there is nowhere
 |   else for it to go.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: g -- the variant's geometry; r -- row within the panel; x -- absolute
 |   column; y -- absolute row
 | Returns: the tile index
 ----------------------*/
static unsigned char cell_at(const DashGeom *g, int r, int x, int y)
{
    if (g->rows == 1) {
        if (x == g->x0) return DT_BAR_L;
        if (x == g->x1) return DT_BAR_R;
        return DT_BAR;
    }
    if (r == 0) {
        if (x == g->x0) return DT_CORNER_TL;
        if (x == g->x1) return DT_CORNER_TR;
        return DT_EDGE_TOP;
    }
    if (r == g->rows - 1) {
        if (x == g->x0) return DT_CORNER_BL;
        if (x == g->x1) return DT_CORNER_BR;
        return DT_EDGE_BOTTOM;
    }
    if (r == 1) {
        if (x == g->x0) return DT_GROOVE_L;
        if (x == g->x1) return DT_GROOVE_R;
        return DT_GROOVE;
    }
    if (x == g->x0) return DT_EDGE_LEFT;
    if (x == g->x1) return DT_EDGE_RIGHT;
    return (unsigned char) (DT_FIELD0 + ((y & 3) << 2) + (x & 3));
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdm
```
Expected: PASS — `test_dash_map: ok`.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/video/dash_map.c saturn/tests/test_dash_map.c
git commit -m "Paint the ten-row dashboard frame: a top bevel sharing the input line's cells, a groove row under it, and marble field addressed by screen coordinate so the stone runs continuously across the panel."
```

---

### Task 3: dividers, junctions, and the keyboard's inner rule

**Files:**
- Modify: `saturn/src/video/dash_map.c` (`cell_at`, plus a new `is_div` helper)
- Test: `saturn/tests/test_dash_map.c` (extend)

**Interfaces:**
- Consumes: everything from Tasks 1 and 2.
- Produces: no new public symbols. `cell_at` now emits `DT_DIVIDER`,
  `DT_DIV_TOP`, `DT_DIV_BOTTOM` and `DT_DIV_CROSS`, and paints the inner rule
  from the rightmost divider to the right edge on `rule_row`.

`DASH_GAMEKB`'s inner rule replaces the `-----` string `render_game_keyboard`
prints at its content row 2, which is screen row `base + 4`.

- [ ] **Step 1: Write the failing test**

Insert into `saturn/tests/test_dash_map.c`, immediately before the
`printf("test_dash_map: ok\n");` line:

```c
    /* Two dividers on the command panel, running from the groove row down to
       the bottom edge and joining both. */
    dash_build(DASH_PANEL, 18);
    assert(dash_cell(14, 19) == DT_DIV_TOP);
    assert(dash_cell(30, 19) == DT_DIV_TOP);
    assert(dash_cell(14, 22) == DT_DIVIDER);
    assert(dash_cell(30, 26) == DT_DIVIDER);
    assert(dash_cell(14, 27) == DT_DIV_BOTTOM);
    assert(dash_cell(30, 27) == DT_DIV_BOTTOM);
    /* The command panel has no inner rule. */
    assert(dash_cell(20, 22) == DT_FIELD0 + ((22 & 3) << 2) + (20 & 3));

    /* The keyboard strip has one divider and one inner rule, at screen row 22
       -- content row 2, where render_game_keyboard used to print "-----". */
    dash_build(DASH_GAMEKB, 18);
    assert(dash_cell(14, 19) == DT_DIV_TOP);
    assert(dash_cell(14, 27) == DT_DIV_BOTTOM);
    assert(dash_cell(14, 21) == DT_DIVIDER);
    assert(dash_cell(14, 22) == DT_DIV_CROSS);
    assert(dash_cell(14, 23) == DT_DIVIDER);
    for (x = 15; x <= 37; x++) assert(dash_cell(x, 22) == DT_GROOVE);
    assert(dash_cell(38, 22) == DT_GROOVE_R);

    /* Left of the divider the rule row is ordinary content, so the rose keeps
       its marble and its left edge. */
    assert(dash_cell(0, 22) == DT_EDGE_LEFT);
    assert(dash_cell(7, 22) == DT_FIELD0 + ((22 & 3) << 2) + (7 & 3));
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdm
```
Expected: FAIL — assertion at the `DT_DIV_TOP` line; the groove row currently
returns `DT_GROOVE` at column 14.

- [ ] **Step 3: Add `is_div` and extend `cell_at`**

In `saturn/src/video/dash_map.c`, insert this function directly above `cell_at`:

```c
/*----------------------
 | is_div
 | Description: Whether a column carries one of the variant's vertical dividers.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: g -- the variant's geometry; x -- absolute column
 | Returns: 1 when x is a divider column, 0 otherwise
 ----------------------*/
static int is_div(const DashGeom *g, int x)
{
    int i;
    for (i = 0; i < g->ndiv; i++) if (g->div[i] == x) return 1;
    return 0;
}
```

Then, in `cell_at`, make these four edits, leaving the rest of the function as
Task 2 left it:

Add as the first two statements of the function body:

```c
    int rule = (g->rule_row >= 0 && r == g->rule_row + 2);
    int last = g->ndiv ? g->div[g->ndiv - 1] : g->x0;
```

In the `r == g->rows - 1` branch, insert before its `return DT_EDGE_BOTTOM;`:

```c
        if (is_div(g, x)) return DT_DIV_BOTTOM;
```

In the `r == 1` branch, insert before its `return DT_GROOVE;`:

```c
        if (is_div(g, x)) return DT_DIV_TOP;
```

And immediately before the final `if (x == g->x0) return DT_EDGE_LEFT;` line,
insert:

```c
    if (rule && x >= last) {
        if (x == last)   return DT_DIV_CROSS;
        if (x == g->x1)  return DT_GROOVE_R;
        return DT_GROOVE;
    }
```

Finally, insert before the function's closing field return:

```c
    if (is_div(g, x)) return DT_DIVIDER;
```

- [ ] **Step 4: Run the test to verify it passes**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdm
```
Expected: PASS — `test_dash_map: ok`.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/video/dash_map.c saturn/tests/test_dash_map.c
git commit -m "Cut the module dividers into the dashboard as grooves that join the groove row above and the bottom edge below, and give the keyboard strip its inner rule as a groove leaving the divider rather than a printed row of dashes."
```

---

### Task 4: hide the panel when no renderer claims it

**Files:**
- Modify: `saturn/src/video/dash_map.h` (declare `dash_frame_end`)
- Modify: `saturn/src/video/dash_map.c` (implement it)
- Test: `saturn/tests/test_dash_map.c` (extend)

**Interfaces:**
- Consumes: `dash_build`, `dash_cell`, the dirty span, from Tasks 1-3.
- Produces: `void dash_frame_end(void)`.

**Why this task exists.** The printed borders disappeared for free: a menu or the
title screen clears the text rows and the frame goes with them. A VDP2 cell layer
does not work that way — once painted it stays painted, so without this the
marble would sit behind every menu, the Options pages and the title screen.

Rather than add a `dash_hide()` call to every screen that leaves the console view
— a list that is wrong the moment someone adds a screen — the panel expires. Each
renderer already calls `dash_set` every frame it draws. `dash_frame_end` runs
once per frame from the flush hook: if no `dash_set` arrived since the last one,
it builds `DASH_NONE`, which clears the painted rectangle and paints nothing. The
panel therefore vanishes one frame after the console view stops drawing, and no
screen has to know the dashboard exists.

- [ ] **Step 1: Write the failing test**

Insert into `saturn/tests/test_dash_map.c`, immediately before the
`printf("test_dash_map: ok\n");` line:

```c
    /* A frame in which a renderer claimed the panel leaves it up. */
    dash_build(DASH_PANEL, 18);
    dash_dirty_clear();
    dash_frame_end();
    assert(dash_cell(0, 18) == DT_CORNER_TL);
    assert(dash_dirty_bottom() < dash_dirty_top());

    /* A frame in which nobody claimed it takes it down -- this is what keeps
       the marble from sitting behind a menu or the title screen. */
    dash_frame_end();
    for (y = 18; y <= 27; y++) assert(row_all(y, DT_BLANK));
    assert(dash_dirty_top() == 18);
    assert(dash_dirty_bottom() == 27);

    /* Once down it stays down, and costs nothing to keep down. */
    dash_dirty_clear();
    dash_frame_end();
    assert(dash_dirty_bottom() < dash_dirty_top());

    /* And it comes back when a renderer claims it again. */
    dash_build(DASH_GAMEKB, 18);
    dash_frame_end();
    assert(dash_cell(0, 18) == DT_CORNER_TL);
    assert(dash_cell(38, 18) == DT_CORNER_TR);
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdm
```
Expected: FAIL — `implicit declaration of function 'dash_frame_end'`.

- [ ] **Step 3: Declare it**

In `saturn/src/video/dash_map.h`, immediately above the `dash_reset` block, add:

```c
/*----------------------
 | dash_frame_end
 | Description: Closes a frame. When no dash_build call arrived during it, takes
 |   the panel down by building DASH_NONE. A printed border vanished for free
 |   when a menu cleared the text rows; a cell layer does not, so the panel
 |   expires instead of relying on every screen that leaves the console view to
 |   remember to hide it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_touched
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void dash_frame_end(void);
```

- [ ] **Step 4: Implement it**

In `saturn/src/video/dash_map.c`, add to the globals block:

```c
static int g_touched = 0;
```

Add `g_touched = 1;` as the last statement of `dash_build`, after the painting
loop, and set `g_touched = 0;` in `dash_reset`. Then add, directly above
`dash_reset`:

```c
void dash_frame_end(void)
{
    if (!g_touched) dash_build(DASH_NONE, 0);
    g_touched = 0;
}
```

`dash_build` sets `g_touched` on every call including the ones it early-returns
from, so move the early return's `return` to set the flag first:

```c
    if (variant == g_variant && base_row == g_base) { g_touched = 1; return; }
```

- [ ] **Step 5: Run the test to verify it passes**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdm
```
Expected: PASS — `test_dash_map: ok`.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/video/dash_map.h saturn/src/video/dash_map.c saturn/tests/test_dash_map.c
git commit -m "Expire the dashboard a frame after the console view stops drawing, so a cell layer that nothing clears cannot sit behind the menus and the title screen the way a printed border never could."
```

---

### Task 5: the tile generator and `dash_tiles.c`

**Files:**
- Create: `tools/gen_dash_tiles.py`
- Create: `saturn/src/video/dash_tiles.c` (generated by the above)
- Create: `saturn/src/video/dash_tiles.h`
- Test: `saturn/tests/test_dash_tiles.c`

**Interfaces:**
- Consumes: the `DT_*` enum and `DT_N` from `dash_map.h` (Task 1).
- Produces: `extern const unsigned char dash_tile_data[DT_N][32];` and
  `extern const unsigned short dash_palette[16];` — 8x8 4bpp tiles, two pixels
  per byte with the left pixel in the high nibble, and sixteen Saturn RGB555
  words for CRAM entries 16..31.

- [ ] **Step 1: Write the generator**

Create `tools/gen_dash_tiles.py`:

```python
#!/usr/bin/env python3
"""Emit saturn/src/video/dash_tiles.c: the input dashboard's 35 8x8 4bpp tiles
and its 16-entry RGB555 palette. Deterministic -- the marble is seeded noise, so
re-running reproduces the same file byte for byte.

Usage: python3 tools/gen_dash_tiles.py > saturn/src/video/dash_tiles.c
"""
import random

N = 35
SEED = 20260828

# Palette index by role. The blue channel runs two steps above red and green
# throughout, which is what makes the grey read as stone.
PALETTE = [
    None,          # 0  transparent
    (2, 2, 3),     # 1  drop shadow
    (4, 4, 6),     # 2  groove, deep
    (6, 6, 8),     # 3  groove
    (8, 8, 10),    # 4  shadow face
    (10, 10, 12),  # 5  stone body
    (11, 11, 13),  # 6
    (12, 12, 14),  # 7
    (14, 14, 16),  # 8
    (15, 15, 17),  # 9
    (16, 16, 18),  # 10 veining
    (18, 18, 20),  # 11
    (20, 20, 22),  # 12
    (22, 22, 24),  # 13 bevel highlight
    (24, 24, 26),  # 14
    (27, 27, 29),  # 15 specular edge
]

BODY = 7

# Light comes from the top left. Bevels are three pixels deep on the lit sides
# and three on the shaded, and the darkest entry sits in the final pixel row and
# column rather than outside the panel: the ten-row variants already end on
# screen row 27 and column 39, so there is no cell left to cast into.
LIT = [15, 14, 13]
SHADE = [4, 2, 1]
GROOVE = [3, 2, 13]


def rgb555(c):
    if c is None:
        return 0x0000
    r, g, b = c
    return 0x8000 | (b << 10) | (g << 5) | r


def marble(n=32):
    rnd = random.Random(SEED)
    f = [[rnd.random() for _ in range(n)] for _ in range(n)]
    for _ in range(2):
        f = [[sum(f[(y + dy) % n][(x + dx) % n]
                  for dy in (-1, 0, 1) for dx in (-1, 0, 1)) / 9.0
              for x in range(n)] for y in range(n)]
    lo = min(min(r) for r in f)
    hi = max(max(r) for r in f)
    span = (hi - lo) or 1.0
    out = [[0] * n for _ in range(n)]
    for y in range(n):
        for x in range(n):
            v = (f[y][x] - lo) / span
            vein = (f[(y + x) % n][x] - lo) / span
            if vein > 0.72:
                p = 10 + int((vein - 0.72) * 10.0)
                out[y][x] = min(12, p)
            else:
                out[y][x] = 5 + min(4, int(v * 5.0))
    return out


def blank():
    return [[0] * 8 for _ in range(8)]


def solid(v):
    return [[v] * 8 for _ in range(8)]


def apply_top(t):
    for i, v in enumerate(LIT):
        for x in range(8):
            t[i][x] = v
    return t


def apply_bottom(t):
    for i, v in enumerate(SHADE):
        for x in range(8):
            t[5 + i][x] = v
    return t


def apply_left(t):
    for i, v in enumerate(LIT):
        for y in range(8):
            t[y][i] = v
    return t


def apply_right(t):
    for i, v in enumerate(SHADE):
        for y in range(8):
            t[y][5 + i] = v
    return t


def apply_hgroove(t):
    for i, v in enumerate(GROOVE):
        for x in range(8):
            t[3 + i][x] = v
    return t


def apply_vgroove(t):
    for i, v in enumerate(GROOVE):
        for y in range(8):
            t[y][3 + i] = v
    return t


def build():
    tiles = [blank()]

    patch = marble()
    for cy in range(4):
        for cx in range(4):
            tiles.append([[patch[cy * 8 + y][cx * 8 + x] for x in range(8)]
                          for y in range(8)])

    tiles.append(apply_top(apply_left(solid(BODY))))          # 17 corner TL
    tiles.append(apply_right(apply_top(solid(BODY))))         # 18 corner TR
    tiles.append(apply_bottom(apply_left(solid(BODY))))       # 19 corner BL
    tiles.append(apply_bottom(apply_right(solid(BODY))))      # 20 corner BR

    tiles.append(apply_top(solid(BODY)))                      # 21 edge top
    tiles.append(apply_bottom(solid(BODY)))                   # 22 edge bottom
    tiles.append(apply_left(solid(BODY)))                     # 23 edge left
    tiles.append(apply_right(solid(BODY)))                    # 24 edge right

    tiles.append(apply_hgroove(solid(BODY)))                  # 25 groove
    tiles.append(apply_left(apply_hgroove(solid(BODY))))      # 26 groove, left
    tiles.append(apply_right(apply_hgroove(solid(BODY))))     # 27 groove, right

    tiles.append(apply_vgroove(solid(BODY)))                  # 28 divider

    t = apply_hgroove(solid(BODY))                            # 29 divider T-down
    for y in range(5, 8):
        for i, v in enumerate(GROOVE):
            t[y][3 + i] = v
    tiles.append(t)

    t = apply_bottom(solid(BODY))                             # 30 divider T-up
    for y in range(0, 5):
        for i, v in enumerate(GROOVE):
            t[y][3 + i] = v
    tiles.append(t)

    t = apply_vgroove(solid(BODY))                            # 31 divider cross
    for x in range(5, 8):
        for i, v in enumerate(GROOVE):
            t[3 + i][x] = v
    tiles.append(t)

    bar = apply_bottom(apply_top(solid(BODY)))
    tiles.append(apply_left([r[:] for r in bar]))             # 32 bar left
    tiles.append([r[:] for r in bar])                         # 33 bar
    tiles.append(apply_right([r[:] for r in bar]))            # 34 bar right

    assert len(tiles) == N, len(tiles)
    return tiles


def emit(tiles):
    print("/*----------------------")
    print(" | dash_tiles.c")
    print(" | Description: The input dashboard's 35 8x8 4bpp tiles and its")
    print(" |   16-entry RGB555 palette for CRAM entries 16..31. Generated by")
    print(" |   tools/gen_dash_tiles.py -- edit that, not this.")
    print(" | Author: suinevere")
    print(" | Dependencies: dash_tiles.h")
    print(" ----------------------*/")
    print('#include "dash_tiles.h"')
    print()
    print("const unsigned short dash_palette[16] = {")
    print("    " + ", ".join("0x%04X" % rgb555(c) for c in PALETTE))
    print("};")
    print()
    print("const unsigned char dash_tile_data[%d][32] = {" % N)
    for i, t in enumerate(tiles):
        row = []
        for y in range(8):
            for x in range(0, 8, 2):
                row.append((t[y][x] << 4) | t[y][x + 1])
        print("    { " + ", ".join("0x%02X" % b for b in row) + " },")
    print("};")


if __name__ == "__main__":
    emit(build())
```

- [ ] **Step 2: Write the header and the failing test**

Create `saturn/src/video/dash_tiles.h`:

```c
/*----------------------
 | dash_tiles.h
 | Description: The generated dashboard tile set and palette. The data is
 |   produced by tools/gen_dash_tiles.py into dash_tiles.c; this only declares
 |   it, so both the Saturn build and the host tests can reach it.
 | Author: suinevere
 | Dependencies: dash_map.h (DT_N)
 ----------------------*/
#ifndef DASH_TILES_H
#define DASH_TILES_H

#include "dash_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | dash_tile_data / dash_palette
 | Description: DT_N tiles of 8x8 4bpp pixels, two per byte with the left pixel
 |   in the high nibble, and the sixteen Saturn RGB555 words that colour them.
 |   Palette word 0 is zero, which is the transparent entry.
 | Author: suinevere
 ----------------------*/
extern const unsigned char  dash_tile_data[DT_N][32];
extern const unsigned short dash_palette[16];

#ifdef __cplusplus
}
#endif
#endif /* DASH_TILES_H */
```

Create `saturn/tests/test_dash_tiles.c`:

```c
/* Build:
     gcc -O2 -I saturn/src -o /tmp/tdt saturn/tests/test_dash_tiles.c \
         saturn/src/video/dash_tiles.c && /tmp/tdt
   dash_tiles.c is generated data and includes no SRL header. */
#include "../src/video/dash_tiles.h"
#include <assert.h>
#include <stdio.h>

static int pixel(int tile, int x, int y) {
    unsigned char b = dash_tile_data[tile][y * 4 + (x >> 1)];
    return (x & 1) ? (b & 0x0F) : (b >> 4);
}

int main(void) {
    int i, x, y;

    /* The transparent tile really is transparent, or the wallpaper would be
       hidden everywhere outside the panel. */
    for (i = 0; i < 32; i++) assert(dash_tile_data[DT_BLANK][i] == 0);

    /* Palette entry 0 must stay transparent; every other entry must be opaque,
       or the stone would show the wallpaper through it. */
    assert(dash_palette[0] == 0x0000);
    for (i = 1; i < 16; i++) assert((dash_palette[i] & 0x8000) != 0);

    /* The marble field uses only body and vein entries -- no bevel, no shadow,
       so a field tile can sit anywhere in the panel without reading as an edge. */
    for (i = 0; i < 16; i++)
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++) {
                int p = pixel(DT_FIELD0 + i, x, y);
                assert(p >= 5 && p <= 12);
            }

    /* Light from the top left: the top edge's first row is the specular entry
       and the bottom edge's last row is the darkest one. */
    for (x = 0; x < 8; x++) {
        assert(pixel(DT_EDGE_TOP, x, 0) == 15);
        assert(pixel(DT_EDGE_BOTTOM, x, 7) == 1);
    }
    for (y = 0; y < 8; y++) {
        assert(pixel(DT_EDGE_LEFT, 0, y) == 15);
        assert(pixel(DT_EDGE_RIGHT, 7, y) == 1);
    }

    /* The one-row bar carries both bevels at once, which is the whole reason
       it cannot reuse an edge tile. */
    for (x = 0; x < 8; x++) {
        assert(pixel(DT_BAR, x, 0) == 15);
        assert(pixel(DT_BAR, x, 7) == 1);
    }

    printf("test_dash_tiles: ok\n");
    return 0;
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tdt saturn/tests/test_dash_tiles.c saturn/src/video/dash_tiles.c && /tmp/tdt
```
Expected: FAIL — `saturn/src/video/dash_tiles.c: No such file or directory`.

- [ ] **Step 4: Generate the data**

Run:
```bash
python3 tools/gen_dash_tiles.py > saturn/src/video/dash_tiles.c
```
Expected: a file of about 50 lines, beginning with the header block and
declaring `dash_palette` then `dash_tile_data`.

- [ ] **Step 5: Run the test to verify it passes**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tdt saturn/tests/test_dash_tiles.c saturn/src/video/dash_tiles.c && /tmp/tdt
```
Expected: PASS — `test_dash_tiles: ok`.

- [ ] **Step 6: Commit**

```bash
git add tools/gen_dash_tiles.py saturn/src/video/dash_tiles.h saturn/src/video/dash_tiles.c saturn/tests/test_dash_tiles.c
git commit -m "Generate the dashboard's marble tile set from seeded noise and a set of bevel rules, with the darkest entry sitting in the bottom and right edges' own last pixels because the panel already reaches the screen's last row and column."
```

---

### Task 6: `dash_view` — NBG2 bring-up and the vblank flush

**Files:**
- Create: `saturn/src/video/dash_view.h`
- Create: `saturn/src/video/dash_view.cxx`

**Interfaces:**
- Consumes: `dash_map.h` (`dash_build`, `dash_cell`, `dash_dirty_*`,
  `dash_frame_end`, `dash_reset`, `DASH_COLS`, `DASH_ROWS`, `DT_N`),
  `dash_tiles.h`.
- Produces: `bool dash_init(void)`, `void dash_set(int variant, int base_row)`,
  `int dash_ready(void)`. Under `NETBIN` all three are inline no-ops and
  `dash_ready()` is a compile-time zero, so `console_view.cxx` compiles in that
  build with no link edge to this file.

There is no host test for this task — it is all SRL and VDP2. The gate is
`syntax-check.sh` in both configurations.

- [ ] **Step 1: Write the header**

Create `saturn/src/video/dash_view.h`:

```c
/*----------------------
 | dash_view.h
 | Description: The input dashboard's hardware half: NBG2 bring-up, and the
 |   vblank flush that copies dash_map's shadow into the pattern name table.
 |   Under NETBIN the whole interface collapses to no-ops, because that build
 |   links neither this file nor the wallpaper and keeps the printed borders.
 | Author: suinevere
 | Dependencies: dash_map.h, dash_tiles.h, srl.hpp
 ----------------------*/
#ifndef DASH_VIEW_H
#define DASH_VIEW_H

#include "dash_map.h"

#ifdef NETBIN

/*----------------------
 | dash_init / dash_set / dash_ready (NETBIN)
 | Description: The netbin has no dashboard. These fold to nothing at every call
 |   site, so the renderers keep printing their ASCII borders and the build gains
 |   neither a source file nor a byte.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: as the real declarations below
 | Returns: init returns false; ready returns 0; set returns N/A
 ----------------------*/
static inline bool dash_init(void) { return false; }
static inline void dash_set(int variant, int base_row) { (void) variant; (void) base_row; }
static inline int  dash_ready(void) { return 0; }

#else

/*----------------------
 | dash_init
 | Description: Allocates NBG2's character patterns and pattern name table in
 |   VRAM bank B0, uploads the tile set and palette, orders the layers, and
 |   subscribes the flush to OnAfterSync. Call once, after text_map_init. A
 |   second call is a no-op. Bank B0 is named explicitly rather than left to
 |   SRL's auto-allocator, which tries A0 first -- and A0 holds the whole NBG0
 |   wallpaper bitmap.
 | Author: suinevere
 | Dependencies: srl.hpp, dash_map.h, dash_tiles.h
 | Globals: g_ready, g_cell, g_map_vram
 | Params: N/A
 | Returns: true when the layer is up, false when either allocation failed
 ----------------------*/
bool dash_init(void);

/*----------------------
 | dash_set
 | Description: Asks for a variant at a base row. Forwards to dash_build, which
 |   is idempotent, so a renderer may call this unconditionally every frame. A
 |   no-op when the layer never came up.
 | Author: suinevere
 | Dependencies: dash_map.h
 | Globals: g_ready
 | Params: variant -- one of the DASH_* values; base_row -- screen row of the
 |   panel's first row
 | Returns: N/A
 ----------------------*/
void dash_set(int variant, int base_row);

/*----------------------
 | dash_ready
 | Description: Whether the dashboard is drawing. Renderers print their ASCII
 |   borders when this is 0, which is what keeps a failed allocation looking
 |   exactly like the build before this feature existed.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ready
 | Params: N/A
 | Returns: 1 when NBG2 is up, 0 otherwise
 ----------------------*/
int dash_ready(void);

#endif /* NETBIN */
#endif /* DASH_VIEW_H */
```

- [ ] **Step 2: Write the implementation**

Create `saturn/src/video/dash_view.cxx`:

```cxx
/*----------------------
 | dash_view.cxx
 | Description: Implements NBG2 bring-up for the input dashboard and the vblank
 |   copy of dash_map's shadow into the pattern name table. See dash_view.h.
 | Author: suinevere
 | Dependencies: dash_view.h, dash_map.h, dash_tiles.h, srl.hpp
 ----------------------*/
#include <srl.hpp>
#include "dash_view.h"

extern "C" {
#include "dash_tiles.h"
}

#ifndef NETBIN

/*----------------------
 | DASH_MAP_PITCH / DASH_PAL_NO
 | Description: The hardware map's row pitch in cells, and the CRAM palette
 |   number the dashboard writes into every pattern name. Palette 1 is entries
 |   16..31: 0..15 belong to the NBG3 font and 256+ to the wallpaper.
 | Author: suinevere
 ----------------------*/
#define DASH_MAP_PITCH 64
#define DASH_PAL_NO    1

/*----------------------
 | g_ready / g_cell / g_map_vram
 | Description: Whether the layer came up, and where its two allocations landed.
 | Author: suinevere
 ----------------------*/
static bool      g_ready = false;
static void     *g_cell = nullptr;
static uint16_t *g_map_vram = nullptr;
static uint16_t  g_char_base = 0;

/*----------------------
 | flush_hook
 | Description: The OnAfterSync subscriber. Closes the frame first, which takes
 |   the panel down when no renderer claimed it, then writes each dirty row's 40
 |   painted columns as 2-word pattern names and empties the span. Runs in vblank
 |   for the reason text_map.h's file header gives: VDP2 re-reads a cell's
 |   pattern name on every scanline of that cell's row, so a store landing
 |   mid-row shows one tile above the beam and another below it.
 | Author: suinevere
 | Dependencies: dash_map.h
 | Globals: g_map_vram, g_char_base
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void flush_hook(void)
{
    dash_frame_end();

    int top = dash_dirty_top();
    int bottom = dash_dirty_bottom();
    if (bottom < top || g_map_vram == nullptr) return;

    for (int y = top; y <= bottom; y++) {
        volatile uint16_t *dst = g_map_vram + (y * DASH_MAP_PITCH * 2);
        for (int x = 0; x < DASH_COLS; x++) {
            dst[x * 2]     = (uint16_t) DASH_PAL_NO;
            dst[x * 2 + 1] = (uint16_t) (g_char_base + dash_cell(x, y));
        }
    }
    dash_dirty_clear();
}

bool dash_init(void)
{
    if (g_ready) return true;

    const int32_t cellBytes = (int32_t) (DT_N * 32);
    const int32_t mapBytes  = (int32_t) (DASH_MAP_PITCH * DASH_MAP_PITCH * 4);

    g_cell = SRL::VDP2::VRAM::Allocate((uint32_t) cellBytes, 32,
                                       SRL::VDP2::VramBank::B0, 4);
    if (g_cell == nullptr) return false;

    void *map = SRL::VDP2::VRAM::Allocate((uint32_t) mapBytes,
                                          (uint32_t) mapBytes,
                                          SRL::VDP2::VramBank::B0, 4);
    if (map == nullptr) return false;
    g_map_vram = (uint16_t *) map;

    SRL::VDP2::NBG2::SetCellAddress(g_cell, cellBytes);
    SRL::VDP2::NBG2::SetMapAddress(map, mapBytes);

    volatile uint8_t *cell = (volatile uint8_t *) g_cell;
    for (int t = 0; t < DT_N; t++)
        for (int b = 0; b < 32; b++) cell[t * 32 + b] = dash_tile_data[t][b];

    g_char_base = (uint16_t) ((((uint32_t) g_cell) - VDP2_VRAM_A0) >> 5);

    SRL::VDP2::NBG2::TilePalette =
        SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted16, DASH_PAL_NO);
    SRL::VDP2::NBG2::TilePalette.Load(
        (SRL::Types::HighColor *) dash_palette, 16);

    SRL::Tilemap::TilemapInfo info(SRL::CRAM::TextureColorMode::Paletted16,
                                   PNB_2WORD, CHAR_SIZE_1x1, PL_SIZE_1x1,
                                   DASH_MAP_PITCH, DASH_MAP_PITCH, cellBytes);
    SRL::VDP2::NBG2::Init(info);

    dash_reset();
    for (int y = 0; y < DASH_ROWS; y++) {
        volatile uint16_t *dst = g_map_vram + (y * DASH_MAP_PITCH * 2);
        for (int x = 0; x < DASH_COLS; x++) {
            dst[x * 2]     = (uint16_t) DASH_PAL_NO;
            dst[x * 2 + 1] = (uint16_t) (g_char_base + DT_BLANK);
        }
    }

    slPriorityNbg0(1);
    slPriorityNbg2(2);

    SRL::Math::Types::Vector2D origin(0, 0);
    SRL::VDP2::NBG2::SetPosition(origin);
    SRL::VDP2::NBG2::ScrollEnable();

    SRL::Core::OnAfterSync += &flush_hook;
    g_ready = true;
    return true;
}

void dash_set(int variant, int base_row)
{
    if (!g_ready) return;
    dash_build(variant, base_row);
}

int dash_ready(void) { return g_ready ? 1 : 0; }

#endif /* NETBIN */
```

- [ ] **Step 3: Run the compile gate**

Run, from `saturn/`:
```bash
sh syntax-check.sh src/video/dash_view.cxx
```
Expected: PASS in both the DEBUG and release passes.

If SRL rejects `NBG2::SetCellAddress` / `SetMapAddress` / `Init` / `TilePalette`
signatures, read `SaturnRingLib/saturnringlib/srl_vdp2.hpp:408-620` and
`:1060-1095` and correct the call to match; do not change the bank, the palette
number, or `PNB_2WORD`. `Vector2D` lives in `SRL::Math::Types` — if the
unqualified name fails, check `SaturnRingLib/modules/SaturnMathPP` for the
namespace this SRL version uses.

Also confirm here that `SRL::VDP2::VRAM::Allocate`'s fourth parameter is the
cycle count (`srl_vdp2.hpp:128`) and that 4 is accepted; if the signature
differs, match it.

- [ ] **Step 4: Run the netbin compile gate**

Run, from `saturn/`:
```bash
NETBIN=1 sh syntax-check.sh src/video/dash_view.cxx
```
Expected: PASS — the whole file compiles to nothing under `NETBIN`.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/video/dash_view.h saturn/src/video/dash_view.cxx
git commit -m "Bring up NBG2 for the input dashboard in VRAM bank B0 so its fetches never contend with the wallpaper bitmap in A0, and copy the map shadow in vblank on the same hook the text layer already flushes through."
```

---

### Task 7: initialise the dashboard at boot

**Files:**
- Modify: `saturn/src/main.cxx:342` (after the `text_map_init()` call)

**Interfaces:**
- Consumes: `dash_init()` from Task 6.
- Produces: nothing.

`main_netbin.cxx` is deliberately not touched: the netbin keeps the printed
borders, and `dash_init()` is a no-op inline there anyway.

- [ ] **Step 1: Add the include**

In `saturn/src/main.cxx`, alongside the other `video/` includes, add:

```cxx
#include "dash_view.h"
```

- [ ] **Step 2: Call it**

Immediately after the `text_map_init();` line at `saturn/src/main.cxx:342`, add:

```cxx
    dash_init();        // after text_map_init: VDP2 and the font are up, and a
                        // failure here only means the printed borders stay
```

- [ ] **Step 3: Run the compile gate**

Run, from `saturn/`:
```bash
sh syntax-check.sh src/main.cxx
```
Expected: PASS in both passes.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Bring the input dashboard up at boot, after the text layer and before anything draws through it."
```

---

### Task 8: the command panel call site

**Files:**
- Modify: `saturn/src/video/command_view.cxx:873-929` (`render_command_panel`)

**Interfaces:**
- Consumes: `dash_set`, `dash_ready`, `DASH_PANEL`.
- Produces: nothing.

`CV_BORDER` at `command_view.cxx:53` stays in the file. It is now the fallback,
printed only when `dash_ready()` is 0.

- [ ] **Step 1: Add the include**

Near the top of `saturn/src/video/command_view.cxx`, with the other local
includes, add:

```cxx
#include "dash_view.h"
```

- [ ] **Step 2: Rewrite the panel's chrome**

In `render_command_panel`, replace the block that runs from the
`image_window_box(...)` call down to the first `text_print(0, border_top, CV_BORDER);`
with:

```cxx
    int dash = dash_ready();
    dash_set(DASH_PANEL, input_row);

    /* Black behind the box on the fallback path, the way a menu box is black:
       NBG3 leaves palette entry 0 transparent, so over a wallpaper the rose and
       the lists would otherwise be read against the picture. With the dashboard
       up the marble covers the input row too, so the rectangle starts there. */
    image_window_box(0, dash ? input_row : border_top, 40,
                     border_bottom - (dash ? input_row : border_top) + 1);
    image_window_on();

    text_clear_line(input_row);
    text_print(0, input_row, "> %s", p.line);

    text_clear_line(border_top);
    if (!dash) text_print(0, border_top, CV_BORDER);
```

- [ ] **Step 3: Suppress the per-row dividers**

Further down in the same function, inside the `for (row = 0; row < CR_ROWS; row++)`
loop, change each of the four divider prints so they are skipped when the
dashboard is up. Replace:

```cxx
            text_print(0, y, "|");
```
with:
```cxx
            if (!dash) text_print(0, y, "|");
```

Replace:
```cxx
            text_print(14, y, "|");
```
with:
```cxx
            if (!dash) text_print(14, y, "|");
```

Both occurrences of:
```cxx
                text_print(30, y, "|");
```
become:
```cxx
                if (!dash) text_print(30, y, "|");
```

And:
```cxx
            text_print(39, y, "|");
```
becomes:
```cxx
            if (!dash) text_print(39, y, "|");
```

- [ ] **Step 4: Suppress the bottom border**

At the end of the function, replace:

```cxx
    text_clear_line(border_bottom);
    text_print(0, border_bottom, CV_BORDER);
```

with:

```cxx
    text_clear_line(border_bottom);
    if (!dash) text_print(0, border_bottom, CV_BORDER);
```

- [ ] **Step 5: Run the compile gate**

Run, from `saturn/`:
```bash
sh syntax-check.sh src/video/command_view.cxx
```
Expected: PASS in both passes, with no unused-variable warning for `CV_BORDER`
(it is still referenced on the fallback path).

- [ ] **Step 6: Commit**

```bash
git add saturn/src/video/command_view.cxx
git commit -m "Let the marble dashboard frame the command panel, keeping the printed border and dividers as the path taken when the layer never came up."
```

---

### Task 9: the keyboard call sites and the window widening

**Files:**
- Modify: `saturn/src/video/console_view.cxx:715-770` (`render_game_keyboard`)
- Modify: `saturn/src/video/console_view.cxx:793` onward (`render_keyboard`)

**Interfaces:**
- Consumes: `dash_set`, `dash_ready`, `DASH_GAMEKB`, `DASH_LINE`.
- Produces: nothing.

`KB_STRIP_BORDER` at `console_view.cxx:695` stays as the fallback. Under
`NETBIN` `dash_ready()` is a compile-time 0, so this file keeps its present
behaviour in that build and gains no link edge.

- [ ] **Step 1: Add the include**

Near the top of `saturn/src/video/console_view.cxx`, with the other local
includes, add:

```cxx
#include "dash_view.h"
```

- [ ] **Step 2: Rewrite the strip's chrome**

In `render_game_keyboard`, replace the block from `image_window_box(...)` down to
and including the two `KB_STRIP_BORDER` prints with:

```cxx
    int dash = dash_ready();
    dash_set(DASH_GAMEKB, input_row);

    image_window_box(0, dash ? input_row : border_top, 40,
                     border_bottom - (dash ? input_row : border_top) + 1);
    image_window_on();

    text_clear_line(input_row);
    draw_input_line(input_row, k, prediction, current_word_len, block_on);
    if (keyboard_get_caps()) text_print(35, input_row, "CAPS");

    text_clear_line(border_top);
    text_clear_line(border_bottom);
    if (!dash) {
        text_print(0, border_top, KB_STRIP_BORDER);
        text_print(0, border_bottom, KB_STRIP_BORDER);
    }
```

- [ ] **Step 3: Suppress the dividers and the inner rule**

Inside the same function's `for (int r = 0; r < CR_ROWS; r++)` loop, replace:

```cxx
        text_print(0, y, "|");
```
with:
```cxx
        if (!dash) text_print(0, y, "|");
```

Replace:
```cxx
        text_print(14, y, "|");
        text_print(38, y, "|");
```
with:
```cxx
        if (!dash) { text_print(14, y, "|"); text_print(38, y, "|"); }
```

And replace:
```cxx
        if (r == 2) { text_print(15, y, "-----------------------"); continue; }
```
with:
```cxx
        if (r == 2) {
            if (!dash) text_print(15, y, "-----------------------");
            continue;
        }
```

- [ ] **Step 4: Give the real-keyboard branch its bar**

In `render_keyboard`, replace the `if (!g_kbd_visible) { ... }` block at
`console_view.cxx:802-812` with:

```cxx
    if (!g_kbd_visible) {
        /* A real keyboard is in hand: no on-screen interface to back, so drop the
           window and draw the input line over the console's ">" prompt row. */
        if (g_in_game) image_window_off();
        int row = base - 1;
        if (g_in_game) dash_set(DASH_LINE, row);
        text_clear_line(base);
        text_clear_line(row);
        draw_input_line(row, k, prediction, current_word_len, block_on);
        if (g_more_below) text_print(34, row, "more v");
        return;
    }
```

The `g_in_game` gate is what keeps the title screen's online terminal looking
exactly as it does now, which the spec puts out of scope. It needs no matching
hide: the terminal simply never calls `dash_set`, and Task 4's `dash_frame_end`
takes the panel down a frame later. The same is true of the off-game on-screen
keyboard branch further down this function, which is left untouched.

- [ ] **Step 5: Run both compile gates**

Run, from `saturn/`:
```bash
sh syntax-check.sh src/video/console_view.cxx
NETBIN=1 sh syntax-check.sh src/video/console_view.cxx
```
Expected: PASS in all four passes.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/video/console_view.cxx
git commit -m "Let the marble dashboard frame the on-screen keyboard strip and give a real keyboard's prompt its own bevelled bar, keeping every printed border as the fallback the netbin still takes."
```

---

### Task 10: full gate and hardware hand-off

**Files:**
- None modified. This task verifies.

**Interfaces:**
- Consumes: everything.
- Produces: a hand-off note for the author.

- [ ] **Step 1: Run every host test touched by this work**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdm
gcc -O2 -I saturn/src -o /tmp/tdt saturn/tests/test_dash_tiles.c saturn/src/video/dash_tiles.c && /tmp/tdt
```
Expected: `test_dash_map: ok` and `test_dash_tiles: ok`.

- [ ] **Step 2: Confirm the netbin source list is untouched**

Run:
```bash
python3 saturn/tests/test_netbin_sources.py
```
Expected: PASS. No `dash_*` file may appear in the netbin list; the test asserts
that list is exactly 27 objects.

- [ ] **Step 3: Run the compile gate over every changed unit, both configurations**

Run, from `saturn/`:
```bash
sh syntax-check.sh src/main.cxx src/video/dash_view.cxx src/video/command_view.cxx src/video/console_view.cxx
NETBIN=1 sh syntax-check.sh src/video/dash_view.cxx src/video/console_view.cxx
```
Expected: PASS in every pass.

- [ ] **Step 4: Hand off for the real build**

Do not run `compile.bat` or the emulator. Report to the author, listing what
still needs eyes on hardware:

1. **The three variants render.** Start a game with a gamepad and toggle
   between the command panel and the on-screen keyboard; then attach a real
   keyboard and confirm the one-row bar.
2. **Layer order.** `dash_init` sets NBG0 to priority 1 and NBG2 to 2, leaving
   NBG3 at the Layer7 SRL assigns it (`srl_vdp2.hpp:1525`). Setting NBG0
   explicitly is what makes the ordering deterministic instead of dependent on
   an SGL default nobody has read — so check the title screen and any sprite
   work for a regression, since those are what a changed NBG0 priority would
   disturb.
3. **Cycle patterns.** If the wallpaper or the dashboard flickers or drops out,
   the VRAM access cycles could not be satisfied; that is the design's named
   risk and the first thing to suspect.
4. **Contrast.** Walk the Display Options colour presets and confirm none puts
   dark text on the stone body. The spec's remedy is to lighten the body range
   in `tools/gen_dash_tiles.py`, not to constrain the presets.
5. **Two-word pattern names.** Confirm the panel is not drawn in the wrong
   palette; if it is, `SRL::Tilemap::TilemapInfo::MapMode` is the field to
   re-check against `srl_vdp2.hpp:740`.

- [ ] **Step 5: Commit nothing**

This task changes no files. If Steps 1-3 surfaced a defect, fix it in the task
that owns the file and commit there.
