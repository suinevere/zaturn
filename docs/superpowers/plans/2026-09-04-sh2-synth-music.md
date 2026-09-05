# SH-2 Generated Music Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the netbin (and any CD build on a disc without CD-DA) looping music generated on the SH-2, costing about 6 KB instead of the 52 KB + 21.5 KB/second a recorded sample would cost.

**Architecture:** Four SCSP slots each loop a 64-sample waveform in hardware, so a held note costs no CPU. A V-blank tracker walks pattern data and writes pitch/level/key registers only when a note changes. Three plain-C modules — register layer, voice layer, sequencer — each host-testable because the SCSP is reached through an injected base pointer rather than a hardcoded address.

**Tech Stack:** C (plain C, not C++, so the modules compile on the host), SaturnRingLib/SGL for `SRL::Core::OnVblank` only, gcc for host tests, `sh2eb-elf-g++` for the target.

**Spec:** `docs/superpowers/specs/2026-09-04-sh2-synth-music-design.md`

## Global Constraints

- Every method, constant and file gets a header comment block in the project's form (`| name | Description: | Author: suinevere | Dependencies: | Globals: | Params: | Returns:`). Tests and generated files get a file header only. No comments inside functions.
- New code goes in an existing `src/` subfolder if one fits. All of this belongs in `saturn/src/sound/` except the two menu units, which go in `saturn/src/menu/`.
- The three engine modules are **plain C** (`.c`), not `.cxx` — host tests compile them with gcc, which cannot see `srl.hpp`.
- The synth owns **SCSP slots 28-31 only** and writes no other slot.
- Level is applied through per-slot `DISDL`, never through `MVOL`.
- Commit after every task. One sentence, no body, no trailers, no mention of tooling.
- `saturn/tests/*.c` are compiled ad hoc; each carries its own gcc line in its file header, which is the repo's convention (see `saturn/tests/test_music_pause.c`).

## SCSP register reference (used by Tasks 1-2)

Slot registers live at `0x25B00000 + slot * 0x20`, 16-bit words:

| Offset | Fields |
| --- | --- |
| `0x00` | bit12 KYONEX (write-only), bit11 KYONB, bits10-9 SBCTL, bits8-7 SSCTL, bits6-5 LPCTL, bit4 PCM8B, bits3-0 SA[19:16] |
| `0x02` | SA[15:0] |
| `0x04` | LSA (samples from SA) |
| `0x06` | LEA (samples from SA) |
| `0x08` | bits15-11 D2R, bits10-6 D1R, bit5 EGHOLD, bits4-0 AR |
| `0x0A` | bit14 LPSLNK, bits13-10 KRS, bits9-5 DL, bits4-0 RR |
| `0x0C` | bit9 STWINH, bit8 SDIR, bits7-0 TL |
| `0x10` | bits14-11 OCT, bits9-0 FNS |
| `0x16` | bits15-13 DISDL, bits12-8 DIPAN, bits7-5 EFSDL, bits4-0 EFPAN |

---

### Task 1: SCSP register layer

**Files:**
- Create: `saturn/src/sound/scsp.h`
- Create: `saturn/src/sound/scsp.c`
- Test: `saturn/tests/test_scsp.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `void scsp_bind(volatile unsigned short *regs, volatile signed char *wave_ram, unsigned long wave_sa)`, `void scsp_silence(void)`, `void scsp_upload_wave(int index, const signed char *data, int len)`, `void scsp_key_on(int voice, unsigned short pitch, int wave, int level)`, `void scsp_key_on_noise(int voice, int level)`, `void scsp_key_off(int voice)`, `void scsp_set_voice_level(int voice, int level)`; constants `SCSP_VOICES` (4), `SCSP_SLOT_FIRST` (28), `SCSP_WAVES` (4), `SCSP_WAVE_MAX` (64).

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_scsp.c`:

```c
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

static unsigned short g_regs[32 * 16];
static signed char    g_wave[SCSP_WAVES * SCSP_WAVE_MAX];

#define SLOT_WORD(voice, off) g_regs[(SCSP_SLOT_FIRST + (voice)) * 16 + ((off) / 2)]

static void reset(void) {
    for (int i = 0; i < 32 * 16; i++) g_regs[i] = 0;
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
    for (int i = 0; i < 32 * 16; i++) g_regs[i] = 0xFFFF;
    scsp_silence();
    for (int v = 0; v < SCSP_VOICES; v++)
        for (int w = 0; w < 16; w++)
            assert(g_regs[(SCSP_SLOT_FIRST + v) * 16 + w] == 0);
    assert(g_regs[0] == 0xFFFF);
}

int main(void) {
    test_key_on_writes_the_documented_words();
    test_key_on_targets_slot_28_upwards();
    test_key_off_clears_kyonb_and_pulses_kyonex();
    test_noise_voice_sets_ssctl();
    test_level_change_leaves_the_note_alone();
    test_upload_copies_into_its_own_wave_area();
    test_silence_zeroes_only_owned_slots();
    printf("test_scsp: all passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run from the repo root:

```bash
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_scsp \
    saturn/tests/test_scsp.c saturn/src/sound/scsp.c && /tmp/t_scsp
```

Expected: FAIL — `scsp.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/sound/scsp.h`:

```c
/*----------------------
 | scsp.h
 | Description: The SCSP register layer: the four slots the synth owns, keyed
 |   on and off directly rather than through the SGL sound driver, which this
 |   build does not load. Knows nothing about music -- pitch arrives as a
 |   ready-made register word. The register file and the waveform memory are
 |   injected by scsp_bind so the host tests can substitute arrays for the two
 |   hardware windows.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef SCSP_H
#define SCSP_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SCSP_VOICES / SCSP_SLOT_FIRST
 | Description: The synth owns four SCSP slots starting at 28. The top of the
 |   file is deliberate: the SGL sound driver allocates from the bottom, and a
 |   probe confirmed it leaves slot 31 untouched across three seconds of
 |   playback, which is what lets this coexist with the CD build's driver.
 | Author: suinevere
 ----------------------*/
#define SCSP_VOICES     4
#define SCSP_SLOT_FIRST 28

/*----------------------
 | SCSP_WAVES / SCSP_WAVE_MAX
 | Description: Four waveforms, 64 samples each, laid end to end in the
 |   waveform area. 64 samples at OCT 0 sounds 44100/64 = 689 Hz, so the pitch
 |   register does the rest.
 | Author: suinevere
 ----------------------*/
#define SCSP_WAVES    4
#define SCSP_WAVE_MAX 64

/*----------------------
 | scsp_bind
 | Description: Points the layer at its register file and waveform memory.
 |   wave_ram is where waveform bytes are copied; wave_sa is the SCSP byte
 |   address of that same memory, which is what goes in the slot's SA field.
 |   On hardware they name one place, on the host they do not, which is the
 |   whole reason they are separate parameters.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: regs -- slot register file base; wave_ram -- waveform bytes;
 |   wave_sa -- SCSP address of wave_ram
 | Returns: N/A
 ----------------------*/
void scsp_bind(volatile unsigned short *regs, volatile signed char *wave_ram, unsigned long wave_sa);

/*----------------------
 | scsp_silence
 | Description: Zeroes the four owned slots and nothing else.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void scsp_silence(void);

/*----------------------
 | scsp_upload_wave
 | Description: Copies one waveform into its slice of the waveform area and
 |   remembers its length, which becomes the slot's loop end.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: index -- 0..SCSP_WAVES-1; data -- signed 8-bit samples; len -- up to
 |   SCSP_WAVE_MAX
 | Returns: N/A
 ----------------------*/
void scsp_upload_wave(int index, const signed char *data, int len);

/*----------------------
 | scsp_key_on
 | Description: Starts a voice on a waveform at a pitch and level, looping in
 |   hardware so the note holds with no further writes.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: voice -- 0..SCSP_VOICES-1; pitch -- packed OCT/FNS word; wave --
 |   waveform index; level -- 0..7
 | Returns: N/A
 ----------------------*/
void scsp_key_on(int voice, unsigned short pitch, int wave, int level);

/*----------------------
 | scsp_key_on_noise
 | Description: Starts a voice on the SCSP's internal noise generator, which
 |   needs no waveform data at all -- the percussion voice.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: voice -- 0..SCSP_VOICES-1; level -- 0..7
 | Returns: N/A
 ----------------------*/
void scsp_key_on_noise(int voice, int level);

/*----------------------
 | scsp_key_off
 | Description: Releases a voice.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: voice -- 0..SCSP_VOICES-1
 | Returns: N/A
 ----------------------*/
void scsp_key_off(int voice);

/*----------------------
 | scsp_set_voice_level
 | Description: Changes a sounding voice's level without disturbing its pitch
 |   or restarting it. This is DISDL, the slot's own send level -- never MVOL,
 |   which is the whole machine's and is shared with CD-DA.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: voice -- 0..SCSP_VOICES-1; level -- 0..7
 | Returns: N/A
 ----------------------*/
void scsp_set_voice_level(int voice, int level);

#ifdef __cplusplus
}
#endif
#endif /* SCSP_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/sound/scsp.c`:

```c
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
```

- [ ] **Step 5: Run test to verify it passes**

```bash
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_scsp \
    saturn/tests/test_scsp.c saturn/src/sound/scsp.c && /tmp/t_scsp
```

