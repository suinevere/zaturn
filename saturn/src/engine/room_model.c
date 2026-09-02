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
static unsigned int g_dict, g_obj, g_glob, g_abbr;
static int g_prop[RM_DIR_N];
static int g_available;

/*----------------------
 | g_model
 | Description: The snapshot the last refresh produced.
 | Author: suinevere
 ----------------------*/
static RoomModel g_model;

/*----------------------
 | g_player / g_player_firm / g_prev_room / g_prev_kids / g_prev_kidn / g_prev_n
 | Description: The identified player object, how it was identified, and the
 |   previous room with its child set and each of those children's own child
 |   count, kept so consecutive prompts can be compared. The child counts are
 |   what let a pick-up be seen without a room change; the child set itself is
 |   what a room change intersects. g_player_firm is set only by that
 |   intersection, which cannot be wrong about what followed the player out of
 |   a room, so the pick-up answer stays provisional and the intersection
 |   overrides it the moment it can speak.
 | Author: suinevere
 ----------------------*/
static unsigned short g_player;
static int g_player_firm;
static unsigned short g_prev_room;
static unsigned short g_prev_kids[RM_HERE_MAX];
static unsigned char  g_prev_kidn[RM_HERE_MAX];
static int g_prev_n;

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
 | Globals: g_story, g_len, g_dict, g_obj, g_glob, g_prop, g_available,
 |   g_player, g_player_firm, g_prev_room, g_prev_n
 | Params: story -- the live story image; len -- its length in bytes
 | Returns: 1 if the model is available for this story, 0 otherwise
 ----------------------*/
