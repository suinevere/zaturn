/*----------------------
 | map_edges.h
 | Description: The map's line layer: accumulates every link between room marks
 |   in viewport cell space, then answers which tile each cell wants. Pure
 |   arithmetic over a grid of shorts, free of SRL, so a host test can exercise
 |   the route choice and the tile choice that a build for the target cannot be
 |   run to check.
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
 | MAP_EDGE_UP / MAP_EDGE_DOWN / MAP_EDGE_LOOP
 | Description: The three glyph bits map_edges_glyph takes: U or D for an exit
 |   whose far end is on another floor, and a loop for one that returns to the
 |   room it left. Declared here rather than only in map_edges.c because
 |   map_edges_glyph's callers need them; the rest of the bit plan
 |   (MAP_EDGE_STAIR .. MAP_EDGE_SOLID) is internal to the accumulator and
 |   stays in the .c file.
 | Author: suinevere
 ----------------------*/
#define MAP_EDGE_UP   0x0400
#define MAP_EDGE_DOWN 0x0800
#define MAP_EDGE_LOOP 0x1000

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
 | Description: Records that a room mark holds a cell. Three things follow: a
 |   route will not be drawn through it, the sweep will not offer a tile for
 |   it so the caller's own mark is not painted over by a groove, and the
 |   glyph placement pass will not offer the cell either, since it reads
 |   occupancy from g_edge rather than from g_mark.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_mark, g_edge
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
 |
 |   flags carries MAP_EXIT_COND, which dashes the whole route rather than
 |   drawing it solid. arrow is three-valued and is never inferred from
 |   MAP_EXIT_ONEWAY here: 0 draws no head, 1 puts it in the last cell before
 |   the (bx, by) end, 2 in the last cell before the (ax, ay) end -- only the
 |   caller knows which end the passage actually leads to, since Task 5's
 |   canonical ordering means the route's far end is the higher-numbered room
 |   and not necessarily the destination.
 | Author: suinevere
 | Dependencies: map_edges_mark
 | Globals: g_edge
 | Params: ax, ay -- the source mark's cell; bx, by -- the destination's; kind
 |   -- MAP_LINK_FLAT or MAP_LINK_VERT; flags -- the exit's MAP_EXIT_* bits;
 |   arrow -- 0 no head, 1 head at (bx,by), 2 head at (ax,ay)
 | Returns: N/A
 ----------------------*/
void map_edges_link(int ax, int ay, int bx, int by, int kind,
                    unsigned int flags, int arrow);

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
 | Dependencies: N/A
 | Globals: g_edge
 | Params: mx, my -- the mark's cell; dx, dy -- the direction as a unit step
 | Returns: N/A
 ----------------------*/
void map_edges_stub(int mx, int my, int dx, int dy);

/*----------------------
 | map_edges_glyph
 | Description: Puts one of MAP_EDGE_UP, MAP_EDGE_DOWN or MAP_EDGE_LOOP in a
 |   cell. The caller has already established the cell is free, which is why
 |   this does not check: glyph placement is a pass of its own that runs after
 |   every line is in, precisely so it can see what is free.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_edge
 | Params: x, y -- the cell; bit -- the glyph
 | Returns: N/A
 ----------------------*/
void map_edges_glyph(int x, int y, unsigned int bit);

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
const unsigned short *map_edges_layer(void);

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