Expected: `test_scsp: all passed`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/sound/scsp.h saturn/src/sound/scsp.c saturn/tests/test_scsp.c
git commit -m "Add the SCSP register layer the synth drives its four slots through, taking its register file and waveform memory as parameters so the host tests can assert the exact words the hardware would receive."
```

---

### Task 2: Note table and waveform generation

**Files:**
- Create: `saturn/src/sound/synth.h`
- Create: `saturn/src/sound/synth.c`
- Test: `saturn/tests/test_synth_note.c`

**Interfaces:**
- Consumes: `scsp.h` from Task 1 (`SCSP_WAVE_MAX`).
- Produces: `unsigned short synth_pitch(int semitone, int octave)`, `void synth_wave_build(int index, signed char *out, int len)`, and the waveform index constants `SYNTH_WAVE_SQUARE` (0), `SYNTH_WAVE_PULSE` (1), `SYNTH_WAVE_TRIANGLE` (2), `SYNTH_WAVE_SAW` (3). The rest of `synth.h` (the five-function public API) arrives in Task 5; declare only these two functions and the four constants now.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_synth_note.c`:

```c
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
         saturn/src/sound/scsp.c && /tmp/t_note
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
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_note \
    saturn/tests/test_synth_note.c saturn/src/sound/synth.c \
    saturn/src/sound/scsp.c && /tmp/t_note
```

Expected: FAIL — `synth.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/sound/synth.h`:

```c
/*----------------------
 | synth.h
 | Description: The voice layer: what a note is, and what the four instruments
 |   sound like. Converts a semitone and octave into the SCSP's packed
 |   OCT/FNS word and builds the waveforms the slots loop. The five-function
 |   playback API that sits on top of this arrives with the tracker.
 | Author: suinevere
 | Dependencies: scsp.h
 ----------------------*/
#ifndef SYNTH_H
#define SYNTH_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SYNTH_WAVE_*
 | Description: The four generated waveforms, in the order scsp_upload_wave
 |   receives them, so the index is both the instrument and its place in the
 |   waveform area.
 | Author: suinevere
 ----------------------*/
#define SYNTH_WAVE_SQUARE   0
#define SYNTH_WAVE_PULSE    1
#define SYNTH_WAVE_TRIANGLE 2
#define SYNTH_WAVE_SAW      3

/*----------------------
 | SYNTH_WAVE_NOISE
 | Description: The percussion voice. One past the generated waveforms because
 |   it is not one of them: the SCSP makes noise internally, so this index
 |   names no waveform data and costs no sound RAM.
 | Author: suinevere
 ----------------------*/
#define SYNTH_WAVE_NOISE    4

/*----------------------
 | synth_pitch
 | Description: Packs a semitone and octave into the SCSP pitch register word
 |   (OCT bits 14-11, FNS bits 9-0). Semitone 0 at octave 0 is the waveform's
 |   own rate, which for a 64-sample wave is 689 Hz. OCT is four bits and
 |   negative octaves wrap into it, so an octave down is 0xF rather than -1.
 |   Inputs outside range are clamped rather than rejected, because a bad note
 |   byte in pattern data should sound wrong, not silence the channel.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: semitone -- 0 (C) to 11 (B); octave -- -8 to 7
 | Returns: the register word for slot offset 0x10
 ----------------------*/
unsigned short synth_pitch(int semitone, int octave);

/*----------------------
 | synth_wave_build
 | Description: Generates one waveform into a caller-supplied buffer. Costs no
 |   image bytes, which is the point -- four instruments for the price of the
 |   code that draws them.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: index -- a SYNTH_WAVE_* value; out -- buffer of at least len bytes;
 |   len -- samples to generate
 | Returns: N/A
 ----------------------*/
void synth_wave_build(int index, signed char *out, int len);

#ifdef __cplusplus
}
#endif
#endif /* SYNTH_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/sound/synth.c`:

```c
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
            v = (i < quarter) ? SYNTH_AMP : -SYNTH_AMP;
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
```

- [ ] **Step 5: Run test to verify it passes**

```bash
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_note \
    saturn/tests/test_synth_note.c saturn/src/sound/synth.c \
    saturn/src/sound/scsp.c && /tmp/t_note
```

Expected: `test_synth_note: all passed`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/sound/synth.h saturn/src/sound/synth.c saturn/tests/test_synth_note.c
git commit -m "Add the voice layer: Sega's published semitone table packed into the SCSP pitch word, and four waveforms drawn by code so the instrument set costs no image bytes."
```

---

### Task 3: Tracker

**Files:**
- Create: `saturn/src/sound/tracker.h`
- Create: `saturn/src/sound/tracker.c`
- Test: `saturn/tests/test_tracker.c`

**Interfaces:**
- Consumes: nothing (the tracker emits events through a sink and never touches the SCSP).
- Produces: `TrackerCell` (`unsigned char note`, `unsigned char wv`), `TrackerSong` (`const TrackerCell *cells; unsigned char rows; unsigned char channels; const unsigned char *order; unsigned char order_len; unsigned char loop_to; unsigned char speed;`), `typedef void (*TrackerSink)(int channel, int semitone, int octave, int wave, int vol)`, `void tracker_start(const TrackerSong *song, TrackerSink sink)`, `void tracker_stop(void)`, `void tracker_tick(void)`, `int tracker_playing(void)`. Note byte convention: 0 = hold, 1 = key off, 2+ = `(note - 2)` as an absolute semitone index where semitone = index % 12 and octave = index / 12 - 2. Key-off is signalled to the sink as `semitone == -1`.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_tracker.c`:

```c
/* The sequencer: rows, speed, pattern order and the loop point.

   The tracker never touches the SCSP -- it emits note events to a sink, which
   is what makes it testable at all. The tests record every event and assert on
   the sequence, so a wrong loop point or an off-by-one in the speed divisor
   shows up as a wrong event list rather than as music that sounds slightly
   odd on hardware.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_trk \
         saturn/tests/test_tracker.c saturn/src/sound/tracker.c && /tmp/t_trk
*/
#include "../src/sound/tracker.h"
#include <stdio.h>
#include <assert.h>

typedef struct { int ch, semi, oct, wave, vol; } Event;
static Event g_ev[256];
static int   g_n;

static void sink(int channel, int semitone, int octave, int wave, int vol) {
    if (g_n < 256) {
        g_ev[g_n].ch = channel;
        g_ev[g_n].semi = semitone;
        g_ev[g_n].oct = octave;
        g_ev[g_n].wave = wave;
        g_ev[g_n].vol = vol;
        g_n++;
    }
}

/* Two channels, two rows per pattern, two patterns. Note 26 is semitone
   index 24, which is semitone 0 at octave 0 (24 % 12 = 0, 24 / 12 - 2 = 0). */
static const TrackerCell CELLS[] = {
    /* pattern 0 */
    { 26, 0x07 }, {  0, 0x00 },
    {  1, 0x00 }, { 38, 0x15 },
    /* pattern 1 */
    { 27, 0x07 }, {  0, 0x00 },
    {  0, 0x00 }, {  1, 0x00 },
};
static const unsigned char ORDER[] = { 0, 1 };

static TrackerSong song(unsigned char speed, unsigned char loop_to) {
    TrackerSong s;
    s.cells = CELLS;
    s.rows = 2;
    s.channels = 2;
    s.order = ORDER;
    s.order_len = 2;
    s.loop_to = loop_to;
    s.speed = speed;
    return s;
}

static void test_first_tick_plays_row_zero(void) {
    TrackerSong s = song(1, 0);
    g_n = 0;
    tracker_start(&s, sink);
    tracker_tick();
    assert(g_n == 1);
    assert(g_ev[0].ch == 0 && g_ev[0].semi == 0 && g_ev[0].oct == 0);
    assert(g_ev[0].wave == 0 && g_ev[0].vol == 7);
}

static void test_hold_emits_nothing(void) {
    TrackerSong s = song(1, 0);
    g_n = 0;
    tracker_start(&s, sink);
    tracker_tick();
    /* Row 0 channel 1 is a hold, so only channel 0 spoke. */
    assert(g_n == 1);
}

static void test_key_off_reaches_the_sink_as_minus_one(void) {
    TrackerSong s = song(1, 0);
    g_n = 0;
    tracker_start(&s, sink);
    tracker_tick();
    tracker_tick();
    /* Row 1: channel 0 keys off, channel 1 plays note 38 (index 36 =
       semitone 0, octave 1) on wave 1 at volume 5. */
    assert(g_n == 3);
    assert(g_ev[1].ch == 0 && g_ev[1].semi == -1);
    assert(g_ev[2].ch == 1 && g_ev[2].semi == 0 && g_ev[2].oct == 1);
    assert(g_ev[2].wave == 1 && g_ev[2].vol == 5);
}

static void test_speed_divides_the_tick_rate(void) {
    TrackerSong s = song(3, 0);
    g_n = 0;
    tracker_start(&s, sink);
    tracker_tick();
    int after_first = g_n;
    tracker_tick();
    tracker_tick();
    assert(g_n == after_first);
    tracker_tick();
    assert(g_n > after_first);
}

static void test_order_advances_to_the_next_pattern(void) {
    TrackerSong s = song(1, 0);
    g_n = 0;
    tracker_start(&s, sink);
    tracker_tick();
    tracker_tick();
    g_n = 0;
    tracker_tick();
    /* Pattern 1 row 0: note 27 is semitone index 25 -- semitone 1, octave 0. */
    assert(g_n == 1);
    assert(g_ev[0].semi == 1 && g_ev[0].oct == 0);
}

static void test_end_of_song_returns_to_the_loop_point(void) {
    TrackerSong s = song(1, 1);
    g_n = 0;
    tracker_start(&s, sink);
    for (int i = 0; i < 4; i++) tracker_tick();
    g_n = 0;
    tracker_tick();
    /* loop_to 1 means it re-enters pattern 1, not pattern 0. */
    assert(g_n == 1);
    assert(g_ev[0].semi == 1 && g_ev[0].oct == 0);
}

static void test_stop_silences_and_stays_stopped(void) {
    TrackerSong s = song(1, 0);
    g_n = 0;
    tracker_start(&s, sink);
    tracker_tick();
    tracker_stop();
    assert(!tracker_playing());
    g_n = 0;
    tracker_tick();
    assert(g_n == 0);
}

static void test_tick_before_start_is_harmless(void) {
    g_n = 0;
    tracker_stop();
    tracker_tick();
    assert(g_n == 0);
}

int main(void) {
    test_first_tick_plays_row_zero();
    test_hold_emits_nothing();
    test_key_off_reaches_the_sink_as_minus_one();
    test_speed_divides_the_tick_rate();
    test_order_advances_to_the_next_pattern();
    test_end_of_song_returns_to_the_loop_point();
    test_stop_silences_and_stays_stopped();
    test_tick_before_start_is_harmless();
    printf("test_tracker: all passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_trk \
    saturn/tests/test_tracker.c saturn/src/sound/tracker.c && /tmp/t_trk
```

