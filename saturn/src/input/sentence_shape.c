/*----------------------
 | sentence_shape.c
 | Description: The table and lookups described in sentence_shape.h.
 | Author: suinevere
 | Dependencies: sentence_shape.h, typeahead.h, string.h
 ----------------------*/
#include "sentence_shape.h"
#include <string.h>

/*----------------------
 | SH_VERBS_MAX / SH_TOTAL_ROWS_MAX / FL_VERB / FL_PREP
 | Description: The table's fixed bounds and the two dictionary flag bits this
 |   file reads. SH_VERBS_MAX is measured by verb *spelling*, not dictionary id
 |   -- synonyms share one id, and the worst shipped story (LEATHERG.Z3) has 359
 |   distinct spellings -- with room above that. SH_TOTAL_ROWS_MAX stays far
 |   below what 384 spellings could otherwise demand because every spelling that
 |   shares an id also shares its row block rather than copying it; see
 |   ShapeVerb.
 | Author: suinevere
 ----------------------*/
#define SH_VERBS_MAX      384
#define SH_TOTAL_ROWS_MAX 512
#define FL_VERB 0x40
#define FL_PREP 0x08

/*----------------------
 | ShapeVerb
 | Description: One verb spelling's slice of the row array, keyed by spelling
 |   since that is what the panel's line holds. Every spelling of one verb id
 |   resolves through the same grammar-table address, so a spelling whose id
 |   already has an entry points `first`/`count` at that entry's rows and copies
 |   nothing -- two entries sharing a row block is the dedupe working, not a
 |   bug, and is what keeps SH_TOTAL_ROWS_MAX sufficient for 384 verb entries.
 | Author: suinevere
 ----------------------*/
typedef struct {
    char text[8];
    unsigned char id;
    unsigned short first;
    unsigned char count;
} ShapeVerb;

/*----------------------
 | ShapeTable
 | Description: The whole owned table: the verbs, their rows end to end, and
 |   the canonical word per preposition dictionary id.
 | Author: suinevere
 ----------------------*/
typedef struct {
    ShapeVerb verb[SH_VERBS_MAX];
    ShapeRow  row[SH_TOTAL_ROWS_MAX];
    DictionaryWord *prep[256];
    int nverb;
    int nrow;
} ShapeTable;

/*----------------------
 | g_shape
 | Description: The module's one table, null until shape_build succeeds.
 | Author: suinevere
 ----------------------*/
static ShapeTable *g_shape = 0;

/*----------------------
 | sh_rd16
 | Description: A big-endian word from the story image.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- story bytes; a -- byte offset
 | Returns: the word
 ----------------------*/
static unsigned int sh_rd16(const unsigned char *s, unsigned int a) {
    return ((unsigned int) s[a] << 8) | s[a + 1];
}

/*----------------------
 | sh_dict_word
 | Description: Decodes a dictionary entry's four text bytes through the A0
 |   alphabet, which is all a dictionary word uses, into `out` (at least 7
 |   bytes) with leading and trailing padding removed -- z-chars 0..5 all map
 |   to a0's space, so a word encoded with a shift/abbreviation z-char (e.g.
 |   the contraction "'ve") decodes to embedded and leading spaces under this
 |   A0-only scheme, and the oracle's str.strip() trims both ends, not just
 |   the trailing pad this file used to strip alone.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- story bytes; off -- the entry's offset; out -- receives the text
 | Returns: N/A
 ----------------------*/
static void sh_dict_word(const unsigned char *s, unsigned int off, char *out) {
    static const char a0[] = "      abcdefghijklmnopqrstuvwxyz";
    char buf[7];
    int n = 0, half, k, start, m;
    for (half = 0; half < 2; half++) {
        unsigned int x = sh_rd16(s, off + (unsigned int) half * 2);
        int z[3];
        z[0] = (int) ((x >> 10) & 31);
        z[1] = (int) ((x >> 5) & 31);
        z[2] = (int) (x & 31);
        for (k = 0; k < 3; k++) buf[n++] = a0[z[k]];
    }
    buf[n] = '\0';
    while (n > 0 && buf[n - 1] == ' ') buf[--n] = '\0';
    start = 0;
    while (buf[start] == ' ') start++;
    m = 0;
    while (buf[start] != '\0') out[m++] = buf[start++];
    out[m] = '\0';
}

