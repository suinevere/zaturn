/* Build:
     gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/engine -o /tmp/tmm \
         saturn/tests/test_map_marks.c saturn/src/engine/map_marks.c && /tmp/tmm */
#include "../src/engine/map_marks.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ZORK1_MARKS 6

static void header(unsigned char *h, unsigned int release, const char *serial) {
    memset(h, 0, 0x18);
    h[0] = 3;
    h[0x02] = (unsigned char) (release >> 8);
    h[0x03] = (unsigned char) (release & 0xFF);
    memcpy(h + 0x12, serial, 6);
}

int main(void) {
    unsigned char h[0x18];
    unsigned char dest = 0xFF, flags = 0xFF;

    assert(!map_marks_for(94, 8, &dest, &flags));

    header(h, 88, "840726");
    assert(map_marks_bind(h, sizeof h) == ZORK1_MARKS);

    /* The chimney: the drawing supplies the destination the routine exit hid,
       and retracts the descent the game never permits. */
    assert(map_marks_for(94, 8, &dest, &flags));
    assert(dest == 203 && (flags & MARK_BAGGAGE));
    assert(map_marks_for(203, 9, &dest, &flags));
    assert(flags & MARK_RETRACT);

    /* Both ways through the Timber Room shaft, including the OUT synonym. */
    assert(map_marks_for(206, 2, &dest, &flags) && (flags & MARK_BAGGAGE));
    assert(map_marks_for(228, 1, &dest, &flags) && (flags & MARK_BAGGAGE));
    assert(map_marks_for(228, 11, &dest, &flags) && (flags & MARK_BAGGAGE));

    /* The Altar's own descent, the one row none of the above touches. */
    assert(map_marks_for(212, 9, &dest, &flags) && (flags & MARK_BAGGAGE));

    /* The White Cliffs and the Damp Cave they open onto all read as a baggage
       limit in source -- DEFLATE gates the three of them with "The path is too
       narrow" -- and none of them is one. */
    assert(!map_marks_for(32, 3, &dest, &flags));
    assert(!map_marks_for(33, 0, &dest, &flags));
    assert(!map_marks_for(39, 1, &dest, &flags));

    /* A story with no table binds nothing rather than reading off the end. */
    header(h, 1, "000000");
    assert(map_marks_bind(h, sizeof h) == 0);
    assert(!map_marks_for(94, 8, &dest, &flags));

    printf("ok\n");
    return 0;
}
