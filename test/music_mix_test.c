/* Host tests for mix modes, scene-keyed debounce, and short-track re-pick.
   gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/scene -o /tmp/mmt \
       test/music_mix_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c \
       saturn/src/sound/event_scan.c saturn/src/scene/scene_map.c && /tmp/mmt

   Room mood used to come from classified text, so a room's category could be
   named by its pool (TC_UNDERGROUND, TC_WILDERNESS, ...) and a track's pool
   membership proved which mood picked it. It now comes from an authored
   scene table keyed by (release, serial, object number) -- these tests use
   real rows from scene/game_rooms.inc (Zork I, release 88, serial "840726")
   for object numbers, rather than room text.

   scene/game_tracks.inc's SCENE_TRACKS is all zero for every game today (no
   tracks are authored yet), so scene_track_mask is always 0 and every scene
   falls back to the same neutral pool -- there is no second pool left to
   prove a category CHANGED by. Where the old test used pool membership to
   confirm that, this one uses music_set_category_fn's announcements instead:
   the scene ids the engine actually decided on are what these tests can
   still see. */
#include <stdio.h>
#include "sound/music.h"
#include "sound/event_scan.h"
#include "scene/scene_map.h"

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

static int g_cats[32], g_ncat;
static void rec_cat(int c) { if (g_ncat < 32) g_cats[g_ncat++] = c; }

/* Zork I object numbers (see saturn/src/scene/game_rooms.inc's
   GAME_ROOM_ZORK1) mapped to distinct authored scenes: MINE_A/MINE_B share
   SC_MINE, FOREST is SC_FOREST, PARLOR is SC_PARLOR, CAVE_A/CAVE_B share
   SC_CAVE. */
#define ZORK1_RELEASE 88
#define ZORK1_SERIAL  "840726"
#define OBJ_MINE_A   16
#define OBJ_MINE_B   17
#define OBJ_FOREST   76
#define OBJ_PARLOR   193
#define OBJ_CAVE_A   38
#define OBJ_CAVE_B   39

