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
        map_edges_link(4, 4, 8, 4, MAP_LINK_FLAT, 0, 0);

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
        map_edges_link(4, 4, 4, 8, MAP_LINK_VERT, 0, 0);
        assert(map_edges_tile(4, 5) == DT_LINK_STAIR);
        assert(map_edges_tile(4, 7) == DT_LINK_STAIR);
    }

    /* A flat vertical run is an ordinary groove, not a staircase. */
    {
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_mark(4, 8);
        map_edges_link(4, 4, 4, 8, MAP_LINK_FLAT, 0, 0);
        assert(map_edges_tile(4, 6) == DT_LINK_V);
    }

    /* Two runs crossing one cell resolve into a crossing rather than one
       overwriting the other. This is why the layer is accumulated whole and
       swept once, and it is the property a per-link painter cannot have. */
    {
        map_edges_reset();
        map_edges_mark(0, 4); map_edges_mark(8, 4);
        map_edges_mark(4, 0); map_edges_mark(4, 8);
        map_edges_link(0, 4, 8, 4, MAP_LINK_FLAT, 0, 0);
        map_edges_link(4, 0, 4, 8, MAP_LINK_FLAT, 0, 0);
        assert(map_edges_tile(4, 4) == DT_LINK0 + 15);
        assert(map_edges_tile(3, 4) == DT_LINK_H);
        assert(map_edges_tile(4, 3) == DT_LINK_V);
    }

    /* An L route turns, so the corner cell is an elbow. */
    {
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_mark(8, 8);
        map_edges_link(4, 4, 8, 8, MAP_LINK_FLAT, 0, 0);
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
        map_edges_link(4, 4, 12, 12, MAP_LINK_FLAT, 0, 0);
        assert(map_edges_tile(4, 8) != 0);
        assert(map_edges_tile(8, 4) == 0);
    }

    /* Every step of this route leaves the viewport, so mark_step refuses all of
       them and the layer comes out empty -- which is the claim, where checking
       the marked cell alone would have passed against a no-op. */
    {
        int x, y, n = 0;
        map_edges_reset();
        map_edges_mark(0, 0);
        map_edges_link(0, 0, -8, 0, MAP_LINK_FLAT, 0, 0);
        for (y = 0; y < MAP_ROOMS_H * MAP_CELLS; y++)
            for (x = 0; x < MAP_ROOMS_W * MAP_CELLS; x++)
                if (map_edges_tile(x, y) != 0) n++;
        assert(n == 0);
    }

    /* map_edges_link is NOT argument-order-independent in general: cand[0] is
       the horizontal-first L and cand[1] the vertical-first L, tried in that
       order, and for a genuinely diagonal pair both are clean, so swapping
       (a,b) picks the other L -- a physically different route. This is why
       draw_once's exit walk canonicalises which end it passes as the source:
       gather() hands rooms out in ascending object-number order, so the old
       i<j pair loop always called this in (lower id, higher id) order, but a
       one-way exit draws from its source, which may be the higher-numbered
       room. Left unswapped, that reversed call could select a different L
       than the pairwise loop did. Passing the lower-numbered room's cell
       first, regardless of which end holds the directed exit, reproduces the
       old loop's order in every case and keeps the two candidate L's from
       ever being asked for in the "wrong" order at the call site. This test
       pins that map_edges_link itself is order-dependent, so a future reader
       does not assume symmetry that was never there. */
    {
        static unsigned char fwd[MAP_ROOMS_H * MAP_CELLS][MAP_ROOMS_W * MAP_CELLS];
        int x, y, diff = 0;

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(10, 8);
        map_edges_link(4, 4, 10, 8, MAP_LINK_FLAT, 0, 0);
        for (y = 0; y < MAP_ROOMS_H * MAP_CELLS; y++)
            for (x = 0; x < MAP_ROOMS_W * MAP_CELLS; x++)
                fwd[y][x] = map_edges_tile(x, y);

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(10, 8);
        map_edges_link(10, 8, 4, 4, MAP_LINK_FLAT, 0, 0);
        for (y = 0; y < MAP_ROOMS_H * MAP_CELLS; y++)
            for (x = 0; x < MAP_ROOMS_W * MAP_CELLS; x++)
                if (fwd[y][x] != map_edges_tile(x, y)) diff++;
        assert(diff > 0);
    }

    /* The canonical order itself -- lower object number's cell passed first
       -- is stable: calling it twice in that order leaves the layer
       unchanged, which is what draw_once actually relies on. */
    {
        static unsigned char once[MAP_ROOMS_H * MAP_CELLS][MAP_ROOMS_W * MAP_CELLS];
        int x, y;

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(10, 8);
        map_edges_link(4, 4, 10, 8, MAP_LINK_FLAT, 0, 0);
        for (y = 0; y < MAP_ROOMS_H * MAP_CELLS; y++)
            for (x = 0; x < MAP_ROOMS_W * MAP_CELLS; x++)
                once[y][x] = map_edges_tile(x, y);

        map_edges_link(4, 4, 10, 8, MAP_LINK_FLAT, 0, 0);
        for (y = 0; y < MAP_ROOMS_H * MAP_CELLS; y++)
            for (x = 0; x < MAP_ROOMS_W * MAP_CELLS; x++)
                assert(once[y][x] == map_edges_tile(x, y));
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
        map_edges_link(4, 4, 4, 12, MAP_LINK_VERT, 0, 0);
        for (y = 0; y < MAP_ROOMS_H * MAP_CELLS; y++)
            for (x = 0; x < MAP_ROOMS_W * MAP_CELLS; x++)
                once[y][x] = map_edges_tile(x, y);

        map_edges_link(4, 4, 4, 12, MAP_LINK_VERT, 0, 0);
        for (y = 0; y < MAP_ROOMS_H * MAP_CELLS; y++)
            for (x = 0; x < MAP_ROOMS_W * MAP_CELLS; x++)
                assert(once[y][x] == map_edges_tile(x, y));
    }

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

    /* Two off-floor exits pointing at each other, four cells apart, contest
       the same middle cell for their letter: the first claims it outright
       two cells out, and map_layout_glyph falls the second back to the cell
       one short of it -- a valid landing spot on its own, but one the far
       cell it never checked lies past. Laying the stub whenever the glyph
       merely landed on the vertical, without checking it reached the far
       cell, runs a dashed line straight through that unchecked cell and
       joins two rooms the story does not join -- reproduced below by laying
       both stubs unconditionally, the way the review found it. Skipping the
       second stub instead, since its glyph did not reach the far cell,
       leaves that cell carrying nothing but its own letter: no dashed edge
       reaches it, so it is not part of any run. */
    {
        const unsigned short (*layer)[MAP_ROOMS_W * MAP_CELLS];
        int gx, gy;
        unsigned short raw;

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(4, 8);

        layer = (const unsigned short (*)[MAP_ROOMS_W * MAP_CELLS]) map_edges_layer();
        assert(map_layout_glyph(4, 4, 0, 1, layer, &gx, &gy));
        assert(gx == 4 && gy == 6);
        map_edges_stub(4, 4, 0, 1);
        map_edges_glyph(gx, gy, MAP_EDGE_DOWN);

        layer = (const unsigned short (*)[MAP_ROOMS_W * MAP_CELLS]) map_edges_layer();
        assert(map_layout_glyph(4, 8, 0, -1, layer, &gx, &gy));
        assert(gx == 4 && gy == 7);
        map_edges_stub(4, 8, 0, -1);
        map_edges_glyph(gx, gy, MAP_EDGE_UP);

        raw = map_edges_layer()[7 * (MAP_ROOMS_W * MAP_CELLS) + 4];
        assert((raw & 15) == (DT_EDGE_N | DT_EDGE_S));

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(4, 8);

        layer = (const unsigned short (*)[MAP_ROOMS_W * MAP_CELLS]) map_edges_layer();
        assert(map_layout_glyph(4, 4, 0, 1, layer, &gx, &gy));
        assert(gx == 4 && gy == 6);
        map_edges_stub(4, 4, 0, 1);
        map_edges_glyph(gx, gy, MAP_EDGE_DOWN);

        layer = (const unsigned short (*)[MAP_ROOMS_W * MAP_CELLS]) map_edges_layer();
        assert(map_layout_glyph(4, 8, 0, -1, layer, &gx, &gy));
        assert(gx == 4 && gy == 7);
        map_edges_glyph(gx, gy, MAP_EDGE_UP);

        raw = map_edges_layer()[7 * (MAP_ROOMS_W * MAP_CELLS) + 4];
        assert((raw & 15) == 0);
        assert(map_edges_tile(4, 7) == DT_GLYPH_U);
        assert(map_edges_tile(4, 5) == DT_DASH0 + (DT_EDGE_N | DT_EDGE_S));
    }

    printf("test_map_edges: ok\n");
    return 0;
}
