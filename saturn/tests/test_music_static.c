/*----------------------
 | test_music_static.c
 | Description: Static room music, and the two endings. Stubs the scene map
 |   rather than linking it, because the whole point is to control what
 |   scene_track_mask answers: the shipped table is all zeros until someone
 |   authors tracks.json, and a test that waited for that would assert
 |   nothing. Stubs pres_of_room the same way, always answering "unauthored",
 |   for the same reason music.c now calls into it too: linking the real
 |   presentation.c would let Zork I's own authored table answer for these
 |   rooms instead of this file's g_scene/g_mask controls.
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

/* The mask every room lookup answers with, set per case. */
static unsigned long g_mask = 0UL;
static int g_scene = 0;

unsigned long scene_track_mask(int scene) { (void) scene; return g_mask; }
int scene_of_room(unsigned short release, const char *serial, unsigned int obj) {
    (void) release; (void) serial; (void) obj; return g_scene;
}
int scene_game_index(unsigned short release, const char *serial) {
    (void) release; (void) serial; return 0;
}
int pres_of_room(unsigned int release, const char *serial, unsigned int obj, Presentation *out) {
    (void) release; (void) serial; (void) obj; (void) out; return 0;
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
    /* One authored track is static music: the draw has nothing to choose
       between, so the same room scene always sounds the same. */
    {
        g_scene = 7; g_mask = 1UL << (23 - MUSIC_TRACK_MIN);
        arm();
        music_on_turn(100);
        settle();
        check(g_track == 23, "a one-track scene plays that track");

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
              "with no restart: rotating to the track already sounding would "
              "fade out and begin it again every third room");
    }

    /* Several authored tracks still rotate -- the guard must not freeze a
       scene that has somewhere to go. */
    {
        g_scene = 7;
        g_mask = (1UL << (18 - MUSIC_TRACK_MIN)) | (1UL << (19 - MUSIC_TRACK_MIN))
               | (1UL << (20 - MUSIC_TRACK_MIN));
        arm();
        music_on_turn(100);
        settle();
        int first = g_track;
        check(first == 18 || first == 19 || first == 20,
              "a multi-track scene draws from its own mask");

        int moved = 0, i;
        for (i = 0; i < 8 && !moved; i++) {
            music_on_turn((unsigned int)(200 + i));
            settle();
            if (g_track != first) moved = 1;
        }
        check(moved, "and does rotate once it has walked far enough");
    }

    /* An unauthored scene falls back to the neutral pool rather than silence. */
    {
        g_scene = 7; g_mask = 0UL;
        arm();
        music_on_turn(100);
        settle();
        check(g_track >= MUSIC_TRACK_MIN, "an unauthored scene still plays something");
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

    /* The death banner takes the mix away from the room's scene. */
    {
        g_scene = 7; g_mask = 1UL << (23 - MUSIC_TRACK_MIN);
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
        g_scene = 7; g_mask = 1UL << (23 - MUSIC_TRACK_MIN);
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
