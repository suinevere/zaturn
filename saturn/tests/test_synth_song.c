/* The shipped catalogue, checked for the things that make a song unplayable
   rather than merely bad: an order entry pointing at a pattern that does not
   exist, a loop point past the end of the order, a waveform index with no
   waveform behind it, or a speed of zero, which would play every row on every
   tick.

   Every tune is checked, not only the default. They now share one cell array
   and one order array, each pointing at its own offset inside them, so the way
   this goes wrong is no longer "the tune is short" but "tune n reads tune n+1"
   -- which is why the bounds below are computed from the arrays' declared
   totals and not from any one song.

   The tunes themselves are a matter of taste and are not asserted here. What
   is asserted is that they play at all, and that each ends where it says it
   does -- a loop point off the end would run the tracker into whatever follows
   the order array in memory.

   The initialiser lengths are NOT checked here; they are checked at compile
   time inside music_synth_data.c, because a short initialiser is zero-filled
   and would satisfy every assertion below while being a truncated catalogue.

   Run it BOTH ways. The catalogue is cut to a prefix on the CD target -- twelve
   tunes is 55 KB of .rodata and __heap_start follows it, which took the heap
   below what the largest story needs -- so the two builds have different song
   counts and different array lengths, and a mistake in that cut only shows in
   one of them.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_song \
         saturn/tests/test_synth_song.c saturn/src/sound/music_synth_data.c \
         saturn/src/sound/tracker.c && /tmp/t_song
     ...and again with -DNETBIN for the full catalogue.
*/
#include "../src/sound/music_synth_data.h"
#include "../src/sound/tracker.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_the_catalogue_is_not_empty(void) {
    assert(music_synth_song_count() == MUSIC_SYNTH_SONGS);
    assert(music_synth_song_count() > 0);
    assert(MUSIC_SYNTH_DEFAULT >= 0 && MUSIC_SYNTH_DEFAULT < MUSIC_SYNTH_SONGS);
    assert(music_synth_song() == music_synth_song_at(MUSIC_SYNTH_DEFAULT));
}

static void test_every_song_is_shaped(void) {
    for (int i = 0; i < music_synth_song_count(); i++) {
        const TrackerSong *s = music_synth_song_at(i);
        assert(s != 0);
        assert(s->cells != 0);
        assert(s->order != 0);
        assert(s->rows == MUSIC_SYNTH_ROWS);
        assert(s->channels > 0 && s->channels <= 4);
        assert(s->order_len > 0);
        assert(s->speed > 0);
        assert(s->loop_to < s->order_len);
    }
}

static void test_every_song_has_a_name(void) {
    for (int i = 0; i < music_synth_song_count(); i++) {
        const char *n = music_synth_song_name(i);
        assert(n != 0);
        assert(n[0] != '\0');
    }
}

static void test_a_bad_index_gives_the_default(void) {
    /* Not nothing: a caller that computed an index wrong should hear the wrong
       tune, which is noticeable, rather than silence, which reads as the whole
       music system being broken. */
    assert(music_synth_song_at(-1) == music_synth_song_at(MUSIC_SYNTH_DEFAULT));
    assert(music_synth_song_at(MUSIC_SYNTH_SONGS)
           == music_synth_song_at(MUSIC_SYNTH_DEFAULT));
    assert(strcmp(music_synth_song_name(-1),
                  music_synth_song_name(MUSIC_SYNTH_DEFAULT)) == 0);
}

static void test_no_song_reads_past_its_own_patterns(void) {
    /* The bound that matters. Each song's order entries index patterns from its
       own cell base, so the highest one it names has to fit inside the cell
       array from that base -- otherwise the last tune in the file walks off the
       end of it and the ones before it play each other's bars. */
    const TrackerCell *base = music_synth_song_at(0)->cells;
    for (int i = 0; i < music_synth_song_count(); i++) {
        const TrackerSong *s = music_synth_song_at(i);
        long offset = s->cells - base;
        int highest = 0;
        for (int k = 0; k < s->order_len; k++)
            if (s->order[k] > highest) highest = s->order[k];
        long last = offset + (long)(highest + 1) * s->rows * s->channels;
        assert(offset >= 0);
        assert(last <= MUSIC_SYNTH_CELLS);
    }
}

static void test_every_cell_is_playable(void) {
    const TrackerCell *base = music_synth_song_at(0)->cells;
    for (int i = 0; i < MUSIC_SYNTH_CELLS; i++) {
        unsigned char note = base[i].note;
        if (note < 2) continue;
        int index = note - 2;
        assert(index / 12 - 2 >= -8 && index / 12 - 2 <= 7);
        assert((base[i].wv >> 4) <= 4);
        assert((base[i].wv & 0x0F) <= 7);
    }
}

static void test_every_cd_track_names_a_real_song(void) {
    for (int t = MUSIC_SYNTH_TRACK_MIN; t <= MUSIC_SYNTH_TRACK_MAX; t++) {
        int s = music_synth_song_for_track(t);
        assert(s >= 0 && s < MUSIC_SYNTH_SONGS);
    }
}

static void test_a_track_off_the_disc_gives_the_default(void) {
    /* music.c hands this whatever the room table named, including 0 for "stop",
       so it has to answer for numbers that are not tracks at all. */
    assert(music_synth_song_for_track(0) == MUSIC_SYNTH_DEFAULT);
    assert(music_synth_song_for_track(MUSIC_SYNTH_TRACK_MIN - 1) == MUSIC_SYNTH_DEFAULT);
    assert(music_synth_song_for_track(MUSIC_SYNTH_TRACK_MAX + 1) == MUSIC_SYNTH_DEFAULT);
    assert(music_synth_song_for_track(-99) == MUSIC_SYNTH_DEFAULT);
}

static void test_a_null_sink_does_not_start_playback(void) {
    /* The song is data and the tracker is told where to send it; handing it
       nowhere must leave it stopped rather than dereferencing the sink on the
       first tick. */
    tracker_start(music_synth_song(), 0);
    assert(!tracker_playing());
}

int main(void) {
    test_the_catalogue_is_not_empty();
    test_every_song_is_shaped();
    test_every_song_has_a_name();
    test_a_bad_index_gives_the_default();
    test_no_song_reads_past_its_own_patterns();
    test_every_cell_is_playable();
    test_every_cd_track_names_a_real_song();
    test_a_track_off_the_disc_gives_the_default();
    test_a_null_sink_does_not_start_playback();
    printf("test_synth_song: all passed\n");
    return 0;
}
