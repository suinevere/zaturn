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
         saturn/src/sound/synth_waves.c \
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

static int mean_of(const signed char *w, int from, int to) {
    int sum = 0;
    for (int i = from; i < to; i++) sum += w[i];
    return sum / (to - from);
}

static int distinct_levels(const signed char *w, int len) {
    int seen[256];
    int n = 0;
    for (int i = 0; i < 256; i++) seen[i] = 0;
    for (int i = 0; i < len; i++) {
        int k = w[i] + 128;
        if (!seen[k]) { seen[k] = 1; n++; }
    }
    return n;
}

static void test_pulse_duties_are_an_eighth_a_quarter_and_a_half(void) {
    /* The three duty cycles are most of what makes a pulse channel sound like
       an NES: 12.5% is the thin nasal lead, 50% the fat one. Each is checked by
       where its high portion ends. */
    signed char w[SCSP_WAVE_MAX];
    const int L = SCSP_WAVE_MAX;

    synth_wave_build(SYNTH_WAVE_PULSE12, w, L);
    assert(mean_of(w, 0, L / 8) > 90);
    assert(mean_of(w, L / 8, L) < -80);

    synth_wave_build(SYNTH_WAVE_PULSE25, w, L);
    assert(mean_of(w, 0, L / 4) > 90);
    assert(mean_of(w, L / 4, L) < -80);

    synth_wave_build(SYNTH_WAVE_PULSE50, w, L);
    assert(mean_of(w, 0, L / 2) > 90);
    assert(mean_of(w, L / 2, L) < -90);
}

static void test_triangle_is_a_sixteen_level_staircase(void) {
    /* The 2A03's triangle steps through 16 levels, and that coarse staircase is
       exactly why NES bass sounds hollow and gritty rather than smooth. A
       mathematically clean triangle would use every level the byte allows and
       would not sound like an NES at all, so the level count is the assertion
       that matters here. */
    signed char w[SCSP_WAVE_MAX];
    synth_wave_build(SYNTH_WAVE_TRIANGLE, w, SCSP_WAVE_MAX);
    assert(distinct_levels(w, SCSP_WAVE_MAX) <= 16);
    assert(w[0] > 90);
    assert(w[SCSP_WAVE_MAX / 2] < -90);
}

static void test_pulses_are_two_level(void) {
    signed char w[SCSP_WAVE_MAX];
    for (int k = 0; k < 4; k++) {
        if (k == SYNTH_WAVE_TRIANGLE) continue;
        synth_wave_build(k, w, SCSP_WAVE_MAX);
        assert(distinct_levels(w, SCSP_WAVE_MAX) == 2);
    }
}

int main(void) {
    /* The waveform tables are .bss built at boot, not .rodata linked in --
       that trade took 5,120 bytes off the netbin image. Nothing reads them
       before synth_waves_build() runs, so a test that reads them without
       calling it measures a page of zeros, which is what this one did from the
       day of that change until someone built it again. Declared here the way
       synth.c declares it: synth_waves.c is deliberately free of includes so a
       host compiler can build it and diff its output against genwaves.py. */
    extern void synth_waves_build(void);
    synth_waves_build();
    test_published_semitone_table();
    test_octave_occupies_bits_14_to_11();
    test_negative_octaves_wrap_into_four_bits();
    test_octave_and_semitone_combine();
    test_out_of_range_inputs_are_clamped();
    test_pulse_duties_are_an_eighth_a_quarter_and_a_half();
    test_triangle_is_a_sixteen_level_staircase();
    test_pulses_are_two_level();
    printf("test_synth_note: all passed\n");
    return 0;
}
