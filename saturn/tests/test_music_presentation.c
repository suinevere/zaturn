/*----------------------
 | test_music_presentation.c
 | Description: Dynamic mode over a story with an authored per-room table.
 |   Run from the repository root:
 |   gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/tmpres \
 |       saturn/tests/test_music_presentation.c saturn/src/sound/music.c \
 |       saturn/src/scene/presentation.c \
 |       saturn/src/sound/event_scan.c saturn/src/sound/music_data.c && /tmp/tmpres
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include "sound/music.h"
#include "scene/presentation.h"

static int fails = 0;
static int g_issued[64];
static int g_issued_n = 0;
static unsigned int g_last_room = 0;
static int g_room_calls = 0;
static int g_playing = 1;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

static void play_stub(int track, int loop) {
    (void) loop;
    if (g_issued_n < 64) g_issued[g_issued_n++] = track;
}

static void room_stub(unsigned int obj) { g_last_room = obj; g_room_calls++; }
static int isplaying_stub(void) { return g_playing; }

static void settle(void) {
    int i;
    for (i = 0; i < 8; i++) music_tick();
}

static void finish_track(void) {
    g_playing = 1; music_tick();
    g_playing = 0; music_tick();
    g_playing = 1;
}

static void reset_all(void) {
    music_set_backend(play_stub);
    music_set_room_fn(room_stub);
    music_set_isplaying(isplaying_stub);
    music_reset();
    g_issued_n = 0; g_room_calls = 0;
    g_playing = 1;
    music_set_debounce_frames(0);
    music_set_game(88, "840726");
}

int main(void) {
    unsigned int a = 0, b = 0, silent = 0, other = 0;
    unsigned int obj;
    unsigned int a_track = 0;

    for (obj = 0; obj < 256 && !(a && b && silent && other); obj++) {
        Presentation p;
        if (!pres_of_room(88, "840726", obj, &p)) continue;
        if (p.track == 0) {
            if (!silent) silent = obj;
            continue;
        }
        if (!a) { a = obj; a_track = p.track; continue; }
        if (!b && p.track == a_track) { b = obj; continue; }
        if (!other && p.track != a_track) { other = obj; continue; }
    }
    check(a && b && silent && other, "the table offers the four rooms this needs");
    if (!(a && b && silent && other)) {
        printf("%d FAILED\n", fails + 1);
        return 1;
    }

    reset_all();
    music_on_turn(a);
    settle();
    check(g_issued_n == 1, "entering the first room issues one track");
    check(g_room_calls == 1, "the room subscriber fired once");
    check(g_last_room == a, "the room subscriber was told which room");

    music_on_turn(b);
    settle();
    check(g_issued_n == 1, "a room sharing the track does not re-issue it");
    check(g_room_calls == 2, "but the picture is still told to change");

    music_on_turn(other);
    settle();
    check(g_issued_n == 2, "a room with a different track issues it");

    music_on_turn(silent);
    settle();
    check(g_issued_n == 3 && g_issued[2] == 0, "a silent room stops the drive");

    reset_all();
    music_on_turn(a);
    settle();
    music_on_turn(b);
    settle();
    music_on_turn(a);
    settle();
    music_on_turn(b);
    settle();
    check(g_issued_n == 1, "walking a whole area never rotates off its track");

    reset_all();
    music_set_game(999, "000000");
    music_on_turn(a);
    settle();
    check(g_room_calls >= 1, "a story with no table still announces rooms");

    reset_all();
    music_on_turn(a);
    settle();
    music_note_output("**** You have died ****", 23);
    music_on_turn(a);
    settle();
    check(g_issued_n == 2, "the death banner takes over the track");
    finish_track();
    check(g_issued_n == 3 && g_issued[2] == (int) a_track,
          "a death sting, once it ends, resumes the room's own authored track");

    reset_all();
    music_on_turn(a);
    settle();
    int before_win = g_issued_n;
    music_on_win();
    check(g_issued_n == before_win + 1, "winning issues the win jingle");
    int win_track = g_issued[g_issued_n - 1];
    finish_track();
    check(g_issued[g_issued_n - 1] == win_track,
          "the win jingle, once its pass ends, is not replaced by the room's track");
    check(g_issued[g_issued_n - 1] != (int) a_track,
          "specifically: winning never falls back to playing the room's own track");

    printf(fails ? "%d FAILED\n" : "ok\n", fails);
    return fails ? 1 : 0;
}
