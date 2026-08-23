/* The music hold latch, and the save/restore idiom the Sound options page leans on.

   There are two holds. Pause stops the drive where it stands; duck leaves the
   track running and only drops the volume. The in-game Options menu takes the
   ducked one for as long as it is up (saturn_glue.cxx pauses on the way in,
   resumes on the way out) -- the game is still underneath, so the music thins
   out rather than cutting. The loading screen is the one caller that still takes
   the hard pause, because it wants the drive quiet and out of the way while its
   own PCM jingle plays over the story reads.

   Either hold is lifted by music_resume, which reads back which one was taken.
   Getting that dispatch wrong is the failure these tests exist to catch: an
   unduck lifted by the resume path would seek the drive to a saved frame that a
   duck never recorded.

   The Sound page is the one page under the Options menu that cannot work ducked
   -- every row on it is judged by ear, at the level being set -- so it lifts on
   entry and has to put the hold back on the way out, or the rest of the Options
   menu plays at full volume. That was the original bug: the resume was there,
   the restore was not.

   It cannot restore unconditionally, either. The same page opens from the main
   menu, where nothing held the music and nothing may -- an unconditional hold on
   exit would leave the main menu's track ducked with nothing to lift it. So the
   page reads music_is_paused() on entry and only puts back what it found, which
   is the pairing these tests pin.

   Host test: music.c is plain C and takes its backend through function
   pointers, so the drive is a set of counters here.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/scene -o /tmp/t \
         saturn/tests/test_music_pause.c saturn/src/sound/music.c \
         saturn/src/sound/music_data.c saturn/src/sound/event_scan.c \
         saturn/src/scene/scene_map.c && /tmp/t
*/
#include "../src/sound/music.h"
#include <stdio.h>
#include <assert.h>

static int g_pauses, g_resumes, g_ducks, g_unducks, g_plays, g_playing, g_cats;

static void record_cat(int cat) { (void) cat; g_cats++; }

static void fake_pause(void)  { g_pauses++;  g_playing = 0; }
static void fake_resume(void) { g_resumes++; g_playing = 1; }
static void fake_duck(void)   { g_ducks++;   }   /* the drive never stops */
static void fake_unduck(void) { g_unducks++; }
static void fake_play(int track, int loop) { (void) loop; g_plays++; g_playing = track > 0; }
static int  fake_isplaying(void) { return g_playing; }

static void reset_probe(void) {
    g_pauses = g_resumes = g_ducks = g_unducks = g_plays = g_cats = 0;
}

/* Bring the engine up with something actually sounding, the way both menus are
   entered. */
static void engine_up(void) {
    music_reset();
    music_set_backend(fake_play);
    music_set_pausefns(fake_pause, fake_resume);
    music_set_duckfns(fake_duck, fake_unduck);
    music_set_isplaying(fake_isplaying);
    music_set_mix(MIX_SEQUENTIAL, MUSIC_TRACK_MIN);
    music_start();
    assert(fake_isplaying());
    reset_probe();
}

/* What the Sound page does, top and bottom. */
static int sound_page_enter(void) {
    int was_held = music_is_paused();
    music_resume();
    return was_held;
}
static void sound_page_leave(int was_held) {
    if (was_held) music_duck();
}

static void test_latch_reports_state(void) {
    engine_up();
    assert(!music_is_paused());
    music_pause();
    assert(music_is_paused());
    music_resume();
    assert(!music_is_paused());

    music_duck();
    assert(music_is_paused());      /* one latch, either hold */
    music_resume();
    assert(!music_is_paused());
    printf("  latch reports hold state:  OK\n");
}

static void test_latch_is_idempotent(void) {
    engine_up();
    music_pause();
    music_pause();                  /* second is a no-op, not a second seek */
    assert(g_pauses == 1);
    music_resume();
    music_resume();
    assert(g_resumes == 1);
    assert(!music_is_paused());

    reset_probe();
    music_duck();
    music_duck();
    assert(g_ducks == 1);
    music_resume();
    music_resume();
    assert(g_unducks == 1);
    assert(!music_is_paused());
    printf("  holds idempotent:          OK\n");
}

/* The dispatch. A duck must be lifted by unduck and never by the resume path,
   which would seek the drive to a frame the duck never saved. */
static void test_resume_lifts_the_hold_that_was_taken(void) {
    engine_up();
    music_duck();
    assert(g_ducks == 1 && g_pauses == 0);   /* ducking never stops the drive */
    music_resume();
    assert(g_unducks == 1 && g_resumes == 0);

    reset_probe();
    music_pause();
    assert(g_pauses == 1 && g_ducks == 0);
    music_resume();
    assert(g_resumes == 1 && g_unducks == 0);
    printf("  resume lifts right hold:   OK\n");
}

/* A duck leaves the track audible and running, so is_playing stays honest and
   there is no seek for the next tick to misread as loop-end. */
static void test_duck_keeps_the_track_running(void) {
    engine_up();
    music_duck();
    assert(fake_isplaying());
    music_resume();
    assert(fake_isplaying());
    reset_probe();
    music_tick();
    assert(g_plays == 0);           /* nothing looked like a track ending */
    printf("  duck keeps track running:  OK\n");
}

/* In-game Options ducks, Sound lifts to be judged by ear, and backing out has to
   leave the music ducked again for the menu underneath. */
