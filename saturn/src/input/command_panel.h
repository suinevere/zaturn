/*----------------------
 | command_panel.h
 | Description: The command panel's state: which of the three modules has focus,
 |   which sentence slot is being filled, where the cursor sits, how far the
 |   word list is scrolled, and the command assembled so far. Pure logic --
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
 | CP_SLOT_VERB .. CP_SLOT_DONE / CP_SLOT_POS_MAX
 | Description: What the panel is waiting for, rather than where in a fixed chain
 |   it stands: a verb, an object, a preposition, or DONE, meaning the command is
 |   complete and has been marked for submission. A sentence can want two
 |   prepositions and two objects, so the same value comes round twice and the
 |   order they fill in is the caller's to supply. CP_SLOT_POS_MAX is the most
 |   words one command can be built from -- verb, prep, noun, prep, noun -- and so
 |   how many places the per-position cursor memory holds.
 | Author: suinevere
 ----------------------*/
enum { CP_SLOT_VERB = 0, CP_SLOT_NOUN, CP_SLOT_PREP, CP_SLOT_DONE };
#define CP_SLOT_POS_MAX 5

/*----------------------
 | CP_ACT_NONE / CP_ACT_MAP / CP_ACT_MENU / CP_ACT_SWAP
 | Description: Something the command module asks for that is not a command to
 |   the story: the map screen, the pause menu, or a swap to the other dashboard
 |   mode. The panel cannot do any of them itself -- the first two run their own
 |   loops and own the whole display while they are up, and the third has to move
 |   a half-built command between two buffers the panel only holds one of -- so
 |   the request is left in CommandPanel::action for the hosting frame loop to
 |   spend and clear.
 | Author: suinevere
 ----------------------*/
enum { CP_ACT_NONE = 0, CP_ACT_MAP, CP_ACT_MENU, CP_ACT_SWAP };

/*----------------------
 | CP_TAB_VERB .. CP_TAB_N
 | Description: The four word lists the module can show, left to right along the
 |   strip above it: the verbs, the objects, the prepositions, and the whole
 |   dictionary by first letter. The slot picks one by default; the player may
 |   pick another, and that choice lasts until the next word is picked.
 | Author: suinevere
 ----------------------*/
enum { CP_TAB_VERB = 0, CP_TAB_NOUN, CP_TAB_PREP, CP_TAB_AZ, CP_TAB_N };

/*----------------------
 | CP_ZONE_LIST / CP_ZONE_TABS / CP_ZONE_LETTERS
 | Description: Which part of the word module the cursor is in, and so what its
 |   index means: a cell of the word grid, a tab of the strip above it, or a
 |   letter of the alphabet grid the A-Z tab opens.
 | Author: suinevere
 ----------------------*/
enum { CP_ZONE_LIST = 0, CP_ZONE_TABS, CP_ZONE_LETTERS };

/*----------------------
 | CommandPanel
 | Description: One prompt's panel state.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int  box;
    int  slot;
    int  cursor;
    int  top;       /* first candidate row showing in the word module */
    /* Where the cursor was left at each word POSITION, so moving on puts it back
       rather than at the top. Indexed by the word count at the time, not by the
       slot kind: a sentence can hold two objects, and the second must not open on
       the row the first was left at. Survives cp_reset, which re-points position
       zero at wherever the cursor actually is rather than moving the cursor to
       it. */
    int  slot_cursor[CP_SLOT_POS_MAX];
    int  slot_top[CP_SLOT_POS_MAX];
    char line[CP_LINE_MAX];
    int  line_len;
    int  submitted;
    int  zone;          /* CP_ZONE_* -- what the cursor is indexing */
    int  tab;           /* CP_TAB_* -- which list is showing */
    int  tab_override;  /* 1 while the player's tab outranks the slot's */
    int  letter;        /* 0..25, the A-Z grid's cursor */
    int  overlay;   /* 1 while the inventory overlay is up */
    int  action;    /* CP_ACT_* the host loop owes, CP_ACT_NONE when none */
} CommandPanel;

