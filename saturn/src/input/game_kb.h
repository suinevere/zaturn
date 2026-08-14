/*----------------------
 | game_kb.h
 | Description: The in-game on-screen keyboard's grid: a fixed five-row key block
 |   (symbols, numbers, and three letter rows) over a single wide space bar, ten
 |   columns wide. Separate from keyboard.h's shared 4x13 grid, which the menu
 |   and online-terminal keyboards still use, so this layout can differ without
 |   disturbing them. Pure logic -- character lookup and picker movement only, no
 |   rendering or device polling. Implemented in game_kb.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef GAME_KB_H
#define GAME_KB_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | GKB_COLS / GKB_KEY_ROWS / GKB_ROWS / GKB_SPACE_ROW
 | Description: Ten columns; five key rows (0..4: symbols, numbers, qwerty x3);
 |   GKB_ROWS counts those plus the space-bar row, which is GKB_SPACE_ROW.
 | Author: suinevere
 ----------------------*/
#define GKB_COLS      10
#define GKB_KEY_ROWS  5
#define GKB_ROWS      6
#define GKB_SPACE_ROW 5

/*----------------------
 | game_kb_char_at
 | Description: The character at (row, col) in the active layer. The space-bar row
 |   is a space at any column; CapsLock uppercases only the letter rows, which a
 |   plain a..z test picks out since the symbol and number rows hold none.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: row -- 0..GKB_ROWS-1; col -- 0..GKB_COLS-1; caps -- non-zero to shift
 |   letters to uppercase
 | Returns: the key character
 ----------------------*/
char game_kb_char_at(int row, int col, int caps);

/*----------------------
 | game_kb_move
 | Description: Steps the picker by one on a single axis. Rows wrap through the
 |   space bar; the space bar keeps the column it was entered on for the return
 |   trip up. A rightward press stops at the last column; a leftward press off
 |   column zero (or off the space bar) does not move and reports the edge, so the
 |   caller can hand focus to the compass rose sitting to the left.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: row, col -- the picker cell, updated in place; dcol, drow -- each -1, 0
 |   or +1
 | Returns: 1 when a leftward press ran off the left edge, 0 otherwise
 ----------------------*/
int game_kb_move(int *row, int *col, int dcol, int drow);

#ifdef __cplusplus
}
#endif
#endif /* GAME_KB_H */
