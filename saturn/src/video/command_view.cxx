/*----------------------
 | command_view.cxx
 | Description: Implements the command panel's rendering and its pad-driven
 |   editor. render_command_panel draws the input line and the three-module
 |   strip (compass rose, word page, fixed commands), highlighting the focused
 |   module's selection and border hint in reverse video. command_edit reads the
 |   pad, drives the CommandPanel state machine, sources and orders each word
 |   slot's candidates, and on a completed sentence hands it to the same
 |   KeyboardState the on-screen keyboard fills.
 | Author: suinevere
 | Dependencies: command_view.h, command_rose.h, text_map.h, console_view.h,
 |   app_state.h, input.h, SRL
 ----------------------*/

#include <srl.hpp>
#include "command_view.h"
#include "command_rose.h"
#include "text_map.h"
#include "console_view.h"
#include "app_state.h"
#include "input.h"

/*----------------------
 | CV_TOP_MARGIN
 | Description: The one blank row reserved at the top of the text map for TV
 |   overscan, mirrored from console_view.cxx's private constant of the same
 |   value so the panel lands directly below the console area without exposing
 |   a new cross-file dependency.
 | Author: suinevere
 ----------------------*/
static const int CV_TOP_MARGIN = 1;

/*----------------------
 | CV_BORDER_TOP / CV_BORDER_BLANK
 | Description: The strip's top border and its blank content rows, verbatim
 |   from the design -- both exactly 40 columns with the dividers already in
 |   place at columns 0, 14, 30 and 39.
 | Author: suinevere
 ----------------------*/
static const char *CV_BORDER_TOP   = "+-------------+---------------+--------+";
static const char *CV_BORDER_BLANK = "|             |               |        |";

/*----------------------
 | CV_HINT_TRAVEL
 | Description: The travel module's bottom-border hint, exactly its inner
 |   width (13) so it slots straight into the border between the corner '+'
 |   marks. L and R are fixed bindings for module focus (command_edit reads
 |   Button::L/Button::R directly, never through face_button/g_face_btn), so
 |   unlike the other two hints this one is never stale and stays a literal.
 | Author: suinevere
 ----------------------*/
static const char *CV_HINT_TRAVEL = "---L/R box---";

/*----------------------
 | CV_CMD_ROW
 | Description: The fixed command module's entries, in display order. Every one
 |   routes to a mechanism that already exists, so none of them needs a new path
 |   to the interpreter.
 | Author: suinevere
 ----------------------*/
static const char *CV_CMD_ROW[5] = { "invent", "look", "save", "load", "quit" };

/*----------------------
 | CV_CMD_WORD
 | Description: The command each CV_CMD_ROW entry actually submits. Only
 |   "invent" differs from its display text -- until Task 9's overlay exists it
 |   submits "inventory" like a typed command, per the brief.
 | Author: suinevere
 ----------------------*/
static const char *CV_CMD_WORD[5] = { "inventory", "look", "save", "load", "quit" };

/*----------------------
 | CV_VERB_CORE
 | Description: The curated verbs offered before the story's own, in likelihood
 |   order. Each is dropped unless the loaded story's dictionary accepts it, so a
 |   game that does not define "attack" never offers it.
 | Author: suinevere
 ----------------------*/
static const char *CV_VERB_CORE[16] = {
    "look", "take", "open", "read", "drop", "close", "push", "pull",
    "move", "attack", "climb", "enter", "throw", "turn", "eat", "drink"
};

/*----------------------
 | CV_CAND_MAX
 | Description: The cap on how many candidate words a slot's sourcing pass
 |   collects before ordering and paging, matching typeahead.c's own CAND_MAX --
 |   the panel does not need to rank more than the trie's own ranking pass does.
 | Author: suinevere
 ----------------------*/
#define CV_CAND_MAX 32

// ---- string helpers (hand-rolled, matching game_catalog.cxx's precedent so
// this file needs no <string.h>) --------------------------------------------

/*----------------------
 | cv_str_eq
 | Description: NUL-terminated string equality.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a, b -- the strings
 | Returns: 1 if equal, 0 otherwise
 ----------------------*/
