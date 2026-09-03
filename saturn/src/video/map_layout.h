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
 | MAP_CELLS / MAP_ROOMS_W / MAP_ROOMS_H / MAP_CX / MAP_CY / MAP_GUTTER /
 | MAP_LEFT / MAP_TOP / MAP_CLIP_X0 / MAP_CLIP_Y0 / MAP_CELL_W / MAP_CELL_H
 | Description: One room is four text cells -- the original's 32-pixel step
 |   over an 8x8 font -- and the grid is inset so that it lands on the paper
 |   rather than over the torn edge of it. MAP.TGA is a sheet with ragged
 |   edges on a transparent surround, and its cells are solid only from column
 |   two to column thirty-eight and from row three to row twenty-five; a grid
 |   that filled the screen -- which is what ten rooms by seven does exactly --
 |   put marks and lines on the black outside the paper along every edge.
 |
 |   MAP_LEFT and MAP_TOP are where the first room's MARK goes, not where the
 |   drawing starts. MAP_GUTTER cells outside them on all four sides belong to
 |   the drawing too, and hold the stubs that run off the viewport toward a
 |   room scrolled past its edge. Without that margin the marks sat on the
 |   boundary itself and an exit to a room just off screen had nowhere to draw
 |   to, so it drew nothing: scrolling one room north made the passages back to
 |   the rooms you had come from vanish rather than run to the edge.
 |
 |   Nine rooms by five with a two-cell gutter is the largest whole-room grid
 |   that fits the solid band and still leaves the two text rows below it on
 |   paper: marks on columns 4..36 and rows 5..21, drawing on columns 2..38 and
 |   rows 3..23. Both spans are odd, so unlike the ten-by-seven grid the centre
 |   room is the true middle and MAP_DX_MIN/MAX come out symmetric.
 |
 |   MAP_CELL_W/H are exclusive far edges, and the layer is indexed in absolute
 |   screen cells so that nothing between here and dash_map_paint translates
 |   between two coordinate spaces; that leaves MAP_CLIP_X0 columns and
 |   MAP_CLIP_Y0 rows of it allocated and never touched, a few hundred bytes
 |   against every call site staying one subtraction simpler. Named
 |   MAP_ROOMS_W/H rather than MAP_VIEW_W/H because the latter collides with
 |   map_view.h's own include guard (MAP_VIEW_H).
 | Author: suinevere
 ----------------------*/
#define MAP_CELLS    4
#define MAP_ROOMS_W  9
#define MAP_ROOMS_H  5
#define MAP_CX       4
#define MAP_CY       2
#define MAP_GUTTER   2
#define MAP_LEFT     4
#define MAP_TOP      5
#define MAP_CLIP_X0  (MAP_LEFT - MAP_GUTTER)
#define MAP_CLIP_Y0  (MAP_TOP - MAP_GUTTER)
#define MAP_CELL_W   (MAP_LEFT + (MAP_ROOMS_W - 1) * MAP_CELLS + MAP_GUTTER + 1)
#define MAP_CELL_H   (MAP_TOP + (MAP_ROOMS_H - 1) * MAP_CELLS + MAP_GUTTER + 1)

/*----------------------
 | MAP_DX_MIN / MAP_DX_MAX / MAP_DY_MIN / MAP_DY_MAX
 | Description: The offsets from the view centre a room can have and still be
 |   on screen, inclusive. Symmetric in both axes, because both spans are odd
 |   and the centre room really is the middle one.
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
 |   MAP_CX or MAP_CY; base -- MAP_LEFT for a column, MAP_TOP for a row
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
    if (*kx < MAP_CLIP_X0) *kx = mx + 2;
    *ky = my - 1;
}

/*----------------------
 | MAP_OFFVIEW_NONE / MAP_OFFVIEW_RUN / MAP_OFFVIEW_GLYPH / map_layout_offview
 | Description: Which of the map's two ways of saying "there is more that way"
 |   an exit takes when its far end is not on screen: a run laid into the gutter
 |   toward where the far room is drawn, or a U/D glyph beside the mark.
 |
 |   A staircase takes the glyph and must never take the run. It has no
 |   direction on the page -- the room above may be drawn anywhere, or nowhere
 |   -- so a run pointing west because that is where it happens to sit is a
 |   corridor the player cannot walk, drawn in a direction the story never
 |   offered.
 |
 |   Both passes ask this one question rather than each testing its own
 |   condition, which is how the fault it fixes arose. The link pass gave every
 |   ungathered exit to the run; the glyph pass declined any whose far room was
 |   placed on this floor, on the assumption the link pass would draw it
 |   properly -- true only while the far end was on screen. Between them, The
 |   Lurking Horror's Renovated Cave drew a line running north out of the room
 |   for an exit that goes DOWN, and drew no D at all. It was invisible until
 |   enough rooms were placed for the far end of such an exit to be in the table
 |   at all; before that the glyph pass had it to itself and was right.
 | Author: suinevere
 ----------------------*/
enum { MAP_OFFVIEW_NONE = 0, MAP_OFFVIEW_RUN, MAP_OFFVIEW_GLYPH };

static inline int map_layout_offview(int vertical, int on_screen)
{
    if (on_screen) return MAP_OFFVIEW_NONE;
    return vertical ? MAP_OFFVIEW_GLYPH : MAP_OFFVIEW_RUN;
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
    const unsigned short taken[][MAP_CELL_W])
{
    if (x < MAP_CLIP_X0 || y < MAP_CLIP_Y0) return 0;
    if (x >= MAP_CELL_W) return 0;
    if (y >= MAP_CELL_H) return 0;
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
    const unsigned short taken[][MAP_CELL_W], int *gx, int *gy)
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
