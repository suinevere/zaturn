/* Host unit tests for the pure-C music engine (data tables, classifiers, and the
   per-turn state machine). Build:
     gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/mt \
         test/music_test.c saturn/src/sound/music.c \
         saturn/src/sound/music_data.c saturn/src/classify/room_class.c \
         saturn/src/classify/room_class_data.c && /tmp/mt */
#include <stdio.h>
#include "sound/music.h"
#include "classify/room_class.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)

int main(void) {
    int fails = 0;

    /* --- data tables: per-category pools --- */
    {
        const unsigned char* p;
        int n = music_category_pool(TC_NEUTRAL, &p);
        CHECK(n == 11);
        int has30 = 0; for (int i = 0; i < n; i++) { CHECK(p[i] >= 2 && p[i] <= 32); if (p[i] == 30) has30 = 1; }
        CHECK(has30);
        /* Neutral is merged into every other category: track 4 (Neutral) appears everywhere. */
        for (int c = TC_WILDERNESS; c <= TC_TRIUMPH; c++) {
            int m = music_category_pool(c, &p);
            CHECK(m > 0);
            int has4 = 0; for (int i = 0; i < m; i++) if (p[i] == 4) has4 = 1;
            CHECK(has4);
        }
        CHECK(music_category_pool(-1, &p) == 0);
        CHECK(music_category_pool(TEXT_NUM_CATEGORIES, &p) == 0);
    }
    int nk = 0; const TextKeyword* kw = text_keywords(&nk);
    CHECK(kw && nk > 0);
    /* Room keywords name a PLACE the classifier can actually pick, so the range
       excludes both ends. Above TC_PLACE_LAST are the EVENT categories: TC_DANGER
       and TC_TRIUMPH are moments scanned out of turn text by text_scan_event, and
       a room that permanently classified as one would sit on a combat sting
       forever. Below TC_WILDERNESS is TC_NEUTRAL, which is the nothing-matched
       answer rather than something a keyword votes for -- text_classify_room's
       winner loop starts past it, so a hit scored there would be counted and then
       silently discarded. That is exactly what TC_HOUSE was added to avoid: the
       domestic words need a real category to win, not the default. */
    for (int i = 0; i < nk; i++)
        CHECK(kw[i].cat >= TC_WILDERNESS && kw[i].cat <= TC_PLACE_LAST);
    int ne = 0; const TextKeyword* ev = text_events(&ne);
    CHECK(ev && ne > 0);
    for (int i = 0; i < ne; i++) CHECK(ev[i].cat == TC_DANGER || ev[i].cat == TC_TRIUMPH);
    CHECK(text_game_room_category(88, "840726", 5) == -1);

    /* --- classifiers --- */
    CHECK(text_classify_room("You are in a damp cave. A narrow tunnel leads north.") == TC_UNDERGROUND);
    CHECK(text_classify_room("A sunny forest clearing, tall trees all around.") == TC_WILDERNESS);
    CHECK(text_classify_room("The airlock hisses. A console blinks on the corridor wall.") == TC_SCIFI);
    CHECK(text_classify_room("The wizard's study is lined with scroll racks; a rune glows.") == TC_MAGIC);
    CHECK(text_classify_room("Nothing in particular here.") == TC_NEUTRAL);
    CHECK(text_classify_room("A CAVERN yawns below.") == TC_UNDERGROUND);
    CHECK(text_classify_room("You scavenge the bins.") == TC_NEUTRAL);   /* no 'cave' false hit */
    CHECK(text_scan_event("A hideous monster lunges to attack!") == TC_DANGER);
    CHECK(text_scan_event("A pile of gold and a jewel gleam here.") == TC_TRIUMPH);
    CHECK(text_scan_event("You wait.") == -1);

    /* --- RNG-backed category pick --- */
    music_seed(12345);
    for (int t = 0; t < 50; t++) {
        int tr = music_category_track(TC_MAGIC);
        const unsigned char* p; int n = music_category_pool(TC_MAGIC, &p);
        int member = 0; for (int i = 0; i < n; i++) if (p[i] == tr) member = 1;
        CHECK(member);
    }
    CHECK(music_category_track(-1) == 0);
    /* Same seed -> same sequence (deterministic for tests). */
    music_seed(777); int a1 = music_category_track(TC_HORROR);
    music_seed(777); int a2 = music_category_track(TC_HORROR);
    CHECK(a1 == a2);

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
