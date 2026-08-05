/*----------------------
 | room_class_test.c
 | Description: Golden-corpus tests for room classification. Two suites over one
 |   corpus. The SNAPSHOT compares every captured room against the verdict
 |   recorded in blessed.inc and fails on any difference, so a keyword or tier
 |   edit shows its blast radius across the whole game library instead of only
 |   where someone thought to look. The ASSERTIONS pin the specific rooms this
 |   work exists to fix.
 |
 |   rooms.inc holds decoded game text with embedded escapes; it compiles clean
 |   today, and because this file #includes it, this suite is what guards that
 |   staying true permanently.
 |
 |   Re-blessing is deliberate and reviewable:
 |       ./rct --bless > test/corpus/blessed.inc
 |   Read the diff. Do not bless to make a red suite green.
 |
 |   gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
 |       test/room_class_test.c saturn/src/classify/room_class.c \
 |       saturn/src/classify/room_class_data.c && /tmp/rct
 | Author: suinevere
 | Dependencies: classify/room_class.h, corpus/rooms.inc, corpus/blessed.inc
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "classify/room_class.h"

typedef struct {
    unsigned short release;
    const char*    serial;
    const char*    title;
    const char*    text;
} CorpusRoom;

#include "corpus/rooms.inc"
#include "corpus/blessed.inc"

static const char* CAT_NAME[TEXT_NUM_CATEGORIES] = {
    "NEUTRAL", "WILDERNESS", "UNDERGROUND", "WATER", "NAUTICAL", "TOWN",
    "DUNGEON", "DESERT", "MAGIC", "SCIFI", "HORROR", "MYSTERY", "HOUSE",
    "DANGER", "TRIUMPH"
};

static int classify_row(const CorpusRoom* r) {
    room_class_reset();
    room_class_note_title(r->title);
    return text_classify_room(r->text);
}
/* Task 6 replaces the reset in this helper with a room_class_set_game call, so
   each corpus room is judged under its own story's genre. Until then reset is
   what keeps one row's title from leaking into the next. */

static int bless(void) {
    printf("/*----------------------\n");
    printf(" | blessed.inc\n");
    printf(" | Description: GENERATED FILE -- do not edit by hand; produced by\n");
    printf(" |   room_class_test --bless. The agreed category for every row of\n");
    printf(" |   rooms.inc, in corpus order. Regenerating it is how a deliberate\n");
    printf(" |   change of judgement is recorded; the diff is the review.\n");
    printf(" | Author: suinevere\n");
    printf(" | Dependencies: N/A\n");
    printf(" | Globals: N/A\n");
    printf(" | Params: N/A\n");
    printf(" | Returns: N/A\n");
    printf(" ----------------------*/\n");
    printf("static const unsigned char BLESSED[CORPUS_N] = {\n");
    for (int i = 0; i < CORPUS_N; i++)
        printf("    %d,   /* %s: %s */\n", classify_row(&CORPUS[i]),
               CORPUS[i].serial, CORPUS[i].title);
    printf("};\n");
    return 0;
}

static int snapshot(void) {
    int fails = 0;
    for (int i = 0; i < CORPUS_N; i++) {
        int got = classify_row(&CORPUS[i]);
        if (got != BLESSED[i]) {
            printf("  %-8s %-40s %s -> %s\n", CORPUS[i].serial, CORPUS[i].title,
                   CAT_NAME[BLESSED[i]], CAT_NAME[got]);
            fails++;
        }
    }
    if (fails)
        printf("SNAPSHOT: %d of %d rooms changed verdict.\n"
               "  Review each line above. If every change is intended:\n"
               "    /tmp/rct --bless > test/corpus/blessed.inc\n", fails, CORPUS_N);
    else
        printf("SNAPSHOT: OK (%d rooms unchanged)\n", CORPUS_N);
    return fails;
}

#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } }while(0)

/* Deliberately does NOT call room_class_reset: from Task 6 that clears the
   resolved genre, which would undo the room_class_set_game the genre assertions
   make just before calling here. note_title always overwrites, so there is
   nothing a reset would add. */
static int classify(const char* title, const char* text) {
    room_class_note_title(title);
    return text_classify_room(text);
}

static int assertions(void) {
    int fails = 0;

    /* snapshot() runs first and leaves whatever genre the last corpus row
       resolved. Clear it so these cases start from a known state. */
    room_class_reset();

    /* A lake in a cave is a cave. Structure outranks Biome outranks Feature, so
       no count of scenery can overturn the thing the player is standing in. */
    CHECK(classify("Cave",
        "You are in a damp cave. A lake stretches away below, and a forest is "
        "visible far above through a crack.") == TC_UNDERGROUND);

    /* Features cannot outvote a Biome however many of them there are -- this is
       the case additive weights got wrong at four features and up. */
    CHECK(classify("Clearing",
        "A tree leans over a boulder beside a still pool. A rug of moss covers "
        "the ground and a desk rots against a stump.") == TC_WILDERNESS);

    /* The title can no longer promote a Feature past a Structure. Under flat
       counting a weighted title word simply won; now the tier decides first. */
    CHECK(classify("Forest Path", "A cave.") == TC_UNDERGROUND);

    /* A landmark named as distant does not get to describe the room. The whole
       sentence is discarded, because "far off" attaches to the landmark and not
       to any particular word next to it. */
    CHECK(classify("Ledge",
        "You are on a narrow ledge. Far off, a forest covers the valley floor.")
        == TC_NEUTRAL);

    /* ...and the same sentence without the modifier still votes. */
    CHECK(classify("Ledge",
        "You are on a narrow ledge. A forest covers the valley floor.")
        == TC_WILDERNESS);

    /* A positive phrase doubles its sentence's hits, which breaks a tie WITHIN a
       tier -- it cannot promote a Feature past a Structure. */
    CHECK(classify("Junction",
        "A tunnel leads north. You are in a large cavern.") == TC_UNDERGROUND);
    CHECK(classify("Junction",
        "You are in a room with a rug. A cave opens to the north.")
        == TC_UNDERGROUND);

    if (!fails) printf("ASSERTIONS: OK\n");
    return fails;
}

static int run_suites(void) {
    int fails = snapshot() + assertions();
    return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--bless") == 0) return bless();
    return run_suites();
}
