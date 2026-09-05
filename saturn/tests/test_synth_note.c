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

static void test_square_is_half_high_half_low(void) {
    signed char w[64];
    synth_wave_build(SYNTH_WAVE_SQUARE, w, 64);
    assert(w[0] > 0 && w[31] > 0);
    assert(w[32] < 0 && w[63] < 0);
}

static void test_pulse_is_a_quarter_high(void) {
    signed char w[64];
    synth_wave_build(SYNTH_WAVE_PULSE, w, 64);
    assert(w[0] > 0 && w[15] > 0);
    assert(w[16] < 0 && w[63] < 0);
}

static void test_triangle_peaks_in_the_middle(void) {
    signed char w[64];
    synth_wave_build(SYNTH_WAVE_TRIANGLE, w, 64);
    assert(w[32] > w[0]);
    assert(w[32] > w[63]);
    for (int i = 1; i <= 32; i++) assert(w[i] >= w[i - 1]);
    for (int i = 33; i < 64; i++) assert(w[i] <= w[i - 1]);
}

static void test_saw_rises_monotonically(void) {
    signed char w[64];
    synth_wave_build(SYNTH_WAVE_SAW, w, 64);
    for (int i = 1; i < 64; i++) assert(w[i] >= w[i - 1]);
    assert(w[0] < 0);
    assert(w[63] > 0);
}

static void test_every_waveform_is_roughly_dc_free(void) {
    /* A waveform with a DC offset wastes headroom and thumps on key-on. */
    for (int k = 0; k < 4; k++) {
        signed char w[64];
        int sum = 0;
        synth_wave_build(k, w, 64);
        for (int i = 0; i < 64; i++) sum += w[i];
        assert(sum > -260 && sum < 260);
    }
}

int main(void) {
    test_published_semitone_table();
    test_octave_occupies_bits_14_to_11();
    test_negative_octaves_wrap_into_four_bits();
    test_octave_and_semitone_combine();
    test_out_of_range_inputs_are_clamped();
    test_square_is_half_high_half_low();
    test_pulse_is_a_quarter_high();
    test_triangle_peaks_in_the_middle();
    test_saw_rises_monotonically();
    test_every_waveform_is_roughly_dc_free();
    printf("test_synth_note: all passed\n");
    return 0;
}
