/* Host tests for the dynamic mix, room-keyed debounce, and short-track
   re-pick. Repeat, Sequential and Random were removed along with the Sound
   Options rows that fed them, so the only mix left is the one keyed on the
   room, and every case below is that one.

   gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/scene -o /tmp/mmt \
       test/music_mix_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c \
       saturn/src/sound/event_scan.c saturn/src/scene/presentation.c && /tmp/mmt

   Room mood used to come from classified text, then from a scene table. Both
   are gone: it now comes from the story's own authored per-room table, where a
   room's category IS its CD-DA track. So these cases link the real
   presentation.c and use real Zork I object numbers, and the track the engine
   issues is directly what they assert on -- no announcement subscriber and no
   pool-membership indirection is needed to see which category was chosen.

   Two rooms sharing a track share a category, which is what the no-restart and
   debounce cases below turn on; the object numbers are chosen for exactly that
   property and the comment beside each records the track it carries. */
#include <stdio.h>
#include "sound/music.h"
#include "sound/event_scan.h"
#include "scene/presentation.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } }while(0)

/* One past the last EV_* id, the same convention music.c/music_data.c share
   privately for "the neutral fallback pool", reconstructed here since it is
   not part of music.h's public surface. */
#define POOL_NEUTRAL EVENT_N

static int g_track = 0, g_loop = 0, g_calls = 0;
static void rec(int track, int loop) { g_track = track; g_loop = loop; g_calls++; }
static int playing = 1;
static int isplaying(void) { return playing; }
static int short_set[64];
static int isshort(int t) { return (t >= 0 && t < 64) ? short_set[t] : 0; }
static int in_pool(int cat, int tr) {
    const unsigned char* p; int n = music_track_pool(cat, &p);
    for (int i = 0; i < n; i++) if (p[i] == tr) return 1;
    return 0;
}

/* Zork I object numbers, from saturn/src/scene/game_presentation.inc's
   GAME_PRES_ZORK1, chosen so each pair shares a track and each singleton
   carries a different one. The names are the rooms' own areas; what the cases
   turn on is the track beside each. */
#define ZORK1_RELEASE 88
#define ZORK1_SERIAL  "840726"
#define OBJ_MINE_A   16    /* track 18 */
#define OBJ_MINE_B   17    /* track 18 -- same category as MINE_A */
#define OBJ_FOREST   76    /* track 11 */
#define OBJ_PARLOR   193   /* track 10 */
#define OBJ_CAVE_A   38    /* track 4  */
#define OBJ_CAVE_B   41    /* track 4  -- same category as CAVE_A */

