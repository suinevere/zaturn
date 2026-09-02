/* Host unit tests for the situational cue table (saturn/src/scene/cues.c)
   driven against the real Zork I story the disc ships, saturn/cd/data/Z3/
   ZORK1.Z3 -- no mock object tree, because the whole point of the table is
   that its object numbers are the ones in that file.

   Run from the repository root, which is where the story path resolves.

   Build:
     gcc -O2 -I saturn/src -I saturn/src/scene -I saturn/src/engine \
         -o /tmp/ct test/cue_test.c saturn/src/scene/cues.c \
         saturn/src/engine/room_model.c && ./ct */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cues.h"
#include "room_model.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } }while(0)

#define STORY_PATH "saturn/cd/data/Z3/ZORK1.Z3"
#define REL 88
#define SER "840726"

/* Object and room numbers gen_cues.py resolved out of this exact story. The
   test states them rather than importing them so a generator change that moved
   a cue onto the wrong object shows up here as a failure rather than as two
   files agreeing with each other. */
enum { TROLL = 217, CYCLOPS = 186, THIEF = 114, SWORD = 110,
       TROLL_ROOM = 102, CYCLOPS_ROOM = 185, ROUND_ROOM = 107,
       TREASURE_ROOM = 190, INVISIBLE = 7 };

static unsigned char *g_img;
static unsigned int   g_imglen;

/*----------------------
 | load_story
 | Description: Reads the story image into a writable buffer, so a test can
 |   clear an attribute bit and re-bind.
 | Author: suinevere
 ----------------------*/
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

/*----------------------
 | obj_entry_off
 | Description: The byte offset of an object's 9-byte table entry, so a test
 |   can poke its attribute flags. Mirrors room_model.c's own arithmetic.
 | Author: suinevere
 ----------------------*/
static unsigned int obj_entry_off(unsigned short obj) {
    unsigned int objbase = ((unsigned int) g_img[0x0A] << 8) | g_img[0x0B];
    return objbase + 62u + ((unsigned int) obj - 1u) * 9u;
}

/*----------------------
 | set_attr
 | Description: Sets or clears one attribute bit on one object.
 | Author: suinevere
 ----------------------*/
static void set_attr(unsigned short obj, int attr, int on) {
    unsigned int e = obj_entry_off(obj) + (unsigned int) (attr >> 3);
    unsigned char bit = (unsigned char) (1u << (7 - (attr & 7)));
    if (on) g_img[e] |= bit; else g_img[e] = (unsigned char) (g_img[e] & ~bit);
}

/*----------------------
 | move_obj
 | Description: Reparents an object the way the story's own MOVE would --
 |   unlinking it from its old parent's child chain and pushing it onto the
 |   front of the new one's. Writing the parent byte alone is not enough: the
 |   model reads a room's contents by walking child and sibling, so an object
 |   moved only by its parent byte stays exactly where it was as far as
 |   anything that matters is concerned.
 | Author: suinevere
 ----------------------*/
static void move_obj(unsigned short obj, unsigned short parent) {
    unsigned int e = obj_entry_off(obj);
    unsigned short old = g_img[e + 4u];
    if (old != 0) {
        unsigned int po = obj_entry_off(old);
        if (g_img[po + 6u] == obj) {
            g_img[po + 6u] = g_img[e + 5u];
        } else {
            unsigned short s = g_img[po + 6u];
            while (s != 0 && g_img[obj_entry_off(s) + 5u] != obj)
                s = g_img[obj_entry_off(s) + 5u];
            if (s != 0) g_img[obj_entry_off(s) + 5u] = g_img[e + 5u];
        }
    }
    g_img[e + 4u] = (unsigned char) parent;
    g_img[e + 5u] = g_img[obj_entry_off(parent) + 6u];
    g_img[obj_entry_off(parent) + 6u] = (unsigned char) obj;
}

