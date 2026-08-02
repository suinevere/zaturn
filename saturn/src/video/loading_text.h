/*----------------------
 | loading_text.h
 | Description: Builds the post-selection loading screen's fixed boot-text
 |   block, substituting the chosen game's title into the two spots that
 |   need it and truncating per-line so no row ever exceeds the console's 40
 |   columns. Pure string logic, no SRL/hardware dependency -- host-testable
 |   with plain gcc (see saturn/tests/test_loading_text.c).
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef LOADING_TEXT_H
#define LOADING_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#define LOADING_TEXT_COLS  40
#define LOADING_TEXT_LINES 11

/*----------------------
 | loading_text_build
 | Description: Fills `lines` with the 11-row boot sequence, NUL-terminated
 |   per row, substituting `title` for the LOAD/SEARCHING FOR lines' target
 |   and truncating it (a plain byte cut, no ellipsis) if the fixed
 |   prefix/suffix on that row would otherwise push past LOADING_TEXT_COLS.
 |   A NULL title is treated as an empty string.
 | Author: suinevere
 | Dependencies: none
 | Params: title -- the game's display title, or NULL; lines -- output,
 |   caller-owned
 | Returns: N/A
 ----------------------*/
void loading_text_build(const char *title, char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1]);

#ifdef __cplusplus
}
#endif
#endif /* LOADING_TEXT_H */
