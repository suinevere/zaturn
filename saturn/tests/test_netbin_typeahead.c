/*----------------------
 | test_netbin_typeahead.c
 | Description: Host test that the story the netbin embeds yields a usable trie:
 |   the solution overlay recognises it, and a prefix predicts a real Zork I word.
 |   Reads the story from the disc copy rather than the generated array so the test
 |   needs no NETBIN compile; Task 4 already pins the two to the same bytes.
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

void *typeahead_malloc(unsigned int n) { return malloc(n); }
void  typeahead_free(void *p) { free(p); }

int main(int argc, char **argv) {
    FILE *f;
    long n;
    unsigned char *story;
    TrieNode *root;
    DictionaryWord *out[8];
    int got, i, found_lamp = 0;

    assert(argc == 2);
    f = fopen(argv[1], "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    story = (unsigned char *) malloc((size_t) n);
    assert(fread(story, 1, (size_t) n, f) == (size_t) n);
    fclose(f);

    root = create_trie_node();
    build_typeahead_from_story(root, story, (unsigned int) n);
    assert(apply_solution_overlay(root, story, (unsigned int) n) == 1);
    typeahead_add_abbreviations(root);

    assert(find_exact_word(root, "lamp") != NULL);
    assert(find_exact_word(root, "north") != NULL);

    got = predict_candidates(root, NULL, "lam", out, 8, 0);
    assert(got > 0);
    for (i = 0; i < got; i++)
        if (strcmp(out[i]->text, "lamp") == 0) found_lamp = 1;
    assert(found_lamp);

    printf("ok\n");
    return 0;
}