Expected: FAIL — `tracker.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/sound/tracker.h`:

```c
/*----------------------
 | tracker.h
 | Description: The sequencer: walks pattern data one tick at a time and emits
 |   note events to a sink. It never touches the SCSP, which is what lets the
 |   host tests assert on the event sequence instead of on hardware writes.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef TRACKER_H
#define TRACKER_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | TrackerCell
 | Description: One channel's cell in one row. note: 0 holds whatever is
 |   sounding, 1 keys off, 2 and above is a semitone index offset by two so
 |   that zero can mean "nothing here". wv packs the waveform index in the high
 |   nibble and the volume (0..7) in the low one.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned char note;
    unsigned char wv;
} TrackerCell;

/*----------------------
 | TrackerSong
 | Description: A song: a flat cell array addressed as
 |   cells[(pattern * rows + row) * channels + channel], an order list of
 |   pattern indices, the order index to jump back to when the order runs out,
 |   and the number of ticks each row is held for.
 | Author: suinevere
 ----------------------*/
typedef struct {
    const TrackerCell   *cells;
    unsigned char        rows;
    unsigned char        channels;
    const unsigned char *order;
    unsigned char        order_len;
    unsigned char        loop_to;
    unsigned char        speed;
} TrackerSong;

/*----------------------
 | TrackerSink
 | Description: Where note events go. semitone is -1 for a key off, in which
 |   case octave, wave and vol carry nothing.
 | Author: suinevere
 ----------------------*/
typedef void (*TrackerSink)(int channel, int semitone, int octave, int wave, int vol);

/*----------------------
 | tracker_start
 | Description: Begins the song at order 0, row 0. The next tracker_tick plays
 |   that row, so a caller gets a note on the first tick rather than the second.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: song -- must outlive playback; sink -- where events go
 | Returns: N/A
 ----------------------*/
void tracker_start(const TrackerSong *song, TrackerSink sink);

/*----------------------
 | tracker_stop
 | Description: Stops playback. Emits nothing -- silencing the voices is the
 |   caller's, since only it knows which ones are sounding.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void tracker_stop(void);

/*----------------------
 | tracker_tick
 | Description: Advances one tick. Every `speed` ticks a row is played and the
 |   position advances, wrapping through the order list to loop_to at the end.
 |   A no-op when stopped, so a V-blank handler can call it unconditionally.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void tracker_tick(void);

/*----------------------
 | tracker_playing
 | Description: Whether a song is running.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: nonzero while playing
 ----------------------*/
int tracker_playing(void);

#ifdef __cplusplus
}
#endif
#endif /* TRACKER_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/sound/tracker.c`:

```c
/*----------------------
 | tracker.c
 | Description: Implementation of the sequencer. The tick counter counts down
 |   to zero rather than up to speed, so a row plays on the tick that reaches
 |   it and the first tick after a start plays row zero.
 | Author: suinevere
 | Dependencies: tracker.h
 ----------------------*/
#include "tracker.h"

static const TrackerSong *g_song;
static TrackerSink        g_sink;
static int                g_playing;
static int                g_order;
static int                g_row;
static int                g_countdown;

void tracker_start(const TrackerSong *song, TrackerSink sink) {
    g_song = song;
    g_sink = sink;
    g_order = 0;
    g_row = 0;
    g_countdown = 0;
    g_playing = (song != 0 && sink != 0 && song->order_len > 0 && song->rows > 0);
}

void tracker_stop(void) {
    g_playing = 0;
}

int tracker_playing(void) {
    return g_playing;
}

void tracker_tick(void) {
    if (!g_playing) return;

    if (g_countdown > 0) {
        g_countdown--;
        return;
    }

    const TrackerSong *s = g_song;
    int pattern = s->order[g_order];
    const TrackerCell *row = s->cells + ((pattern * s->rows + g_row) * s->channels);

    for (int c = 0; c < s->channels; c++) {
        unsigned char note = row[c].note;
        if (note == 0) continue;
        if (note == 1) {
            g_sink(c, -1, 0, 0, 0);
        } else {
            int index = note - 2;
            g_sink(c, index % 12, index / 12 - 2, row[c].wv >> 4, row[c].wv & 0x0F);
        }
    }

    g_countdown = (s->speed > 0) ? s->speed - 1 : 0;
    g_row++;
    if (g_row >= s->rows) {
        g_row = 0;
        g_order++;
        if (g_order >= s->order_len) g_order = s->loop_to;
    }
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_trk \
    saturn/tests/test_tracker.c saturn/src/sound/tracker.c && /tmp/t_trk
```

Expected: `test_tracker: all passed`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/sound/tracker.h saturn/src/sound/tracker.c saturn/tests/test_tracker.c
git commit -m "Add the sequencer, emitting note events to a sink rather than to the sound chip so its rows, speed, pattern order and loop point are all assertable on the host."
```

---

### Task 4: The authored loop

**Files:**
- Create: `saturn/src/sound/music_synth_data.c`
- Create: `saturn/src/sound/music_synth_data.h`
- Test: `saturn/tests/test_synth_song.c`

**Interfaces:**
- Consumes: `TrackerSong`, `TrackerCell` from Task 3; `SYNTH_WAVE_*` from Task 2.
- Produces: `const TrackerSong *music_synth_song(void)`.

**Note:** the loop authored here is four patterns. The spec's 2 KB budget line allows up to eight; patterns are pure data, so extending the tune later changes no engine code.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_synth_song.c`:

```c
/* The shipped loop, checked for the things that make a song unplayable rather
   than merely bad: an order entry pointing at a pattern that does not exist, a
   loop point past the end of the order, a waveform index with no waveform
   behind it, or a speed of zero, which would play every row on every tick.

   The tune itself is a matter of taste and is not asserted here. What is
   asserted is that it plays at all, and that it ends where it says it does --
   a loop point off the end would run the tracker into whatever follows the
   order array in memory.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_song \
         saturn/tests/test_synth_song.c saturn/src/sound/music_synth_data.c \
         saturn/src/sound/tracker.c && /tmp/t_song
*/
#include "../src/sound/music_synth_data.h"
#include "../src/sound/tracker.h"
#include <stdio.h>
#include <assert.h>

static void test_song_exists_and_is_shaped(void) {
    const TrackerSong *s = music_synth_song();
    assert(s != 0);
    assert(s->cells != 0);
    assert(s->order != 0);
    assert(s->rows > 0);
    assert(s->channels > 0 && s->channels <= 4);
    assert(s->order_len > 0);
    assert(s->speed > 0);
}

static void test_loop_point_is_inside_the_order(void) {
    const TrackerSong *s = music_synth_song();
    assert(s->loop_to < s->order_len);
}

static void test_every_order_entry_names_a_real_pattern(void) {
    const TrackerSong *s = music_synth_song();
    int highest = 0;
    for (int i = 0; i < s->order_len; i++)
        if (s->order[i] > highest) highest = s->order[i];
    assert(highest < MUSIC_SYNTH_PATTERNS);
}

static void test_every_cell_is_playable(void) {
    const TrackerSong *s = music_synth_song();
    int cells = MUSIC_SYNTH_PATTERNS * s->rows * s->channels;
    for (int i = 0; i < cells; i++) {
        unsigned char note = s->cells[i].note;
        if (note < 2) continue;
        int index = note - 2;
        assert(index / 12 - 2 >= -8 && index / 12 - 2 <= 7);
        assert((s->cells[i].wv >> 4) <= 4);
        assert((s->cells[i].wv & 0x0F) <= 7);
    }
}

static void test_a_null_sink_does_not_start_playback(void) {
    /* The song is data and the tracker is told where to send it; handing it
       nowhere must leave it stopped rather than dereferencing the sink on the
       first tick. */
    tracker_start(music_synth_song(), 0);
    assert(!tracker_playing());
}

int main(void) {
    test_song_exists_and_is_shaped();
    test_loop_point_is_inside_the_order();
    test_every_order_entry_names_a_real_pattern();
    test_every_cell_is_playable();
    test_a_null_sink_does_not_start_playback();
    printf("test_synth_song: all passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_song \
    saturn/tests/test_synth_song.c saturn/src/sound/music_synth_data.c \
    saturn/src/sound/tracker.c && /tmp/t_song
```

Expected: FAIL — `music_synth_data.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/sound/music_synth_data.h`:

