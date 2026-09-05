#!/usr/bin/env python3
"""Generate the synth's waveform tables as C data.

Two voice sets, chosen with --voice:

  nes     What the Ricoh 2A03 in a Nintendo Entertainment System produces:
          pulse waves at 12.5%, 25% and 50% duty, and the NES's own triangle,
          which is a staircase of 32 steps quantised to 16 levels. That
          staircase is the point -- it is why NES bass sounds hollow and gritty
          instead of smooth, and a mathematically clean triangle does not sound
          like an NES at all. These are hard-edged on purpose.

  smooth  Band-limited waves summed from a bounded number of harmonics. A hard
          edge has infinitely many harmonics and every one above half the
          playback rate folds back as an inharmonic whine, so these trade
          authenticity for the absence of that buzz.

Built here rather than on the Saturn because the alternative is calling sin()
thousands of times at boot through soft-float libm: several kilobytes of maths
code linked in, and most of a second of startup, to produce one kilobyte of
constants that never change.
"""
import argparse, math, pathlib

LEN = 256
HARMONICS = 16
AMP = 100.0

# The percussion table is longer than the tonal ones and is not a cycle of
# anything: it is a slice of the 2A03's 15-bit noise shift register, played back
# slowly enough that one pass outlasts the gap between two drum hits, so a hit
# never hears the table come round again. Two table samples per shift-register
# bit, which is what puts the useful clock rates inside the octave range a note
# byte can name -- the tracker's note encodes octaves from -2 upward, and one
# bit per sample would need -3.
NOISE_LEN = 4096
NOISE_OVERSAMPLE = 2
# The shift register is seeded with 1, which is a corner of its state space, and
# its first few hundred outputs are heavily biased: the first 623 samples -- one
# drum hit's worth -- averaged +39.6 of a possible 100, so every hit carried a
# thump with a definite pitch rather than a noise burst. On the machine the
# register free-runs from reset and is long past that by the time a note plays.
# Running it forward this far before recording anything reproduces that: the
# worst local mean over any hit's worth of the table falls from 39.7 to 5.8.
NOISE_WARMUP = 4096
# How much of the table one hit can read, and how far the start address moves
# between hits. scsp.c keeps the same two numbers; saturn/tests/test_noise_table.py
# fails if they drift apart.
NOISE_RUN = 1024
NOISE_STRIDE = 293
# The percussion's fine level. DISDL's steps are 6 dB and the drum sits between
# two of them, so something finer is needed; the chip's own TL attenuator is not
# it -- see the note above SCSP_EG_PERC_D1R in scsp.h -- and this is, because it
# is exact by construction and is also what the offline preview model plays.
# Of a possible 127, and swept on the chip against a recording of the NES
# original: with the drum on DISDL 6, amplitudes 100 / 80 / 64 / 55 give
# whole-mix band errors of 0.235 / 0.112 / 0.099 / 0.080, and again at 56 once
# beat accent below took energy out of it -- 55 / 65 / 75 then give 0.123 /
# 0.086 / 0.084, and 65 is the one that puts the 4-8 kHz band on 3.9 per cent
# against the original 3.7 rather than 75's 4.6 -- and 56 after the fast runs
# stopped taking the beat accent, which put eighty-eight strikes back up 6 dB
# and left the 4-8 kHz band at 4.4. It reads 3.7 now, the original exactly. Sanity check on the
# calibration: amplitude 50 on DISDL 6 reproduces amplitude 100 on DISDL 5
# digit for digit, which is the 6 dB step it should be.
NOISE_AMP = 56.0

NES_NAMES = ["pulse 12.5%", "pulse 25%", "NES triangle (4-bit staircase)", "pulse 50%"]
SMOOTH_NAMES = ["square", "pulse 25%", "triangle", "saw"]


