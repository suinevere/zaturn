/* The public API: what the rest of the program sees. Bound to array stand-ins
   for the two hardware windows, so the test can watch the slots the way the
   SCSP would see them.

   The level test is the one worth reading twice. A level change has to reach
   voices that are already sounding without restarting them, because the
   in-game duck happens mid-note; and it must go to the slots' own send level,
   never to the machine's master volume, which CD-DA and the splash jingle also
   use.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_api \
         saturn/tests/test_synth_api.c saturn/src/sound/synth.c \
         saturn/src/sound/scsp.c saturn/src/sound/tracker.c \
         saturn/src/sound/synth_waves.c \
         saturn/src/sound/music_synth_data.c && /tmp/t_api
*/
#include "../src/sound/synth.h"
#include "../src/sound/scsp.h"
#include <stdio.h>
#include <assert.h>

static unsigned short g_regs[SCSP_REG_WORDS];
static signed char    g_wave[SCSP_WAVE_BYTES];

#define SLOT_WORD(voice, off) g_regs[(SCSP_SLOT_FIRST + (voice)) * 16 + ((off) / 2)]

static void bind_fresh(void) {
    for (int i = 0; i < SCSP_REG_WORDS; i++) g_regs[i] = 0;
    for (int i = 0; i < SCSP_WAVE_BYTES; i++) g_wave[i] = 0;
    synth_bind(g_regs, g_wave, 0x70000UL);
    synth_init();
}

static void test_init_uploads_every_waveform_including_the_long_one(void) {
    /* The percussion table is sixteen times the length of a tonal one, so its
       tail is the part a fixed-stride upload would silently drop -- and a
       half-written noise table still sounds like noise for the first
       sixteenth of every hit. */
    bind_fresh();
    int nonzero = 0;
    for (int w = 0; w < SCSP_NOISE_WAVE; w++)
        for (int i = 0; i < SCSP_WAVE_MAX; i++)
            if (g_wave[w * SCSP_WAVE_MAX + i] != 0) { nonzero++; break; }
    assert(nonzero == SCSP_NOISE_WAVE);

    int noise = 0;
    for (int i = 0; i < SCSP_NOISE_LEN; i++)
        if (g_wave[SCSP_NOISE_WAVE * SCSP_WAVE_MAX + i] != 0) noise++;
    assert(noise == SCSP_NOISE_LEN);
}

static void test_init_leaves_the_slots_silent(void) {
    bind_fresh();
    for (int v = 0; v < SCSP_VOICES; v++) assert((SLOT_WORD(v, 0x00) & (1u << 11)) == 0);
    assert(!synth_playing());
}

static void test_start_then_tick_keys_a_voice_on(void) {
    /* Ticked past the lead-in rather than once. Starting a tune now holds the
       sequencer silent for a few frames first, so the voices the outgoing tune
       left have finished releasing before the incoming one keys anything --
       "still hearing notes from the previous track selecting new track, okay
       if there's a silence between the two". A boot start pays the same gap
       and nobody hears a fault in it. What this test is for is unchanged: the
       tracker has to actually reach the chip. */
    bind_fresh();
    synth_start();
    assert(synth_playing());
    for (int i = 0; i < 16; i++) synth_tick();
    assert((SLOT_WORD(0, 0x00) & (1u << 11)) != 0);
}

static void test_tick_while_stopped_writes_nothing(void) {
    bind_fresh();
    synth_tick();
    for (int v = 0; v < SCSP_VOICES; v++) assert(SLOT_WORD(v, 0x00) == 0);
}

static void test_stop_keys_every_voice_off(void) {
    bind_fresh();
    synth_start();
    for (int i = 0; i < 40; i++) synth_tick();
    synth_stop();
    assert(!synth_playing());
    for (int v = 0; v < SCSP_VOICES; v++) assert((SLOT_WORD(v, 0x00) & (1u << 11)) == 0);
}

static void test_level_scales_sounding_voices_without_restarting_them(void) {
    bind_fresh();
    synth_start();
    for (int i = 0; i < 16; i++) synth_tick();   /* past the lead-in silence */
    unsigned short pitch_before = SLOT_WORD(0, 0x10);
    synth_set_level(7);
    unsigned short loud = SLOT_WORD(0, 0x16);
    synth_set_level(2);
    unsigned short quiet = SLOT_WORD(0, 0x16);
    assert(loud > quiet);
    assert(SLOT_WORD(0, 0x10) == pitch_before);
}

static void test_level_zero_is_silence(void) {
    bind_fresh();
    synth_start();
    synth_tick();
    synth_set_level(0);
    assert((SLOT_WORD(0, 0x16) >> 13) == 0);
}

static void test_the_percussion_voice_decays_and_a_note_does_not(void) {
    /* Both are ordinary waveforms now, so nothing about the slot distinguishes
       them except the envelope -- and the pattern data never keys a drum off,
       so a drum that does not decay is a hiss under the rest of the tune for
       the rest of the session. */
    bind_fresh();
    synth_note_on(0, 10, -2, SYNTH_WAVE_NOISE, 7);
    assert(((SLOT_WORD(0, 0x08) >> 6) & 0x1F) != 0);

    synth_note_on(1, 0, 0, SYNTH_WAVE_PULSE50, 7);
    assert(((SLOT_WORD(1, 0x08) >> 6) & 0x1F) == 0);
}

static void test_gating_defers_to_cd_audio(void) {
    assert(synth_should_play(0) != 0);
    assert(synth_should_play(1) == 0);
}

int main(void) {
    test_init_uploads_every_waveform_including_the_long_one();
    test_init_leaves_the_slots_silent();
    test_start_then_tick_keys_a_voice_on();
    test_tick_while_stopped_writes_nothing();
    test_stop_keys_every_voice_off();
    test_level_scales_sounding_voices_without_restarting_them();
    test_level_zero_is_silence();
    test_the_percussion_voice_decays_and_a_note_does_not();
    test_gating_defers_to_cd_audio();
    printf("test_synth_api: all passed\n");
    return 0;
}
