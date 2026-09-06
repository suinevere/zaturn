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
#include "controller.h"
#include "command_rose.h"
#include "rose_draw.h"
#include "text_map.h"
#include "console_view.h"
#include "app_state.h"
#include "input.h"
#include "dash_view.h"
#include "sentence_shape.h"
#ifndef NETBIN
#include "video/item_art.h"
#endif

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
 | g_strip_top
 | Description: The row the strip's top border was last drawn on, which is also
 |   the first row the room picture does not reach. cv_pointer_travel needs the
 |   picture's extent and this is where it is already known.
 | Author: suinevere
 ----------------------*/
static int g_strip_top = 0;

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
 | CV_CMD_MENU .. CV_CMD_N / CV_CMD_ROW
 | Description: The fixed command module's entries, in display order, and how
 |   many. Every one routes to a mechanism that already exists, so none of them
 |   needs a new path to the interpreter.
 |
 |   Three of the five are not commands at all: menu opens the pause menu, map
 |   opens the map screen, and swap flips the dashboard to its other mode. They
 |   are the entries with no word to submit, and map is the only one that can be
 |   missing -- see cv_cmd_count. Save, Load and Quit used to sit here and do not
 |   any more: all three live on the menu this module now opens, and a row that
 |   costs one press to reach beats three rows that each cost one.
 | Author: suinevere
 ----------------------*/
enum { CV_CMD_MENU = 0, CV_CMD_INVENT, CV_CMD_LOOK, CV_CMD_MAP,
       CV_CMD_SWAP, CV_CMD_N };
static const char *CV_CMD_ROW[CV_CMD_N] = { "menu", "invent", "look", "map", "swap" };

/*----------------------
 | CV_CMD_WORD
 | Description: The command each CV_CMD_ROW entry actually submits, which differs
 |   from the display text where the interpreter's verb is not the label: "invent"
 |   submits "inventory". Menu, map and swap submit nothing; their entries are
 |   empty and cv_cmd_accept never reaches them.
 | Author: suinevere
 ----------------------*/
static const char *CV_CMD_WORD[CV_CMD_N] = { "", "inventory", "look", "", "" };

/*----------------------
 | cv_cmd_count / cv_cmd_entry
 | Description: How many rows the command module shows, and which entry a row
 |   holds. Hard hides the map -- exactly as the pause menu does, for the same
 |   reason: a difficulty that shows the player only the rooms they have walked
 |   into does not hand them the sheet -- and it is the only entry that ever goes,
 |   so hiding it is one row off the count and a single skip in the index.
 | Author: suinevere
 | Dependencies: app_state.h (g_difficulty)
 | Globals: g_difficulty
 | Params: row -- 0..cv_cmd_count()-1
 | Returns: the row count, or the CV_CMD_* entry that row holds
 ----------------------*/
static int cv_cmd_count(void) {
    return (g_difficulty == DIFF_HARD) ? CV_CMD_N - 1 : CV_CMD_N;
}

static int cv_cmd_entry(int row) {
    if (g_difficulty == DIFF_HARD && row >= CV_CMD_MAP) row++;
    return row;
}

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
 | CV_LOBBY_WORDS / g_cv_lobby
 | Description: Everything a multizork session accepts before its game starts,
 |   and whether that is what the word module is currently offering. Read off
 |   multizorkd.c's four pre-game input handlers rather than guessed at, because
 |   a word this list offers that they do not take is a pick that spends a turn
 |   and answers "Wrong choice or room name":
 |
 |     inpfn_waiting_for_players  "go" starts the game, "quit" closes the room
 |     inpfn_player_waiting       "quit" leaves it
 |     inpfn_new_room_privacy     "yes" or "no" lists the room or hides it
 |     inpfn_lobby                "n" opens a new room, "q" leaves,
 |                                a number picks a row
 |
 |   Both quits are here because they are not the same word: the lobby matches
 |   "q" alone and the waiting rooms match "quit" alone, so either one typed at
 |   the other screen does nothing. Numbers run to twenty-five because that is
 |   the most the lobby can print -- LOBBY_MAX_ROWS rows numbered from two, over
 |   the "<new room>" that is always one -- and the whole point of listing them
 |   is that a two-digit row cannot be reached by picking twice: a pick here is
 |   the whole command, and two picks would send "1 2".
 |
 |   Six words then twenty-five numbers, so the words fill the first three rows
 |   of the two-column module and the numbers start on a row of their own.
 | Author: suinevere
 ----------------------*/
