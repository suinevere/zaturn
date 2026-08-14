/*----------------------
 | test_numpad.c
 | Description: Host test for the shared dial-page numpad: the key table and its
 |   blank corners, and the D-pad walk that crosses between the pad and the two
 |   action rows below it (including the pad-active gate a hidden pad imposes).
 | Author: suinevere
 | Dependencies: ../src/input/numpad.h and numpad.c, assert.h, stdio.h
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/tnp.exe \
 |          saturn/tests/test_numpad.c saturn/src/input/numpad.c && /tmp/tnp.exe
 ----------------------*/
#include "../src/input/numpad.h"
#include <assert.h>
#include <stdio.h>

static void test_layout(void) {
    assert(np_char(0, 0) == '1');
    assert(np_char(0, 2) == '3');
    assert(np_char(2, 2) == '9');
    assert(np_char(3, 1) == '0');
    /* The two bottom corners are blank and never valid. */
    assert(np_char(3, 0) == ' ' && !np_valid(3, 0));
    assert(np_char(3, 2) == ' ' && !np_valid(3, 2));
    assert(np_valid(0, 0) && np_valid(3, 1));
    /* Out of range is a blank, never a read past the table. */
    assert(np_char(-1, 0) == ' ' && np_char(0, NP_COLS) == ' ');
}

static void test_walk_within_pad(void) {
    int arow = -1, r = 0, c = 0;

    np_dpad(0, 1, 0, 0, 1, &arow, &r, &c);           /* down */
    assert(arow == -1 && r == 1 && c == 0);
    np_dpad(0, 0, 0, 1, 1, &arow, &r, &c);           /* right */
    assert(c == 1);
    np_dpad(1, 0, 0, 0, 1, &arow, &r, &c);           /* up */
    assert(r == 0 && c == 1);
}

static void test_last_row_snaps_to_zero(void) {
    /* Landing on the bottom row from a corner column snaps onto the only key. */
    int arow = -1, r = 2, c = 0;
    np_dpad(0, 1, 0, 0, 1, &arow, &r, &c);           /* down onto the 0 row */
    assert(arow == -1 && r == 3 && c == 1);
    /* Left/right have nowhere to go on that row. */
    np_dpad(0, 0, 1, 0, 1, &arow, &r, &c);
    assert(c == 1);
    np_dpad(0, 0, 0, 1, 1, &arow, &r, &c);
    assert(c == 1);
}

static void test_cross_to_action_rows(void) {
    /* Down off the pad bottom -> row 0 -> row 1; Up climbs back. */
    int arow = -1, r = 3, c = 1;
    np_dpad(0, 1, 0, 0, 1, &arow, &r, &c);           /* off the bottom */
    assert(arow == 0);
    np_dpad(0, 1, 0, 0, 1, &arow, &r, &c);
    assert(arow == 1);
    np_dpad(1, 0, 0, 0, 1, &arow, &r, &c);
    assert(arow == 0);
    np_dpad(1, 0, 0, 0, 1, &arow, &r, &c);           /* back into the pad */
    assert(arow == -1 && r == NP_ROWS - 1 && c == 1);
}

static void test_pad_hidden_gate(void) {
    /* With the pad hidden (keyboard active), Up from row 0 must not dive in. */
    int arow = 0, r = 0, c = 0;
    np_dpad(1, 0, 0, 0, 0, &arow, &r, &c);
    assert(arow == 0);
    /* The two rows still toggle. */
    np_dpad(0, 1, 0, 0, 0, &arow, &r, &c);
    assert(arow == 1);
    np_dpad(1, 0, 0, 0, 0, &arow, &r, &c);
    assert(arow == 0);
}

int main(void) {
    test_layout();
    test_walk_within_pad();
    test_last_row_snaps_to_zero();
    test_cross_to_action_rows();
    test_pad_hidden_gate();
    printf("test_numpad ok\n");
    return 0;
}
