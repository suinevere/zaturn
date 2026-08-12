/*----------------------
 | test_command_rose.c
 | Description: Host test for the compass rose's row composition and its cursor
 |   grid. The rose is drawn from the decoded exit states alone, so an absent
 |   exit erases both its label and its spoke and a conditional one lowercases
 |   it. Asserts the exact 13-column rows, which is what keeps the module inside
 |   its 40-column strip, and walks the grid to pin that a press always lands on
 |   an available direction or reports the edge.
 | Author: suinevere
 | Dependencies: ../src/video/command_rose.h and command_rose.c,
 |   ../src/engine/room_model.h, assert.h, string.h, stdio.h
 | Build: gcc -std=c11 -Wall -Wextra -I saturn/src/engine -o /tmp/tcr.exe \
 |          saturn/tests/test_command_rose.c saturn/src/video/command_rose.c \
 |          && /tmp/tcr.exe
 |   The -I saturn/src/engine is needed because command_rose.c includes
 |   "room_model.h" unqualified, which the real build resolves through
 |   makefile:34's -I for every src subdirectory.
 ----------------------*/
#include "../src/video/command_rose.h"
#include "../src/engine/room_model.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void all(unsigned char *e, unsigned char st) {
    int i;
    for (i = 0; i < RM_DIR_N; i++) e[i] = st;
}

static void test_rows(void) {
    unsigned char e[RM_DIR_N];
    char row[CR_COLS + 1];

    /* Nothing available: every row is blank but the centre marker. */
    all(e, RM_EXIT_NONE);
    cr_row(e, 0, row); assert(strcmp(row, "             ") == 0);
    cr_row(e, 1, row); assert(strcmp(row, "             ") == 0);
    cr_row(e, 2, row); assert(strcmp(row, "             ") == 0);
    cr_row(e, 3, row); assert(strcmp(row, "      +      ") == 0);
    cr_row(e, 4, row); assert(strcmp(row, "             ") == 0);
    cr_row(e, 5, row); assert(strcmp(row, "             ") == 0);
    cr_row(e, 6, row); assert(strcmp(row, "             ") == 0);

    /* Everything available: the shape in command_rose.h's header, in full. */
    all(e, RM_EXIT_OPEN);
    cr_row(e, 0, row); assert(strcmp(row, "UP         IN") == 0);
    cr_row(e, 1, row); assert(strcmp(row, "  NW  N  NE  ") == 0);
    cr_row(e, 2, row); assert(strcmp(row, "    \\ | /    ") == 0);
    cr_row(e, 3, row); assert(strcmp(row, "  W - + - E  ") == 0);
    cr_row(e, 4, row); assert(strcmp(row, "    / | \\    ") == 0);
    cr_row(e, 5, row); assert(strcmp(row, "  SW  S  SE  ") == 0);
    cr_row(e, 6, row); assert(strcmp(row, "DOWN      OUT") == 0);

    /* North of House: n, e, w, se and sw open; s blocked; nothing else. Each
       absent direction takes its own spoke with it. */
    all(e, RM_EXIT_NONE);
    e[RM_N] = e[RM_E] = e[RM_W] = e[RM_SE] = e[RM_SW] = RM_EXIT_OPEN;
    e[RM_S] = RM_EXIT_BLOCKED;
    cr_row(e, 0, row); assert(strcmp(row, "             ") == 0);
    cr_row(e, 1, row); assert(strcmp(row, "      N      ") == 0);
    cr_row(e, 2, row); assert(strcmp(row, "      |      ") == 0);
    cr_row(e, 3, row); assert(strcmp(row, "  W - + - E  ") == 0);
    cr_row(e, 4, row); assert(strcmp(row, "    /   \\    ") == 0);
    cr_row(e, 5, row); assert(strcmp(row, "  SW     SE  ") == 0);
    cr_row(e, 6, row); assert(strcmp(row, "             ") == 0);

    /* A conditional exit is lowercased rather than promised. */
    all(e, RM_EXIT_NONE);
    e[RM_NE] = RM_EXIT_MAYBE;
    cr_row(e, 1, row); assert(strcmp(row, "         ne  ") == 0);
    cr_row(e, 2, row); assert(strcmp(row, "        /    ") == 0);
}

