/*----------------------
 | test_event_scan.c
 | Description: The one text signal left: the Z-machine death banner. The
 |   keyword table this replaced fired on any turn that said "gold" or
 |   "troll", so a chest of coins sounded like the end of the game -- the
 |   prose cases below are the ones that used to misfire.
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
    check(EV_LOSE == 0 && EV_WIN == 1,
          "the values are pool indices in music_data.c, so their order is "
          "load-bearing");

    check(event_scan("**** You have died ****") == EV_LOSE,
          "the death banner is a loss");
    check(event_scan("You crawl on.\n\n    ****  You have died  ****\n") == EV_LOSE,
          "the banner is found mid-turn, spaced as the stories print it");
    check(event_scan("*** You have died ***") == EV_LOSE,
          "three stars still count, since the buffer can truncate a line");
    check(event_scan("**** You have been killed ****") == EV_LOSE,
          "killed is a death");
    check(event_scan("**** You have suffered a fate worse than death ****")
          == EV_LOSE, "so is that");

    check(event_scan("The troll swings his axe at you!") == EV_NONE,
          "a fight is not an ending");
    check(event_scan("The chest of gold gleams before you.") == EV_NONE,
          "treasure is not an ending");
    check(event_scan("Flames leap from the burning wreck. You scream.") == EV_NONE,
          "nor is any amount of danger-flavoured prose");
    check(event_scan("**** Disk Failure ****") == EV_NONE,
          "an interpreter error banner names no death");
    check(event_scan("The sign reads: * DANGER *") == EV_NONE,
          "fewer than three stars is not a banner");
    check(event_scan("You are in a small room.") == EV_NONE,
          "plain room prose is not an ending");
    check(event_scan("") == EV_NONE, "empty text is not an ending");
    check(event_scan(0) == EV_NONE, "a null pointer is not an ending");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
