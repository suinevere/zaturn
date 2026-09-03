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
#define MAP_EDGE_SOLID    0x2000

/*----------------------
 | g_edge / g_mark
 | Description: Which sides of each cell a line leaves through, and which cells
 |   a room mark holds. A whole viewport of shorts rather than a sparse list
 |   because the sweep then costs one pass with no lookup, and four kilobytes is
 |   nothing beside the story image the heap is already carrying.
 | Author: suinevere
 ----------------------*/
static unsigned short g_edge[MAP_ROOMS_H * MAP_CELLS][MAP_ROOMS_W * MAP_CELLS];
static unsigned char  g_mark[MAP_ROOMS_H * MAP_CELLS][MAP_ROOMS_W * MAP_CELLS];

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
    if (!in_view(cx, cy)) return;
    g_mark[cy][cx] = 1;
    g_edge[cy][cx] |= MAP_EDGE_SOLID;
}

/*----------------------
 | mark_step
 | Description: Records one cell-to-cell step of a route, setting the side it
 |   leaves the first cell by and the facing side it enters the second by, so the
 |   two tiles meet. Ignores a step either end of which is off the viewport,
 |   which no candidate route should produce but which would corrupt the
 |   neighbouring static if one ever did.
 | Author: suinevere
 | Dependencies: dash_map.h, in_view
 | Globals: g_edge
 | Params: x, y -- the cell stepped from; nx, ny -- the cell stepped to, always
 |   one cell away in exactly one axis; stair -- nonzero for a vertical exit;
 |   flags -- MAP_EDGE_DASH or MAP_EDGE_SOLID, ORed into both cells
 | Returns: N/A
 ----------------------*/
static void mark_step(int x, int y, int nx, int ny, int stair,
                      unsigned short flags)
{
    unsigned short out, in;
    if (!in_view(x, y) || !in_view(nx, ny)) return;

    if      (nx > x) { out = DT_EDGE_E; in = DT_EDGE_W; }
    else if (nx < x) { out = DT_EDGE_W; in = DT_EDGE_E; }
    else if (ny > y) { out = DT_EDGE_S; in = DT_EDGE_N; }
    else             { out = DT_EDGE_N; in = DT_EDGE_S; }

    if (stair) { out |= MAP_EDGE_STAIR; in |= MAP_EDGE_STAIR; }
    out |= flags;
    in  |= flags;
    g_edge[y][x]   |= out;
    g_edge[ny][nx] |= in;
}

/*----------------------
 | trace
 | Description: Walks an orthogonal route through a list of corner points,
 |   either recording it or only testing it. In test mode it stops at the first
 |   cell short of the far end that a room mark holds and answers 0; in record
 |   mode it marks every step and answers 1.
 |
 |   The two modes share one body deliberately: the route that gets drawn has to
 |   be the one that was checked, and two separate walkers would eventually
 |   disagree about which cells that is.
 |
 |   arrow == 1 puts the head on the cell stepped from on the last step of the
 |   walk, pointing the way travel went; arrow == 2 puts it on the cell stepped
 |   to on the first step, pointing back the way it came. Either way the head
 |   lands adjacent to the destination mark, pointing at it, and never in a
 |   mark's own cell, since the walk never steps past (ex, ey).
 | Author: suinevere
 | Dependencies: mark_step
 | Globals: g_mark, g_edge
 | Params: pts -- corner points as x, y pairs; npts -- how many points; stair --
 |   nonzero for a vertical exit; record -- nonzero to mark, zero to only test;
 |   deco -- MAP_EDGE_DASH or MAP_EDGE_SOLID; arrow -- 0 none, 1 head at the far
 |   end, 2 head at the near end
 | Returns: 1 when the route touches no room mark between its ends, 0 otherwise
 ----------------------*/
static int trace(const short *pts, int npts, int stair, int record,
                 unsigned short deco, int arrow)
{
    int ex = pts[(npts - 1) * 2], ey = pts[(npts - 1) * 2 + 1];
    int i, x = pts[0], y = pts[1];
    int first = 1;
    for (i = 1; i < npts; i++) {
        int tx = pts[i * 2], ty = pts[i * 2 + 1];
        while (x != tx || y != ty) {
            int px = x, py = y;
            if      (x < tx) x++;
            else if (x > tx) x--;
            else if (y < ty) y++;
            else             y--;
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
            else if (!(x == ex && y == ey) && in_view(x, y) && g_mark[y][x]) return 0;
        }
    }
    return 1;
}