static const char *CV_LOBBY_WORDS[] = {
    "go", "quit", "yes", "no", "n", "q",
    "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",  "10", "11", "12", "13",
    "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25"
};
#define CV_LOBBY_WORDS_N ((int) (sizeof(CV_LOBBY_WORDS) / sizeof(CV_LOBBY_WORDS[0])))

static int g_cv_lobby = 0;

void cv_set_lobby(int on) { g_cv_lobby = on ? 1 : 0; }
int  cv_lobby(void)       { return g_cv_lobby; }

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
    static int   last_lobby = -1;
    static TrieNode *last_root = 0;
    static char  last_line[CP_LINE_MAX];
    int gen = typeahead_screen_gen();
    /* The lobby flag is part of the key for the same reason the trie pointer is:
       it decides which list gets sourced, and the frame the game begins on
       changes nothing else the key is watching. */
    int i, same = (p.slot == last_slot && g_difficulty == last_diff && gen == last_gen
                   && root == last_root && p.line_len == last_len
                   && g_cv_lobby == last_lobby);
    if (same)
        for (i = 0; i < p.line_len; i++)
            if (p.line[i] != last_line[i]) { same = 0; break; }
    if (same) return 0;
    last_slot = p.slot; last_diff = g_difficulty; last_gen = gen;
    last_root = root;   last_len = p.line_len;  last_lobby = g_cv_lobby;
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

static int cv_refill_words(CommandPanel &p, TrieNode *root, CommandWords &w) {
    if (cv_cache_stale(p, root)) {
        int core_n = 0;
        DictionaryWord *prev = 0;
        g_cv_ncand = 0;
        /* Whole list, not a head the story's words rank below: before the game
           exists there is no story vocabulary that could work, and every entry
           is protected so cv_reorder leaves the wanted-first order alone at all
           three difficulties. There is only ever the verb slot here -- picking
           in lobby mode submits rather than opening a sentence -- so no other
           branch can be reached. */
        if (g_cv_lobby) {
            int i;
            for (i = 0; i < CV_LOBBY_WORDS_N; i++)
                g_cv_ncand = cv_add_cand(g_cv_cand, g_cv_ncand, CV_LOBBY_WORDS[i]);
            core_n = g_cv_ncand;
        } else if (p.slot == CP_SLOT_VERB) {
            g_cv_ncand = cv_build_verb_cands(root, g_cv_cand, &core_n);
        } else if (p.slot == CP_SLOT_NOUN) {
            prev = cv_last_word(p, root);
            g_cv_ncand = cv_build_noun_cands(root, prev, g_cv_cand, &core_n);
        } else if (p.slot == CP_SLOT_PREP) {
            g_cv_ncand = cv_build_prep_cands(root, g_cv_cand);
        }
        cv_reorder(g_cv_cand, g_cv_ncand, prev, core_n);
        cv_truncate_all(g_cv_cand, g_cv_ncand);
    }
    // Before the fill, not after: the cursor and scroll a slot change restored
    // were measured against that slot's previous list, and this one may be
    // shorter -- a noun list is the room's, so it changes on every move. Nothing
    // else clamps on a refill; cp_clamp_cursor was only ever reached from
    // cp_word_move, so an out-of-range place survived until the player pushed
    // the stick.
    cp_clamp(&p, g_cv_ncand);
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
    int used = cv_pad_field(CV_CMD_ROW[cv_cmd_entry(row)], field);
    text_print_dim(x, y, field);
    if (used > 0 && p.box == CP_BOX_CMD && p.cursor == row) {
        field[used] = '\0';
        text_print(x, y, field);
    }
}

