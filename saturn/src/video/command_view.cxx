/*----------------------
 | command_view.cxx
 | Description: Implements the command panel's rendering and its pad-driven
 |   editor. render_command_panel draws the input line and the three-module
 |   strip (compass rose, word list, fixed commands) dim, with the focused
 |   module's selected entry alone at full brightness. command_edit reads the
 |   pad, drives the CommandPanel state machine, sources and orders each word
 |   slot's candidates, and hands the sentence to the same KeyboardState the
 |   on-screen keyboard fills -- either when the grammar slot chain completes on
 |   its own or when Accept sends what is there early.
 |
 |   The three modules are one grid as far as the D-pad is concerned: each has
 |   its own cursor, and a press that runs off a module's edge carries focus into
 |   the module beside it at the row it left from. CV_LIST_ROW0 is the single
 |   place that maps between a list row and a rose row, in both directions.
 |   The two pure-logic halves of that -- the rose's grid in command_rose.c and
 |   the word window's in command_panel.c -- are host-tested; what lives here is
 |   the wiring between them, which needs SRL and cannot be.
 | Author: suinevere
 | Dependencies: command_view.h, command_rose.h, text_map.h, console_view.h,
 |   app_state.h, input.h, SRL
 ----------------------*/

#include <srl.hpp>
#include "command_view.h"
#include "command_rose.h"
#include "rose_draw.h"
#include "text_map.h"
#include "console_view.h"
#include "app_state.h"
#include "input.h"
#include "dash_view.h"

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
 | CV_BORDER
 | Description: The strip's horizontal border, verbatim from the design --
 |   exactly 40 columns with the dividers already in place at columns 0, 14, 30
 |   and 39. Both rows print it. The bottom row used to be built segment by
 |   segment instead, carrying each module's control hints and a highlight on
 |   the focused one; it carries neither now, so the two rows are the
 |   same string and the focused module is shown only by its selected entry.
 | Author: suinevere
 ----------------------*/
static const char *CV_BORDER = "+-------------+---------------+--------+";

/*----------------------
 | CV_LIST_ROW0
 | Description: The strip content row the word and command lists start on. The
 |   rose is seven rows tall and they are five, so they sit one row in, centred
 |   between its up/in and down/out corners. Every mapping between a list row and
 |   a rose row goes through this, in both directions -- it is what makes focus
 |   crossing sideways arrive at the same height it left.
 | Author: suinevere
 ----------------------*/
#define CV_LIST_ROW0 1

/*----------------------
 | CV_CMD_N / CV_CMD_ROW
 | Description: The fixed command module's entries, in display order, and how
 |   many. Every one routes to a mechanism that already exists, so none of them
 |   needs a new path to the interpreter.
 | Author: suinevere
 ----------------------*/
#define CV_CMD_N 5
static const char *CV_CMD_ROW[CV_CMD_N] = { "invent", "look", "save", "load", "quit" };

/*----------------------
 | CV_CMD_WORD
 | Description: The command each CV_CMD_ROW entry actually submits, which differs
 |   from the display text where the interpreter's verb is not the label: "invent"
 |   submits "inventory", and "load" submits "restore" -- the z-machine's load verb,
 |   the same word the physical Load key and the options-menu Load Game send.
 | Author: suinevere
 ----------------------*/
static const char *CV_CMD_WORD[CV_CMD_N] = { "inventory", "look", "save", "restore", "quit" };

/*----------------------
 | CV_VERB_CORE
 | Description: The curated verbs offered before the story's own, in likelihood
 |   order. Each is dropped unless the loaded story's dictionary accepts it, so a
 |   game that does not define "attack" never offers it.
 | Author: suinevere
 ----------------------*/
static const char *CV_VERB_CORE[] = {
    "take", "open", "read", "drop", "close", "push", "pull",
    "move", "attack", "climb", "enter", "throw", "turn", "eat", "drink"
};
#define CV_VERB_CORE_N ((int) (sizeof(CV_VERB_CORE) / sizeof(CV_VERB_CORE[0])))

/*----------------------
 | CV_CAND_MAX / CV_PRED_MAX
 | Description: The cap on how many candidate words a slot's sourcing pass
 |   collects before ordering and paging. It has to hold a whole story's nouns,
 |   because the module pages through the list and anything past the cap is
 |   unreachable however far the player pages -- at the old 32 the Hard word list
 |   stopped inside the letter D and ZORK1's "leafle" (58th of its 118 nouns) could
 |   never be picked. 448 covers the largest shipped v3 game, Hitchhiker's, at 406.
 |   CV_PRED_MAX is the separate, smaller cap on the predicted head, matching
 |   typeahead.c's own CAND_MAX -- asking predict_candidates for more than it
 |   ranks would just size an array for nothing.
 | Author: suinevere
 ----------------------*/
#define CV_CAND_MAX 448
#define CV_PRED_MAX 32

