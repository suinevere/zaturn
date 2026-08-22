/*----------------------
 | test_music_scene.c
 | Description: Track selection from a scene's bitmask, and the shape of the
 |   per-turn decision now that no classification happens.
 |   gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/scene -o /tmp/tms \
 |       saturn/tests/test_music_scene.c saturn/src/sound/music.c \
 |       saturn/src/sound/music_data.c saturn/src/sound/event_scan.c \
 |       saturn/src/scene/scene_map.c && /tmp/tms
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include "sound/music.h"
#include "scene/scene_map.h"

static int fails = 0;
static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

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

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
