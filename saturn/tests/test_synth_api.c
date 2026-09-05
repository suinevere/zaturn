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
         saturn/src/sound/music_synth_data.c && /tmp/t_api
*/
#include "../src/sound/synth.h"
#include "../src/sound/scsp.h"
#include <stdio.h>
#include <assert.h>

static unsigned short g_regs[SCSP_REG_WORDS];
static signed char    g_wave[SCSP_WAVES * SCSP_WAVE_MAX];

#define SLOT_WORD(voice, off) g_regs[(SCSP_SLOT_FIRST + (voice)) * 16 + ((off) / 2)]

static void bind_fresh(void) {
    for (int i = 0; i < SCSP_REG_WORDS; i++) g_regs[i] = 0;
    for (int i = 0; i < SCSP_WAVES * SCSP_WAVE_MAX; i++) g_wave[i] = 0;
    synth_bind(g_regs, g_wave, 0x70000UL);
    synth_init();
}

static void test_init_uploads_four_waveforms(void) {
    bind_fresh();
    int nonzero = 0;
    for (int w = 0; w < SCSP_WAVES; w++)
        for (int i = 0; i < SCSP_WAVE_MAX; i++)
            if (g_wave[w * SCSP_WAVE_MAX + i] != 0) { nonzero++; break; }
    assert(nonzero == SCSP_WAVES);
}

static void test_init_leaves_the_slots_silent(void) {
    bind_fresh();
    for (int v = 0; v < SCSP_VOICES; v++) assert((SLOT_WORD(v, 0x00) & (1u << 11)) == 0);
    assert(!synth_playing());
}

static void test_start_then_tick_keys_a_voice_on(void) {
    bind_fresh();
    synth_start();
    assert(synth_playing());
    synth_tick();
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
    synth_tick();
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

static void test_noise_wave_reaches_the_noise_generator(void) {
    /* Waveform index 4 is one past the generated set, and the SCSP makes that
       one itself -- so the voice must come up with SSCTL set to noise rather
       than pointed at waveform memory it does not have. */
    bind_fresh();
    synth_note_on(0, 0, 0, SYNTH_WAVE_NOISE, 7);
    assert((SLOT_WORD(0, 0x00) & (3u << 7)) == (1u << 7));
}

static void test_gating_defers_to_cd_audio(void) {
    assert(synth_should_play(0) != 0);
    assert(synth_should_play(1) == 0);
}

int main(void) {
    test_init_uploads_four_waveforms();
    test_init_leaves_the_slots_silent();
    test_start_then_tick_keys_a_voice_on();
    test_tick_while_stopped_writes_nothing();
    test_stop_keys_every_voice_off();
    test_level_scales_sounding_voices_without_restarting_them();
    test_level_zero_is_silence();
    test_noise_wave_reaches_the_noise_generator();
    test_gating_defers_to_cd_audio();
    printf("test_synth_api: all passed\n");
    return 0;
}
