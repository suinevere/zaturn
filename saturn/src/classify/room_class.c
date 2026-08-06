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
 | has_word_n
 | Description: Case-insensitive whole-word search within the first `len` bytes
 |   of `text`, so "cave" does not match "caverns". The bounded form exists so a
 |   single sentence can be scored without copying it out of the turn buffer.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: text -- haystack; len -- how much of it to search; word -- lowercase needle
 | Returns: 1 on a whole-word match, 0 otherwise
 ----------------------*/
static int has_word_n(const char* text, int len, const char* word) {
    int wl = (int) strlen(word), p, i;
    for (p = 0; p + wl <= len; p++) {
        i = 0;
        while (i < wl && lc(text[p + i]) == word[i]) i++;
        if (i == wl) {
            char before = (p == 0) ? ' ' : text[p - 1];
            char after  = (p + wl < len) ? text[p + wl] : ' ';
            int lb = !((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z'));
            int la = !((after  >= 'a' && after  <= 'z') || (after  >= 'A' && after  <= 'Z'));
            if (lb && la) return 1;
        }
    }
    return 0;
}

/*----------------------
 | has_word
 | Description: Case-insensitive whole-word search over the whole of `text`;
 |   the unbounded form of has_word_n, kept for callers that have no sentence
 |   boundary to respect (the title, and text_scan_event's full-turn scan).
 | Author: suinevere
 | Dependencies: string.h, has_word_n
 | Globals: N/A
 | Params: text -- haystack; word -- lowercase needle
 | Returns: 1 on a whole-word match, 0 otherwise
 ----------------------*/
static int has_word(const char* text, const char* word) {
    return has_word_n(text, (int) strlen(text), word);
}

/*----------------------
 | has_phrase_n
 | Description: Case-insensitive substring search within the first `len` bytes of
 |   `text`. Not word-bounded, because the modifier phrases contain spaces and
 |   are matched as written.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: text -- haystack; len -- how much of it to search; phrase -- lowercase needle
 | Returns: 1 on a match, 0 otherwise
 ----------------------*/
static int has_phrase_n(const char* text, int len, const char* phrase) {
    int pl = (int) strlen(phrase), p, i;
    for (p = 0; p + pl <= len; p++) {
        i = 0;
        while (i < pl && lc(text[p + i]) == phrase[i]) i++;
        if (i == pl) return 1;
    }
    return 0;
}

/*----------------------
 | sentence_weight
 | Description: What one sentence's keyword hits are worth: 0 when it names
 |   something distant, 2 when it names the room the player is in, 1 otherwise.
 | Author: suinevere
 | Dependencies: room_class.h (text_neg_phrases, text_pos_phrases)
 | Globals: N/A
 | Params: s -- the sentence; len -- its length
 | Returns: the multiplier
 ----------------------*/
static int sentence_weight(const char* s, int len) {
    int n = 0, i;
    const char* const* p = text_neg_phrases(&n);
    for (i = 0; i < n; i++) if (has_phrase_n(s, len, p[i])) return 0;
    p = text_pos_phrases(&n);
    for (i = 0; i < n; i++) if (has_phrase_n(s, len, p[i])) return 2;
    return 1;
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
 | GENRE_LOCK_HITS / GENRE_LOCK_MARGIN / GENRE_LOCK_ROOMS
 | Description: When an inferred genre is settled enough to act on: the leader
 |   needs this many marker hits, this much of a lead over every other genre, and
 |   this many classified rooms behind it. Three conditions rather than one
 |   because a single sci-fi word in a fantasy game's opening room is common, and
 |   locking on it would be worse than never locking at all.
 | Author: suinevere
 ----------------------*/
#define GENRE_LOCK_HITS   3
#define GENRE_LOCK_MARGIN 2
#define GENRE_LOCK_ROOMS  3

/*----------------------
 | genre state (g_genre .. g_genre_rooms)
 | Description: g_genre is the resolved genre mask (0 = unresolved); g_genre_lock
 |   is 1 once it can be acted on and never clears within a game; g_genre_hits
 |   counts markers per genre for inference; g_genre_rooms counts classified rooms
 |   so a lock cannot happen on the strength of a single one.
 | Author: suinevere
 ----------------------*/
static unsigned char g_genre = 0;
static int g_genre_lock = 0;
static int g_genre_hits[3] = {0, 0, 0};
static int g_genre_rooms = 0;

/*----------------------
 | room_class_reset
 | Description: Clears the module's per-game state, so neither a room name nor a
 |   resolved genre recorded for one story can leak into the next -- on its own.
 |   music_reset calls this and then immediately room_class_set_game(g_release,
 |   g_serial), which re-resolves the genre from the (still-loaded) game
 |   identity, so on the title-screen reset path the previous game's genre
 |   comes right back. Harmless today only because the one path that loads a
 |   new game overwrites identity before any room is classified; the leak this
 |   function guards against now depends on that call order, not on this
 |   function alone.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room_title, g_genre, g_genre_lock, g_genre_hits, g_genre_rooms
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void room_class_reset(void) {
    int i;
    g_room_title[0] = 0;
    g_genre = 0;
    g_genre_lock = 0;
    g_genre_rooms = 0;
    for (i = 0; i < 3; i++) g_genre_hits[i] = 0;
}

/*----------------------
 | genre_slot
 | Description: The g_genre_hits index for a single-bit genre mask.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: mask -- GN_FANTASY, GN_SCIFI or GN_MODERN
 | Returns: 0..2, or -1 for anything else
 ----------------------*/
static int genre_slot(unsigned char mask) {
    if (mask == GN_FANTASY) return 0;
    if (mask == GN_SCIFI)   return 1;
    if (mask == GN_MODERN)  return 2;
    return -1;
}

/*----------------------
 | room_class_set_game
 | Description: Looks up the story's authored genre. A listed game resolves at
 |   load and never infers; an unlisted one starts unresolved with its counters
 |   cleared, so ambiguous keywords abstain until the markers settle.
 | Author: suinevere
 | Dependencies: room_class.h (text_game_genre)
 | Globals: g_genre, g_genre_lock, g_genre_hits, g_genre_rooms
 | Params: release -- Z-machine release number; serial -- the 6-char game serial
 | Returns: N/A
 ----------------------*/
void room_class_set_game(unsigned int release, const char* serial) {
    int i;
    g_genre = text_game_genre(release, serial);
    g_genre_lock = g_genre ? 1 : 0;
    g_genre_rooms = 0;
    for (i = 0; i < 3; i++) g_genre_hits[i] = 0;
}

/*----------------------
 | room_class_genre_locked
 | Description: 1 once the genre can be acted on, which is the signal to discard
 |   any category memoized while ambiguous words were abstaining.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_genre_lock
 | Params: N/A
 | Returns: 1 when locked, 0 otherwise
 ----------------------*/
int room_class_genre_locked(void) { return g_genre_lock; }

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
 | tier_wins
 | Description: True when hit vector `a` outranks `b`. Comparison is
 |   lexicographic over the tiers, so Structure decides before Biome, which
 |   decides before Feature: the better tier wins outright at any count, and a
 |   tie at one tier falls to the next tier down. Equal vectors lose, which is
 |   what leaves an exact tie to the caller's enum order.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a, b -- per-tier hit counts, KT_NUM_TIERS entries each
 | Returns: 1 when a outranks b, 0 otherwise
 ----------------------*/
static int tier_wins(const unsigned short* a, const unsigned short* b) {
    for (int t = 0; t < KT_NUM_TIERS; t++)
        if (a[t] != b[t]) return a[t] > b[t];
    return 0;
}

/*----------------------
 | genre_accumulate
 | Description: Counts this room's genre markers and locks the inferred genre
 |   once all three GENRE_LOCK_* conditions are met. A no-op once locked, which
 |   includes every game whose genre was authored in GAME_GENRE.
 | Author: suinevere
 | Dependencies: room_class.h (text_genre_keywords, GN_*)
 | Globals: g_genre, g_genre_lock, g_genre_hits, g_genre_rooms
 | Params: text -- the room's text
 | Returns: N/A
 ----------------------*/
static void genre_accumulate(const char* text) {
    static const unsigned char MASK[3] = { GN_FANTASY, GN_SCIFI, GN_MODERN };
    int n = 0, i, lead = 0, second = 0, best = -1;
    if (g_genre_lock) return;
    const GenreKeyword* gk = text_genre_keywords(&n);
    for (i = 0; i < n; i++) {
        int s = genre_slot(gk[i].genre);
        if (s >= 0 && has_word(text, gk[i].word)) g_genre_hits[s]++;
    }
    g_genre_rooms++;
    for (i = 0; i < 3; i++) {
        if (g_genre_hits[i] > lead) { second = lead; lead = g_genre_hits[i]; best = i; }
        else if (g_genre_hits[i] > second) { second = g_genre_hits[i]; }
    }
    if (best >= 0 && g_genre_rooms >= GENRE_LOCK_ROOMS &&
        lead >= GENRE_LOCK_HITS && lead - second >= GENRE_LOCK_MARGIN) {
        g_genre = MASK[best];
        g_genre_lock = 1;
    }
}

/*----------------------
 | KW_VOTES
 | Description: Whether a keyword row may vote right now: always when it means
 |   the same thing in every genre, otherwise only once the genre is resolved and
 |   matches. An ambiguous row abstains while unresolved, because a confidently
 |   wrong vote costs more than no vote at all.
 | Author: suinevere
 ----------------------*/
#define KW_VOTES(k) ((k).genre == GN_ANY || (g_genre && ((k).genre & g_genre)))

/*----------------------
 | text_classify_room
 | Description: Scores room text against the keyword table and returns the
 |   winning category, or TC_NEUTRAL when nothing matched. The text is scored one
 |   sentence at a time: a sentence naming something distant ("far off, a forest")
 |   contributes nothing, a sentence naming the room itself ("you are in a cave")
 |   counts double, and an ordinary sentence counts once, all in place with no
 |   copy of the sentence out of the turn buffer. Hits are counted per tier rather
 |   than summed, and the tiers are compared in order, so a keyword that names the
 |   room the player is in beats any amount of scenery mentioned inside it. Title
 |   words count for TEXT_TITLE_WEIGHT extra WITHIN their tier, so a weighted
 |   title (or a doubled positive sentence) can break a tie but can no longer
 |   promote a Feature past a Structure.
 | Author: suinevere
 | Dependencies: room_class.h (text_keywords, TC_*, KT_*)
 | Globals: g_room_title
 | Params: text -- the turn's text, opening with the room title (NULL -> NEUTRAL)
 | Returns: the winning TC_* category
 ----------------------*/
int text_classify_room(const char* text) {
    if (!text) return TC_NEUTRAL;
    genre_accumulate(text);
    char firstline[TEXT_TITLE_MAX];
    const char* title;
    unsigned short hits[TEXT_NUM_CATEGORIES][KT_NUM_TIERS];
    int c, t, i;

    for (c = 0; c < TEXT_NUM_CATEGORIES; c++)
        for (t = 0; t < KT_NUM_TIERS; t++) hits[c][t] = 0;

    if (g_room_title[0] != 0) {
        title = g_room_title;
    } else {
        text_room_title(text, firstline);
        title = firstline;
    }

    int nk = 0; const TextKeyword* kw = text_keywords(&nk);
    int start = 0, pos = 0;
    for (;;) {
        char ch = text[pos];
        if (ch == 0 || ch == '.' || ch == '!' || ch == '?') {
            int len = pos - start;
            if (len > 0) {
                int w = sentence_weight(text + start, len);
                if (w > 0)
                    for (i = 0; i < nk; i++)
                        if (KW_VOTES(kw[i]) && has_word_n(text + start, len, kw[i].word))
                            hits[kw[i].cat][kw[i].tier] += (unsigned short) w;
            }
            if (ch == 0) break;
            start = pos + 1;
        }
        pos++;
    }
    for (i = 0; i < nk; i++)
        if (KW_VOTES(kw[i]) && has_word(title, kw[i].word))
            hits[kw[i].cat][kw[i].tier] += TEXT_TITLE_WEIGHT;

    /* Starts past TC_NEUTRAL on purpose: it is the nothing-matched answer, not
       something a keyword can vote for. An exact vector tie falls to the lowest
       id, which is what the strict tier_wins gives. */
    int best = TC_NEUTRAL;
    unsigned short none[KT_NUM_TIERS] = {0, 0, 0};
    const unsigned short* bestv = none;
    for (c = TC_WILDERNESS; c <= TC_PLACE_LAST; c++)
        if (tier_wins(hits[c], bestv)) { bestv = hits[c]; best = c; }
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
