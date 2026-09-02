/*----------------------
 | test_presentation.c
 | Description: Lookup, bounds and identity for the generated presentation
 |   table. Run from the repository root:
 |   gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/tpres \
 |       saturn/tests/test_presentation.c saturn/src/scene/presentation.c && /tmp/tpres
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include "scene/presentation.h"

static int fails = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

int main(void) {
    Presentation p;
    int area, authored = 0, silent = 0;
    unsigned long off, len;
    unsigned int obj;

    /* Floors, not equalities: both grow as generated pictures join the supply,
       and test_presentation_counts.py is what holds the header to the table. */
    check(PRES_FRAME_N >= 74, "the 74 measured frames are all still in the table");
    check(PRES_AREA_N >= 11, "the eleven measured archives are all still there");

    check(pres_game_index(88, "840726") == 0, "Zork I is the known game");
    check(pres_game_index(88, "999999") == -1, "a wrong serial is unknown");
    check(pres_game_index(999, "840726") == -1, "a wrong release is unknown");
    check(pres_game_index(88, 0) == -1, "a null serial is unknown");

    check(pres_of_room(999, "000000", 15, &p) == 0, "an unknown game has no room");
    check(pres_of_room(88, "840726", 999, &p) == 0, "an out-of-range object is refused");
    check(pres_of_room(88, "840726", 15, 0) == 0, "a null destination is refused");

    for (obj = 0; obj < 256; obj++) {
        if (!pres_of_room(88, "840726", obj, &p)) continue;
        authored++;
        check(p.image >= 1 && p.image <= PRES_FRAME_N, "an authored image is in range");
        check(p.track == 0 || (p.track >= 2 && p.track <= 32),
              "an authored track is silence or a real disc track");
        check(p.se_bank <= 10, "an authored SE bank is in range");
        if (p.track == 0) silent++;
        check(pres_frame((int) p.image, &area, &off, &len) == 1,
              "every authored image has a frame record");
        check(area >= 0 && area < PRES_AREA_N, "the frame's area is in range");
        check(len > 512, "a frame record is larger than its palette alone");
        check(pres_area_name(area) != 0, "the frame's area has a name");
    }
    check(authored == 110, "all 110 rooms are authored");
    check(silent == 10, "ten rooms are silent");

    check(pres_frame(0, &area, &off, &len) == 0, "image 0 has no frame record");
    check(pres_frame(PRES_FRAME_N + 1, &area, &off, &len) == 0,
          "an image past the table has no frame record");
    check(pres_frame(1, 0, 0, 0) == 1, "null outputs are tolerated");
    check(pres_area_name(-1) == 0, "a negative area has no name");
    check(pres_area_name(PRES_AREA_N) == 0, "an out-of-range area has no name");

    printf(fails ? "%d FAILED\n" : "ok\n", fails);
    return fails ? 1 : 0;
}
