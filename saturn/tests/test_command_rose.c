/*----------------------
 | test_command_rose.c
 | Description: Host test for the compass rose's row composition, the chord key
 |   that replaces it while the modifier is held, and the cursor grid. The rose is
 |   drawn from the decoded exit states alone, so an absent exit erases both its
 |   label and its spoke and a conditional one lowercases it. Asserts the exact
 |   13-column rows of both blocks, which is what keeps the module inside its
 |   40-column strip, and walks the grid to pin that a press always lands on
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
    cr_row(e, 1, row); assert(strcmp(row, "   NW N NE   ") == 0);
    cr_row(e, 2, row); assert(strcmp(row, "     \\|/     ") == 0);
    cr_row(e, 3, row); assert(strcmp(row, "   W -+- E   ") == 0);
    cr_row(e, 4, row); assert(strcmp(row, "     /|\\     ") == 0);
    cr_row(e, 5, row); assert(strcmp(row, "   SW S SE   ") == 0);
    cr_row(e, 6, row); assert(strcmp(row, "DOWN      OUT") == 0);

    /* North of House: n, e, w, se and sw open; s blocked; nothing else. Each
       absent direction takes its own spoke with it. */
    all(e, RM_EXIT_NONE);
    e[RM_N] = e[RM_E] = e[RM_W] = e[RM_SE] = e[RM_SW] = RM_EXIT_OPEN;
    e[RM_S] = RM_EXIT_BLOCKED;
    cr_row(e, 0, row); assert(strcmp(row, "             ") == 0);
    cr_row(e, 1, row); assert(strcmp(row, "      N      ") == 0);
    cr_row(e, 2, row); assert(strcmp(row, "      |      ") == 0);
    cr_row(e, 3, row); assert(strcmp(row, "   W -+- E   ") == 0);
    cr_row(e, 4, row); assert(strcmp(row, "     / \\     ") == 0);
    cr_row(e, 5, row); assert(strcmp(row, "   SW   SE   ") == 0);
    cr_row(e, 6, row); assert(strcmp(row, "             ") == 0);

    /* A conditional exit is lowercased rather than promised. */
    all(e, RM_EXIT_NONE);
    e[RM_NE] = RM_EXIT_MAYBE;
    cr_row(e, 1, row); assert(strcmp(row, "        ne   ") == 0);
    cr_row(e, 2, row); assert(strcmp(row, "       /     ") == 0);
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

#define LEAVE (-2)

/*----------------------
 | moved
 | Description: Where a press from `from` lands, or LEAVE when it carries focus
 |   out of the module. Collapses cr_move's split return so a case reads as one
 |   expected value.
 ----------------------*/
static int moved(const unsigned char *e, int from, int dx, int dy) {
    int out = -99;
    if (cr_move(e, from, dx, dy, &out) != 0) return LEAVE;
    return out;
}