/*----------------------
 | cv_overlay_border
 | Description: Builds one horizontal frame row of the overlay box for the
 |   fallback path: '+', dashes across the strip, '+', and a third '+' on the
 |   divider column when the box carries a picture module. Only ever printed
 |   when the tile layer never came up -- with it up the frame and the divider
 |   are stone, laid by dash_map, and nothing here is printed over them.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: tall -- 1 when the box carries a picture module; out -- receives
 |   CV_OVERLAY_W characters plus a NUL
 | Returns: N/A
 ----------------------*/
static void cv_overlay_border(char *out, int tall) {
    int i;
    for (i = 0; i < CV_OVERLAY_W; i++) out[i] = '-';
    out[0] = '+';
    out[CV_OVERLAY_W - 1] = '+';
    if (tall) out[CV_OVERLAY_DIV_X] = '+';
    out[CV_OVERLAY_W] = '\0';
}

/*----------------------
 | cv_overlay_row_text
 | Description: Builds one row of the overlay's item list: a leading space, the
 |   row's position in the inventory right-aligned in CV_OVERLAY_NUM_COLS, and
 |   then the carried item at `idx`'s parser word -- recovered to its full
 |   spelling from the object's own short name where a longer one exists --
 |   padded out to `field_w` characters. The row is the list module's interior
 |   and nothing else, with no frame characters in it, because the frame is the
 |   strip's own. The caller picks field_w: the whole interior for the plain
 |   box, or as far as the divider for the tall one. Blank -- number and all --
 |   once idx runs past the carried count, and a recovered spelling longer than
 |   the field truncates cleanly. The field is padded to its full width rather
 |   than stopped at the word so that a short name cannot leave a longer one's
 |   tail standing beside it, whatever order the rows are drawn in.
 |
 |   The number counts the whole inventory rather than the visible page, so the
 |   second page of a long list carries on from where the first stopped instead
 |   of starting at one again. Nothing selects by it; it is there to say how
 |   much is in hand and where in that the cursor is.
 | Author: suinevere
 | Dependencies: room_model.h
 | Globals: N/A
 | Params: m -- the room snapshot; idx -- carried index, may be out of range;
 |   field_w -- the list module's width in characters; out -- receives field_w
 |   characters plus a NUL
 | Returns: N/A
 ----------------------*/
static void cv_overlay_row_text(const RoomModel &m, int idx, int field_w, char *out) {
    char word[8] = {0};
    char full[16] = {0};
    int text_x = 1 + CV_OVERLAY_NUM_COLS;
    int have = (idx >= 0 && idx < m.ncarried);
    int i, wl;
    if (have && room_model_object_word(m.carried[idx], word, sizeof word))
        room_model_full_word(m.carried[idx], word, full, sizeof full);
    wl = 0;
    while (wl < (int) sizeof(full) - 1 && full[wl] != '\0') wl++;
    out[0] = ' ';
    for (i = 1; i < field_w; i++)
        out[i] = (i >= text_x && i - text_x < wl) ? full[i - text_x] : ' ';
    if (have) {
        int n = idx + 1;
        out[1] = (n >= 10) ? (char) ('0' + (n / 10) % 10) : ' ';
        out[2] = (char) ('0' + n % 10);
        out[3] = ')';
    }
    out[field_w] = '\0';
}

/*----------------------
 | cv_draw_overlay
 | Description: Draws the inventory overlay's item list, scrolled in blocks
 |   around the cursor, with every row but the selected one dim. The box's frame
 |   is the strip's own frame and its rows are the strip's own content rows, so
 |   there is nothing here to draw a border with: the plain box's list runs the
 |   whole interior, and the tall box's stops at the divider column, leaving
 |   the picture module's columns untouched for NBG1's plate.
 |     On the fallback path, where the tile layer never came up and there is no
 |   stone to be the frame, the frame characters are printed instead -- the
 |   strip's two sides on every row, plus the divider between list and picture.
 | Author: suinevere
 | Dependencies: room_model.h, text_map.h, command_panel.h, dash_view.h
 | Globals: N/A
 | Params: p -- panel state; m -- the room snapshot; top_y -- the strip's first
 |   content row, which is the list's first row; dash -- 1 when the tile
 |   dashboard is up, so no frame characters are printed; tall -- 1 when the
 |   story has item art and the box is carrying a picture module
 | Returns: N/A
 ----------------------*/
