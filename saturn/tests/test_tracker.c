/* The sequencer: rows, speed, pattern order and the loop point.

   The tracker never touches the SCSP -- it emits note events to a sink, which
   is what makes it testable at all. The tests record every event and assert on
   the sequence, so a wrong loop point or an off-by-one in the speed divisor
   shows up as a wrong event list rather than as music that sounds slightly
   odd on hardware.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_trk \
         saturn/tests/test_tracker.c saturn/src/sound/tracker.c && /tmp/t_trk
*/
#include "../src/sound/tracker.h"
#include <stdio.h>
#include <assert.h>

typedef struct { int ch, semi, oct, wave, vol; } Event;
static Event g_ev[256];
static int   g_n;

static void sink(int channel, int semitone, int octave, int wave, int vol) {
    if (g_n < 256) {
        g_ev[g_n].ch = channel;
        g_ev[g_n].semi = semitone;
        g_ev[g_n].oct = octave;
        g_ev[g_n].wave = wave;
        g_ev[g_n].vol = vol;
        g_n++;
    }
}

/* Two channels, two rows per pattern, two patterns. Note 26 is semitone
   index 24, which is semitone 0 at octave 0 (24 % 12 = 0, 24 / 12 - 2 = 0). */
static const TrackerCell CELLS[] = {
    /* pattern 0 */
    { 26, 0x07 }, {  0, 0x00 },
    {  1, 0x00 }, { 38, 0x15 },
    /* pattern 1 */
    { 27, 0x07 }, {  0, 0x00 },
    {  0, 0x00 }, {  1, 0x00 },
};
static const unsigned char ORDER[] = { 0, 1 };

static TrackerSong song(unsigned char speed, unsigned char loop_to) {
    TrackerSong s;
    s.cells = CELLS;
    s.rows = 2;
    s.channels = 2;
    s.order = ORDER;
    s.order_len = 2;
    s.loop_to = loop_to;
    s.speed = speed;
    return s;
}

static void test_first_tick_plays_row_zero(void) {
    TrackerSong s = song(1, 0);
    g_n = 0;
    tracker_start(&s, sink);
    tracker_tick();
    assert(g_n == 1);
    assert(g_ev[0].ch == 0 && g_ev[0].semi == 0 && g_ev[0].oct == 0);
    assert(g_ev[0].wave == 0 && g_ev[0].vol == 7);
}

static void test_hold_emits_nothing(void) {
    TrackerSong s = song(1, 0);
    g_n = 0;
    tracker_start(&s, sink);
    tracker_tick();
    /* Row 0 channel 1 is a hold, so only channel 0 spoke. */
    assert(g_n == 1);
}

static void test_key_off_reaches_the_sink_as_minus_one(void) {
    TrackerSong s = song(1, 0);
    g_n = 0;
    tracker_start(&s, sink);
    tracker_tick();
    tracker_tick();
    /* Row 1: channel 0 keys off, channel 1 plays note 38 (index 36 =
       semitone 0, octave 1) on wave 1 at volume 5. */
    assert(g_n == 3);
    assert(g_ev[1].ch == 0 && g_ev[1].semi == -1);
    assert(g_ev[2].ch == 1 && g_ev[2].semi == 0 && g_ev[2].oct == 1);
    assert(g_ev[2].wave == 1 && g_ev[2].vol == 5);
}

static void test_speed_divides_the_tick_rate(void) {
    TrackerSong s = song(3, 0);
    g_n = 0;
    tracker_start(&s, sink);
    tracker_tick();
    int after_first = g_n;
    tracker_tick();
    tracker_tick();
    assert(g_n == after_first);
    tracker_tick();
    assert(g_n > after_first);
}

static void test_order_advances_to_the_next_pattern(void) {
    TrackerSong s = song(1, 0);
    g_n = 0;
    tracker_start(&s, sink);
    tracker_tick();
    tracker_tick();
    g_n = 0;
    tracker_tick();
    /* Pattern 1 row 0: note 27 is semitone index 25 -- semitone 1, octave 0. */
    assert(g_n == 1);
    assert(g_ev[0].semi == 1 && g_ev[0].oct == 0);
}

static void test_end_of_song_returns_to_the_loop_point(void) {
    TrackerSong s = song(1, 1);
    g_n = 0;
    tracker_start(&s, sink);
    for (int i = 0; i < 4; i++) tracker_tick();
    g_n = 0;
    tracker_tick();
    /* loop_to 1 means it re-enters pattern 1, not pattern 0. */
    assert(g_n == 1);
    assert(g_ev[0].semi == 1 && g_ev[0].oct == 0);
}

static void test_stop_silences_and_stays_stopped(void) {
    TrackerSong s = song(1, 0);
    g_n = 0;
    tracker_start(&s, sink);
    tracker_tick();
    tracker_stop();
    assert(!tracker_playing());
    g_n = 0;
    tracker_tick();
    assert(g_n == 0);
}

static void test_tick_before_start_is_harmless(void) {
    g_n = 0;
    tracker_stop();
    tracker_tick();
    assert(g_n == 0);
}

int main(void) {
    test_first_tick_plays_row_zero();
    test_hold_emits_nothing();
    test_key_off_reaches_the_sink_as_minus_one();
    test_speed_divides_the_tick_rate();
    test_order_advances_to_the_next_pattern();
    test_end_of_song_returns_to_the_loop_point();
    test_stop_silences_and_stays_stopped();
    test_tick_before_start_is_harmless();
    printf("test_tracker: all passed\n");
    return 0;
}