def build_nes(kind):
    """The 2A03's own shapes. Hard edges and a quantised triangle, deliberately."""
    if kind == 2:
        # 32 steps, 16 levels: 15..0 then 0..15, exactly as the NES sequencer
        # walks its table. Held for LEN/32 samples each so the staircase is
        # audible rather than smoothed away by the table length.
        steps = list(range(15, -1, -1)) + list(range(0, 16))
        out = []
        for i in range(LEN):
            level = steps[(i * 32) // LEN]
            out.append(int(round((level - 7.5) / 7.5 * AMP)))
        return out
    duty = {0: 0.125, 1: 0.25, 3: 0.5}[kind]
    return [int(AMP) if (i / float(LEN)) < duty else -int(AMP) for i in range(LEN)]


def build_noise():
    """The 2A03's noise channel: a 15-bit LFSR, new bit from bits 0 and 1.

    This is the whole reason the table exists. The SCSP has its own noise
    generator and no filter of any kind -- the slot registers stop at 0x16 --
    so the only way to choose how bright the percussion is, which is what the
    NES does with its sixteen clock periods, is to hold the sequence as a
    waveform and pick the rate by the note it is keyed at. Measured against a
    recording of the NES original, the chip's own generator peaks at 4 kHz and
    is still bright at 15 kHz where the NES peaks at 2 kHz and rolls off.
    """
    reg = 1
    for _ in range(NOISE_WARMUP):
        feedback = (reg ^ (reg >> 1)) & 1
        reg = (reg >> 1) | (feedback << 14)
    amp = int(round(NOISE_AMP))
    out = []
    for _ in range(NOISE_LEN // NOISE_OVERSAMPLE):
        feedback = (reg ^ (reg >> 1)) & 1
        reg = (reg >> 1) | (feedback << 14)
        out.extend([amp if not reg & 1 else -amp] * NOISE_OVERSAMPLE)
    return out


def build_smooth(kind):
    """Additive and band-limited: no harmonic above the cutoff, so no fold-back."""
    raw = []
    for i in range(LEN):
        phase = 2.0 * math.pi * i / LEN
        v = 0.0
        for h in range(1, HARMONICS + 1):
            if kind == 0:
                if h % 2 == 0:
                    continue
                amp = 1.0 / h
            elif kind == 1:
                amp = math.sin(h * math.pi * 0.25) / h
            elif kind == 2:
                if h % 2 == 0:
                    continue
                amp = (1.0 if h % 4 == 1 else -1.0) / float(h * h)
            else:
                amp = (1.0 if h % 2 else -1.0) / h
            v += amp * math.sin(h * phase)
        raw.append(v)
    peak = max(abs(x) for x in raw)
    return [max(-127, min(127, int(round(x * AMP / peak)))) for x in raw]


def build(kind, voice="nes"):
    return build_nes(kind) if voice == "nes" else build_smooth(kind)


def main():
    global NOISE_AMP, NOISE_OVERSAMPLE
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default="saturn/src/sound/synth_waves.c")
    ap.add_argument("--voice", choices=("nes", "smooth"), default="nes")
    ap.add_argument("--noise-oversample", type=int, default=NOISE_OVERSAMPLE,
                    help="table samples per shift-register bit; halving it "
                         "doubles the bit rate and so the noise bandwidth")
    ap.add_argument("--noise-amp", type=float, default=NOISE_AMP,
                    help="percussion table amplitude, 0-127; the drum's fine "
                         "level, because the chip's own attenuator is coarse and clamps")
    args = ap.parse_args()
    NOISE_AMP = args.noise_amp
    NOISE_OVERSAMPLE = args.noise_oversample

    names = NES_NAMES if args.voice == "nes" else SMOOTH_NAMES
    blocks = []
    for k in range(4):
        table = build(k, args.voice)
        rows = ["    " + " ".join("%4d," % v for v in table[i:i + 16])
                for i in range(0, LEN, 16)]
        blocks.append("  { /* %s */\n%s\n  }" % (names[k], "\n".join(rows)))

    note = ("the 2A03's own shapes: hard-edged pulses and the NES's 32-step,\n"
            " |   16-level triangle staircase, which is what gives NES bass its\n"
            " |   hollow grit. Aliasing above the table's pitch is part of that sound."
            if args.voice == "nes" else
            "band-limited, summed from %d harmonics so notes played well\n"
            " |   above the table's own pitch do not alias into a buzz." % HARMONICS)

    noise = build_noise()
    noise_rows = ["    " + " ".join("%4d," % v for v in noise[i:i + 16])
                  for i in range(0, NOISE_LEN, 16)]

    pathlib.Path(args.out).write_text('''/*----------------------
 | synth_waves.c
 | Description: The four waveform tables, generated by tools/assets/genwaves.py
 |   --voice %(voice)s -- do not hand-edit. %(len)d samples each. These are
 |   %(note)s
 |   Generated rather than computed at boot because the alternative links
 |   soft-float libm for constants that never change.
 | Author: suinevere
 ----------------------*/

/*----------------------
 | SYNTH_WAVE_TABLE
 | Description: %(names)s, in the order the SYNTH_WAVE_* constants name them.
 | Author: suinevere
 ----------------------*/
const signed char SYNTH_WAVE_TABLE[4][%(len)d] = {
%(body)s
};

/*----------------------
 | SYNTH_NOISE_TABLE
 | Description: %(nlen)d samples of the 2A03's 15-bit noise shift register, two
 |   samples per bit. Not one cycle of anything: it loops, but one pass at the
 |   rate a drum is keyed at outlasts the gap between two hits, so the repeat is
 |   never heard. Held as data rather than made by the chip because the SCSP's
 |   own noise generator has one setting and the chip has no filter to darken it
 |   with -- its slot registers stop at 0x16. Keying this at a lower note is
 |   what the NES does with its sixteen shift-register clock periods. Written at
 |   amplitude %(amp)g of a possible 127, which is also the drum's fine level --
 |   the chip's own attenuator is too coarse in one direction and too clamped in
 |   the other, so the trim lives in the data.
 | Author: suinevere
 ----------------------*/
const signed char SYNTH_NOISE_TABLE[%(nlen)d] = {
%(noise)s
};
''' % {"voice": args.voice, "len": LEN, "note": note, "nlen": NOISE_LEN,
       "names": ", ".join(names), "body": ",\n".join(blocks),
       "noise": "\n".join(noise_rows), "amp": NOISE_AMP}, encoding="utf-8")
    print("wrote %s (%s voices, %d bytes of tonal table, %d of noise at amplitude %g)"
          % (args.out, args.voice, 4 * LEN, NOISE_LEN, NOISE_AMP))


if __name__ == "__main__":
    main()
