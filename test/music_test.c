/* Host unit tests for the pure-C music engine: the three surviving track pools,
   the rule that a random draw never lands on a track a cue has spoken for, the
   RNG-backed pool pick, and music_note_output's overflow behaviour now that
   event_scan -- not a room classifier -- is what reads the accumulated turn
   text.

   The keyword-based room classifier this engine used to read moods from is
   gone entirely, along with its own test coverage; room mood now comes from
   the authored table in saturn/src/scene, which this file does not depend on.
   Its danger/triumph events went with it: event_scan now recognises the
   Z-machine death banner and nothing else.

   Build:
     gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/scene \
         -I saturn/src/engine -o /tmp/mt test/music_test.c \
         saturn/src/sound/music.c saturn/src/sound/music_data.c \
         saturn/src/sound/event_scan.c saturn/src/scene/presentation.c \
         saturn/src/scene/cues.c saturn/src/engine/room_model.c && ./mt */
#include <stdio.h>
#include <string.h>
#include "sound/music.h"
#include "sound/event_scan.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)

/* One past the last EV_* id -- the neutral-pool selector music.c and
   music_data.c share privately, reconstructed here since it is not part of
   music.h's public surface. */
#define POOL_NEUTRAL EVENT_N

/* --- engine harness. The category callback is gone, so what the engine
   decided has to be read off the track the backend was asked to play. --- */
static int  g_ov_track;
static void ov_play(int t, int loop) { (void) loop; g_ov_track = t; }
static int  ov_isplaying(void) { return 1; }

/* The drive reporting the track ended, which is what loop-end reads. */
static int g_playing = 1;
static int ov_isplaying_var(void) { return g_playing; }
static void track_ended(void) {
    g_playing = 1; music_tick();       /* clear the seen-playing latch */
    g_playing = 0; music_tick();       /* now it reads as loop-end */
    g_playing = 1;
}

/* Records which halves of a transition's ramp the engine actually drives. */
static int g_fade_art_seen, g_fade_audio_seen, g_fade_calls;
static void rec_fade(int level, int audio, int art) {
    (void) level;
    g_fade_calls++;
    if (audio) g_fade_audio_seen++;
    if (art)   g_fade_art_seen++;
}
static void fade_watch_reset(void) {
    g_fade_art_seen = 0; g_fade_audio_seen = 0; g_fade_calls = 0;
}

/* Whether a track is in a given pool. */
static int in_pool(int cat, int track) {
    const unsigned char* p; int n = music_track_pool(cat, &p), i;
    for (i = 0; i < n; i++) if (p[i] == track) return 1;
    return 0;
}

