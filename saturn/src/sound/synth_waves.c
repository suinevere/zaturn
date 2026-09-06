/*----------------------
 | synth_waves.c
 | Description: Builds the synth's four waveform tables and its percussion noise
 |   table at boot, into RAM, instead of carrying them as linked constants.
 |
 |   They used to be 5,120 bytes of .rodata generated offline by
 |   tools/assets/genwaves.py, and that file's own header explains why offline:
 |   the alternative is "calling sin() thousands of times at boot through
 |   soft-float libm". That is true, and it is true of the `smooth` voice, which
 |   sums band-limited harmonics. It is not true of the voice actually shipped.
 |   The `nes` voice is three pulses (a comparison against a duty point), the
 |   2A03's sixteen-level staircase triangle (a table of integers and one
 |   division), and the 2A03's fifteen-bit noise register (a shift and an xor).
 |   There is no floating point anywhere in it and nothing here calls libm.
 |
 |   What that buys is netbin bytes. .bss costs the netbin image nothing -- the
 |   loader is bounded by file size, and PlanetWeb 4.0 refuses an oversized one
 |   without saying why -- so moving 5,120 bytes from .rodata into a table filled
 |   at boot is 5,120 bytes off an image that was 3,088 over its own headroom
 |   floor. On the CD build it is neutral: __heap_start follows .bss, so the two
 |   sections cost the story heap the same.
 |
 |   Kept free of includes on purpose, so saturn/tests/test_synth_waves.py can
 |   compile this file with a host compiler and diff what it produces against
 |   what genwaves.py produces. That check is the whole safety of the change:
 |   the SCSP is uploaded the same bytes it was before or the test fails.
 | Author: suinevere
 | Dependencies: none (deliberately -- see above)
 ----------------------*/

/*----------------------
 | WAVE_LEN / WAVE_AMP / TRI_STEPS / TRI_LEVELS
 | Description: One tonal table's length, its amplitude, and the NES triangle's
 |   step and level counts -- thirty-two steps quantised to sixteen levels, which
 |   is what the 2A03's sequencer walks. genwaves.py's LEN, AMP and the 32 of
 |   `(i * 32) // LEN`; saturn/tests/test_synth_waves.py fails if they drift.
 | Author: suinevere
 ----------------------*/
#define WAVE_LEN    256
#define WAVE_AMP    100
#define TRI_STEPS   32
#define TRI_LEVELS  16

/*----------------------
 | NOISE_LEN / NOISE_OVERSAMPLE / NOISE_WARMUP / NOISE_AMP
 | Description: The percussion table's length, how many table samples one shift
 |   register bit fills, how far the register is run before anything is recorded,
 |   and the amplitude it is written at. Every one of them is a calibration, not
 |   a convenience: the warm-up is there because the register seeded with 1 is
 |   heavily biased for its first few hundred outputs and every drum hit carried
 |   an audible pitch, and the amplitude was swept on the chip against a
 |   recording of the NES original. genwaves.py holds the same four numbers and
 |   the reasoning behind them at length.
 | Author: suinevere
 ----------------------*/
#define NOISE_LEN         4096
#define NOISE_OVERSAMPLE  2
#define NOISE_WARMUP      4096
#define NOISE_AMP         56

/*----------------------
 | SYNTH_WAVE_TABLE / SYNTH_NOISE_TABLE
 | Description: The four tonal tables and the percussion table, filled by
 |   synth_waves_build. Not const any more, because they are written once at
 |   boot rather than linked; synth.c's externs match.
 | Author: suinevere
 ----------------------*/
signed char SYNTH_WAVE_TABLE[4][WAVE_LEN];
signed char SYNTH_NOISE_TABLE[NOISE_LEN];

/*----------------------
 | round_div
 | Description: v/d rounded to nearest, away from zero on a tie. Written out
 |   rather than left to integer division because C truncates toward zero and
 |   the triangle's lower half is negative, where truncation and rounding differ
 |   by one -- which is audible as a lopsided waveform and invisible in the code.
 |
 |   Python's round() breaks ties to even rather than away from zero, so the two
 |   would disagree on a tie. The triangle never produces one: every value is
 |   some multiple of a fifteenth, and a fifteenth is never a half.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: v -- the numerator; d -- the divisor, positive
 | Returns: v/d to the nearest integer
 ----------------------*/
static int round_div(int v, int d) {
    if (v >= 0) return (2 * v + d) / (2 * d);
    return -((-2 * v + d) / (2 * d));
}

/*----------------------
 | synth_waves_build
 | Description: Fills both tables. Idempotent and cheap -- about six thousand
 |   integer operations -- so synth_init calls it every time rather than guarding
 |   it with a flag that a soft reset would have to remember to clear.
 |
 |   The three pulses are a comparison against a duty point, expressed as
 |   `i * denominator < WAVE_LEN` so the boundary is exact rather than a float
 |   comparison that happens to land right. The triangle walks the 2A03's own
 |   sequencer table -- fifteen down to zero, then zero back up to fifteen, held
 |   for WAVE_LEN/TRI_STEPS samples each -- and that quantised staircase is the
 |   point of it: a clean triangle does not sound like an NES.
 |
 |   The noise register is fifteen bits, its new bit the xor of bits 0 and 1, and
 |   it is run NOISE_WARMUP times before the first sample is kept.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: SYNTH_WAVE_TABLE, SYNTH_NOISE_TABLE
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_waves_build(void) {
    static const int DUTY_DEN[4] = { 8, 4, 0, 2 };
    int w, i, n, reg, fb, level;

    for (w = 0; w < 4; w++) {
        if (w == 2) continue;
        for (i = 0; i < WAVE_LEN; i++)
            SYNTH_WAVE_TABLE[w][i] =
                (signed char) ((i * DUTY_DEN[w] < WAVE_LEN) ? WAVE_AMP : -WAVE_AMP);
    }

    for (i = 0; i < WAVE_LEN; i++) {
        int step = (i * TRI_STEPS) / WAVE_LEN;
        level = (step < TRI_LEVELS) ? (TRI_LEVELS - 1 - step) : (step - TRI_LEVELS);
        SYNTH_WAVE_TABLE[2][i] =
            (signed char) round_div((2 * level - (TRI_LEVELS - 1)) * WAVE_AMP,
                                    TRI_LEVELS - 1);
    }

    reg = 1;
    for (i = 0; i < NOISE_WARMUP; i++) {
        fb = (reg ^ (reg >> 1)) & 1;
        reg = (reg >> 1) | (fb << 14);
    }
    n = 0;
    while (n < NOISE_LEN) {
        signed char v;
        fb = (reg ^ (reg >> 1)) & 1;
        reg = (reg >> 1) | (fb << 14);
        v = (signed char) ((reg & 1) ? -NOISE_AMP : NOISE_AMP);
        for (i = 0; i < NOISE_OVERSAMPLE && n < NOISE_LEN; i++)
            SYNTH_NOISE_TABLE[n++] = v;
    }
}