/*----------------------
 | cp_init
 | Description: Prepares a panel that has never been used: clears the per-slot
 |   rows and puts focus on the word module at the top of the verb list, then
 |   resets it. This is the only call that chooses where the cursor starts --
 |   cp_reset leaves it where the player put it -- so a panel that skips this one
 |   opens on whatever the stack happened to hold.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state to prepare
 | Returns: N/A
 ----------------------*/
void cp_init(CommandPanel *p);

/*----------------------
 | cp_reset
 | Description: Clears the assembled command and returns the slot to VERB, and
 |   moves nothing the player can see: the module in focus and the cursor in it
 |   are left exactly where they were. The per-prompt entry, and also what a
 |   submitted command runs through, which is the same thing said twice -- so a
 |   direction sent off the rose leaves the cursor on that direction, ready to be
 |   sent again, instead of dropping the player into the verb list. A panel that
 |   has never been touched wants cp_init first.
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
 |   ends rather than wrapping, and resets the cursor and scroll for the module
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
 |   slot named by `next_slot`, which the caller has already read off the story's
 |   grammar -- this file holds no chain of its own, since only the caller can
 |   know whether a preposition belongs before the next object or the sentence is
 |   finished. A pick made from the travel module completes immediately whatever
 |   is passed, since a direction is a whole command. Marks `submitted` when the
 |   command is complete. A pick made from the inventory overlay hands focus back
 |   to the word module on its way out, since the cursor it leaves behind is a
 |   word cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; word -- the word picked; next_slot -- the slot to
 |   open after this word, one of CP_SLOT_*
 | Returns: N/A
 ----------------------*/
void cp_pick(CommandPanel *p, const char *word, int next_slot);

/*----------------------
 | cp_set_slot
 | Description: Points the panel at `slot`, taking back that position's remembered
 |   cursor -- the caller's way of saying what the sentence wants next after a
 |   change this file cannot resolve on its own, which is every change that is not
 |   a pick: a Back, a recalled line, a tab override that took the sentence off
 |   grammar. Out-of-range values are ignored rather than stored.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; slot -- one of CP_SLOT_*
 | Returns: N/A
 ----------------------*/
void cp_set_slot(CommandPanel *p, int slot);

/*----------------------
 | cp_word_count
 | Description: How many space-separated words the command holds, which is the
 |   position the next pick fills and the index its place is remembered at.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state
 | Returns: the word count, 0 for an empty line
 ----------------------*/
int cp_word_count(const CommandPanel *p);

/*----------------------
 | cp_pick_whole
 | Description: Picks a word that is the whole command, replacing whatever the
 |   line held and submitting it at once -- what a rose direction already does
 |   through cp_pick's travel branch, made available to a word module that is
 |   offering answers rather than sentence openings. Not a shortcut for
 |   cp_pick + cp_submit: those two walk the slot chain on the way past, which
 |   restores the noun slot's remembered row and so moves the cursor off the word
 |   just picked, and an answer list wants the cursor left where it was so the
 |   same answer can be given again.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; word -- the whole command, ignored when empty
 | Returns: N/A
 ----------------------*/
void cp_pick_whole(CommandPanel *p, const char *word);

/*----------------------
 | cp_submit
 | Description: Marks the command submitted as it stands, however far short of
 |   the grammar slot chain it stops -- the player's explicit send, alongside the
 |   automatic one cp_pick performs when the chain completes. A no-op on an empty
 |   line; the caller guards the overlay case.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state
 | Returns: N/A
 ----------------------*/
void cp_submit(CommandPanel *p);

/*----------------------
 | cp_load_line
 | Description: Replaces the command with `text`, leaving the panel as though
 |   those words had been picked one at a time, so Back unwinds a recalled command
 |   like a built one. The slot is VERB on an empty line and NOUN otherwise; a
 |   caller that knows the story's grammar corrects it with cp_set_slot. Null or
 |   empty clears to the verb slot. Focus is not moved.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; text -- the command to load, may be null or empty
 | Returns: N/A
 ----------------------*/
void cp_load_line(CommandPanel *p, const char *text);

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
 | Description: The word module's visible window: the words to draw in cell
 |   order, how many cells are filled, which candidate row sits at the top, and
 |   how many rows the whole list occupies. Every cell holds a real word -- the
 |   list scrolls a row at a time against the bottom edge rather than spending a
 |   cell on a "more" marker, so the module offers ten choices and not nine.
 | Author: suinevere
 ----------------------*/
