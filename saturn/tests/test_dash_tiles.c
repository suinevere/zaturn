/* Build:
     gcc -O2 -I saturn/src -o /tmp/tdt saturn/tests/test_dash_tiles.c \
         saturn/src/video/dash_tiles.c && /tmp/tdt
   dash_tiles.c is generated data and includes no SRL header. */
#include "../src/video/dash_tiles.h"
#include <assert.h>
#include <stdio.h>

/* The frame, outermost pixel first: a rim, two groove entries, the highlight.
   Behind the fourth pixel the marble field carries straight on. */
static const int FRAME[4] = { 7, 3, 2, 13 };

static int pixel(int tile, int x, int y) {
    unsigned char b = dash_tile_data[tile][y * 4 + (x >> 1)];
    return (x & 1) ? (b & 0x0F) : (b >> 4);
}

static int is_field(int v) { return v >= 5 && v <= 12; }

/* How far outside the ground's own index range a map mark has to reach.
   Byte inequality proves nothing here: dash_view's half-tint compresses the
   sixteen entries until neighbours are about a thirty-first of a channel
   apart, so a mark drawn one step off the ground differs in every byte and is
   still invisible on hardware. Four steps is the separation that survives the
   tint. */
#define MARK_MIN_STEPS 4

/* The ground tile's index range, the band every mark has to escape. */
static int ground_lo = 15, ground_hi = 0;

static void measure_ground(void) {
    int x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            int v = pixel(DT_GROUND, x, y);
            if (v < ground_lo) ground_lo = v;
            if (v > ground_hi) ground_hi = v;
        }
}

/* Whether a tile carries at least one pixel MARK_MIN_STEPS clear of the
   ground's band, in either direction. */
static int escapes_ground(int tile) {
    int x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            int v = pixel(tile, x, y);
            if (v + MARK_MIN_STEPS <= ground_lo) return 1;
            if (v >= ground_hi + MARK_MIN_STEPS) return 1;
        }
    return 0;
}

/* The tile a frame tile's marble must be taken from: the field at the same
   phase. Comparing against it is what proves the stone was carried through
   rather than replaced by a run of flat body, which is the defect that put a
   grey band down every edge. */
static int field_tile(int rp, int cp) { return DT_FIELD0 + (rp << 2) + cp; }

static void same_as_field(int tile, int rp, int cp,
                          int x0, int x1, int y0, int y1) {
    int x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            assert(pixel(tile, x, y) == pixel(field_tile(rp, cp), x, y));
}

/*----------------------
 | is_map_ink
 | Description: Whether a tile is one the map draws over its parchment, and so
 |   one whose surround must be transparent. Two ranges rather than one because
 |   the inventory overlay's picture frame sits between them in the enum and is
 |   marble like every other frame -- it is drawn over a module, not over paper.
 | Author: suinevere
 | Dependencies: dash_map.h
 | Globals: N/A
 | Params: t -- a DT_* index
 | Returns: 1 when the tile is map ink, 0 otherwise
 ----------------------*/
static int is_map_ink(int t) {
    return (t >= DT_ROOM && t <= DT_LINK_STAIR) ||
           (t >= DT_ROOM_SEL && t < DT_N);
}