int main(void) {
    int fails = 0;
    for (int i = 0; i < 64; i++) short_set[i] = 0;
    music_set_backend(rec);
    music_set_isplaying(isplaying);
    music_set_isshort(isshort);
    music_seed(1);
    music_set_game(ZORK1_RELEASE, ZORK1_SERIAL);

    /* --- Dynamic: first room commits immediately (nothing playing yet) --- */
    music_set_mix(MIX_DYNAMIC, 10);
    music_reset();
    music_set_debounce_frames(6);
    g_calls = 0;
    music_on_turn(OBJ_MINE_A);
    CHECK(g_calls == 1);
    CHECK(in_pool(POOL_NEUTRAL, g_track));   /* SC_MINE has no authored mask yet */
    CHECK(g_loop == 0);   /* one-shot: the engine counts the passes itself */

    /* Same scene room: smooth, no new play, no restart. */
    g_calls = 0;
    music_on_turn(OBJ_MINE_B);
    CHECK(g_calls == 0);

    /* Different scene: pending, does NOT play until the countdown elapses. */
    g_calls = 0;
    music_on_turn(OBJ_FOREST);
    CHECK(g_calls == 0);            /* still debouncing */
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_calls == 0);            /* not yet (6 frames) */
    music_tick();
    CHECK(g_calls == 1);            /* committed on the 6th tick */
    CHECK(in_pool(POOL_NEUTRAL, g_track));

    /* A scene flip before commit resets the countdown to the newest target. */
    music_set_mix(MIX_DYNAMIC, 10);
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

    /* --- Override: plays the override track looped, ignores rooms --- */
    music_set_mix(MIX_OVERRIDE, 7);
    music_reset(); g_calls = 0;
    music_start();
    CHECK(g_track == 7 && g_loop == 1);
    music_on_turn(OBJ_CAVE_A);
    CHECK(g_track == 7);           /* unchanged by room */

    /* --- Sequential: one-shot; advances on loop-end (isplaying==0) --- */
    music_set_mix(MIX_SEQUENTIAL, 5);
    music_reset(); music_start();
    CHECK(g_track == 5 && g_loop == 0);
    playing = 1; music_tick();     /* track registers as playing -> latch clears */
    playing = 0; music_tick();     /* track ended -> advance */
    CHECK(g_track == 6 && g_loop == 0);
    playing = 1; music_tick();     /* still playing -> no change */
    CHECK(g_track == 6);
    /* wrap at MAX (bounds come from music.h, so raising the track max can't
       silently leave this asserting the old wrap point) */
    music_set_mix(MIX_SEQUENTIAL, MUSIC_TRACK_MAX); music_reset(); music_start();
    CHECK(g_track == MUSIC_TRACK_MAX);
    playing = 1; music_tick();     /* settle */
    playing = 0; music_tick();
    CHECK(g_track == MUSIC_TRACK_MIN);
    playing = 1;

    /* --- Random: one-shot; picks within the track range on loop-end --- */
    music_set_mix(MIX_RANDOM, 10); music_reset(); music_start();
    CHECK(g_track >= MUSIC_TRACK_MIN && g_track <= MUSIC_TRACK_MAX && g_loop == 0);
    playing = 1; music_tick();      /* settle */
    playing = 0; int r0 = g_track; music_tick();
    CHECK(g_track >= MUSIC_TRACK_MIN && g_track <= MUSIC_TRACK_MAX);
    playing = 1; (void)r0;

    /* --- Dynamic repeats MUSIC_DYN_LOOPS times, then cycles inside the pool ---
           The track must be re-issued unchanged for passes 2..MUSIC_DYN_LOOPS, and
           only the pass after that may pick something new. A regression here reads
           as "plays once then cycles", which is exactly what a drive-side repeat
           count produced. --- */
    for (int i = 0; i < 64; i++) short_set[i] = 0;
    music_set_mix(MIX_DYNAMIC, 10); music_reset(); music_set_debounce_frames(0);
    music_on_turn(OBJ_CAVE_A);
    int room40_track = g_track;
    for (int pass = 2; pass <= MUSIC_DYN_LOOPS; pass++) {
        playing = 1; music_tick();     /* track registers as playing -> latch clears */
        playing = 0; music_tick();     /* pass ended -> same track again */
        CHECK(g_track == room40_track);
        CHECK(g_loop == 0);
    }
    playing = 1; music_tick();
    playing = 0; music_tick();         /* passes used up -> cycle */
    CHECK(g_track != room40_track);
    CHECK(in_pool(POOL_NEUTRAL, g_track));
    playing = 1;

    /* --- Same scene, new room: the track stays. Cycling is driven by loop-end
           and by a change of scene, never by the move alone -- another cave is
           the same mood as this cave. --- */
    int cave_track = g_track;
    music_on_turn(OBJ_CAVE_B);
    CHECK(g_track == cave_track);    /* the move alone does not interrupt the stream */
    CHECK(in_pool(POOL_NEUTRAL, g_track));

    /* --- A pool with only one long track has nowhere to cycle to, so loop-end
           re-issues that same track rather than dropping onto a short one. --- */
    music_set_mix(MIX_DYNAMIC, 10); music_reset(); music_set_debounce_frames(0);
    { const unsigned char* p; int n = music_track_pool(POOL_NEUTRAL, &p);
      for (int i = 0; i < n; i++) short_set[p[i]] = 1;
      short_set[p[n-1]] = 0;   /* exactly one long track */
    }
    music_on_turn(OBJ_CAVE_A);
    int only_long = g_track;
    CHECK(!isshort(only_long));
    for (int pass = 0; pass < MUSIC_DYN_LOOPS; pass++) {   /* the repeats, then the cycle */
        playing = 1; music_tick();
        playing = 0; music_tick();
        CHECK(g_track == only_long);
    }
    playing = 1;
    for (int i = 0; i < 64; i++) short_set[i] = 0;

    /* --- New room, NEW scene: the track cycles again (there is only the one
           pool to cycle within today, but the pick must still move off the
           track that was sounding) --- */
    music_set_mix(MIX_DYNAMIC, 10); music_reset(); music_set_debounce_frames(0);
    music_on_turn(OBJ_CAVE_B);
    int cave2_track = g_track;
    music_on_turn(OBJ_FOREST);
    music_tick();                    /* debounce is 0 -> the switch commits at once */
    CHECK(in_pool(POOL_NEUTRAL, g_track));
    CHECK(g_track != cave2_track);
    int forest_track = g_track;
    for (int pass = 2; pass <= MUSIC_DYN_LOOPS; pass++) {
        playing = 1; music_tick();   /* the new track registers as playing */
        playing = 0; music_tick();   /* its own passes, unaffected by the switch */
        CHECK(g_track == forest_track);
    }
    playing = 1; music_tick();
    playing = 0; music_tick();       /* passes used up -> a new neutral-pool track */
    CHECK(in_pool(POOL_NEUTRAL, g_track));
    CHECK(g_track != forest_track);
    playing = 1;

    /* --- Anti-runaway: no advance during the CD seek window ---
       Right after PlaySingle the CD block sits in SEEK for several frames and
       is_playing() reads 0 before the track has ever registered as playing.
       The engine must NOT treat that as loop-end (which caused runaway skips /
       re-rolls / re-picks). Advance only after is_playing() has first gone true. */
    music_set_mix(MIX_SEQUENTIAL, 5);
    music_reset(); music_start();
    CHECK(g_track == 5);
    int seek_track = g_track;
    playing = 0;                        /* simulate the SEEK window after PlaySingle */
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_track == seek_track);       /* must NOT advance during the seek window */
    playing = 1; music_tick();          /* track settles -> latch clears */
    playing = 0; music_tick();          /* real loop-end -> advance now */
    CHECK(g_track == 6);
    playing = 1;

    /* Random: no re-roll during the seek window. */
    music_set_mix(MIX_RANDOM, 10); music_reset(); music_start();
    int rseek = g_track;
    playing = 0;
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_track == rseek);            /* no re-roll mid-seek */
    playing = 1; music_tick();          /* settle */
    playing = 0; g_calls = 0; music_tick();
    CHECK(g_calls == 1);               /* re-rolls on real loop-end */
    playing = 1;

    /* Dynamic: the active room track is not re-picked during the seek window. */
    for (int i = 0; i < 64; i++) short_set[i] = 0;
    music_set_mix(MIX_DYNAMIC, 10); music_reset(); music_set_debounce_frames(0);
    music_on_turn(OBJ_CAVE_A);
    int dseek = g_track;
    playing = 0; g_calls = 0;
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_track == dseek);            /* not re-picked mid-seek */
    CHECK(g_calls == 0);               /* no backend play issued during the seek window */
    playing = 1;

    /* --- Menus: Dynamic has no room to classify, so music_start_menu opens on the
           selected track and then cycles it through the neutral pool. Without this
           the menu track went straight to the backend looped and repeated forever. --- */
    for (int i = 0; i < 64; i++) short_set[i] = 0;
    music_reset(); music_set_mix(MIX_DYNAMIC, 10); music_set_debounce_frames(0);
    g_calls = 0;
    music_start_menu();
    CHECK(g_calls == 1);
    CHECK(g_track == 10);              /* opens on the player's selected track */
    CHECK(g_loop == 0);                /* counted, not looped forever */
    for (int pass = 2; pass <= MUSIC_DYN_LOOPS; pass++) {
        playing = 1; music_tick();
        playing = 0; music_tick();
        CHECK(g_track == 10);
    }
    playing = 1; music_tick();
    playing = 0; music_tick();
    CHECK(g_track != 10);
    CHECK(in_pool(POOL_NEUTRAL, g_track));   /* a menu is "no particular mood" */
    playing = 1;

    /* A Sound Options preview mid-game must not drag the room's mood to neutral.
       There is no second pool left to prove this by track membership -- both a
       real scene with no authored mask and the menu's "nothing" both draw from
       POOL_NEUTRAL. What must NOT happen is a second "nothing" announcement
       while a room is still active, so this checks the category-change count
       instead: entering the room announces once; the preview must not announce
       again. */
    music_reset(); music_set_mix(MIX_DYNAMIC, 10); music_set_debounce_frames(0);
    music_set_category_fn(rec_cat);
    g_ncat = 0;
    music_on_turn(OBJ_CAVE_A);
    CHECK(g_ncat == 1);
    music_start_menu();                /* what the preview row calls */
    CHECK(g_ncat == 1);                 /* no second announcement -- still the room's scene */
    playing = 1; music_tick();
    playing = 0; music_tick();
    for (int pass = 2; pass <= MUSIC_DYN_LOOPS; pass++) { playing = 1; music_tick(); playing = 0; music_tick(); }
    CHECK(in_pool(POOL_NEUTRAL, g_track));   /* still drawing from the room's source */
    music_set_category_fn(0);
    playing = 1;

    /* --- Pause holds the track: a paused drive reads as not-playing, and every
           tick of an open menu would otherwise look like loop-end. --- */
    music_reset(); music_set_mix(MIX_DYNAMIC, 10); music_set_debounce_frames(0);
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

    /* Override survives a resume: the drive was repeating it, so the tail of the
       interrupted pass is the only loop-end Override ever sees. */
    music_reset(); music_set_mix(MIX_OVERRIDE, 7); music_start();
    CHECK(g_track == 7 && g_loop == 1);
    playing = 1; music_tick();
    music_pause(); playing = 0;
    music_resume();
    playing = 1; music_tick();
    playing = 0; g_calls = 0; music_tick();
    CHECK(g_calls == 1);
    CHECK(g_track == 7 && g_loop == 1);   /* back to repeating its own track */
    playing = 1;

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