```c
/*----------------------
 | music_synth_data.h
 | Description: The one loop the synth plays, in both builds. Data only: the
 |   engine reads it and never reaches back, so a different tune is a different
 |   table and no code change.
 | Author: suinevere
 | Dependencies: tracker.h
 ----------------------*/
#ifndef MUSIC_SYNTH_DATA_H
#define MUSIC_SYNTH_DATA_H

#include "tracker.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | MUSIC_SYNTH_PATTERNS
 | Description: How many patterns the cell array holds. The order list indexes
 |   into that count, so the two have to agree or the tracker reads past the
 |   table -- which is what test_synth_song.c pins.
 | Author: suinevere
 ----------------------*/
#define MUSIC_SYNTH_PATTERNS 4

/*----------------------
 | music_synth_song
 | Description: The shipped loop.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: a song valid for the life of the program
 ----------------------*/
const TrackerSong *music_synth_song(void);

#ifdef __cplusplus
}
#endif
#endif /* MUSIC_SYNTH_DATA_H */
```

- [ ] **Step 4: Write the data**

Create `saturn/src/sound/music_synth_data.c`. Note byte is semitone index + 2, where index 26 is C at octave 0; one octave is 12. Channel 0 is the bass (triangle), channel 1 the lead (pulse), channel 2 a counter-line (square), channel 3 unused for now.

```c
/*----------------------
 | music_synth_data.c
 | Description: The shipped loop -- a slow four-pattern figure in A minor,
 |   bass on the triangle, lead on the pulse, a sparse square counter-line.
 |   Note bytes are a semitone index plus two, since zero has to mean "hold";
 |   index 26 sounds C at octave 0, and twelve indices make an octave.
 | Author: suinevere
 ----------------------*/
#include "music_synth_data.h"
#include "synth.h"

#define R  0                                  /* hold */
#define X  1                                  /* key off */
#define N(semi, oct) ((unsigned char)(((oct) + 2) * 12 + (semi) + 2))
#define V(wave, vol) ((unsigned char)(((wave) << 4) | (vol)))

#define A  9
#define C  0
#define D  2
#define E  4
#define F  5
#define G  7

static const TrackerCell MUSIC_CELLS[MUSIC_SYNTH_PATTERNS * 16 * 3] = {
    /* pattern 0 -- Am */
    { N(A, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(A, 1), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { N(C, 2), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { N(E, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(E, 2), V(SYNTH_WAVE_PULSE, 4) }, { N(A, 0), V(SYNTH_WAVE_SQUARE, 3) },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { X, 0 },                            { X, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { N(A, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(D, 2), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { N(C, 2), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { N(E, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(A, 1), V(SYNTH_WAVE_PULSE, 4) }, { N(E, 0), V(SYNTH_WAVE_SQUARE, 3) },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { X, 0 },                                { X, 0 },                            { X, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },

    /* pattern 1 -- F */
    { N(F, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(C, 2), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { N(A, 1), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { N(C, 0), V(SYNTH_WAVE_TRIANGLE, 6) },  { N(F, 1), V(SYNTH_WAVE_PULSE, 4) }, { N(C, 0), V(SYNTH_WAVE_SQUARE, 3) },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { X, 0 },                            { X, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { N(F, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(G, 1), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { N(A, 1), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { N(C, 0), V(SYNTH_WAVE_TRIANGLE, 6) },  { N(C, 2), V(SYNTH_WAVE_PULSE, 4) }, { N(G, 0), V(SYNTH_WAVE_SQUARE, 3) },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { X, 0 },                                { X, 0 },                            { X, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },

    /* pattern 2 -- G */
    { N(G, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(D, 2), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { N(G, 1), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { N(D, 0), V(SYNTH_WAVE_TRIANGLE, 6) },  { N(E, 2), V(SYNTH_WAVE_PULSE, 4) }, { N(D, 0), V(SYNTH_WAVE_SQUARE, 3) },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { X, 0 },                            { X, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { N(G, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(D, 2), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { N(C, 2), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { N(D, 0), V(SYNTH_WAVE_TRIANGLE, 6) },  { N(A, 1), V(SYNTH_WAVE_PULSE, 4) }, { N(D, 0), V(SYNTH_WAVE_SQUARE, 3) },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { X, 0 },                                { X, 0 },                            { X, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },

    /* pattern 3 -- Am, resolving */
    { N(A, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(A, 1), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { N(E, 2), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { N(E, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(C, 2), V(SYNTH_WAVE_PULSE, 4) }, { N(A, 0), V(SYNTH_WAVE_SQUARE, 3) },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { X, 0 },                            { X, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { N(A, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(A, 1), V(SYNTH_WAVE_PULSE, 5) }, { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
    { X, 0 },                                { X, 0 },                            { X, 0 },
    { R, 0 },                                { R, 0 },                            { R, 0 },
};

static const unsigned char MUSIC_ORDER[] = { 0, 1, 2, 3 };

static const TrackerSong MUSIC_SONG = {
    MUSIC_CELLS, 16, 3, MUSIC_ORDER, 4, 0, 9
};

const TrackerSong *music_synth_song(void) {
    return &MUSIC_SONG;
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_song \
    saturn/tests/test_synth_song.c saturn/src/sound/music_synth_data.c \
    saturn/src/sound/tracker.c && /tmp/t_song
```

Expected: `test_synth_song: all passed`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/sound/music_synth_data.h saturn/src/sound/music_synth_data.c saturn/tests/test_synth_song.c
git commit -m "Author the shipped loop as four patterns of pattern data, with a test that pins the things which would make a song unplayable rather than merely unlovely."
```

---

### Task 5: The public API

**Files:**
- Modify: `saturn/src/sound/synth.h` (append the five-function API and the gating helper)
- Modify: `saturn/src/sound/synth.c` (append the implementation)
- Test: `saturn/tests/test_synth_api.c`

**Interfaces:**
- Consumes: `scsp.h` (Task 1), `synth_pitch`/`synth_wave_build` (Task 2), `tracker.h` (Task 3), `music_synth_data.h` (Task 4).
- Produces: `void synth_bind(volatile unsigned short *regs, volatile signed char *wave_ram, unsigned long wave_sa)`, `void synth_note_on(int channel, int semitone, int octave, int wave, int vol)`, `void synth_init(void)`, `void synth_start(void)`, `void synth_stop(void)`, `void synth_set_level(int level)`, `void synth_tick(void)`, `int synth_playing(void)`, `int synth_should_play(int has_cd_audio)`. `synth_note_on` matches `TrackerSink`'s signature exactly, because it is passed directly as the sink.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_synth_api.c`:

```c
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

static unsigned short g_regs[32 * 16];
static signed char    g_wave[SCSP_WAVES * SCSP_WAVE_MAX];

#define SLOT_WORD(voice, off) g_regs[(SCSP_SLOT_FIRST + (voice)) * 16 + ((off) / 2)]

static void bind_fresh(void) {
    for (int i = 0; i < 32 * 16; i++) g_regs[i] = 0;
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
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_api \
    saturn/tests/test_synth_api.c saturn/src/sound/synth.c \
    saturn/src/sound/scsp.c saturn/src/sound/tracker.c \
    saturn/src/sound/music_synth_data.c && /tmp/t_api
```

Expected: FAIL — `undefined reference to 'synth_bind'`.

- [ ] **Step 3: Append the API to the header**

Add to `saturn/src/sound/synth.h`, immediately before the closing `#ifdef __cplusplus`:

```c
/*----------------------
 | synth_bind
 | Description: Points the synth at the sound chip. On the target this is the
 |   SCSP register window and the waveform area inside sound RAM; the host
 |   tests pass arrays. Call before synth_init.
 | Author: suinevere
 | Dependencies: scsp.h
 | Globals: N/A
 | Params: regs -- slot register file base; wave_ram -- waveform memory;
 |   wave_sa -- SCSP address of wave_ram
 | Returns: N/A
 ----------------------*/
void synth_bind(volatile unsigned short *regs, volatile signed char *wave_ram, unsigned long wave_sa);

/*----------------------
 | synth_note_on
 | Description: Sounds one note, or releases the channel when semitone is -1.
 |   This is the tracker's sink, public because it is also the only way to play
 |   a note that is not in the song -- which is how the noise voice is reached
 |   and tested, since the shipped loop carries no percussion yet. Scales the
 |   note's own volume by the current music level on the way to the chip, and
 |   remembers both so a later level change can rescale a sounding voice.
 | Author: suinevere
 | Dependencies: scsp.h
 | Globals: N/A
 | Params: channel -- 0..SCSP_VOICES-1; semitone -- 0..11 or -1 to release;
 |   octave -- -8..7; wave -- a SYNTH_WAVE_* value; vol -- 0..7
 | Returns: N/A
 ----------------------*/
void synth_note_on(int channel, int semitone, int octave, int wave, int vol);

/*----------------------
 | synth_init
 | Description: Silences the owned slots and uploads the four generated
 |   waveforms. Safe to call twice.
 | Author: suinevere
 | Dependencies: scsp.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_init(void);

/*----------------------
 | synth_start
 | Description: Starts the shipped loop from its beginning. The first
 |   synth_tick after this sounds the first row.
 | Author: suinevere
 | Dependencies: tracker.h, music_synth_data.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_start(void);

/*----------------------
 | synth_stop
 | Description: Stops the loop and keys every voice off.
 | Author: suinevere
 | Dependencies: tracker.h, scsp.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_stop(void);

/*----------------------
 | synth_set_level
 | Description: Sets the music level, 0 (silent) to 7. Reaches voices that are
 |   already sounding without restarting them, because the in-game duck happens
 |   mid-note. Applied per slot, never through the machine's master volume,
 |   which CD-DA and the splash jingle share.
 | Author: suinevere
 | Dependencies: scsp.h
 | Globals: N/A
 | Params: level -- 0..7, clamped
 | Returns: N/A
 ----------------------*/
void synth_set_level(int level);

/*----------------------
 | synth_tick
 | Description: One V-blank of playback. A no-op when stopped, so the handler
 |   can call it unconditionally.
 | Author: suinevere
 | Dependencies: tracker.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_tick(void);

/*----------------------
 | synth_playing
 | Description: Whether the loop is running.
 | Author: suinevere
 | Dependencies: tracker.h
 | Globals: N/A
 | Params: N/A
 | Returns: nonzero while playing
 ----------------------*/
int synth_playing(void);

/*----------------------
 | synth_should_play
 | Description: Whether the synth is the right music source. It is the
 |   fallback, so the answer is no wherever the disc brought its own music.
 |   The netbin passes 0 because it has no disc, which makes this one rule for
 |   both builds rather than a compile-time switch.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: has_cd_audio -- nonzero when the disc carries CD-DA
 | Returns: nonzero when the synth should play
 ----------------------*/
int synth_should_play(int has_cd_audio);
```

