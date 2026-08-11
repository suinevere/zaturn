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
 |   module at the verb slot, page zero.
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
    p->page = 0;
    p->line[0] = '\0';
    p->line_len = 0;
    p->submitted = 0;
}

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
void cp_focus(CommandPanel *p, int dir) {
    int b = p->box + dir;
    if (b < 0) b = 0;
    if (b >= CP_BOX_N) b = CP_BOX_N - 1;
    if (b != p->box) { p->box = b; p->cursor = 0; p->page = 0; }
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
 |   Marks `submitted` when the command is complete.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; word -- the word picked; wants_prep -- 1 when the
 |   story's grammar says this verb takes a preposition
 | Returns: N/A
 ----------------------*/
void cp_pick(CommandPanel *p, const char *word, int wants_prep) {
    int i = 0;
    if (word == 0 || word[0] == '\0') return;
    if (p->line_len > 0 && p->line_len < CP_LINE_MAX - 1) p->line[p->line_len++] = ' ';
    while (word[i] && p->line_len < CP_LINE_MAX - 1) p->line[p->line_len++] = word[i++];
    p->line[p->line_len] = '\0';

    if (p->box == CP_BOX_TRAVEL) { p->slot = CP_SLOT_DONE; p->submitted = 1; return; }

    switch (p->slot) {
        case CP_SLOT_VERB:  p->slot = CP_SLOT_NOUN; break;
        case CP_SLOT_NOUN:  p->slot = wants_prep ? CP_SLOT_PREP : CP_SLOT_DONE; break;
        case CP_SLOT_PREP:  p->slot = CP_SLOT_NOUN2; break;
        case CP_SLOT_NOUN2: p->slot = CP_SLOT_DONE; break;
        default: break;
    }
    p->cursor = 0;
    p->page = 0;
    if (p->slot == CP_SLOT_DONE) p->submitted = 1;
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
    p->page = 0;
    p->submitted = 0;
}
