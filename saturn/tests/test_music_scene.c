/*----------------------
 | test_music_scene.c
 | Description: Track selection from a scene's bitmask, the shape of the
 |   per-turn decision now that no classification happens, and the scene-only
 |   contract on music_set_category_fn: an event taking over the track must
 |   never be announced to the subscriber, since events carry no picture.
 |   gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/scene -o /tmp/tms \
 |       saturn/tests/test_music_scene.c saturn/src/sound/music.c \
 |       saturn/src/sound/music_data.c saturn/src/sound/event_scan.c \
 |       saturn/src/scene/scene_map.c && /tmp/tms
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "sound/music.h"
#include "scene/scene_map.h"

static int fails = 0;
static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

static int g_track = 0, g_loop = 0;
static void rec_play(int track, int loop) { g_track = track; g_loop = loop; }
static int isplaying_true(void) { return 1; }

static int g_cat_calls = 0;
static void rec_cat(int cat) { (void) cat; g_cat_calls++; }

int main(void) {
    check(music_track_from_mask(0UL, 0) == 0, "an empty mask picks no track");

    check(music_track_from_mask(1UL << 4, 0) == 4, "a single bit picks its track");
    check(music_track_from_mask(1UL << 4, 7) == 4,
          "r is reduced modulo the population count");

    {
        unsigned long m = (1UL << 2) | (1UL << 9) | (1UL << 30);
        check(music_track_from_mask(m, 0) == 2, "first set bit");
        check(music_track_from_mask(m, 1) == 9, "second set bit");
        check(music_track_from_mask(m, 2) == 30, "third set bit");
        check(music_track_from_mask(m, 3) == 2, "wraps back to the first");
    }

    /* An event taking over the track must NOT announce a category. Object 38
       of Zork I (release 88, serial "840726") carries an authored scene
       (SC_CAVE, per saturn/src/scene/game_rooms.inc's GAME_ROOM_ZORK1) so
       entering it fires the category callback once. Then the very same room
       prints danger text; the event overrides the mix, the callback must stay
       silent, and the track must still audibly move. */
    {
        music_set_backend(rec_play);
        music_set_isplaying(isplaying_true);
        music_set_category_fn(rec_cat);
        music_set_game(88, "840726");
        music_set_mix(MIX_DYNAMIC, 10);
        music_reset();
        music_set_debounce_frames(0);

        g_cat_calls = 0;
        music_on_turn(38);
        check(g_cat_calls == 1, "entering a scene fires the category callback once");
        int scene_track = g_track;

        music_note_output("A hideous monster lunges to attack!", 36);
        music_on_turn(38);      /* same room: the danger event overrides the mix */
        music_tick();           /* commit the (zero-frame) debounced switch */
        check(g_cat_calls == 1,
              "an event taking over the track must NOT announce a category");
        check(g_track != scene_track,
              "the track nonetheless swung to the event's pool");
    }

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
