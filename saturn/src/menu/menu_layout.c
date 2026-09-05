/*----------------------
 | menu_layout.c
 | Description: The pure, screen-fitting geometry and digit-key mapping behind the
 |   menu framework -- box sizing and centering, and the maps from a typed digit
 |   (or its shifted symbol) to a menu row. No SRL or rendering, so it unit-tests
 |   on the host; menu.cxx / menu_pages.cxx draw and poll using these results.
 | Author: suinevere
 | Dependencies: menu_layout.h (MENU_SCREEN_COLS/ROWS)
 ----------------------*/
#include "menu_layout.h"

/*----------------------
 | menu_box_fit
 | Description: Computes a centered menu box big enough for its title and content.
 |   Guards against pathological input: the title count stops at the screen width
 |   (it only competes with content_w for "widest"), rows gets a negative floor
 |   (its height has no max() shield), and both inputs are clamped to the screen
 |   BEFORE the "+4" borders/padding so a huge value cannot overflow into a
 |   negative size that escapes the clamp. The result is clamped again and centered.
 | Author: suinevere
 | Dependencies: menu_layout.h
 | Globals: N/A
 | Params: title -- box title (may be NULL); content_w -- widest content row;
 |   rows -- content row count; x0/y0/w/h -- receive the box geometry in cells
 | Returns: N/A
 ----------------------*/
void menu_box_fit(const char *title, int content_w, int rows,
                  int *x0, int *y0, int *w, int *h) {
    int tlen = 0;
    int bw, bh;

    if (title != 0) while (tlen < MENU_SCREEN_COLS && title[tlen] != '\0') tlen++;

    if (rows < 0) rows = 0;

    if (content_w > MENU_SCREEN_COLS) content_w = MENU_SCREEN_COLS;
    if (rows > MENU_SCREEN_ROWS)      rows = MENU_SCREEN_ROWS;

    bw = (tlen > content_w ? tlen : content_w) + 4;   /* borders + pads */
    bh = rows + 4;                                    /* borders + title + blank */

    if (bw > MENU_SCREEN_COLS) bw = MENU_SCREEN_COLS;
    if (bh > MENU_SCREEN_ROWS) bh = MENU_SCREEN_ROWS;

    *w  = bw;
    *h  = bh;
    *x0 = (MENU_SCREEN_COLS - bw) / 2;
    *y0 = (MENU_SCREEN_ROWS - bh) / 2;
}

/*----------------------
 | MENU_DIGIT_KEYS / MENU_SHIFTED_DIGITS
 | Description: The keys that select a row, in row order, and their shifted forms
 |   on a US layout -- index 0 is '1'/'!', index 9 is '0'/')'. Zero comes last
 |   because that is where it sits on a keyboard: the tenth row is reached with
 |   the tenth key, not with a two-digit number nobody can type.
 | Author: suinevere
 ----------------------*/
static const char MENU_DIGIT_KEYS[]     = "1234567890";
static const char MENU_SHIFTED_DIGITS[] = "!@#$%^&*()";

/*----------------------
 | menu_plain_digit_row / menu_shifted_digit_row
 | Description: Map a plain digit or a shifted digit symbol to its 0-based row
 |   index, or -1 if the character is neither.
 | Author: suinevere
 ----------------------*/
static int menu_plain_digit_row(char ch) {
    int i;
    for (i = 0; i < MENU_DIGIT_ROWS; i++) if (MENU_DIGIT_KEYS[i] == ch) return i;
    return -1;
}

static int menu_shifted_digit_row(char ch) {
    int i;
    for (i = 0; i < MENU_DIGIT_ROWS; i++) if (MENU_SHIFTED_DIGITS[i] == ch) return i;
    return -1;
}

char menu_row_digit_char(int row) {
    if (row < 0 || row >= MENU_DIGIT_ROWS) return '\0';
    return MENU_DIGIT_KEYS[row];
}

/*----------------------
 | menu_row_digit
 | Description: Maps a typed character to a selectable row and a direction: a plain
 |   digit selects that row forward (+1), its shifted symbol selects it backward
 |   (-1). Rejects anything outside [0, nrows).
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: ch -- the typed character; nrows -- number of selectable rows; dir --
 |   receives +1/-1 on a match (may be NULL)
 | Returns: the row index, or -1 if `ch` maps to no row
 ----------------------*/
int menu_row_digit(char ch, int nrows, int *dir) {
    int row = menu_plain_digit_row(ch);
    int d   = 1;

    if (row < 0) {
        row = menu_shifted_digit_row(ch);
        d   = -1;
    }
    if (row < 0 || row >= nrows) return -1;

    if (dir != 0) *dir = d;
    return row;
}

/*----------------------
 | menu_visible_digit
 | Description: Maps a plain digit to an absolute list index through a scroll
 |   window: the digit picks a visible row (0-based), which is offset by `top` and
 |   validated against the list size, so digits stay correct while a long list is
 |   scrolled. Shifted digits are not used here.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: ch -- typed digit; top -- index of the first visible row; visible --
 |   window height; count -- total list length
 | Returns: the absolute item index, or -1 if out of the window or the list
 ----------------------*/
int menu_visible_digit(char ch, int top, int visible, int count) {
    int row = menu_plain_digit_row(ch);
    int idx;

    if (row < 0 || row >= visible) return -1;
    idx = top + row;
    if (idx < 0 || idx >= count) return -1;
    return idx;
}

int sound_page_rows(int has_cd, int has_blb, int *rows, int max) {
    int n = 0;
    if (n < max) rows[n++] = SND_ROW_MASTER;
    if (has_cd) { if (n < max) rows[n++] = SND_ROW_CD; }
    else        { if (n < max) rows[n++] = SND_ROW_SYNTH; }
    if (has_blb) { if (n < max) rows[n++] = SND_ROW_PCM; }
    if (n < max) rows[n++] = SND_ROW_OK;
    if (n < max) rows[n++] = SND_ROW_CANCEL;
    return n;
}
