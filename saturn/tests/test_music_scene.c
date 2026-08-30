/*----------------------
 | test_music_scene.c
 | Description: Track selection from a scene's bitmask, the shape of the
 |   per-turn decision now that no classification happens, and the scene-only
 |   contract on music_set_category_fn: an event taking over the track must
 |   never be announced to the subscriber, since events carry no picture.
 |   gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/scene -o /tmp/tms \
 |       saturn/tests/test_music_scene.c saturn/src/sound/music.c \
 |       saturn/src/sound/music_data.c saturn/src/sound/event_scan.c \
 |       saturn/src/scene/scene_map.c saturn/src/scene/presentation.c && /tmp/tms
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

    check(music_track_from_mask(1UL << 4, 0) == 4 + MUSIC_TRACK_MIN,
          "a single bit picks its track");
    check(music_track_from_mask(1UL << 4, 7) == 4 + MUSIC_TRACK_MIN,
          "r is reduced modulo the population count");

    {
        unsigned long m = (1UL << 0) | (1UL << 7) | (1UL << 28);
        check(music_track_from_mask(m, 0) == 2, "first set bit");
        check(music_track_from_mask(m, 1) == 9, "second set bit");
        check(music_track_from_mask(m, 2) == 30, "third set bit");
        check(music_track_from_mask(m, 3) == 2, "wraps back to the first");
    }

    /* The disc runs 2..32. Bit 30 must reach the last of them: under the old
       bit-i-is-track-i encoding track 32 had no bit at all. */
    check(music_track_from_mask(1UL << 30, 0) == 32, "the last disc track is reachable");
    check(music_track_from_mask(1UL << 0, 0) == MUSIC_TRACK_MIN, "bit 0 is the first track");

    /* An event taking over the track must NOT announce a category. Object 21
       of ADVENT (release 1, serial "151001") carries an authored scene
       (SC_MAZE, per saturn/src/scene/game_rooms.inc's GAME_ROOM_ADVENT) so
       entering it fires the category callback once. ADVENT, not Zork I: it
       carries no presentation table (only Zork I does), so its rooms stay on
       the scene path this test means to exercise -- Zork I's own rooms now
       take the room-track path instead (see test_music_presentation.c) and
       would silence the very callback this test is checking. Then the very
       same room prints the death banner; the event overrides the mix, the
       callback must stay silent, and the track must still audibly move. */
    {
        music_set_backend(rec_play);
        music_set_isplaying(isplaying_true);
        music_set_category_fn(rec_cat);
        music_set_game(1, "151001");
        music_reset();
        music_set_debounce_frames(0);

        g_cat_calls = 0;
        music_on_turn(21);
        check(g_cat_calls == 1, "entering a scene fires the category callback once");
        int scene_track = g_track;

        music_note_output("**** You have died ****", 23);
        music_on_turn(21);      /* same room: the death banner overrides the mix */
        music_tick();           /* commit the (zero-frame) debounced switch */
        check(g_cat_calls == 1,
              "an event taking over the track must NOT announce a category");
        check(g_track != scene_track,
              "the track nonetheless swung to the event's pool");
    }

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
