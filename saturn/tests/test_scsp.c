/* The SCSP register layer, checked against the bit positions in
   docs/superpowers/specs/2026-09-04-sh2-synth-music-design.md.

   The layer takes its register file and its waveform memory through
   scsp_bind, so the host substitutes plain arrays for the two hardware
   windows and asserts the exact words the target would have written. The
   split between wave_ram (where bytes are copied) and wave_sa (the address
   programmed into SA) is what makes that possible: on hardware they name the
   same memory, on the host they do not.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_scsp \
         saturn/tests/test_scsp.c saturn/src/sound/scsp.c && /tmp/t_scsp
*/
#include "../src/sound/scsp.h"
#include <stdio.h>
#include <assert.h>

static unsigned short g_regs[SCSP_REG_WORDS];
static signed char    g_wave[SCSP_WAVES * SCSP_WAVE_MAX];

#define SLOT_WORD(voice, off) g_regs[(SCSP_SLOT_FIRST + (voice)) * 16 + ((off) / 2)]

static void reset(void) {
    for (int i = 0; i < SCSP_REG_WORDS; i++) g_regs[i] = 0;
    for (int i = 0; i < SCSP_WAVES * SCSP_WAVE_MAX; i++) g_wave[i] = 0;
    scsp_bind(g_regs, g_wave, 0x70000UL);
}

static void test_key_on_writes_the_documented_words(void) {
    signed char wave[64];
    for (int i = 0; i < 64; i++) wave[i] = (i < 32) ? 100 : -100;
    reset();
    scsp_upload_wave(1, wave, 64);
    scsp_key_on(0, 0x02BA, 1, 7);

    /* SA = 0x70000 + 1 * 64 = 0x70040, so the high nibble is 7 and the low
       word is 0x0040. LPCTL = normal loop (bit5), PCM8B (bit4), KYONB (bit11)
       and KYONEX (bit12) all set by the end of the sequence. */
    assert(SLOT_WORD(0, 0x00) == 0x1837);
    assert(SLOT_WORD(0, 0x02) == 0x0040);
    assert(SLOT_WORD(0, 0x04) == 0x0000);
    assert(SLOT_WORD(0, 0x06) == 63);
    assert(SLOT_WORD(0, 0x10) == 0x02BA);
    assert(SLOT_WORD(0, 0x16) == 0xE000);
}

static void test_key_on_targets_slot_28_upwards(void) {
    signed char wave[64];
    for (int i = 0; i < 64; i++) wave[i] = 0;
    reset();
    scsp_upload_wave(0, wave, 64);
    scsp_key_on(3, 0x0000, 0, 7);
    /* Voice 3 is slot 31; nothing below slot 28 may be touched. */
    assert(SLOT_WORD(3, 0x00) != 0);
    for (int slot = 0; slot < SCSP_SLOT_FIRST; slot++)
        for (int w = 0; w < 16; w++)
            assert(g_regs[slot * 16 + w] == 0);
}

static void test_key_off_clears_kyonb_and_pulses_kyonex(void) {
    signed char wave[64];
    for (int i = 0; i < 64; i++) wave[i] = 0;
    reset();
    scsp_upload_wave(0, wave, 64);
    scsp_key_on(0, 0x0000, 0, 7);
    scsp_key_off(0);
    assert((SLOT_WORD(0, 0x00) & (1u << 11)) == 0);
    assert((SLOT_WORD(0, 0x00) & (1u << 12)) != 0);
}

static void test_noise_voice_sets_ssctl(void) {
    reset();
    scsp_key_on_noise(2, 5);
    assert((SLOT_WORD(2, 0x00) & (3u << 7)) == (1u << 7));
    assert(SLOT_WORD(2, 0x16) == (unsigned short)(5 << 13));
}

static void test_level_change_leaves_the_note_alone(void) {
    signed char wave[64];
    for (int i = 0; i < 64; i++) wave[i] = 0;
    reset();
    scsp_upload_wave(0, wave, 64);
    scsp_key_on(1, 0x0157, 0, 7);
    unsigned short before = SLOT_WORD(1, 0x10);
    scsp_set_voice_level(1, 2);
    assert(SLOT_WORD(1, 0x16) == (unsigned short)(2 << 13));
    assert(SLOT_WORD(1, 0x10) == before);
}

static void test_upload_copies_into_its_own_wave_area(void) {
    signed char wave[64];
    for (int i = 0; i < 64; i++) wave[i] = (signed char)(i - 32);
    reset();
    scsp_upload_wave(2, wave, 64);
    assert(g_wave[2 * SCSP_WAVE_MAX] == -32);
    assert(g_wave[2 * SCSP_WAVE_MAX + 63] == 31);
    assert(g_wave[0] == 0);
}

static void test_silence_zeroes_only_owned_slots(void) {
    signed char wave[64];
    for (int i = 0; i < 64; i++) wave[i] = 0;
    reset();
    for (int i = 0; i < SCSP_REG_WORDS; i++) g_regs[i] = 0xFFFF;
    scsp_silence();
    for (int v = 0; v < SCSP_VOICES; v++)
        for (int w = 0; w < 16; w++)
            assert(g_regs[(SCSP_SLOT_FIRST + v) * 16 + w] == 0);
    assert(g_regs[0] == 0xFFFF);
}


static void test_enable_output_raises_master_volume_only(void) {
    /* The bug this pins: every slot register correct and the machine silent,
       because with no sound driver nothing had ever raised MVOL. The write
       must set the low nibble to full and disturb nothing else in that
       register -- MEM4MB and DAC18B live in the same word. */
    reset();
    g_regs[0x400 / 2] = 0x0300;
    scsp_enable_output();
    assert((g_regs[0x400 / 2] & 0x000F) == 0x000F);
    assert((g_regs[0x400 / 2] & 0xFFF0) == 0x0300);
}

int main(void) {
    test_key_on_writes_the_documented_words();
    test_key_on_targets_slot_28_upwards();
    test_key_off_clears_kyonb_and_pulses_kyonex();
    test_noise_voice_sets_ssctl();
    test_level_change_leaves_the_note_alone();
    test_upload_copies_into_its_own_wave_area();
    test_silence_zeroes_only_owned_slots();
    test_enable_output_raises_master_volume_only();
    printf("test_scsp: all passed\n");
    return 0;
}