typedef struct {
    const char *word[CP_WORD_CELLS];
    int         n;
    int         top;
    int         rows;
} CommandWords;

/*----------------------
 | cp_fill
 | Description: Fills the word module's window from an ordered candidate list,
 |   starting at candidate row `top`, which is clamped so the window never hangs
 |   past the end of the list.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: cands -- ordered candidates; ncand -- how many; top -- first
 |   candidate row to show; out -- receives the window
 | Returns: N/A
 ----------------------*/
void cp_fill(const char *const *cands, int ncand, int top, int rows_visible,
             CommandWords *out);

/*----------------------
 | cp_clamp
 | Description: Brings the scroll and the cursor back inside a list of `ncand`
 |   candidates. Call once per frame after the list is sourced: the cursor a
 |   slot change restores was measured against that slot's previous list, which
 |   may have been longer, and until this runs it can name a cell the new list
 |   does not reach.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; ncand -- candidate count for the current slot
 | Returns: N/A
 ----------------------*/
void cp_clamp(CommandPanel *p, int ncand, int rows_visible);

/*----------------------
 | cp_word_rows
 | Description: How many candidate rows a list occupies.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: ncand -- candidate count
 | Returns: the row count, 0 for an empty list
 ----------------------*/
int cp_word_rows(int ncand);

/*----------------------
 | cp_word_move
 | Description: Steps the word module's cursor one cell, spreadsheet fashion:
 |   left and right stay on their row instead of running on into the next one,
 |   and up and down at the window's edge scroll the list by a row rather than
 |   stopping. A horizontal press with nowhere to go reports which edge it hit,
 |   which is what lets the caller carry focus into the module beside it. The
 |   cursor never lands on an empty cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; dx, dy -- -1, 0 or +1; ncand -- the whole candidate
 |   count, not just the visible part
 | Returns: 0 when the cursor stayed in the module, -1 when it stepped off the
 |   left edge, +1 when it stepped off the right
 ----------------------*/
int cp_word_move(CommandPanel *p, int dx, int dy, int ncand, int rows_visible);

/*----------------------
 | cp_tab_for_slot
 | Description: The list a slot asks for while nothing is overriding it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- one of CP_SLOT_*
 | Returns: one of CP_TAB_*; the verb tab for DONE, which shows no list
 ----------------------*/
int cp_tab_for_slot(int slot);

/*----------------------
 | cp_tab_move
 | Description: Steps along the tab strip, marking the choice as the player's. A
 |   step off either end is the module's edge, reported the way cp_word_move
 |   reports the list's column edges so focus crosses out by the same rule.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; dx -- -1 or +1
 | Returns: -1 or +1 when the step left the strip sideways, 0 when it did not
 ----------------------*/
int cp_tab_move(CommandPanel *p, int dx);

/*----------------------
 | cp_letter_move
 | Description: Walks the A-Z tab's two rows of thirteen letters. Up off the top
 |   row returns to the tab strip and down off the bottom row enters the word
 |   list, so the three zones are one column the cursor walks; a step off either
 |   side is the module's edge, reported as cp_word_move reports it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; dx -- -1, 0 or +1; dy -- -1, 0 or +1
 | Returns: -1 or +1 when the step left the module sideways, 0 otherwise
 ----------------------*/
int cp_letter_move(CommandPanel *p, int dx, int dy);

/*----------------------
 | cp_word_enter
 | Description: Places the cursor when focus arrives in the word module: on the
 |   window row asked for, in the column nearest the edge it came through, backed
 |   off to a filled cell. The scroll position is left where the module last had
 |   it, so leaving and returning does not lose the player's place in a long list.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; row -- window row to aim for; from_right -- 1 when
 |   focus arrived from the module to the right; ncand -- candidate count
 | Returns: N/A
 ----------------------*/
void cp_word_enter(CommandPanel *p, int row, int from_right, int ncand,
                   int rows_visible);

#ifdef __cplusplus
}
#endif
#endif /* COMMAND_PANEL_H */
