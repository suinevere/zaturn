/*----------------------
 | room_class.c
 | Description: The classification logic: whole-word matching, the room-title
 |   read, the keyword scoring that picks a room's mood, and the event scan. The
 |   tables it reads live in room_class_data.c and are meant to be edited freely.
 | Author: suinevere
 | Dependencies: room_class.h, string.h
 ----------------------*/
#include "room_class.h"
#include <string.h>

/*----------------------
 | lc
 | Description: Lowercases one ASCII byte.
 | Author: suinevere
 ----------------------*/
static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/*----------------------
 | has_word
 | Description: Case-insensitive whole-word search: true when `word` (stored
 |   lowercase) occurs in `text` bounded on both sides by a non-alphabetic char or
 |   a string end, so "cave" does not match "caverns".
 | Author: suinevere
 | Dependencies: string.h
 | Globals: N/A
 | Params: text -- haystack; word -- lowercase needle
 | Returns: 1 on a whole-word match, 0 otherwise
 ----------------------*/
static int has_word(const char* text, const char* word) {
    int wl = (int) strlen(word);
    for (const char* p = text; *p; p++) {
        int i = 0;
        while (i < wl && p[i] && lc(p[i]) == word[i]) i++;
        if (i == wl) {
            char before = (p == text) ? ' ' : p[-1];
            char after  = p[wl];
            int lb = !((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z'));
            int la = !((after  >= 'a' && after  <= 'z') || (after  >= 'A' && after  <= 'Z'));
            if (lb && la) return 1;
        }
    }
    return 0;
}

/*----------------------
 | TEXT_TITLE_MAX / TEXT_TITLE_WEIGHT
 | Description: How much of the room title is read, and how much more a keyword
 |   found in it counts for than the same keyword found in the description.
 |
 |   The title is what the room IS; the description is what can be seen from it,
 |   and the two disagree constantly. Zork I's opening room is titled "West of
 |   House" and described as an open field with a boarded white house in it -- one
 |   wilderness word against one town word, which the flat count below scored 1-1
 |   and broke by enum order, silently handing every house on the map to the
 |   forest. "North of House" and "Behind House" were worse: they mention trees,
 |   a path and a forest, so the house lost outright 2-1 in its own room.
 |
 |   Weighting the title fixes those without a per-game table, because a title is
 |   authored to name the place while a description is authored to be walked
 |   around in. 2 is deliberately modest -- a title word ends up worth 3 (once as
 |   part of the text, twice as the title), so two agreeing description words can
 |   still outvote a title that is merely a direction, e.g. "Up a Tree" in a room
 |   that is really about the forest around it.
 | Author: suinevere
 ----------------------*/
#define TEXT_TITLE_MAX    64
#define TEXT_TITLE_WEIGHT 2

/*----------------------
 | g_room_title
 | Description: The room name the interpreter decoded for this turn, empty when
 |   nothing supplied one.
 | Author: suinevere
 ----------------------*/
static char g_room_title[TEXT_TITLE_MAX];

/*----------------------
 | room_class_note_title
 | Description: Records the authoritative room name for the turn about to be
 |   classified, which the interpreter reads off the location object rather than
 |   guessing from printed text.
 |
 |   It exists because the printed text lies on turn one. Zork I opens with its
 |   banner -- "ZORK I: The Great Underground Empire" -- above the room, so the
 |   first-line heuristic read that as the title, handed "underground" the title
 |   weight, and put West of House in a bunker. Any game whose banner names a
 |   place does the same, and so does any turn that prints something before the
 |   room description.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room_title
 | Params: title -- the room name, truncated to TEXT_TITLE_MAX-1; NULL clears it
 | Returns: N/A
 ----------------------*/
void room_class_note_title(const char* title) {
    int i = 0;
    if (title) for (; title[i] && i < TEXT_TITLE_MAX - 1; i++) g_room_title[i] = title[i];
    g_room_title[i] = 0;
}

/*----------------------
 | room_class_reset
 | Description: Clears the module's per-game state, so a room name recorded for
 |   one story cannot be weighted into the next one's first classification.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room_title
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void room_class_reset(void) {
    g_room_title[0] = 0;
}

/*----------------------
 | text_room_title
 | Description: Copies the first non-blank line of a turn's text into `out` (at
 |   most TEXT_TITLE_MAX-1 chars), which on the turn a room is entered is the room
 |   title the interpreter just printed above the description.
 |
 |   "On the turn a room is entered" is the whole of the contract, and it holds
 |   because music_on_turn only classifies when the room number changed -- so the
 |   buffer being read is the one that opened with the title. On any other turn
 |   this would return the first line of whatever the game said, which is why
 |   nothing else calls it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: text -- the turn's output; out -- TEXT_TITLE_MAX bytes of destination
 | Returns: N/A
 ----------------------*/
static void text_room_title(const char* text, char* out) {
    int i = 0, n = 0;
    while (text[i] == '\n' || text[i] == '\r' || text[i] == ' ' || text[i] == '\t') i++;
    while (text[i] && text[i] != '\n' && text[i] != '\r' && n < TEXT_TITLE_MAX - 1)
        out[n++] = text[i++];
    out[n] = 0;
}

/*----------------------
 | text_classify_room
 | Description: Scores room text against the keyword table and returns the
 |   category with the most keyword hits, defaulting to TC_NEUTRAL on a tie at
 |   zero. Keywords in the room's title count for TEXT_TITLE_WEIGHT more than the
 |   same word in the description -- see that box. This is the fallback when a game
 |   has no per-room category map.
 | Author: suinevere
 | Dependencies: music.h (text_keywords, TC_* / TEXT_NUM_CATEGORIES)
 | Globals: N/A
 | Params: text -- the turn's text, opening with the room title (NULL -> TC_NEUTRAL)
 | Returns: the winning TC_* category
 ----------------------*/
int text_classify_room(const char* text) {
    if (!text) return TC_NEUTRAL;
    char firstline[TEXT_TITLE_MAX];
    const char* title;
    int counts[TEXT_NUM_CATEGORIES];
    for (int i = 0; i < TEXT_NUM_CATEGORIES; i++) counts[i] = 0;
    if (g_room_title[0] != 0) {
        title = g_room_title;
    } else {
        text_room_title(text, firstline);
        title = firstline;
    }
    int nk = 0; const TextKeyword* kw = text_keywords(&nk);
    for (int i = 0; i < nk; i++) {
        /* The title is part of the text, so a title word scores in both passes --
           1 + TEXT_TITLE_WEIGHT -- while a description-only word scores 1. */
        if (has_word(text,  kw[i].word)) counts[kw[i].cat]++;
        if (has_word(title, kw[i].word)) counts[kw[i].cat] += TEXT_TITLE_WEIGHT;
    }
    /* Starts past TC_NEUTRAL on purpose: it is the nothing-matched answer, not
       something a keyword can vote for, so a hit scored into it could never be
       acted on. TC_HOUSE exists precisely so the domestic words have a real
       category to win -- see the keyword block in music_data.c. */
    int best = TC_NEUTRAL, bestn = 0;
    for (int c = TC_WILDERNESS; c <= TC_PLACE_LAST; c++)
        if (counts[c] > bestn) { bestn = counts[c]; best = c; }
    return best;
}

/*----------------------
 | text_scan_event
 | Description: Looks for an event keyword (combat, death, etc.) in the turn text,
 |   returning the first match's category so an event track can override the
 |   room's base mood for that turn.
 | Author: suinevere
 | Dependencies: music.h (text_events)
 | Globals: N/A
 | Params: text -- the turn's output text (NULL -> no event)
 | Returns: the event category, or -1 when none is present
 ----------------------*/
int text_scan_event(const char* text) {
    if (!text) return -1;
    int ne = 0; const TextKeyword* ev = text_events(&ne);
    for (int i = 0; i < ne; i++) if (has_word(text, ev[i].word)) return ev[i].cat;
    return -1;
}
