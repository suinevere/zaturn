/*----------------------
 | scsp.c
 | Description: Implementation of the SCSP register layer. Bit positions are
 |   the SCSP User's Manual's, tabulated in the design spec; the only ones with
 |   a trap are KYONEX, which is write-only and reads back 0, and LSA/LEA,
 |   which count samples from SA rather than bytes.
 | Author: suinevere
 | Dependencies: scsp.h
 ----------------------*/
#include "scsp.h"

static volatile unsigned short *g_regs;
static volatile signed char    *g_wave;
static unsigned long            g_wave_sa;
static int                      g_wave_len[SCSP_WAVES];

#define SLOT(v) (g_regs + ((SCSP_SLOT_FIRST + (v)) * (0x20 / 2)))

void scsp_bind(volatile unsigned short *regs, volatile signed char *wave_ram, unsigned long wave_sa) {
    g_regs = regs;
    g_wave = wave_ram;
    g_wave_sa = wave_sa;
    for (int i = 0; i < SCSP_WAVES; i++) g_wave_len[i] = SCSP_WAVE_MAX;
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
    if (len > SCSP_WAVE_MAX) len = SCSP_WAVE_MAX;
    for (int i = 0; i < len; i++) g_wave[index * SCSP_WAVE_MAX + i] = data[i];
    g_wave_len[index] = len;
}

void scsp_key_on(int voice, unsigned short pitch, int wave, int level) {
    if (voice < 0 || voice >= SCSP_VOICES) return;
    if (wave < 0 || wave >= SCSP_WAVES) return;

    volatile unsigned short *s = SLOT(voice);
    unsigned long sa = g_wave_sa + (unsigned long) wave * SCSP_WAVE_MAX;

    s[0x00 / 2] = (unsigned short)((1u << 5) | (1u << 4) | ((sa >> 16) & 0x0Fu));
    s[0x02 / 2] = (unsigned short)(sa & 0xFFFFu);
    s[0x04 / 2] = 0;
    s[0x06 / 2] = (unsigned short)(g_wave_len[wave] - 1);
    s[0x08 / 2] = 0x001F;
    s[0x0A / 2] = 0x001F;
    s[0x0C / 2] = 0x0000;
    s[0x0E / 2] = 0x0000;
    s[0x10 / 2] = pitch;
    s[0x12 / 2] = 0x0000;
    s[0x14 / 2] = 0x0000;
    s[0x16 / 2] = (unsigned short)((level & 7) << 13);

    s[0x00 / 2] = (unsigned short)(s[0x00 / 2] | (1u << 11));
    s[0x00 / 2] = (unsigned short)(s[0x00 / 2] | (1u << 12));
}

void scsp_key_on_noise(int voice, int level) {
    if (voice < 0 || voice >= SCSP_VOICES) return;

    volatile unsigned short *s = SLOT(voice);

    s[0x00 / 2] = (unsigned short)(1u << 7);
    s[0x02 / 2] = 0x0000;
    s[0x04 / 2] = 0x0000;
    s[0x06 / 2] = 0x0000;
    s[0x08 / 2] = 0x001F;
    s[0x0A / 2] = 0x001F;
    s[0x0C / 2] = 0x0000;
    s[0x0E / 2] = 0x0000;
    s[0x10 / 2] = 0x0000;
    s[0x12 / 2] = 0x0000;
    s[0x14 / 2] = 0x0000;
    s[0x16 / 2] = (unsigned short)((level & 7) << 13);

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