static int cv_str_eq(const char *a, const char *b) {
    int i;
    for (i = 0; a[i] || b[i]; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/*----------------------
 | cv_str_gt
 | Description: Lexicographic greater-than, for the Hard alphabetical sort.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a, b -- the strings
 | Returns: nonzero if a sorts after b
 ----------------------*/
static int cv_str_gt(const char *a, const char *b) {
    int i;
    for (i = 0; ; i++) {
        char ca = a[i], cb = b[i];
        if (ca != cb) return ca > cb;
        if (ca == '\0') return 0;
    }
}

/*----------------------
 | cv_add_cand
 | Description: Appends `word` to `out` unless it is already present or the
 |   list is full.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out -- candidate list; n -- current count; word -- text to add
 | Returns: the new count
 ----------------------*/
static int cv_add_cand(const char **out, int n, const char *word) {
    int i;
    if (n >= CV_CAND_MAX || word == 0 || word[0] == '\0') return n;
    for (i = 0; i < n; i++) if (cv_str_eq(out[i], word)) return n;
    out[n++] = word;
    return n;
}

// ---- candidate sourcing -----------------------------------------------------

/*----------------------
 | cv_collect_type
 | Description: Walks the trie collecting every distinct word of `type`, along
 |   with its base weight, so a fallback tier can be ranked without a
 |   prefix-driven predict_candidates call. Recursion depth is bounded by the
 |   trie's shape (branching factor 26, word length under 10), not by
 |   vocabulary size.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: node -- trie node to walk; type -- word type to collect; out, wt --
 |   parallel output arrays; n -- count so far
 | Returns: the new count
 ----------------------*/
static int cv_collect_type(TrieNode *node, WordType type,
                           const char **out, int *wt, int n) {
    if (node == 0 || n >= CV_CAND_MAX) return n;
    if (node->word_data != 0 && node->word_data->type == type) {
        int dup = 0, i;
        for (i = 0; i < n; i++) if (cv_str_eq(out[i], node->word_data->text)) { dup = 1; break; }
        if (!dup) { out[n] = node->word_data->text; wt[n] = node->word_data->base_weight; n++; }
    }
    n = cv_collect_type(node->first_child, type, out, wt, n);
    n = cv_collect_type(node->next_sibling, type, out, wt, n);
    return n;
}

/*----------------------
 | cv_sort_weight
 | Description: Selection-sorts `n` candidates by descending weight in place.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out, wt -- parallel arrays; n -- count
 | Returns: N/A
 ----------------------*/
static void cv_sort_weight(const char **out, int *wt, int n) {
    int i, j, best;
    for (i = 0; i < n; i++) {
        best = i;
        for (j = i + 1; j < n; j++) if (wt[j] > wt[best]) best = j;
        if (best != i) {
            const char *tw = out[i]; out[i] = out[best]; out[best] = tw;
            int ti = wt[i]; wt[i] = wt[best]; wt[best] = ti;
        }
    }
}

/*----------------------
 | cv_build_verb_cands
 | Description: Sources the verb slot: the curated core filtered against the
 |   story (room_model_has_word when the model is available, else the trie),
 |   then the story's remaining verbs, trie-weight ranked. Reports how many
 |   leading entries are core, so cv_reorder can keep them in place at every
 |   difficulty.
 | Author: suinevere
 | Dependencies: room_model.h, typeahead.h
 | Globals: N/A
 | Params: root -- typeahead trie, may be null; out -- receives candidates;
 |   core_n -- receives the count of leading core entries
 | Returns: candidate count
 ----------------------*/
static int cv_build_verb_cands(TrieNode *root, const char **out, int *core_n) {
    int n = 0, i;
    int have_model = room_model_available();
    const char *rest[CV_CAND_MAX];
    int restwt[CV_CAND_MAX];
    int nrest;

    for (i = 0; i < 16; i++) {
        const char *v = CV_VERB_CORE[i];
        int ok = have_model ? room_model_has_word(v)
                             : (root != 0 ? (find_exact_word(root, v) != 0) : 1);
        if (ok) n = cv_add_cand(out, n, v);
    }
    *core_n = n;
    if (root != 0) {
        nrest = cv_collect_type(root, TYPE_VERB, rest, restwt, 0);
        cv_sort_weight(rest, restwt, nrest);
        for (i = 0; i < nrest && n < CV_CAND_MAX; i++) n = cv_add_cand(out, n, rest[i]);
    }
    return n;
}

/*----------------------
 | CV_DICT_FL_NOUN
 | Description: The dictionary flag-byte bit marking a v3 entry as a parser
 |   noun -- the filter the Hard-difficulty dictionary fallback applies, since
 |   no trie ranking survives to distinguish word types there.
 | Author: suinevere
 ----------------------*/
#define CV_DICT_FL_NOUN 0x80

/*----------------------
 | g_cv_dict_scratch
 | Description: Owned storage for the noun candidates cv_build_dict_nouns reads
 |   straight out of the story's dictionary -- room_model_dict_word writes into
 |   caller-owned bytes, and cv_add_cand only ever keeps a pointer, so something
 |   has to hold the text for the rest of the refill. Overwritten every refill,
 |   same lifetime rule as g_cv_word_scratch.
 | Author: suinevere
 ----------------------*/
static char g_cv_dict_scratch[CV_CAND_MAX][8];

/*----------------------
 | cv_present_word
 | Description: Whether `text` names an object the room model currently reports
 |   present, compared against each present object's own parser word.
 | Author: suinevere
 | Dependencies: room_model.h
 | Globals: N/A
 | Params: text -- the candidate word; m -- the room snapshot to check against
 | Returns: 1 if some present object's word matches, 0 otherwise
 ----------------------*/
static int cv_present_word(const char *text, const RoomModel *m) {
    int i;
    char w[8];
    for (i = 0; i < m->nhere; i++)
        if (room_model_object_word(m->here[i], w, (int) sizeof w) && cv_str_eq(w, text)) return 1;
    return 0;
}

/*----------------------
 | cv_build_dict_nouns
 | Description: Sources the noun slot straight from the story's own dictionary
 |   -- the vocabulary source on Hard, where no typeahead trie is built and the
 |   trie-based sourcing above never yields anything. Walks the dictionary
 |   twice: entries naming a present object first, then the rest of the noun
 |   entries, so a word for something actually in the room leads the page.
 | Author: suinevere
 | Dependencies: room_model.h
 | Globals: g_cv_dict_scratch
 | Params: out -- receives candidates; n -- current count
 | Returns: the new count
 ----------------------*/
static int cv_build_dict_nouns(const char **out, int n) {
    int cnt = room_model_dict_count();
    const RoomModel *m = room_model_get();
    int pass, i;
    for (pass = 0; pass < 2 && n < CV_CAND_MAX; pass++) {
        for (i = 0; i < cnt && n < CV_CAND_MAX; i++) {
            char w[8];
            unsigned char fl;
            int here;
            if (!room_model_dict_word(i, w, (int) sizeof w, &fl)) continue;
            if ((fl & CV_DICT_FL_NOUN) == 0) continue;
            here = cv_present_word(w, m);
            if ((pass == 0) != (here != 0)) continue;
            {
                int j;
                for (j = 0; j < 7 && w[j]; j++) g_cv_dict_scratch[n][j] = w[j];
                g_cv_dict_scratch[n][j] = '\0';
                n = cv_add_cand(out, n, g_cv_dict_scratch[n]);
            }
        }
    }
    return n;
}

/*----------------------
 | cv_build_noun_cands
 | Description: Sources a noun slot: context-linked and on-screen nouns for
 |   `prev` (the verb for CP_SLOT_NOUN, the preposition for CP_SLOT_NOUN2) via
 |   predict_candidates' own on-screen boost, then the story's remaining nouns,
 |   trie-weight ranked. Falls back to the dictionary enumerator when that
 |   yields nothing -- root null or (on Hard) present but wordless either mean
 |   there is no trie to source from.
 | Author: suinevere
 | Dependencies: typeahead.h, room_model.h
 | Globals: N/A
 | Params: root -- typeahead trie, may be null; prev -- the preceding word, may
 |   be null; out -- receives candidates
 | Returns: candidate count
 ----------------------*/
static int cv_build_noun_cands(TrieNode *root, DictionaryWord *prev, const char **out) {
    int n = 0, i;
    if (root != 0) {
        DictionaryWord *hot[CV_CAND_MAX];
        int nh = predict_candidates(root, prev, "", hot, CV_CAND_MAX, 0);
        const char *rest[CV_CAND_MAX];
        int restwt[CV_CAND_MAX];
        int nrest;
        for (i = 0; i < nh; i++) n = cv_add_cand(out, n, hot[i]->text);
        nrest = cv_collect_type(root, TYPE_NOUN, rest, restwt, 0);
        cv_sort_weight(rest, restwt, nrest);
        for (i = 0; i < nrest && n < CV_CAND_MAX; i++) n = cv_add_cand(out, n, rest[i]);
    }
    if (n == 0) n = cv_build_dict_nouns(out, n);
    return n;
}

/*----------------------
 | cv_build_prep_cands
 | Description: Sources the preposition slot: every trie word of type
 |   TYPE_PREP, trie-weight ranked.
 | Author: suinevere
 | Dependencies: typeahead.h
 | Globals: N/A
 | Params: root -- typeahead trie, may be null; out -- receives candidates
 | Returns: candidate count
 ----------------------*/
static int cv_build_prep_cands(TrieNode *root, const char **out) {
    const char *rest[CV_CAND_MAX];
    int restwt[CV_CAND_MAX];
    int nrest, n = 0, i;
    if (root == 0) return 0;
    nrest = cv_collect_type(root, TYPE_PREP, rest, restwt, 0);
    cv_sort_weight(rest, restwt, nrest);
    for (i = 0; i < nrest; i++) n = cv_add_cand(out, n, rest[i]);
    return n;
}

/*----------------------
 | cv_last_word
 | Description: Looks up the trie word for the last space-separated token of
 |   the panel's sentence so far -- the verb while the noun slot is being
 |   filled, the preposition while the second noun slot is.
 | Author: suinevere
 | Dependencies: typeahead.h, command_panel.h
 | Globals: N/A
 | Params: p -- panel state; root -- typeahead trie, may be null
 | Returns: the trie word, or null if there is none or root is null
 ----------------------*/
static DictionaryWord *cv_last_word(const CommandPanel &p, TrieNode *root) {
    char buf[CP_WORD_MAX + 1];
    int i, start = 0, n;
    if (root == 0 || p.line_len == 0) return 0;
    for (i = p.line_len - 1; i >= 0; i--) if (p.line[i] == ' ') { start = i + 1; break; }
    n = p.line_len - start;
    if (n > CP_WORD_MAX) n = CP_WORD_MAX;
    for (i = 0; i < n; i++) buf[i] = p.line[start + i];
    buf[n] = '\0';
    return find_exact_word(root, buf);
}

/*----------------------
 | cv_has_solution_link
 | Description: Whether `prev` has a winning-path (solution-overlay) transition
 |   to a word spelled `text`.
 | Author: suinevere
 | Dependencies: typeahead.h
 | Globals: N/A
 | Params: prev -- the preceding trie word, may be null; text -- the candidate
 | Returns: 1 if a solution link exists, 0 otherwise
 ----------------------*/
static int cv_has_solution_link(DictionaryWord *prev, const char *text) {
    NextWordLink *l;
    if (prev == 0) return 0;
    for (l = prev->next_words; l != 0; l = l->next)
        if (l->solution != 0 && cv_str_eq(l->target_word->text, text)) return 1;
    return 0;
}

/*----------------------
 | cv_reorder
 | Description: Reorders a sourced candidate list to match g_difficulty --
 |   solution-overlay links first on Easy, unchanged (trie weight, already
 |   folded into the sourcing order) on Medium, flat alphabetical on Hard. The
 |   leading `protect` entries -- the verb slot's curated core, in its declared
 |   order -- are never moved by either branch, so the core leads at every
 |   difficulty and only what ranks below it changes. Membership is whatever
 |   cv_build_*_cands sourced; only the order changes.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_difficulty
 | Params: cand -- candidate list, reordered in place; n -- count; prev -- the
 |   preceding trie word for the Easy solution-link check, may be null;
 |   protect -- leading entry count to leave untouched
 | Returns: N/A
 ----------------------*/
static void cv_reorder(const char **cand, int n, DictionaryWord *prev, int protect) {
    int i;
    if (protect < 0) protect = 0;
    if (protect > n) protect = n;
    if (g_difficulty == DIFF_HARD) {
        for (i = protect + 1; i < n; i++) {
            const char *key = cand[i];
            int j = i - 1;
            while (j >= protect && cv_str_gt(cand[j], key)) { cand[j + 1] = cand[j]; j--; }
            cand[j + 1] = key;
        }
    } else if (g_difficulty == DIFF_EASY) {
        const char *tmp[CV_CAND_MAX];
        int w = protect;
        for (i = protect; i < n; i++) tmp[i] = cand[i];
        for (i = protect; i < n; i++) if (cv_has_solution_link(prev, tmp[i])) cand[w++] = tmp[i];
        for (i = protect; i < n; i++) if (!cv_has_solution_link(prev, tmp[i])) cand[w++] = tmp[i];
    }
}

/*----------------------
 | g_cv_word_scratch
 | Description: Owned storage for the truncated candidate copies cv_refill_words
 |   hands to cp_fill -- CommandWords only ever holds pointers, so something
 |   has to own the six-character-or-less text they point at. Overwritten on
 |   every refill; safe because cp_pick copies the characters out of a picked
 |   cell immediately rather than keeping the pointer.
 | Author: suinevere
 ----------------------*/
static char g_cv_word_scratch[CV_CAND_MAX][CP_WORD_MAX];

/*----------------------
 | cv_truncate_all
 | Description: Rewrites `cand`'s first `n` entries in place to point at
 |   six-character-or-less copies in g_cv_word_scratch -- the truncation
 |   CommandWords must carry, since a v3 dictionary entry (and the parser
 |   reading it) only ever distinguishes six characters.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cv_word_scratch
 | Params: cand -- candidate list, rewritten in place; n -- count
 | Returns: N/A
 ----------------------*/
static void cv_truncate_all(const char **cand, int n) {
    int i, j;
    if (n > CV_CAND_MAX) n = CV_CAND_MAX;
    for (i = 0; i < n; i++) {
        const char *src = cand[i];
        for (j = 0; j < CP_WORD_MAX - 1 && src[j] != '\0'; j++) g_cv_word_scratch[i][j] = src[j];
        g_cv_word_scratch[i][j] = '\0';
        cand[i] = g_cv_word_scratch[i];
    }
}

/*----------------------
 | cv_refill_words
 | Description: Sources and orders the current slot's candidates, truncates
 |   each to six characters, and fills the page the renderer should draw.
 | Author: suinevere
 | Dependencies: command_panel.h, typeahead.h
 | Globals: N/A
 | Params: p -- panel state; root -- typeahead trie, may be null; w -- receives
 |   the page
 | Returns: N/A
 ----------------------*/
static void cv_refill_words(const CommandPanel &p, TrieNode *root, CommandWords &w) {
    const char *cand[CV_CAND_MAX];
    int ncand = 0;
    int core_n = 0;
    DictionaryWord *prev = 0;

    if (p.slot == CP_SLOT_VERB) {
        ncand = cv_build_verb_cands(root, cand, &core_n);
    } else if (p.slot == CP_SLOT_NOUN || p.slot == CP_SLOT_NOUN2) {
        prev = cv_last_word(p, root);
        ncand = cv_build_noun_cands(root, prev, cand);
    } else if (p.slot == CP_SLOT_PREP) {
        ncand = cv_build_prep_cands(root, cand);
    }
    cv_reorder(cand, ncand, prev, core_n);
    cv_truncate_all(cand, ncand);
    cp_fill(cand, ncand, p.page, &w);
}

// ---- rendering ---------------------------------------------------------------

/*----------------------
 | cv_flatten_hard
 | Description: Builds the Hard-difficulty view of a room's exits: every
 |   decoded-open direction demoted to maybe, so cr_row lowercases it along with
 |   the genuinely conditional ones. Blocked and absent directions are left
 |   alone -- there is still no exit there to reveal.
 | Author: suinevere
 | Dependencies: room_model.h
 | Globals: N/A
 | Params: exits -- the decoded exits; out -- receives the flattened copy
 | Returns: N/A
 ----------------------*/
static void cv_flatten_hard(const unsigned char *exits, unsigned char *out) {
    int i;
    for (i = 0; i < RM_DIR_N; i++)
        out[i] = (exits[i] == RM_EXIT_OPEN) ? RM_EXIT_MAYBE : exits[i];
}

/*----------------------
 | cv_draw_rose_row
 | Description: Composes and prints one rose row, then overprints the up/down
 |   markers (row 0's carets, row 4's v's) in reverse video -- the only
 |   inversion the travel module ever carries, since the D-pad is literal there.
 | Author: suinevere
 | Dependencies: command_rose.h, text_map.h
 | Globals: N/A
 | Params: row -- 0..CR_ROWS-1; exits -- the exits to draw; y -- text row
 | Returns: N/A
 ----------------------*/
static void cv_draw_rose_row(int row, const unsigned char *exits, int y) {
    char buf[CR_COLS + 1];
    cr_row(exits, row, buf);
    text_print(CV_TRAVEL_X, y, buf);
    if (row == 0) {
        if (buf[CR_UP_L] == '^') text_print_hl(CV_TRAVEL_X + CR_UP_L, y, "^");
        if (buf[CR_UP_R] == '^') text_print_hl(CV_TRAVEL_X + CR_UP_R, y, "^");
    } else if (row == CR_ROWS - 1) {
        if (buf[CR_UP_L] == 'v') text_print_hl(CV_TRAVEL_X + CR_UP_L, y, "v");
        if (buf[CR_UP_R] == 'v') text_print_hl(CV_TRAVEL_X + CR_UP_R, y, "v");
    }
}

/*----------------------
 | cv_pad_field
 | Description: Copies up to six characters of `text` into `field`, blank-
 |   padding to a fixed seven-column width so a shorter word erases whatever the
 |   previous frame drew in the same cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: text -- the word, may be null; field -- receives 8 bytes (7 + NUL)
 | Returns: N/A
 ----------------------*/
static void cv_pad_field(const char *text, char *field) {
    int i = 0;
    if (text != 0) for (; text[i] != '\0' && i < 6; i++) field[i] = text[i];
    for (; i < 7; i++) field[i] = ' ';
    field[7] = '\0';
}

/*----------------------
 | cv_draw_word_row
 | Description: Draws one word-module content row: two seven-column fields
 |   (one-column left margin already accounted for by CV_WORD_X), the last cell
 |   of the grid showing "v more" instead of a word when a further page exists.
 |   The focused module's selected cell prints in reverse video.
 | Author: suinevere
 | Dependencies: command_panel.h, text_map.h
 | Globals: N/A
 | Params: row -- 0..CR_ROWS-1; p -- panel state; w -- the word page; y -- text
 |   row
 | Returns: N/A
 ----------------------*/
static void cv_draw_word_row(int row, const CommandPanel &p, const CommandWords &w, int y) {
    int col;
    for (col = 0; col < CP_WORD_COLS; col++) {
        int idx = row * CP_WORD_COLS + col;
        int x = CV_WORD_X + 1 + col * 7;
        char field[8];
        const char *text = 0;
        int has = 0;
        if (idx == CP_WORD_CELLS - 1 && w.more) { text = "v more"; has = 1; }
        else if (idx < w.n && w.word[idx] != 0) { text = w.word[idx]; has = 1; }
        cv_pad_field(text, field);
        if (has && p.box == CP_BOX_WORD && p.cursor == idx) text_print_hl(x, y, field);
        else                                                text_print(x, y, field);
    }
}

/*----------------------
 | cv_draw_cmd_row
 | Description: Draws one command-module content row -- CV_CMD_ROW[row] in a
 |   seven-column field, in reverse video when the command module holds focus
 |   and its cursor sits on this row.
 | Author: suinevere
 | Dependencies: command_panel.h, text_map.h
 | Globals: N/A
 | Params: row -- 0..CR_ROWS-1; p -- panel state; y -- text row
 | Returns: N/A
 ----------------------*/
static void cv_draw_cmd_row(int row, const CommandPanel &p, int y) {
    int x = CV_CMD_X + 1;
    char field[8];
    cv_pad_field(CV_CMD_ROW[row], field);
    if (p.box == CP_BOX_CMD && p.cursor == row) text_print_hl(x, y, field);
    else                                        text_print(x, y, field);
}

/*----------------------
 | cv_word_hint
 | Description: Builds the word module's bottom-border hint from the live
 |   Accept/Back face-button mapping, so the letters shown always match the
 |   buttons that actually pick and back up -- command_edit fires both through
 |   face_button(FA_ACCEPT)/face_button(FA_BACK), which the player can remap on
 |   the Controls page. Fixed at the module's 15-character inner width:
 |   face_btn_name always returns a single character ("A", "B", or "C"), so
 |   substituting it in place of the old "A"/"B" literals never changes the
 |   count.
 | Author: suinevere
 | Dependencies: input.h (face_btn_name)
 | Globals: g_face_btn
 | Params: out -- receives the 15-character hint plus a NUL (16 bytes)
 | Returns: N/A
 ----------------------*/
static void cv_word_hint(char *out) {
    const char *accept = face_btn_name(FA_ACCEPT);
    const char *back   = face_btn_name(FA_BACK);
    int i = 0;
    out[i++] = '-';
    out[i++] = accept[0];
    out[i++] = '=';
    out[i++] = 'p'; out[i++] = 'i'; out[i++] = 'c'; out[i++] = 'k';
    out[i++] = ' ';
    out[i++] = back[0];
    out[i++] = '=';
    out[i++] = 'b'; out[i++] = 'c'; out[i++] = 'k';
    out[i++] = '-'; out[i++] = '-';
    out[i] = '\0';
}

/*----------------------
 | cv_cmd_hint
 | Description: Builds the command module's bottom-border hint from the live
 |   toggle-button binding, so the letter shown always matches the shift
 |   button that actually swaps the panel back to the on-screen keyboard.
 |   Fixed at the module's 8-character inner width.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_toggle_btn
 | Params: out -- receives the 8-character hint plus a NUL (9 bytes)
 | Returns: N/A
 ----------------------*/
static void cv_cmd_hint(char *out) {
    int i = 0;
    out[i++] = '-';
    out[i++] = (g_toggle_btn == 1) ? 'Y' : 'Z';
    out[i++] = '=';
    out[i++] = 'k'; out[i++] = 'b'; out[i++] = 'd';
    out[i++] = '-'; out[i++] = '-';
    out[i] = '\0';
}

/*----------------------
 | cv_draw_bottom_border
 | Description: Draws the bottom border's three corner-to-corner segments,
 |   printing the focused module's hint in reverse video. The word and command
 |   hints are rebuilt every call from the live face-button/toggle-button
 |   bindings (cv_word_hint/cv_cmd_hint); the travel hint is a fixed literal,
 |   since L/R module focus is not remappable.
 | Author: suinevere
 | Dependencies: command_panel.h, text_map.h, input.h, app_state.h
 | Globals: g_face_btn, g_toggle_btn
 | Params: focus -- p.box; y -- text row
 | Returns: N/A
 ----------------------*/
static void cv_draw_bottom_border(int focus, int y) {
    char word_hint[16];
    char cmd_hint[9];
    cv_word_hint(word_hint);
    cv_cmd_hint(cmd_hint);
    text_print(0, y, "+");
    if (focus == CP_BOX_TRAVEL) text_print_hl(CV_TRAVEL_X, y, CV_HINT_TRAVEL);
    else                        text_print(CV_TRAVEL_X, y, CV_HINT_TRAVEL);
    text_print(14, y, "+");
    if (focus == CP_BOX_WORD) text_print_hl(CV_WORD_X, y, word_hint);
    else                      text_print(CV_WORD_X, y, word_hint);
    text_print(30, y, "+");
    if (focus == CP_BOX_CMD) text_print_hl(CV_CMD_X, y, cmd_hint);
    else                     text_print(CV_CMD_X, y, cmd_hint);
    text_print(39, y, "+");
}

/*----------------------
 | CV_OVERLAY_X / CV_OVERLAY_W
 | Description: The inventory overlay's left column and total width: 34
 |   columns starting at column 2, drawn over the strip's seven interior rows
 |   (the blank rows flanking the content plus the five content rows) so the
 |   outer border and the 40-column geometry around it are undisturbed.
 | Author: suinevere
 ----------------------*/
#define CV_OVERLAY_X 2
#define CV_OVERLAY_W 34

/*----------------------
 | cv_overlay_border
 | Description: Builds one horizontal border row of the overlay box: '+',
 |   CV_OVERLAY_W - 2 dashes, '+'.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out -- receives CV_OVERLAY_W characters plus a NUL
 | Returns: N/A
 ----------------------*/
static void cv_overlay_border(char *out) {
    int i;
    out[0] = '+';
    for (i = 1; i < CV_OVERLAY_W - 1; i++) out[i] = '-';
    out[CV_OVERLAY_W - 1] = '+';
    out[CV_OVERLAY_W] = '\0';
}

/*----------------------
 | cv_overlay_row_text
 | Description: Builds one overlay content row: '|', the carried item at
 |   `idx`'s parser word -- recovered to its full spelling from the object's
 |   own short name where a longer one exists -- filling the word field at
 |   the overlay's own width (CV_OVERLAY_W minus the border and indent), not
 |   the strip's six-character column, since the overlay has room to spare.
 |   Blank once idx runs past the carried count, padded to the box's inner
 |   width, '|'. A recovered spelling longer than the field still truncates
 |   cleanly.
 | Author: suinevere
 | Dependencies: room_model.h
 | Globals: N/A
 | Params: m -- the room snapshot; idx -- carried index, may be out of range;
 |   out -- receives CV_OVERLAY_W characters plus a NUL
 | Returns: N/A
 ----------------------*/
static void cv_overlay_row_text(const RoomModel &m, int idx, char *out) {
    char word[8] = {0};
    char full[16] = {0};
    int i, wl, field_w;
    if (idx >= 0 && idx < m.ncarried && room_model_object_word(m.carried[idx], word, sizeof word))
        room_model_full_word(m.carried[idx], word, full, sizeof full);
    wl = 0;
    while (wl < (int) sizeof(full) - 1 && full[wl] != '\0') wl++;
    field_w = CV_OVERLAY_W - 3;
    out[0] = '|';
    out[1] = ' ';
    for (i = 0; i < field_w; i++) out[2 + i] = (i < wl) ? full[i] : ' ';
    out[CV_OVERLAY_W - 1] = '|';
    out[CV_OVERLAY_W] = '\0';
}

/*----------------------
 | cv_draw_overlay
 | Description: Draws the inventory overlay across the strip's seven interior
 |   rows: a top border, five carried-item rows scrolled in blocks of CR_ROWS
 |   around the cursor, and a bottom border, with the selected row in reverse
 |   video.
 | Author: suinevere
 | Dependencies: room_model.h, text_map.h, command_panel.h
 | Globals: N/A
 | Params: p -- panel state; m -- the room snapshot; top_y -- the row the
 |   overlay's top border is drawn on (the strip's first blank row)
 | Returns: N/A
 ----------------------*/
static void cv_draw_overlay(const CommandPanel &p, const RoomModel &m, int top_y) {
    char border[CV_OVERLAY_W + 1];
    char row_text[CV_OVERLAY_W + 1];
    int window, i;

    cv_overlay_border(border);
    text_print(CV_OVERLAY_X, top_y, border);

    window = (p.cursor / CR_ROWS) * CR_ROWS;
    for (i = 0; i < CR_ROWS; i++) {
        int y = top_y + 1 + i;
        int idx = window + i;
        cv_overlay_row_text(m, idx, row_text);
        if (idx == p.cursor) text_print_hl(CV_OVERLAY_X, y, row_text);
        else                 text_print(CV_OVERLAY_X, y, row_text);
    }

    text_print(CV_OVERLAY_X, top_y + 1 + CR_ROWS, border);
}

/*----------------------
 | render_command_panel
 | Description: Draws the input line, the strip's borders and dividers, and
 |   either the inventory overlay or the compass rose/word page/command list,
 |   highlighting the focused module's selected entry and its border hint in
 |   reverse video.
 | Author: suinevere
 | Dependencies: command_rose.h, text_map.h, console_view.h
 | Globals: g_difficulty
 | Params: p -- panel state; m -- the room snapshot; w -- the current word page
 | Returns: N/A
 ----------------------*/
void render_command_panel(const CommandPanel &p, const RoomModel &m, const CommandWords &w) {
    int base = CV_TOP_MARGIN + console_height();
    int input_row = base;
    int border_top = input_row + 1;
    int blank1 = border_top + 1;
    int content0 = blank1 + 1;
    int blank2 = content0 + CR_ROWS;
    int border_bottom = blank2 + 1;
    int row;

    text_clear_line(input_row);
    text_print(0, input_row, "> %s", p.line);

    text_clear_line(border_top);
    text_print(0, border_top, CV_BORDER_TOP);

    if (p.overlay) {
        int y;
        for (y = blank1; y <= blank2; y++) text_clear_line(y);
        cv_draw_overlay(p, m, blank1);
    } else {
        unsigned char flat[RM_DIR_N];
        const unsigned char *exits;

        text_clear_line(blank1);
        text_print(0, blank1, CV_BORDER_BLANK);

        exits = m.exits;
        if (g_difficulty == DIFF_HARD) { cv_flatten_hard(m.exits, flat); exits = flat; }

        for (row = 0; row < CR_ROWS; row++) {
            int y = content0 + row;
            text_clear_line(y);
            text_print(0, y, "|");
            cv_draw_rose_row(row, exits, y);
            text_print(14, y, "|");
            cv_draw_word_row(row, p, w, y);
            text_print(30, y, "|");
            cv_draw_cmd_row(row, p, y);
            text_print(39, y, "|");
        }

        text_clear_line(blank2);
        text_print(0, blank2, CV_BORDER_BLANK);
    }

    text_clear_line(border_bottom);
    cv_draw_bottom_border(p.box, border_bottom);
}

// ---- pad-driven editing ------------------------------------------------------

/*----------------------
 | cv_travel_pick
 | Description: Reads the D-pad as the literal compass: a lone direction picks
 |   its cardinal, and two adjacent directions firing together (one edging this
 |   frame, the other already held) pick the diagonal between them.
 | Author: suinevere
 | Dependencies: input.h, room_model.h, command_panel.h
 | Globals: g_pad
 | Params: p -- panel state
 | Returns: N/A
 ----------------------*/
static void cv_travel_pick(CommandPanel &p) {
    bool f_up    = pad_fired(Button::Up);
    bool f_down  = pad_fired(Button::Down);
    bool f_left  = pad_fired(Button::Left);
    bool f_right = pad_fired(Button::Right);
    int dir = -1;

    if      (f_up    && g_pad->IsHeld(Button::Right)) dir = RM_NE;
    else if (f_right && g_pad->IsHeld(Button::Up))     dir = RM_NE;
    else if (f_up    && g_pad->IsHeld(Button::Left))   dir = RM_NW;
    else if (f_left  && g_pad->IsHeld(Button::Up))     dir = RM_NW;
    else if (f_down  && g_pad->IsHeld(Button::Right))  dir = RM_SE;
    else if (f_right && g_pad->IsHeld(Button::Down))   dir = RM_SE;
    else if (f_down  && g_pad->IsHeld(Button::Left))   dir = RM_SW;
    else if (f_left  && g_pad->IsHeld(Button::Down))   dir = RM_SW;
    else if (f_up)    dir = RM_N;
    else if (f_down)  dir = RM_S;
    else if (f_left)  dir = RM_W;
    else if (f_right) dir = RM_E;

    if (dir >= 0) cp_pick(&p, room_model_dir_word(dir), 0);
}

/*----------------------
 | cv_word_dpad
 | Description: Walks the word module's grid: Up/Down step by the column count
 |   (one row), Left/Right by one cell. The cursor's reachable range is
 |   w.n + (w.more ? 1 : 0) -- exactly the filled cells, plus one more when the
 |   "v more" marker occupies the next cell, so the cursor never lands on a
 |   blank cell but can still reach the marker to turn the page.
 | Author: suinevere
 | Dependencies: input.h, command_panel.h
 | Globals: N/A
 | Params: p -- panel state; w -- the word page currently drawn
 | Returns: N/A
 ----------------------*/
static void cv_word_dpad(CommandPanel &p, const CommandWords &w) {
    int count = w.n + (w.more ? 1 : 0);
    if (pad_fired(Button::Up))    cp_move(&p, -CP_WORD_COLS, count);
    if (pad_fired(Button::Down))  cp_move(&p,  CP_WORD_COLS, count);
    if (pad_fired(Button::Left))  cp_move(&p, -1, count);
    if (pad_fired(Button::Right)) cp_move(&p,  1, count);
}

/*----------------------
 | cv_cmd_dpad
 | Description: Walks the command module's single column of five entries: each
 |   direction steps the cursor by one.
 | Author: suinevere
 | Dependencies: input.h, command_panel.h
 | Globals: N/A
 | Params: p -- panel state
 | Returns: N/A
 ----------------------*/
static void cv_cmd_dpad(CommandPanel &p) {
    if (pad_fired(Button::Up))    cp_move(&p, -1, 5);
    if (pad_fired(Button::Down))  cp_move(&p,  1, 5);
    if (pad_fired(Button::Left))  cp_move(&p, -1, 5);
    if (pad_fired(Button::Right)) cp_move(&p,  1, 5);
}

/*----------------------
 | cv_overlay_dpad
 | Description: Walks the inventory overlay's carried-item list: each
 |   direction steps the cursor by one, clamped to the carried count.
 | Author: suinevere
 | Dependencies: input.h, command_panel.h
 | Globals: N/A
 | Params: p -- panel state; count -- carried item count
 | Returns: N/A
 ----------------------*/
static void cv_overlay_dpad(CommandPanel &p, int count) {
    if (pad_fired(Button::Up))    cp_move(&p, -1, count);
    if (pad_fired(Button::Down))  cp_move(&p,  1, count);
    if (pad_fired(Button::Left))  cp_move(&p, -1, count);
    if (pad_fired(Button::Right)) cp_move(&p,  1, count);
}

/*----------------------
 | cv_verb_wants_prep
 | Description: Whether the sentence's verb has a TYPE_PREP transition in the
 |   trie -- the flag cp_pick needs when the noun slot closes. At the verb slot
 |   itself the verb being picked right now (still absent from p.line) is
 |   `picking`; afterwards it is the sentence's first word.
 | Author: suinevere
 | Dependencies: typeahead.h, command_panel.h
 | Globals: N/A
 | Params: p -- panel state; root -- typeahead trie, may be null; picking -- the
 |   word about to be picked, used only at the verb slot
 | Returns: 1 if the verb takes a preposition, 0 otherwise
 ----------------------*/
static int cv_verb_wants_prep(const CommandPanel &p, TrieNode *root, const char *picking) {
    char buf[CP_WORD_MAX + 1];
    const char *verb_text;
    DictionaryWord *verb;
    NextWordLink *l;
    if (root == 0) return 0;
    if (p.slot == CP_SLOT_VERB) {
        verb_text = picking;
    } else {
        int i = 0;
        while (i < p.line_len && p.line[i] != ' ' && i < CP_WORD_MAX) { buf[i] = p.line[i]; i++; }
        buf[i] = '\0';
        verb_text = buf;
    }
    verb = find_exact_word(root, verb_text);
    if (verb == 0) return 0;
    for (l = verb->next_words; l != 0; l = l->next)
        if (l->target_word->type == TYPE_PREP) return 1;
    return 0;
}

/*----------------------
 | cv_word_accept
 | Description: Accept in the word module: the cell one past the last filled
 |   one, when it reads "v more" (cp_fill guarantees that cell sits at index
 |   w.n whenever w.more is set), turns the page instead of picking; otherwise
 |   the cell under the cursor is picked, with wants_prep resolved via
 |   cv_verb_wants_prep.
 | Author: suinevere
 | Dependencies: command_panel.h, typeahead.h
 | Globals: N/A
 | Params: p -- panel state; w -- the word page currently drawn; root --
 |   typeahead trie, may be null
 | Returns: N/A
 ----------------------*/
static void cv_word_accept(CommandPanel &p, const CommandWords &w, TrieNode *root) {
    if (p.cursor == w.n && w.more) {
        p.page++;
        p.cursor = 0;
        return;
    }
    if (p.cursor < w.n && w.word[p.cursor] != 0) {
        int wants_prep = cv_verb_wants_prep(p, root, w.word[p.cursor]);
        cp_pick(&p, w.word[p.cursor], wants_prep);
    }
}

/*----------------------
 | cv_cmd_accept
 | Description: Accept in the command module: overwrites the sentence in
 |   progress with the selected entry's submit word and marks it submitted --
 |   these are whole standalone commands, not sentence-slot picks. "invent" is
 |   the one exception: with the room model available there is a carried set
 |   to show, so it opens the overlay instead of submitting; unavailable, it
 |   falls through and submits "inventory" like a typed command, since there
 |   is nothing to browse.
 | Author: suinevere
 | Dependencies: command_panel.h, room_model.h
 | Globals: N/A
 | Params: p -- panel state
 | Returns: N/A
 ----------------------*/
static void cv_cmd_accept(CommandPanel &p) {
    const char *cmd;
    int i = 0;
    if (p.cursor == 0 && room_model_available()) { cp_overlay_open(&p); return; }
    cmd = CV_CMD_WORD[p.cursor];
    while (cmd[i] != '\0' && i < CP_LINE_MAX - 1) { p.line[i] = cmd[i]; i++; }
    p.line[i] = '\0';
    p.line_len = i;
    p.slot = CP_SLOT_DONE;
    p.submitted = 1;
}

/*----------------------
 | cv_overlay_accept
 | Description: Accept from the inventory overlay: resolves the selected
 |   carried object's parser word and hands it to cp_pick, which owns every
 |   outcome -- the pick lands (waiting for a noun, a word resolved), or the
 |   overlay closes unchanged (waiting for a verb; or waiting for a noun but
 |   nothing carried at the cursor, or the object has no detectable synonym
 |   property, in which case `word` is empty). Always calls cp_pick, never
 |   branching on cp_overlay_takes_noun itself, so Accept can never leave the
 |   overlay stuck open with no word to offer and no close to fall back on.
 | Author: suinevere
 | Dependencies: room_model.h, command_panel.h, typeahead.h
 | Globals: N/A
 | Params: p -- panel state; m -- the room snapshot; root -- typeahead trie,
 |   may be null
 | Returns: N/A
 ----------------------*/
static void cv_overlay_accept(CommandPanel &p, const RoomModel &m, TrieNode *root) {
    char word[8] = {0};
    int has = 0;
    if (p.cursor >= 0 && p.cursor < m.ncarried)
        has = room_model_object_word(m.carried[p.cursor], word, sizeof word);
    cp_pick(&p, has ? word : 0, has ? cv_verb_wants_prep(p, root, word) : 0);
}

/*----------------------
 | command_edit
 | Description: One frame of command-mode input. With the inventory overlay up
 |   it takes the pad exclusively: the D-pad walks the carried list, Accept
 |   resolves the selection through cp_pick, Back closes it unchanged --
 |   module focus does not move, since the overlay spans all three modules.
 |   Otherwise: L/R move focus, the D-pad walks the focused module or acts as
 |   the literal compass in travel, Accept picks, Back unwinds. A completed
 |   command is copied into `k` and submitted, so it leaves through the same
 |   path a typed one does. `ke` is accepted for the physical-keyboard escape
 |   hatch a later task wires in, and is not consumed here. `w` is refreshed
 |   for the current slot/page before the D-pad is read, since the word
 |   module's cursor bound depends on it.
 | Author: suinevere
 | Dependencies: input.h, command_panel.h, room_model.h
 | Globals: g_pad
 | Params: k -- keyboard state the command is written into; p -- panel state;
 |   m -- the room snapshot; root -- the typeahead trie for ranking, may be null;
 |   ke -- the decoded key event, consumed as handled; w -- (out) the word page
 |   the renderer should draw
 | Returns: N/A
 ----------------------*/
void command_edit(KeyboardState &k, CommandPanel &p, const RoomModel &m,
                  TrieNode *root, SaturnKeyEvent &ke, CommandWords &w) {
    if (p.overlay) {
        cv_overlay_dpad(p, m.ncarried);
        if (pad_fired(face_button(FA_ACCEPT))) cv_overlay_accept(p, m, root);
        if (pad_fired(face_button(FA_BACK)))   cp_overlay_close(&p);
    } else {
        if (pad_fired(Button::L)) cp_focus(&p, -1);
        if (pad_fired(Button::R)) cp_focus(&p, +1);

        cv_refill_words(p, root, w);

        if      (p.box == CP_BOX_TRAVEL) cv_travel_pick(p);
        else if (p.box == CP_BOX_WORD)   cv_word_dpad(p, w);
        else if (p.box == CP_BOX_CMD)    cv_cmd_dpad(p);

        if (pad_fired(face_button(FA_ACCEPT))) {
            if      (p.box == CP_BOX_WORD) cv_word_accept(p, w, root);
            else if (p.box == CP_BOX_CMD)  cv_cmd_accept(p);
        }
        if (pad_fired(face_button(FA_BACK))) cp_back(&p);
    }

    if (p.submitted) {
        int i;
        for (i = 0; i < p.line_len && i < KB_INPUT_MAX - 1; i++) k.input[i] = p.line[i];
        k.input[i] = '\0';
        k.input_len = i;
        k.cursor = i;
        k.submitted = 1;
        cp_reset(&p);
    }

    cv_refill_words(p, root, w);
    (void) ke;
}