int main(void) {
    int fails = 0;
    for (int i = 0; i < 64; i++) short_set[i] = 0;
    music_set_backend(rec);
    music_set_isplaying(isplaying);
    music_set_isshort(isshort);
    music_seed(1);
    music_set_game(ZORK1_RELEASE, ZORK1_SERIAL);

    /* --- Dynamic: first room commits immediately (nothing playing yet) --- */
    music_reset();
    music_set_debounce_frames(6);
    g_calls = 0;
    music_on_turn(OBJ_MINE_A);
    CHECK(g_calls == 1);
    CHECK(g_track == 18);                    /* the room's own authored track */
    CHECK(g_loop == 0);   /* one-shot: the engine counts the passes itself */

    /* A room sharing that track: smooth, no new play, no restart. */
    g_calls = 0;
    music_on_turn(OBJ_MINE_B);
    CHECK(g_calls == 0);

    /* A room on a different track: pending, does NOT play until the countdown
       elapses. */
    g_calls = 0;
    music_on_turn(OBJ_FOREST);
    CHECK(g_calls == 0);            /* still debouncing */
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_calls == 0);            /* not yet (6 frames) */
    music_tick();
    CHECK(g_calls == 1);            /* committed on the 6th tick */
    CHECK(in_pool(POOL_NEUTRAL, g_track));

    /* A scene flip before commit resets the countdown to the newest target. */
    music_reset(); music_set_debounce_frames(6);
    music_on_turn(OBJ_MINE_A);      /* immediate SC_MINE */
    music_on_turn(OBJ_FOREST);      /* pending SC_FOREST */
    music_tick(); music_tick();     /* 2 frames */
    music_on_turn(OBJ_PARLOR);      /* flip -> SC_PARLOR, reset */
    g_calls = 0;
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_calls == 0);
    music_tick();
    CHECK(g_calls == 1);
    CHECK(in_pool(POOL_NEUTRAL, g_track));

    /* --- music_start starts nothing: there is no room yet to key off, and the
           mode that used to play a track outright is gone. --- */
    music_reset(); g_calls = 0;
    music_start();
    CHECK(g_calls == 0);

    /* --- Dynamic repeats MUSIC_DYN_LOOPS times, then cycles inside the pool ---
           The track must be re-issued unchanged for passes 2..MUSIC_DYN_LOOPS, and
           only the pass after that may pick something new. A regression here reads
           as "plays once then cycles", which is exactly what a drive-side repeat
           count produced. --- */
    for (int i = 0; i < 64; i++) short_set[i] = 0;
    music_reset(); music_set_debounce_frames(0);
    music_on_turn(OBJ_CAVE_A);
    int room40_track = g_track;
    for (int pass = 2; pass <= MUSIC_DYN_LOOPS; pass++) {
        playing = 1; music_tick();     /* track registers as playing -> latch clears */
        playing = 0; music_tick();     /* pass ended -> same track again */
        CHECK(g_track == room40_track);
        CHECK(g_loop == 0);
    }
    playing = 1; music_tick();
    playing = 0; music_tick();         /* passes used up -> re-issue, not cycle */
    /* An authored room has nowhere to cycle TO: its track is its category, so
       the pick that would choose a new one hands back the same number. The
       original release loops one track per room, and this is that. */
    CHECK(g_track == room40_track);
    playing = 1;

    /* --- Another room on the same track: the track stays. Cycling is driven by
           loop-end and by a change of category, never by the move alone -- and
           two rooms sharing a track share a category. --- */
    int cave_track = g_track;
    music_on_turn(OBJ_CAVE_B);
    CHECK(g_track == cave_track);    /* the move alone does not interrupt the stream */

    /* --- An authored room's track is re-issued across every pass boundary,
           whether or not the drive reports it short. The short-track re-pick
           applies to a pool draw, and an authored room is not one. --- */
    music_reset(); music_set_debounce_frames(0);
    for (int i = 0; i < 64; i++) short_set[i] = 1;   /* every track reads short */
    music_on_turn(OBJ_CAVE_A);
    int room_track = g_track;
    CHECK(room_track == 4);                          /* CAVE_A's authored track */
    for (int pass = 0; pass < MUSIC_DYN_LOOPS; pass++) {
        playing = 1; music_tick();
        playing = 0; music_tick();
        CHECK(g_track == room_track);
    }
    playing = 1;
    for (int i = 0; i < 64; i++) short_set[i] = 0;

    /* --- New room on a DIFFERENT track: the music swings to it, and then stays
           there through its own passes. --- */
    music_reset(); music_set_debounce_frames(0);
    music_on_turn(OBJ_CAVE_B);
    int cave2_track = g_track;
    CHECK(cave2_track == 4);
    music_on_turn(OBJ_FOREST);
    music_tick();                    /* debounce is 0 -> the switch commits at once */
    CHECK(g_track == 11);            /* FOREST's authored track */
    CHECK(g_track != cave2_track);
    int forest_track = g_track;
    for (int pass = 2; pass <= MUSIC_DYN_LOOPS; pass++) {
        playing = 1; music_tick();   /* the new track registers as playing */
        playing = 0; music_tick();   /* its own passes, unaffected by the switch */
        CHECK(g_track == forest_track);
    }
    playing = 1; music_tick();
    playing = 0; music_tick();       /* passes used up -> re-issued, not replaced */
    CHECK(g_track == forest_track);
    playing = 1;

    /* --- Anti-runaway: no advance during the CD seek window ---
       Right after PlaySingle the CD block sits in SEEK for several frames and
       is_playing() reads 0 before the track has ever registered as playing.
       The engine must NOT treat that as loop-end (which caused runaway skips /
       re-rolls / re-picks). Advance only after is_playing() has first gone true. */
    /* Dynamic: the active room track is not re-picked during the seek window. */
    for (int i = 0; i < 64; i++) short_set[i] = 0;
    music_reset(); music_set_debounce_frames(0);
    music_on_turn(OBJ_CAVE_A);
    int dseek = g_track;
    playing = 0; g_calls = 0;
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_track == dseek);            /* not re-picked mid-seek */
    CHECK(g_calls == 0);               /* no backend play issued during the seek window */
    playing = 1;

    /* --- Menus: there is no room to classify, so music_start_menu opens on the
           neutral pool -- "no particular mood", which is what a menu is -- and
           then cycles inside it. It used to open on the track picked in Sound
           Options; that row is gone. Without this the menu track went straight to
           the backend looped and repeated forever. --- */
    for (int i = 0; i < 64; i++) short_set[i] = 0;
    music_reset(); music_set_debounce_frames(0);
    g_calls = 0;
    music_start_menu();
    CHECK(g_calls == 1);
    CHECK(in_pool(POOL_NEUTRAL, g_track));
    CHECK(g_loop == 0);                /* counted, not looped forever */
    int menu_track = g_track;
    for (int pass = 2; pass <= MUSIC_DYN_LOOPS; pass++) {
        playing = 1; music_tick();
        playing = 0; music_tick();
        CHECK(g_track == menu_track);
    }
    playing = 1; music_tick();
    playing = 0; music_tick();
    CHECK(in_pool(POOL_NEUTRAL, g_track));   /* still "no particular mood" */
    playing = 1;

    /* music_start_menu mid-game must not drag the room's mood to neutral. It
       always issues a play -- that is its job, the menu needs something
       sounding -- so the count says nothing; what it must not do is change the
       CATEGORY out from under the room. With the room's track being its
       category, that is directly visible: the track it issues has to be the
       room's own, not a neutral-pool draw. */
    music_reset(); music_set_debounce_frames(0);
    g_calls = 0;
    music_on_turn(OBJ_CAVE_A);
    CHECK(g_calls == 1);                /* the room's own track, issued once */
    CHECK(g_track == 4);
    music_start_menu();
    CHECK(g_track == 4);                /* still the room's track, not neutral */
    playing = 1; music_tick();
    playing = 0; music_tick();
    for (int pass = 2; pass <= MUSIC_DYN_LOOPS; pass++) { playing = 1; music_tick(); playing = 0; music_tick(); }
    CHECK(g_track == 4);                /* and it holds across the pass boundary */
    playing = 1;

    /* --- Pause holds the track: a paused drive reads as not-playing, and every
           tick of an open menu would otherwise look like loop-end. --- */
    music_reset(); music_set_debounce_frames(0);
    music_start_menu();
    playing = 1; music_tick();
    int held = g_track;
    music_pause();
    playing = 0;                       /* the drive is stopped, as it would really be */
    g_calls = 0;
    for (int i = 0; i < 30; i++) music_tick();
    CHECK(g_calls == 0);               /* nothing started while paused */
    CHECK(g_track == held);
    music_resume();
    playing = 0; music_tick();         /* resume seeks: must not read as loop-end */
    CHECK(g_track == held);
    playing = 1; music_tick();         /* the tail registers as playing */
    playing = 0; music_tick();         /* now the interrupted pass really ends */
    CHECK(g_track == held);            /* pass 2 of 3, so the same track again */
    playing = 1;

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
