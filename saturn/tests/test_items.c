/*----------------------
 | test_items.c
 | Description: The runtime lookup over game_items.inc. Run from the
 |   repository root:
 |   gcc -O2 -I saturn/src -o /tmp/titems \
 |       saturn/tests/test_items.c saturn/src/scene/items.c && /tmp/titems
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include "scene/items.h"

static int fails = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

int main(void) {
    check(items_available(88, "840726") == 1, "Zork I has a table");
    check(items_available(88, "999999") == 0, "a wrong serial has none");
    check(items_available(42, "840726") == 0, "a wrong release has none");

    check(items_picture_of(88, "840726", 87) == 7, "the jewelled egg is picture 7");
    check(items_picture_of(88, "840726", 209) == 0, "the sceptre is picture 0");
    check(items_picture_of(88, "840726", 231) == 15, "the crystal skull is picture 15");
    check(items_picture_of(88, "840726", 101) == 18, "the trunk of jewels is picture 18");

    check(items_picture_of(88, "840726", 86) == -1, "the broken egg is unbound");
    check(items_picture_of(88, "840726", 110) == -1, "the sword is unbound");
    check(items_picture_of(88, "840726", 0) == -1, "object 0 is unbound");
    check(items_picture_of(88, "840726", 65535) == -1, "an absurd object is unbound");
    check(items_picture_of(88, "999999", 87) == -1, "a wrong serial binds nothing");

    printf(fails ? "%d FAILURES\n" : "all pass\n", fails);
    return fails ? 1 : 0;
}
