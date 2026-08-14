/*----------------------
 | numpad.c
 | Description: The shared dial-page numpad described in numpad.h.
 | Author: suinevere
 | Dependencies: numpad.h
 ----------------------*/
#include "numpad.h"

/*----------------------
 | NP_LAYOUT
 | Description: The pad, indexed [row][col]: 1-9 then a lone 0 centred on the
 |   bottom row, whose two corners are blank.
 | Author: suinevere
 ----------------------*/
static const char NP_LAYOUT[NP_ROWS][NP_COLS + 1] = { "123", "456", "789", " 0 " };

char np_char(int r, int c) {
    if (r < 0 || r >= NP_ROWS || c < 0 || c >= NP_COLS) return ' ';
    return NP_LAYOUT[r][c];
}

int np_valid(int r, int c) {
    return r >= 0 && r < NP_ROWS && c >= 0 && c < NP_COLS && NP_LAYOUT[r][c] != ' ';
}

void np_dpad(int up, int down, int left, int right, int pad_active,
             int *arow, int *cursor_row, int *cursor_col) {
    if (up) {
        if      (*arow == 1) *arow = 0;
        else if (*arow == 0) { if (pad_active) { *arow = -1; *cursor_row = NP_ROWS - 1; *cursor_col = 1; } }
        else if (*cursor_row > 0) { (*cursor_row)--; if (!np_valid(*cursor_row, *cursor_col)) *cursor_col = 1; }
    }
    if (down) {
        if      (*arow == -1) { if (*cursor_row < NP_ROWS - 1) { (*cursor_row)++; if (!np_valid(*cursor_row, *cursor_col)) *cursor_col = 1; } else *arow = 0; }
        else if (*arow == 0)  *arow = 1;
    }
    if (left  && *arow < 0) { int nc = *cursor_col - 1; while (nc >= 0       && !np_valid(*cursor_row, nc)) nc--; if (nc >= 0)       *cursor_col = nc; }
    if (right && *arow < 0) { int nc = *cursor_col + 1; while (nc < NP_COLS  && !np_valid(*cursor_row, nc)) nc++; if (nc < NP_COLS)  *cursor_col = nc; }
}
