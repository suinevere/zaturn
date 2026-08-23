/*----------------------
 | test_display_scene.c
 | Description: Per-game folder resolution and scene index ranges.
 |   gcc -O2 -I saturn/src -I saturn/src/video -I saturn/src/scene \
 |       -o /tmp/tds saturn/tests/test_display_scene.c \
 |       saturn/src/video/display.c saturn/src/scene/scene_map.c && /tmp/tds
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "video/display.h"
#include "scene/scene_map.h"

static int fails = 0;
static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

int main(void) {
    int zork = scene_game_index(88, "840726");
    check(zork >= 0, "Zork I resolves to a game index");

    display_set_game(zork);

    check(strlen(display_image_file(display_slot_make(SC_FOREST, 1))) <= 15,
          "a resolved path fits the frozen 15-character field");

    {
        const char *p = display_image_file(display_slot_make(SC_FOREST, 1));
        check(strstr(p, "ZORK1/") == p || p[0] == '\0',
              "the folder is the game's, not the scene's");
        check(p[0] == '\0' || strstr(p, ".TGA") != 0, "a path ends in .TGA");
    }

    check(display_image_file(DISP_IMAGE_NONE)[0] == '\0',
          "DISP_IMAGE_NONE resolves to the empty string");
    check(display_scene_image(-1) == 0, "a negative scene has no image");
    check(display_scene_image(SCENE_N) == 0, "a past-the-end scene has no image");
    check(display_scene_image_count(SCENE_N) == 0, "...and no count");

    display_set_game(-1);
    check(display_scene_image(SC_FOREST) == 0, "no game selected yields no image");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