int main(void) {
    int i, v, x, y;

    /* The transparent tile really is transparent, or the wallpaper would be
       hidden everywhere outside the panel. */
    for (i = 0; i < 32; i++) assert(dash_tile_data[DT_BLANK][i] == 0);

    /* Palette entry 0 must stay transparent; every other entry must be opaque,
       or the stone would show the wallpaper through it. */
    assert(dash_palette[0] == 0x0000);
    for (i = 1; i < 16; i++) assert((dash_palette[i] & 0x8000) != 0);

    /* The marble field uses only body and vein entries -- no groove, no shadow,
       so a field tile can sit anywhere inside a module without reading as one
       of its edges. */
    for (i = 0; i < 16; i++)
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
                assert(is_field(pixel(DT_FIELD0 + i, x, y)));

    /* A pixel's entry is its depth from the module's edge, four deep. */
    for (v = 0; v < 4; v++)
        for (i = 0; i < 4; i++) {
            for (x = 0; x < 8; x++) {
                assert(pixel(DT_TOP0 + v, x, i) == FRAME[i]);
                assert(pixel(DT_BOTTOM0 + v, x, 7 - i) == FRAME[i]);
            }
            for (y = 0; y < 8; y++) {
                assert(pixel(DT_LEFT0 + v, i, y) == FRAME[i]);
                assert(pixel(DT_MODLEFT0 + v, i, y) == FRAME[i]);
                assert(pixel(DT_RIGHT0 + v, 7 - i, y) == FRAME[i]);
            }
        }

    /* Behind the fourth pixel every frame tile holds the field's own stone,
       pixel for pixel, at the phase that cell sits on -- the marble reaches the
       highlight with no band of flat body in front of it. */
    for (v = 0; v < 4; v++) {
        same_as_field(DT_TOP0 + v, 3, v, 0, 7, 4, 7);
        same_as_field(DT_BOTTOM0 + v, 3, v, 0, 7, 0, 3);
        same_as_field(DT_LEFT0 + v, v, 0, 4, 7, 0, 7);
        same_as_field(DT_RIGHT0 + v, v, 3, 0, 3, 0, 7);
        same_as_field(DT_MODLEFT0 + v, v, 3, 4, 7, 0, 7);
        same_as_field(DT_DIVIDER0 + v, v, 2, 0, 1, 0, 7);
    }

    /* Each phase is a different piece of the field, or the four tiles would be
       one tile in four costumes and the 32-pixel repeat would collapse. */
    for (v = 0; v < 3; v++) {
        int same = 1;
        for (y = 0; y < 8; y++)
            for (x = 4; x < 8; x++)
                if (pixel(DT_LEFT0 + v, x, y) != pixel(DT_LEFT0 + v + 1, x, y))
                    same = 0;
        assert(!same);
    }

    /* Corners mitre on the diagonal: the entry is the depth from whichever of
       the two edges is nearer. */
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            int l = x, r = 7 - x, t = y, b = 7 - y;
            int d = l < t ? l : t;
            if (d < 4) assert(pixel(DT_CORNER_TL, x, y) == FRAME[d]);
            d = r < t ? r : t;
            if (d < 4) assert(pixel(DT_CORNER_TR, x, y) == FRAME[d]);
            d = l < b ? l : b;
            if (d < 4) assert(pixel(DT_CORNER_BL, x, y) == FRAME[d]);
            d = r < b ? r : b;
            if (d < 4) assert(pixel(DT_CORNER_BR, x, y) == FRAME[d]);
            d = l < t ? l : t;
            if (d < 4) assert(pixel(DT_TOP_MODLEFT, x, y) == FRAME[d]);
            d = l < b ? l : b;
            if (d < 4) assert(pixel(DT_BOTTOM_MODLEFT, x, y) == FRAME[d]);
        }

    /* The divider carries the frame of the module on its left and then two
       pixels of nothing; the module on its right opens in the next cell. */
    for (v = 0; v < 4; v++)
        for (y = 0; y < 8; y++) {
            for (i = 0; i < 4; i++)
                assert(pixel(DT_DIVIDER0 + v, 5 - i, y) == FRAME[i]);
            assert(pixel(DT_DIVIDER0 + v, 6, y) == 0);
            assert(pixel(DT_DIVIDER0 + v, 7, y) == 0);
        }

    /* The gap runs the full height of the panel, top row and bottom row
       included, or the boxes would be joined at their corners. */
    for (y = 0; y < 8; y++)
        for (x = 6; x <= 7; x++) {
            assert(pixel(DT_TOP_DIVIDER, x, y) == 0);
            assert(pixel(DT_BOTTOM_DIVIDER, x, y) == 0);
        }

    /* The rule inside a module is a plain centred groove on marble, not a frame
       edge: it splits one box rather than ending it, and where it meets the
       module's own frame the frame wins. */
    for (v = 0; v < 4; v++) {
        for (x = 0; x < 8; x++) {
            assert(pixel(DT_RULE0 + v, x, 3) == 3);
            assert(pixel(DT_RULE0 + v, x, 4) == 2);
            assert(pixel(DT_RULE0 + v, x, 5) == 13);
        }
        assert(is_field(pixel(DT_RULE0 + v, 0, 0)));
    }
    for (i = 0; i < 4; i++) {
        assert(pixel(DT_RULE_MODLEFT, i, 3) == FRAME[i]);
        assert(pixel(DT_RULE_RIGHT, 7 - i, 3) == FRAME[i]);
    }
    assert(pixel(DT_RULE_MODLEFT, 7, 5) == 13);
    assert(pixel(DT_RULE_RIGHT, 0, 5) == 13);

    /* The map tiles exist and none is silently blank -- an empty tile draws as
       a hole in the map. dash_tile_data is [DT_N][32]: 8x8 pixels at 4bpp, two
       pixels to the byte.

       DT_LINK0 is the exception and is skipped everywhere below: it is the link
       tile for a cell no line leaves through any side, so it is empty by
       construction and the renderer never paints it. Every other mask draws at
       least one arm. */
    {
        int t;
        for (t = DT_GROUND; t < DT_N; t++) {
            int i, nonzero = 0;
            if (t == DT_LINK0) continue;
            for (i = 0; i < 32; i++)
                if (dash_tile_data[t][i] != 0) { nonzero = 1; break; }
            assert(nonzero);
        }
    }

    /* The map's ink is drawn on transparency, because the ground it sits on is
       not on this layer: map_view paints NBG2 with DT_BLANK and the parchment
       shows through from NBG0 behind it. A tile carrying its own opaque field
       would be an 8x8 patch of marble on a sheet of paper, so every one of them
       has to leave some of itself unpainted.

       DT_GROUND is the other half of the same statement. Nothing paints it any
       more -- it stays in the set so the indices after it do not move -- but it
       is what an opaque tile looks like, so requiring it to have no transparent
       pixel at all is what stops this check passing by accident on a set where
       everything came out blank. */
    {
        int t, x, y, clear;
        for (t = DT_GROUND; t < DT_N; t++) {
            if (!is_map_ink(t) && t != DT_GROUND) continue;
            clear = 0;
            for (y = 0; y < 8; y++)
                for (x = 0; x < 8; x++)
                    if (pixel(t, x, y) == 0) clear++;
            if (t == DT_GROUND) assert(clear == 0);
            else                assert(clear > 0);
        }
    }

    /* Every mark must be visible against the tan it sits on, as a palette
       distance rather than as a byte difference. The tan is the parchment now
       rather than the marble field, but it is the same entries either way:
       dash_view's half-tint bends 5..8 to the ground colour, and a mark painted
       inside that range would vanish into it. DT_GROUND still measures the
       range because it is still the tile made of exactly those entries. */
    {
        int t;
        measure_ground();
        assert(ground_lo <= ground_hi);
        for (t = DT_GROUND; t < DT_N; t++) {
            if (t == DT_LINK0 || !is_map_ink(t)) continue;
            assert(escapes_ground(t));
        }
    }

    /* The link tiles are indexed by their connection mask, so the shape of each
       must follow from the bits. An arm reaches the tile's own edge exactly
       when its bit is set, which is what makes two cells drawn from adjoining
       masks meet rather than stop short of each other. Tested against
       transparency rather than against the ground tile: an unpainted edge is
       index 0 now, and comparing it to the marble it used to be drawn over
       would call every edge an arm. */
    {
        int mask;
        for (mask = 1; mask < 16; mask++) {
            int t = DT_LINK0 + mask;
            assert(!!(mask & DT_EDGE_N) == (pixel(t, 3, 0) != 0));
            assert(!!(mask & DT_EDGE_S) == (pixel(t, 3, 7) != 0));
            assert(!!(mask & DT_EDGE_W) == (pixel(t, 0, 3) != 0));
            assert(!!(mask & DT_EDGE_E) == (pixel(t, 7, 3) != 0));
        }
        /* And the two straight masks are what the H/V names claim. */
        assert(DT_LINK_H == DT_LINK0 + (DT_EDGE_E | DT_EDGE_W));
        assert(DT_LINK_V == DT_LINK0 + (DT_EDGE_N | DT_EDGE_S));
    }

    printf("test_dash_tiles: ok\n");
    return 0;
}
