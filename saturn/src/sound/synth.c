/*----------------------
 | synth.c
 | Description: Implementation of the voice layer. The FNS table is Sega's,
 |   from the SCSP User's Manual 4.2.5 -- twelve values covering one octave,
 |   with OCT carrying the rest.
 | Author: suinevere
 | Dependencies: synth.h, scsp.h
 ----------------------*/
#include "synth.h"
#include "scsp.h"

/*----------------------
 | SYNTH_FNS
 | Description: Sega's published FNS value per semitone, C through B.
 | Author: suinevere
 ----------------------*/
static const unsigned short SYNTH_FNS[12] = {
    0x000, 0x03D, 0x07D, 0x0C2, 0x10A, 0x157,
    0x1A8, 0x1FE, 0x25A, 0x2BA, 0x321, 0x38D
};

/*----------------------
 | SYNTH_AMP
 | Description: Peak sample value for the generated waveforms. Short of 127 so
 |   four voices summing in the SCSP mixer have somewhere to go.
 | Author: suinevere
 ----------------------*/
#define SYNTH_AMP 100

unsigned short synth_pitch(int semitone, int octave) {
    if (semitone < 0) semitone = 0;
    if (semitone > 11) semitone = 11;
    if (octave < -8) octave = -8;
    if (octave > 7) octave = 7;
    return (unsigned short)(((unsigned)(octave & 0x0F) << 11) | SYNTH_FNS[semitone]);
}

void synth_wave_build(int index, signed char *out, int len) {
    int half = len / 2;
    int quarter = len / 4;

    for (int i = 0; i < len; i++) {
        int v = 0;
        if (index == SYNTH_WAVE_SQUARE) {
            v = (i < half) ? SYNTH_AMP : -SYNTH_AMP;
        } else if (index == SYNTH_WAVE_PULSE) {
            v = (i < quarter) ? (SYNTH_AMP * 3 / 4) : -(SYNTH_AMP / 4);
        } else if (index == SYNTH_WAVE_TRIANGLE) {
            v = (i <= half)
                ? (-SYNTH_AMP + (2 * SYNTH_AMP * i) / half)
                : (SYNTH_AMP - (2 * SYNTH_AMP * (i - half)) / (len - half));
        } else {
            v = -SYNTH_AMP + (2 * SYNTH_AMP * i) / (len - 1);
        }
        out[i] = (signed char) v;
    }
}

#include "tracker.h"
#include "music_synth_data.h"

static int g_level = 7;
static int g_voice_vol[SCSP_VOICES];
static int g_voice_on[SCSP_VOICES];

void synth_note_on(int channel, int semitone, int octave, int wave, int vol) {
    if (channel < 0 || channel >= SCSP_VOICES) return;
    if (semitone < 0) {
        scsp_key_off(channel);
        g_voice_on[channel] = 0;
        return;
    }
    g_voice_vol[channel] = vol;
    g_voice_on[channel] = 1;
    if (wave >= SCSP_WAVES) scsp_key_on_noise(channel, (vol * g_level) / 7);
    else scsp_key_on(channel, synth_pitch(semitone, octave), wave, (vol * g_level) / 7);
}

void synth_bind(volatile unsigned short *regs, volatile signed char *wave_ram, unsigned long wave_sa) {
    scsp_bind(regs, wave_ram, wave_sa);
}

void synth_init(void) {
    signed char wave[SCSP_WAVE_MAX];
    scsp_silence();
    for (int w = 0; w < SCSP_WAVES; w++) {
        synth_wave_build(w, wave, SCSP_WAVE_MAX);
        scsp_upload_wave(w, wave, SCSP_WAVE_MAX);
    }
    for (int v = 0; v < SCSP_VOICES; v++) {
        g_voice_vol[v] = 0;
        g_voice_on[v] = 0;
    }
}

void synth_start(void) {
    tracker_start(music_synth_song(), synth_note_on);
}

void synth_stop(void) {
    tracker_stop();
    for (int v = 0; v < SCSP_VOICES; v++) {
        scsp_key_off(v);
        g_voice_on[v] = 0;
    }
}

void synth_set_level(int level) {
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    g_level = level;
    for (int v = 0; v < SCSP_VOICES; v++)
        if (g_voice_on[v]) scsp_set_voice_level(v, (g_voice_vol[v] * g_level) / 7);
}

void synth_tick(void) {
    tracker_tick();
}

int synth_playing(void) {
    return tracker_playing();
}

int synth_should_play(int has_cd_audio) {
    return has_cd_audio ? 0 : 1;
}
