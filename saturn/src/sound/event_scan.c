/*----------------------
 | event_scan.c
 | Description: The event scan lifted out of the doomed room classifier
 |   (room_class.c's text_scan_event and room_class_data.c's EV table),
 |   renamed and made self-contained: whole-word, case-insensitive,
 |   first-match-wins over the whole of a turn's text. No dependency on the
 |   classifier or on music.h's TC_* enum, both of which this scan used to
 |   share -- events are a sound concern now, not a text-classification one.
 | Author: suinevere
 | Dependencies: event_scan.h, string.h
 ----------------------*/
#include "event_scan.h"
#include <string.h>

/*----------------------
 | lc
 | Description: Lowercases one ASCII byte.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: c -- the byte to lowercase
 | Returns: the lowercased byte
 ----------------------*/
static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/*----------------------
 | is_letter
 | Description: True for an ASCII letter -- what "inside a word" means to
 |   matches_at and has_word.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: c -- the byte to test
 | Returns: 1 for A-Z or a-z, 0 otherwise
 ----------------------*/
static int is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/*----------------------
 | matches_at
 | Description: True when `word` sits at offset p of `text`, compared without
 |   case. Says nothing about boundaries -- has_word decides those.
 | Author: suinevere
 | Dependencies: lc
 | Globals: N/A
 | Params: text -- haystack; p -- offset to test; word -- lowercase needle;
 |   wl -- its length
 | Returns: 1 on a case-insensitive match at p, 0 otherwise
 ----------------------*/
static int matches_at(const char* text, int p, const char* word, int wl) {
    int i = 0;
    while (i < wl && lc(text[p + i]) == word[i]) i++;
    return i == wl;
}

/*----------------------
 | has_word
 | Description: Case-insensitive whole-word search over the whole of `text`,
 |   so "fire" does not match "campfires".
 | Author: suinevere
 | Dependencies: string.h, matches_at, is_letter
 | Globals: N/A
 | Params: text -- haystack; word -- lowercase needle
 | Returns: 1 on a whole-word match, 0 otherwise
 ----------------------*/
static int has_word(const char* text, const char* word) {
    int len = (int) strlen(text);
    int wl = (int) strlen(word), p;
    for (p = 0; p + wl <= len; p++) {
        if (matches_at(text, p, word, wl)) {
            char before = (p == 0) ? ' ' : text[p - 1];
            char after  = (p + wl < len) ? text[p + wl] : ' ';
            if (!is_letter(before) && !is_letter(after)) return 1;
        }
    }
    return 0;
}

/*----------------------
 | EventKeyword
 | Description: One keyword -> EV_* mapping row.
 | Author: suinevere
 ----------------------*/
typedef struct {
    const char*   word;
    unsigned char cat;
} EventKeyword;

/*----------------------
 | EV
 | Description: Event keywords (danger / triumph) mapped to categories,
 |   lowercase. Carried over unchanged from room_class_data.c's EV table.
 | Author: suinevere
 ----------------------*/
static const EventKeyword EV[] = {
    {"monster",EV_DANGER},{"troll",EV_DANGER},
    {"grue",EV_DANGER},{"attack",EV_DANGER},
    {"fight",EV_DANGER},{"flames",EV_DANGER},
    {"fire",EV_DANGER},{"burning",EV_DANGER},
    {"scream",EV_DANGER},{"danger",EV_DANGER},
    {"treasure",EV_TRIUMPH},{"gold",EV_TRIUMPH},
    {"jewel",EV_TRIUMPH},{"chest",EV_TRIUMPH},
    {"reward",EV_TRIUMPH},{"gleaming",EV_TRIUMPH},
    {"victory",EV_TRIUMPH},
};

/*----------------------
 | event_scan
 | Description: Looks for an event keyword in the turn text, returning the
 |   first match's category so an event track can override the room's base
 |   mix for that turn.
 | Author: suinevere
 | Dependencies: has_word
 | Globals: EV
 | Params: text -- the turn's output text (NULL -> EV_NONE)
 | Returns: EV_DANGER, EV_TRIUMPH, or EV_NONE
 ----------------------*/
int event_scan(const char* text) {
    if (!text) return EV_NONE;
    int n = (int)(sizeof EV / sizeof EV[0]);
    for (int i = 0; i < n; i++)
        if (has_word(text, EV[i].word)) return EV[i].cat;
    return EV_NONE;
}