/*----------------------
 | sh_find_id
 | Description: Finds an already-built verb entry with the given dictionary id,
 |   so a later spelling of the same verb can share its row block instead of
 |   re-decoding it. See ShapeVerb.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: t -- the table being built; id -- the verb dictionary id
 | Returns: index of the matching entry, or -1
 ----------------------*/
static int sh_find_id(const ShapeTable *t, unsigned char id) {
    int i;
    for (i = 0; i < t->nverb; i++)
        if (t->verb[i].id == id) return i;
    return -1;
}

/*----------------------
 | shape_destroy
 | Description: The null check makes double-free and destroy-before-build
 |   both safe no-ops.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_shape
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void shape_destroy(void) {
    if (g_shape == 0) return;
    TYPEAHEAD_FREE(g_shape);
    g_shape = 0;
}

/*----------------------
 | sh_verb_addr
 | Description: Resolves one verb's grammar-table row address, bounding the
 |   address of the fetch (`addr + 1 < len`) before dereferencing it and, only
 |   then, the value fetched (inside static memory) -- the two are different
 |   things to guard, since `255 - id` spans 0..255 and can point the read up
 |   to 510 bytes past `base`, past the value that read would have to yield to
 |   pass the second check. Both sh_grammar_malformed and shape_build's build
 |   pass call this rather than repeat the checks, so the two cannot drift
 |   apart from each other.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: story -- story bytes; len -- its length; base -- static memory
 |   base; id -- the verb's dictionary id; out -- receives the row address
 | Returns: 1 and *out set if the address is valid, 0 otherwise
 ----------------------*/
static int sh_verb_addr(const unsigned char *story, unsigned int len, unsigned int base,
                         unsigned char id, unsigned int *out) {
    unsigned int addr = base + 2 * (255u - id);
    unsigned int a;
    if (addr + 1 >= len) return 0;
    a = sh_rd16(story, addr);
    if (a < base || a >= len) return 0;
    *out = a;
    return 1;
}

/*----------------------
 | sh_grammar_malformed
 | Description: Amendment-3-turned-story-level: walks every verb-flagged
 |   dictionary entry, resolves its grammar address through sh_verb_addr and
 |   the row-count guard (1..12), and reads its rows. True the moment any row
 |   in any entry has nobj > 2. The grammar table's format is a property of
 |   the file, not of one verb -- a file whose grammar address holds
 |   something else does not become half-readable just because some entries
 |   happen to decode to a legal value by chance (measured: 37 of
 |   MZORKI2.Z3's 206 verb entries and 30 of MZORKII.Z3's 180 survive a
 |   per-verb-only check, every one of them an nobj==0 row that is not real
 |   grammar). One bad row anywhere means the file is not in this format at
 |   all, so shape_build refuses the whole story rather than keep the entries
 |   that happened to look legal.
 | Author: suinevere
 | Dependencies: sh_verb_addr
 | Globals: N/A
 | Params: story -- story bytes; len -- its length; base -- static memory
 |   base; p -- dictionary entries start; entry_len -- bytes per entry;
 |   count -- entry count
 | Returns: 1 if any verb's grammar entry contains an impossible row, 0
 |   otherwise
 ----------------------*/
static int sh_grammar_malformed(const unsigned char *story, unsigned int len, unsigned int base,
                                 unsigned int p, int entry_len, int count) {
    int k;
    for (k = 0; k < count; k++) {
        unsigned int off = p + (unsigned int) k * (unsigned int) entry_len;
        unsigned int a;
        int nrows, e;
        if (off + 6 > len) break;
        if ((story[off + 4] & FL_VERB) == 0) continue;
        if (!sh_verb_addr(story, len, base, story[off + 5], &a)) continue;
        nrows = story[a];
        if (nrows < 1 || nrows > SHAPE_ROWS_MAX) continue;
        for (e = 0; e < nrows; e++) {
            unsigned int r = a + 1 + (unsigned int) e * 8;
            if (r + 4 >= len) break;
            if (story[r] > 2) return 1;
        }
    }
    return 0;
}

