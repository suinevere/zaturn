/*----------------------
 | test_typeahead_oom.c
 | Description: Host test for the typeahead trie's behaviour when its allocator
 |   runs dry. The trie is the client's largest live allocation (89-318 KB across
 |   the shipped stories), so an out-of-room build is a real state, not a
 |   theoretical one: every allocation site must hand back NULL and every caller
 |   must degrade to "fewer suggestions" rather than write through it. Supplies its
 |   own capped typeahead_malloc/typeahead_free -- the allocator hooks typeahead.c
 |   links against -- so no Saturn or SRL code is involved.
 | Author: suinevere
 | Dependencies: ../src/input/typeahead.h and typeahead.c, assert.h, stdio.h
 | Build: gcc -std=c11 -Wall -Wextra -I src/input tests/test_typeahead_oom.c
 |          src/input/typeahead.c -o /tmp/test_typeahead_oom && /tmp/test_typeahead_oom
 ----------------------*/
#include "typeahead.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The allocator under test: hands out at most g_cap bytes in total, then NULL. */
static unsigned long g_cap = 0, g_used = 0;

void* typeahead_malloc(unsigned int size) {
    if (g_used + size > g_cap) return NULL;
    g_used += size;
    return malloc(size);
}

void typeahead_free(void* ptr) { free(ptr); }

static void set_cap(unsigned long cap) { g_cap = cap; g_used = 0; }

/* A vocabulary big enough that a small cap truncates it part-way. */
static void build_vocab(TrieNode* root) {
    static const char* words[] = {
        "take", "takes", "taken", "talk", "tall", "lamp", "lantern", "lock",
        "north", "northeast", "northwest", "nod", "open", "opens", "operate",
        "sword", "swim", "switch", "mailbox", "mail", "map", "examine", "east"
    };
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        insert_trie(root, create_word(words[i], TYPE_NOUN, 10));
    }
}

/*----------------------
 | test_no_cap_builds_and_finds
 | Description: The baseline -- with room to spare the trie behaves exactly as it
 |   always did, so the NULL guards cost nothing in the normal case.
 ----------------------*/
static void test_no_cap_builds_and_finds(void) {
    set_cap(1000000);
    TrieNode* root = create_trie_node();
    assert(root != NULL);
    build_vocab(root);

    assert(find_exact_word(root, "lantern") != NULL);
    assert(find_exact_word(root, "northeast") != NULL);
    assert(find_exact_word(root, "nosuchword") == NULL);

    DictionaryWord* out[8];
    assert(predict_candidates(root, NULL, "ta", out, 8, 1) > 0);

    destroy_typeahead(root);
}

/*----------------------
 | test_exhausted_allocator_yields_null_root
 | Description: With nothing to give, create_trie_node reports NULL instead of
 |   returning an unwritable pointer, and every read path accepts that root -- the
 |   prompt simply offers no suggestions, as it does on Hard difficulty.
 ----------------------*/
static void test_exhausted_allocator_yields_null_root(void) {
    set_cap(0);
    TrieNode* root = create_trie_node();
    assert(root == NULL);

    /* None of these may dereference the null root. */
    insert_trie(root, NULL);
    typeahead_set_screen(root, "west of house mailbox");
    assert(find_exact_word(root, "lamp") == NULL);

    DictionaryWord* out[8];
    assert(predict_candidates(root, NULL, "la", out, 8, 1) == 0);
    assert(predict_candidates(root, NULL, "", out, 8, 0) == 0);

    destroy_typeahead(root);   /* NULL is a no-op */
}

/*----------------------
 | test_partial_build_survives_and_stays_usable
 | Description: The case the shipped library actually hits -- room for some of the
 |   vocabulary but not all. The build must finish, the words that made it in must
 |   still be findable, and the trie must still free cleanly.
 ----------------------*/
static void test_partial_build_survives_and_stays_usable(void) {
    /* Enough for the root and a first handful of words, then dry. */
    set_cap(300);
    TrieNode* root = create_trie_node();
    assert(root != NULL);
    build_vocab(root);

    /* Whatever survived must be coherent: found words carry their own text. */
    int found = 0;
    static const char* probes[] = { "take", "lamp", "north", "sword", "east" };
    for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        DictionaryWord* w = find_exact_word(root, probes[i]);
        if (w != NULL) { assert(strcmp(w->text, probes[i]) == 0); found++; }
    }
    assert(found < 5);   /* the cap really did bite */

    DictionaryWord* out[8];
    (void) predict_candidates(root, NULL, "ta", out, 8, 1);
    typeahead_set_screen(root, "lamp sword mailbox");

    destroy_typeahead(root);
}

/*----------------------
 | test_create_word_reports_failure
 | Description: create_word hands back NULL rather than a half-built word, and
 |   insert_trie accepts that NULL -- which is what makes the common
 |   insert_trie(root, create_word(...)) one-liner safe.
 ----------------------*/
static void test_create_word_reports_failure(void) {
    set_cap(1000000);
    TrieNode* root = create_trie_node();

    set_cap(0);
    DictionaryWord* w = create_word("lamp", TYPE_NOUN, 10);
    assert(w == NULL);
    insert_trie(root, w);                        /* must not crash */
    assert(find_exact_word(root, "lamp") == NULL);

    set_cap(1000000);
    destroy_typeahead(root);
}

/*----------------------
 | test_links_decline_instead_of_crashing
 | Description: add_next_word drops the association when either end is NULL or the
 |   link cannot be allocated, and add_solution_link then marks nothing -- in
 |   particular not the previous head, which belongs to somebody else's plain link.
 ----------------------*/
static void test_links_decline_instead_of_crashing(void) {
    set_cap(1000000);
    DictionaryWord* a = create_word("take", TYPE_VERB, 10);
    DictionaryWord* b = create_word("lamp", TYPE_NOUN, 10);
    assert(a != NULL && b != NULL);

    add_next_word(a, b, 5);
    assert(a->next_words != NULL);
    assert(a->next_words->solution == 0);
    NextWordLink* head = a->next_words;

    /* A NULL end is dropped, not dereferenced. */
    add_next_word(a, NULL, 5);
    add_next_word(NULL, b, 5);
    add_solution_link(NULL, b, 5);
    assert(a->next_words == head);

    /* Out of room: the solution link is not added, and the existing plain link
       must NOT be relabelled as a winning-path link. */
    set_cap(0);
    add_solution_link(a, b, 5);
    assert(a->next_words == head);
    assert(head->solution == 0);

    set_cap(1000000);
    typeahead_free(head);
    typeahead_free(a->text); typeahead_free(a);
    typeahead_free(b->text); typeahead_free(b);
}

int main(void) {
    test_no_cap_builds_and_finds();
    test_exhausted_allocator_yields_null_root();
    test_partial_build_survives_and_stays_usable();
    test_create_word_reports_failure();
    test_links_decline_instead_of_crashing();
    printf("test_typeahead_oom: OK\n");
    return 0;
}