/*----------------------
 | g_cv_rest / g_cv_restwt / g_cv_cand / g_cv_tmp
 | Description: The sourcing and ordering scratch, file-scope rather than
 |   function-local: at CV_CAND_MAX entries these are kilobytes apiece and the
 |   call chain nests three deep, which is more than the Saturn stack should
 |   carry. Nothing here recurses or runs concurrently, so one set is enough --
 |   g_cv_rest/g_cv_restwt for whichever sourcing pass is running, g_cv_cand for
 |   the refill that called it, g_cv_tmp for the reorder that follows.
 | Author: suinevere
 ----------------------*/
static const char *g_cv_rest[CV_CAND_MAX];
static int         g_cv_restwt[CV_CAND_MAX];
static const char *g_cv_cand[CV_CAND_MAX];
static const char *g_cv_tmp[CV_CAND_MAX];

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

/*----------------------
 | cv_in_cmd_module
 | Description: Whether the fixed command module already offers `word`, so the
 |   word list can leave it out rather than showing it twice in one strip. Tests
 |   both tables: CV_CMD_WORD is what those rows submit and so what the story's
 |   dictionary holds, CV_CMD_ROW is what they display, and only "invent" makes
 |   the two differ.
 |
 |   The word list needs this because it sources verbs from the story's own
 |   dictionary as well as from CV_VERB_CORE -- dropping a duplicate from the
 |   curated table alone would not remove it, only move it down into the
 |   weighted tail where it reappears.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: word -- the candidate to test
 | Returns: 1 when the command module already carries it, else 0
 ----------------------*/
