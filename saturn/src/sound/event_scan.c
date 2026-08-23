/*----------------------
 | event_scan.c
 | Description: Recognises the Z-machine death banner in a turn's output, and
 |   nothing else. The keyword table this file used to carry -- treasure,
 |   gold, troll, flames, scream -- fired on any turn that merely mentioned
 |   one, so opening a chest of coins sounded like the end of the game.
 |
 |   The banner is structural, not a guess: `**** You have died ****` is
 |   printed by the story's own death routine and by nothing else, and a
 |   decode of every string in all thirty-two shipped stories found 52 of
 |   them and no other starred banner but the interpreter's own disk-error
 |   messages, which name no death word.
 | Author: suinevere
 | Dependencies: event_scan.h, string.h
 ----------------------*/
#include "event_scan.h"
#include <string.h>

/*----------------------
 | BANNER_STARS
 | Description: How many consecutive asterisks make a banner rather than
 |   emphasis. Three, because the stories print four but a line that has been
 |   wrapped or truncated by the output buffer can arrive with fewer.
 | Author: suinevere
 ----------------------*/
#define BANNER_STARS 3

/*----------------------
 | DEATH_WORDS
 | Description: The words a death banner carries. "died" covers 52 of the 54
 |   starred banners in the library; "killed" and "death" are the phrasings
 |   the other endings use, and "school" is Wishbringer's, which is a death
 |   however it is worded.
 | Author: suinevere
 ----------------------*/
static const char* const DEATH_WORDS[] = { "died", "killed", "death", "school" };

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
 | contains_at
 | Description: True when `word` sits at offset p of `text`, compared without
 |   case. Substring, not whole word: the banner is already the boundary, so
 |   "died." and "died" must both count.
 | Author: suinevere
 | Dependencies: lc
 | Globals: N/A
 | Params: text -- haystack; p -- offset to test; word -- lowercase needle
 | Returns: 1 on a case-insensitive match at p, 0 otherwise
 ----------------------*/
static int contains_at(const char* text, int p, const char* word) {
    int i = 0;
    while (word[i] && text[p + i] && lc(text[p + i]) == word[i]) i++;
    return word[i] == 0;
}

/*----------------------
 | banner_says_death
 | Description: Whether any death word appears in the `span` bytes of `text`
 |   starting at `from` -- the window a banner's own line occupies.
 | Author: suinevere
 | Dependencies: contains_at
 | Globals: DEATH_WORDS
 | Params: text -- the turn's output; from -- where the stars ended; span --
 |   how far to look
 | Returns: 1 when the window names a death, 0 otherwise
 ----------------------*/
static int banner_says_death(const char* text, int from, int span) {
    int n = (int)(sizeof DEATH_WORDS / sizeof DEATH_WORDS[0]);
    int p, w;
    for (p = from; p < from + span && text[p]; p++)
        for (w = 0; w < n; w++)
            if (contains_at(text, p, DEATH_WORDS[w])) return 1;
    return 0;
}

/*----------------------
 | event_scan
 | Description: EV_LOSE when the turn's output carries the death banner,
 |   EV_NONE otherwise. Never returns EV_WIN -- winning is signalled by the
 |   interpreter ending, not by anything printed.
 | Author: suinevere
 | Dependencies: string.h, banner_says_death
 | Globals: BANNER_STARS
 | Params: text -- the turn's output text (NULL -> EV_NONE)
 | Returns: EV_LOSE or EV_NONE
 ----------------------*/
int event_scan(const char* text) {
    int len, p, stars;
    if (!text) return EV_NONE;
    len = (int) strlen(text);
    for (p = 0; p < len; p++) {
        if (text[p] != '*') continue;
        stars = 0;
        while (p + stars < len && text[p + stars] == '*') stars++;
        if (stars >= BANNER_STARS && banner_says_death(text, p + stars, 48))
            return EV_LOSE;
        p += stars - 1;
    }
    return EV_NONE;
}
