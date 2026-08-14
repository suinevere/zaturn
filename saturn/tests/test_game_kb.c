/*----------------------
 | test_game_kb.c
 | Description: Host test for the in-game keyboard grid: the character lookup
 |   (including the caps-shifted letter rows and the space-bar row) and the picker
 |   movement (column clamp, row wrap through the space bar, and the leftward edge
 |   report that hands focus to the compass rose).
 | Author: suinevere
 | Dependencies: ../src/input/game_kb.h and game_kb.c, assert.h, stdio.h
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/tgk.exe \
 |          saturn/tests/test_game_kb.c saturn/src/input/game_kb.c && /tmp/tgk.exe
 ----------------------*/
#include "../src/input/game_kb.h"
#include <assert.h>
#include <stdio.h>

static void test_char_at(void) {
    /* Symbols row, with its blank eighth cell. */
    assert(game_kb_char_at(0, 0, 0) == '!');
    assert(game_kb_char_at(0, 6, 0) == '\\');
    assert(game_kb_char_at(0, 7, 0) == ' ');
    assert(game_kb_char_at(0, 9, 0) == ')');

    /* Numbers are unaffected by caps. */
    assert(game_kb_char_at(1, 0, 0) == '1');
    assert(game_kb_char_at(1, 9, 0) == '0');
    assert(game_kb_char_at(1, 4, 1) == '5');

    /* Letters shift on caps; their punctuation tails do not. */
    assert(game_kb_char_at(2, 0, 0) == 'q');
    assert(game_kb_char_at(2, 0, 1) == 'Q');
    assert(game_kb_char_at(3, 9, 0) == '"');
    assert(game_kb_char_at(3, 9, 1) == '"');
    assert(game_kb_char_at(4, 0, 1) == 'Z');
    assert(game_kb_char_at(4, 9, 1) == '\'');

    /* The space bar is a space at any column, caps or not. */
    assert(game_kb_char_at(GKB_SPACE_ROW, 0, 0) == ' ');
    assert(game_kb_char_at(GKB_SPACE_ROW, 5, 1) == ' ');

    /* Out of range is a blank, never a read past the table. */
    assert(game_kb_char_at(-1, 0, 0) == ' ');
    assert(game_kb_char_at(0, GKB_COLS, 0) == ' ');
}

static void test_move_columns(void) {
    int row = 2, col = 4;

    /* Right steps and then clamps at the last column. */
    assert(game_kb_move(&row, &col, +1, 0) == 0 && col == 5);
    col = GKB_COLS - 1;
    assert(game_kb_move(&row, &col, +1, 0) == 0 && col == GKB_COLS - 1);

    /* Left steps, then reports the edge off column zero without moving. */
    col = 1;
    assert(game_kb_move(&row, &col, -1, 0) == 0 && col == 0);
    assert(game_kb_move(&row, &col, -1, 0) == 1 && col == 0);
}

static void test_move_rows_wrap_through_space(void) {
    int row = 4, col = 3;

    /* Down off the last letter row lands on the space bar, keeping the column. */
    assert(game_kb_move(&row, &col, 0, +1) == 0 && row == GKB_SPACE_ROW && col == 3);
    /* Down again wraps to the top. */
    assert(game_kb_move(&row, &col, 0, +1) == 0 && row == 0 && col == 3);
    /* Up from the top wraps to the space bar. */
    assert(game_kb_move(&row, &col, 0, -1) == 0 && row == GKB_SPACE_ROW && col == 3);
}

static void test_space_bar_edges(void) {
    int row = GKB_SPACE_ROW, col = 3;

    /* Left off the space bar reports the edge (its left is the block's left). */
    assert(game_kb_move(&row, &col, -1, 0) == 1 && row == GKB_SPACE_ROW && col == 3);
    /* Right on the space bar does nothing -- one key spans the row. */
    assert(game_kb_move(&row, &col, +1, 0) == 0 && col == 3);
    /* Up returns to the letter row at the kept column. */
    assert(game_kb_move(&row, &col, 0, -1) == 0 && row == 4 && col == 3);
}

int main(void) {
    test_char_at();
    test_move_columns();
    test_move_rows_wrap_through_space();
    test_space_bar_edges();
    printf("test_game_kb ok\n");
    return 0;
}
