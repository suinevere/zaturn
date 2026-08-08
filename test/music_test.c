/* Host unit tests for the pure-C music engine (data tables, classifiers, and the
   per-turn state machine). Build:
     gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/mt \
         test/music_test.c saturn/src/sound/music.c \
         saturn/src/sound/music_data.c saturn/src/classify/room_class.c \
         saturn/src/classify/room_class_data.c && /tmp/mt */
#include <stdio.h>
#include <string.h>
#include "sound/music.h"
#include "classify/room_class.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)

/* --- engine harness for the music_note_output overflow tests below: the
   buffer's content can only be observed by driving a turn to a category
   announcement, so these mirror test/music_category_test.c's pattern rather
   than adding a getter that would exist only for tests. --- */
static int g_ov_cats[4], g_ov_ncat;
static void ov_rec_cat(int c) { if (g_ov_ncat < 4) g_ov_cats[g_ov_ncat++] = c; }
static void ov_play(int t, int loop) { (void) t; (void) loop; }
static int  ov_isplaying(void) { return 1; }

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

    /* --- music_note_output keeps the NEWEST bytes on overflow, not the oldest ---
       Feed a keyword near the front of a chunk that alone blows past
       MUSIC_TEXT_MAX, pad past the limit with keyword-free filler across
       several calls, then close with a different-category keyword. If the
       buffer kept the oldest bytes (the pre-fix behavior) "lake" would still
       be in there and WATER would win; keeping the newest drops it and
       WILDERNESS wins on "forest" alone. */
    {
        char pad[600];
        int i;
        for (i = 0; i < 599; i++) pad[i] = 'x';
        pad[599] = 0;

        music_set_backend(ov_play);
        music_set_isplaying(ov_isplaying);
        music_set_category_fn(ov_rec_cat);
        music_set_game(0, "000000");
        music_reset();

        g_ov_ncat = 0;
        music_note_output("A lake shimmers in the distance. ", 34);
        music_note_output(pad, 599);
        music_note_output("A dense forest surrounds you on every side.", 44);
        music_on_turn(500);
        CHECK(g_ov_ncat == 1);
        CHECK(g_ov_cats[0] == TC_WILDERNESS);   /* forest survived */
        CHECK(g_ov_cats[0] != TC_WATER);        /* lake did not */
    }

    /* --- regression: Zork III turn one must classify HORROR, not WATER ---
       Turn one is ~600 bytes of dream sequence (which mentions "a cool, clear
       lake"), THEN the banner, THEN "Endless Stair" and its room text. Before
       the fix, only the first MUSIC_TEXT_MAX-1 bytes -- the dream alone --
       ever reached the classifier, and "lake" (TC_WATER, KT_BIOME) beat the
       room's own "eerie"/"shadow" (TC_HORROR, KT_FEATURE) under strict tier
       comparison. Fed here across three calls the way the interpreter prints
       it: the dream paragraph, then the banner, then the room. This is the
       real captured turn-one text, not a paraphrase. */
    {
        music_set_backend(ov_play);
        music_set_isplaying(ov_isplaying);
        music_set_category_fn(ov_rec_cat);
        music_set_game(17, "840727");           /* Zork III, per mojozork */
        music_reset();
        music_note_room_title("Endless Stair"); /* per mojozork's location object */

        g_ov_ncat = 0;
        music_note_output(
            "As in a dream, you see yourself tumbling down a great, dark staircase. "
            "All about you are shadowy images of struggles against fierce opponents "
            "and diabolical traps. These give way to another round of images: of "
            "imposing stone figures, a cool, clear lake, and, now, of an old, yet "
            "oddly youthful man. He turns toward you slowly, his long, silver hair "
            "dancing about him in a fresh breeze. \"You have reached the final test, "
            "my friend! You are proved clever and powerful, but this is not yet "
            "enough! Seek me when you feel yourself worthy!\" The dream dissolves "
            "around you as his last words echo through the void....\n"
            "\n", 611);
        music_note_output(
            "ZORK III: The Dungeon Master\n"
            "Copyright 1982 by Infocom, Inc. All rights reserved.\n"
            "ZORK is a trademark of Infocom, Inc.\n"
            "Release 17 / Serial number 840727\n"
            "\n", 154);
        music_note_output(
            "Endless Stair\n"
            "You are at the bottom of a seemingly endless stair, winding its way "
            "upward beyond your vision. An eerie light, coming from all around you, "
            "casts strange shadows on the walls. To the south is a dark and winding "
            "trail.\n"
            "Your old friend, the brass lantern, is at your feet.\n"
            "\n", 285);
        music_on_turn(180);
        CHECK(g_ov_ncat == 1);
        CHECK(g_ov_cats[0] == TC_HORROR);
        CHECK(g_ov_cats[0] != TC_WATER);
    }

    /* --- a short turn (well under MUSIC_TEXT_MAX) classifies exactly as
       before -- the non-overflow append path is untouched by the fix. --- */
    {
        music_set_backend(ov_play);
        music_set_isplaying(ov_isplaying);
        music_set_category_fn(ov_rec_cat);
        music_set_game(0, "000000");
        music_reset();

        g_ov_ncat = 0;
        music_note_output("A damp cave with a narrow tunnel.", 34);
        music_on_turn(600);
        CHECK(g_ov_ncat == 1);
        CHECK(g_ov_cats[0] == TC_UNDERGROUND);
    }

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
