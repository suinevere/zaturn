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

        for (y = MAP_CLIP_Y0; y < MAP_CELL_H; y++)
            for (x = MAP_CLIP_X0; x < MAP_CELL_W; x++)
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
        map_edges_mark(4, 8); map_edges_mark(12, 8);
        map_edges_mark(8, 4); map_edges_mark(8, 12);
        map_edges_link(4, 8, 12, 8, MAP_LINK_FLAT, 0, 0);
        map_edges_link(8, 4, 8, 12, MAP_LINK_FLAT, 0, 0);
        assert(map_edges_tile(8, 8) == DT_LINK0 + 15);
        assert(map_edges_tile(7, 8) == DT_LINK_H);
        assert(map_edges_tile(8, 7) == DT_LINK_V);
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
        map_edges_mark(MAP_CLIP_X0, MAP_CLIP_Y0);
        map_edges_link(MAP_CLIP_X0, MAP_CLIP_Y0, MAP_CLIP_X0 - 8, MAP_CLIP_Y0,
                       MAP_LINK_FLAT, 0, 0);
        for (y = MAP_CLIP_Y0; y < MAP_CELL_H; y++)
            for (x = MAP_CLIP_X0; x < MAP_CELL_W; x++)
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
        static unsigned char fwd[MAP_CELL_H][MAP_CELL_W];
        int x, y, diff = 0;

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(10, 8);
        map_edges_link(4, 4, 10, 8, MAP_LINK_FLAT, 0, 0);
        for (y = MAP_CLIP_Y0; y < MAP_CELL_H; y++)
            for (x = MAP_CLIP_X0; x < MAP_CELL_W; x++)
                fwd[y][x] = map_edges_tile(x, y);

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(10, 8);
        map_edges_link(10, 8, 4, 4, MAP_LINK_FLAT, 0, 0);
        for (y = MAP_CLIP_Y0; y < MAP_CELL_H; y++)
            for (x = MAP_CLIP_X0; x < MAP_CELL_W; x++)
                if (fwd[y][x] != map_edges_tile(x, y)) diff++;
        assert(diff > 0);
    }

    /* The canonical order itself -- lower object number's cell passed first
       -- is stable: calling it twice in that order leaves the layer
       unchanged, which is what draw_once actually relies on. */
    {
        static unsigned char once[MAP_CELL_H][MAP_CELL_W];
        int x, y;

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(10, 8);
        map_edges_link(4, 4, 10, 8, MAP_LINK_FLAT, 0, 0);
        for (y = MAP_CLIP_Y0; y < MAP_CELL_H; y++)
            for (x = MAP_CLIP_X0; x < MAP_CELL_W; x++)
                once[y][x] = map_edges_tile(x, y);

        map_edges_link(4, 4, 10, 8, MAP_LINK_FLAT, 0, 0);
        for (y = MAP_CLIP_Y0; y < MAP_CELL_H; y++)
            for (x = MAP_CLIP_X0; x < MAP_CELL_W; x++)
                assert(once[y][x] == map_edges_tile(x, y));
    }

    /* Laying the same link twice, which the enumeration does not do but which
       a bug in the canonical-order rule would cause, must not change the
       layer either. The accumulation is idempotent by construction and this
       pins it, so a double-draw shows up as a test failure rather than as a
       map that looks right. */
    {
        static unsigned char once[MAP_CELL_H][MAP_CELL_W];
        int x, y;

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(4, 12);
        map_edges_link(4, 4, 4, 12, MAP_LINK_VERT, 0, 0);
        for (y = MAP_CLIP_Y0; y < MAP_CELL_H; y++)
            for (x = MAP_CLIP_X0; x < MAP_CELL_W; x++)
                once[y][x] = map_edges_tile(x, y);

        map_edges_link(4, 4, 4, 12, MAP_LINK_VERT, 0, 0);
        for (y = MAP_CLIP_Y0; y < MAP_CELL_H; y++)
            for (x = MAP_CLIP_X0; x < MAP_CELL_W; x++)
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
        map_edges_mark(4, 8); map_edges_mark(12, 8);
        map_edges_mark(8, 4); map_edges_mark(8, 12);
        map_edges_link(4, 8, 12, 8, MAP_LINK_FLAT, MAP_EXIT_COND, 0);
        map_edges_link(8, 4, 8, 12, MAP_LINK_FLAT, 0, 0);
        assert(map_edges_tile(7, 8) == DT_DASH0 + (DT_EDGE_E | DT_EDGE_W));
        assert(map_edges_tile(8, 7) == DT_LINK_V);
        assert(map_edges_tile(6, 8) == DT_DASH0 + (DT_EDGE_E | DT_EDGE_W));
        /* The shared cell itself, which is the whole point of the case. */
        assert(map_edges_tile(8, 8) == DT_LINK0 + 15);
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
        map_edges_mark(4, 8);
        map_edges_glyph(4, 6, MAP_EDGE_UP);
        map_edges_glyph(6, 8, MAP_EDGE_DOWN);
        map_edges_glyph(4, 10, MAP_EDGE_LOOP);
        assert(map_edges_tile(4, 6) == DT_GLYPH_U);
        assert(map_edges_tile(6, 8) == DT_GLYPH_D);
        assert(map_edges_tile(4, 10) == DT_LOOP);
    }

    /* A stub is a dashed run of its own, so an exit leaving the floor shows as
       something rather than as nothing -- which is what it showed before. */
    {
        map_edges_reset();
        map_edges_mark(4, 8);
        map_edges_stub(4, 8, 0, -1, 0);
        assert(map_edges_tile(4, 7) == DT_DASH0 + (DT_EDGE_N | DT_EDGE_S));
    }

    /* An off-view run from a mark on the rim reaches the gutter instead of
       being clipped away, and reaches all MAP_GUTTER cells of it. This is the
       property draw_once's edge_stub rests on: with the marks sitting on the
       boundary itself, both of mark_step's cells were off the layer and an
       exit to a room one step off screen drew nothing at all -- scrolling one
       room north made the passages back the way you came vanish rather than
       run to the edge. */
    {
        const int lo = MAP_LEFT, tp = MAP_TOP;

        map_edges_reset();
        map_edges_mark(lo, tp);
        map_edges_offview(lo, tp, -1, 0, 0);
        assert(map_edges_tile(lo - 1, tp) == DT_LINK_H);
        assert(map_edges_tile(lo - MAP_GUTTER, tp) != 0);

        map_edges_reset();
        map_edges_mark(lo, tp);
        map_edges_offview(lo, tp, 0, -1, 0);
        assert(map_edges_tile(lo, tp - 1) == DT_LINK_V);
        assert(map_edges_tile(lo, tp - MAP_GUTTER) != 0);
    }

    /* The far rim has the same margin, so a run east or south is not the one
       direction that still draws nothing. */
    {
        const int hi = MAP_LEFT + (MAP_ROOMS_W - 1) * MAP_CELLS;
        const int bt = MAP_TOP + (MAP_ROOMS_H - 1) * MAP_CELLS;

        map_edges_reset();
        map_edges_mark(hi, bt);
        map_edges_offview(hi, bt, 1, 0, 0);
        assert(map_edges_tile(hi + 1, bt) == DT_LINK_H);
        assert(map_edges_tile(hi + MAP_GUTTER, bt) != 0);

        map_edges_reset();
        map_edges_mark(hi, bt);
        map_edges_offview(hi, bt, 0, 1, 0);
        assert(map_edges_tile(hi, bt + 1) == DT_LINK_V);
        assert(map_edges_tile(hi, bt + MAP_GUTTER) != 0);
    }

    /* An off-view run carries the exit's own decoration, where a stub to
       another floor is dashed whatever the exit says. The two look identical
       for a conditional passage and must not for an open one: drawing every
       run to the viewport edge dashed would put the legend's "requires problem
       solving" mark on passages the story lets you walk. */
    {
        const int lo = MAP_LEFT, tp = MAP_TOP;

        map_edges_reset();
        map_edges_mark(lo, tp);
        map_edges_offview(lo, tp, -1, 0, MAP_EXIT_COND);
        assert(map_edges_tile(lo - 1, tp) == DT_DASH0 + (DT_EDGE_E | DT_EDGE_W));

        map_edges_reset();
        map_edges_mark(lo, tp);
        map_edges_stub(lo, tp, -1, 0, 0);
        assert(map_edges_tile(lo - 1, tp) == DT_DASH0 + (DT_EDGE_E | DT_EDGE_W));

        /* A baggage limit still draws solid and still puts its one bar on the
           shaft cell, not on the mark or on the cell past it. */
        map_edges_reset();
        map_edges_mark(lo, tp);
        map_edges_offview(lo, tp, -1, 0, MAP_EXIT_BAGGAGE | MAP_EXIT_COND);
        assert(map_edges_tile(lo - 1, tp) == DT_BAGGAGE_H);
        assert(map_edges_tile(lo - MAP_GUTTER, tp) != DT_BAGGAGE_H);
    }

    /* The layer is readable, which is how the placement pass finds free cells. */
    {
        const unsigned short *layer;
        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(8, 4);
        map_edges_link(4, 4, 8, 4, MAP_LINK_FLAT, 0, 0);
        layer = map_edges_layer();
        assert(layer[4 * MAP_CELL_W + 5] != 0);
        assert(layer[MAP_CLIP_Y0 * MAP_CELL_W + MAP_CLIP_X0] == 0);
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
        const unsigned short (*layer)[MAP_CELL_W];
        int gx, gy;
        unsigned short raw;

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(4, 8);

        layer = (const unsigned short (*)[MAP_CELL_W]) map_edges_layer();
        assert(map_layout_glyph(4, 4, 0, 1, layer, &gx, &gy));
        assert(gx == 4 && gy == 6);
        map_edges_stub(4, 4, 0, 1, 0);
        map_edges_glyph(gx, gy, MAP_EDGE_DOWN);

        layer = (const unsigned short (*)[MAP_CELL_W]) map_edges_layer();
        assert(map_layout_glyph(4, 8, 0, -1, layer, &gx, &gy));
        assert(gx == 4 && gy == 7);
        map_edges_stub(4, 8, 0, -1, 0);
        map_edges_glyph(gx, gy, MAP_EDGE_UP);

        raw = map_edges_layer()[7 * MAP_CELL_W + 4];
        assert((raw & 15) == (DT_EDGE_N | DT_EDGE_S));

        map_edges_reset();
        map_edges_mark(4, 4); map_edges_mark(4, 8);

        layer = (const unsigned short (*)[MAP_CELL_W]) map_edges_layer();
        assert(map_layout_glyph(4, 4, 0, 1, layer, &gx, &gy));
        assert(gx == 4 && gy == 6);
        map_edges_stub(4, 4, 0, 1, 0);
        map_edges_glyph(gx, gy, MAP_EDGE_DOWN);

        layer = (const unsigned short (*)[MAP_CELL_W]) map_edges_layer();
        assert(map_layout_glyph(4, 8, 0, -1, layer, &gx, &gy));
        assert(gx == 4 && gy == 7);
        map_edges_glyph(gx, gy, MAP_EDGE_UP);

        raw = map_edges_layer()[7 * MAP_CELL_W + 4];
        assert((raw & 15) == 0);
        assert(map_edges_tile(4, 7) == DT_GLYPH_U);
        assert(map_edges_tile(4, 5) == DT_DASH0 + (DT_EDGE_N | DT_EDGE_S));
    }

    /* A baggage run draws solid rather than dashed even though the exit is
       conditional -- Infocom makes the baggage limit its own legend entry, not
       a flavour of "requires problem solving", and draws Timber to Drafty solid
       despite its being a conditional exit. Exactly one cross-bar cell falls in
       the three between adjacent rooms. */
    {
        int x, bars = 0, solid = 0;
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_mark(8, 4);
        map_edges_link(4, 4, 8, 4, MAP_LINK_FLAT,
                       MAP_EXIT_COND | MAP_EXIT_BAGGAGE, 0);
        for (x = 5; x <= 7; x++) {
            unsigned char t = map_edges_tile(x, 4);
            if (t == DT_BAGGAGE_H) bars++;
            else if (t == DT_LINK_H) solid++;
            assert(t != DT_DASH0 + (DT_EDGE_E | DT_EDGE_W));
        }
        assert(bars == 1);
        assert(solid == 2);
    }

    /* A conditional run with no baggage mark still dashes, so the override is
       the mark's doing and not a change to conditional passages at large. */
    {
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_mark(8, 4);
        map_edges_link(4, 4, 8, 4, MAP_LINK_FLAT, MAP_EXIT_COND, 0);
        assert(map_edges_tile(6, 4) == DT_DASH0 + (DT_EDGE_E | DT_EDGE_W));
    }

    /* The chimney is drawn as a stub, because Studio and the Kitchen are on
       different floors of the atlas, so the mark has to reach the stub path as
       well as the link path. A stub's single shaft cell always takes one. */
    {
        map_edges_reset();
        map_edges_mark(6, 6);
        map_edges_stub(6, 6, 0, -1, MAP_EXIT_BAGGAGE);
        assert(map_edges_tile(6, 5) == DT_BAGGAGE_V);
    }

    /* Review finding 1: a baggage link only one cell long has no genuine
       interior neighbour -- both cells flanking its single interior cell are
       room marks, which pick up MAP_EDGE_BAGGAGE as a side effect of
       mark_step's both-ends convention but are not route cells and must not
       count as carrying the mark for the isolation test. Before has_baggage
       excluded marks, this cell's neighbours both "carried" the bit, the
       clause never fired, and the run fell back to phase-only -- which does
       not select (6+4)%3=1, so the cell drew DT_LINK_H with no bar at all. */
    {
        map_edges_reset();
        map_edges_mark(5, 4);
        map_edges_mark(7, 4);
        map_edges_link(5, 4, 7, 4, MAP_LINK_FLAT, MAP_EXIT_BAGGAGE, 0);
        assert(map_edges_tile(6, 4) == DT_BAGGAGE_H);
    }

    /* Review finding 2: a baggage link that also climbs a floor keeps the
       staircase tile on every cell the baggage rule does not claim, and the
       bar on the one it does -- Altar's own exit to Cave is exactly this
       case in the shipped table, and checking MAP_EDGE_STAIR before the
       baggage branch would have drawn DT_LINK_STAIR everywhere and lost the
       mark on this passage entirely. */
    {
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_mark(4, 12);
        map_edges_link(4, 4, 4, 12, MAP_LINK_VERT, MAP_EXIT_BAGGAGE, 0);
        assert(map_edges_tile(4, 5) == DT_BAGGAGE_V);
        assert(map_edges_tile(4, 6) == DT_LINK_STAIR);
    }

    printf("test_map_edges: ok\n");
    return 0;
}
