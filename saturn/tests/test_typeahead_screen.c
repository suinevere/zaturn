/*----------------------
 | test_typeahead_screen.c
 | Description: Host test for screen recency in predict_candidates. The object
 |   slot after a verb must lead with what the last command just printed --
 |   "open mailbox" makes the leaflet the thing to take -- while the room
 |   description still on screen above it (house, door) stays ordinary scenery
 |   ranked below the grammar and winning-path links. Builds its own small trie,
 |   so no story file or Saturn code is involved.
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

static const char* OLDER =
    "West of House. You are standing in an open field west of a white house, "
    "with a boarded front door. There is a small mailbox here.";
static const char* RECENT =
    "Opening the small mailbox reveals a small leaflet.";

static int rank(TrieNode* root, DictionaryWord* prev, const char* prefix,
                const char* want) {
    DictionaryWord* out[24];
    int n = predict_candidates(root, prev, prefix, out, 24, 0);
    for (int i = 0; i < n; i++) if (strcmp(out[i]->text, want) == 0) return i + 1;
    return 0;
}

int main(void) {
    TrieNode* root = create_trie_node();

    DictionaryWord* take = create_word("take", TYPE_VERB, 46);
    DictionaryWord* leaflet = create_word("leaflet", TYPE_NOUN, 42); /* just revealed */
    DictionaryWord* house = create_word("house", TYPE_NOUN, 45);     /* older scenery */
    DictionaryWord* lamp = create_word("lamp", TYPE_NOUN, 48);       /* winning path */
    DictionaryWord* boat = create_word("boat", TYPE_NOUN, 30);       /* grammar link */
    DictionaryWord* ladder = create_word("ladder", TYPE_NOUN, 30);   /* nowhere */
    insert_trie(root, take);
    insert_trie(root, leaflet);
    insert_trie(root, house);
    insert_trie(root, lamp);
    insert_trie(root, boat);
    insert_trie(root, ladder);
    add_next_word(take, boat, 55);
    add_solution_link(take, lamp, 3000);

    typeahead_set_screen_recent(root, OLDER, RECENT);

    /* Normal. The just-revealed object leads; the winning-path object is next;
       the room description's scenery ranks below it, where it already sat. */
    int rl = rank(root, take, "", "leaflet");
    int rlamp = rank(root, take, "", "lamp");
    int rh = rank(root, take, "", "house");
    printf("  normal, 'take ': leaflet %d, lamp %d, house %d\n", rl, rlamp, rh);
    assert(rl == 1);
    assert(rlamp > rl && rh > rlamp);
    assert(rank(root, take, "", "ladder") == 0);

    /* A typed prefix reaches it: the grammar filter must not drop a noun the
       game just put in front of the player, verb link or no. */
    assert(rank(root, take, "l", "leaflet") == 1);
    assert(rank(root, take, "le", "leaflet") == 1);

    /* Easy narrows the hints to the winning path but must not hide it either. */
    typeahead_set_easy(1, 1);
    assert(rank(root, take, "", "leaflet") == 1);
    assert(rank(root, take, "l", "leaflet") == 1);
    assert(rank(root, take, "l", "lamp") == 2);
    assert(rank(root, take, "l", "ladder") == 0);
    typeahead_set_easy(0, 0);

    /* Scenery is reachable when the player spells it out; what it never does is
       lead an object slot the player has not aimed at. */
    assert(rank(root, take, "h", "house") == 1);
    assert(rank(root, take, "l", "house") == 0);

    /* Noun after noun stays an invalid shape however fresh the noun is. */
    assert(rank(root, boat, "l", "leaflet") == 0);

    /* Freshness expires: next turn the leaflet is ordinary screen text -- still
       reachable, no longer leading the winning-path word -- and the turn after
       that it is gone from the screen entirely. */
    typeahead_set_screen_recent(root, RECENT, "Taken.");
    assert(rank(root, take, "l", "lamp") == 1);
    assert(rank(root, take, "l", "leaflet") == 2);
    assert(rank(root, take, "", "leaflet") > rank(root, take, "", "lamp"));
    typeahead_set_screen_recent(root, "You are in an open field.", "Taken.");
    assert(rank(root, take, "", "leaflet") == 0);
    assert(rank(root, take, "l", "leaflet") == 0);

    /* The one-argument form marks a whole screen with no recency: everything on
       it is reachable, nothing is promoted, so the winning-path word still leads. */
    typeahead_set_screen(root, OLDER);
    assert(rank(root, take, "", "lamp") == 1);
    assert(rank(root, take, "", "house") > 1);
    assert(rank(root, take, "h", "house") == 1);

    destroy_typeahead(root);
    printf("test_typeahead_screen ok\n");
    return 0;
}