static void cv_draw_overlay(const CommandPanel &p, const RoomModel &m, int top_y, int dash, int tall) {
    char row_text[CV_OVERLAY_W + 1];
    int window, i;
    int rows = tall ? CV_OVERLAY_ROWS : CV_OVERLAY_SHORT_LIST;
    int field_w = tall ? CV_OVERLAY_LIST_W : (CV_OVERLAY_W - 2);

    window = (p.cursor / rows) * rows;
    for (i = 0; i < rows; i++) {
        int y = top_y + i;
        int idx = window + i;
        cv_overlay_row_text(m, idx, field_w, row_text);
        /* The list module's ink carries the selection, the way every other
           picker's does now that reverse video is gone: the unselected rows go
           dim and the selected one is left at the player's own text colour.
           The frame characters below are not part of a row and never dim --
           they are the box, and a dim side beside a bright one would read as a
           break in the border rather than as a cursor. */
        if (idx == p.cursor) text_print(CV_OVERLAY_LIST_X, y, row_text);
        else                 text_print_dim(CV_OVERLAY_LIST_X, y, row_text);
        if (!dash) {
            text_print(CV_OVERLAY_X, y, "|");
            if (tall) text_print(CV_OVERLAY_DIV_X, y, "|");
            text_print(CV_OVERLAY_X + CV_OVERLAY_W - 1, y, "|");
        }
    }
}

/*----------------------
 | render_command_panel
 | Description: Draws the input line, the strip's borders and dividers, and
 |   either the inventory overlay or the compass rose/word page/command list,
 |   drawing the focused module's unselected entries dim and its selected one at
 |   the player's own text colour. The
 |   borders carry no highlight and no control hints -- both rows are the one
 |   CV_BORDER string.
 |     The overlay is the strip rather than a box drawn inside it: its frame is
 |   the strip's frame and its rows are the strip's content rows. So it takes a
 |   dashboard variant of its own shape -- OVERLAY, the three-module strip with
 |   its grooves removed so they do not cut across the item list, or
 |   OVERLAY_TALL, which is that five rows taller and split once at
 |   CV_OVERLAY_DIV_X to hold the picture module. That split is the border
 |   between the list and the picture, and it is cut in the same stone as every
 |   other seam on the strip.
 |     The tall shape is what a story with item art gets -- and only when its
 |   archive actually loaded, so a disc missing OITEM.CZ falls back to the plain
 |   list rather than showing a module whose frame is black for every item. The input line climbs
 |   with it by CV_OVERLAY_RISE rows, so the strip's own bottom border stays on
 |   the row it always sat on -- only the transcript above the panel loses rows,
 |   never the box's floor. The selected carried item's picture goes in the
 |   module's own frame, an item the story has no picture for fills that same
 |   frame with black, and outside the tall shape the window comes down
 |   altogether; the netbin has no item art module at all, so it never sees a
 |   tall box. Nothing here opens or frees the archive: item_art_set_game read
 |   it at game load and it is held for the session, so raising the overlay
 |   costs no disc access at all.
 | Author: suinevere
 | Dependencies: command_rose.h, rose_draw.h, text_map.h, console_view.h,
 |   dash_view.h, item_art.h
 | Globals: g_difficulty
 | Params: p -- panel state; m -- the room snapshot; w -- the current word page
 | Returns: N/A
 ----------------------*/
