/* Host tests for the music engine's side of the situational cues: that a
   villain in the room takes the track off the room's own theme, that he gives
   it back when he leaves, that death and the ending play Zork I's own tracks
   rather than a pool draw, and that the take sting plays exactly one pass and
   restores what it interrupted.

   Drives the real engine against the real story, saturn/cd/data/Z3/ZORK1.Z3,
   with a recording backend standing in for the CD. Run from the repository
   root, which is where the story path resolves.

   Build:
     gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/scene \
         -I saturn/src/engine -o /tmp/cet test/cue_engine_test.c \
         saturn/src/sound/music.c saturn/src/sound/music_data.c \
         saturn/src/sound/event_scan.c saturn/src/scene/presentation.c \
         saturn/src/scene/cues.c saturn/src/engine/room_model.c && ./cet */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "music.h"
#include "cues.h"
#include "room_model.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } }while(0)

#define STORY_PATH "saturn/cd/data/Z3/ZORK1.Z3"
#define REL 88
#define SER "840726"

enum { PLAYER = 4, SWORD = 110, TROLL = 217,
       WEST_OF_HOUSE = 180, TROLL_ROOM = 102, LIVING_ROOM = 193 };

/* The rooms' own themes, from analysis/zork_bg/room_audio.csv by way of the
   generated presentation table: West of House and the Living Room are on 10,
   the Troll Room on 4. */
enum { TRACK_HOUSE = 10, TRACK_TROLLROOM = 4,
       TRACK_TROLL = 14, TRACK_TAKE = 25, TRACK_DEATH = 19, TRACK_WIN = 30 };

static unsigned char *g_img;
static unsigned int   g_imglen;
static int g_track, g_plays;
static void play_cb(int t, int loop) { (void) loop; g_track = t; g_plays++; }
static int  g_playing = 1;
static int  isplaying(void) { return g_playing; }

static int load_story(void) {
    FILE *f = fopen(STORY_PATH, "rb");
    long n;
    if (!f) { printf("FAIL: cannot open %s (run from the repo root)\n", STORY_PATH); return 0; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    g_img = (unsigned char *) malloc((size_t) n);
    g_imglen = (unsigned int) fread(g_img, 1, (size_t) n, f);
    fclose(f);
    return g_imglen == (unsigned int) n;
}

static unsigned int entry_off(unsigned short obj) {
    unsigned int objbase = ((unsigned int) g_img[0x0A] << 8) | g_img[0x0B];
    return objbase + 62u + ((unsigned int) obj - 1u) * 9u;
}

/* Reparent through the child/sibling chain, the way the story's MOVE does --
   see test/cue_test.c, which explains why the parent byte alone is not enough. */
static void move_obj(unsigned short obj, unsigned short parent) {
    unsigned int e = entry_off(obj);
    unsigned short old = g_img[e + 4u];
    if (old != 0) {
        unsigned int po = entry_off(old);
        if (g_img[po + 6u] == obj) {
            g_img[po + 6u] = g_img[e + 5u];
        } else {
            unsigned short s = g_img[po + 6u];
            while (s != 0 && g_img[entry_off(s) + 5u] != obj)
                s = g_img[entry_off(s) + 5u];
            if (s != 0) g_img[entry_off(s) + 5u] = g_img[e + 5u];
        }
    }
    g_img[e + 4u] = (unsigned char) parent;
    g_img[e + 5u] = g_img[entry_off(parent) + 6u];
    g_img[entry_off(parent) + 6u] = (unsigned char) obj;
}

/* One prompt: refresh the snapshot for `room` the way mojozork.c does, then
   hand the same room to the engine. */
static void turn(unsigned short room) {
    room_model_refresh_room(room);
    music_on_turn(room);
}

/* Settle a pending switch: with the debounce at one frame and no fade, a
   single tick commits it. */
static void settle(void) { music_tick(); }

/* The drive reporting the track ended, which is what loop-end reads. */
static void track_ended(void) {
    g_playing = 1; music_tick();       /* clear g_await_play */
    g_playing = 0; music_tick();       /* now it reads as loop-end */
    g_playing = 1;
}

int main(void) {
    int fails = 0;

    if (!load_story()) return 1;
    if (!room_model_bind(g_img, g_imglen)) { printf("FAIL: story did not bind\n"); return 1; }

    music_reset();
    music_set_backend(play_cb);
    music_set_isplaying(isplaying);
    music_set_debounce_frames(1);
    music_set_fade_frames(0);
    music_set_game(REL, SER);
    music_seed(1);

    /* West of House, with nothing in it that plays a cue. */
    turn(WEST_OF_HOUSE);
    CHECK(g_track == TRACK_HOUSE);

    /* Into the Troll Room, where the troll is standing: the cue outranks the
       room's own theme. */
    turn(TROLL_ROOM);
    settle();
    CHECK(g_track == TRACK_TROLL);

    /* Kill him. No room change, but the cue is re-decided every turn, so the
       room's own theme takes over -- a latched event would still be playing. */
    move_obj(TROLL, 0);
    turn(TROLL_ROOM);
    settle();
    CHECK(g_track == TRACK_TROLLROOM);

    /* The take sting. The model works out which object the player is by
       intersecting two rooms' contents, so walk the player object between two
       rooms first and check it actually spoke -- the sting is gated on knowing
       the inventory, and an unknown player means no sting rather than a wrong
       one. */
    move_obj(PLAYER, WEST_OF_HOUSE);
    turn(WEST_OF_HOUSE);
    settle();
    move_obj(PLAYER, LIVING_ROOM);
    turn(LIVING_ROOM);
    settle();
    CHECK(room_model_player() == PLAYER);
    CHECK(g_track == TRACK_HOUSE);          /* both rooms are on the same theme */
    {
        int carried_before = room_model_get()->ncarried;
        move_obj(SWORD, PLAYER);            /* the take */
        turn(LIVING_ROOM);
        CHECK(room_model_get()->ncarried == carried_before + 1);
        CHECK(g_track == TRACK_TAKE);
        CHECK(g_plays > 0);
        /* One pass only: when it ends, what it interrupted comes back. */
        track_ended();
        CHECK(g_track == TRACK_HOUSE);
        /* And a turn that takes nothing does not re-arm it. */
        turn(LIVING_ROOM);
        settle();
        CHECK(g_track == TRACK_HOUSE);
    }

    /* Death plays Zork I's own track rather than a pool draw. */
    music_note_output("**** You have died ****", 24);
    turn(LIVING_ROOM);
    settle();
    CHECK(g_track == TRACK_DEATH);

    /* And the ending plays the ending theme. */
    music_on_win();
    CHECK(g_track == TRACK_WIN);

    /* A story with no cue row keeps the old behaviour: no cue can fire, and the
       endings fall back to the pools. */
    music_reset();
    music_set_game(1, "000000");
    room_model_refresh_room(TROLL_ROOM);
    move_obj(TROLL, TROLL_ROOM);
    room_model_refresh_room(TROLL_ROOM);
    CHECK(cue_track(1, "000000", room_model_get()) == 0);

    printf(fails ? "%d FAILURE(S)\n" : "all cue engine tests passed\n", fails);
    return fails ? 1 : 0;
}
