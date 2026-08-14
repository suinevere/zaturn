/*----------------------
 | numpad.h
 | Description: The dial pages' shared on-screen numpad -- a phone-style 3x4 pad,
 |   digits 1-9 over a lone 0 centred on the bottom row, the two bottom corners
 |   blank and never a cursor target. Both the CD build's network_page and the
 |   netbin build's netbin_dial_page draw and walk it through this one unit instead
 |   of each carrying its own copy. Pure logic -- the character table, the valid-key
 |   test, and one frame of D-pad movement; the pages own the rendering. Implemented
 |   in numpad.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef NUMPAD_H
#define NUMPAD_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | NP_ROWS / NP_COLS
 | Description: The pad's shape: three columns, four rows (1-9 then a lone 0).
 | Author: suinevere
 ----------------------*/
#define NP_ROWS 4
#define NP_COLS 3

/*----------------------
 | np_char / np_valid
 | Description: The character at (r, c) -- a blank for the two empty bottom corners
 |   or an out-of-range cell -- and whether that cell is a real key a cursor may
 |   land on.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: r -- 0..NP_ROWS-1; c -- 0..NP_COLS-1
 | Returns: np_char the key or ' '; np_valid 1 for a real key, 0 otherwise
 ----------------------*/
char np_char(int r, int c);
int  np_valid(int r, int c);

/*----------------------
 | np_dpad
 | Description: One frame of D-pad over the pad and the two action rows a dial page
 |   sits below it. `*arow` is -1 on the pad, 0 on the first row, 1 on the second.
 |   Up climbs the pad and, from row 0, back into it -- but only when pad_active,
 |   since a real keyboard hides the pad and the cursor must not dive into nothing;
 |   Down drops off the pad's bottom onto row 0, then row 1. Left/Right walk the pad
 |   columns, skipping the blank corners, and do nothing off the pad. The pages read
 |   the C/A/B/Accept buttons themselves, since what the two rows mean differs.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: up/down/left/right -- the pressed edges this frame; pad_active -- whether
 |   the pad is on screen; arow -- (in/out) -1/0/1 as above; cursor_row/cursor_col --
 |   (in/out) the pad cursor
 | Returns: N/A
 ----------------------*/
void np_dpad(int up, int down, int left, int right, int pad_active,
             int *arow, int *cursor_row, int *cursor_col);

#ifdef __cplusplus
}
#endif
#endif /* NUMPAD_H */
