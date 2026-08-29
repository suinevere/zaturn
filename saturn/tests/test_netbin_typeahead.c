/*----------------------
 | test_netbin_typeahead.c
 | Description: Host test that the story the netbin embeds yields a usable trie:
 |   the solution overlay recognises it, and a prefix predicts a real Zork I word.
 |   Reads the story from the disc copy rather than the generated array so the test
 |   needs no NETBIN compile; test_netbin_story_pin.py pins the two to the same
 |   bytes.
 |
 |   Also proves the trim tools/trim_z3_vocab.py applies is lossless for the
 |   typeahead: the netbin embeds only the bytes below the header's base of high
 |   memory, so this builds a second trie from that truncation and requires the
 |   same vocabulary, the same overlay verdict and the same ranked predictions.
 |   That equivalence is the whole justification for dropping 63 KB of Z-code and
 |   text out of the image, and it is what would break first if the extractor
 |   ever started following a pointer into high memory.
 | Author: suinevere
 | Build: gcc -std=c11 -Wall -o /tmp/tnt.exe saturn/tests/test_netbin_typeahead.c \
 |          saturn/src/input/typeahead.c saturn/src/input/typeahead_extract.c \
 |          saturn/src/input/typeahead_solution_zork1.c \
 |          -I saturn/src/input && /tmp/tnt.exe saturn/cd/data/Z3/ZORK1.Z3
 ----------------------*/
#include "typeahead.h"
#include "typeahead_extract.h"
#include "typeahead_solution.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOCAB_MAX 8000

void *typeahead_malloc(unsigned int n) { return malloc(n); }
void  typeahead_free(void *p) { free(p); }

static TrieNode *build(const unsigned char *story, unsigned int len, int *overlay) {
    TrieNode *root = create_trie_node();
    build_typeahead_from_story(root, story, len);
    *overlay = apply_solution_overlay(root, story, len);
    typeahead_add_abbreviations(root);
    return root;
}

static void collect(TrieNode *node, char *buf, int depth, char **words, int *n) {
    TrieNode *c;
    if (node == NULL) return;
    if (node->word_data != NULL && *n < VOCAB_MAX) {
        buf[depth] = '\0';
        words[(*n)++] = strdup(buf);
    }
    for (c = node->first_child; c != NULL; c = c->next_sibling) {
        buf[depth] = c->letter;
        collect(c, buf, depth + 1, words, n);
    }
}

static int cmp(const void *a, const void *b) {
    return strcmp(*(char *const *) a, *(char *const *) b);
}

static int vocab(TrieNode *root, char **words) {
    char buf[64];
    int n = 0;
    collect(root, buf, 0, words, &n);
    qsort(words, (size_t) n, sizeof(char *), cmp);
    return n;
}

int main(int argc, char **argv) {
    FILE *f;
    long n;
    unsigned char *story;
    unsigned int himem;
    TrieNode *root, *trimmed;
    DictionaryWord *out[8], *alt[8];
    static char *vf[VOCAB_MAX], *vt[VOCAB_MAX];
    int got, i, k, nf, nt, overlay = 0, trim_overlay = 0, found_lamp = 0;
    const char *prefixes[] = { "lam", "ta", "op", "no", "g", "tr", "s", "dr" };

    assert(argc == 2);
    f = fopen(argv[1], "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    story = (unsigned char *) malloc((size_t) n);
    assert(fread(story, 1, (size_t) n, f) == (size_t) n);
    fclose(f);

    root = build(story, (unsigned int) n, &overlay);
    assert(overlay == 1);

    assert(find_exact_word(root, "lamp") != NULL);
    assert(find_exact_word(root, "north") != NULL);

    got = predict_candidates(root, NULL, "lam", out, 8, 0);
    assert(got > 0);
    for (i = 0; i < got; i++)
        if (strcmp(out[i]->text, "lamp") == 0) found_lamp = 1;
    assert(found_lamp);

    /* The trim the netbin ships must change nothing the typeahead can see. */
    assert(story[0] == 3);
    himem = ((unsigned int) story[0x04] << 8) | story[0x05];
    assert(himem > 0x40 && himem < (unsigned int) n);

    trimmed = build(story, himem, &trim_overlay);
    assert(trim_overlay == overlay);

    nf = vocab(root, vf);
    nt = vocab(trimmed, vt);
    assert(nf == nt);
    for (i = 0; i < nf; i++)
        assert(strcmp(vf[i], vt[i]) == 0);

    for (k = 0; k < (int) (sizeof(prefixes) / sizeof(prefixes[0])); k++) {
        int a = predict_candidates(root,    NULL, prefixes[k], out, 8, 0);
        int b = predict_candidates(trimmed, NULL, prefixes[k], alt, 8, 0);
        assert(a == b);
        for (i = 0; i < a; i++)
            assert(strcmp(out[i]->text, alt[i]->text) == 0);
    }

    printf("ok (%d words, identical from %ld and %u bytes)\n", nf, n, himem);
    return 0;
}