int main(void) {
    int fails = 0;

    if (!load_story()) return 1;
    if (!room_model_bind(g_img, g_imglen)) { printf("FAIL: story did not bind\n"); return 1; }

    /* The table found this game, and the three standalone cues are the tracks
       the audio map measured. */
    CHECK(cue_game_index(REL, SER) >= 0);
    CHECK(cue_game_index(REL, "000000") < 0);
    CHECK(cue_game_index(1, SER) < 0);
    CHECK(cue_take_track(REL, SER) == 25);
    CHECK(cue_death_track(REL, SER) == 19);
    CHECK(cue_win_track(REL, SER) == 30);
    CHECK(cue_take_track(1, SER) == 0);

    /* The attribute the generator solved really is the one the thief starts
       with and the troll and cyclops do not. */
    CHECK(room_model_object_attr(THIEF, INVISIBLE) == 1);
    CHECK(room_model_object_attr(TROLL, INVISIBLE) == 0);
    CHECK(room_model_object_attr(CYCLOPS, INVISIBLE) == 0);
    CHECK(room_model_object_parent(TROLL) == TROLL_ROOM);
    CHECK(room_model_object_parent(CYCLOPS) == CYCLOPS_ROOM);
    CHECK(room_model_object_parent(THIEF) == ROUND_ROOM);
    CHECK(room_model_object_parent(0) == 0);
    CHECK(room_model_object_attr(TROLL, -1) == 0);
    CHECK(room_model_object_attr(TROLL, 32) == 0);

    /* Standing in the Troll Room with the troll in it. */
    room_model_refresh_room(TROLL_ROOM);
    CHECK(cue_track(REL, SER, room_model_get()) == 14);
    /* And in the Cyclops Room with the cyclops. */
    room_model_refresh_room(CYCLOPS_ROOM);
    CHECK(cue_track(REL, SER, room_model_get()) == 17);
    /* A story with no cue row is left alone even standing on a villain. */
    room_model_refresh_room(TROLL_ROOM);
    CHECK(cue_track(1, SER, room_model_get()) == 0);
    CHECK(cue_track(REL, SER, 0) == 0);

    /* The thief starts in the Round Room, invisible: no cue. This is the whole
       reason the INVISIBLE bit had to be solved rather than skipped -- he is in
       a room from the first turn of the game. */
    room_model_refresh_room(ROUND_ROOM);
    CHECK(cue_track(REL, SER, room_model_get()) == 0);

    /* Show him and it is the away-from-his-lair track. */
    set_attr(THIEF, INVISIBLE, 0);
    room_model_refresh_room(ROUND_ROOM);
    CHECK(cue_track(REL, SER, room_model_get()) == 15);

    /* In the Treasure Room it is the lair track instead. */
    move_obj(THIEF, TREASURE_ROOM);
    room_model_refresh_room(TREASURE_ROOM);
    CHECK(cue_track(REL, SER, room_model_get()) == 16);

    /* The troll outranks the thief when both are in the Troll Room, because
       the troll's rule is reached first -- the original suppressed the thief
       cue there with a second condition; ordering does it here. */
    move_obj(THIEF, TROLL_ROOM);
    room_model_refresh_room(TROLL_ROOM);
    CHECK(cue_track(REL, SER, room_model_get()) == 14);

    /* Hide him again and the Treasure Room falls silent even though he is
       standing in it. */
    set_attr(THIEF, INVISIBLE, 1);
    move_obj(THIEF, TREASURE_ROOM);
    room_model_refresh_room(TREASURE_ROOM);
    CHECK(cue_track(REL, SER, room_model_get()) == 0);

    /* The danger cue: a hand-built snapshot, because it turns on the player's
       inventory and the model only fills that in once it has worked out which
       object the player is -- which takes a move it cannot be given here. One
       open exit into the Troll Room, the sword in hand. */
    {
        RoomModel m;
        int i;
        memset(&m, 0, sizeof m);
        m.room = ROUND_ROOM;
        for (i = 0; i < RM_DIR_N; i++) { m.exits[i] = RM_EXIT_NONE; m.dest[i] = 0; }
        m.exits[RM_N] = RM_EXIT_OPEN; m.dest[RM_N] = TROLL_ROOM;
        m.carried[0] = SWORD; m.ncarried = 1;
        move_obj(TROLL, TROLL_ROOM);
        CHECK(cue_track(REL, SER, &m) == 13);

        /* Without the sword there is nothing to glow. */
        m.ncarried = 0;
        CHECK(cue_track(REL, SER, &m) == 0);

        /* Sword in hand but the exit is not an open one: no adjacency. */
        m.ncarried = 1;
        m.exits[RM_N] = RM_EXIT_MAYBE;
        CHECK(cue_track(REL, SER, &m) == 0);

        /* An open exit somewhere with no villain in it says nothing either. */
        m.exits[RM_N] = RM_EXIT_OPEN; m.dest[RM_N] = CYCLOPS_ROOM;
        move_obj(CYCLOPS, TREASURE_ROOM);
        CHECK(cue_track(REL, SER, &m) == 0);

        /* A villain the story is hiding does not make the sword glow either. */
        m.dest[RM_N] = TREASURE_ROOM;
        move_obj(THIEF, TREASURE_ROOM);
        move_obj(CYCLOPS, CYCLOPS_ROOM);
        set_attr(THIEF, INVISIBLE, 1);
        CHECK(cue_track(REL, SER, &m) == 0);
        set_attr(THIEF, INVISIBLE, 0);
        CHECK(cue_track(REL, SER, &m) == 13);
    }

    printf(fails ? "%d FAILURE(S)\n" : "all cue tests passed\n", fails);
    return fails ? 1 : 0;
}