int room_model_bind(const unsigned char *story, unsigned int len) {
    int seen[PROP_MAX + 1];
    int i, top, run, cardinals;
    unsigned int n, k, elen, first;

    g_available = 0;
    for (i = 0; i < RM_DIR_N; i++) g_prop[i] = 0;
    g_player = 0;
    g_player_firm = 0;
    g_prev_room = 0;
    g_prev_n = 0;
    g_story = story; g_len = len;
    if (story == 0 || len < 64u) return 0;

    g_dict = rd16(0x08);
    g_obj  = rd16(0x0a);
    g_glob = rd16(0x0c);
    g_abbr = rd16(0x18);
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

/*----------------------
 | obj_entry
 | Description: A v3 object's 9-byte table entry (four attribute bytes, then
 |   parent, sibling and child, then the two-byte property-table address).
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_obj
 | Params: id -- object number, 1-based
 | Returns: the entry's byte offset
 ----------------------*/
static unsigned int obj_entry(unsigned short id) {
    return g_obj + 62u + ((unsigned int) id - 1u) * 9u;
}

/*----------------------
 | obj_valid
 | Description: Whether an object number's whole 9-byte table entry lies inside
 |   the image. Every walk of the object tree follows numbers read out of the
 |   story, so none of them can be trusted to name a real object.
 | Author: suinevere
 | Dependencies: obj_entry
 | Globals: g_available, g_len
 | Params: id -- object number, 1-based
 | Returns: 1 when the entry is readable, 0 otherwise
 ----------------------*/
static int obj_valid(unsigned short id) {
    if (!g_available || id == 0) return 0;
    return obj_entry(id) + 9u <= g_len;
}

/*----------------------
 | obj_props
 | Description: The address of an object's first property -- past the short
 |   name, whose length in words is the property table's first byte.
 | Author: suinevere
 | Dependencies: rd16, obj_valid, obj_entry
 | Globals: g_story, g_len
 | Params: id -- object number, 1-based
 | Returns: the property list's byte offset, or 0 when unreadable
 ----------------------*/
static unsigned int obj_props(unsigned short id) {
    unsigned int t;
    if (!obj_valid(id)) return 0u;
    t = rd16(obj_entry(id) + 7u);
    if (t == 0u || t + 1u >= g_len) return 0u;
    return t + 1u + 2u * g_story[t];
}

/*----------------------
 | dir_of_prop
 | Description: Which direction index a property number belongs to, or -1 when
 |   it is not a direction property in this story.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_prop
 | Params: prop -- property number
 | Returns: the RM_* index, or -1
 ----------------------*/
static int dir_of_prop(int prop) {
    int i;
    for (i = 0; i < RM_DIR_N; i++) if (g_prop[i] == prop) return i;
    return -1;
}

/*----------------------
 | is_room
 | Description: Whether an object is itself a room -- carries at least one
 |   property whose number is a resolved direction property. Guards a
 |   conditional or door exit's destination byte, which only names a room when
 |   the object it points at is one.
 | Author: suinevere
 | Dependencies: obj_props, dir_of_prop
 | Globals: g_story, g_len
 | Params: id -- object number, 1-based
 | Returns: 1 when id is a room, 0 otherwise
 ----------------------*/
static int is_room(unsigned short id) {
    unsigned int a = obj_props(id);
    if (a == 0u) return 0;
    while (a < g_len && g_story[a] != 0) {
        int size = (int) g_story[a];
        int plen = (size >> 5) + 1;
        if (a + 1u + (unsigned int) plen > g_len) break;
        if (dir_of_prop(size & 31) >= 0) return 1;
        a += 1u + (unsigned int) plen;
    }
    return 0;
}

/*----------------------
 | obj_child / obj_sibling
 | Description: An object's first child and next sibling, from bytes 6 and 5 of
 |   its table entry.
 | Author: suinevere
 | Dependencies: obj_entry
 | Globals: g_story
 | Params: id -- object number
 | Returns: the related object number, or 0
 ----------------------*/
static unsigned short obj_child(unsigned short id) {
    if (!obj_valid(id)) return 0;
    return (unsigned short) g_story[obj_entry(id) + 6u];
}
static unsigned short obj_sibling(unsigned short id) {
    if (!obj_valid(id)) return 0;
    return (unsigned short) g_story[obj_entry(id) + 5u];
}

/*----------------------
 | collect_children
 | Description: Fills `out` with an object's children, up to `max`, returning
 |   how many were written. Bounded by max rather than by the chain so a corrupt
 |   sibling link cannot spin.
 | Author: suinevere
 | Dependencies: obj_child, obj_sibling
 | Globals: N/A
 | Params: parent -- the object to walk; out -- destination; max -- its capacity
 | Returns: the count written
 ----------------------*/
static int collect_children(unsigned short parent, unsigned short *out, int max) {
    unsigned short c = obj_child(parent);
    int n = 0;
    while (c != 0 && n < max) { out[n++] = c; c = obj_sibling(c); }
    return n;
}

/*----------------------
 | room_model_player
 | Description: The object the player inhabits, or 0 while it is still unknown.
 |   There is no specified way to find it, so it is identified by intersecting
 |   the child sets of two consecutive rooms -- only the player follows the
 |   player -- which converges on the first room change. Until then carried items
 |   are simply absent.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_player
 | Params: N/A
 | Returns: the object number, or 0
 ----------------------*/
unsigned short room_model_player(void) { return g_player; }

/*----------------------
 | room_model_object_word
 | Description: An object's first parser synonym, read straight from its own
 |   property list rather than decoded from a short name. ZILCH stores an
 |   object's dictionary words as a synonym property whose data is a run of
 |   2-byte dictionary addresses; the property number varies by game, so it is
 |   detected rather than hardcoded: a property qualifies only when every one
 |   of its 16-bit values lands exactly on a dictionary entry boundary within
 |   the bound dictionary's range. The first qualifying property's first word
 |   is decoded and copied out.
 | Author: suinevere
 | Dependencies: obj_valid, obj_props, rd16, dict_first, dict_entry_len,
 |   dict_count, decode_word
 | Globals: g_story, g_len, g_available
 | Params: obj -- object number; out -- receives up to six characters plus a
 |   NUL; max -- out's capacity
 | Returns: 1 and fills out on success, 0 and empties out otherwise
 ----------------------*/
int room_model_object_word(unsigned short obj, char *out, int max) {
    unsigned int a, lo, hi, elen;
    char text[8];

    if (out != 0 && max > 0) out[0] = '\0';
    if (!g_available || !obj_valid(obj)) return 0;

    a = obj_props(obj);
    if (a == 0u) return 0;

    elen = dict_entry_len();
    lo = dict_first();
    hi = lo + dict_count() * elen;

    while (a < g_len && g_story[a] != 0) {
        int size = (int) g_story[a];
        int plen = (size >> 5) + 1;
        unsigned int base = a + 1u;
        if (base + (unsigned int) plen > g_len) break;
        if (plen >= 2 && (plen % 2) == 0) {
            int nwords = plen / 2;
            int ok = 1, w;
            for (w = 0; w < nwords; w++) {
                unsigned int v = rd16(base + (unsigned int) (w * 2));
                if (!(v >= lo && v < hi && (v - lo) % elen == 0)) { ok = 0; break; }
            }
            if (ok) {
                decode_word(rd16(base), text);
                if (out != 0 && max > 0) {
                    int i = 0;
                    while (text[i] != '\0' && i < max - 1) { out[i] = text[i]; i++; }
                    out[i] = '\0';
                }
                return 1;
            }
        }
        a = base + (unsigned int) plen;
    }
    return 0;
}

/*----------------------
 | RM_A0 / RM_A1 / RM_A2 (ZSCII alphabets)
 | Description: The three Z-machine alphabet tables mapping z-chars 6..31 to
 |   ASCII: lowercase, uppercase, and the punctuation/digit set. A2 index 0
 |   (z-char 6) is the 10-bit ZSCII escape, handled inline in rm_emit_zchars.
 | Author: suinevere
 ----------------------*/
static const char RM_A0[] = "abcdefghijklmnopqrstuvwxyz";
static const char RM_A1[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char RM_A2[] = { 0, '\n', '0', '1', '2', '3', '4', '5', '6', '7',
                               '8', '9', '.', ',', '!', '?', '_', '#', '\'',
                               '"', '/', '\\', '-', ':', '(', ')' };

/*----------------------
 | RM_ZBUF_MAX / RM_ABBR_N / RM_NAME_MAX / RM_TOK_MAX / RM_TOK_LEN
 | Description: Bounds for the short-name decode: the z-char scratch buffer
 |   (room for 31 packed words), the abbreviation table's fixed entry count
 |   (3 sets of 32), the decoded short-name text buffer, and the tokenizer's
 |   maximum token count and per-token length.
 | Author: suinevere
 ----------------------*/
#define RM_ZBUF_MAX  96
#define RM_ABBR_N    96
#define RM_NAME_MAX  64
#define RM_TOK_MAX   12
#define RM_TOK_LEN   16

/*----------------------
 | rm_decode_zstring (forward declaration)
 | Description: Decodes a Z-string at byte address `addr`, appending into
 |   buf[pos..cap) and returning the new position. Declared ahead of
 |   rm_emit_zchars because the two recurse (an abbreviation reference decodes
 |   another string) -- that recursion is capped at one level by always
 |   passing allow_abbr = 0 into the nested call, so a self-referential or
 |   cyclic abbreviation table cannot recurse further.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_story, g_len
 | Params: addr -- byte address of the string; abbr -- abbreviation table
 |   address; allow_abbr -- whether abbreviation references expand; buf/cap --
 |   output; pos -- start position
 | Returns: the new buffer position
 ----------------------*/
static int rm_decode_zstring(unsigned int addr, unsigned int abbr, int allow_abbr,
                              char *buf, int cap, int pos);

/*----------------------
 | rm_emit_zchars
 | Description: Renders a run of z-chars into `buf`, tracking the shift
 |   alphabet (A0/A1/A2), expanding abbreviation references via
 |   rm_decode_zstring (when allowed and in range), and handling the 10-bit
 |   ZSCII escape. Stops at the buffer cap.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_story, g_len, RM_A0, RM_A1, RM_A2
 | Params: zc/n -- the z-chars; abbr -- abbreviation table address; allow_abbr
 |   -- whether references expand; buf/cap -- output; pos -- start position
 | Returns: the new buffer position
 ----------------------*/
static int rm_emit_zchars(const unsigned char *zc, int n, unsigned int abbr,
                           int allow_abbr, char *buf, int cap, int pos) {
    int alpha = 0, i;
    for (i = 0; i < n; i++) {
        unsigned char c = zc[i];
        if (c == 0) { if (pos < cap - 1) buf[pos++] = ' '; alpha = 0; continue; }
        if (c <= 3) {
            if (allow_abbr && i + 1 < n && abbr != 0u) {
                int idx = 32 * (c - 1) + zc[i + 1];
                if (idx >= 0 && idx < RM_ABBR_N) {
                    unsigned int entry = abbr + 2u * (unsigned int) idx;
                    if (entry + 1u < g_len) {
                        unsigned int aa = rd16(entry) * 2u;
                        if (aa + 1u < g_len)
                            pos = rm_decode_zstring(aa, abbr, 0, buf, cap, pos);
                    }
                }
            }
            if (i + 1 < n) i++;
            alpha = 0; continue;
        }
        if (c == 4) { alpha = 1; continue; }
        if (c == 5) { alpha = 2; continue; }
        if (alpha == 2 && c == 6) {
            if (i + 2 < n) {
                int zs = (zc[i + 1] << 5) | zc[i + 2];
                i += 2;
                if (zs >= 32 && zs < 127 && pos < cap - 1) buf[pos++] = (char) zs;
            }
            alpha = 0; continue;
        }
        {
            char ch;
            if (alpha == 0) ch = RM_A0[c - 6];
            else if (alpha == 1) ch = RM_A1[c - 6];
            else ch = RM_A2[c - 6];
            if (pos < cap - 1) buf[pos++] = ch;
            alpha = 0;
        }
    }
    if (cap > 0) buf[pos < cap ? pos : cap - 1] = '\0';
    return pos;
}

/*----------------------
 | rm_decode_zstring
 | Description: Reads the packed z-char words of a Z-string at `addr`
 |   (stopping at the end-bit, the scratch buffer's capacity, or the image
 |   end) and renders them via rm_emit_zchars. The other half of the
 |   rm_decode_zstring/rm_emit_zchars recursion.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_story, g_len
 | Params: addr -- byte address of the string; abbr -- abbreviation table
 |   address; allow_abbr -- whether references expand; buf/cap -- output;
 |   pos -- start position
 | Returns: the new buffer position
 ----------------------*/
static int rm_decode_zstring(unsigned int addr, unsigned int abbr, int allow_abbr,
                              char *buf, int cap, int pos) {
    unsigned char zc[RM_ZBUF_MAX];
    int n = 0;
    unsigned int a = addr;
    while (n <= RM_ZBUF_MAX - 3) {
        unsigned int w;
        if (a + 1u >= g_len) break;
        w = rd16(a); a += 2u;
        zc[n++] = (unsigned char) ((w >> 10) & 0x1fu);
        zc[n++] = (unsigned char) ((w >> 5) & 0x1fu);
        zc[n++] = (unsigned char) (w & 0x1fu);
        if (w & 0x8000u) break;
    }
    return rm_emit_zchars(zc, n, abbr, allow_abbr, buf, cap, pos);
}

/*----------------------
 | obj_name_addr
 | Description: The address of an object's short name -- the Z-string just
 |   past its property table's length byte -- and its length in 2-byte words.
 |   Refuses (returns 0) unless the whole declared span lies inside the bound
 |   image, so a malformed or out-of-range property-table pointer is never
 |   handed to the decoder.
 | Author: suinevere
 | Dependencies: rd16, obj_valid, obj_entry
 | Globals: g_story, g_len
 | Params: id -- object number; out_words -- receives the short name's length
 |   in 2-byte words
 | Returns: the short name's byte address, or 0 when unreadable
 ----------------------*/
static unsigned int obj_name_addr(unsigned short id, unsigned int *out_words) {
    unsigned int t, addr, words;
    *out_words = 0u;
    if (!obj_valid(id)) return 0u;
    t = rd16(obj_entry(id) + 7u);
    if (t == 0u || t >= g_len) return 0u;
    words = g_story[t];
    if (words == 0u) return 0u;
    addr = t + 1u;
    if (addr + 2u * words > g_len) return 0u;
    *out_words = words;
    return addr;
}

/*----------------------
 | obj_short_name
 | Description: Decodes an object's short name into `out`, bounded by
 |   obj_name_addr's span check.
 | Author: suinevere
 | Dependencies: obj_name_addr, rm_decode_zstring
 | Globals: g_abbr
 | Params: id -- object number; out -- receives the text; cap -- its capacity
 | Returns: 1 on success, 0 when the short name cannot be read
 ----------------------*/
static int obj_short_name(unsigned short id, char *out, int cap) {
    unsigned int words, addr;
    if (cap > 0) out[0] = '\0';
    addr = obj_name_addr(id, &words);
    if (addr == 0u) return 0;
    rm_decode_zstring(addr, g_abbr, 1, out, cap, 0);
    return 1;
}

/*----------------------
 | rm_tokenize
 | Description: Splits a decoded short name into lowercase [a-z]+ tokens
 |   (non-letters split tokens), filling tok[][] and returning the token
 |   count. Capped at RM_TOK_MAX tokens of RM_TOK_LEN - 1 characters each.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: name -- the decoded short name; tok -- output token array; maxtok
 |   -- capacity
 | Returns: the number of tokens
 ----------------------*/
static int rm_tokenize(const char *name, char tok[][RM_TOK_LEN], int maxtok) {
    int nt = 0, tp = 0;
    const char *p;
    for (p = name; ; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if (c >= 'a' && c <= 'z') {
            if (tp < RM_TOK_LEN - 1) tok[nt][tp++] = c;
        } else {
            if (tp > 0) { tok[nt][tp] = '\0'; if (++nt >= maxtok) return nt; tp = 0; }
            if (c == '\0') break;
        }
    }
    return nt;
}

/*----------------------
 | room_model_full_word
 | Description: Recovers a fuller spelling for a six-character-truncated
 |   dictionary word by decoding an object's own short name -- the Z-string at
 |   that object's property table, using the full A0/A1/A2 shift alphabets,
 |   the abbreviation table, and the 10-bit ZSCII escape -- and preferring a
 |   token from it whose first six characters match `word`. Display only: the
 |   parser distinguishes six characters and no more, so callers must keep
 |   submitting `word` itself, never this function's result.
 | Author: suinevere
 | Dependencies: obj_valid, obj_short_name, rm_tokenize
 | Globals: g_available
 | Params: obj -- object number; word -- the dictionary word, six characters
 |   or fewer; out -- receives the recovered spelling, or a copy of `word`
 |   when no longer match exists or obj/word cannot be decoded; max -- out's
 |   capacity
 | Returns: 1 when a longer spelling was recovered, 0 otherwise (out is still
 |   filled with a safe copy of `word`)
 ----------------------*/
int room_model_full_word(unsigned short obj, const char *word, char *out, int max) {
    char name[RM_NAME_MAX];
    char tok[RM_TOK_MAX][RM_TOK_LEN];
    int nt, i, wl, j;

    if (out != 0 && max > 0) out[0] = '\0';
    if (word == 0 || out == 0 || max <= 0) return 0;

    wl = 0;
    while (word[wl] != '\0' && wl < 6) wl++;
    for (j = 0; j < wl && j < max - 1; j++) out[j] = word[j];
    out[j] = '\0';

    if (wl != 6) return 0;
    if (!g_available || !obj_valid(obj)) return 0;
    if (!obj_short_name(obj, name, (int) sizeof name)) return 0;

    nt = rm_tokenize(name, tok, RM_TOK_MAX);
    for (i = 0; i < nt; i++) {
        int tl = 0, k, match = 1;
        while (tok[i][tl] != '\0') tl++;
        if (tl <= 6) continue;
        for (k = 0; k < 6; k++) if (tok[i][k] != word[k]) { match = 0; break; }
        if (!match) continue;
        {
            int m;
            for (m = 0; m < tl && m < max - 1; m++) out[m] = tok[i][m];
            out[m] = '\0';
        }
        return 1;
    }
    return 0;
}

/*----------------------
 | here_holds
 | Description: Whether an object is one of the current room's children, read
 |   off the snapshot rather than the story so it costs nothing.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_model
 | Params: obj -- the object to look for
 | Returns: 1 when the room holds it, 0 otherwise
 ----------------------*/
static int here_holds(unsigned short obj) {
    int i;
    if (obj == 0) return 0;
    for (i = 0; i < g_model.nhere; i++) if (g_model.here[i] == obj) return 1;
    return 0;
}

/*----------------------
 | child_count
 | Description: How many children an object has, bounded by RM_HERE_MAX so a
 |   corrupt sibling link cannot spin -- the same bound collect_children takes
 |   and for the same reason.
 | Author: suinevere
 | Dependencies: obj_child, obj_sibling
 | Globals: N/A
 | Params: obj -- the object to count under
 | Returns: the count, saturating at RM_HERE_MAX
 ----------------------*/
static int child_count(unsigned short obj) {
    unsigned short c = obj_child(obj);
    int n = 0;
    while (c != 0 && n < RM_HERE_MAX) { n++; c = obj_sibling(c); }
    return n;
}

/*----------------------
 | player_from_pickup
 | Description: The player object as the last turn gives it away, for a player
 |   who has picked something up without leaving the room -- which is the
 |   ordinary opening move, and until now left the inventory reported empty
 |   until the first room change.
 |
 |   Taking a thing moves it under the player, so of the objects standing in
 |   this room, the one that gained a child since the last prompt is the player.
 |   Putting a thing into a container moves it the other way and would name the
 |   container instead -- but nothing can be put down before something has been
 |   picked up, so the first transfer of the game is always a take, and this is
 |   only ever asked while the player is still unknown.
 |
 |   Refuses rather than guesses when two of the room's objects gained
 |   children in the same turn.
 | Author: suinevere
 | Dependencies: here_holds, child_count
 | Globals: g_prev_kids, g_prev_kidn, g_prev_n
 | Params: N/A
 | Returns: the object number, or 0 when nothing or more than one thing fits
 ----------------------*/
static unsigned short player_from_pickup(void) {
    unsigned short cand = 0;
    int ncand = 0, j;
    for (j = 0; j < g_prev_n; j++) {
        unsigned short k = g_prev_kids[j];
        if (!here_holds(k)) continue;
        if (child_count(k) <= (int) g_prev_kidn[j]) continue;
        cand = k;
        ncand++;
    }
    return (ncand == 1) ? cand : 0;
}

/*----------------------
 | room_model_refresh
 | Description: Reads the current room out of global 0 -- which the v3
 |   specification defines as the room the status line names -- and rebuilds the
 |   snapshot for it. Call once per prompt.
 | Author: suinevere
 | Dependencies: room_model_refresh_room, rd16
 | Globals: g_available, g_glob
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void room_model_refresh(void) {
    unsigned short room;
    if (!g_available) return;
    room = (unsigned short) rd16(g_glob);
    room_model_refresh_room(room);
}

/*----------------------
 | room_model_refresh_room
 | Description: Rebuilds the snapshot for a given room object -- the entry the
 |   host tests drive, and what room_model_refresh calls once it has read the
 |   room out of global 0. Also fills the room's contents, advances the player
 |   heuristic on a room change, and fills carried items once the player is
 |   known.
 | Author: suinevere
 | Dependencies: obj_entry, obj_props, dir_of_prop, collect_children,
 |   player_from_pickup, here_holds, child_count
 | Globals: g_story, g_len, g_available, g_model, g_player, g_player_firm,
 |   g_prev_room, g_prev_kids, g_prev_kidn, g_prev_n
 | Params: room -- the room object number
 | Returns: N/A
 ----------------------*/
/*----------------------
 | g_exits_only
 | Description: Set by room_model_set_exits_only. See room_model.h for why a
 |   static-image client must not report contents or inventory.
 | Author: suinevere
 ----------------------*/
static int g_exits_only = 0;

/*----------------------
 | room_model_set_exits_only / room_model_has_room
 | Description: The mode switch and the "do we know a room yet" test. See
 |   room_model.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_exits_only, g_model
 | Params: on -- nonzero to report exits and nothing else
 | Returns: N/A / nonzero once a room is known
 ----------------------*/
void room_model_set_exits_only(int on) { g_exits_only = on ? 1 : 0; }

int room_model_has_room(void) { return g_model.room != 0; }

void room_model_refresh_room(unsigned short room) {
    unsigned int a;
    int i;

    for (i = 0; i < RM_DIR_N; i++) { g_model.exits[i] = RM_EXIT_NONE; g_model.dest[i] = 0; }
    g_model.nhere = 0;
    g_model.ncarried = 0;
    g_model.room = room;
    if (!g_available || room == 0) return;
    if (obj_entry(room) + 9u > g_len) return;

    a = obj_props(room);
    if (a == 0u) return;
    while (a < g_len && g_story[a] != 0) {
        int size = (int) g_story[a];
        int prop = size & 31;
        int plen = (size >> 5) + 1;
        int dir  = dir_of_prop(prop);
        if (a + 1u + (unsigned int) plen > g_len) break;
        if (dir >= 0) {
            if (plen == 1) {
                g_model.exits[dir] = RM_EXIT_OPEN;
                g_model.dest[dir]  = g_story[a + 1u];
            } else if (plen == 2) {
                g_model.exits[dir] = RM_EXIT_BLOCKED;
            } else {
                g_model.exits[dir] = RM_EXIT_MAYBE;
                if ((plen == 4 || plen == 5) && is_room(g_story[a + 1u]))
                    g_model.dest[dir] = g_story[a + 1u];
            }
        }
        a += 1u + (unsigned int) plen;
    }

    /* Exits are all a client bound to a static image may honestly report; see
       room_model_set_exits_only in room_model.h. */
    if (g_exits_only) return;

    g_model.nhere = collect_children(room, g_model.here, RM_HERE_MAX);

    if (!g_player_firm && g_prev_room != 0 && g_prev_room != room) {
        int j, k, cand = 0, ncand = 0;
        for (j = 0; j < g_prev_n; j++)
            for (k = 0; k < g_model.nhere; k++)
                if (g_prev_kids[j] == g_model.here[k]) { cand = g_prev_kids[j]; ncand++; }
        if (ncand == 1) { g_player = (unsigned short) cand; g_player_firm = 1; }
    }
    /* The intersection needs a room change to speak, and until it does the
       inventory is reported empty -- which the command panel reads as nothing
       to browse, so picking Inventory submitted the typed command instead of
       opening the item list. Picking something up without leaving the room is
       the ordinary opening move, so that is watched for as well; its answer is
       provisional, and is dropped here the moment the object stops being a
       child of the room. */
    if (!g_player_firm) {
        if (!here_holds(g_player)) g_player = 0;
        if (g_player == 0 && g_prev_room == room) g_player = player_from_pickup();
    }
    g_prev_room = room;
    g_prev_n = g_model.nhere;
    for (i = 0; i < g_model.nhere; i++) {
        g_prev_kids[i] = g_model.here[i];
        g_prev_kidn[i] = (unsigned char) child_count(g_model.here[i]);
    }

    if (g_player != 0)
        g_model.ncarried = collect_children(g_player, g_model.carried, RM_CARRIED_MAX);
}

/*----------------------
 | room_model_get
 | Description: The current snapshot. Valid but empty before the first refresh
 |   and whenever the model is unavailable.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_model
 | Params: N/A
 | Returns: the snapshot, never NULL
 ----------------------*/
const RoomModel *room_model_get(void) { return &g_model; }

/*----------------------
 | room_model_dict_count
 | Description: How many entries the story's dictionary holds.
 | Author: suinevere
 | Dependencies: dict_count
 | Globals: g_available
 | Params: N/A
 | Returns: the entry count, or 0 when the model is unavailable
 ----------------------*/
int room_model_dict_count(void) {
    if (!g_available) return 0;
    return (int) dict_count();
}

/*----------------------
 | room_model_dict_word
 | Description: Copies dictionary entry `index`'s text and flag byte.
 | Author: suinevere
 | Dependencies: dict_count, dict_first, dict_entry_len, decode_word
 | Globals: g_story, g_available
 | Params: index -- entry index; out -- receives the text; max -- its
 |   capacity; flags_out -- receives the flag byte, may be null
 | Returns: 1 on success, 0 when unavailable or the index is out of range
 ----------------------*/
int room_model_dict_word(int index, char *out, int max, unsigned char *flags_out) {
    unsigned int off;
    char w[8];
    int i;
    if (max > 0) out[0] = '\0';
    if (!g_available || out == 0 || max <= 0) return 0;
    if (index < 0 || index >= (int) dict_count()) return 0;
    off = dict_first() + (unsigned int) index * dict_entry_len();
    decode_word(off, w);
    for (i = 0; i < max - 1 && w[i]; i++) out[i] = w[i];
    out[i] = '\0';
    if (flags_out != 0) *flags_out = g_story[off + 4];
    return 1;
}

/*----------------------
 | room_model_object_name
 | Description: See room_model.h.
 | Author: suinevere
 | Dependencies: obj_short_name
 | Globals: g_available
 | Params: obj -- object number; out -- receives the name; max -- its capacity
 | Returns: 1 on success, 0 otherwise
 ----------------------*/
int room_model_object_name(unsigned short obj, char *out, int max) {
    if (max > 0) out[0] = '\0';
    if (!g_available || max <= 0) return 0;
    return obj_short_name(obj, out, max);
}
