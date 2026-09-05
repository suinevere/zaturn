/* The shipped loop, checked for the things that make a song unplayable rather
   than merely bad: an order entry pointing at a pattern that does not exist, a
   loop point past the end of the order, a waveform index with no waveform
   behind it, or a speed of zero, which would play every row on every tick.

   The tune itself is a matter of taste and is not asserted here. What is
   asserted is that it plays at all, and that it ends where it says it does --
   a loop point off the end would run the tracker into whatever follows the
   order array in memory.

   The initialiser's length is NOT checked here; it is checked at compile time
   inside music_synth_data.c, because a short initialiser is zero-filled and
   would satisfy every assertion below while being a truncated tune.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_song \
         saturn/tests/test_synth_song.c saturn/src/sound/music_synth_data.c \
         saturn/src/sound/tracker.c && /tmp/t_song
*/
#include "../src/sound/music_synth_data.h"
#include "../src/sound/tracker.h"
#include <stdio.h>
#include <assert.h>

static void test_song_exists_and_is_shaped(void) {
    const TrackerSong *s = music_synth_song();
    assert(s != 0);
    assert(s->cells != 0);
    assert(s->order != 0);
    assert(s->rows > 0);
    assert(s->channels > 0 && s->channels <= 4);
    assert(s->order_len > 0);
    assert(s->speed > 0);
}

static void test_loop_point_is_inside_the_order(void) {
    const TrackerSong *s = music_synth_song();
    assert(s->loop_to < s->order_len);
}

static void test_every_order_entry_names_a_real_pattern(void) {
    const TrackerSong *s = music_synth_song();
    int highest = 0;
    for (int i = 0; i < s->order_len; i++)
        if (s->order[i] > highest) highest = s->order[i];
    assert(highest < MUSIC_SYNTH_PATTERNS);
}

static void test_every_cell_is_playable(void) {
    const TrackerSong *s = music_synth_song();
    int cells = MUSIC_SYNTH_PATTERNS * s->rows * s->channels;
    for (int i = 0; i < cells; i++) {
        unsigned char note = s->cells[i].note;
        if (note < 2) continue;
        int index = note - 2;
        assert(index / 12 - 2 >= -8 && index / 12 - 2 <= 7);
        assert((s->cells[i].wv >> 4) <= 4);
        assert((s->cells[i].wv & 0x0F) <= 7);
    }
}

static void test_a_null_sink_does_not_start_playback(void) {
    /* The song is data and the tracker is told where to send it; handing it
       nowhere must leave it stopped rather than dereferencing the sink on the
       first tick. */
    tracker_start(music_synth_song(), 0);
    assert(!tracker_playing());
}

int main(void) {
    test_song_exists_and_is_shaped();
    test_loop_point_is_inside_the_order();
    test_every_order_entry_names_a_real_pattern();
    test_every_cell_is_playable();
    test_a_null_sink_does_not_start_playback();
    printf("test_synth_song: all passed\n");
    return 0;
}
