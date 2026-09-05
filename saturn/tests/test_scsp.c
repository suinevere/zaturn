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
static signed char    g_wave[SCSP_WAVE_BYTES];

#define SLOT_WORD(voice, off) g_regs[(SCSP_SLOT_FIRST + (voice)) * 16 + ((off) / 2)]

static void reset(void) {
    for (int i = 0; i < SCSP_REG_WORDS; i++) g_regs[i] = 0;
    for (int i = 0; i < SCSP_WAVE_BYTES; i++) g_wave[i] = 0;
    scsp_bind(g_regs, g_wave, 0x70000UL);
}

static void test_key_on_writes_the_documented_words(void) {
    signed char wave[64];
    for (int i = 0; i < 64; i++) wave[i] = (i < 32) ? 100 : -100;
    reset();
    scsp_upload_wave(1, wave, 64);
    scsp_key_on(0, 0x02BA, 1, 7, 0);

    /* SA = 0x70000 + one waveform's worth, so the high nibble is 7 and the low
       word follows the table size -- derived rather than spelled out, because
       hard-coding it is what broke this test when the tables grew from 64 to
       256 samples. LPCTL = normal loop (bit5), PCM8B (bit4), KYONB (bit11) and
       KYONEX (bit12) are all set by the end of the sequence. */
    assert(SLOT_WORD(0, 0x00) == 0x1837);
    assert(SLOT_WORD(0, 0x02) == (unsigned short)((0x70000UL + SCSP_WAVE_MAX) & 0xFFFF));
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
    scsp_key_on(3, 0x0000, 0, 7, 0);
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
    scsp_key_on(0, 0x0000, 0, 7, 0);
    scsp_key_off(0);
    assert((SLOT_WORD(0, 0x00) & (1u << 11)) == 0);
    assert((SLOT_WORD(0, 0x00) & (1u << 12)) != 0);
}

/* NOT pinned here, and it cannot be: scsp_key_on clears KYONB and pulses
   KYONEX before setting them, because the chip acts on the transition and a
   slot already keyed on is otherwise never re-struck. The stand-in for the
   register window is an array, which holds only the last value written, so the
   intermediate state that carries the whole meaning is invisible to it. It was
   found and verified by recording: a probe striking the percussion voice alone
   measured rms 0.00009 without the key-off and 0.03812 with it, and the drums
   in the shipped tune went from 19 audible onsets in twelve seconds to 68
   against the NES original's 60. Anyone changing the key-on sequence has to
   record it again. */

static void test_the_percussion_waveform_is_addressed_past_the_tonal_ones(void) {
    /* The percussion voice used to be the chip's own noise generator, reached
       by setting SSCTL and pointing at no waveform memory at all. It is a
       waveform now -- a slice of the 2A03's shift register -- because the
       chip's generator has one setting and the SCSP has no filter to darken it
       with. So the slot must come up pointed at that table, sixteen tonal
       tables past the base, with SSCTL back to reading sound RAM. */
    reset();
    scsp_key_on(2, 0x0321, SCSP_NOISE_WAVE, 5, 1);
    unsigned long sa = 0x70000UL + (SCSP_WAVES - 1) * SCSP_WAVE_MAX;
    assert((SLOT_WORD(2, 0x00) & (3u << 7)) == 0);
    assert(SLOT_WORD(2, 0x02) == (unsigned short)(sa & 0xFFFF));
    assert(SLOT_WORD(2, 0x06) == SCSP_NOISE_LEN - 1);
    assert(SLOT_WORD(2, 0x16) == (unsigned short)(5 << 13));
}

static void test_no_two_percussion_hits_start_at_the_same_place(void) {
    /* The chip restarts a slot from SA on every key-on, so without moving the
       start each strike replays the same bytes -- 176 byte-identical hits in the
       shipped loop, heard as one pitched click repeating rather than as noise.
       Measured before this: successive hits correlated 0.797 against the NES
       original's 0.486. A pitched note must NOT move, or every note would start
       partway through its own waveform. */
    reset();
    unsigned short first = 0, seen[4];
    for (int i = 0; i < 4; i++) {
        scsp_key_on(0, 0x0321, SCSP_NOISE_WAVE, 7, 1);
        seen[i] = SLOT_WORD(0, 0x02);
        if (i == 0) first = seen[i];
    }
    for (int i = 1; i < 4; i++)
        for (int j = 0; j < i; j++)
            assert(seen[i] != seen[j]);
    assert(seen[1] - first == SCSP_NOISE_STRIDE);
    /* and the loop end follows the start, or the slot reads past the table */
    assert(SLOT_WORD(0, 0x06) == SCSP_NOISE_LEN - 1 - 3 * SCSP_NOISE_STRIDE);

    reset();
    unsigned short a, b;
    scsp_key_on(1, 0x0321, 0, 7, 0);
    a = SLOT_WORD(1, 0x02);
    scsp_key_on(1, 0x0321, 0, 7, 0);
    b = SLOT_WORD(1, 0x02);
    assert(a == b);
}