int main(void) {
    int fails = 0;

    /* --- data tables: the three surviving pools --- */
    {
        const unsigned char* p;
        int n = music_track_pool(POOL_NEUTRAL, &p), i;
        CHECK(n > 0);
        for (i = 0; i < n; i++) CHECK(p[i] >= MUSIC_TRACK_MIN && p[i] < MUSIC_TRACK_MAX);

        n = music_track_pool(EV_LOSE, &p);
        CHECK(n > 0);
        for (i = 0; i < n; i++) CHECK(p[i] >= MUSIC_TRACK_MIN && p[i] < MUSIC_TRACK_MAX);

        n = music_track_pool(EV_WIN, &p);
        CHECK(n > 0);
        for (i = 0; i < n; i++) CHECK(p[i] >= MUSIC_TRACK_MIN && p[i] < MUSIC_TRACK_MAX);

        CHECK(music_track_pool(-1, &p) == 0);
        CHECK(music_track_pool(POOL_NEUTRAL + 1, &p) == 0);   /* one past the last valid selector */
    }

    /* --- the reserved list, and the rule that the neutral draw honours it --- */
    {
        const unsigned char* p;
        int n, i;
        /* The cues, the fanfares, the stings and the muted duplicate. */
        CHECK(music_track_reserved(13));
        CHECK(music_track_reserved(17));
        CHECK(music_track_reserved(19));
        CHECK(music_track_reserved(25));
        CHECK(music_track_reserved(30));
        CHECK(music_track_reserved(32));
        /* The room themes are not reserved. */
        CHECK(!music_track_reserved(2));
        CHECK(!music_track_reserved(11));
        CHECK(!music_track_reserved(20));
        CHECK(!music_track_reserved(31));
        /* Off the disc entirely: not reserved, because it is not a track. */
        CHECK(!music_track_reserved(0));
        CHECK(!music_track_reserved(1));
        CHECK(!music_track_reserved(33));
        CHECK(!music_track_reserved(-1));

        /* The neutral pool holds none of them, and no draw from it can return
           one however the pool is later edited. */
        n = music_track_pool(POOL_NEUTRAL, &p);
        for (i = 0; i < n; i++) CHECK(!music_track_reserved(p[i]));
        music_seed(4242);
        for (i = 0; i < 400; i++) {
            int tr = music_category_track(POOL_NEUTRAL);
            CHECK(tr > 0);
            CHECK(!music_track_reserved(tr));
            CHECK(in_pool(POOL_NEUTRAL, tr));
        }

        /* The ending pools are exempt: an ending playing the ending theme is
           the point, so the filter must not empty them out. */
        music_seed(99);
        for (i = 0; i < 50; i++) {
            CHECK(music_category_track(EV_WIN) == 30);
            CHECK(in_pool(EV_LOSE, music_category_track(EV_LOSE)));
        }
    }

    /* --- event scan: the death banner and nothing else --- */
    CHECK(event_scan("**** You have died ****") == EV_LOSE);
    CHECK(event_scan("A hideous monster lunges to attack!") == EV_NONE);
    CHECK(event_scan("A pile of gold and a jewel gleam here.") == EV_NONE);
    CHECK(event_scan("You wait.") == EV_NONE);
    CHECK(event_scan(0) == EV_NONE);

    /* --- RNG-backed pool pick --- */
    music_seed(12345);
    {
        int t;
        for (t = 0; t < 50; t++) CHECK(in_pool(EV_LOSE, music_category_track(EV_LOSE)));
    }
    CHECK(music_category_track(-1) == 0);
    /* Same seed -> same sequence (deterministic for tests). */
    {
        int a1, a2;
        music_seed(777); a1 = music_category_track(POOL_NEUTRAL);
        music_seed(777); a2 = music_category_track(POOL_NEUTRAL);
        CHECK(a1 == a2);
    }

    /* --- music_note_output keeps the NEWEST bytes on overflow, not the oldest --
       Put the death banner at the front of a turn that alone blows past
       MUSIC_TEXT_MAX, pad past the limit with banner-free filler, then close
       with no banner at all. If the buffer kept the oldest bytes (the
       pre-fix behaviour) the banner would still be in there and the engine
       would play the lose pool; keeping the newest drops it, and an unmapped
       game falls back to the neutral pool instead.

       The two pools are disjoint by construction -- every track in the lose
       pool is reserved and no track in the neutral pool is -- so which one the
       engine drew from is exactly what the played track reports. */
    {
        char pad[600];
        int i;
        for (i = 0; i < 599; i++) pad[i] = 'x';
        pad[599] = 0;

        music_set_backend(ov_play);
        music_set_isplaying(ov_isplaying);
        music_set_game(0, "000000");
        music_reset();

        g_ov_track = 0;
        music_note_output("**** You have died **** ", 25);
        music_note_output(pad, 599);
        music_note_output("Nothing else happens here.", 27);
        music_on_turn(500);
        CHECK(g_ov_track != 0);                       /* an unmapped room still plays */
        CHECK(!music_track_reserved(g_ov_track));     /* from the neutral pool */
        CHECK(in_pool(POOL_NEUTRAL, g_ov_track));     /* the banner did not survive the trim */
    }

    /* --- a short turn (well under MUSIC_TEXT_MAX) scans exactly as before --
       the non-overflow append path is untouched by the fix, so the same banner
       now unpadded is detected and the lose pool plays. --- */
    {
        music_set_backend(ov_play);
        music_set_isplaying(ov_isplaying);
        music_set_game(0, "000000");
        music_reset();

        g_ov_track = 0;
        music_note_output("**** You have died ****", 24);
        music_on_turn(600);
        CHECK(in_pool(EV_LOSE, g_ov_track));          /* the banner WAS detected this time */
    }

    /* --- the menu track: named on the opening, drawn on a Return to Title ---
       The opening has to introduce the machine the same way every session, so
       music_start_menu holds the track it is given instead of cycling off it
       the way a drawn one does. --- */
    {
        int i;
        music_set_backend(ov_play);
        music_set_isplaying(ov_isplaying_var);
        music_set_debounce_frames(1);
        music_set_fade_frames(0);
        music_set_game(0, "000000");
        music_reset();

        g_ov_track = 0;
        music_start_menu(MUSIC_OPENING_TRACK);
        CHECK(g_ov_track == MUSIC_OPENING_TRACK);
        /* Four loop ends -- past MUSIC_DYN_LOOPS, where a pool-drawn track
           cycles to another one -- and it is still the same track. */
        for (i = 0; i < 4; i++) {
            track_ended();
            CHECK(g_ov_track == MUSIC_OPENING_TRACK);
        }
        /* And it is not reserved, so a Return to Title can draw it too. */
        CHECK(!music_track_reserved(MUSIC_OPENING_TRACK));

        /* A Return to Title passes 0 and draws from the neutral pool. Repeated
           over fresh resets because one draw proves nothing about a pool. */
        for (i = 0; i < 60; i++) {
            music_reset();
            g_ov_track = 0;
            music_start_menu(0);
            CHECK(in_pool(POOL_NEUTRAL, g_ov_track));
            CHECK(!music_track_reserved(g_ov_track));
        }
    }

    /* --- music off: nothing issued, and nothing left holding the prompt ---
       run_room_transition spins the game on every frame music_transition_active
       reports, so an armed switch with the music off is a second and a half of
       frozen prompt for a swap nobody can hear. The contrast pass first, so the
       off pass cannot pass by doing nothing at all. --- */
    {
        int i;
        music_set_backend(ov_play);
        music_set_isplaying(ov_isplaying_var);
        music_set_debounce_frames(30);
        music_set_fade_frames(45);
        music_set_game(0, "000000");

        /* Music on, an unmapped game: every room shares the neutral category, so
           what arms a transition is the rotation after MUSIC_ROTATE_ROOMS. */
        music_set_audible(1);
        music_reset();
        g_ov_track = 0;
        for (i = 0; i <= MUSIC_ROTATE_ROOMS; i++) music_on_turn(500 + (unsigned) i);
        CHECK(g_ov_track != 0);                  /* it played */
        CHECK(music_transition_active() != 0);   /* and armed the switch that pauses */

        /* Same walk with the music off. */
        music_reset();
        music_set_audible(0);
        g_ov_track = 0;
        for (i = 0; i <= MUSIC_ROTATE_ROOMS; i++) music_on_turn(500 + (unsigned) i);
        CHECK(g_ov_track == 0);                  /* nothing was ever issued */
        CHECK(music_transition_active() == 0);   /* and nothing holds the prompt */

        /* Switching it back on starts the category that was tracked all along,
           without making the player walk into another room to hear it. */
        music_set_audible(1);
        CHECK(g_ov_track != 0);
        CHECK(in_pool(POOL_NEUTRAL, g_ov_track));
        CHECK(!music_track_reserved(g_ov_track));

        /* And off again mid-flight drops the audio half rather than leaving a
           ramp running with nothing to fade. */
        music_reset();
        music_set_audible(1);
        for (i = 0; i <= MUSIC_ROTATE_ROOMS; i++) music_on_turn(600 + (unsigned) i);
        CHECK(music_transition_active() != 0);
        music_set_audible(0);
        CHECK(music_transition_active() == 0);
        music_set_audible(1);
    }

    /* --- a track change holds nobody up; a picture change does ---
       run_room_transition waits on music_transition_art, not on
       music_transition_active, because only a picture has to be put up unseen.
       The ramp still runs for a track change -- it is a volume ramp -- but it
       drives neither the screen nor the sound effects, and the client is free to
       let it finish under the read loop. --- */
    {
        int i;
        music_set_backend(ov_play);
        music_set_isplaying(ov_isplaying_var);
        music_set_debounce_frames(2);
        music_set_fade_frames(3);
        music_set_fade_fn(rec_fade);
        music_set_game(0, "000000");
        music_set_audible(1);

        /* An unmapped story: nothing it does can ever move a picture, so every
           transition it makes is one the client must not wait for. */
        music_reset();
        for (i = 0; i <= MUSIC_ROTATE_ROOMS; i++) music_on_turn(700 + (unsigned) i);
        CHECK(music_transition_active() != 0);
        CHECK(music_transition_art() == 0);
        fade_watch_reset();
        for (i = 0; i < 60 && music_transition_active(); i++) music_tick();
        CHECK(music_transition_active() == 0);   /* it did finish, unattended */
        CHECK(g_fade_calls > 0);
        CHECK(g_fade_audio_seen > 0);            /* the volume rode the ramp */
        CHECK(g_fade_art_seen == 0);             /* the screen never moved */

        /* A picture change, with no track change under it: the mirror image. */
        music_reset();
        music_on_turn(800);
        music_art_change(1);
        CHECK(music_transition_active() != 0);
        CHECK(music_transition_art() != 0);
        fade_watch_reset();
        for (i = 0; i < 60 && music_transition_active(); i++) music_tick();
        CHECK(music_transition_active() == 0);
        CHECK(g_fade_art_seen > 0);              /* the screen went dark for it */
        CHECK(g_fade_audio_seen == 0);           /* the track was not re-issued */

        /* An art change walked back out of arms nothing to wait for. */
        music_reset();
        music_on_turn(900);
        music_art_change(1);
        CHECK(music_transition_art() != 0);
        music_art_change(0);
        CHECK(music_transition_art() == 0);

        music_set_fade_fn(0);
        music_set_fade_frames(0);
    }

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
