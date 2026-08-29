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
 |   Each is the nine-row box the ASCII chrome drew. PANEL closes at column 39
 |   and GAMEKB at 38 because their printed borders do; reproducing that keeps
 |   every existing text position exactly where it is. OVERLAY repeats PANEL's
 |   rectangle with no dividers, for the frames the command panel gives over to
 |   the inventory overlay's own box.
 | Author: suinevere
 ----------------------*/
static const DashGeom g_geom[DASH_VARIANT_N] = {
    { 0, 0,  0, 0, {  0, 0 }, -1 },
    { 9, 0, 39, 2, { 14, 30 }, -1 },
    { 9, 0, 38, 1, { 14,  0 },  2 },
    { 9, 0, 39, 0, {  0, 0 }, -1 }
};

/*----------------------
 | g_map / g_variant / g_base / g_dirty_top / g_dirty_bottom / g_touched
 | Description: The shadow, what is painted in it, the row span changed since
 |   the last flush, and whether a dash_build call has landed since the last
 |   dash_frame_end.
 | Author: suinevere
 ----------------------*/
static unsigned char g_map[DASH_ROWS][DASH_COLS];
static int g_variant = DASH_NONE;
static int g_base = 0;
static int g_dirty_top = DASH_ROWS;
static int g_dirty_bottom = -1;
static int g_touched = 0;

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

/*----------------------
 | cell_at
 | Description: The tile one cell of a variant wants. Each module is drawn as
 |   its own box: the outer frame runs the panel's rectangle, and every divider
 |   column closes the module on its left and opens the one on its right with
 |   two transparent pixels between them. That frame is ten pixels wide where
 |   two boxes meet and a cell is eight, so the columns either side of a divider
 |   carry the highlight that spills out and need their own tiles. Every frame
 |   frame tile carries the field's own marble behind it, so the stone reaches
 |   the highlight: the horizontal runs pick their tile by x & 3, the vertical
 |   ones by y & 3.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: g -- the variant's geometry; r -- row within the panel; x -- absolute
 |   column; y -- absolute row
 | Returns: the tile index
 ----------------------*/
static unsigned char cell_at(const DashGeom *g, int r, int x, int y)
{
    int top  = (r == 0);
    int bot  = (r == g->rows - 1);
    int rule = (g->rule_row >= 0 && r == g->rule_row + 1);

    if (x == g->x0) {
        if (top) return DT_CORNER_TL;
        if (bot) return DT_CORNER_BL;
        return (unsigned char) (DT_LEFT0 + (y & 3));
    }
    if (x == g->x1) {
        if (top) return DT_CORNER_TR;
        if (bot) return DT_CORNER_BR;
        if (rule) return DT_RULE_RIGHT;
        return (unsigned char) (DT_RIGHT0 + (y & 3));
    }
    if (is_div(g, x)) {
        if (top) return DT_TOP_DIVIDER;
        if (bot) return DT_BOTTOM_DIVIDER;
        return (unsigned char) (DT_DIVIDER0 + (y & 3));
    }
    if (is_div(g, x - 1)) {
        if (top) return DT_TOP_MODLEFT;
        if (bot) return DT_BOTTOM_MODLEFT;
        if (rule) return DT_RULE_MODLEFT;
        return (unsigned char) (DT_MODLEFT0 + (y & 3));
    }
    if (top) return (unsigned char) (DT_TOP0 + (x & 3));
    if (bot) return (unsigned char) (DT_BOTTOM0 + (x & 3));
    if (rule && x > g->div[g->ndiv - 1])
        return (unsigned char) (DT_RULE0 + (x & 3));
    return (unsigned char) (DT_FIELD0 + ((y & 3) << 2) + (x & 3));
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
    if (variant == g_variant && base_row == g_base) { g_touched = 1; return; }

    clear_painted();
    g_variant = variant;
    g_base = base_row;
    g = &g_geom[variant];

    for (r = 0; r < g->rows; r++)
        for (x = g->x0; x <= g->x1; x++)
            put(x, base_row + r, cell_at(g, r, x, base_row + r));
    g_touched = 1;
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

void dash_frame_end(void)
{
    if (!g_touched) dash_build(DASH_NONE, 0);
    g_touched = 0;
}

void dash_reset(void)
{
    int y, x;
    for (y = 0; y < DASH_ROWS; y++)
        for (x = 0; x < DASH_COLS; x++) g_map[y][x] = DT_BLANK;
    g_variant = DASH_NONE;
    g_base = 0;
    g_touched = 0;
    dash_dirty_clear();
}
