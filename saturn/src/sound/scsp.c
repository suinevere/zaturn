/*----------------------
 | scsp.c
 | Description: Implementation of the SCSP register layer. Bit positions are
 |   the SCSP User's Manual's, tabulated in the design spec; the only ones with
 |   a trap are KYONEX, which is write-only and reads back 0, and LSA/LEA,
 |   which count samples from SA rather than bytes. The waveform area is laid
 |   out by cumulative offset rather than a fixed stride, because the percussion
 |   waveform is sixteen times the length of a tonal one.
 | Author: suinevere
 | Dependencies: scsp.h
 ----------------------*/
#include "scsp.h"

/*----------------------
 | SCSP_EG_SUSTAINED / SCSP_EG_PERCUSSIVE / SCSP_EG_PERCUSSIVE_DL
 | Description: Two envelopes. A pitched note sustains until the tracker keys it
 |   off, so its decay rate is zero. A drum has no key-off -- the pattern data
 |   only ever strikes it -- so it must decay to silence by itself, or the first
 |   hit latches its voice on and every note afterwards plays under a continuous
 |   hiss. Register 0x08 is D2R in bits 15-11, D1R in 10-6, EGHOLD in 5 and AR
 |   in 4-0; register 0x0A carries DL in bits 9-5 and RR in 4-0. DL is full
 |   attenuation, so D1R alone runs the strike to silence and D2R is never
 |   reached -- SCSP_EG_PERC_D1R is therefore the length of a drum hit.
 | Author: suinevere
 ----------------------*/
#define SCSP_EG_SUSTAINED       0x001F
#define SCSP_EG_PERCUSSIVE      ((unsigned short)(0xF81Fu | ((SCSP_EG_PERC_D1R & 0x1F) << 6)))
#define SCSP_EG_PERCUSSIVE_DL   0x03FF

static volatile unsigned short *g_regs;
static volatile signed char    *g_wave;
static unsigned long            g_wave_sa;
static int                      g_wave_len[SCSP_WAVES];
static unsigned long            g_wave_off[SCSP_WAVES];
static unsigned long            g_noise_start;

#define SLOT(v) (g_regs + ((SCSP_SLOT_FIRST + (v)) * (0x20 / 2)))

