/*----------------------
 | command_panel.c
 | Description: The panel state machine described in command_panel.h.
 | Author: suinevere
 | Dependencies: command_panel.h
 ----------------------*/
#include "command_panel.h"

/*----------------------
 | slot_remember / slot_restore
 | Description: Save the word module's place in the slot being left, and take
 |   back the place held in the slot being entered. Two halves of one rule: each
 |   slot's list keeps its own cursor, so a slot change never drops the player at
 |   the top of a list they were part-way down, and the verb they used last turn
 |   is still under the cursor on the next one. A restored cursor was measured
 |   against that slot's previous list and can name a cell a shorter one does not
 |   reach, which is what cp_clamp answers for on the refill that follows.
 |
 |   The bound at the end is for a panel whose rows were never initialised: this
 |   reads them on the very first cp_reset, so a caller that skipped cp_init on a
 |   stack panel would restore a cursor out of whatever was there. It bounds the
 |   damage to something the module can draw rather than curing it -- cp_init is
 |   the cure, and the shipped panels take it or live in static storage.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state
 | Returns: N/A
 ----------------------*/
static void slot_remember(CommandPanel *p) {
    if (p->slot >= 0 && p->slot < CP_SLOT_DONE) {
        p->slot_cursor[p->slot] = p->cursor;
        p->slot_top[p->slot]    = p->top;
    }
}

static void slot_restore(CommandPanel *p) {
    if (p->slot >= 0 && p->slot < CP_SLOT_DONE) {
        p->cursor = p->slot_cursor[p->slot];
        p->top    = p->slot_top[p->slot];
    } else {
        p->cursor = 0;
        p->top    = 0;
    }
    if (p->cursor < 0 || p->cursor >= CP_WORD_CELLS) p->cursor = 0;
    if (p->top < 0) p->top = 0;
}

/*----------------------
 | cp_reset
 | Description: Clears the assembled command and returns focus to the word
 |   module at the verb slot, on the row that slot was last left on. The
 |   remembered rows deliberately survive this: it runs once per prompt, and
 |   zeroing them here would put the cursor back at the top of the verb list
 |   every turn, which is the whole thing slot_restore exists to stop.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state to clear
 | Returns: N/A
 ----------------------*/
void cp_init(CommandPanel *p) {
    int i;
    for (i = 0; i < CP_SLOT_DONE; i++) { p->slot_cursor[i] = 0; p->slot_top[i] = 0; }
    cp_reset(p);
}

void cp_reset(CommandPanel *p) {
    p->box = CP_BOX_WORD;
    p->slot = CP_SLOT_VERB;
    slot_restore(p);
    p->line[0] = '\0';
    p->line_len = 0;
    p->submitted = 0;
    p->overlay = 0;
}

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
void cp_focus(CommandPanel *p, int dir) {
    int b = p->box + dir;
    if (b < 0) b = 0;
    if (b >= CP_BOX_N) b = CP_BOX_N - 1;
    if (b != p->box) { p->box = b; p->cursor = 0; p->top = 0; }
}

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
void cp_move(CommandPanel *p, int d, int count) {
    int c = p->cursor + d;
    if (count <= 0) { p->cursor = 0; return; }
    if (c < 0) c = 0;
    if (c >= count) c = count - 1;
    p->cursor = c;
}

/*----------------------
 | cp_pick
 | Description: Appends `word` to the command, space-separated, and advances the
 |   slot. wants_prep is consulted only when leaving the noun slot: set, the
 |   preposition slot opens; clear, the command is complete. A pick made from the
 |   travel module completes immediately, since a direction is a whole command.
 |   Marks `submitted` when the command is complete. While the overlay is up,
 |   this is also its sole close path: a pick that cannot land -- the panel is
 |   waiting for a verb, or `word` is empty -- closes the overlay instead of
 |   being silently dropped, so the picking button can never leave it stuck open.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; word -- the word picked, may be null or empty when
 |   the overlay had nothing to offer; wants_prep -- 1 when the story's grammar
 |   says this verb takes a preposition
 | Returns: N/A
 ----------------------*/
