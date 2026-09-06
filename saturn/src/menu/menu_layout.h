/*----------------------
 | menu_layout.h
 | Description: Pure layout arithmetic and digit-key mapping for the menu system,
 |   deliberately free of any SRL/Saturn dependency so it unit-tests on the host.
 |   menu.cxx / menu_pages.cxx own the drawing and input and call in here for the
 |   geometry. Implemented in menu_layout.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef MENU_LAYOUT_H
#define MENU_LAYOUT_H

/*----------------------
 | MENU_SCREEN_COLS / MENU_SCREEN_ROWS / MENU_DIGIT_COLS / MENU_ROW_TEXT_MAX
 | Description: The screen size in text cells; the columns reserved for a "N) "
 |   row-number prefix (reserved unconditionally so a box does not resize when the
 |   player switches pad<->keyboard mid-menu); and the longest text a single menu
 |   row can draw without touching the box border (31 = 32 ceiling minus one margin
 |   column, after 5 columns of chrome). Callers building rows from external data
 |   clamp to MENU_ROW_TEXT_MAX, since a clamped box truncates silently.
 |   MENU_SCREEN_ROWS is the one place this project spells out the screen's row
 |   count as a literal; console_view.cxx's SCREEN_ROWS is defined from it rather
 |   than carrying its own copy, specifically so the two cannot read differently
 |   again the way MENU_SCREEN_ROWS itself once did against the real 30.
 | Author: suinevere
 ----------------------*/
#define MENU_SCREEN_COLS 40
#define MENU_SCREEN_ROWS 30
#define MENU_DIGIT_COLS  3
#define MENU_ROW_TEXT_MAX 31

/*----------------------
 | menu_box_fit
 | Description: Fits a centered box around `content_w` columns and `rows` rows:
 |   width is the wider of content and title plus border+pad each side, height is
 |   the content plus top border, title row, blank row, and bottom border; both
 |   clamped so the result is always fully on-screen.
 | Author: suinevere
 ----------------------*/
void menu_box_fit(const char *title, int content_w, int rows,
                  int *x0, int *y0, int *w, int *h);

/*----------------------
 | MENU_DIGIT_ROWS
 | Description: How many rows the digit shortcuts can reach: '1'..'9' then '0',
 |   ten in all. A page with more selectable rows than this still works, it just
 |   leaves the ones past the tenth to the D-pad.
 | Author: suinevere
 ----------------------*/
#define MENU_DIGIT_ROWS 10

/*----------------------
 | menu_row_digit
 | Description: Maps a character to a 0-based row index (or -1). A plain digit
 |   sets *dir to +1; its shifted symbol (!@#$%^&*(), US layout) sets -1, so
 |   value rows cycle forward/backward and action rows just activate. The shifted
 |   character is matched because SaturnKeyEvent carries no modifier flag.
 | Author: suinevere
 ----------------------*/
int menu_row_digit(char ch, int nrows, int *dir);

/*----------------------
 | menu_row_digit_char
 | Description: The key that selects a row, for pages that print the shortcut
 |   beside the label. Exists so a page cannot print a number the player then
 |   cannot type -- the tenth row's key is '0', not "10".
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: row -- 0-based row index
 | Returns: the character, or '\0' when the row is past what digits reach
 ----------------------*/
char menu_row_digit_char(int row);

/*----------------------
 | menu_visible_digit
 | Description: Which absolute list index a plain digit selects through a scroll
 |   window of `visible` rows starting at `top` in a list of `count`; -1 if the
 |   digit names no visible row.
 | Author: suinevere
 ----------------------*/
int menu_visible_digit(char ch, int top, int visible, int count);

/*----------------------
 | SND_ROW_*
 | Description: The Sound page's rows, as IDs rather than positions. The page
 |   shows a different subset on a different disc, so the same position names a
 |   different row and only the ID is stable enough to remember a selection by.
 | Author: suinevere
 ----------------------*/
enum {
    SND_ROW_MASTER = 0,
    SND_ROW_CD     = 1,
    SND_ROW_SYNTH  = 2,
    SND_ROW_PCM    = 3,
    SND_ROW_OK     = 4,
    SND_ROW_CANCEL = 5,
    SND_ROW_SOURCE = 6,
    SND_ROW_TEST   = 7
};

/*----------------------
 | SND_PAGE_ROW_MAX
 | Description: The most rows the page can show at once, for callers sizing the
 |   array they hand in. Every row but Master, Ok and Cancel is conditional, so
 |   this is the sum and not a count of the enum.
 | Author: suinevere
 ----------------------*/
#define SND_PAGE_ROW_MAX 7

/*----------------------
 | sound_page_rows
 | Description: Fills `rows` with the Sound page's visible rows in display
 |   order.
 |
 |   CD Music and Synth Music are still mutually exclusive, but the reason has
 |   changed: it used to be which one the disc had, and it is now which one is
 |   playing. A disc with CD-DA can be switched to the synth, so the page shows
 |   the level of whichever source is active and a Source row to move between
 |   them -- and that row appears only where there are two, since on a disc
 |   without CD-DA it would be a control with one setting.
 |
 |   Test Track sits under the level it is judged against. It is conditional on
 |   the caller because what there is to test differs by source, and a build
 |   carrying one tune with no CD-DA has a row that can only replay the same
 |   thing.
 |
 |   Ok and Cancel are always the last two.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: has_cd -- disc carries CD-DA; has_blb -- game carries a sound blorb;
 |   synth_active -- the synth is the source, whether by preference or because
 |   the disc has no audio; can_test -- there is something to preview;
 |   rows -- destination; max -- its capacity
 | Returns: how many rows were written
 ----------------------*/
int sound_page_rows(int has_cd, int has_blb, int synth_active, int can_test,
                    int *rows, int max);

/*----------------------
 | menu_list_step
 | Description: Moves a cursor through a list of things, wrapping at both ends.
 |   For a LIST and not for a level: the rows of every menu already wrap, and
 |   the track list was the one thing on the Sound page that stopped dead at
 |   each end, which is what got reported. A volume is a scale rather than a
 |   list and must keep clamping -- 7 stepping to 0 is a surprise nobody wants
 |   from a right press.
 |
 |   Inline in the header because both Sound pages need it and they are in
 |   different builds: menu_pages.cxx is the disc's and netbin_pages.cxx is the
 |   browser's, and the two carried their own copy of this arithmetic already.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: index -- where the cursor is; count -- how many entries there are;
 |   left, right -- whether either was pressed this frame
 | Returns: the new index, unchanged when the list is empty
 ----------------------*/
static inline int menu_list_step(int index, int count, int left, int right) {
    if (count <= 0) return index;
    if (left)  index = (index - 1 + count) % count;
    if (right) index = (index + 1) % count;
    return index;
}

#endif
