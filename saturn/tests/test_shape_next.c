/*----------------------
 | test_shape_next.c
 | Description: Host test for the sentence-shape matching rule, against Zork I's
 |   own grammar: look's preposition-then-object slot, look's second object
 |   preposition arriving only in its own slot and never re-offering the
 |   first, put's every one-object row being itself preposition-led so bare
 |   put opens on noun rather than preposition, put's second-object
 |   preposition staying out of the first object's slot, an unknown verb, and
 |   an empty sentence. take's own (2, -, {from,off,out}) row is real grammar
 |   (used by "take egg from nest"), and under the same noun>prep>end rule
 |   that governs every case here it keeps `take lamp` at SHAPE_PREP, not
 |   SHAPE_END -- this asserts that verified, simulator-checked value rather
 |   than the value first proposed for it. Deadline's `peek` has one row,
 |   (1, 247, 0), whose preposition id 247 has no FL_PREP dictionary entry
 |   anywhere in that story, so shape_build never resolves it -- a real,
 |   honestly-reached case (not one poked into g_shape from outside) of a row
 |   whose preposition id this module cannot name, proving bare `peek` opens
 |   on an object rather than an empty preposition list.
 | Author: suinevere
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/tsn.exe saturn/tests/test_shape_next.c \
 |          saturn/src/input/sentence_shape.c saturn/src/input/typeahead.c \
 |          saturn/src/input/typeahead_extract.c \
 |          && /tmp/tsn.exe saturn/cd/data/Z3/ZORK1.Z3 saturn/cd/data/Z3/DEADLINE.Z3
 ----------------------*/
#include "../src/input/sentence_shape.h"
#include "../src/input/typeahead_extract.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* typeahead_malloc(unsigned int size) { return malloc(size); }
void typeahead_free(void* ptr) { free(ptr); }

static int offers(const ShapeSlot *s, const char *word) {
    for (int i = 0; i < s->nprep; i++)
        if (s->prep[i] != NULL && strcmp(s->prep[i]->text, word) == 0) return 1;
    return 0;
}

static int offers_any(const ShapeSlot *s, const char *const *words, int nwords) {
    for (int i = 0; i < nwords; i++)
        if (offers(s, words[i])) return 1;
    return 0;
}

static unsigned char *load_and_build(const char *path, TrieNode **out_root) {
    FILE *f;
    long len;
    unsigned char *story;

    f = fopen(path, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    story = (unsigned char *) malloc((size_t) len);
    assert(story != NULL);
    assert(fread(story, 1, (size_t) len, f) == (size_t) len);
    fclose(f);

    *out_root = create_trie_node();
    build_typeahead_from_story(*out_root, story, (unsigned int) len);
    shape_build(story, (unsigned int) len, *out_root);
    return story;
}

int main(int argc, char **argv) {
    unsigned char *story;
    TrieNode *root;
    ShapeSlot s;
    const char *line[5];
    static const char *through_spellings[] = { "throug", "thru", "using", "with" };
    static const char *in_spellings[] = { "in", "inside", "into" };

    assert(argc >= 3);
    story = load_and_build(argv[1], &root);

    line[0] = "look";
    shape_next(line, 1, &s);
    assert(s.kind == SHAPE_PREP);
    assert(offers(&s, "at"));

    line[1] = "at";
    shape_next(line, 2, &s);
    assert(s.kind == SHAPE_NOUN);

    line[2] = "lamp";
    shape_next(line, 3, &s);
    assert(s.kind == SHAPE_PREP);
    assert(offers_any(&s, through_spellings, 4));
    assert(!offers(&s, "at"));

    line[3] = "through";
    line[4] = "window";
    shape_next(line, 5, &s);
    assert(s.kind == SHAPE_END);

    line[0] = "put";
    shape_next(line, 1, &s);
    assert(s.kind == SHAPE_NOUN);
    assert(!offers(&s, "in"));

    line[1] = "lamp";
    shape_next(line, 2, &s);
    assert(s.kind == SHAPE_PREP);
    assert(offers_any(&s, in_spellings, 3));

    line[2] = "in";
    line[3] = "case";
    shape_next(line, 4, &s);
    assert(s.kind == SHAPE_END);

    line[0] = "take";
    line[1] = "lamp";
    shape_next(line, 2, &s);
    assert(s.kind == SHAPE_PREP);

    line[0] = "zzzzzz";
    shape_next(line, 1, &s);
    assert(s.kind == SHAPE_FREE);

    shape_next(line, 0, &s);
    assert(s.kind == SHAPE_FREE);

    shape_destroy();
    destroy_typeahead(root);
    free(story);

    story = load_and_build(argv[2], &root);

    line[0] = "peek";
    shape_next(line, 1, &s);
    assert(s.kind == SHAPE_NOUN);
    assert(s.nprep == 0);

    shape_destroy();
    destroy_typeahead(root);
    free(story);

    printf("test_shape_next: ok\n");
    return 0;
}