static void test_a_hit_never_reads_past_the_table(void) {
    /* The start only moves while a whole hit still fits after it. Off by one
       here and the last slice runs off the end of the waveform area into
       whatever the SGL driver has below it. */
    reset();
    for (int i = 0; i < 64; i++) {
        scsp_key_on(0, 0x0321, SCSP_NOISE_WAVE, 7, 1);
        unsigned long sa = ((unsigned long)(SLOT_WORD(0, 0x00) & 0x0Fu) << 16)
                         | SLOT_WORD(0, 0x02);
        unsigned long start = sa - 0x70000UL - (SCSP_WAVES - 1) * SCSP_WAVE_MAX;
        assert(start + SCSP_NOISE_RUN <= SCSP_NOISE_LEN);
        assert(start + SLOT_WORD(0, 0x06) + 1 == SCSP_NOISE_LEN);
    }
}

static void test_the_percussion_waveform_has_its_own_length(void) {
    /* A fixed stride would put the noise table's last 15/16ths on top of
       nothing and read the tonal tables back at the wrong addresses. */
    signed char probe[8];
    for (int i = 0; i < 8; i++) probe[i] = (signed char)(i + 1);
    reset();
    scsp_upload_wave(SCSP_NOISE_WAVE, probe, 8);
    assert(g_wave[(SCSP_WAVES - 1) * SCSP_WAVE_MAX] == 1);
    assert(g_wave[(SCSP_WAVES - 1) * SCSP_WAVE_MAX + 7] == 8);
    assert(g_wave[(SCSP_WAVES - 2) * SCSP_WAVE_MAX] == 0);
}

static void test_level_change_leaves_the_note_alone(void) {
    signed char wave[64];
    for (int i = 0; i < 64; i++) wave[i] = 0;
    reset();
    scsp_upload_wave(0, wave, 64);
    scsp_key_on(1, 0x0157, 0, 7, 0);
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


static void test_noise_decays_but_a_pitched_note_sustains(void) {
    /* The drum voice is only ever struck -- the pattern data emits no key-off
       for it -- so its envelope has to end the sound by itself. A pitched note
       is the opposite: it must hold until the tracker releases it. D1R (bits
       10-6 of register 0x08) is the decay rate, so it must be zero for a note
       and non-zero for a drum. Getting this backwards latches the noise
       voice on at the first hit and buries the music under a hiss. It is the
       flag that decides, not the waveform: the same table keyed without it
       would never stop. */
    signed char wave[64];
    for (int i = 0; i < 64; i++) wave[i] = 0;
    reset();
    scsp_upload_wave(0, wave, 64);

    scsp_key_on(0, 0x0000, 0, 7, 0);
    assert(((SLOT_WORD(0, 0x08) >> 6) & 0x1F) == 0);

    scsp_key_on(1, 0x0321, SCSP_NOISE_WAVE, 7, 1);
    assert(((SLOT_WORD(1, 0x08) >> 6) & 0x1F) != 0);
    /* and it must decay all the way down, not to a floor it holds at */
    assert(((SLOT_WORD(1, 0x0A) >> 5) & 0x1F) == 0x1F);
}

int main(void) {
    test_key_on_writes_the_documented_words();
    test_key_on_targets_slot_28_upwards();
    test_key_off_clears_kyonb_and_pulses_kyonex();
    test_the_percussion_waveform_is_addressed_past_the_tonal_ones();
    test_the_percussion_waveform_has_its_own_length();
    test_no_two_percussion_hits_start_at_the_same_place();
    test_a_hit_never_reads_past_the_table();
    test_level_change_leaves_the_note_alone();
    test_upload_copies_into_its_own_wave_area();
    test_silence_zeroes_only_owned_slots();
    test_enable_output_raises_master_volume_only();
    test_noise_decays_but_a_pitched_note_sustains();
    printf("test_scsp: all passed\n");
    return 0;
}