static int cv_in_cmd_module(const char *word) {
    int i;
    if (word == 0) return 0;
    for (i = 0; i < CV_CMD_N; i++) {
        if (cv_str_eq(CV_CMD_WORD[i], word)) return 1;
        if (cv_str_eq(CV_CMD_ROW[i],  word)) return 1;
    }
    return 0;
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
    int nrest;

    for (i = 0; i < CV_VERB_CORE_N; i++) {
        const char *v = CV_VERB_CORE[i];
        int ok = have_model ? room_model_has_word(v)
                             : (root != 0 ? (find_exact_word(root, v) != 0) : 1);
        if (ok) n = cv_add_cand(out, n, v);
    }
    *core_n = n;
    if (root != 0) {
        nrest = cv_collect_type(root, TYPE_VERB, g_cv_rest, g_cv_restwt, 0);
        cv_sort_weight(g_cv_rest, g_cv_restwt, nrest);
        for (i = 0; i < nrest && n < CV_CAND_MAX; i++)
            if (!cv_in_cmd_module(g_cv_rest[i])) n = cv_add_cand(out, n, g_cv_rest[i]);
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
 | Description: Sources a noun slot exactly as the on-screen keyboard's typeahead
 |   does: predict_candidates ranks the head -- the nouns the last command printed
 |   first, then on-screen and grammar/solution links -- and that order is what the
 |   picker shows, so the two input methods agree. The story's remaining nouns are
 |   appended after it for paging. `ranked` reports the predicted head count so
 |   cv_reorder leaves it untouched; without that the Easy pass would hoist the
 |   verb's winning-path objects over the leaflet the game just described, which is
 |   the bug that made the picker and the keyboard disagree. Falls back to the
 |   dictionary enumerator when the trie yields nothing -- root null or (on Hard)
 |   present but wordless -- and that fallback is meant to be reordered.
 | Author: suinevere
 | Dependencies: typeahead.h, room_model.h
 | Globals: N/A
 | Params: root -- typeahead trie, may be null; prev -- the preceding word, may
 |   be null; out -- receives candidates; ranked -- receives the predicted head
 |   count cv_reorder must protect
 | Returns: candidate count
 ----------------------*/
static int cv_build_noun_cands(TrieNode *root, DictionaryWord *prev, const char **out,
                               int *ranked) {
    int n = 0, i;
    *ranked = 0;
    if (root != 0) {
        DictionaryWord *hot[CV_PRED_MAX];
        int nh = predict_candidates(root, prev, "", hot, CV_PRED_MAX, 0);
        int nrest;
        for (i = 0; i < nh; i++) n = cv_add_cand(out, n, hot[i]->text);
        *ranked = n;
        nrest = cv_collect_type(root, TYPE_NOUN, g_cv_rest, g_cv_restwt, 0);
        cv_sort_weight(g_cv_rest, g_cv_restwt, nrest);
        for (i = 0; i < nrest && n < CV_CAND_MAX; i++) n = cv_add_cand(out, n, g_cv_rest[i]);
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
    int nrest, n = 0, i;
    if (root == 0) return 0;
    nrest = cv_collect_type(root, TYPE_PREP, g_cv_rest, g_cv_restwt, 0);
    cv_sort_weight(g_cv_rest, g_cv_restwt, nrest);
    for (i = 0; i < nrest; i++) n = cv_add_cand(out, n, g_cv_rest[i]);
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
 |   leading `protect` entries -- the verb slot's curated core or the noun slot's
 |   already-ranked predicted head, each in its own order -- are never moved by
 |   either branch, so that head leads at every difficulty and only what ranks
 |   below it changes. Membership is whatever cv_build_*_cands sourced; only the
 |   order changes.
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
        int w = protect;
        for (i = protect; i < n; i++) g_cv_tmp[i] = cand[i];
        for (i = protect; i < n; i++) if (cv_has_solution_link(prev, g_cv_tmp[i])) cand[w++] = g_cv_tmp[i];
        for (i = protect; i < n; i++) if (!cv_has_solution_link(prev, g_cv_tmp[i])) cand[w++] = g_cv_tmp[i];
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
 | cv_cache_stale
 | Description: Whether the sourced list has to be rebuilt: true when the slot,
 |   the sentence so far, the difficulty, the trie or the screen generation has
 |   changed since the last build. Records the new key as it answers.
 |
 |   The list is worth caching because command_edit runs cv_refill_words every
 |   frame while the panel is up, and a rebuild walks the whole trie and then
 |   selection-sorts what it found -- quadratic work that a full story's nouns
 |   turn from a few hundred comparisons into a hundred thousand. None of its
 |   inputs can change without one of these five changing too. The page position
 |   is deliberately not part of the key: paging re-windows the same list, which
 |   cp_fill does for nothing.
 | Author: suinevere
 | Dependencies: command_panel.h, typeahead.h, app_state.h
 | Globals: g_difficulty
 | Params: p -- panel state; root -- the trie, may be null
 | Returns: 1 when the list must be rebuilt, 0 when the cached one still stands
 ----------------------*/
static int cv_cache_stale(const CommandPanel &p, TrieNode *root) {
    static int   last_slot = -1, last_diff = -1, last_gen = -1, last_len = -1;
    static TrieNode *last_root = 0;
    static char  last_line[CP_LINE_MAX];
    int gen = typeahead_screen_gen();
    int i, same = (p.slot == last_slot && g_difficulty == last_diff && gen == last_gen
                   && root == last_root && p.line_len == last_len);
    if (same)
        for (i = 0; i < p.line_len; i++)
            if (p.line[i] != last_line[i]) { same = 0; break; }
    if (same) return 0;
    last_slot = p.slot; last_diff = g_difficulty; last_gen = gen;
    last_root = root;   last_len = p.line_len;
    for (i = 0; i < p.line_len && i < CP_LINE_MAX; i++) last_line[i] = p.line[i];
    return 1;
}

/*----------------------
 | cv_refill_words
 | Description: Sources and orders the current slot's candidates, truncates
 |   each to six characters, and fills the window the renderer should draw.
 |   Sourcing is skipped while cv_cache_stale says the list still stands; the
 |   window is refilled either way, since that is where the page position lands.
 | Author: suinevere
 | Dependencies: command_panel.h, typeahead.h
 | Globals: g_cv_cand, g_cv_ncand
 | Params: p -- panel state; root -- typeahead trie, may be null; w -- receives
 |   the window
 | Returns: the whole candidate count, which the cursor's scrolling needs and
 |   the window itself does not carry
 ----------------------*/
static int g_cv_ncand = 0;

static int cv_refill_words(const CommandPanel &p, TrieNode *root, CommandWords &w) {
    if (cv_cache_stale(p, root)) {
        int core_n = 0;
        DictionaryWord *prev = 0;
        g_cv_ncand = 0;
        if (p.slot == CP_SLOT_VERB) {
            g_cv_ncand = cv_build_verb_cands(root, g_cv_cand, &core_n);
        } else if (p.slot == CP_SLOT_NOUN || p.slot == CP_SLOT_NOUN2) {
            prev = cv_last_word(p, root);
            g_cv_ncand = cv_build_noun_cands(root, prev, g_cv_cand, &core_n);
        } else if (p.slot == CP_SLOT_PREP) {
            g_cv_ncand = cv_build_prep_cands(root, g_cv_cand);
        }
        cv_reorder(g_cv_cand, g_cv_ncand, prev, core_n);
        cv_truncate_all(g_cv_cand, g_cv_ncand);
    }
    cp_fill(g_cv_cand, g_cv_ncand, p.top, &w);
    return g_cv_ncand;
}

/*----------------------
 | cv_submit_form
 | Description: The spelling a picked word should be sent to the parser as. A v3
 |   dictionary entry stops at six characters, so a word module cell reads
 |   "mailbo" where the object is a mailbox; the parser truncates its own input
 |   to six characters too, so handing it the recovered full spelling resolves to
 |   exactly the same entry while leaving "> open mailbox" in the transcript
 |   instead of "> open mailbo". The spelling is recovered from the short name of
 |   whichever object in the room -- present or carried -- answers to the picked
 |   word; a word that names no object here has nothing to recover from and is
 |   sent as it stands.
 | Author: suinevere
 | Dependencies: room_model.h
 | Globals: N/A
 | Params: word -- the picked cell's text; m -- the room snapshot; out -- receives
 |   the spelling to submit; max -- out's capacity
 | Returns: N/A
 ----------------------*/
static void cv_submit_form(const char *word, const RoomModel &m, char *out, int max) {
    int list, i;
    out[0] = '\0';
    if (word == 0 || max <= 0) return;
    for (i = 0; word[i] != '\0' && i < max - 1; i++) out[i] = word[i];
    out[i] = '\0';

    for (list = 0; list < 2; list++) {
        const unsigned short *objs = list ? m.carried : m.here;
        int n = list ? m.ncarried : m.nhere;
        for (i = 0; i < n; i++) {
            char w[8];
            if (!room_model_object_word(objs[i], w, (int) sizeof w)) continue;
            if (!cv_str_eq(w, word)) continue;
            room_model_full_word(objs[i], word, out, max);
            return;
        }
    }
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
 | cv_pad_field
 | Description: Copies up to six characters of `text` into `field`, blank-
 |   padding to a fixed seven-column width so a shorter word erases whatever the
 |   previous frame drew in the same cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: text -- the word, may be null; field -- receives 8 bytes (7 + NUL)
 | Returns: how many of the seven columns the word itself occupies, which is
 |   what a highlight covers -- the padding is spacing, not part of the word
 ----------------------*/
static int cv_pad_field(const char *text, char *field) {
    int i = 0;
    if (text != 0) for (; text[i] != '\0' && i < 6; i++) field[i] = text[i];
    {
        int used = i;
        for (; i < 7; i++) field[i] = ' ';
        field[7] = '\0';
        return used;
    }
}

/*----------------------
 | cv_draw_word_row
 | Description: Draws one word-module content row: two seven-column fields
 |   (one-column left margin already accounted for by CV_WORD_X). Every cell
 |   holds a candidate -- the list scrolls a row at a time against the bottom
 |   edge rather than spending a cell on a marker.
 |
 |   Every cell is drawn dim and the selected one has its letters alone
 |   overprinted at full brightness. Its letters and not its whole field: the
 |   padding is there to erase the previous frame, not to be part of the
 |   selection, and brightening seven columns for a four-letter word would read
 |   as the cursor covering the empty cell beside it. Matches the rose, where
 |   the bright text is the direction's label and nothing else.
 | Author: suinevere
 | Dependencies: command_panel.h, text_map.h
 | Globals: N/A
 | Params: row -- 0..CP_WORD_ROWS-1; p -- panel state; w -- the word window;
 |   y -- text row
 | Returns: N/A
 ----------------------*/
static void cv_draw_word_row(int row, const CommandPanel &p, const CommandWords &w, int y) {
    int col;
    for (col = 0; col < CP_WORD_COLS; col++) {
        int idx = row * CP_WORD_COLS + col;
        int x = CV_WORD_X + 1 + col * 7;
        char field[8];
        const char *text = (idx < w.n) ? w.word[idx] : 0;
        int used = cv_pad_field(text, field);
        text_print_dim(x, y, field);
        if (used > 0 && p.box == CP_BOX_WORD && p.cursor == idx) {
            field[used] = '\0';
            text_print(x, y, field);
        }
    }
}

/*----------------------
 | cv_draw_cmd_row
 | Description: Draws one command-module content row -- CV_CMD_ROW[row] in a
 |   seven-column field, dim, its letters alone at full brightness when the
 |   command module holds focus and its cursor sits on this row. Letters and not
 |   the whole field for the reason cv_draw_word_row gives.
 | Author: suinevere
 | Dependencies: command_panel.h, text_map.h
 | Globals: N/A
 | Params: row -- 0..CP_WORD_ROWS-1; p -- panel state; y -- text row
 | Returns: N/A
 ----------------------*/
static void cv_draw_cmd_row(int row, const CommandPanel &p, int y) {
    int x = CV_CMD_X + 1;
    char field[8];
    int used = cv_pad_field(CV_CMD_ROW[row], field);
    text_print_dim(x, y, field);
    if (used > 0 && p.box == CP_BOX_CMD && p.cursor == row) {
        field[used] = '\0';
        text_print(x, y, field);
    }
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
 | CV_OVERLAY_ROWS
 | Description: How many carried items the overlay lists at once: the strip's
 |   content height less its own two border rows. Derived rather than written as
 |   5, because it is the strip that bounds it -- the rose's own row count grew
 |   from five to seven when up, down, in and out moved into its corners, and an
 |   overlay still sized off that would have drawn two rows past the strip and
 |   through the bottom border.
 | Author: suinevere
 ----------------------*/
#define CV_OVERLAY_ROWS (CV_STRIP_ROWS - 2)

/*----------------------
 | cv_draw_overlay
 | Description: Draws the inventory overlay across the strip's seven content
 |   rows: a top border, CV_OVERLAY_ROWS carried-item rows scrolled in blocks
 |   around the cursor, and a bottom border, with the selected row in reverse
 |   video.
 | Author: suinevere
 | Dependencies: room_model.h, text_map.h, command_panel.h, dash_view.h
 | Globals: N/A
 | Params: p -- panel state; m -- the room snapshot; top_y -- the row the
 |   overlay's top border is drawn on (the strip's first content row); dash --
 |   1 when the dashboard panel is up, so the border rows are skipped and the
 |   overlay sits on the marble instead of drawing a second frame over it; the
 |   rows stay blank either way, since the caller clears them first
 | Returns: N/A
 ----------------------*/
static void cv_draw_overlay(const CommandPanel &p, const RoomModel &m, int top_y, int dash) {
    char border[CV_OVERLAY_W + 1];
    char row_text[CV_OVERLAY_W + 1];
    int window, i;

    if (!dash) {
        cv_overlay_border(border);
        text_print(CV_OVERLAY_X, top_y, border);
    }

    window = (p.cursor / CV_OVERLAY_ROWS) * CV_OVERLAY_ROWS;
    for (i = 0; i < CV_OVERLAY_ROWS; i++) {
        int y = top_y + 1 + i;
        int idx = window + i;
        cv_overlay_row_text(m, idx, row_text);
        /* Only the span between the two pipes takes the selection's ink. The
           frame is the box and not a row, so dimming its sides along with an
           unselected item would leave the one bright pipe pair looking like a
           break in the border rather than like a cursor. */
        row_text[CV_OVERLAY_W - 1] = '\0';
        text_print(CV_OVERLAY_X, y, "|");
        if (idx == p.cursor) text_print(CV_OVERLAY_X + 1, y, row_text + 1);
        else                 text_print_dim(CV_OVERLAY_X + 1, y, row_text + 1);
        text_print(CV_OVERLAY_X + CV_OVERLAY_W - 1, y, "|");
    }

    if (!dash) text_print(CV_OVERLAY_X, top_y + 1 + CV_OVERLAY_ROWS, border);
}

/*----------------------
 | render_command_panel
 | Description: Draws the input line, the strip's borders and dividers, and
 |   either the inventory overlay or the compass rose/word page/command list,
 |   highlighting the focused module's selected entry in reverse video. The
 |   borders carry no highlight and no control hints -- both rows are the one
 |   CV_BORDER string. The overlay takes the divider-less dashboard variant,
 |   since its box spans all three modules and the grooves would otherwise show
 |   through the item list.
 | Author: suinevere
 | Dependencies: command_rose.h, rose_draw.h, text_map.h, console_view.h,
 |   dash_view.h
 | Globals: g_difficulty
 | Params: p -- panel state; m -- the room snapshot; w -- the current word page
 | Returns: N/A
 ----------------------*/
void render_command_panel(const CommandPanel &p, const RoomModel &m, const CommandWords &w) {
    int base = CV_TOP_MARGIN + console_height();
    int input_row = base;
    int border_top = input_row + 1;
    int content0 = border_top + 1;
    int border_bottom = content0 + CR_ROWS;
    int row;

    int dash = dash_ready();
    dash_set(p.overlay ? DASH_OVERLAY : DASH_PANEL, border_top);

    /* Black behind the box on the fallback path, the way a menu box is black:
       NBG3 leaves palette entry 0 transparent, so over a wallpaper the rose and
       the lists would otherwise be read against the picture. */
    image_window_box(0, border_top, 40, border_bottom - border_top + 1);
    image_window_on();

    text_clear_line(input_row);
    text_print(0, input_row, "> %s", p.line);

    text_clear_line(border_top);
    if (!dash) text_print(0, border_top, CV_BORDER);

    if (p.overlay) {
        int y;
        for (y = content0; y < border_bottom; y++) text_clear_line(y);
        cv_draw_overlay(p, m, content0, dash);
    } else {
        unsigned char flat[RM_DIR_N];
        const unsigned char *exits;
        int sel = (p.box == CP_BOX_TRAVEL) ? p.cursor : -1;

        exits = m.exits;
        if (g_difficulty == DIFF_HARD) { cv_flatten_hard(m.exits, flat); exits = flat; }

        /* The rose owns all seven rows; the word and command modules take the
           five between its corner rows, which is what leaves their lists
           vertically centred against it. */
        for (row = 0; row < CR_ROWS; row++) {
            int y = content0 + row;
            int inner = row - CV_LIST_ROW0;
            text_clear_line(y);
            if (!dash) text_print(0, y, "|");
            cv_draw_rose_row(row, exits, y, sel);
            if (!dash) text_print(14, y, "|");
            if (inner >= 0 && inner < CP_WORD_ROWS) {
                cv_draw_word_row(inner, p, w, y);
                if (!dash) text_print(30, y, "|");
                cv_draw_cmd_row(inner, p, y);
            } else {
                if (!dash) text_print(30, y, "|");
            }
            if (!dash) text_print(39, y, "|");
        }
    }

    text_clear_line(border_bottom);
    if (!dash) text_print(0, border_bottom, CV_BORDER);
}

// ---- pad-driven editing ------------------------------------------------------

/*----------------------
 | cv_enter_travel
 | Description: Carries focus into the travel module at the height it arrived
 |   at, refusing when the room offers no direction to sit on -- a rose with
 |   nothing in it is not somewhere the cursor can be, and focus stays where it
 |   was rather than vanishing into an empty box.
 | Author: suinevere
 | Dependencies: command_rose.h, command_panel.h
 | Globals: N/A
 | Params: p -- panel state; exits -- the exits as drawn; want_row -- the strip
 |   content row to aim for
 | Returns: 1 when focus moved, 0 when the module was refused
 ----------------------*/
static int cv_enter_travel(CommandPanel &p, const unsigned char *exits, int want_row) {
    int dir = cr_enter(exits, want_row, 1);
    if (dir < 0) return 0;
    p.box = CP_BOX_TRAVEL;
    p.cursor = dir;
    return 1;
}

/*----------------------
 | cv_travel_dpad
 | Description: Walks the rose's grid. The D-pad is a cursor here, not a literal
 |   compass -- Type Word is what travels -- so every direction the room offers is
 |   reachable by pressing toward it, including the four corners, and stepping
 |   off the right edge carries focus into the word module at the same height.
 |   The left edge is the strip's own, so a press against it does nothing.
 | Author: suinevere
 | Dependencies: input.h, command_rose.h, command_panel.h
 | Globals: N/A
 | Params: p -- panel state; exits -- the exits as drawn; ncand -- the word
 |   module's candidate count, for placing the cursor if focus crosses
 | Returns: N/A
 ----------------------*/
static void cv_travel_dpad(CommandPanel &p, const unsigned char *exits, int ncand) {
    bool fr = pad_fired(Button::Right), fl = pad_fired(Button::Left);
    bool fd = pad_fired(Button::Down),  fu = pad_fired(Button::Up);
    int dx, dy, dir = p.cursor, edge;

    if (!fr && !fl && !fd && !fu) return;

    /* A diagonal is one press to the player and two edges to the pad, and the
       two almost never land on the same frame. So the axis that did not fire is
       read as held instead: whichever edged this frame carries the press, and
       anything still down alongside it makes that press diagonal -- the same
       fired-plus-held pairing the rose used when the D-pad travelled directly. */
    dx = fr ? 1 : fl ? -1 : g_pad->IsHeld(Button::Right) ? 1
                          : g_pad->IsHeld(Button::Left)  ? -1 : 0;
    dy = fd ? 1 : fu ? -1 : g_pad->IsHeld(Button::Down)  ? 1
                          : g_pad->IsHeld(Button::Up)    ? -1 : 0;

    if (cr_dir_row(dir) < 0) { cv_enter_travel(p, exits, CV_LIST_ROW0); return; }

    edge = cr_move(exits, dir, dx, dy, &dir);
    p.cursor = dir;
    if (edge > 0) cp_word_enter(&p, cr_dir_row(dir) - CV_LIST_ROW0, 0, ncand);
}

/*----------------------
 | cv_word_dpad
 | Description: Walks the word module's grid and carries focus out of either
 |   side of it: left into the travel module, right into the command list, both
 |   at the row the cursor left from. Up and down against the window's edge
 |   scroll the candidate list a row rather than stopping, which is what replaced
 |   the page-turning marker that used to occupy a cell.
 | Author: suinevere
 | Dependencies: input.h, command_panel.h
 | Globals: N/A
 | Params: p -- panel state; exits -- the exits as drawn, for the travel module
 |   it may hand focus to; ncand -- the whole candidate count
 | Returns: N/A
 ----------------------*/
static void cv_word_dpad(CommandPanel &p, const unsigned char *exits, int ncand) {
    int dx = (pad_fired(Button::Right) ? 1 : 0) - (pad_fired(Button::Left) ? 1 : 0);
    int dy = (pad_fired(Button::Down)  ? 1 : 0) - (pad_fired(Button::Up)   ? 1 : 0);
    int row = p.cursor / CP_WORD_COLS;
    int edge;

    if (dx == 0 && dy == 0) return;
    edge = cp_word_move(&p, dx, dy, ncand);
    if (edge < 0)      cv_enter_travel(p, exits, row + CV_LIST_ROW0);
    else if (edge > 0) { p.box = CP_BOX_CMD; p.cursor = row; }
}

/*----------------------
 | cv_cmd_dpad
 | Description: Walks the command module's single column of five entries, and
 |   carries focus back into the word module on a press against its left edge.
 |   It is the rightmost module, so a press against that side does nothing.
 | Author: suinevere
 | Dependencies: input.h, command_panel.h
 | Globals: N/A
 | Params: p -- panel state; ncand -- the word module's candidate count, for
 |   placing the cursor if focus crosses
 | Returns: N/A
 ----------------------*/
static void cv_cmd_dpad(CommandPanel &p, int ncand) {
    int row = p.cursor;
    if (pad_fired(Button::Up))   cp_move(&p, -1, CV_CMD_N);
    if (pad_fired(Button::Down)) cp_move(&p,  1, CV_CMD_N);
    if (pad_fired(Button::Left)) cp_word_enter(&p, row, 1, ncand);
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
 | Description: Type Word in the word module: the cell under the cursor is picked,
 |   with wants_prep resolved via cv_verb_wants_prep. What reaches the command is
 |   cv_submit_form's spelling, not the cell's -- the cell shows the six
 |   characters a v3 dictionary entry holds and the sentence should read in full.
 |   The grammar lookup still uses the cell's own text, since that is the form the
 |   trie is keyed by.
 | Author: suinevere
 | Dependencies: command_panel.h, typeahead.h, room_model.h
 | Globals: N/A
 | Params: p -- panel state; w -- the word window currently drawn; m -- the room
 |   snapshot; root -- typeahead trie, may be null
 | Returns: N/A
 ----------------------*/
static void cv_word_accept(CommandPanel &p, const CommandWords &w,
                           const RoomModel &m, TrieNode *root) {
    char submit[CP_WORD_MAX + 24];
    int wants_prep;
    if (p.cursor < 0 || p.cursor >= w.n || w.word[p.cursor] == 0) return;
    wants_prep = cv_verb_wants_prep(p, root, w.word[p.cursor]);
    cv_submit_form(w.word[p.cursor], m, submit, (int) sizeof submit);
    cp_pick(&p, submit, wants_prep);
}

/*----------------------
 | cv_cmd_accept
 | Description: Type Word in the command module: overwrites the sentence in
 |   progress with the selected entry's submit word and marks it submitted --
 |   these are whole standalone commands, not sentence-slot picks. "invent" is
 |   the one exception: with a model that actually holds carried objects there is
 |   a set to browse, so it opens the overlay; with nothing carried -- or no
 |   model at all -- it falls through and submits "inventory" like a typed
 |   command and lets the game answer.
 | Author: suinevere
 | Dependencies: command_panel.h, room_model.h
 | Globals: N/A
 | Params: p -- panel state; m -- the room snapshot, for what is carried
 | Returns: N/A
 ----------------------*/
static void cv_cmd_accept(CommandPanel &p, const RoomModel &m) {
    const char *cmd;
    int i = 0;
    /* Browse only when there is something to browse. An empty box tells the
       player nothing; the game's own "You are empty-handed" tells them the
       thing they asked. It also keeps the netbin honest, where the model is
       bound to a static story and can never have an inventory to show. */
    if (p.cursor == 0 && room_model_available() && m.ncarried > 0) {
        cp_overlay_open(&p);
        return;
    }
    cmd = CV_CMD_WORD[p.cursor];
    while (cmd[i] != '\0' && i < CP_LINE_MAX - 1) { p.line[i] = cmd[i]; i++; }
    p.line[i] = '\0';
    p.line_len = i;
    p.slot = CP_SLOT_DONE;
    p.submitted = 1;
}

/*----------------------
 | cv_overlay_accept
 | Description: Type Word from the inventory overlay: resolves the selected
 |   carried object's parser word and hands it to cp_pick, which owns every
 |   outcome -- the pick lands (waiting for a noun, a word resolved), or the
 |   overlay closes unchanged (waiting for a verb; or waiting for a noun but
 |   nothing carried at the cursor, or the object has no detectable synonym
 |   property, in which case `word` is empty). Always calls cp_pick, never
 |   branching on cp_overlay_takes_noun itself, so Type Word can never leave the
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
    char submit[32] = {0};
    int has = 0;
    if (p.cursor >= 0 && p.cursor < m.ncarried)
        has = room_model_object_word(m.carried[p.cursor], word, sizeof word);
    /* The full spelling, same as the word module submits -- the object is
       already in hand here, so it is read straight off rather than searched
       for. */
    if (has) room_model_full_word(m.carried[p.cursor], word, submit, (int) sizeof submit);
    cp_pick(&p, has ? submit : 0, has ? cv_verb_wants_prep(p, root, word) : 0);
}

/*----------------------
 | command_edit
 | Description: One frame of command-mode input. With the inventory overlay up
 |   it takes the pad exclusively: the D-pad walks the carried list, Type Word
 |   resolves the selection through cp_pick, Back closes it unchanged --
 |   module focus does not move, since the overlay spans all three modules.
 |
 |   Otherwise the three modules are walked as one grid: the D-pad moves within
 |   the focused module and carries focus into the next one when it runs off an
 |   edge, at the row it left from, so reaching the command list from the rose is
 |   the same gesture as reaching the next word. L and R still jump modules
 |   outright. Type Word picks, Accept sends the line as it stands, Back unwinds.
 |
 |   Travel is a cursor like the other two rather than a literal compass: with
 |   twelve directions on a five-by-three grid the D-pad cannot both select and
 |   travel, and the diagonals used to need two buttons held at once to reach.
 |   Type Word is what travels now.
 |
 |   Focus is refused entry to a rose with no exits at all rather than left
 |   sitting on nothing, which is why every arrival there goes through
 |   cv_enter_travel and puts the box back when it returns 0.
 |
 |   The fixed L+R caps toggle is read here too, ahead of the overlay branch so
 |   it works from anywhere in the panel, matching typeahead_edit's placement of
 |   the same call on the keyboard side.
 |
 |   A completed command is copied into `k` and submitted, so it leaves through
 |   the same path a typed one does. `ke` is accepted for the physical-keyboard
 |   escape hatch a later task wires in, and is not consumed here. `w` is
 |   refreshed for the current slot and scroll before the D-pad is read, since
 |   the word module's cursor bound depends on it.
 | Author: suinevere
 | Dependencies: input.h (pad_fired/face_button/caps_combo_fired), keyboard.h
 |   (keyboard_get_caps/keyboard_set_caps), command_panel.h, room_model.h
 | Globals: g_pad
 | Params: k -- keyboard state the command is written into; p -- panel state;
 |   m -- the room snapshot; root -- the typeahead trie for ranking, may be null;
 |   ke -- the decoded key event, consumed as handled; w -- (out) the word page
 |   the renderer should draw
 | Returns: N/A
 ----------------------*/
void command_edit(KeyboardState &k, CommandPanel &p, const RoomModel &m,
                  TrieNode *root, SaturnKeyEvent &ke, CommandWords &w) {
    /* L+R is the caps toggle in both interfaces, so the panel has to stop
       reading the two triggers as module jumps while they are held together --
       otherwise the combo also cycles focus, and because cp_focus clamps rather
       than wraps the L and R of one press do not cancel out at the ends: from
       the leftmost module the pair lands one to the right with the cursor
       reset. Same rule slot_raw applies to SL_LR, for the same reason. */
    bool lr_both = g_pad->IsHeld(Button::L) && g_pad->IsHeld(Button::R);
    if (caps_combo_fired()) keyboard_set_caps(!keyboard_get_caps());

    if (p.overlay) {
        cv_overlay_dpad(p, m.ncarried);
        if (pad_fired(face_button(FA_TYPE))) cv_overlay_accept(p, m, root);
        if (pad_fired(face_button(FA_BACK))) cp_overlay_close(&p);
    } else {
        unsigned char flat[RM_DIR_N];
        const unsigned char *exits = m.exits;
        int ncand;
        int was = p.box, was_cursor = p.cursor, was_top = p.top;

        /* The rose is drawn flattened on Hard, and the cursor must walk what is
           drawn -- stepping onto a direction the player cannot see would give
           the difficulty away. */
        if (g_difficulty == DIFF_HARD) { cv_flatten_hard(m.exits, flat); exits = flat; }

        if (!lr_both) {
            if (pad_fired(Button::L)) cp_focus(&p, -1);
            if (pad_fired(Button::R)) cp_focus(&p, +1);
        }
        /* A rose with no exits at all is not somewhere the cursor can sit, so
           the jump is put back rather than half-taken. */
        if (p.box == CP_BOX_TRAVEL && was != CP_BOX_TRAVEL &&
            !cv_enter_travel(p, exits, CV_LIST_ROW0)) {
            p.box = was; p.cursor = was_cursor; p.top = was_top;
        }

        ncand = cv_refill_words(p, root, w);

        /* Recall loads a past command into the panel's own line rather than the
           input line the on-screen keyboard fills -- the panel draws p.line, and
           a history entry written anywhere else would not appear. "" is the step
           past the newest entry and clears, which cp_load_line reads as empty;
           null is "nothing moved" and must leave the line alone, not clear it. */
        if (chord_fired(CA_RECALL, -1)) {
            const char *h = history_recall_text(1);
            if (h != 0) cp_load_line(&p, h);
        }
        if (chord_fired(CA_RECALL, +1)) {
            const char *h = history_recall_text(0);
            if (h != 0) cp_load_line(&p, h);
        }

        /* The D-pad is the cursor AND the direction half of every chord, so it
           stops moving the selection while a shift is held -- otherwise a recall
           or a scroll drags the cursor across the module underneath it. */
        if (!chord_shift_held()) {
            if      (p.box == CP_BOX_TRAVEL) cv_travel_dpad(p, exits, ncand);
            else if (p.box == CP_BOX_WORD)   cv_word_dpad(p, exits, ncand);
            else if (p.box == CP_BOX_CMD)    cv_cmd_dpad(p, ncand);
        }

        if (pad_fired(face_button(FA_TYPE))) {
            if (p.box == CP_BOX_TRAVEL) {
                int d = p.cursor;
                if (d >= 0 && d < RM_DIR_N &&
                    (exits[d] == RM_EXIT_OPEN || exits[d] == RM_EXIT_MAYBE))
                    cp_pick(&p, room_model_dir_word(d), 0);
            }
            else if (p.box == CP_BOX_WORD) cv_word_accept(p, w, m, root);
            else if (p.box == CP_BOX_CMD)  cv_cmd_accept(p, m);
        }
        if (pad_fired(face_button(FA_ACCEPT))) cp_submit(&p);
        if (pad_fired(face_button(FA_BACK))) {
            int before = p.box, before_cursor = p.cursor;
            cp_back(&p);
            /* Back out of an empty word module lands in travel, which has to be
               placed on a real direction like any other arrival there. */
            if (p.box == CP_BOX_TRAVEL && before != CP_BOX_TRAVEL &&
                !cv_enter_travel(p, exits, CV_LIST_ROW0)) {
                p.box = before; p.cursor = before_cursor;
            }
        }
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
