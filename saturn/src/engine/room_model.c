/*----------------------
 | room_model.c
 | Description: The story decode described in room_model.h.
 | Author: suinevere
 | Dependencies: room_model.h
 ----------------------*/
#include "room_model.h"

/*----------------------
 | FL_DIR / PROP_MAX / RM_DIR_WORD
 | Description: The v3 dictionary's direction flag bit, the highest v3 property
 |   number, and the canonical spelling of each direction as Infocom's parsers
 |   hold it.
 | Author: suinevere
 ----------------------*/
#define FL_DIR   0x10
#define PROP_MAX 31

static const char *RM_DIR_WORD[RM_DIR_N] = {
    "north", "east", "west", "south", "ne", "nw", "se", "sw",
    "up", "down", "in", "out"
};

/*----------------------
 | g_story .. g_available
 | Description: The bound image and the header addresses read out of it, the
 |   per-direction property numbers, and whether the decode passed its gate.
 | Author: suinevere
 ----------------------*/
static const unsigned char *g_story;
static unsigned int g_len;
static unsigned int g_dict, g_obj, g_glob;
static int g_prop[RM_DIR_N];
static int g_available;

/*----------------------
 | rd16
 | Description: Reads a big-endian 16-bit word out of the bound image.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_story
 | Params: a -- byte offset
 | Returns: the word
 ----------------------*/
static unsigned int rd16(unsigned int a) {
    return (unsigned int) ((g_story[a] << 8) | g_story[a + 1]);
}

/*----------------------
 | dict_entry_len / dict_count / dict_first
 | Description: The dictionary's entry size, entry count, and the offset of its
 |   first entry, all read from the header's dictionary block.
 | Author: suinevere
 | Dependencies: rd16
 | Globals: g_story, g_dict
 | Params: N/A
 | Returns: the respective value
 ----------------------*/
static unsigned int dict_first(void) {
    return g_dict + 1u + g_story[g_dict] + 3u;
}
static unsigned int dict_entry_len(void) {
    return g_story[g_dict + 1u + g_story[g_dict]];
}
static unsigned int dict_count(void) {
    return rd16(g_dict + 2u + g_story[g_dict]);
}

/*----------------------
 | decode_word
 | Description: Decodes an entry's four text bytes into up to six lowercase
 |   letters. Only the A0 alphabet is decoded; shift and abbreviation codes are
 |   rendered as spaces, which is enough because every word compared here is
 |   plain lowercase.
 | Author: suinevere
 | Dependencies: rd16
 | Globals: g_story
 | Params: off -- entry offset; out -- receives 7 bytes (6 chars + NUL)
 | Returns: N/A
 ----------------------*/
static void decode_word(unsigned int off, char *out) {
    static const char A0[] =
        "      abcdefghijklmnopqrstuvwxyz";
    int p = 0, k;
    for (k = 0; k < 4; k += 2) {
        unsigned int x = rd16(off + (unsigned int) k);
        out[p++] = A0[(x >> 10) & 31];
        out[p++] = A0[(x >> 5) & 31];
        out[p++] = A0[x & 31];
    }
    out[6] = '\0';
    while (p > 0 && out[p - 1] == ' ') out[--p] = '\0';
}

/*----------------------
 | dir_prop_of
 | Description: The direction property number carried by a dictionary entry: of
 |   its data bytes, the unique one in 1..PROP_MAX. A word that is also an
 |   adjective or preposition carries that class's value too, which is why the
 |   byte is found by range rather than by offset. Zero or two candidates is a
 |   decode failure.
 | Author: suinevere
 | Dependencies: dict_entry_len
 | Globals: g_story
 | Params: off -- entry offset
 | Returns: the property number, or 0
 ----------------------*/
static int dir_prop_of(unsigned int off) {
    unsigned int elen = dict_entry_len();
    int found = 0, prop = 0;
    unsigned int i;
    for (i = 5; i < elen; i++) {
        unsigned char b = g_story[off + i];
        if (b >= 1 && b <= PROP_MAX) { prop = (int) b; found++; }
    }
    return (found == 1) ? prop : 0;
}

/*----------------------
 | room_model_bind
 | Description: Reads the story header, recovers the direction-word to
 |   property-number map from the dictionary, and gates on the recovered set
 |   being a contiguous run ending at 31 -- ZILCH allocates direction properties
 |   first, from the top down, so anything else means the story was not built
 |   that way and nothing here can be trusted. Call once per story load.
 | Author: suinevere
 | Dependencies: rd16, dict_first, dict_entry_len, dict_count, dir_prop_of,
 |   decode_word
 | Globals: g_story, g_len, g_dict, g_obj, g_glob, g_prop, g_available
 | Params: story -- the live story image; len -- its length in bytes
 | Returns: 1 if the model is available for this story, 0 otherwise
 ----------------------*/