- [ ] **Step 4: Append the implementation**

Add to the end of `saturn/src/sound/synth.c`:

```c
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
```

- [ ] **Step 5: Run test to verify it passes**

```bash
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_api \
    saturn/tests/test_synth_api.c saturn/src/sound/synth.c \
    saturn/src/sound/scsp.c saturn/src/sound/tracker.c \
    saturn/src/sound/music_synth_data.c && /tmp/t_api
```

Expected: `test_synth_api: all passed`

- [ ] **Step 6: Re-run every earlier test**

```bash
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_scsp saturn/tests/test_scsp.c saturn/src/sound/scsp.c && /tmp/t_scsp
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_note saturn/tests/test_synth_note.c saturn/src/sound/synth.c saturn/src/sound/scsp.c saturn/src/sound/tracker.c saturn/src/sound/music_synth_data.c && /tmp/t_note
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_trk saturn/tests/test_tracker.c saturn/src/sound/tracker.c && /tmp/t_trk
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_song saturn/tests/test_synth_song.c saturn/src/sound/music_synth_data.c saturn/src/sound/tracker.c && /tmp/t_song
```

Expected: four `all passed` lines. Note `test_synth_note` now needs the extra objects, since `synth.c` gained references to the tracker and the song — update the build line in that test file's own header comment to match, since those comments are how the next person compiles them.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/sound/synth.h saturn/src/sound/synth.c saturn/tests/test_synth_api.c
git commit -m "Give the synth its five-function public surface, scaling each note by the music level on the way to the chip so a mid-note duck changes volume without restarting the voice."
```

---

### Task 6: Target wiring

**Files:**
- Create: `saturn/src/sound/synth_target.h`
- Create: `saturn/src/sound/synth_target.cxx`
- Modify: `saturn/makefile:63-102` (the netbin `SOURCES` list)

**Interfaces:**
- Consumes: `synth_bind`, `synth_init`, `synth_tick` (Task 5).
- Produces: `void synth_target_init(void)` — binds the real addresses, puts the sound block into a known state, subscribes the V-blank tick.

This is the one task with no host test: it exists to touch hardware. It is verified by compiling and by Task 10's ODE run.

- [ ] **Step 1: Write the header**

Create `saturn/src/sound/synth_target.h`:

```c
/*----------------------
 | synth_target.h
 | Description: The Saturn-side half of the synth: the real SCSP addresses, the
 |   sound-block state the netbin has to establish for itself, and the V-blank
 |   subscription that drives the tick. Split from synth.c so that file stays
 |   plain C and keeps compiling on the host, where none of this exists.
 | Author: suinevere
 | Dependencies: srl.hpp
 ----------------------*/
#ifndef SYNTH_TARGET_H
#define SYNTH_TARGET_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | synth_target_init
 | Description: Binds the synth to the SCSP, uploads its waveforms and
 |   subscribes the V-blank tick. Call once at startup, before any synth_start.
 |   Idempotent: a second call re-binds and re-uploads but subscribes only once.
 | Author: suinevere
 | Dependencies: SRL (Core::OnVblank), SGL (slSoundOffWait), synth.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_target_init(void);

#ifdef __cplusplus
}
#endif
#endif /* SYNTH_TARGET_H */
```

- [ ] **Step 2: Write the implementation**

Create `saturn/src/sound/synth_target.cxx`:

```cpp
/*----------------------
 | synth_target.cxx
 | Description: Binds the synth to real hardware and drives it from V-blank.
 |   Two addresses matter: the SCSP register window at 0x25B00000 and the
 |   waveform area at 0x25A70000, high in sound RAM and clear of the region the
 |   SGL sound driver allocates from the bottom.
 | Author: suinevere
 | Dependencies: srl.hpp, synth.h, synth_target.h
 ----------------------*/
#include <srl.hpp>

extern "C" {
#include "synth.h"
#include "synth_target.h"
}

/*----------------------
 | SYNTH_SCSP_REGS / SYNTH_WAVE_RAM / SYNTH_WAVE_SA
 | Description: The SCSP register file, the sound-RAM address the waveforms are
 |   copied to, and that same address as the SCSP sees it -- sound RAM is
 |   0x25A00000 to the SH-2 and 0 to the chip, so the SA field carries the
 |   offset alone.
 | Author: suinevere
 ----------------------*/
#define SYNTH_SCSP_REGS ((volatile unsigned short*) 0x25B00000)
#define SYNTH_WAVE_RAM  ((volatile signed char*)   0x25A70000)
#define SYNTH_WAVE_SA   0x70000UL

static bool g_synth_subscribed = false;

/*----------------------
 | synth_target_vblank
 | Description: One tick per frame. Runs in interrupt context, which is the
 |   point: the netbin blocks on modem reads and the CD build blocks on disc
 |   reads, and a tick driven from either main loop would lose tempo every time
 |   one of those happened.
 | Author: suinevere
 | Dependencies: synth.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void synth_target_vblank(void) {
    synth_tick();
}

void synth_target_init(void) {
    /* The netbin never turns the sound block on -- SRL only does that when the
       SGL driver is enabled, and this build disables it (srl_core.hpp:107).
       PlanetWeb leaves the block in whatever state its own audio finished in,
       so put it into a known one rather than inheriting it. In the CD build the
       driver is already up and this is a no-op against slots it does not own. */
#ifdef NETBIN
    slSoundOffWait();
#endif

    synth_bind(SYNTH_SCSP_REGS, SYNTH_WAVE_RAM, SYNTH_WAVE_SA);
    synth_init();

    if (!g_synth_subscribed) {
        SRL::Core::OnVblank += synth_target_vblank;
        g_synth_subscribed = true;
    }
}
```

- [ ] **Step 3: Add the files to the netbin source list**

In `saturn/makefile`, inside the `ifeq ($(strip $(NETBIN)),1)` block's `SOURCES` list (which begins at line 64), add these four lines after `src/engine/app_state.cxx`:

```make
          src/sound/scsp.c \
          src/sound/synth.c \
          src/sound/tracker.c \
          src/sound/music_synth_data.c \
          src/sound/synth_target.cxx
```

Take care with the line continuations: the last entry in the list must not end with a backslash. The CD build needs no change — its `SOURCES` is a `find` over `src/`, which picks the new files up on its own.

- [ ] **Step 4: Type-check both configurations**

```bash
cd saturn && sh syntax-check.sh src/sound/synth_target.cxx
```

Expected: clean exit, no output.

- [ ] **Step 5: Build the netbin**

```bash
cd saturn && cmd //c compile-netbin.bat clean && cmd //c compile-netbin.bat
```

Expected: `zaturn.netbin` written to `BuildDrop/`. Incremental builds misreport size by about 32 bytes, which is why the clean comes first.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/sound/synth_target.h saturn/src/sound/synth_target.cxx saturn/makefile
git commit -m "Wire the synth to real hardware: the SCSP window, a waveform area high in sound RAM clear of the sound driver, and a V-blank tick that holds tempo while the main loop is blocked on the modem or the drive."
```

---

### Task 7: Starting and stopping in both builds

**Files:**
- Modify: `saturn/src/main_netbin.cxx:246-310` (startup) and its `for (;;)` loop
- Modify: `saturn/src/main.cxx` (startup, alongside the existing music setup)
- Test: `saturn/tests/test_synth_gating.c`

**Interfaces:**
- Consumes: `synth_target_init`, `synth_should_play`, `synth_start`, `synth_set_level` (Tasks 5-6); `music_cdda_has_audio()` from `saturn/src/sound/music.h:267`; `g_synth_level` (Task 8).

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_synth_gating.c`:

```c
/* Which build plays the synth, and when.

   The rule is one line -- the synth is the fallback, so it plays wherever the
   disc did not bring music -- but it has to hold for both builds without a
   compile-time switch, because a rule that reads differently in the two builds
   is a rule that will drift apart. The netbin passes 0 because it has no disc
   at all.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_gate \
         saturn/tests/test_synth_gating.c saturn/src/sound/synth.c \
         saturn/src/sound/scsp.c saturn/src/sound/tracker.c \
         saturn/src/sound/music_synth_data.c && /tmp/t_gate
*/
#include "../src/sound/synth.h"
#include <stdio.h>
#include <assert.h>

static void test_disc_with_cd_audio_keeps_the_synth_quiet(void) {
    assert(synth_should_play(1) == 0);
}

static void test_disc_without_cd_audio_hands_over_to_the_synth(void) {
    assert(synth_should_play(0) != 0);
}

static void test_the_netbin_case_is_the_same_rule(void) {
    /* The netbin has no disc, so it asks the same question with 0 and gets
       the same answer -- no #ifdef in the decision. */
    int netbin_has_cd_audio = 0;
    assert(synth_should_play(netbin_has_cd_audio) != 0);
}