void render_command_panel(const CommandPanel &p, const RoomModel &m, const CommandWords &w) {
    int base = CV_TOP_MARGIN + console_height();
#ifndef NETBIN
    int tall = (p.overlay && item_art_available()) ? 1 : 0;
#else
    int tall = 0;
#endif
    int input_row = base - (tall ? CV_OVERLAY_RISE : 0);
    int border_top = input_row + 1;
    int content0 = border_top + 1;
    int border_bottom = content0 + (tall ? CV_OVERLAY_TALL_ROWS : CV_STRIP_ROWS);
    char frame[CV_OVERLAY_W + 1];
    int row;

    /* The overlay's frame is the strip's frame, so on the fallback path the
       strip's own three-module border is the wrong shape for it: the grooves
       between the modules would cut across the item list. */
    if (p.overlay) cv_overlay_border(frame, tall);

    int dash = dash_ready();
    g_strip_top = border_top;
    dash_set(p.overlay ? (tall ? DASH_OVERLAY_TALL : DASH_OVERLAY) : DASH_PANEL, border_top);

    /* Black behind the box on the fallback path, the way a menu box is black:
       NBG3 leaves palette entry 0 transparent, so over a wallpaper the rose and
       the lists would otherwise be read against the picture. */
    image_window_box(0, border_top, 40, border_bottom - border_top + 1);
    image_window_on();

    text_clear_line(input_row);
    text_print(0, input_row, "> %s", p.line);

    text_clear_line(border_top);
    if (!dash) text_print(0, border_top, p.overlay ? frame : CV_BORDER);

    if (p.overlay) {
        int y;
        for (y = content0; y < border_bottom; y++) text_clear_line(y);
        cv_draw_overlay(p, m, content0, dash, tall);
#ifndef NETBIN
        if (!tall)
            item_art_hide();
        else if (p.cursor >= 0 && p.cursor < m.ncarried)
            item_art_show((unsigned int) m.carried[p.cursor]);
        else
            item_art_blank();
#endif
    } else {
#ifndef NETBIN
        item_art_hide();
#endif
        unsigned char flat[RM_DIR_N];
        const unsigned char *exits;
        int sel = (p.box == CP_BOX_TRAVEL) ? p.cursor : -1;

        exits = m.exits;
        if (g_difficulty == DIFF_HARD) { cv_flatten_hard(m.exits, flat); exits = flat; }

        /* The rose owns all seven rows; the word module takes the five between
           its corner rows, which is what leaves that list vertically centred
           against it. The command module starts on the same row so focus
           crossing sideways arrives at the height it left from, and runs one
           row further down when it is carrying the map. */
        int ncmd = cv_cmd_count();
        for (row = 0; row < CR_ROWS; row++) {
            int y = content0 + row;
            int inner = row - CV_LIST_ROW0;
            text_clear_line(y);
            if (!dash) text_print(0, y, "|");
            cv_draw_rose_row(row, exits, y, sel);
            if (!dash) text_print(14, y, "|");
            if (inner >= 0 && inner < CP_WORD_ROWS) cv_draw_word_row(inner, p, w, y);
            if (!dash) text_print(30, y, "|");
            if (inner >= 0 && inner < ncmd) cv_draw_cmd_row(inner, p, y);
            if (!dash) text_print(39, y, "|");
        }
    }

    text_clear_line(border_bottom);
    if (!dash) text_print(0, border_bottom, p.overlay ? frame : CV_BORDER);
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
 | Description: Walks the command module's single column of entries, and
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
    if (pad_fired(Button::Up))   cp_move(&p, -1, cv_cmd_count());
    if (pad_fired(Button::Down)) cp_move(&p,  1, cv_cmd_count());
    /* The word list is the shorter of the two, so the row the cursor left from
       may not exist in it; cp_word_enter clamps to what is showing. */
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
 | cv_next_slot
 | Description: The slot the next pick should open, read off the story's own verb
 |   grammar: the sentence so far plus the word about to be picked, matched
 |   against the rows that verb allows. SHAPE_FREE -- a verb the grammar does not
 |   describe, a sentence an override took off it, a story whose grammar table is
 |   not Infocom-format, or Hard, where no trie and no shape table are built at
 |   all -- falls back to the chain this file used to hardcode, so a story whose
 |   grammar cannot be read is served no worse than before.
 | Author: suinevere
 | Dependencies: sentence_shape.h, command_panel.h
 | Globals: N/A
 | Params: p -- panel state; picking -- the word about to be picked, null when
 |   the line is being re-read rather than added to
 | Returns: one of CP_SLOT_VERB..CP_SLOT_DONE
 ----------------------*/
static int cv_next_slot(const CommandPanel &p, const char *picking) {
    const char *words[CP_SLOT_POS_MAX];
    char buf[CP_SLOT_POS_MAX][CP_WORD_MAX + 1];
    ShapeSlot s;
    int n = 0, i = 0, len = 0;

    while (i < p.line_len && n < CP_SLOT_POS_MAX - 1) {
        if (p.line[i] == ' ') {
            if (len > 0) { buf[n][len] = '\0'; words[n] = buf[n]; n++; len = 0; }
        } else if (len < CP_WORD_MAX) {
            buf[n][len++] = p.line[i];
        }
        i++;
    }
    if (len > 0 && n < CP_SLOT_POS_MAX - 1) { buf[n][len] = '\0'; words[n] = buf[n]; n++; }
    if (picking != 0 && picking[0] != '\0' && n < CP_SLOT_POS_MAX) words[n++] = picking;

    shape_next(words, n, &s);
    if (s.kind == SHAPE_PREP) return CP_SLOT_PREP;
    if (s.kind == SHAPE_NOUN) return CP_SLOT_NOUN;
    if (s.kind == SHAPE_END)  return CP_SLOT_DONE;
    return (n <= 1) ? CP_SLOT_NOUN : CP_SLOT_DONE;
}

/*----------------------
 | cv_word_accept
 | Description: Type Word in the word module: the cell under the cursor is picked,
 |   with the slot that follows it resolved via cv_next_slot. What reaches the
 |   command is cv_submit_form's spelling, not the cell's -- the cell shows the six
 |   characters a v3 dictionary entry holds and the sentence should read in full.
 |   The grammar lookup still uses the cell's own text, since six characters is
 |   what the story's grammar table is keyed by.
 | Author: suinevere
 | Dependencies: command_panel.h, sentence_shape.h, room_model.h
 | Globals: N/A
 | Params: p -- panel state; w -- the word window currently drawn; m -- the room
 |   snapshot
 | Returns: N/A
 ----------------------*/
static void cv_word_accept(CommandPanel &p, const CommandWords &w,
                           const RoomModel &m) {
    char submit[CP_WORD_MAX + 24];
    int next;
    if (p.cursor < 0 || p.cursor >= w.n || w.word[p.cursor] == 0) return;
    /* A lobby word is the whole answer, so it goes as it stands and goes now.
       Neither of the two steps skipped has anything to say here: the grammar
       lookup would consult a trie built from a story this prompt is not being
       read by, and the spelling recovery would search a room the player is not
       standing in. */
    if (g_cv_lobby) {
        cp_pick_whole(&p, w.word[p.cursor]);
        return;
    }
    next = cv_next_slot(p, w.word[p.cursor]);
    cv_submit_form(w.word[p.cursor], m, submit, (int) sizeof submit);
    cp_pick(&p, submit, next);
}

/*----------------------
 | CV_TRAVEL_EDGE
 | Description: How many cells deep the picture's four movement bands are. The
 |   picture is divided into a border of this thickness and a dead middle, so a
 |   shot has to be meant rather than merely land somewhere in the frame.
 | Author: suinevere
 ----------------------*/
#define CV_TRAVEL_EDGE 3

/*----------------------
 | cv_pointer_travel
 | Description: Turns a shot at the edge of the room picture into a move, which is
 |   the light gun's way of doing what the compass rose does for a pad: the top
 |   band is north, the bottom south, the left west and the right east, and the
 |   middle is dead. Only an exit the room actually has is taken, so a shot at a
 |   wall does nothing rather than submitting a command the parser will refuse.
 |   Corners resolve to the vertical band, since a room is likelier to have north
 |   and south than the diagonal the corner would otherwise suggest.
 | Author: suinevere
 | Dependencies: controller.h, command_panel.h, room_model.h
 | Globals: g_strip_top
 | Params: p -- panel state; exits -- the room's exit table
 | Returns: true if a direction was picked, so the caller can stop looking
 ----------------------*/
static bool cv_pointer_travel(CommandPanel &p, const unsigned char *exits) {
    const DevPointer *ptr = controller_pointer();
    if (!ptr->valid || !ptr->hot) return false;
    if (g_strip_top <= 0 || ptr->row >= g_strip_top) return false;

    int d = -1;
    if      (ptr->row < CV_TRAVEL_EDGE)                  d = 0;   /* north */
    else if (ptr->row >= g_strip_top - CV_TRAVEL_EDGE)   d = 3;   /* south */
    else if (ptr->col < CV_TRAVEL_EDGE)                  d = 2;   /* west  */
    else if (ptr->col >= CV_OVERLAY_W - CV_TRAVEL_EDGE)  d = 1;   /* east  */
    if (d < 0) return false;
    if (exits[d] != RM_EXIT_OPEN && exits[d] != RM_EXIT_MAYBE) return false;

    cp_pick(&p, room_model_dir_word(d), CP_SLOT_DONE);
    controller_pointer_consume();
    return true;
}

/*----------------------
 | cv_cmd_accept
 | Description: Type Word in the command module: overwrites the sentence in
 |   progress with the selected entry's submit word and marks it submitted --
 |   these are whole standalone commands, not sentence-slot picks. Two entries are
 |   not: "invent" opens the inventory overlay when the model actually holds
 |   carried objects, and falls through to submit "inventory" like a typed command
 |   when it does not -- with nothing carried, or no model at all, the game's own
 |   answer is the better one. "menu", "map" and "swap" submit nothing and only
 |   record the request; see CP_ACT_MAP.
 | Author: suinevere
 | Dependencies: command_panel.h, room_model.h
 | Globals: N/A
 | Params: p -- panel state; m -- the room snapshot, for what is carried
 | Returns: N/A
 ----------------------*/
static void cv_cmd_accept(CommandPanel &p, const RoomModel &m) {
    const char *cmd;
    int entry;
    int i = 0;
    if (p.cursor < 0 || p.cursor >= cv_cmd_count()) return;
    entry = cv_cmd_entry(p.cursor);
    /* Browse only when there is something to browse. An empty box tells the
       player nothing; the game's own "You are empty-handed" tells them the
       thing they asked. It also keeps the netbin honest, where the model is
       bound to a static story and can never have an inventory to show. */
    if (entry == CV_CMD_INVENT && room_model_available() && m.ncarried > 0) {
        cp_overlay_open(&p);
        return;
    }
    /* Three of the five are screens or mode changes, not commands. Each is left
       as a request for the frame loop hosting the panel: that loop owns the fade
       around the map and the menu and is the only thing that can hand the display
       over and take it back, and it is the only place that holds both command
       buffers a swap has to move the half-built line between. */
    if (entry == CV_CMD_MAP)  { p.action = CP_ACT_MAP;  return; }
    if (entry == CV_CMD_MENU) { p.action = CP_ACT_MENU; return; }
    if (entry == CV_CMD_SWAP) { p.action = CP_ACT_SWAP; return; }
    cmd = CV_CMD_WORD[entry];
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
 | Dependencies: room_model.h, command_panel.h, sentence_shape.h
 | Globals: N/A
 | Params: p -- panel state; m -- the room snapshot
 | Returns: N/A
 ----------------------*/
static void cv_overlay_accept(CommandPanel &p, const RoomModel &m) {
    char word[8] = {0};
    char submit[32] = {0};
    int has = 0;
    if (p.cursor >= 0 && p.cursor < m.ncarried)
        has = room_model_object_word(m.carried[p.cursor], word, sizeof word);
    /* The full spelling, same as the word module submits -- the object is
       already in hand here, so it is read straight off rather than searched
       for. */
    if (has) room_model_full_word(m.carried[p.cursor], word, submit, (int) sizeof submit);
    cp_pick(&p, has ? submit : 0, has ? cv_next_slot(p, word) : CP_SLOT_DONE);
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
 |   Neither send moves the cursor -- cp_reset leaves the player where they were,
 |   so a direction can be sent twice without re-aiming and a command-module entry
 |   twice without leaving the module.
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
 | Dependencies: input.h (pad_fired/face_button), keyboard.h
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
    /* L+R swaps the dashboard's two modes, so the panel has to stop reading the
       two triggers as module jumps while they are held together --
       otherwise the combo also cycles focus, and because cp_focus clamps rather
       than wraps the L and R of one press do not cancel out at the ends: from
       the leftmost module the pair lands one to the right with the cursor
       reset. Same rule slot_raw applies to SL_LR, for the same reason. */
    bool lr_both = g_pad->IsHeld(Button::L) && g_pad->IsHeld(Button::R);

    if (p.overlay) {
        cv_overlay_dpad(p, m.ncarried);
        if (pad_fired(face_button(FA_TYPE))) cv_overlay_accept(p, m);
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

        /* A shot at the picture's edge is a move. Resolved here, after the Hard
           flatten, so the gun can only reach the exits the rose is showing; and
           before anything reads the pointer's fallback action, or the same
           trigger pull would arrive again as a plain letter-click. */
        if (cv_pointer_travel(p, exits)) return;

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
        /* And a rose the player is already standing on can go stale under them
           now that a sent command leaves focus where it was: the direction they
           travelled by is a direction of the room they LEFT, and the room they
           arrived in need not offer it. Re-aimed at the nearest one that room
           does offer, keeping the row so the cursor lands beside where it was,
           and handed to the word module when the room offers nothing at all. */
        if (p.box == CP_BOX_TRAVEL) {
            int d = p.cursor, row;
            if (d < 0 || d >= RM_DIR_N ||
                (exits[d] != RM_EXIT_OPEN && exits[d] != RM_EXIT_MAYBE)) {
                row = (d >= 0 && d < RM_DIR_N) ? cr_dir_row(d) : -1;
                if (!cv_enter_travel(p, exits, row < 0 ? CV_LIST_ROW0 : row)) {
                    p.box = CP_BOX_WORD;
                    p.cursor = 0;
                    p.top = 0;
                }
            }
        }

        ncand = cv_refill_words(p, root, w);

        /* Recall loads a past command into the panel's own line rather than the
           input line the on-screen keyboard fills -- the panel draws p.line, and
           a history entry written anywhere else would not appear. "" is the step
           past the newest entry and clears, which cp_load_line reads as empty;
           null is "nothing moved" and must leave the line alone, not clear it. */
        if (chord_fired(CA_RECALL, -1)) {
            const char *h = history_recall_text(1);
            if (h != 0) { cp_load_line(&p, h); cp_set_slot(&p, cv_next_slot(p, 0)); }
        }
        if (chord_fired(CA_RECALL, +1)) {
            const char *h = history_recall_text(0);
            if (h != 0) { cp_load_line(&p, h); cp_set_slot(&p, cv_next_slot(p, 0)); }
        }

        /* The D-pad is the cursor AND the direction half of every chord, so it
           stops moving the selection while the modifier is held -- otherwise a
           recall or a scroll drags the cursor across the module underneath it.
           It is the modifier itself that is asked, not a fixed list of shift
           buttons: the player picks which button that is. */
        if (!chord_mod_held()) {
            if      (p.box == CP_BOX_TRAVEL) cv_travel_dpad(p, exits, ncand);
            else if (p.box == CP_BOX_WORD)   cv_word_dpad(p, exits, ncand);
            else if (p.box == CP_BOX_CMD)    cv_cmd_dpad(p, ncand);
        }

        if (pad_fired(face_button(FA_TYPE))) {
            if (p.box == CP_BOX_TRAVEL) {
                int d = p.cursor;
                if (d >= 0 && d < RM_DIR_N &&
                    (exits[d] == RM_EXIT_OPEN || exits[d] == RM_EXIT_MAYBE))
                    cp_pick(&p, room_model_dir_word(d), CP_SLOT_DONE);
            }
            else if (p.box == CP_BOX_WORD) cv_word_accept(p, w, m);
            else if (p.box == CP_BOX_CMD)  cv_cmd_accept(p, m);
        }
        if (pad_fired(face_button(FA_ACCEPT))) cp_submit(&p);
        if (pad_fired(face_button(FA_BACK))) {
            int before = p.box, before_cursor = p.cursor;
            cp_back(&p);
            cp_set_slot(&p, cv_next_slot(p, 0));
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