int room_model_bind(const unsigned char *story, unsigned int len) {
    int seen[PROP_MAX + 1];
    int i, top, run, cardinals;
    unsigned int n, k, elen, first;

    g_available = 0;
    for (i = 0; i < RM_DIR_N; i++) g_prop[i] = 0;
    g_story = story; g_len = len;
    if (story == 0 || len < 64u) return 0;

    g_dict = rd16(0x08);
    g_obj  = rd16(0x0a);
    g_glob = rd16(0x0c);
    if (g_dict == 0 || g_obj == 0 || g_glob == 0) return 0;
    if (g_dict + 4u >= len || g_obj + 64u >= len || g_glob + 2u >= len) return 0;
    if (g_dict + (unsigned int) g_story[g_dict] + 4u > len) return 0;

    for (i = 0; i <= PROP_MAX; i++) seen[i] = 0;

    elen  = dict_entry_len();
    n     = dict_count();
    first = dict_first();
    if (elen < 6u || elen > 16u || n == 0u || first + n * elen > len) return 0;

    for (k = 0; k < n; k++) {
        unsigned int off = first + k * elen;
        char text[8];
        int prop;
        if (!(g_story[off + 4] & FL_DIR)) continue;
        prop = dir_prop_of(off);
        if (prop == 0) continue;
        seen[prop] = 1;
        decode_word(off, text);
        for (i = 0; i < RM_DIR_N; i++) {
            const char *w = RM_DIR_WORD[i];
            int j = 0;
            while (w[j] && j < 6 && w[j] == text[j]) j++;
            if ((w[j] == '\0' || j == 6) && text[j] == '\0') g_prop[i] = prop;
        }
    }

    top = 0;
    for (i = PROP_MAX; i >= 1; i--) { if (seen[i]) { top = i; break; } }
    if (top != PROP_MAX) return 0;
    run = 0;
    for (i = PROP_MAX; i >= 1 && seen[i]; i--) run++;
    for (; i >= 1; i--) { if (seen[i]) return 0; }
    if (run < 4) return 0;

    cardinals = (g_prop[RM_N] != 0) + (g_prop[RM_S] != 0)
              + (g_prop[RM_E] != 0) + (g_prop[RM_W] != 0);
    if (cardinals < 4) return 0;

    g_available = 1;
    return 1;
}

/*----------------------
 | room_model_available
 | Description: Whether the last bind produced a usable model.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_available
 | Params: N/A
 | Returns: 1 when available, 0 otherwise
 ----------------------*/
int room_model_available(void) { return g_available; }

/*----------------------
 | room_model_dir_prop
 | Description: The property number recovered for a direction.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_prop
 | Params: dir -- one of the RM_* direction indices
 | Returns: the property number, or 0 when dir is out of range or unresolved
 ----------------------*/
int room_model_dir_prop(int dir) {
    if (dir < 0 || dir >= RM_DIR_N) return 0;
    return g_prop[dir];
}

/*----------------------
 | room_model_dir_word
 | Description: A direction's canonical word.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: dir -- one of the RM_* direction indices
 | Returns: the word, or "" when dir is out of range
 ----------------------*/
const char *room_model_dir_word(int dir) {
    if (dir < 0 || dir >= RM_DIR_N) return "";
    return RM_DIR_WORD[dir];
}

/*----------------------
 | room_model_has_word
 | Description: Whether the story's dictionary accepts `text`, comparing only
 |   the first six characters because a v3 entry holds four text bytes, which is
 |   six Z-characters -- the parser cannot tell longer words apart either. Exists
 |   so the verb filter works on Hard, where no typeahead trie is built at all.
 | Author: suinevere
 | Dependencies: dict_entry_len, dict_count, dict_first, decode_word
 | Globals: g_available, g_dict
 | Params: text -- the word to look for
 | Returns: 1 if present, 0 otherwise
 ----------------------*/
int room_model_has_word(const char *text) {
    unsigned int elen, n, k, first;
    if (!g_available || text == 0) return 0;
    elen = dict_entry_len(); n = dict_count(); first = dict_first();
    for (k = 0; k < n; k++) {
        char w[8];
        int j = 0;
        decode_word(first + k * elen, w);
        while (j < 6 && text[j] && w[j] && text[j] == w[j]) j++;
        if (w[j] == '\0' && (text[j] == '\0' || j == 6)) return 1;
    }
    return 0;
}