int main(void) {
    test_disc_with_cd_audio_keeps_the_synth_quiet();
    test_disc_without_cd_audio_hands_over_to_the_synth();
    test_the_netbin_case_is_the_same_rule();
    printf("test_synth_gating: all passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it passes already**

```bash
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_gate \
    saturn/tests/test_synth_gating.c saturn/src/sound/synth.c \
    saturn/src/sound/scsp.c saturn/src/sound/tracker.c \
    saturn/src/sound/music_synth_data.c && /tmp/t_gate
```

Expected: PASS. `synth_should_play` landed in Task 5; this test exists to pin the rule at the point the call sites start depending on it, and it is the regression guard if someone later adds a build switch to it.

- [ ] **Step 3: Start the synth in the netbin**

In `saturn/src/main_netbin.cxx`, add the include next to the existing ones:

```cpp
#include "synth.h"
#include "synth_target.h"
```

and in `main()`, immediately after `options_load();`:

```cpp
    /* The netbin has no disc and therefore no CD-DA, so the fallback rule
       always resolves in the synth's favour here -- asked through the same
       function the CD build asks, rather than assumed. */
    synth_target_init();
    if (synth_should_play(0)) {
        synth_set_level(g_synth_level);
        synth_start();
    }
```

- [ ] **Step 4: Start the synth in the CD build**

In `saturn/src/main.cxx`, add the same two includes, and after the existing options load and music setup:

```cpp
    /* Fallback only: a disc with CD-DA keeps its own music and the synth stays
       stopped. music_cdda_has_audio() is the same source the Sound page's
       has_cd row gate reads, so the page and the engine cannot disagree. */
    synth_target_init();
    if (synth_should_play(music_cdda_has_audio())) {
        synth_set_level(g_synth_level);
        synth_start();
    }
```

- [ ] **Step 5: Type-check and build both**

```bash
cd saturn && sh syntax-check.sh src/main_netbin.cxx src/main.cxx
cd saturn && cmd //c compile-netbin.bat clean && cmd //c compile-netbin.bat
```

Expected: clean type-check, netbin builds.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/main_netbin.cxx saturn/src/main.cxx saturn/tests/test_synth_gating.c
git commit -m "Start the synth in both builds through one fallback rule, so a disc that brought its own music keeps it and everything else gets the generated loop."
```

---

### Task 8: Sound page rows and the level global

**Files:**
- Modify: `saturn/src/menu/menu_layout.h` (row IDs and the row-building function)
- Modify: `saturn/src/menu/menu_layout.c` (the implementation)
- Modify: `saturn/src/engine/app_state.h:102-105` (add `g_synth_level`)
- Modify: `saturn/src/engine/app_state.cxx` (define it)
- Modify: `saturn/src/menu/menu_pages.cxx:697-800` (use the new row list, add the Synth Music row)
- Modify: `saturn/src/net/netbin_pages.cxx:898` (add "Sound" to the pause menu)
- Test: `saturn/tests/test_sound_rows.c`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `enum { SND_ROW_MASTER = 0, SND_ROW_CD = 1, SND_ROW_SYNTH = 2, SND_ROW_PCM = 3, SND_ROW_OK = 4, SND_ROW_CANCEL = 5 }` and `int sound_page_rows(int has_cd, int has_blb, int *rows, int max)` returning the count written; `extern int g_synth_level`.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_sound_rows.c`:

```c
/* Which rows the Sound page shows.

   The page has always built its row list from what is available rather than
   from a fixed layout, and remembers the selection as a row ID rather than an
   index, because the same index names a different row on a different disc.
   The synth adds one more row to that list under one rule: it is the fallback,
   so its slider appears exactly where the CD Music slider does not. The page
   must never offer two music sliders at once, which is the assertion below
   that is worth keeping if every other one is rewritten.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/menu -I saturn/src/video \
         -o /tmp/t_rows saturn/tests/test_sound_rows.c \
         saturn/src/menu/menu_layout.c && /tmp/t_rows
*/
#include "../src/menu/menu_layout.h"
#include <stdio.h>
#include <assert.h>

static int has_row(const int *rows, int n, int want) {
    for (int i = 0; i < n; i++) if (rows[i] == want) return 1;
    return 0;
}

static void test_cd_disc_shows_cd_and_hides_synth(void) {
    int rows[8];
    int n = sound_page_rows(1, 0, rows, 8);
    assert(has_row(rows, n, SND_ROW_CD));
    assert(!has_row(rows, n, SND_ROW_SYNTH));
}

static void test_silent_disc_shows_synth_and_hides_cd(void) {
    int rows[8];
    int n = sound_page_rows(0, 0, rows, 8);
    assert(has_row(rows, n, SND_ROW_SYNTH));
    assert(!has_row(rows, n, SND_ROW_CD));
}

static void test_never_two_music_sliders(void) {
    for (int cd = 0; cd < 2; cd++)
        for (int blb = 0; blb < 2; blb++) {
            int rows[8];
            int n = sound_page_rows(cd, blb, rows, 8);
            assert(!(has_row(rows, n, SND_ROW_CD) && has_row(rows, n, SND_ROW_SYNTH)));
        }
}

static void test_master_row_is_always_present_now(void) {
    /* It used to be hidden when neither CD-DA nor a Blorb was there, because
       it would have switched two levels nothing read. The synth is always a
       source, so there is always something for it to switch. */
    for (int cd = 0; cd < 2; cd++)
        for (int blb = 0; blb < 2; blb++) {
            int rows[8];
            int n = sound_page_rows(cd, blb, rows, 8);
            assert(has_row(rows, n, SND_ROW_MASTER));
        }
}

static void test_pcm_row_follows_the_blorb(void) {
    int rows[8];
    int n = sound_page_rows(0, 1, rows, 8);
    assert(has_row(rows, n, SND_ROW_PCM));
    n = sound_page_rows(0, 0, rows, 8);
    assert(!has_row(rows, n, SND_ROW_PCM));
}

static void test_ok_and_cancel_come_last_and_always(void) {
    for (int cd = 0; cd < 2; cd++)
        for (int blb = 0; blb < 2; blb++) {
            int rows[8];
            int n = sound_page_rows(cd, blb, rows, 8);
            assert(n >= 2);
            assert(rows[n - 2] == SND_ROW_OK);
            assert(rows[n - 1] == SND_ROW_CANCEL);
        }
}

static void test_the_netbin_page_is_master_synth_ok_cancel(void) {
    int rows[8];
    int n = sound_page_rows(0, 0, rows, 8);
    assert(n == 4);
    assert(rows[0] == SND_ROW_MASTER);
    assert(rows[1] == SND_ROW_SYNTH);
    assert(rows[2] == SND_ROW_OK);
    assert(rows[3] == SND_ROW_CANCEL);
}

static void test_a_short_buffer_is_not_overrun(void) {
    int rows[3];
    int n = sound_page_rows(1, 1, rows, 3);
    assert(n <= 3);
}

int main(void) {
    test_cd_disc_shows_cd_and_hides_synth();
    test_silent_disc_shows_synth_and_hides_cd();
    test_never_two_music_sliders();
    test_master_row_is_always_present_now();
    test_pcm_row_follows_the_blorb();
    test_ok_and_cancel_come_last_and_always();
    test_the_netbin_page_is_master_synth_ok_cancel();
    test_a_short_buffer_is_not_overrun();
    printf("test_sound_rows: all passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -O2 -I saturn/src -I saturn/src/menu -I saturn/src/video \
    -o /tmp/t_rows saturn/tests/test_sound_rows.c \
    saturn/src/menu/menu_layout.c && /tmp/t_rows
```

Expected: FAIL — `SND_ROW_MASTER undeclared`.

- [ ] **Step 3: Declare the rows**

Add to `saturn/src/menu/menu_layout.h`, before its closing guard:

```c
/*----------------------
 | SND_ROW_*
 | Description: The Sound page's rows, as IDs rather than positions. The page
 |   shows a different subset on a different disc, so the same position names a
 |   different row and only the ID is stable enough to remember a selection by.
 | Author: suinevere
 ----------------------*/
enum {
    SND_ROW_MASTER = 0,
    SND_ROW_CD     = 1,
    SND_ROW_SYNTH  = 2,
    SND_ROW_PCM    = 3,
    SND_ROW_OK     = 4,
    SND_ROW_CANCEL = 5
};

/*----------------------
 | sound_page_rows
 | Description: Fills `rows` with the Sound page's visible rows in display
 |   order. CD Music and Synth Music are mutually exclusive -- the synth is the
 |   fallback, so its slider appears exactly where CD-DA is absent, and the page
 |   never offers two music sliders at once. Ok and Cancel are always the last
 |   two.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: has_cd -- disc carries CD-DA; has_blb -- game carries a sound blorb;
 |   rows -- destination; max -- its capacity
 | Returns: how many rows were written
 ----------------------*/
int sound_page_rows(int has_cd, int has_blb, int *rows, int max);
```

- [ ] **Step 4: Implement it**

Add to `saturn/src/menu/menu_layout.c`:

```c
int sound_page_rows(int has_cd, int has_blb, int *rows, int max) {
    int n = 0;
    if (n < max) rows[n++] = SND_ROW_MASTER;
    if (has_cd) { if (n < max) rows[n++] = SND_ROW_CD; }
    else        { if (n < max) rows[n++] = SND_ROW_SYNTH; }
    if (has_blb) { if (n < max) rows[n++] = SND_ROW_PCM; }
    if (n < max) rows[n++] = SND_ROW_OK;
    if (n < max) rows[n++] = SND_ROW_CANCEL;
    return n;
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
gcc -O2 -I saturn/src -I saturn/src/menu -I saturn/src/video \
    -o /tmp/t_rows saturn/tests/test_sound_rows.c \
    saturn/src/menu/menu_layout.c && /tmp/t_rows
```

Expected: `test_sound_rows: all passed`

- [ ] **Step 6: Add the level global**

In `saturn/src/engine/app_state.h`, after the `g_pcm_level` declaration at line 105:

```c
/*----------------------
 | g_synth_level
 | Description: The generated music's level, 0 (silent) to 7 -- the Synth Music
 |   row on the Sound page. Separate from g_music_level because the two are
 |   never both in play: one is CD-DA's level, the other the synth's, and the
 |   page shows whichever the disc calls for.
 | Author: suinevere
 ----------------------*/
extern int g_synth_level;
```

In `saturn/src/engine/app_state.cxx`, beside the other level definitions:

```cpp
int g_synth_level = 5;
```

- [ ] **Step 7: Use the new rows in the Sound page**

In `saturn/src/menu/menu_pages.cxx`, in `sound_options_page` (line 697 onwards):

1. Delete the local `enum { SR_SOUND, SR_MUSIC, SR_PCM, SR_OK, SR_CANCEL };` and the hand-built `rows[]`/`nrows` block, replacing them with:

```cpp
    int rows[8];
    int nrows = sound_page_rows(has_cd, has_blb, rows, 8);
```

2. Replace every `SR_SOUND` with `SND_ROW_MASTER`, `SR_MUSIC` with `SND_ROW_CD`, `SR_PCM` with `SND_ROW_PCM`, `SR_OK` with `SND_ROW_OK`, `SR_CANCEL` with `SND_ROW_CANCEL`, and change `static int last_row = SR_SOUND;` to `static int last_row = SND_ROW_MASTER;`.

3. Extend the snapshot taken for Cancel to carry the synth level:

```cpp
    int s_mus = g_music_level, s_pcm = g_pcm_level, s_syn = g_synth_level;
```

and in both Cancel restore paths, beside the existing two restores:

```cpp
            g_synth_level = s_syn;
            synth_set_level(g_synth_level);
```

4. Extend the master row so it covers the synth. Replace the master toggle body with:

```cpp
            bool on = (g_music_level > 0 || g_pcm_level > 0 || g_synth_level > 0);
            g_music_level  = on ? 0 : MUSIC_LEVEL_DEFAULT;
            g_pcm_level    = on ? 0 : PCM_LEVEL_DEFAULT;
            g_synth_level  = on ? 0 : SYNTH_LEVEL_DEFAULT;
            music_set_volume(g_music_level);
            sound_set_level(g_pcm_level);
            synth_set_level(g_synth_level);
```

and the master row's displayed value with:

```cpp
                              (g_music_level > 0 || g_pcm_level > 0 || g_synth_level > 0) ? "On" : "Off");
```

5. Add the Synth Music row's handling beside the CD Music row's:

```cpp
        else if (row == SND_ROW_SYNTH) { if (left && g_synth_level > 0) g_synth_level--;
                                         if (right && g_synth_level < 7) g_synth_level++;
                                         if (left || right) synth_set_level(g_synth_level); }
```

and its drawing as a new arm in the `switch (rows[i])` block (line 786), beside `SND_ROW_CD`'s:

```cpp
                case SND_ROW_SYNTH:
                    menu_rowf(fx, fw, y++, i == sel, SND_ROW_W, "%s%s%d", n,
                              menu_pad("Synth Music", SND_LABEL_W), g_synth_level);
                    break;
```

The label is "Synth Music", not "Music": the master row already draws `menu_pad("Music", SND_LABEL_W)` with its On/Off value, so reusing that word would put two rows called Music on the same page. Eleven characters fits `SND_LABEL_W` (14) with no change to the box.

6. Add `#define SYNTH_LEVEL_DEFAULT 5` next to the existing level defaults, and include `synth.h` at the top of the file.

- [ ] **Step 8: Add the Sound entry to the netbin pause menu**

`netbin_pause_menu` (line 894) keeps its items in an enum, a parallel label array, and a `build` lambda that lists the visible ones. All three change together, plus the dispatch chain.

Add the include at the top of the file, beside the other page headers:

```cpp
#include "synth.h"
```

Change the enum and labels (line 896):

```cpp
    enum { PI_RESUME, PI_MAP, PI_DISPLAY, PI_GAMEPLAY, PI_SOUND, PI_CONTROLS, PI_RESTART, PI_N };
    static const char *const LABEL[PI_N] = {
        "Resume", "Map", "Display", "Gameplay", "Sound", "Controls", "Restart"
    };
```

Add the item to `build`, after the `PI_GAMEPLAY` line:

```cpp
        items[nitems++] = PI_SOUND;
```

Add the dispatch arm, between the `PI_GAMEPLAY` and `PI_CONTROLS` arms:

```cpp
            else if (item == PI_SOUND)    { page_fade_out(g_menu_page_fade); sound_options_page(); menu_clear(); need_fade_in = true; }
```

It takes the `menu_clear()` form rather than Gameplay's `build()` form: `build()` is there because Gameplay can change the difficulty and so change which items exist, and the Sound page changes no item's visibility.

`PI_N` grows on its own since it is the last enumerator, and `menu_box_fit("PAUSED", 18, nitems + 2, ...)` sizes from `nitems`, so the box follows. "Gameplay" is still the longest label at eight characters, so `label_w` does not move.

- [ ] **Step 9: Type-check and build**

```bash
cd saturn && sh syntax-check.sh src/menu/menu_pages.cxx src/net/netbin_pages.cxx
cd saturn && cmd //c compile-netbin.bat clean && cmd //c compile-netbin.bat
```

Expected: clean type-check, netbin builds.

- [ ] **Step 10: Commit**

```bash
git add saturn/src/menu/menu_layout.h saturn/src/menu/menu_layout.c \
        saturn/src/engine/app_state.h saturn/src/engine/app_state.cxx \
        saturn/src/menu/menu_pages.cxx saturn/src/net/netbin_pages.cxx \
        saturn/tests/test_sound_rows.c
git commit -m "Give the generated music its own level row on the Sound page, shown exactly where the CD Music row is not so the page never offers two music sliders, and open that page from the netbin's pause menu for the first time."
```

---

### Task 9: Persistence

**Files:**
- Create: `saturn/src/menu/options_blob.h`
- Create: `saturn/src/menu/options_blob.c`
- Modify: `saturn/src/menu/options.cxx:275-280` (decode) and its save function around line 338 (encode)
- Test: `saturn/tests/test_options_blob.c`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `#define OPTS_SOUND_BLOCK_BYTES 3`, `void opts_sound_block_encode(unsigned char *buf, int synth_level)`, `int opts_sound_block_decode(const unsigned char *buf, int *synth_level)` returning 1 when the new form was read, 0 otherwise.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_options_blob.c`:

```c
/* The sound block in the MOJOOPTS blob.

   The blob is strictly positional -- every field after this block is located
   by counting from it -- and it already carries a dead three-byte block:
   sentinel 1, then a mix mode and a track number, both settings long gone and
   kept only so the count still works. The synth level moves into those bytes
   under a new sentinel, which is why the width must not change and why a
   sentinel-1 blob must still be skipped exactly as before.

   Getting this wrong does not lose the music level. It silently misparses
   every block behind it -- the controller mapping, the gameplay block, the
   display block -- which is why the width is asserted as hard as the value.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/menu -o /tmp/t_blob \
         saturn/tests/test_options_blob.c saturn/src/menu/options_blob.c \
         && /tmp/t_blob
*/
#include "../src/menu/options_blob.h"
#include <stdio.h>
#include <assert.h>

static void test_round_trip(void) {
    unsigned char buf[8] = { 0 };
    int level = -1;
    opts_sound_block_encode(buf, 6);
    assert(opts_sound_block_decode(buf, &level) == 1);
    assert(level == 6);
}

static void test_block_is_exactly_three_bytes(void) {
    unsigned char buf[8];
    for (int i = 0; i < 8; i++) buf[i] = 0xAA;
    opts_sound_block_encode(buf, 3);
    assert(buf[OPTS_SOUND_BLOCK_BYTES] == 0xAA);
    assert(OPTS_SOUND_BLOCK_BYTES == 3);
}

static void test_legacy_sentinel_one_is_skipped_not_read(void) {
    /* An old blob's mix mode and track number must not be mistaken for a
       level. Decode reports "not mine" and leaves the caller's value alone. */
    unsigned char buf[3] = { 1, 2, 5 };
    int level = 4;
    assert(opts_sound_block_decode(buf, &level) == 0);
    assert(level == 4);
}

static void test_an_absent_block_leaves_the_default(void) {
    unsigned char buf[3] = { 0, 0, 0 };
    int level = 4;
    assert(opts_sound_block_decode(buf, &level) == 0);
    assert(level == 4);
}

static void test_out_of_range_level_is_rejected(void) {
    unsigned char buf[3] = { 10, 99, 0 };
    int level = 4;
    assert(opts_sound_block_decode(buf, &level) == 0);
    assert(level == 4);
}

static void test_every_valid_level_survives(void) {
    for (int l = 0; l <= 7; l++) {
        unsigned char buf[3] = { 0 };
        int got = -1;
        opts_sound_block_encode(buf, l);
        assert(opts_sound_block_decode(buf, &got) == 1);
        assert(got == l);
    }
}

static void test_encoded_sentinel_is_ten(void) {
    /* 10 is chosen because display sentinels use 1-4, 6, 8 and 9 and gameplay
       uses 5 and 7, so it can be mistaken for none of them. */
    unsigned char buf[3] = { 0 };
    opts_sound_block_encode(buf, 0);
    assert(buf[0] == 10);
}

int main(void) {
    test_round_trip();
    test_block_is_exactly_three_bytes();
    test_legacy_sentinel_one_is_skipped_not_read();
    test_an_absent_block_leaves_the_default();
    test_out_of_range_level_is_rejected();
    test_every_valid_level_survives();
    test_encoded_sentinel_is_ten();
    printf("test_options_blob: all passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -O2 -I saturn/src -I saturn/src/menu -o /tmp/t_blob \
    saturn/tests/test_options_blob.c saturn/src/menu/options_blob.c && /tmp/t_blob
```

Expected: FAIL — `options_blob.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/menu/options_blob.h`:

```c
/*----------------------
 | options_blob.h
 | Description: The sound block inside the MOJOOPTS save blob, split out here
 |   because options.cxx pulls in srl.hpp and so cannot be reached by the host
 |   tests -- and this is the one part of that file where a mistake corrupts
 |   every field behind it rather than just its own.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef OPTIONS_BLOB_H
#define OPTIONS_BLOB_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | OPTS_SOUND_BLOCK_BYTES
 | Description: The block's width, unchanged from the dead form it replaces.
 |   Every field after it in the blob is positioned by counting from here, so
 |   this number is not free to grow: reclaiming or adding a byte would
 |   silently misparse every blob already written.
 | Author: suinevere
 ----------------------*/
#define OPTS_SOUND_BLOCK_BYTES 3

/*----------------------
 | OPTS_SOUND_SENTINEL
 | Description: Marks the block as carrying a synth level. 10 because display
 |   sentinels run 1-4, 6, 8 and 9 and gameplay uses 5 and 7, so this value can
 |   be mistaken for neither. Sentinel 1 is the dead form -- a mix mode and a
 |   track number -- and is still recognised so an older save is skipped
 |   rather than misread.
 | Author: suinevere
 ----------------------*/
#define OPTS_SOUND_SENTINEL 10

/*----------------------
 | opts_sound_block_encode
 | Description: Writes the three-byte sound block.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: buf -- at least OPTS_SOUND_BLOCK_BYTES writable; synth_level -- 0..7
 | Returns: N/A
 ----------------------*/
void opts_sound_block_encode(unsigned char *buf, int synth_level);

/*----------------------
 | opts_sound_block_decode
 | Description: Reads the three-byte sound block. Leaves *synth_level untouched
 |   for the dead form, an absent block, or an out-of-range value, so a blob
 |   that never carried a level comes back at the compiled default rather than
 |   at whatever those bytes happened to hold.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: buf -- at least OPTS_SOUND_BLOCK_BYTES readable; synth_level -- out
 | Returns: 1 when a level was read, 0 otherwise
 ----------------------*/
int opts_sound_block_decode(const unsigned char *buf, int *synth_level);

#ifdef __cplusplus
}
#endif
#endif /* OPTIONS_BLOB_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/menu/options_blob.c`:

```c
/*----------------------
 | options_blob.c
 | Description: Implementation of the MOJOOPTS sound block.
 | Author: suinevere
 | Dependencies: options_blob.h
 ----------------------*/
#include "options_blob.h"

void opts_sound_block_encode(unsigned char *buf, int synth_level) {
    if (synth_level < 0) synth_level = 0;
    if (synth_level > 7) synth_level = 7;
    buf[0] = OPTS_SOUND_SENTINEL;
    buf[1] = (unsigned char) synth_level;
    buf[2] = 0;
}

int opts_sound_block_decode(const unsigned char *buf, int *synth_level) {
    if (buf[0] != OPTS_SOUND_SENTINEL) return 0;
    if (buf[1] > 7) return 0;
    *synth_level = (int) buf[1];
    return 1;
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
gcc -O2 -I saturn/src -I saturn/src/menu -o /tmp/t_blob \
    saturn/tests/test_options_blob.c saturn/src/menu/options_blob.c && /tmp/t_blob
```

Expected: `test_options_blob: all passed`

- [ ] **Step 6: Wire it into options.cxx**

Add `#include "options_blob.h"` (inside the existing `extern "C"` block if there is one, since it is a C header) and `#include "app_state.h"` is already present.

In `options_load`, replace the read-past of the sound block — the comment block and `int s = m + 1 + fa_stored + CA_N;` at line 275 — with:

```cpp
    /* The sound block used to carry a mix mode and a track number, both long
       gone. Its three bytes now carry the generated music's level under a new
       sentinel; sentinel 1 is still recognised and still skipped, so an older
       blob keeps the compiled default rather than reading a track number as a
       volume. The width is unchanged either way, which is what every block
       behind it depends on. */
    int s = m + 1 + fa_stored + CA_N;
    if (s + OPTS_SOUND_BLOCK_BYTES <= (int) sizeof(buf))
        opts_sound_block_decode(&buf[s], &g_synth_level);
```

In the save function, replace the line that writes the sound-block sentinel (`buf[n++] = 1;` around line 338) and its two following bytes with:

```cpp
    opts_sound_block_encode(&buf[n], g_synth_level);
    n += OPTS_SOUND_BLOCK_BYTES;
```

Check the two bytes that followed the old sentinel are no longer written separately — the encode writes all three.

- [ ] **Step 7: Type-check and build**

```bash
cd saturn && sh syntax-check.sh src/menu/options.cxx
cd saturn && cmd //c compile-netbin.bat clean && cmd //c compile-netbin.bat
```

Expected: clean type-check, netbin builds.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/menu/options_blob.h saturn/src/menu/options_blob.c \
        saturn/src/menu/options.cxx saturn/tests/test_options_blob.c
git commit -m "Persist the generated music's level in the three dead bytes the save blob still reserved for a mix mode and track number, under a sentinel that cannot be mistaken for the old form or for any block behind it."
```

---

### Task 10: Measure and verify

**Files:**
- Modify: `docs/superpowers/specs/2026-09-04-sh2-synth-music-design.md` (record measured size and hardware result)

**Interfaces:**
- Consumes: everything.
- Produces: no code.

- [ ] **Step 1: Run every host test**

```bash
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_scsp saturn/tests/test_scsp.c saturn/src/sound/scsp.c && /tmp/t_scsp
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_note saturn/tests/test_synth_note.c saturn/src/sound/synth.c saturn/src/sound/scsp.c saturn/src/sound/tracker.c saturn/src/sound/music_synth_data.c && /tmp/t_note
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_trk saturn/tests/test_tracker.c saturn/src/sound/tracker.c && /tmp/t_trk
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_song saturn/tests/test_synth_song.c saturn/src/sound/music_synth_data.c saturn/src/sound/tracker.c && /tmp/t_song
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_api saturn/tests/test_synth_api.c saturn/src/sound/synth.c saturn/src/sound/scsp.c saturn/src/sound/tracker.c saturn/src/sound/music_synth_data.c && /tmp/t_api
gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_gate saturn/tests/test_synth_gating.c saturn/src/sound/synth.c saturn/src/sound/scsp.c saturn/src/sound/tracker.c saturn/src/sound/music_synth_data.c && /tmp/t_gate
gcc -O2 -I saturn/src -I saturn/src/menu -I saturn/src/video -o /tmp/t_rows saturn/tests/test_sound_rows.c saturn/src/menu/menu_layout.c && /tmp/t_rows
gcc -O2 -I saturn/src -I saturn/src/menu -o /tmp/t_blob saturn/tests/test_options_blob.c saturn/src/menu/options_blob.c && /tmp/t_blob
```

Expected: eight `all passed` lines.

- [ ] **Step 2: Run the existing suites, to catch what this disturbed**

```bash
cd saturn && python -m pytest tests -q
```

Expected: the same pass/skip counts as before this work — the three skips are hardware RAM-budget checks and are expected. Any new failure is this plan's to fix, particularly in the five map host tests and anything touching options persistence.

- [ ] **Step 3: Measure the netbin**

```bash
cd saturn && cmd //c compile-netbin.bat clean && cmd //c compile-netbin.bat
ls -l saturn/BuildDrop/zaturn.netbin
```

Record the byte count. The baseline is 200,464 bytes and the spec's estimate is about 6 KB, so expect roughly 206,000. Investigate anything past 210,000 before proceeding — the likeliest cause is the pattern table being larger than planned.

- [ ] **Step 4: Verify on hardware**

Build the CD image and run it from an ODE on a disc with no CD-DA, which is the only path that exercises the synth without a NetLink session:

```bash
cd saturn && cmd //c compile.bat release
```

Confirm by ear: the loop plays, it loops cleanly rather than stopping after one pass, the Sound page shows a Music row and no CD Music row, moving that row changes the volume of the currently sounding note without restarting it, and setting it to 0 is silent.

- [ ] **Step 5: Confirm the two open items the spec flags**

- **Sound-RAM collision.** With the SGL driver loaded, confirm the splash jingle and any Blorb effects still sound correctly with the synth running. If either is damaged, the waveform area at `0x25A70000` overlaps the driver's map and the address must move.
- **The netbin's starting state.** If a NetLink session is available, confirm the netbin sounds. If it is not, record that this remains untested rather than treating the CD-build result as covering it.

- [ ] **Step 6: Record the results in the spec**

Add a short "Measured" section to `docs/superpowers/specs/2026-09-04-sh2-synth-music-design.md` giving the netbin's before and after byte counts, the hardware result, and the disposition of both open items. Move anything still unverified into the Risks section rather than deleting it.

- [ ] **Step 7: Commit**

```bash
git add docs/superpowers/specs/2026-09-04-sh2-synth-music-design.md
git commit -m "Record what the generated music actually cost the netbin and how it behaved on hardware, including which of the spec's open items the run closed and which it did not."
```