static void test_sound_page_restores_ingame_hold(void) {
    engine_up();
    music_duck();                   /* saturn_glue.cxx, opening Options */
    assert(music_is_paused());

    int was = sound_page_enter();
    assert(was);
    assert(!music_is_paused());     /* full volume while the page is up */

    sound_page_leave(was);
    assert(music_is_paused());      /* ...and ducked again for the root menu */
    assert(g_pauses == 0);          /* never once stopped the drive */

    music_resume();                 /* saturn_glue.cxx, closing Options */
    assert(!music_is_paused());
    printf("  in-game duck restored:     OK\n");
}

/* The other half: from the main menu nothing was held, so nothing may be. */
static void test_sound_page_leaves_main_menu_playing(void) {
    engine_up();
    assert(!music_is_paused());

    int was = sound_page_enter();
    assert(!was);
    sound_page_leave(was);

    assert(!music_is_paused());
    assert(g_pauses == 0 && g_ducks == 0);   /* never touched it on the way out */
    printf("  main menu left playing:    OK\n");
}

/* Ok commits by restarting the track; the restore still has to hold that one,
   rather than being skipped because something started playing. */
static void test_restore_survives_a_commit_on_exit(void) {
    engine_up();
    music_duck();

    int was = sound_page_enter();
    music_set_mix(MIX_SEQUENTIAL, MUSIC_TRACK_MIN + 1);
    music_start();                  /* what the Ok branch does before breaking */
    assert(!music_is_paused());
    sound_page_leave(was);

    assert(music_is_paused());
    printf("  restore holds Ok's track:  OK\n");
}

/* The two holds are not interchangeable, so "already held" is not a good enough
   reason to skip one. The loading screen asks for a stop because it is about to
   read the story off the disc; a soft reset out of the in-game Options menu
   leaves a duck standing across the longjmp, and if that made the stop a no-op
   the read would contend with a CD-DA track that never stopped. */
static void test_pause_upgrades_a_duck(void) {
    engine_up();
    music_duck();
    assert(g_ducks == 1 && g_pauses == 0);

    music_pause();
    assert(g_pauses == 1);          /* the drive really was taken out of the way */
    assert(music_is_paused());

    music_resume();
    assert(g_resumes == 1);         /* and lifted as the stop it had become... */
    assert(g_unducks == 0);         /* ...not as the duck it started as */
    printf("  pause upgrades a duck:     OK\n");
}

/* The other direction does not downgrade: a stop is the stronger hold, and the
   drive is already parked, so ducking over one has nothing to do. */
static void test_duck_does_not_downgrade_a_pause(void) {
    engine_up();
    music_pause();
    reset_probe();
    music_duck();
    assert(g_ducks == 0);
    music_resume();
    assert(g_resumes == 1 && g_unducks == 0);
    printf("  duck yields to a pause:    OK\n");
}

/* A paused drive reads exactly like a track that ended. music_tick must sit
   still while THAT hold is on, or an open menu advances a track per frame. */
static void test_tick_is_inert_while_paused(void) {
    engine_up();
    music_pause();
    reset_probe();
    for (int i = 0; i < 240; i++) music_tick();
    assert(g_plays == 0);
    assert(music_is_paused());
    printf("  tick inert while paused:   OK\n");
}

/* A duck is the opposite case and must NOT park the tick. The drive is still
   running, so is_playing is honest, and a track reaching its end under an open
   menu is a real loop-end that nothing else will service -- leaving it unhandled
   is what made the music cut to silence a few passes into sitting in Options.
   Only the category-commit path is withheld while ducked, because that one
   reaches for the room's picture and the disc read would kill the very track the
   duck is keeping alive. */
static void test_tick_advances_while_ducked(void) {
    engine_up();
    music_duck();
    reset_probe();

    /* Nothing to do while the track is still running. */
    for (int i = 0; i < 60; i++) music_tick();
    assert(g_plays == 0);

    /* The track ends: the next one has to be issued despite the hold. */
    g_playing = 0;
    music_tick();
    assert(g_plays == 1);
    assert(fake_isplaying());
    assert(music_is_paused());      /* still held -- the duck did not lift */
    assert(g_unducks == 0);
    printf("  tick advances while duck:  OK\n");
}

/* And the withheld half: an armed category change must not commit under a duck,
   since committing is what reaches for art. */
static void test_duck_withholds_category_commit(void) {
    engine_up();
    music_set_category_fn(record_cat);
    music_set_debounce_frames(1);
    music_set_mix(MIX_DYNAMIC, MUSIC_TRACK_MIN);
    /* Adventure (release 1, serial "151001"), object 21 -> an authored scene
       (SC_MAZE), so the turn actually has something to commit. Without a game
       set, scene_of_room finds no row and the room is "hold whatever is
       showing" -- nothing this test could ever observe as withheld. */
    music_set_game(1, "151001");
    music_duck();
    reset_probe();

    music_on_turn(21);
    for (int i = 0; i < 120; i++) music_tick();
    assert(g_cats == 0);            /* nothing announced, so no picture asked for */

    music_resume();
    for (int i = 0; i < 120; i++) music_tick();
    assert(g_cats > 0);             /* and it lands once the hold is lifted */
    printf("  duck withholds category:   OK\n");
}

int main(void) {
    printf("test_music_pause:\n");
    test_latch_reports_state();
    test_latch_is_idempotent();
    test_resume_lifts_the_hold_that_was_taken();
    test_duck_keeps_the_track_running();
    test_sound_page_restores_ingame_hold();
    test_sound_page_leaves_main_menu_playing();
    test_restore_survives_a_commit_on_exit();
    test_pause_upgrades_a_duck();
    test_duck_does_not_downgrade_a_pause();
    test_tick_is_inert_while_paused();
    test_tick_advances_while_ducked();
    test_duck_withholds_category_commit();
    printf("test_music_pause: OK\n");
    return 0;
}