/*----------------------
 | scsp_settle
 | Description: Waits long enough for the chip to have walked its slots once, so
 |   a key-off written just before a key-on is actually seen. One output sample
 |   at 44.1 kHz is 22.7 us; at the SH-2's 28.6 MHz this loop is about twice
 |   that, which is the margin. Measured rather than derived: 200 iterations
 |   already gave every strike, 0 gave one strike in four seconds.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void scsp_settle(void) {
    volatile int spin = SCSP_KEY_SETTLE;
    while (spin-- > 0) { }
}

void scsp_bind(volatile unsigned short *regs, volatile signed char *wave_ram, unsigned long wave_sa) {
    g_regs = regs;
    g_wave = wave_ram;
    g_wave_sa = wave_sa;
    g_noise_start = 0;
    unsigned long off = 0;
    for (int i = 0; i < SCSP_WAVES; i++) {
        g_wave_len[i] = (i == SCSP_NOISE_WAVE) ? SCSP_NOISE_LEN : SCSP_WAVE_MAX;
        g_wave_off[i] = off;
        off += (unsigned long) g_wave_len[i];
    }
}

void scsp_silence(void) {
    for (int v = 0; v < SCSP_VOICES; v++) {
        volatile unsigned short *s = SLOT(v);
        for (int i = 0; i < 0x20 / 2; i++) s[i] = 0;
    }
}

void scsp_enable_output(void) {
    volatile unsigned short *ctrl = g_regs + (0x400 / 2);
    ctrl[0] = (unsigned short)((ctrl[0] & 0xFFF0u) | 0x000Fu);
}

void scsp_upload_wave(int index, const signed char *data, int len) {
    if (index < 0 || index >= SCSP_WAVES) return;
    int room = (index == SCSP_NOISE_WAVE) ? SCSP_NOISE_LEN : SCSP_WAVE_MAX;
    if (len > room) len = room;
    for (int i = 0; i < len; i++) g_wave[g_wave_off[index] + (unsigned long) i] = data[i];
    g_wave_len[index] = len;
}

void scsp_key_on(int voice, unsigned short pitch, int wave, int level, int percussive) {
    if (voice < 0 || voice >= SCSP_VOICES) return;
    if (wave < 0 || wave >= SCSP_WAVES) return;

    volatile unsigned short *s = SLOT(voice);
    unsigned long sa = g_wave_sa + g_wave_off[wave];
    int len = g_wave_len[wave];

    /* Start the percussion somewhere else each time. A pitched voice wants the
       waveform from its beginning; the percussion is a stretch of shift-register
       output, and replaying the same stretch on every strike is heard as one
       click repeating rather than as noise. Advanced after use, so the first
       strike after a bind is still the start of the table. */
    if (wave == SCSP_NOISE_WAVE) {
        sa += g_noise_start;
        len -= (int) g_noise_start;
        g_noise_start += SCSP_NOISE_STRIDE;
        if (g_noise_start + SCSP_NOISE_RUN > SCSP_NOISE_LEN) g_noise_start = 0;
    }

    s[0x00 / 2] = (unsigned short)((1u << 5) | (1u << 4) | ((sa >> 16) & 0x0Fu));
    s[0x02 / 2] = (unsigned short)(sa & 0xFFFFu);
    s[0x04 / 2] = 0;
    s[0x06 / 2] = (unsigned short)(len - 1);
    s[0x08 / 2] = percussive ? SCSP_EG_PERCUSSIVE : SCSP_EG_SUSTAINED;
    s[0x0A / 2] = percussive ? SCSP_EG_PERCUSSIVE_DL : SCSP_EG_SUSTAINED;
    s[0x0C / 2] = 0x0000;
    s[0x0E / 2] = 0x0000;
    s[0x10 / 2] = pitch;
    s[0x12 / 2] = 0x0000;
    s[0x14 / 2] = 0x0000;
    s[0x16 / 2] = (unsigned short)((level & 7) << 13);

    /* Key off before keying on, and give the chip time to notice. KYONEX applies
       each slot's KYONB, and the chip acts on the transition -- so a slot whose
       KYONB is already 1 is not re-struck, it just carries on. A pitched note
       does not care: it is sustaining, and the new pitch takes effect where it
       stands. A drum does: its envelope has decayed to silence by the time the
       next hit arrives, and with no transition to restart it the hit is silent.

       The wait is the whole thing. The two KYONEX pulses are a handful of
       instructions apart, and the chip walks all thirty-two slots once per
       output sample -- so back to back they land inside one pass, the key-off is
       never observed, and the retrigger is lost. Measured: striking one voice
       every eight frames and counting what sounds, thirty strikes per four
       seconds became **one** with no wait and thirty with this one. */
    if (percussive) {
        s[0x00 / 2] = (unsigned short)(s[0x00 / 2] & ~(1u << 11));
        s[0x00 / 2] = (unsigned short)(s[0x00 / 2] | (1u << 12));
        scsp_settle();
    }

    s[0x00 / 2] = (unsigned short)(s[0x00 / 2] | (1u << 11));
    s[0x00 / 2] = (unsigned short)(s[0x00 / 2] | (1u << 12));
}

void scsp_key_off(int voice) {
    if (voice < 0 || voice >= SCSP_VOICES) return;
    volatile unsigned short *s = SLOT(voice);
    s[0x00 / 2] = (unsigned short)(s[0x00 / 2] & ~(1u << 11));
    s[0x00 / 2] = (unsigned short)(s[0x00 / 2] | (1u << 12));
}

void scsp_set_voice_level(int voice, int level) {
    if (voice < 0 || voice >= SCSP_VOICES) return;
    SLOT(voice)[0x16 / 2] = (unsigned short)((level & 7) << 13);
}
