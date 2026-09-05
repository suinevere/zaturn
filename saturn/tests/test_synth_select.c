/* Choosing a tune, and holding one. Everything here exists because the synth
   stopped being a single loop: the room engine now drives it through
   synth_play_track, which is called again on every room change whether or not
   the room's mood moved.

   The assertion that matters most is the one about asking for the tune already
   playing. music.c re-asserts its choice constantly -- a room change inside a
   category, a return from a menu, a fade landing -- and if each of those
   restarted the loop, a walk through four rooms of one mood would chop the
   music into four first bars. Nothing else in the system would report that;
   it would just sound wrong.

   Run it both ways: without -DNETBIN the CD build carries one tune, and the
   two-tune assertions below skip themselves rather than fail. With -DNETBIN
   they are the ones that matter.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_sel \
         saturn/tests/test_synth_select.c saturn/src/sound/synth.c \
         saturn/src/sound/scsp.c saturn/src/sound/tracker.c \
         saturn/src/sound/synth_waves.c \
         saturn/src/sound/music_synth_data.c && /tmp/t_sel
*/
#include "../src/sound/synth.h"
#include "../src/sound/scsp.h"
#include "../src/sound/music_synth_data.h"
#include <stdio.h>
#include <assert.h>

static unsigned short g_regs[SCSP_REG_WORDS];
static signed char    g_wave[SCSP_WAVE_BYTES];

#define SLOT_WORD(voice, off) g_regs[(SCSP_SLOT_FIRST + (voice)) * 16 + ((off) / 2)]
#define KEYED_ON(voice)       ((SLOT_WORD(voice, 0x00) & (1u << 11)) != 0)

static void bind_fresh(void) {
    for (int i = 0; i < SCSP_REG_WORDS; i++) g_regs[i] = 0;
    for (int i = 0; i < SCSP_WAVE_BYTES; i++) g_wave[i] = 0;
    synth_bind(g_regs, g_wave, 0x70000UL);
    synth_init();
    synth_stop();
}

static void test_start_uses_the_default(void) {
    bind_fresh();
    synth_start();
    assert(synth_song() == MUSIC_SYNTH_DEFAULT);
    assert(synth_song_count() == music_synth_song_count());
}

static void test_a_different_song_replaces_the_one_playing(void) {
    bind_fresh();
    if (synth_song_count() < 2) return;     /* nothing to switch to */
    synth_start_song(0);
    synth_tick();
    synth_start_song(1);
    assert(synth_song() == 1);
    assert(synth_playing());
}

static void test_asking_for_the_playing_song_again_does_not_restart_it(void) {
    /* Checked by position and not by the call returning: the tune has to be
       where it was, not merely still going. Two ticks in, the tracker is part
       way through the first row's hold; a restart would put it back on row 0
       and the next tick would key the first row's notes a second time. */
    bind_fresh();
    if (synth_song_count() < 2) return;
    synth_start_song(1);
    for (int i = 0; i < 40; i++) synth_tick();
    unsigned short before[SCSP_REG_WORDS];
    for (int i = 0; i < SCSP_REG_WORDS; i++) before[i] = g_regs[i];
    synth_start_song(1);
    for (int i = 0; i < SCSP_REG_WORDS; i++) assert(g_regs[i] == before[i]);
    assert(synth_song() == 1);
}

static void test_a_track_number_picks_a_tune_and_zero_stops(void) {
    bind_fresh();
    synth_play_track(MUSIC_SYNTH_TRACK_MIN, 0);
    assert(synth_playing());
    assert(synth_song() == music_synth_song_for_track(MUSIC_SYNTH_TRACK_MIN));
    synth_play_track(0, 0);
    assert(!synth_playing());
    for (int v = 0; v < SCSP_VOICES; v++) assert(!KEYED_ON(v));
}

static void test_nothing_the_synth_plays_is_short(void) {
    /* The engine's loop-end rules key off this. Every tune loops forever, so a
       yes here would have music.c cycling to another track a few seconds in. */
    for (int t = 0; t <= MUSIC_SYNTH_TRACK_MAX + 1; t++)
        assert(synth_track_is_short(t) == 0);
}

static void test_pause_silences_the_voices_and_keeps_the_place(void) {
    bind_fresh();
    synth_start_song(MUSIC_SYNTH_DEFAULT);
    for (int i = 0; i < 20; i++) synth_tick();
    synth_pause();
    for (int v = 0; v < SCSP_VOICES; v++) assert(!KEYED_ON(v));
    /* Still "playing" as far as the engine is concerned: a paused tune has not
       ended, and music.c reads not-playing as loop-end. */
    assert(synth_playing());
    for (int i = 0; i < 200; i++) synth_tick();
    for (int v = 0; v < SCSP_VOICES; v++) assert(!KEYED_ON(v));
    synth_resume();
    int keyed = 0;
    for (int i = 0; i < 200 && !keyed; i++) {
        synth_tick();
        for (int v = 0; v < SCSP_VOICES; v++) if (KEYED_ON(v)) keyed = 1;
    }
    assert(keyed);
}

static void test_resuming_a_stopped_synth_stays_stopped(void) {
    /* synth_stop clears the pause, so a resume that arrives after a stop must
       not bring the tracker back -- the engine pairs pause with resume, but a
       Restart can longjmp out between them. */
    bind_fresh();
    synth_start();
    synth_pause();
    synth_stop();
    synth_resume();
    assert(!synth_playing());
    synth_tick();
    for (int v = 0; v < SCSP_VOICES; v++) assert(!KEYED_ON(v));
}

int main(void) {
    test_start_uses_the_default();
    test_a_different_song_replaces_the_one_playing();
    test_asking_for_the_playing_song_again_does_not_restart_it();
    test_a_track_number_picks_a_tune_and_zero_stops();
    test_nothing_the_synth_plays_is_short();
    test_pause_silences_the_voices_and_keeps_the_place();
    test_resuming_a_stopped_synth_stays_stopped();
    printf("test_synth_select: all passed\n");
    return 0;
}
