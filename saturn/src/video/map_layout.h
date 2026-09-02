/*----------------------
 | map_layout.h
 | Description: The map screen's viewport geometry and the three pieces of
 |   arithmetic built on it: which cell an offset lands in, how far the view has
 |   to move to keep the crosshair inside it, and where the figure stands beside
 |   a mark. Header-only and free of SRL, so a host test can exercise the
 |   arithmetic that a build for the target cannot be run to check -- the same
 |   reason panel_layout.h exists beside the overlay.
 |
 |   map_view.cxx is the only caller. The split is not about reuse; it is about
 |   the fact that every defect this file could hold shows up as "the map looks
 |   wrong on a television" and nothing else.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef MAP_LAYOUT_H
#define MAP_LAYOUT_H

/*----------------------
 | MAP_CELLS / MAP_ROOMS_W / MAP_ROOMS_H / MAP_CX / MAP_CY / MAP_TOP
 | Description: One room is four text cells -- the original's 32-pixel step
 |   over an 8x8 font -- so a 320x224 screen shows ten rooms by seven, and the
 |   player sits at the middle one. Seven rooms of four rows is exactly the 28
 |   rows the screen has, so MAP_TOP is zero and the map fills it; it exists as
 |   a name rather than a bare 0 because the ground and the marks must agree on
 |   it, and they did not in an earlier draft. Named MAP_ROOMS_W/H rather than
 |   MAP_VIEW_W/H because the latter collides with map_view.h's own include
 |   guard (which is MAP_VIEW_H).
 | Author: suinevere
 ----------------------*/
#define MAP_CELLS    4
#define MAP_ROOMS_W  10
#define MAP_ROOMS_H  7
#define MAP_CX       5
#define MAP_CY       3
#define MAP_TOP      0

/*----------------------
 | MAP_DX_MIN / MAP_DX_MAX / MAP_DY_MIN / MAP_DY_MAX
 | Description: The offsets from the view centre a room can have and still be
 |   on screen, inclusive. Asymmetric because the centre is not the middle of an
 |   even span: ten columns around column five leaves five to the left and four
 |   to the right.
 | Author: suinevere
 ----------------------*/
#define MAP_DX_MIN (-MAP_CX)
#define MAP_DX_MAX (MAP_ROOMS_W - 1 - MAP_CX)
#define MAP_DY_MIN (-MAP_CY)
#define MAP_DY_MAX (MAP_ROOMS_H - 1 - MAP_CY)

/*----------------------
 | map_layout_visible
 | Description: Whether an offset from the view centre is on screen.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: dx, dy -- the offset from the view centre, in rooms
 | Returns: 1 when the cell is inside the viewport, 0 otherwise
 ----------------------*/
static inline int map_layout_visible(int dx, int dy)
{
    return dx >= MAP_DX_MIN && dx <= MAP_DX_MAX &&
           dy >= MAP_DY_MIN && dy <= MAP_DY_MAX;
}

/*----------------------
 | map_layout_cell
 | Description: The text cell a room offset lands in, given the view's own
 |   offset. Answers a column when given the x pair and a row when given the y
 |   pair, which is why the centre and the top come in as arguments rather than
 |   being read from the defines: one rule, used twice, instead of two that can
 |   drift.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: d -- the room's offset from the player; s -- the view's; centre --
 |   MAP_CX or MAP_CY; base -- 0 for a column, MAP_TOP for a row
 | Returns: the cell
 ----------------------*/
static inline int map_layout_cell(int d, int s, int centre, int base)
{
    return base + (centre + (d - s)) * MAP_CELLS;
}

/*----------------------
 | map_layout_follow
 | Description: Moves the view the least it can to bring the crosshair back
 |   inside the viewport, and leaves it alone when the crosshair is already
 |   there. This is what lets one control do both jobs: the D-pad moves the
 |   cursor, and the map comes along only when it has to. A map that scrolled
 |   under a fixed cursor would need a second binding to reach a room the scroll
 |   clamp had stopped short of.
 |
 |   Both are in the same space -- offsets in rooms from the player -- so the
 |   test is on their difference and the correction is applied to the view.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: hx, hy -- the crosshair; sx, sy -- the view, updated in place
 | Returns: N/A
 ----------------------*/
static inline void map_layout_follow(int hx, int hy, int *sx, int *sy)
{
    if (hx - *sx > MAP_DX_MAX) *sx = hx - MAP_DX_MAX;
    if (hx - *sx < MAP_DX_MIN) *sx = hx - MAP_DX_MIN;
    if (hy - *sy > MAP_DY_MAX) *sy = hy - MAP_DY_MAX;
    if (hy - *sy < MAP_DY_MIN) *sy = hy - MAP_DY_MIN;
}

/*----------------------
 | map_layout_knight
 | Description: Where the figure's top-left cell goes for a mark at (mx, my):
 |   to the left with one cell of clearance, so a link leaving west still shows
 |   where it goes, and flipped to the right when the left would run off the
 |   viewport. dash_map_paint drops cells it cannot place, so without the flip
 |   the alternative is not a figure hanging over the edge but half a figure,
 |   which reads as a drawing fault.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: mx, my -- the mark's cell; w -- the figure's width in cells; kx, ky
 |   -- receive the top-left cell
 | Returns: N/A
 ----------------------*/
static inline void map_layout_knight(int mx, int my, int w, int *kx, int *ky)
{
    *kx = mx - 1 - w;
    if (*kx < 0) *kx = mx + 2;
    *ky = my - 1;
}

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
 |   preferred direction, then one, then a diagonal, then nowhere. Declining is
 |   a real outcome; the caller must handle it rather than assume placement
 |   always succeeds.
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

#endif /* MAP_LAYOUT_H */
