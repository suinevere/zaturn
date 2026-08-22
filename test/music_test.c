/* Host unit tests for the pure-C music engine: the surviving track pools
   (neutral fallback, danger, triumph), the RNG-backed pool pick, and the
   music_note_output overflow behavior now that event_scan -- not a room
   classifier -- is what reads the accumulated turn text.

   Room classification itself (text_classify_room, its keyword tables, title
   weighting, per-game fallback/genre) moved out of music's flow entirely; it
   is covered by test/room_class_test.c against saturn/src/classify, which
   this file no longer depends on.

   Build:
     gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/scene -o /tmp/mt \
         test/music_test.c saturn/src/sound/music.c \
         saturn/src/sound/music_data.c saturn/src/sound/event_scan.c \
         saturn/src/scene/scene_map.c && /tmp/mt */
#include <stdio.h>
#include <string.h>
#include "sound/music.h"
#include "sound/event_scan.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)

/* One past the last EV_* id -- the neutral-pool selector music.c and
   music_data.c share privately, reconstructed here since it is not part of
   music.h's public surface. */
#define POOL_NEUTRAL EVENT_N

/* --- engine harness for the music_note_output overflow tests below. The
   category callback fires only for a scene (music_set_category_fn's
   scene-only contract, see music.h), so an event's arrival can no longer be
   observed that way -- it has to be read off the track the backend was
   asked to play instead. --- */
static int g_ov_cats[4], g_ov_ncat;
static void ov_rec_cat(int c) { if (g_ov_ncat < 4) g_ov_cats[g_ov_ncat++] = c; }
static int  g_ov_track;
static void ov_play(int t, int loop) { (void) loop; g_ov_track = t; }
static int  ov_isplaying(void) { return 1; }

int main(void) {
    int fails = 0;

    /* --- data tables: the three surviving pools --- */
    {
        const unsigned char* p;
        int n = music_category_pool(POOL_NEUTRAL, &p);
        CHECK(n == 11);
        int has30 = 0; for (int i = 0; i < n; i++) { CHECK(p[i] >= 2 && p[i] <= 32); if (p[i] == 30) has30 = 1; }
        CHECK(has30);

        n = music_category_pool(EV_DANGER, &p);
        CHECK(n > 0);
        for (int i = 0; i < n; i++) CHECK(p[i] >= 2 && p[i] <= 32);

        n = music_category_pool(EV_TRIUMPH, &p);
        CHECK(n > 0);
        for (int i = 0; i < n; i++) CHECK(p[i] >= 2 && p[i] <= 32);

        CHECK(music_category_pool(-1, &p) == 0);
        CHECK(music_category_pool(POOL_NEUTRAL + 1, &p) == 0);   /* one past the last valid selector */
    }

    /* --- event scan: whole-word, case-insensitive, first match over the turn --- */
    CHECK(event_scan("A hideous monster lunges to attack!") == EV_DANGER);
    CHECK(event_scan("A pile of gold and a jewel gleam here.") == EV_TRIUMPH);
    CHECK(event_scan("You wait.") == EV_NONE);
    CHECK(event_scan(0) == EV_NONE);

    /* --- RNG-backed pool pick --- */
    music_seed(12345);
    for (int t = 0; t < 50; t++) {
        int tr = music_category_track(EV_TRIUMPH);
        const unsigned char* p; int n = music_category_pool(EV_TRIUMPH, &p);
        int member = 0; for (int i = 0; i < n; i++) if (p[i] == tr) member = 1;
        CHECK(member);
    }
    CHECK(music_category_track(-1) == 0);
    /* Same seed -> same sequence (deterministic for tests). */
    music_seed(777); int a1 = music_category_track(EV_DANGER);
    music_seed(777); int a2 = music_category_track(EV_DANGER);
    CHECK(a1 == a2);

    /* --- music_note_output keeps the NEWEST bytes on overflow, not the oldest ---
       Feed a danger keyword near the front of a chunk that alone blows past
       MUSIC_TEXT_MAX, pad past the limit with keyword-free filler across
       several calls, then close with NO keyword at all. If the buffer kept
       the oldest bytes (the pre-fix behavior) "monster" would still be in
       there, event_scan would still fire on it, and the engine would issue a
       play; keeping the newest drops "monster" entirely, so with no event and
       no authored scene (an unmapped game/room) nothing plays at all -- the
       track stays exactly what music_reset left it, 0.

       Track rather than category here because the category callback is
       scene-only (see music.h's music_set_category_fn contract) and would
       stay silent either way; a leaked "monster" is only visible in whether
       the backend was ever asked to play. */
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

        g_ov_ncat = 0; g_ov_track = 0;
        music_note_output("A monster lunges from the shadows. ", 36);
        music_note_output(pad, 599);
        music_note_output("Nothing else happens here.", 27);
        music_on_turn(500);
        CHECK(g_ov_track == 0);   /* "monster" did not survive the trim */
        CHECK(g_ov_ncat == 0);    /* no scene either, on an unmapped room */
    }

    /* --- a short turn (well under MUSIC_TEXT_MAX) scans exactly as before --
       the non-overflow append path is untouched by the fix, so the same
       keyword now unpadded is detected and does play. --- */
    {
        music_set_backend(ov_play);
        music_set_isplaying(ov_isplaying);
        music_set_category_fn(ov_rec_cat);
        music_set_game(0, "000000");
        music_reset();

        g_ov_ncat = 0; g_ov_track = 0;
        music_note_output("A troll blocks the passage!", 28);
        music_on_turn(600);
        CHECK(g_ov_track != 0);   /* the danger event WAS detected this time */
        CHECK(g_ov_ncat == 0);    /* still no scene, still silent */
    }

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
