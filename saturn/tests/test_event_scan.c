/*----------------------
 | test_event_scan.c
 | Description: The surviving text scan: danger and triumph, nothing else.
 |   gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/tes \
 |       saturn/tests/test_event_scan.c saturn/src/sound/event_scan.c && /tmp/tes
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include "sound/event_scan.h"

static int fails = 0;
static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

int main(void) {
    check(EVENT_N == 2, "two event categories");
    check(EV_NONE == -1, "EV_NONE is -1");

    check(event_scan("The troll swings his axe at you!") == EV_DANGER,
          "an attack is danger");
    check(event_scan("The chest of gold gleams before you.") == EV_TRIUMPH,
          "a real triumph phrase fires triumph");
    check(event_scan("A voice booms: your quest is complete.") == EV_NONE,
          "triumph-flavored prose with no table word does not misfire");
    check(event_scan("You are in a small room.") == EV_NONE,
          "plain room prose is not an event");
    check(event_scan("") == EV_NONE, "empty text is not an event");
    check(event_scan(0) == EV_NONE, "a null pointer is not an event");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
