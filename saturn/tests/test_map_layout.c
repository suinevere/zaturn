/*----------------------
 | test_map_layout.c
 | Description: Host test for map_layout.h -- the map screen's viewport
 |   arithmetic. Everything here shows up on a television and nowhere else, so
 |   it is checked exhaustively rather than at a few chosen points: the follow
 |   rule is run over every crosshair and view pair in a range wider than any
 |   authored map, and each result is required to be both correct and minimal.
 |
 |   Minimality is the half worth having. A follow that snapped the view onto
 |   the crosshair every step would satisfy "the cursor is on screen" and would
 |   scroll the whole map on every press, which is the behaviour this replaced.
 | Author: suinevere
 | Build: gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/video \
 |          -o /tmp/tmlay saturn/tests/test_map_layout.c && /tmp/tmlay
 ----------------------*/
#include <assert.h>
#include <stdio.h>
#include "dash_map.h"
#include "map_layout.h"

#define SPAN 24

int main(void) {
    int hx, hy, sx, sy;

    /* The viewport is what the constants say it is, and its centre is inside
       it. A sign slip in either bound would put the player off screen. */
    assert(MAP_DX_MIN == -5 && MAP_DX_MAX == 4);
    assert(MAP_DY_MIN == -3 && MAP_DY_MAX == 3);
    assert(MAP_DX_MAX - MAP_DX_MIN + 1 == MAP_ROOMS_W);
    assert(MAP_DY_MAX - MAP_DY_MIN + 1 == MAP_ROOMS_H);
    assert(map_layout_visible(0, 0));
    assert(!map_layout_visible(MAP_DX_MIN - 1, 0));
    assert(!map_layout_visible(MAP_DX_MAX + 1, 0));
    assert(!map_layout_visible(0, MAP_DY_MIN - 1));
    assert(!map_layout_visible(0, MAP_DY_MAX + 1));

    /* A cell is on the four-cell grid and the whole viewport fits the screen:
       ten rooms of four cells is the 40 columns a 320-pixel screen shows, and
       seven rooms of four rows is the 28 the map is given. */
    assert(map_layout_cell(0, 0, MAP_CX, 0) == MAP_CX * MAP_CELLS);
    assert(map_layout_cell(MAP_DX_MIN, 0, MAP_CX, 0) == 0);
    assert(map_layout_cell(MAP_DX_MAX, 0, MAP_CX, 0) == 36);
    assert(map_layout_cell(MAP_DY_MIN, 0, MAP_CY, MAP_TOP) == MAP_TOP);
    assert(map_layout_cell(MAP_DY_MAX, 0, MAP_CY, MAP_TOP) == MAP_TOP + 24);
    assert(MAP_ROOMS_W * MAP_CELLS == 40);
    assert(MAP_ROOMS_H * MAP_CELLS == 28);

    /* Scrolling the view by one room moves every cell by exactly one room,
       which is what keeps a mark on the grid the links are routed along. */
    assert(map_layout_cell(3, 1, MAP_CX, 0) ==
           map_layout_cell(2, 0, MAP_CX, 0));

    /* The follow rule, over every pair in a span wider than any authored map.
       Three things must hold at once: the crosshair ends up on screen, the view
       moves no further than it had to, and it does not move at all when the
       crosshair was already inside. */
    for (hx = -SPAN; hx <= SPAN; hx++) {
        for (hy = -SPAN; hy <= SPAN; hy++) {
            for (sx = -SPAN; sx <= SPAN; sx++) {
                for (sy = -SPAN; sy <= SPAN; sy++) {
                    int nx = sx, ny = sy;
                    int was_in = map_layout_visible(hx - sx, hy - sy);
                    int dx, dy, k;

                    map_layout_follow(hx, hy, &nx, &ny);
                    assert(map_layout_visible(hx - nx, hy - ny));

                    if (was_in) {
                        assert(nx == sx && ny == sy);
                        continue;
                    }

                    dx = (nx > sx) ? (nx - sx) : (sx - nx);
                    dy = (ny > sy) ? (ny - sy) : (sy - ny);

                    /* Minimal on each axis independently: no smaller shift of
                       that axis alone would have brought the crosshair in. */
                    for (k = 1; k < dx; k++) {
                        int try_lo = sx - k, try_hi = sx + k;
                        assert(!map_layout_visible(hx - try_lo, hy - ny));
                        assert(!map_layout_visible(hx - try_hi, hy - ny));
                    }
                    for (k = 1; k < dy; k++) {
                        int try_lo = sy - k, try_hi = sy + k;
                        assert(!map_layout_visible(hx - nx, hy - try_lo));
                        assert(!map_layout_visible(hx - nx, hy - try_hi));
                    }

                    /* An axis already inside is never touched. */
                    if (hx - sx >= MAP_DX_MIN && hx - sx <= MAP_DX_MAX)
                        assert(nx == sx);
                    if (hy - sy >= MAP_DY_MIN && hy - sy <= MAP_DY_MAX)
                        assert(ny == sy);
                }
            }
        }
    }

    /* Following twice changes nothing the first pass did not, which is what
       lets the caller run it on every cursor step without the view creeping. */
    for (hx = -SPAN; hx <= SPAN; hx++) {
        int ax = 0, ay = 0, bx, by;
        map_layout_follow(hx, hx, &ax, &ay);
        bx = ax; by = ay;
        map_layout_follow(hx, hx, &bx, &by);
        assert(bx == ax && by == ay);
    }

    /* The figure stands to the left with a cell of clearance wherever there is
       room, flips right rather than hanging off the left edge, and never lands
       on the mark itself in either case. */
    {
        int mx, kx, ky;
        for (mx = 0; mx < MAP_ROOMS_W; mx++) {
            int cell = mx * MAP_CELLS;
            map_layout_knight(cell, 12, DT_KNIGHT_W, &kx, &ky);
            assert(kx >= 0);
            assert(ky == 11);
            /* Two cells wide, and neither of them is the mark's own cell. */
            assert(kx + DT_KNIGHT_W - 1 < cell || kx > cell);
            if (cell >= 1 + DT_KNIGHT_W) {
                assert(kx == cell - 1 - DT_KNIGHT_W);
                assert(kx + DT_KNIGHT_W == cell - 1);   /* the clearance */
            } else {
                assert(kx == cell + 2);
            }
        }
    }

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

    printf("test_map_layout: ok\n");
    return 0;
}
