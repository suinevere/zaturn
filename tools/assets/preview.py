#!/usr/bin/env python3
"""Render a MIDI through the Saturn synth's model and write a WAV to listen to.

This is the fast half of the loop. It calls mid2pat.convert, which is literally
the function the build emits its C from, then plays the resulting pattern data
through a software model of what the SCSP does with it -- the same waveform
tables, the same note-to-pitch maths, the same row timing, the same per-voice
levels. So what you hear is what the conversion will sound like, in about a
second, instead of building a disc image and booting an emulator.

It is a model, not the chip. It does not reproduce the SCSP's envelope rates,
its interpolation, or its output filtering, so treat it as a preview for
choosing tunes and settings -- and confirm the real thing on hardware or in
Mednafen before shipping. Things it will not warn you about: aliasing at
extreme pitches, and anything to do with voice stealing.

Usage:
  tools/assets/preview.bat IN.mid OUT.wav [--grid 16] [--seconds 30]   (Windows)
  tools/assets/preview.sh  IN.mid OUT.wav                              (sh)

Both wrappers resolve their own location, so they run from any directory.
"""
import argparse, math, pathlib, struct, sys, wave

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import mid2pat
import genwaves

RATE = 44100
# The engine's own base: a 256-sample table clocked at the SCSP's 44100 Hz.
BASE_HZ = RATE / float(mid2pat.CHANNELS and 256)


def voice_tables():
    """The same tables the build uploads: four tonal, then the percussion one.

    The percussion table is a different length from the others, so a voice's
    phase wraps at its own table's end rather than at a shared constant.
    """
    return [genwaves.build(k) for k in range(4)] + [genwaves.build_noise()]


def render(cells, speed, frac, ch_wave, seconds):
    waves = voice_tables()
    frames_per_row = speed + frac / 256.0
    samples_per_row = RATE * frames_per_row / 60.0
    total = int(min(len(cells) * samples_per_row, seconds * RATE))
    buf = [0.0] * total

    nvoices = mid2pat.CHANNELS
    phase = [0.0] * nvoices
    step = [0.0] * nvoices
    amp = [0.0] * nvoices
    wave_of = [0] * nvoices
    env = [1.0] * nvoices
    # Where the next percussion hit starts, as scsp.c rotates it: the chip
    # restarts a slot from its start address every key-on, so a fixed one would
    # replay the same bytes on every strike.
    noise_start = 0

    pos = 0.0
    for row in cells:
        start = int(pos)
        end = int(min(pos + samples_per_row, total))
        for ch, (note, wv) in enumerate(row):
            if note == 0:
                continue
            if note == 1:
                amp[ch] = 0.0
                continue
            index = note - 2
            semi, octv = index % 12, index // 12 - 2
            wave_of[ch] = wv >> 4
            vol = wv & 0x0F
            # DISDL is three bits in roughly 6 dB steps; 7 is full output.
            amp[ch] = 0.0 if vol == 0 else 2.0 ** (min(7, vol) - 7)
            # Table samples per output sample. The engine's OCT 0 semitone 0 is
            # one table sample per output sample whatever the table's length.
            step[ch] = 2.0 ** (semi / 12.0 + octv)
            env[ch] = 1.0
            if wave_of[ch] == mid2pat.WAVE_NOISE:
                phase[ch] = float(noise_start)
                noise_start += genwaves.NOISE_STRIDE
                if noise_start + genwaves.NOISE_RUN > genwaves.NOISE_LEN:
                    noise_start = 0
            else:
                phase[ch] = 0.0
        for i in range(start, end):
            acc = 0.0
            for ch in range(nvoices):
                if amp[ch] <= 0.0 or env[ch] <= 0.0005:
                    continue
                table = waves[wave_of[ch]]
                phase[ch] += step[ch]
                if phase[ch] >= len(table):
                    phase[ch] -= len(table)
                acc += table[int(phase[ch])] * amp[ch] * env[ch]
                # The percussion voice decays by itself: the pattern data never
                # keys it off, so the chip's envelope has to end the hit.
                if wave_of[ch] == mid2pat.WAVE_NOISE:
                    # Half-life about 4 ms, measured off the chip. The 22 ms this
                    # used to be barely showed while the drum was quiet and the
                    # drum was not sounding on hardware at all; at full output it
                    # made the model's percussion five times longer than the
                    # machine's and swamped everything else in it.
                    env[ch] *= 0.9961
            buf[i] = acc
        pos += samples_per_row
        if pos >= total:
            break
    return buf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("midi")
    ap.add_argument("wav")
    ap.add_argument("--seconds", type=float, default=30.0)
    mid2pat.add_shared_arguments(ap)
    args = ap.parse_args()

    song = mid2pat.convert(args.midi, args.grid, 0, args.max_rows,
                           args.no_drums, args.bpm, args.fold_octaves)

    buf = render(song["cells"], song["speed"], song["frac"],
                 song["ch_wave"], args.seconds)
    peak = max(1.0, max(abs(x) for x in buf))
    with wave.open(args.wav, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(x / peak * 26000)) for x in buf))

    print("%.1f BPM, speed %d+%d/256, %s, transpose %+d"
          % (song["bpm"], song["speed"], song["frac"],
             "drums on noise" if song["has_drums"] else "no drums", song["shift"]))
    print("wrote %s (%.1f s)" % (args.wav, len(buf) / float(RATE)))


if __name__ == "__main__":
    main()