void cp_pick(CommandPanel *p, const char *word, int wants_prep) {
    int i = 0;
    int empty = (word == 0 || word[0] == '\0');
    if (p->overlay && (!cp_overlay_takes_noun(p) || empty)) { cp_overlay_close(p); return; }
    if (empty) return;
    if (p->line_len > 0 && p->line_len < CP_LINE_MAX - 1) p->line[p->line_len++] = ' ';
    while (word[i] && p->line_len < CP_LINE_MAX - 1) p->line[p->line_len++] = word[i++];
    p->line[p->line_len] = '\0';

    if (p->box == CP_BOX_TRAVEL) { p->slot = CP_SLOT_DONE; p->submitted = 1; p->overlay = 0; return; }

    slot_remember(p);
    switch (p->slot) {
        case CP_SLOT_VERB:  p->slot = CP_SLOT_NOUN; break;
        case CP_SLOT_NOUN:  p->slot = wants_prep ? CP_SLOT_PREP : CP_SLOT_DONE; break;
        case CP_SLOT_PREP:  p->slot = CP_SLOT_NOUN2; break;
        case CP_SLOT_NOUN2: p->slot = CP_SLOT_DONE; break;
        default: break;
    }
    slot_restore(p);
    p->overlay = 0;
    if (p->slot == CP_SLOT_DONE) p->submitted = 1;
}

/*----------------------
 | cp_submit
 | Description: Sends the command as it stands, however far short of the grammar
 |   slot chain it stops -- the player's explicit "that will do", alongside the
 |   automatic submit cp_pick performs when the chain runs out on its own. Does
 |   nothing on an empty line, so the button cannot post a blank command, and
 |   nothing while the overlay is up: that is the caller's guard, since only the
 |   caller knows a picker is open over the line.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state
 | Returns: N/A
 ----------------------*/
void cp_submit(CommandPanel *p) {
    if (p->line_len == 0) return;
    p->slot = CP_SLOT_DONE;
    p->submitted = 1;
}

/*----------------------
 | cp_load_line
 | Description: Replaces the command with `text` and leaves the panel in the
 |   state it would have been in had the player picked those words one at a
 |   time, so Back unwinds a recalled command exactly like a built one: the slot
 |   is derived from the word count along the VERB -> NOUN -> (PREP -> NOUN2) ->
 |   DONE chain. Two words land on DONE rather than PREP because whether the verb
 |   takes a preposition is the trie's answer, not this file's, and a recalled
 |   line has no pick to ask on; a verb that does wants one more Back than it
 |   would have taken. An empty or null `text` clears the line to the verb slot.
 |   Focus is left where it was -- a recall is not a reason to move the player
 |   off the module they were reading. Truncates at CP_LINE_MAX, which equals
 |   KB_INPUT_MAX, so nothing the history can hold is ever cut.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; text -- the command to load, may be null or empty
 | Returns: N/A
 ----------------------*/
void cp_load_line(CommandPanel *p, const char *text) {
    int i = 0, words = 0, in_word = 0;
    while (text != 0 && text[i] != '\0' && i < CP_LINE_MAX - 1) {
        if (text[i] == ' ') in_word = 0;
        else if (!in_word) { in_word = 1; words++; }
        p->line[i] = text[i];
        i++;
    }
    p->line[i] = '\0';
    p->line_len = i;
    p->slot = (words == 0) ? CP_SLOT_VERB
            : (words == 1) ? CP_SLOT_NOUN
            : (words == 3) ? CP_SLOT_NOUN2
                           : CP_SLOT_DONE;
    slot_restore(p);
    p->overlay = 0;
    p->submitted = 0;
}

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
void cp_back(CommandPanel *p) {
    int i;
    if (p->line_len == 0) {
        if (p->box == CP_BOX_WORD) p->box = CP_BOX_TRAVEL;
        return;
    }
    for (i = p->line_len - 1; i >= 0; i--) if (p->line[i] == ' ') break;
    p->line_len = (i < 0) ? 0 : i;
    p->line[p->line_len] = '\0';
    slot_remember(p);
    if (p->slot > CP_SLOT_VERB) p->slot--;
    if (p->slot > CP_SLOT_NOUN && p->line_len == 0) p->slot = CP_SLOT_VERB;
    slot_restore(p);
    p->submitted = 0;
}

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
void cp_overlay_open(CommandPanel *p) { p->overlay = 1; p->cursor = 0; }

void cp_overlay_close(CommandPanel *p) { p->overlay = 0; p->cursor = 0; }

int cp_overlay_takes_noun(const CommandPanel *p) {
    return p->overlay && (p->slot == CP_SLOT_NOUN || p->slot == CP_SLOT_NOUN2);
}

/*----------------------
 | cp_word_rows
 | Description: How many candidate rows a list occupies.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: ncand -- candidate count
 | Returns: the row count, 0 for an empty list
 ----------------------*/
int cp_word_rows(int ncand) {
    if (ncand <= 0) return 0;
    return (ncand + CP_WORD_COLS - 1) / CP_WORD_COLS;
}

