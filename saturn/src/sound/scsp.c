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
 |
 |   The pitched envelope is split across the two registers because only one of
 |   its rates is a dial: 0x08 keeps the instant attack and the zero decay a
 |   held note wants, and 0x0A carries SCSP_EG_SUSTAINED_RR, which is how
 |   quickly the note ends once the tracker lets it go. Both fields were 31 --
 |   the maximum -- until "very sharp release" was reported.
 | Author: suinevere
 ----------------------*/
#define SCSP_EG_SUSTAINED_A     0x001F
#define SCSP_EG_SUSTAINED_DL    ((unsigned short)(SCSP_EG_SUSTAINED_RR & 0x1F))
#define SCSP_EG_PERCUSSIVE      ((unsigned short)(0xF81Fu | ((SCSP_EG_PERC_D1R & 0x1F) << 6)))
#define SCSP_EG_PERCUSSIVE_DL   0x03FF

static volatile unsigned short *g_regs;
static volatile signed char    *g_wave;
static volatile unsigned short *g_wave16;
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
    g_wave16 = (volatile unsigned short *)(void *) wave_ram;
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

/*----------------------
 | scsp_silence_all
 | Description: Clears every one of the chip's thirty-two slots and then pulses
 |   KYONEX so the release is executed, for a program that is taking the SCSP
 |   over from another one rather than starting it.
 |
 |   scsp_silence clears the four slots this synth uses, which is right when the
 |   rest of the chip belongs to the SGL driver -- clearing those would stop the
 |   CD-DA and the sound effects. This is for the netbin, where the previous
 |   owner has gone and nothing else is entitled to a slot.
 |
 |   It was added on the theory that PlanetWeb hands over slots still keyed,
 |   which the probe then disproved: measured over NetLink, the count of keyed
 |   slots on arrival was zero. It stays because a program taking a chip over
 |   should not inherit its state on trust, and because it costs thirty-two
 |   register writes once -- not because it fixed the fault it was written for.
 |
 |   KYONEX is a single execute bit that makes the chip scan every slot's KYONB
 |   at once, so it is pulsed once at the end rather than per slot.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_regs
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void scsp_silence_all(void) {
    for (int v = 0; v < SCSP_SLOTS; v++) {
        volatile unsigned short *s = g_regs + (v * (0x20 / 2));
        for (int i = 0; i < 0x20 / 2; i++) s[i] = 0;
    }
    g_regs[0x00 / 2] = (unsigned short) (1u << 12);
    scsp_settle();
}

/*----------------------
 | scsp_enable_output
 | Description: Sets the chip's master volume, and the bit that tells it how wide
 |   sound RAM is.
 |
 |   MEM4MB (bit 9) is not optional here and its absence is invisible from the
 |   SH-2 side. The Saturn has 512 KB of sound RAM -- 4 Mbit -- and the SH-2
 |   reaches all of it through the SCU whatever this bit says, so a read-back
 |   check of any address passes either way. The chip's own sample fetch is what
 |   honours it: with the bit clear, a slot's start address is taken as 18 bits,
 |   and this synth's waveforms at 0x70000 are fetched from 0x30000 instead --
 |   where nothing has been written. Measured on hardware as slots that key onto
 |   silence and produce a pop rather than a note, while a read-back of the very
 |   region they were meant to be playing came back 256 of 256 correct.
 |
 |   SND_Init sets it, which is why the CD build never had to. The netbin has no
 |   driver, so nothing sets it there but this.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_regs
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void scsp_enable_output(void) {
    volatile unsigned short *ctrl = g_regs + (0x400 / 2);
    ctrl[0] = (unsigned short)((ctrl[0] & 0xFFF0u) | 0x0200u | 0x000Fu);
}

/*----------------------
 | wave_poke
 | Description: Copies `len` sample bytes into sound RAM at byte offset `off`,
 |   writing whole 16-bit words.
 |
 |   Words rather than bytes because sound RAM is behind the SCSP on the Saturn's
 |   sixteen-bit B-bus, and a word is the access that bus is built for. This was
 |   a byte loop through a `signed char *` for as long as the synth existed.
 |
 |   It was changed on the theory that the byte write was what silenced the
 |   netbin on hardware, and that theory was wrong: measured from a netbin, a
 |   region written by bytes and a region written by words both read back 256 of
 |   256 correct. The fault was MEM4MB (see scsp_enable_output). The word write
 |   stays because it is the right access for the bus and costs nothing, not
 |   because it ever fixed anything.
 |
 |   Every caller is word-aligned and even-length (the tables are 256 bytes at
 |   multiples of 256, the noise 4096), so the odd-end paths below are never
 |   taken today. They are here because the alternative is a copy that is correct
 |   only for its current callers and silently wrong for the next one, and the
 |   failure it would produce is this same one again.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_wave16
 | Params: off -- byte offset into sound RAM; data -- samples; len -- how many
 | Returns: N/A
 ----------------------*/
static void wave_poke(unsigned long off, const signed char *data, int len) {
    int i = 0;
    if ((off & 1u) != 0u) {
        unsigned long w = off >> 1;
        g_wave16[w] = (unsigned short)((g_wave16[w] & 0xFF00u)
                                       | (unsigned char) data[0]);
        off++;
        i = 1;
    }
    for (; i + 1 < len; i += 2, off += 2)
        g_wave16[off >> 1] = (unsigned short)
            (((unsigned int)(unsigned char) data[i] << 8)
             | (unsigned char) data[i + 1]);
    if (i < len) {
        unsigned long w = off >> 1;
        g_wave16[w] = (unsigned short)
            (((unsigned int)(unsigned char) data[i] << 8)
             | (g_wave16[w] & 0x00FFu));
    }
}

void scsp_upload_wave(int index, const signed char *data, int len) {
    if (index < 0 || index >= SCSP_WAVES) return;
    int room = (index == SCSP_NOISE_WAVE) ? SCSP_NOISE_LEN : SCSP_WAVE_MAX;
    if (len > room) len = room;
    wave_poke(g_wave_off[index], data, len);
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
    s[0x08 / 2] = percussive ? SCSP_EG_PERCUSSIVE : SCSP_EG_SUSTAINED_A;
    s[0x0A / 2] = percussive ? SCSP_EG_PERCUSSIVE_DL : SCSP_EG_SUSTAINED_DL;
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
