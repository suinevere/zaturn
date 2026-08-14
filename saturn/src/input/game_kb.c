/*----------------------
 | game_kb.c
 | Description: The in-game keyboard grid described in game_kb.h.
 | Author: suinevere
 | Dependencies: game_kb.h
 ----------------------*/
#include "game_kb.h"

/*----------------------
 | GKB_LAYOUT
 | Description: The five key rows, indexed [row][col]. Row 0's eighth cell is a
 |   deliberate blank -- nine symbols spread across ten columns. Letters are
 |   lowercase here; game_kb_char_at uppercases them when CapsLock is on.
 | Author: suinevere
 ----------------------*/
static const char GKB_LAYOUT[GKB_KEY_ROWS][GKB_COLS + 1] = {
    "!?#_/^\\ ()",
    "1234567890",
    "qwertyuiop",
    "asdfghjkl\"",
    "zxcvbnm,.'"
};

char game_kb_char_at(int row, int col, int caps) {
    char c;
    if (row < 0 || row >= GKB_ROWS || col < 0 || col >= GKB_COLS) return ' ';
    if (row == GKB_SPACE_ROW) return ' ';
    c = GKB_LAYOUT[row][col];
    if (caps && c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
    return c;
}

int game_kb_move(int *row, int *col, int dcol, int drow) {
    if (drow != 0) {
        *row = (*row + drow + GKB_ROWS) % GKB_ROWS;
        return 0;
    }
    if (dcol < 0) {
        if (*row == GKB_SPACE_ROW || *col == 0) return 1;
        *col = *col - 1;
        return 0;
    }
    if (dcol > 0) {
        if (*row == GKB_SPACE_ROW) return 0;
        if (*col < GKB_COLS - 1) *col = *col + 1;
    }
    return 0;
}