/*----------------------
 | cp_top_max
 | Description: The furthest the window can scroll before it hangs past the end
 |   of the list.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: ncand -- candidate count
 | Returns: the largest valid `top`, never negative
 ----------------------*/
static int cp_top_max(int ncand) {
    int rows = cp_word_rows(ncand);
    return (rows > CP_WORD_ROWS) ? (rows - CP_WORD_ROWS) : 0;
}

/*----------------------
 | cp_clamp_cursor
 | Description: Backs the cursor off any cell the list does not reach, so it
 |   always sits on a word. Called after anything that can shorten the window --
 |   a scroll onto a part-filled last row, or a refill with fewer candidates.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; ncand -- candidate count
 | Returns: N/A
 ----------------------*/
/*----------------------
 | cp_clamp
 | Description: See command_panel.h. The scroll first, because the cursor is
 |   clamped against the window the scroll selects and a `top` left past the end
 |   of a shorter list would make every cell in it read as empty.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; ncand -- candidate count for the current slot
 | Returns: N/A
 ----------------------*/
static void cp_clamp_cursor(CommandPanel *p, int ncand);

void cp_clamp(CommandPanel *p, int ncand) {
    if (p->top < 0) p->top = 0;
    if (p->top > cp_top_max(ncand)) p->top = cp_top_max(ncand);
    cp_clamp_cursor(p, ncand);
}

static void cp_clamp_cursor(CommandPanel *p, int ncand) {
    int filled = ncand - p->top * CP_WORD_COLS;
    if (filled > CP_WORD_CELLS) filled = CP_WORD_CELLS;
    if (filled <= 0) { p->cursor = 0; return; }
    if (p->cursor >= filled) p->cursor = filled - 1;
    if (p->cursor < 0) p->cursor = 0;
}

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
void cp_fill(const char *const *cands, int ncand, int top, CommandWords *out) {
    int start, room, i;

    out->n = 0;
    out->top = 0;
    out->rows = 0;
    for (i = 0; i < CP_WORD_CELLS; i++) out->word[i] = 0;
    if (cands == 0 || ncand <= 0) return;

    if (top < 0) top = 0;
    if (top > cp_top_max(ncand)) top = cp_top_max(ncand);

    start = top * CP_WORD_COLS;
    room = ncand - start;
    if (room > CP_WORD_CELLS) room = CP_WORD_CELLS;
    for (i = 0; i < room; i++) out->word[i] = cands[start + i];
    out->n = room;
    out->top = top;
    out->rows = cp_word_rows(ncand);
}

int cp_word_move(CommandPanel *p, int dx, int dy, int ncand) {
    int row = p->cursor / CP_WORD_COLS;
    int col = p->cursor % CP_WORD_COLS;
    int vis;

    if (p->top < 0) p->top = 0;
    if (p->top > cp_top_max(ncand)) p->top = cp_top_max(ncand);

    if (dx != 0) {
        int nc = col + dx;
        if (nc < 0) return -1;
        if (nc >= CP_WORD_COLS) return 1;
        /* An empty cell to the right is the module's edge as far as the cursor
           is concerned -- a short last row must not trap it. */
        if ((p->top + row) * CP_WORD_COLS + nc >= ncand) return 1;
        p->cursor = row * CP_WORD_COLS + nc;
        return 0;
    }

    if (dy != 0) {
        int nr = row + dy;
        vis = cp_word_rows(ncand) - p->top;
        if (vis > CP_WORD_ROWS) vis = CP_WORD_ROWS;
        if (nr < 0) {
            if (p->top > 0) p->top--;
        } else if (nr >= vis) {
            if (p->top < cp_top_max(ncand)) p->top++;
        } else {
            p->cursor = nr * CP_WORD_COLS + col;
        }
        cp_clamp_cursor(p, ncand);
    }
    return 0;
}

void cp_word_enter(CommandPanel *p, int row, int from_right, int ncand) {
    int vis;
    p->box = CP_BOX_WORD;
    if (p->top < 0) p->top = 0;
    if (p->top > cp_top_max(ncand)) p->top = cp_top_max(ncand);
    vis = cp_word_rows(ncand) - p->top;
    if (vis > CP_WORD_ROWS) vis = CP_WORD_ROWS;
    if (row < 0) row = 0;
    if (vis > 0 && row >= vis) row = vis - 1;
    p->cursor = row * CP_WORD_COLS + (from_right ? CP_WORD_COLS - 1 : 0);
    cp_clamp_cursor(p, ncand);
}
