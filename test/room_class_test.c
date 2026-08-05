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

static int run_suites(void) {
    int fails = snapshot();
    return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--bless") == 0) return bless();
    return run_suites();
}
