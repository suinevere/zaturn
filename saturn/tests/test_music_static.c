/*----------------------
 | test_music_static.c
 | Description: Static room music, and the two endings. Stubs pres_of_room
 |   rather than linking presentation.c, because the whole point is to control
 |   what the room table answers: linking the real one would let Zork I's own
 |   authored table answer for these object numbers instead of this file's
 |   g_room_track control, and the cases below would then be asserting Zork I's
 |   table rather than the engine.
 |   gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/scene -o /tmp/tmt \
 |       saturn/tests/test_music_static.c saturn/src/sound/music.c \
 |       saturn/src/sound/music_data.c saturn/src/sound/event_scan.c && /tmp/tmt
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "sound/music.h"
#include "sound/event_scan.h"
#include "scene/presentation.h"

static int fails = 0;
static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

static int g_track = 0, g_plays = 0;
static void rec_play(int track, int loop) { (void) loop; g_track = track; g_plays++; }
static int isplaying_true(void) { return 1; }

/*----------------------
 | g_room_track / pres_of_room
 | Description: The track the authored table answers with for every room, set
 |   per case. Zero means the story has no authored entry at all, which the
 |   engine reads as "no category" and answers from the neutral pool -- so one
 |   control covers both the authored and the unauthored case.
 | Author: suinevere
 ----------------------*/
static unsigned char g_room_track = 0;

int pres_of_room(unsigned int release, const char *serial, unsigned int obj, Presentation *out) {
    (void) release; (void) serial; (void) obj;
    if (g_room_track == 0) return 0;
    out->image = 1; out->track = g_room_track; out->se_bank = 0;
    return 1;
}

/*----------------------
 | settle
 | Description: Runs the engine far enough past a zero-frame debounce that any
 |   armed switch has committed.
 | Author: suinevere
 ----------------------*/
static void settle(void) {
    int i;
    for (i = 0; i < 4; i++) music_tick();
}

/*----------------------
 | arm
 | Description: A fresh Dynamic engine with no settle delay, playing whatever
 |   room 100 resolves to.
 | Author: suinevere
 ----------------------*/
static void arm(void) {
    music_set_backend(rec_play);
    music_set_isplaying(isplaying_true);
    music_set_game(1, "151001");
    music_reset();
    music_set_debounce_frames(0);
    g_plays = 0;
}

int main(void) {
    /* An authored room's track IS its category, so the engine has nothing to
       choose between and the room always sounds the same. */
    {
        g_room_track = 23;
        arm();
        music_on_turn(100);
        settle();
        check(g_track == 23, "an authored room plays its own track");

        int before = g_plays;
        music_on_turn(101);
        settle();
        music_on_turn(102);
        settle();
        music_on_turn(103);
        settle();
        music_on_turn(104);
        settle();
        check(g_track == 23, "and keeps playing it across four more rooms");
        check(g_plays == before,
              "with no restart: rooms that share a track share a category, and "
              "re-issuing it would fade out and begin it again on every step");
    }

    /* Walking into a room the table gives a different track must swing the
       music. The static case above must not have frozen it. */
    {
        g_room_track = 18;
        arm();
        music_on_turn(100);
        settle();
        check(g_track == 18, "the first room's authored track");

        g_room_track = 20;
        music_on_turn(101);
        settle();
        check(g_track == 20, "and a room with a different track swings to it");
    }

    /* A room with no authored entry falls back to the neutral pool rather than
       to silence. */
    {
        g_room_track = 0;
        arm();
        music_on_turn(100);
        settle();
        check(g_track >= MUSIC_TRACK_MIN, "an unauthored room still plays something");
    }

    /* Losing is the death routine's banner, and nothing else. */
    {
        check(event_scan("**** You have died ****") == EV_LOSE,
              "the death banner loses");
        check(event_scan("*** You have died ***") == EV_LOSE,
              "three stars is still a banner");
        check(event_scan("**** You have been sent to school ****") == EV_LOSE,
              "Wishbringer's wording is a death however it is worded");
        check(event_scan("The troll swings his axe at you!") == EV_NONE,
              "a fight is not an ending");
        check(event_scan("The chest of gold gleams before you.") == EV_NONE,
              "treasure is not an ending");
        check(event_scan("**** Disk Failure ****") == EV_NONE,
              "an interpreter error banner names no death");
        check(event_scan("You are in a small room.") == EV_NONE,
              "plain room prose is not an ending");
        check(event_scan("") == EV_NONE, "empty text is not an ending");
        check(event_scan(0) == EV_NONE, "a null pointer is not an ending");
    }

    /* The death banner takes the music away from the room's own track. */
    {
        g_room_track = 23;
        arm();
        music_on_turn(100);
        settle();
        check(g_track == 23, "the room's own track first");
        music_note_output("**** You have died ****", 23);
        music_on_turn(100);
        settle();
        check(g_track != 23, "then the lose pool takes over");
    }

    /* Winning arrives from the interpreter, not from the text, and does not
       wait for a settle: there is no next room to walk to. */
    {
        g_room_track = 23;
        arm();
        music_on_turn(100);
        settle();
        check(g_track == 23, "the room's own track first");
        music_on_win();
        check(g_track != 23, "music_on_win swings the track immediately");
    }

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
