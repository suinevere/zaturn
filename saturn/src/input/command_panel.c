/*----------------------
 | command_panel.c
 | Description: The panel state machine described in command_panel.h.
 | Author: suinevere
 | Dependencies: command_panel.h
 ----------------------*/
#include "command_panel.h"

/*----------------------
 | cp_reset
 | Description: Clears the assembled command and returns focus to the word
 |   module at the verb slot, scrolled to the top of the list.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state to clear
 | Returns: N/A
 ----------------------*/
void cp_reset(CommandPanel *p) {
    p->box = CP_BOX_WORD;
    p->slot = CP_SLOT_VERB;
    p->cursor = 0;
    p->top = 0;
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

    switch (p->slot) {
        case CP_SLOT_VERB:  p->slot = CP_SLOT_NOUN; break;
        case CP_SLOT_NOUN:  p->slot = wants_prep ? CP_SLOT_PREP : CP_SLOT_DONE; break;
        case CP_SLOT_PREP:  p->slot = CP_SLOT_NOUN2; break;
        case CP_SLOT_NOUN2: p->slot = CP_SLOT_DONE; break;
        default: break;
    }
    p->cursor = 0;
    p->top = 0;
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
    if (p->slot > CP_SLOT_VERB) p->slot--;
    if (p->slot > CP_SLOT_NOUN && p->line_len == 0) p->slot = CP_SLOT_VERB;
    p->cursor = 0;
    p->top = 0;
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