/*----------------------
 | map_edges_link
 | Description: See map_edges.h.
 |
 |   Four routes are tried in order and the first clean one is taken: the two L
 |   shapes, then a dogleg bending at the midpoint of whichever axis is long
 |   enough to have one. Every candidate stays inside the rectangle spanned by
 |   the two rooms, so none can wander off the viewport. Over the twenty rooms of
 |   the shipped table the two L shapes alone answer all twenty-nine links and
 |   the doglegs have never been needed; they are there because a table drawn for
 |   another story will not have that property. If nothing is clean the first
 |   candidate is drawn anyway, on the grounds that a map missing a link is worse
 |   than one drawing an ambiguous link.
 |
 |   Nothing is painted here. The route is accumulated into g_edge and the
 |   caller sweeps the layer in one pass afterwards, which is what lets a cell
 |   two lines cross come out as a crossing rather than as whichever was drawn
 |   last.
 |
 |   MAP_EXIT_BAGGAGE forces the whole route solid even when MAP_EXIT_COND is
 |   also set: Infocom draws its baggage-limit mark as its own legend entry,
 |   not as a flavour of the dashed "requires problem solving" mark, and it
 |   draws a baggage passage solid even where that passage is also
 |   conditional. Dashing it as well would report the same fact twice.
 | Author: suinevere
 | Dependencies: trace
 | Globals: g_edge
 | Params: ax, ay -- the source mark's cell; bx, by -- the destination mark's;
 |   kind -- MAP_LINK_FLAT or MAP_LINK_VERT; flags -- the exit's MAP_EXIT_*
 |   bits; arrow -- 0 no head, 1 head at (bx,by), 2 head at (ax,ay)
 | Returns: N/A
 ----------------------*/
void map_edges_link(int ax, int ay, int bx, int by, int kind,
                    unsigned int flags, int arrow)
{
    unsigned short deco = (unsigned short)
        ((flags & MAP_EXIT_BAGGAGE) ? (MAP_EDGE_SOLID | MAP_EDGE_BAGGAGE)
         : (flags & MAP_EXIT_COND) ? MAP_EDGE_DASH : MAP_EDGE_SOLID);
    int stair = (kind == MAP_LINK_VERT);
    int mx = (ax + bx) / 2, my = (ay + by) / 2;
    short cand[4][8];
    int len[4], i, ncand = 2;

    cand[0][0] = (short) ax; cand[0][1] = (short) ay;
    cand[0][2] = (short) bx; cand[0][3] = (short) ay;
    cand[0][4] = (short) bx; cand[0][5] = (short) by;
    len[0] = 3;

    cand[1][0] = (short) ax; cand[1][1] = (short) ay;
    cand[1][2] = (short) ax; cand[1][3] = (short) by;
    cand[1][4] = (short) bx; cand[1][5] = (short) by;
    len[1] = 3;

    if (mx != ax && mx != bx) {
        cand[ncand][0] = (short) ax; cand[ncand][1] = (short) ay;
        cand[ncand][2] = (short) mx; cand[ncand][3] = (short) ay;
        cand[ncand][4] = (short) mx; cand[ncand][5] = (short) by;
        cand[ncand][6] = (short) bx; cand[ncand][7] = (short) by;
        len[ncand] = 4;
        ncand++;
    }
    if (my != ay && my != by) {
        cand[ncand][0] = (short) ax; cand[ncand][1] = (short) ay;
        cand[ncand][2] = (short) ax; cand[ncand][3] = (short) my;
        cand[ncand][4] = (short) bx; cand[ncand][5] = (short) my;
        cand[ncand][6] = (short) bx; cand[ncand][7] = (short) by;
        len[ncand] = 4;
        ncand++;
    }

    for (i = 0; i < ncand; i++) {
        if (!trace(cand[i], len[i], stair, 0, deco, arrow)) continue;
        trace(cand[i], len[i], stair, 1, deco, arrow);
        return;
    }
    trace(cand[0], len[0], stair, 1, deco, arrow);
}

/*----------------------
 | map_edges_stub
 | Description: See map_edges.h.
 |
 |   A baggage stub cannot pass MAP_EDGE_SOLID | MAP_EDGE_BAGGAGE as its deco
 |   the way map_edges_link does: mark_step ORs deco into both cells of every
 |   step, and a stub is two steps, so that would light three cells --
 |   including the mark's own cell and the far cell one short of the
 |   neighbouring room's mark -- rather than the one cell the mark actually
 |   belongs on. Passing plain MAP_EDGE_SOLID and then setting the bit only on
 |   (mx + dx, my + dy), the shaft cell between the two steps, is what keeps it
 |   to one.
 | Author: suinevere
 | Dependencies: mark_step, in_view
 | Globals: g_edge
 | Params: mx, my -- the mark's cell; dx, dy -- the direction as a unit step;
 |   flags -- the exit's MAP_EXIT_* bits
 | Returns: N/A
 ----------------------*/
void map_edges_stub(int mx, int my, int dx, int dy, unsigned int flags)
{
    int baggage = (flags & MAP_EXIT_BAGGAGE) != 0;
    unsigned short deco = (unsigned short) (baggage ? MAP_EDGE_SOLID : MAP_EDGE_DASH);
    mark_step(mx, my, mx + dx, my + dy, 0, deco);
    mark_step(mx + dx, my + dy, mx + 2 * dx, my + 2 * dy, 0, deco);
    if (baggage && in_view(mx + dx, my + dy))
        g_edge[my + dy][mx + dx] |= MAP_EDGE_BAGGAGE;
}