/*----------------------
 | shape_build
 | Description: Bounds every header-derived offset before it is dereferenced,
 |   then validates every verb's grammar entry before allocating anything, so
 |   a malformed or truncated story is refused whole rather than partially
 |   built.
 | Author: suinevere
 | Dependencies: sh_rd16, sh_grammar_malformed, sh_verb_addr, sh_dict_word,
 |   sh_find_id
 | Globals: g_shape
 | Params: story -- story bytes; len -- its length; root -- the trie already
 |   built from the same story
 | Returns: N/A
 ----------------------*/
void shape_build(const unsigned char *story, unsigned int len, TrieNode *root) {
    unsigned int dict_addr, base, p;
    int nsep, entry_len, count, k;

    shape_destroy();
    if (story == 0 || root == 0 || len < 0x40 || story[0] != 3) return;

    dict_addr = sh_rd16(story, 0x08);
    base = sh_rd16(story, 0x0e);
    if (dict_addr >= len) return;
    p = dict_addr;
    nsep = story[p];
    p += 1 + (unsigned int) nsep;
    if (p >= len) return;
    entry_len = story[p];
    p += 1;
    if (entry_len < 6) return;
    if (p + 1 >= len) return;
    count = (int) sh_rd16(story, p);
    p += 2;

    if (sh_grammar_malformed(story, len, base, p, entry_len, count)) return;

    g_shape = (ShapeTable *) TYPEAHEAD_MALLOC((unsigned int) sizeof(ShapeTable));
    if (g_shape == 0) return;
    memset(g_shape, 0, sizeof(ShapeTable));

    for (k = 0; k < count; k++) {
        unsigned int off = p + (unsigned int) k * (unsigned int) entry_len;
        char text[12];
        if (off + 6 > len) break;
        if ((story[off + 4] & FL_PREP) == 0) continue;
        sh_dict_word(story, off, text);
        if (text[0] == '\0') continue;
        if (g_shape->prep[story[off + 5]] == 0)
            g_shape->prep[story[off + 5]] = find_exact_word(root, text);
    }

    for (k = 0; k < count; k++) {
        unsigned int off = p + (unsigned int) k * (unsigned int) entry_len;
        unsigned int a;
        int nrows, e, dup, actual;
        unsigned char id;
        char text[12];
        ShapeRow tmp[SHAPE_ROWS_MAX];
        int valid;
        if (off + 6 > len) break;
        if ((story[off + 4] & FL_VERB) == 0) continue;
        if (g_shape->nverb >= SH_VERBS_MAX) break;
        sh_dict_word(story, off, text);
        if (text[0] == '\0') continue;
        id = story[off + 5];

        dup = sh_find_id(g_shape, id);
        if (dup >= 0) {
            ShapeVerb *v = &g_shape->verb[g_shape->nverb];
            strncpy(v->text, text, sizeof v->text - 1);
            v->text[sizeof v->text - 1] = '\0';
            v->id = id;
            v->first = g_shape->verb[dup].first;
            v->count = g_shape->verb[dup].count;
            g_shape->nverb++;
            continue;
        }

        if (!sh_verb_addr(story, len, base, id, &a)) continue;
        nrows = story[a];
        if (nrows < 1 || nrows > SHAPE_ROWS_MAX) continue;

        valid = 1;
        actual = 0;
        for (e = 0; e < nrows; e++) {
            unsigned int r = a + 1 + (unsigned int) e * 8;
            if (r + 4 >= len) break;
            tmp[actual].nobj  = story[r];
            tmp[actual].prep1 = story[r + 1];
            tmp[actual].prep2 = story[r + 2];
            tmp[actual].attr1 = story[r + 3];
            tmp[actual].attr2 = story[r + 4];
            if (tmp[actual].nobj > 2) valid = 0;
            actual++;
        }
        if (!valid || actual == 0) continue;
        if (g_shape->nrow + actual > SH_TOTAL_ROWS_MAX) break;

        {
            ShapeVerb *v = &g_shape->verb[g_shape->nverb];
            int j;
            strncpy(v->text, text, sizeof v->text - 1);
            v->text[sizeof v->text - 1] = '\0';
            v->id = id;
            v->first = (unsigned short) g_shape->nrow;
            v->count = (unsigned char) actual;
            for (j = 0; j < actual; j++) g_shape->row[g_shape->nrow++] = tmp[j];
            g_shape->nverb++;
        }
    }
}

