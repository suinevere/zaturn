/*----------------------
 | command_panel.h
 | Description: The command panel's state: which of the three modules has focus,
 |   which sentence slot is being filled, where the cursor sits, which page of
 |   the word list is showing, and the command assembled so far. Pure logic --
 |   no rendering, no device polling, and no opinion about where candidate words
 |   come from; the caller supplies them already ordered. Implemented in
 |   command_panel.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef COMMAND_PANEL_H
#define COMMAND_PANEL_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | CP_WORD_COLS / CP_WORD_ROWS / CP_WORD_CELLS / CP_WORD_MAX / CP_LINE_MAX
 | Description: The word module's two columns over five rows, the cell count
 |   they make, the display width of one word (six characters plus its NUL --
 |   six is what a v3 dictionary entry distinguishes), and the assembled
 |   command's capacity, matching KB_INPUT_MAX.
 | Author: suinevere
 ----------------------*/
#define CP_WORD_COLS  2
#define CP_WORD_ROWS  5
#define CP_WORD_CELLS (CP_WORD_COLS * CP_WORD_ROWS)
#define CP_WORD_MAX   7
#define CP_LINE_MAX   64

/*----------------------
 | CP_BOX_TRAVEL / CP_BOX_WORD / CP_BOX_CMD / CP_BOX_N
 | Description: The three modules, left to right.
 | Author: suinevere
 ----------------------*/
enum { CP_BOX_TRAVEL = 0, CP_BOX_WORD, CP_BOX_CMD, CP_BOX_N };

/*----------------------
 | CP_SLOT_VERB .. CP_SLOT_DONE
 | Description: The sentence slots, in the order they fill. DONE means the
 |   command is complete and has been marked for submission.
 | Author: suinevere
 ----------------------*/
enum { CP_SLOT_VERB = 0, CP_SLOT_NOUN, CP_SLOT_PREP, CP_SLOT_NOUN2, CP_SLOT_DONE };

/*----------------------
 | CommandPanel
 | Description: One prompt's panel state.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int  box;
    int  slot;
    int  cursor;
    int  page;
    char line[CP_LINE_MAX];
    int  line_len;
    int  submitted;
    int  overlay;   /* 1 while the inventory overlay is up */
} CommandPanel;

/*----------------------
 | cp_reset
 | Description: Clears the assembled command and returns focus to the word
 |   module at the verb slot, page zero.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state to clear
 | Returns: N/A
 ----------------------*/
void cp_reset(CommandPanel *p);

/*----------------------
 | cp_focus
 | Description: Moves focus one module left (-1) or right (+1), clamped at the
 |   ends rather than wrapping, and resets the cursor and page for the module
 |   arrived at.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; dir -- -1 or +1
 | Returns: N/A
 ----------------------*/
void cp_focus(CommandPanel *p, int dir);

/*----------------------
 | cp_move
 | Description: Steps the cursor within the focused module by `d`, clamped to
 |   0..count-1.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; d -- signed step; count -- entries in the module
 | Returns: N/A
 ----------------------*/
void cp_move(CommandPanel *p, int d, int count);

/*----------------------
 | cp_pick
 | Description: Appends `word` to the command, space-separated, and advances the
 |   slot. wants_prep is consulted only when leaving the noun slot: set, the
 |   preposition slot opens; clear, the command is complete. A pick made from the
 |   travel module completes immediately, since a direction is a whole command.
 |   Marks `submitted` when the command is complete.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; word -- the word picked; wants_prep -- 1 when the
 |   story's grammar says this verb takes a preposition
 | Returns: N/A
 ----------------------*/
void cp_pick(CommandPanel *p, const char *word, int wants_prep);

/*----------------------
 | cp_back
 | Description: Removes the last word from the command and steps the slot back
 |   one. From an empty command at the verb slot, moves focus to the travel
 |   module instead.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state
 | Returns: N/A
 ----------------------*/
void cp_back(CommandPanel *p);

/*----------------------
 | cp_overlay_open / cp_overlay_close / cp_overlay_takes_noun
 | Description: Raises and lowers the inventory overlay, and reports whether a
 |   pick made from it would land somewhere -- true only while the panel is
 |   waiting for a noun. With a verb slot active the overlay is a viewer, since
 |   a held object cannot start a sentence.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state
 | Returns: cp_overlay_takes_noun returns 1 when a pick would fill a slot
 ----------------------*/
void cp_overlay_open(CommandPanel *p);
void cp_overlay_close(CommandPanel *p);
int  cp_overlay_takes_noun(const CommandPanel *p);

/*----------------------
 | CommandWords
 | Description: One page of the word module: the words to draw in cell order and
 |   whether the last cell should instead read "v more".
 | Author: suinevere
 ----------------------*/
typedef struct {
    const char *word[CP_WORD_CELLS];
    int         n;
    int         more;
} CommandWords;

/*----------------------
 | cp_fill
 | Description: Fills one page of the word module from an ordered candidate
 |   list. A list that fits uses every cell; one that does not gives its last
 |   cell to the "v more" marker and pages by CP_WORD_CELLS - 1, so no candidate
 |   is skipped by the marker.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: cands -- ordered candidates; ncand -- how many; page -- zero-based
 |   page; out -- receives the page
 | Returns: N/A
 ----------------------*/
void cp_fill(const char *const *cands, int ncand, int page, CommandWords *out);

/*----------------------
 | cp_pages
 | Description: How many pages a candidate list occupies.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: ncand -- candidate count
 | Returns: the page count, at least 1
 ----------------------*/
int cp_pages(int ncand);

#ifdef __cplusplus
}
#endif
#endif /* COMMAND_PANEL_H */
