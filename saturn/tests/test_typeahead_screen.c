/*----------------------
 | test_typeahead_screen.c
 | Description: Host test for the on-screen ("hot") noun path through
 |   predict_candidates. A story's grammar links its verbs to object classes, not
 |   to every object, so most concrete nouns -- Zork's mailbox and leaflet among
 |   them -- have no verb link at all. The suggestion after a verb must still
 |   offer a noun the game has just put on screen, both at the empty object slot
 |   and under a typed prefix, while an off-screen unlinked noun stays filtered
 |   out. Builds its own small trie, so no story file or Saturn code is involved.
 | Author: suinevere
 | Dependencies: ../src/input/typeahead.h and typeahead.c, assert.h, stdio.h
 | Build: gcc -std=c11 -Wall -Wextra -I src/input tests/test_typeahead_screen.c
 |          src/input/typeahead.c -o /tmp/test_typeahead_screen &&
 |          /tmp/test_typeahead_screen
 ----------------------*/
#include "typeahead.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* typeahead_malloc(unsigned int size) { return malloc(size); }
void typeahead_free(void* ptr) { free(ptr); }

static int offers(TrieNode* root, DictionaryWord* prev, const char* prefix,
                  const char* want) {
    DictionaryWord* out[16];
    int n = predict_candidates(root, prev, prefix, out, 16, 0);
    for (int i = 0; i < n; i++) if (strcmp(out[i]->text, want) == 0) return i + 1;
    return 0;
}

int main(void) {
    TrieNode* root = create_trie_node();

    DictionaryWord* take = create_word("take", TYPE_VERB, 46);
    DictionaryWord* boat = create_word("boat", TYPE_NOUN, 30);       /* grammar-linked */
    DictionaryWord* leaflet = create_word("leaflet", TYPE_NOUN, 30); /* on screen only */
    DictionaryWord* lamp = create_word("lamp", TYPE_NOUN, 30);       /* neither */
    insert_trie(root, take);
    insert_trie(root, boat);
    insert_trie(root, leaflet);
    insert_trie(root, lamp);
    add_next_word(take, boat, 55);

    typeahead_set_screen(root, "Opening the small mailbox reveals a small leaflet.");

    /* The empty object slot surfaces the on-screen noun, ahead of the linked
       but unseen one. */
    int rl = offers(root, take, "", "leaflet");
    int rb = offers(root, take, "", "boat");
    printf("  after 'take', empty prefix: leaflet at %d, boat at %d\n", rl, rb);
    assert(rl > 0);
    assert(rb > 0);
    assert(rl < rb);

    /* A typed prefix completes it too. */
    assert(offers(root, take, "l", "leaflet") > 0);
    assert(offers(root, take, "le", "leaflet") > 0);

    /* An off-screen noun the grammar does not link stays filtered out. */
    assert(offers(root, take, "l", "lamp") == 0);
    assert(offers(root, take, "", "lamp") == 0);

    /* Noun after noun is still an invalid shape, on screen or not. */
    assert(offers(root, boat, "l", "leaflet") == 0);

    /* The marks expire with the screen: a later screen without it drops it. */
    typeahead_set_screen(root, "You are standing in an open field.");
    assert(offers(root, take, "l", "leaflet") == 0);

    destroy_typeahead(root);
    printf("test_typeahead_screen ok\n");
    return 0;
}