/*----------------------
 | map_edges_glyph
 | Description: See map_edges.h.
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
 | map_edges_layer
 | Description: See map_edges.h.
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

/*----------------------
 | has_baggage
 | Description: Whether a cell is a genuine interior route cell carrying
 |   MAP_EDGE_BAGGAGE, off-viewport cells and room-mark cells both answering
 |   no rather than faulting or misleading a caller.
 |
 |   A room mark is excluded on purpose, not just off-viewport ground: trace's
 |   first and last steps land on the mark cells at both ends of a route, and a
 |   baggage link's deco carries MAP_EDGE_BAGGAGE on every step, so mark_step
 |   ORs the bit onto those mark cells too as a side effect of its both-ends
 |   convention -- the same convention map_edges_stub deliberately avoids by
 |   passing plain MAP_EDGE_SOLID. Counting a mark as a "neighbour that
 |   carries the bit" was the review's Finding 1: at distance 4 the run's real
 |   interior neighbour always outvotes it, but at distance 2 -- one interior
 |   cell flanked by two marks -- there is no interior neighbour to outvote it,
 |   so the isolated-cell clause silently stopped firing and the run's only
 |   cell lost its bar. A mark cell is not a route cell; it must not count.
 | Author: suinevere
 | Dependencies: in_view
 | Globals: g_edge, g_mark
 | Params: x, y -- the cell
 | Returns: 1 if the cell is in view, is not a room mark, and carries the bit;
 |   0 otherwise
 ----------------------*/
static int has_baggage(int x, int y)
{
    return in_view(x, y) && !g_mark[y][x] && (g_edge[y][x] & MAP_EDGE_BAGGAGE) != 0;
}

/*----------------------
 | map_edges_tile
 | Description: See map_edges.h.
 |
 |   A cell carrying MAP_EDGE_BAGGAGE only ever has it on a straight run --
 |   map_edges_link sets it on every cell of a baggage route and
 |   map_edges_stub sets it on a stub's single shaft cell only, never on an
 |   elbow, T or crossing -- so the mask is checked for E|W or N|S before the
 |   bit is trusted at all.
 |
 |   The mark draws on such a cell when (x + y) % 3 == 0, which gives a link
 |   three or more cells long one mark at a fixed period in cell space, in
 |   phase across cell edges the way the dash stipple already is, OR when
 |   neither of the cell's two run-neighbours -- (x-1,y) and (x+1,y) for an
 |   E|W mask, (x,y-1) and (x,y+1) for N|S -- carries the bit itself. See
 |   has_baggage for why a room-mark cell is never counted as a carrying
 |   neighbour even though it can hold the bit.
 |
 |   That second clause is not a simplification of the first; it is the only
 |   thing that marks a stub. A stub's shaft cell is the one cell in the
 |   accumulator carrying MAP_EDGE_BAGGAGE with no neighbour that does, so its
 |   own mask is always DT_EDGE_N|DT_EDGE_S or DT_EDGE_E|DT_EDGE_W -- both of
 |   its mark_step calls OR one direction into it -- and a stub is two cells
 |   long, too short for any fixed period to be sure of landing on it. Do not
 |   drop this clause to "simplify" the period check: doing so silently
 |   un-marks every stub, including the chimney this feature exists to draw.
 |
 |   The baggage branch runs before the staircase check, not after: a baggage
 |   link that climbs a floor (MAP_LINK_VERT) sets MAP_EDGE_STAIR on every
 |   cell of the route the same as any other vertical link, and the shipped
 |   table has exactly this case -- Altar's exit down to Cave, a conditional,
 |   one-way, baggage-marked passage on the same floor. Checking STAIR first
 |   would return DT_LINK_STAIR on every cell the phase or isolation rule
 |   would otherwise have marked, so the bars would never draw on a vertical
 |   baggage run at all. Checking baggage first and falling through to STAIR
 |   only on the cells baggage does not claim keeps both facts on screen: the
 |   run still reads as a staircase everywhere else, punctuated by a bar every
 |   third cell, which is what Infocom's own map draws.
 | Author: suinevere
 | Dependencies: has_baggage
 | Globals: g_edge, g_mark
 | Params: x, y -- the cell
 | Returns: the tile index, or 0
 ----------------------*/
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
    if (e & MAP_EDGE_BAGGAGE) {
        int horiz = (mask == (DT_EDGE_E | DT_EDGE_W));
        int vert  = (mask == (DT_EDGE_N | DT_EDGE_S));
        if (horiz || vert) {
            int isolated = horiz
                ? (!has_baggage(x - 1, y) && !has_baggage(x + 1, y))
                : (!has_baggage(x, y - 1) && !has_baggage(x, y + 1));
            if (isolated || ((x + y) % 3) == 0)
                return horiz ? DT_BAGGAGE_H : DT_BAGGAGE_V;
        }
    }
    if ((e & MAP_EDGE_STAIR) && mask == (DT_EDGE_N | DT_EDGE_S))
        return DT_LINK_STAIR;
    if ((e & MAP_EDGE_DASH) && !(e & MAP_EDGE_SOLID))
        return (unsigned char) (DT_DASH0 + mask);
    return (unsigned char) (DT_LINK0 + mask);
}