static void test_move_takes_the_obvious_neighbour(void) {
    /* With every direction available a press lands on whatever is squarely
       along it -- the case that has to keep working however the missing-exit
       rule is written. */
    unsigned char e[RM_DIR_N];
    all(e, RM_EXIT_OPEN);

    /* Down each column, and round the ends. */
    assert(moved(e, RM_UP,   0, +1) == RM_NW);
    assert(moved(e, RM_NW,   0, +1) == RM_W);
    assert(moved(e, RM_W,    0, +1) == RM_SW);
    assert(moved(e, RM_SW,   0, +1) == RM_DOWN);
    assert(moved(e, RM_DOWN, 0, -1) == RM_SW);
    assert(moved(e, RM_IN,   0, +1) == RM_NE);
    assert(moved(e, RM_NE,   0, +1) == RM_E);
    assert(moved(e, RM_E,    0, +1) == RM_SE);
    assert(moved(e, RM_SE,   0, +1) == RM_OUT);
    assert(moved(e, RM_N,    0, +1) == RM_S);
    assert(moved(e, RM_S,    0, -1) == RM_N);

    /* Across each row. */
    assert(moved(e, RM_UP, +1, 0) == RM_IN);
    assert(moved(e, RM_NW, +1, 0) == RM_N);
    assert(moved(e, RM_N,  +1, 0) == RM_NE);
    assert(moved(e, RM_W,  +1, 0) == RM_E);
    assert(moved(e, RM_SW, +1, 0) == RM_S);
    assert(moved(e, RM_S,  +1, 0) == RM_SE);
    assert(moved(e, RM_IN, -1, 0) == RM_UP);
    assert(moved(e, RM_NE, -1, 0) == RM_N);
    assert(moved(e, RM_E,  -1, 0) == RM_W);
    assert(moved(e, RM_SE, -1, 0) == RM_S);

    /* The right-hand column is the module's edge; the left-hand one is the
       strip's, so a press against it has nowhere to go. */
    assert(moved(e, RM_IN,  +1, 0) == LEAVE);
    assert(moved(e, RM_NE,  +1, 0) == LEAVE);
    assert(moved(e, RM_E,   +1, 0) == LEAVE);
    assert(moved(e, RM_SE,  +1, 0) == LEAVE);
    assert(moved(e, RM_OUT, +1, 0) == LEAVE);
    assert(moved(e, RM_UP,  -1, 0) == RM_UP);
    assert(moved(e, RM_W,   -1, 0) == RM_W);

    /* A vertical press off the end wraps to the far side of the same column. */
    assert(moved(e, RM_UP,   0, -1) == RM_DOWN);
    assert(moved(e, RM_DOWN, 0, +1) == RM_UP);
    assert(moved(e, RM_IN,   0, -1) == RM_OUT);
    assert(moved(e, RM_OUT,  0, +1) == RM_IN);
}

static void test_move_reaches_the_next_nearest(void) {
    /* The room that made the old neighbour table dead-end: standing on south
       with the whole bottom of the rose gone, every press still goes somewhere,
       and it goes to whatever is nearest along the bearing pressed. */
    unsigned char e[RM_DIR_N];
    all(e, RM_EXIT_NONE);
    e[RM_S] = e[RM_W] = e[RM_E] = e[RM_NW] = e[RM_NE] = RM_EXIT_OPEN;

    assert(moved(e, RM_S, -1, 0) == RM_W);    /* nearest on the left  */
    assert(moved(e, RM_S, +1, 0) == RM_E);    /* nearest on the right */
    assert(moved(e, RM_S,  0, -1) == RM_NW);  /* squarest above       */
    assert(moved(e, RM_S,  0, +1) == RM_NE);  /* nothing below; wraps */
}

static void test_move_prefers_bearing_over_distance(void) {
    /* Up-and-right from west finds north, not north-east: both are up and to
       the right, but north sits squarely on the diagonal. */
    unsigned char e[RM_DIR_N];
    all(e, RM_EXIT_OPEN);
    assert(moved(e, RM_W, +1, -1) == RM_N);
    assert(moved(e, RM_W, +1, +1) == RM_S);
    assert(moved(e, RM_E, -1, -1) == RM_N);
    assert(moved(e, RM_E, -1, +1) == RM_S);

    /* With north gone the same press carries on along that bearing. */
    e[RM_N] = RM_EXIT_NONE;
    assert(moved(e, RM_W, +1, -1) == RM_IN);

    /* A diagonal is not a horizontal: right alone from west is east, which the
       diagonal never picks even though it is nearer than north-east. */
    all(e, RM_EXIT_OPEN);
    assert(moved(e, RM_W, +1, 0) == RM_E);
    e[RM_N] = e[RM_IN] = RM_EXIT_NONE;
    assert(moved(e, RM_W, +1, -1) == RM_NE);
}

