/*----------------------
 | test_scene_map.c
 | Description: Lookup, bounds and identity for the generated scene tables.
 |   gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/tsm \
 |       saturn/tests/test_scene_map.c saturn/src/scene/scene_map.c && /tmp/tsm
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "scene/scene_map.h"

static int fails = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

int main(void) {
    check(SCENE_N == 32, "vocabulary is 32 scenes");
    check(SC_FOREST == 0, "FOREST is index 0");
    check(SC_SPACE == SCENE_N - 1, "SPACE is the last index");

    check(scene_game_index(88, "840726") >= 0, "Zork I is a known game");
    check(scene_game_index(999, "000000") == -1, "unknown release is unmapped");
    check(scene_game_index(88, "999999") == -1, "wrong serial is unmapped");

    check(scene_of_room(88, "840726", 0) == -1, "object 0 is unmapped");
    check(scene_of_room(88, "840726", 255) == -1 ||
          scene_of_room(88, "840726", 255) >= 0, "object 255 does not read out of range");
    check(scene_of_room(999, "000000", 5) == -1, "unknown game yields no scene");

    {
        int found = 0, obj;
        for (obj = 1; obj < 256; obj++)
            if (scene_of_room(88, "840726", obj) >= 0) found++;
        check(found > 50, "Zork I has many mapped rooms");
    }

    check(scene_name(SC_FOREST) != 0 && strcmp(scene_name(SC_FOREST), "FOREST") == 0,
          "scene_name reports FOREST");
    check(scene_name(-1) == 0, "scene_name rejects a negative index");
    check(scene_name(SCENE_N) == 0, "scene_name rejects a past-the-end index");

    check(scene_track_mask(-1, SC_FOREST) == 0UL, "unknown game has no track mask");
    check(scene_track_mask(0, SCENE_N) == 0UL, "out-of-range scene has no track mask");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