static void test_labels_land_where_drawn(void) {
    /* The view overprints the selected label in reverse video from cr_dir_cell's
       position, so a label whose drawn text does not sit exactly there would be
       highlighted in the wrong place. */
    unsigned char e[RM_DIR_N];
    char row[CR_COLS + 1];
    int d;

    all(e, RM_EXIT_MAYBE);
    for (d = 0; d < RM_DIR_N; d++) {
        int r, c, len, i;
        assert(cr_dir_cell(d, &r, &c, &len));
        assert(r >= 0 && r < CR_ROWS);
        assert(c >= 0 && c + len <= CR_COLS);
        cr_row(e, r, row);
        for (i = 0; i < len; i++) assert(row[c + i] != ' ');
        assert(cr_dir_row(d) == r);
    }
    assert(!cr_dir_cell(-1, 0, 0, 0));
    assert(!cr_dir_cell(RM_DIR_N, 0, 0, 0));
    assert(cr_dir_row(RM_DIR_N) == -1);
}

static void test_grid_is_complete(void) {
    /* Every direction owns exactly one grid cell, and the three centre-column
       gaps are the only empty ones. */
    int seen[RM_DIR_N];
    int r, c, d, gaps = 0;
    for (d = 0; d < RM_DIR_N; d++) seen[d] = 0;
    for (r = 0; r < CR_GRID_ROWS; r++)
        for (c = 0; c < CR_GRID_COLS; c++) {
            d = cr_grid_dir(r, c);
            if (d < 0) { gaps++; continue; }
            assert(d < RM_DIR_N && seen[d] == 0);
            seen[d] = 1;
        }
    for (d = 0; d < RM_DIR_N; d++) assert(seen[d]);
    assert(gaps == CR_GRID_ROWS * CR_GRID_COLS - RM_DIR_N);
    assert(cr_grid_dir(-1, 0) == -1);
    assert(cr_grid_dir(0, CR_GRID_COLS) == -1);
}

static void test_move_skips_and_reports_edges(void) {
    unsigned char e[RM_DIR_N];
    int out;

    all(e, RM_EXIT_OPEN);

    /* Along the middle row: the '+' in the centre column is stepped over, not
       landed on. */
    assert(cr_move(e, RM_W, +1, 0, &out) == 0 && out == RM_E);
    assert(cr_move(e, RM_E, -1, 0, &out) == 0 && out == RM_W);

    /* Rightmost column reports the edge so the view can carry focus on. */
    assert(cr_move(e, RM_E,  +1, 0, &out) == 1 && out == RM_E);
    assert(cr_move(e, RM_IN, +1, 0, &out) == 1);
    assert(cr_move(e, RM_W,  -1, 0, &out) == -1);

    /* Vertical presses stop at the ends rather than reporting anything. */
    assert(cr_move(e, RM_UP,   -1 * 0, -1, &out) == 0 && out == RM_UP);
    assert(cr_move(e, RM_DOWN,  0,     +1, &out) == 0 && out == RM_DOWN);
    assert(cr_move(e, RM_N,     0,     -1, &out) == 0 && out == RM_UP);
    assert(cr_move(e, RM_N,     0,     +1, &out) == 0 && out == RM_S);

    /* An unavailable direction is stepped over, never selected. */
    all(e, RM_EXIT_OPEN);
    e[RM_E] = RM_EXIT_NONE;
    assert(cr_move(e, RM_W, +1, 0, &out) == 1);          /* nothing right of W */
    e[RM_N] = RM_EXIT_BLOCKED;
    /* With north blocked the column above south is empty, so the press spills
       to the nearest row that has anything -- west, one row up, not the corner
       two rows further. */
    assert(cr_move(e, RM_S, 0, -1, &out) == 0 && out == RM_W);
    /* And with that row gone too it carries on to the one above. */
    e[RM_W] = RM_EXIT_NONE;
    assert(cr_move(e, RM_S, 0, -1, &out) == 0 && out == RM_NW);

    /* A room with no exits at all: the cursor has nowhere to be. */
    all(e, RM_EXIT_NONE);
    assert(cr_enter(e, 0, 0) == -1);
    assert(cr_move(e, -1, +1, 0, &out) == 0 && out == -1);
}

static void test_enter_keeps_its_row(void) {
    unsigned char e[RM_DIR_N];

    all(e, RM_EXIT_OPEN);
    /* Crossing in from the right lands on the right-hand column at the row
       asked for. */
    assert(cr_enter(e, 3, 1) == RM_E);
    assert(cr_enter(e, 1, 1) == RM_NE);
    assert(cr_enter(e, 5, 1) == RM_SE);
    assert(cr_enter(e, 0, 0) == RM_UP);

    /* With that row empty it falls to the nearest one that is not. */
    all(e, RM_EXIT_NONE);
    e[RM_S] = RM_EXIT_OPEN;
    assert(cr_enter(e, 0, 1) == RM_S);
    assert(cr_enter(e, 6, 1) == RM_S);
}

int main(void) {
    test_rows();
    test_labels_land_where_drawn();
    test_grid_is_complete();
    test_move_skips_and_reports_edges();
    test_enter_keeps_its_row();
    printf("test_command_rose ok\n");
    return 0;
}