/*----------------------
 | sh_eq6
 | Description: True when `a` and `b` agree on their first six characters, or
 |   both end before six -- the precision a v3 dictionary entry actually holds
 |   and what the story's own parser compares. shape_verb_rows must match on
 |   this rather than full equality: typeahead_extract.c's full-word recovery
 |   rewrites a six-character dictionary form to a longer object name whenever
 |   the two share their first six characters, so the panel's assembled line
 |   can carry a spelling longer than anything this table stores. Guards both
 |   pointers: shape_next walks caller-supplied `picked` entries this file
 |   never allocated, on hardware with no memory protection to turn a null
 |   dereference into a clean crash.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a -- first spelling, may be null; b -- second spelling, may be null
 | Returns: 1 if they match to six characters, 0 otherwise (including either
 |   being null)
 ----------------------*/
static int sh_eq6(const char *a, const char *b) {
    int i;
    if (a == 0 || b == 0) return 0;
    for (i = 0; i < 6; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == '\0') return 1;
    }
    return 1;
}

/*----------------------
 | shape_verb_rows
 | Description: A linear scan over the built table's at-most few hundred
 |   entries, matched via sh_eq6 rather than exact equality.
 | Author: suinevere
 | Dependencies: sh_eq6
 | Globals: g_shape
 | Params: verb -- spelling to look up, may be null; out -- receives the
 |   rows; max -- out's capacity
 | Returns: rows written, 0 when the verb is unknown or has no entry
 ----------------------*/
int shape_verb_rows(const char *verb, ShapeRow *out, int max) {
    int i, n;
    if (g_shape == 0 || verb == 0 || out == 0 || max <= 0) return 0;
    for (i = 0; i < g_shape->nverb; i++) {
        if (!sh_eq6(g_shape->verb[i].text, verb)) continue;
        n = g_shape->verb[i].count;
        if (n > max) n = max;
        for (int j = 0; j < n; j++) out[j] = g_shape->row[g_shape->verb[i].first + j];
        return n;
    }
    return 0;
}

/*----------------------
 | sh_is_prep
 | Description: Whether `word` is the canonical spelling of preposition id `id`,
 |   compared to six characters via sh_eq6 rather than strcmp -- the panel's
 |   assembled line can carry a spelling typeahead_extract.c's full-word
 |   recovery has lengthened past what the dictionary entry holds, the same
 |   reason shape_verb_rows does not use exact equality either.
 | Author: suinevere
 | Dependencies: sh_eq6
 | Globals: g_shape
 | Params: id -- a preposition dictionary id; word -- the word picked
 | Returns: 1 on a match, 0 otherwise
 ----------------------*/
static int sh_is_prep(unsigned char id, const char *word) {
    DictionaryWord *w;
    if (id == 0 || g_shape == 0) return 0;
    w = g_shape->prep[id];
    return (w != 0 && sh_eq6(w->text, word));
}

/*----------------------
 | sh_prep_known
 | Description: Whether preposition id `id` resolved to an actual word during
 |   shape_build. shape_build leaves an id unresolved when its FL_PREP entry
 |   decodes to empty text or find_exact_word returns null, and does not
 |   reject the row that references it -- so a row's own nonzero prep1/prep2
 |   byte is not proof the panel can ever show that preposition, and the
 |   decision to want a preposition must check this, not just the byte.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_shape
 | Params: id -- a preposition dictionary id
 | Returns: 1 if the id resolved to a word, 0 otherwise
 ----------------------*/