static void test_move_always_offers_a_way_out(void) {
    /* However few exits a room has, a rightward press leaves the module -- the
       cursor can never be trapped in the rose. */
    unsigned char e[RM_DIR_N];
    int d;
    for (d = 0; d < RM_DIR_N; d++) {
        all(e, RM_EXIT_NONE);
        e[d] = RM_EXIT_OPEN;
        assert(moved(e, d, +1, 0) == LEAVE);
        /* And no press loops or lands on a direction that is not there. */
        assert(moved(e, d, -1, 0) == d);
        assert(moved(e, d,  0, -1) == d);
        assert(moved(e, d,  0, +1) == d);
    }

    /* A room with no exits at all: the cursor has nowhere to be. */
    all(e, RM_EXIT_NONE);
    assert(cr_enter(e, 0, 0) == -1);
    assert(moved(e, -1, +1, 0) == -1);
}

static void test_move_never_picks_a_missing_direction(void) {
    /* Exhaustive over every room a four-exit rose can be: whatever a press
       returns is either the cursor standing still, a direction the room
       actually offers, or the module edge. */
    unsigned char e[RM_DIR_N];
    int mask, d, i, dx, dy;
    static const int PRESS[8][2] = {
        { 0, -1 }, { 0, +1 }, { -1, 0 }, { +1, 0 },
        { -1, -1 }, { +1, -1 }, { -1, +1 }, { +1, +1 }
    };

    for (mask = 1; mask < (1 << RM_DIR_N); mask += 37) {   /* a coprime stride */
        all(e, RM_EXIT_NONE);
        for (d = 0; d < RM_DIR_N; d++)
            if (mask & (1 << d)) e[d] = RM_EXIT_OPEN;
        for (d = 0; d < RM_DIR_N; d++) {
            if (!(mask & (1 << d))) continue;
            for (i = 0; i < 8; i++) {
                int got;
                dx = PRESS[i][0]; dy = PRESS[i][1];
                got = moved(e, d, dx, dy);
                if (got == LEAVE) { assert(dx > 0); continue; }
                assert(got == d || (mask & (1 << got)));
            }
        }
    }
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

static void test_dir_words(void) {
    assert(strcmp(cr_dir_word(RM_N),    "north") == 0);
    assert(strcmp(cr_dir_word(RM_E),    "east")  == 0);
    assert(strcmp(cr_dir_word(RM_W),    "west")  == 0);
    assert(strcmp(cr_dir_word(RM_S),    "south") == 0);
    assert(strcmp(cr_dir_word(RM_NE),   "ne")    == 0);
    assert(strcmp(cr_dir_word(RM_NW),   "nw")    == 0);
    assert(strcmp(cr_dir_word(RM_SE),   "se")    == 0);
    assert(strcmp(cr_dir_word(RM_SW),   "sw")    == 0);
    assert(strcmp(cr_dir_word(RM_UP),   "up")    == 0);
    assert(strcmp(cr_dir_word(RM_DOWN), "down")  == 0);
    assert(strcmp(cr_dir_word(RM_IN),   "in")    == 0);
    assert(strcmp(cr_dir_word(RM_OUT),  "out")   == 0);
    assert(strcmp(cr_dir_word(-1), "") == 0);
    assert(strcmp(cr_dir_word(RM_DIR_N), "") == 0);
}


/* ---- the chord key, which stands in for the rose while the modifier is held.
   Same 13 columns and same seven rows, and the same compass: the cells say which
   way the D-pad goes under the modifier instead of which exits the room has. */

static void key_all(const CrKey *k, char out[CR_ROWS][CR_COLS + 1]) {
    int r;
    for (r = 0; r < CR_ROWS; r++) cr_key_row(k, r, out[r]);
}

static void test_key_rows(void) {
    char k[CR_ROWS][CR_COLS + 1];
    CrKey d = { "Up", "Down", "Home", "End", 0, 0 };
    int r;

    /* The factory mapping: Line on the D-pad's vertical, Home/End on its
       horizontal, no trigger chord. The centre marker is drawn exactly as the
       rose's is, and every row is exactly CR_COLS. */
    key_all(&d, k);
    assert(strcmp(k[0], "             ") == 0);
    assert(strcmp(k[1], "     Up      ") == 0);
    assert(strcmp(k[2], "      ^      ") == 0);
    assert(strcmp(k[3], "Home <+> End ") == 0);
    assert(strcmp(k[4], "      v      ") == 0);
    assert(strcmp(k[5], "    Down     ") == 0);
    assert(strcmp(k[6], "             ") == 0);
    for (r = 0; r < CR_ROWS; r++) assert(strlen(k[r]) == CR_COLS);
}

static void test_key_unbound_pairs_take_their_arrows(void) {
    char k[CR_ROWS][CR_COLS + 1];
    CrKey vert = { "Up", "Down", 0, 0, 0, 0 };
    CrKey horz = { 0, 0, "Prev", "Next", 0, 0 };
    CrKey none = { 0, 0, 0, 0, 0, 0 };
    int r;

    /* An unbound pair erases its own arrows along with its labels, the way an
       absent exit takes its spoke -- but never the centre. */
    key_all(&vert, k);
    assert(strcmp(k[1], "     Up      ") == 0);
    assert(strcmp(k[2], "      ^      ") == 0);
    assert(strcmp(k[3], "      +      ") == 0);
    assert(strcmp(k[5], "    Down     ") == 0);

    key_all(&horz, k);
    assert(strcmp(k[1], "             ") == 0);
    assert(strcmp(k[2], "             ") == 0);
    assert(strcmp(k[3], "Prev <+> Next") == 0);
    assert(strcmp(k[4], "             ") == 0);
    assert(strcmp(k[5], "             ") == 0);

    /* Nothing bound at all still shows the centre, and nothing else: the block
       is a compass and carries no prose to fall back on. */
    key_all(&none, k);
    for (r = 0; r < CR_ROWS; r++) {
        assert(strcmp(k[r], r == 3 ? "      +      " : "             ") == 0);
        assert(strlen(k[r]) == CR_COLS);
    }
}

static void test_key_triggers_take_the_corner_row(void) {
    char row[CR_COLS + 1];
    CrKey both = { 0, 0, 0, 0, "Prev", "Next" };
    CrKey left = { 0, 0, 0, 0, "Home", 0 };
    CrKey right = { 0, 0, 0, 0, 0, "End" };

    /* The widest trigger row -- both triggers, both labels at the full width --
       is exactly CR_COLS. */
    cr_key_row(&both, 0, row);
    assert(strcmp(row, "L:Prev R:Next") == 0);
    assert(strlen(row) == CR_COLS);

    /* A trigger modifier leaves only the other trigger reachable, and the one
       that is stays in its own half of the row. */
    cr_key_row(&left, 0, row);
    assert(strcmp(row, "L:Home       ") == 0);
    cr_key_row(&right, 0, row);
    assert(strcmp(row, "       R:End ") == 0);
}

static void test_key_clips_long_labels(void) {
    char row[CR_COLS + 1];
    CrKey wide = { "Upward", "Downward", "Homeward", "Endward", 0, 0 };
    /* A label longer than the four columns beside the marker is cut there rather
       than allowed to run into it or off the module. */
    cr_key_row(&wide, 3, row);
    assert(strcmp(row, "Home <+> Endw") == 0);
    assert(strlen(row) == CR_COLS);
    cr_key_row(&wide, 1, row);
    assert(strcmp(row, "    Upwa     ") == 0);
    assert(strlen(row) == CR_COLS);
}

int main(void) {
    test_rows();
    test_labels_land_where_drawn();
    test_grid_is_complete();
    test_move_takes_the_obvious_neighbour();
    test_move_reaches_the_next_nearest();
    test_move_prefers_bearing_over_distance();
    test_move_always_offers_a_way_out();
    test_move_never_picks_a_missing_direction();
    test_enter_keeps_its_row();
    test_dir_words();
    test_key_rows();
    test_key_unbound_pairs_take_their_arrows();
    test_key_triggers_take_the_corner_row();
    test_key_clips_long_labels();
    printf("test_command_rose ok\n");
    return 0;
}
