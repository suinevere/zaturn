/* The note table and the generated waveforms.

   The FNS values are Sega's own, from the SCSP User's Manual 4.2.5 table 4.13,
   which gives C4 as OCT 0 / FNS 0 and lists a value per semitone. They are
   pinned here as literals rather than recomputed from the cent formula: the
   point of the test is that the shipped table matches the published one, and a
   test that derived them the same way the code does would agree with a wrong
   table.

   OCT is four bits and wraps for negative octaves -- octave -1 is 0xF, not
   -1 -- which is the arithmetic most likely to be got wrong here.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_note \
         saturn/tests/test_synth_note.c saturn/src/sound/synth.c \
         saturn/src/sound/scsp.c saturn/src/sound/tracker.c \
         saturn/src/sound/music_synth_data.c && /tmp/t_note
*/
#include "../src/sound/synth.h"
#include "../src/sound/scsp.h"
#include <stdio.h>
#include <assert.h>

static void test_published_semitone_table(void) {
    static const unsigned short want[12] = {
        0x000, 0x03D, 0x07D, 0x0C2, 0x10A, 0x157,
        0x1A8, 0x1FE, 0x25A, 0x2BA, 0x321, 0x38D
    };
    for (int s = 0; s < 12; s++) assert(synth_pitch(s, 0) == want[s]);
}

static void test_octave_occupies_bits_14_to_11(void) {
    assert(synth_pitch(0, 1) == 0x0800);
    assert(synth_pitch(0, 2) == 0x1000);
    assert(synth_pitch(0, 7) == 0x3800);
}

static void test_negative_octaves_wrap_into_four_bits(void) {
    /* OCT 0xF divides by two, so one octave down is 0xF, not -1. */
    assert(synth_pitch(0, -1) == 0x7800);
    assert(synth_pitch(0, -2) == 0x7000);
}

static void test_octave_and_semitone_combine(void) {
    assert(synth_pitch(9, 1) == (0x0800 | 0x2BA));
}

static void test_out_of_range_inputs_are_clamped(void) {
    assert(synth_pitch(-1, 0) == synth_pitch(0, 0));
    assert(synth_pitch(12, 0) == synth_pitch(11, 0));
    assert(synth_pitch(0, 99) == synth_pitch(0, 7));
    assert(synth_pitch(0, -99) == synth_pitch(0, -8));
}

static int half_mean(const signed char *w, int from, int to) {
    int sum = 0;
    for (int i = from; i < to; i++) sum += w[i];
    return sum / (to - from);
}

static void test_square_is_positive_then_negative(void) {
    /* Band-limited now, so individual samples ring around the edges and the
       old sample-by-sample sign test no longer describes it. What still holds
       is the shape in the mean. */
    signed char w[SCSP_WAVE_MAX];
    synth_wave_build(SYNTH_WAVE_SQUARE, w, SCSP_WAVE_MAX);
    assert(half_mean(w, 0, SCSP_WAVE_MAX / 2) > 40);
    assert(half_mean(w, SCSP_WAVE_MAX / 2, SCSP_WAVE_MAX) < -40);
}

static void test_pulse_is_high_for_about_a_quarter(void) {
    signed char w[SCSP_WAVE_MAX];
    synth_wave_build(SYNTH_WAVE_PULSE, w, SCSP_WAVE_MAX);
    assert(half_mean(w, 0, SCSP_WAVE_MAX / 4) > 20);
    assert(half_mean(w, SCSP_WAVE_MAX / 2, SCSP_WAVE_MAX) < 0);
}

static void test_triangle_peaks_near_the_middle(void) {
    signed char w[SCSP_WAVE_MAX];
    synth_wave_build(SYNTH_WAVE_TRIANGLE, w, SCSP_WAVE_MAX);
    int peak = 0, at = 0;
    for (int i = 0; i < SCSP_WAVE_MAX; i++)
        if (w[i] > peak) { peak = w[i]; at = i; }
    assert(peak > 60);
    assert(at > SCSP_WAVE_MAX / 8 && at < SCSP_WAVE_MAX / 2);
}

static void test_saw_sweeps_across_its_range(void) {
    signed char w[SCSP_WAVE_MAX];
    int lo = 127, hi = -128;
    synth_wave_build(SYNTH_WAVE_SAW, w, SCSP_WAVE_MAX);
    for (int i = 0; i < SCSP_WAVE_MAX; i++) {
        if (w[i] < lo) lo = w[i];
        if (w[i] > hi) hi = w[i];
    }
    assert(hi > 60 && lo < -60);
}

static void test_every_waveform_is_band_limited(void) {
    /* The point of the additive rebuild. A hard-edged square jumps 200 counts
       between two adjacent samples, and every harmonic above half the playback
       rate folds back as an inharmonic whine -- the buzz. A waveform built from
       a bounded number of harmonics cannot step that far. */
    for (int k = 0; k < 4; k++) {
        signed char w[SCSP_WAVE_MAX];
        int worst = 0;
        synth_wave_build(k, w, SCSP_WAVE_MAX);
        for (int i = 1; i < SCSP_WAVE_MAX; i++) {
            int d = w[i] - w[i - 1];
            if (d < 0) d = -d;
            if (d > worst) worst = d;
        }
        assert(worst < 60);
    }
}

static void test_every_waveform_is_roughly_dc_free(void) {
    for (int k = 0; k < 4; k++) {
        signed char w[SCSP_WAVE_MAX];
        int sum = 0;
        synth_wave_build(k, w, SCSP_WAVE_MAX);
        for (int i = 0; i < SCSP_WAVE_MAX; i++) sum += w[i];
        assert(sum > -400 && sum < 400);
    }
}

int main(void) {
    test_published_semitone_table();
    test_octave_occupies_bits_14_to_11();
    test_negative_octaves_wrap_into_four_bits();
    test_octave_and_semitone_combine();
    test_out_of_range_inputs_are_clamped();
    test_square_is_positive_then_negative();
    test_pulse_is_high_for_about_a_quarter();
    test_triangle_peaks_near_the_middle();
    test_saw_sweeps_across_its_range();
    test_every_waveform_is_band_limited();
    test_every_waveform_is_roughly_dc_free();
    printf("test_synth_note: all passed\n");
    return 0;
}