static int sh_prep_known(unsigned char id) {
    return (id != 0 && g_shape != 0 && g_shape->prep[id] != 0);
}

/*----------------------
 | sh_add_prep
 | Description: Adds preposition id `id`'s canonical word to the answer, once.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_shape
 | Params: out -- the answer being built; id -- the preposition id
 | Returns: N/A
 ----------------------*/
static void sh_add_prep(ShapeSlot *out, unsigned char id) {
    DictionaryWord *w;
    int i;
    if (id == 0 || g_shape == 0 || out->nprep >= SHAPE_PREP_MAX) return;
    w = g_shape->prep[id];
    if (w == 0) return;
    for (i = 0; i < out->nprep; i++) if (out->prep[i] == w) return;
    out->prep[out->nprep++] = w;
}

/*----------------------
 | shape_next
 | Description: Walks each row through a five-state machine (0: nothing taken;
 |   1: prep1 matched, object1 due; 2: object1 done; 3: prep2 matched, object2
 |   due; 4: object2 done) one picked word at a time, dying the moment a word
 |   fits none of the current state's transitions -- including a word offered
 |   past a row's own object2. A row whose preposition was not picked still
 |   consumes that position as an object (state 0/2's second branch), which is
 |   what lets `look lamp` match the `(1,1,0)` row's object slot the same as
 |   `look at lamp` does. Survivors report what their resting state wants;
 |   the union wins noun over preposition over end, so `put`, whose every
 |   one-object row is itself preposition-led, still asks for a noun first.
 | Author: suinevere
 | Dependencies: shape_verb_rows, sh_is_prep, sh_add_prep
 ----------------------*/
void shape_next(const char *const *picked, int npicked, ShapeSlot *out) {
    ShapeRow rows[SHAPE_ROWS_MAX];
    int nrows, i, want_prep = 0, want_noun = 0, want_end = 0;

    if (out == 0) return;
    out->kind = SHAPE_FREE;
    out->nprep = 0;
    if (picked == 0 || npicked < 1) return;
    nrows = shape_verb_rows(picked[0], rows, SHAPE_ROWS_MAX);
    if (nrows == 0) return;

    for (i = 0; i < nrows; i++) {
        const ShapeRow *r = &rows[i];
        int at = 1, state = 0, alive = 1;
        while (alive && at < npicked) {
            const char *word = picked[at];
            switch (state) {
                case 0:
                    if (r->nobj >= 1 && r->prep1 != 0 && sh_is_prep(r->prep1, word)) state = 1;
                    else if (r->nobj >= 1) state = 2;
                    else alive = 0;
                    break;
                case 1:
                    if (r->nobj >= 1) state = 2;
                    else alive = 0;
                    break;
                case 2:
                    if (r->nobj >= 2 && r->prep2 != 0 && sh_is_prep(r->prep2, word)) state = 3;
                    else if (r->nobj >= 2) state = 4;
                    else alive = 0;
                    break;
                case 3:
                    if (r->nobj >= 2) state = 4;
                    else alive = 0;
                    break;
                default:
                    alive = 0;
                    break;
            }
            at++;
        }
        if (!alive) continue;
        switch (state) {
            case 0:
                if (r->nobj >= 1) {
                    if (r->prep1 != 0 && sh_prep_known(r->prep1)) { want_prep = 1; sh_add_prep(out, r->prep1); }
                    else want_noun = 1;
                } else {
                    want_end = 1;
                }
                break;
            case 1:
                want_noun = 1;
                break;
            case 2:
                if (r->nobj >= 2) {
                    if (r->prep2 != 0 && sh_prep_known(r->prep2)) { want_prep = 1; sh_add_prep(out, r->prep2); }
                    else want_noun = 1;
                } else {
                    want_end = 1;
                }
                break;
            case 3:
                want_noun = 1;
                break;
            default:
                want_end = 1;
                break;
        }
    }

    if (want_noun) out->kind = SHAPE_NOUN;
    else if (want_prep) out->kind = SHAPE_PREP;
    else if (want_end) out->kind = SHAPE_END;
    else out->kind = SHAPE_FREE;
}
