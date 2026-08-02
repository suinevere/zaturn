/* The pause latch, and the save/restore idiom the Sound options page leans on.

   The in-game Options menu holds the drive for as long as it is up
   (saturn_glue.cxx:264 pauses, :266 resumes on the way out). The Sound page is
   the one page underneath it that cannot open in silence -- every row on it is
   judged by ear -- so it resumes on entry and has to put the pause back on the
   way out, or the rest of the Options menu plays on top of a menu that is
   supposed to be silent. That was the bug: the resume was there, the restore
   was not.

   It cannot restore unconditionally, either. The same page opens from the main
   menu, where nothing paused the music and nothing may -- an unconditional
   pause on exit would stop the main menu's track and leave nothing to turn it
   back on. So the page reads music_is_paused() on entry and only puts back what
   it found, which is the pairing these tests pin.

   Host test: music.c is plain C and takes its backend through function
   pointers, so the drive is a pair of counters here.

   Build:  cc -I../src/sound -o t test_music_pause.c ../src/sound/music.c && ./t
*/
#include "../src/sound/music.h"
#include <stdio.h>
#include <assert.h>

static int g_pauses, g_resumes, g_plays, g_playing;

static void fake_pause(void)  { g_pauses++;  g_playing = 0; }
static void fake_resume(void) { g_resumes++; g_playing = 1; }
static void fake_play(int track, int loop) { (void) loop; g_plays++; g_playing = track > 0; }
static int  fake_isplaying(void) { return g_playing; }

static void reset_probe(void) { g_pauses = g_resumes = g_plays = 0; }

/* Bring the engine up with something actually sounding, the way both menus are
   entered. */
static void engine_up(void) {
    music_reset();
    music_set_backend(fake_play);
    music_set_pausefns(fake_pause, fake_resume);
    music_set_isplaying(fake_isplaying);
    music_set_mix(MIX_SEQUENTIAL, MUSIC_TRACK_MIN);
    music_start();
    assert(fake_isplaying());
    reset_probe();
}

/* What the Sound page does, top and bottom, with the fix in place. */
static int sound_page_enter(void) {
    int was_paused = music_is_paused();
    music_resume();
    return was_paused;
}
static void sound_page_leave(int was_paused) {
    if (was_paused) music_pause();
}

static void test_latch_reports_state(void) {
    engine_up();
    assert(!music_is_paused());
    music_pause();
    assert(music_is_paused());
    music_resume();
    assert(!music_is_paused());
    printf("  latch reports pause state: OK\n");
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
    printf("  pause/resume idempotent:   OK\n");
}

/* The reported bug: in-game Options pauses, Sound resumes to be audible, and
   backing out has to leave the drive held again for the menu underneath. */
static void test_sound_page_restores_ingame_pause(void) {
    engine_up();
    music_pause();                  /* saturn_glue.cxx:264, opening Options */
    assert(music_is_paused());

    int was = sound_page_enter();
    assert(was);
    assert(!music_is_paused());     /* audible while the page is up */

    sound_page_leave(was);
    assert(music_is_paused());      /* ...and held again for the root menu */

    music_resume();                 /* saturn_glue.cxx:266, closing Options */
    assert(!music_is_paused());
    printf("  in-game pause restored:    OK\n");
}

/* The other half: from the main menu nothing was paused, so nothing may be. */
static void test_sound_page_leaves_main_menu_playing(void) {
    engine_up();
    assert(!music_is_paused());

    int was = sound_page_enter();
    assert(!was);
    sound_page_leave(was);

    assert(!music_is_paused());
    assert(g_pauses == 0);          /* never touched the drive on the way out */
    printf("  main menu left playing:    OK\n");
}

/* Ok commits by restarting the track; the restore still has to hold that one,
   rather than being skipped because something started playing. */
static void test_restore_survives_a_commit_on_exit(void) {
    engine_up();
    music_pause();

    int was = sound_page_enter();
    music_set_mix(MIX_SEQUENTIAL, MUSIC_TRACK_MIN + 1);
    music_start();                  /* what the Ok branch does before breaking */
    assert(!music_is_paused());
    sound_page_leave(was);

    assert(music_is_paused());
    printf("  restore holds Ok's track:  OK\n");
}

/* A paused drive reads exactly like a track that ended. music_tick must sit
   still while the latch is set, or an open menu advances a track per frame. */
static void test_tick_is_inert_while_paused(void) {
    engine_up();
    music_pause();
    reset_probe();
    for (int i = 0; i < 240; i++) music_tick();
    assert(g_plays == 0);
    assert(music_is_paused());
    printf("  tick inert while paused:   OK\n");
}

int main(void) {
    printf("test_music_pause:\n");
    test_latch_reports_state();
    test_latch_is_idempotent();
    test_sound_page_restores_ingame_pause();
    test_sound_page_leaves_main_menu_playing();
    test_restore_survives_a_commit_on_exit();
    test_tick_is_inert_while_paused();
    printf("test_music_pause: OK\n");
    return 0;
}
