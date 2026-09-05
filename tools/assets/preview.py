#!/usr/bin/env python3
"""Render a MIDI through the Saturn synth's model and write a WAV to listen to.

This is the fast half of the loop. It imports mid2pat and runs the SAME
conversion the build uses, then plays the resulting pattern data through a
software model of what the SCSP does with it -- the same waveform tables, the
same note-to-pitch maths, the same row timing, the same per-voice levels. So
what you hear is what the conversion will sound like, in about a second,
instead of building a disc image and booting an emulator.

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
    return [genwaves.build(k) for k in range(4)]


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
    noise_env = [0.0] * nvoices
    rnd = 12345

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
            if wave_of[ch] >= 4:
                noise_env[ch] = 1.0
            else:
                hz = BASE_HZ * (2.0 ** (semi / 12.0)) * (2.0 ** octv)
                step[ch] = 256.0 * hz / RATE
        for i in range(start, end):
            acc = 0.0
            for ch in range(nvoices):
                if amp[ch] <= 0.0:
                    continue
                if wave_of[ch] >= 4:
                    if noise_env[ch] > 0.0:
                        rnd = (rnd * 1103515245 + 12345) & 0x7FFFFFFF
                        acc += ((rnd >> 16) / 16384.0 - 1.0) * 100.0 * amp[ch] * noise_env[ch]
                        noise_env[ch] *= 0.9993
                else:
                    phase[ch] += step[ch]
                    if phase[ch] >= 256.0:
                        phase[ch] -= 256.0
                    acc += waves[wave_of[ch]][int(phase[ch])] * amp[ch]
            buf[i] = acc
        pos += samples_per_row
        if pos >= total:
            break
    return buf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("midi")
    ap.add_argument("wav")
    ap.add_argument("--grid", type=int, default=16)
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--no-drums", action="store_true")
    args = ap.parse_args()

    division, tempo, events = mid2pat.read_midi(args.midi)
    has_drums = (not args.no_drums
                 and any(e[3] == mid2pat.DRUM_MIDI_CHANNEL and e[2] for e in events))
    tonal_slots = mid2pat.CHANNELS - 1 if has_drums else mid2pat.CHANNELS
    ch_wave = mid2pat.CH_WAVE_DRUMS if has_drums else mid2pat.CH_WAVE_TONAL

    bpm = 60000000.0 / tempo
    frames = (60.0 / bpm) * (4.0 / args.grid) * 60.0
    speed = max(1, int(frames))
    frac = int(round((frames - speed) * 256))
    if frac > 255:
        speed, frac = speed + 1, 0

    tonal, hits = mid2pat.grid_rows(division, events, args.grid, has_drums)
    played = [p for r in tonal for p in r]
    lowest, highest = min(played), max(played)
    shift = 0
    while (lowest + shift - mid2pat.BASE_MIDI) + 24 < mid2pat.MIN_INDEX:
        shift += 12
    while (highest + shift - mid2pat.BASE_MIDI) + 24 > mid2pat.MAX_INDEX:
        shift -= 12

    cells = []
    previous = [None] * mid2pat.CHANNELS
    for pitches, hit in zip(tonal, hits):
        picked = mid2pat.assign(pitches, tonal_slots)
        row = []
        for ch in range(tonal_slots):
            pitch = picked[ch]
            if pitch is None:
                row.append((1, 0) if previous[ch] is not None else (0, 0))
            elif pitch != previous[ch]:
                idx = (pitch + shift - mid2pat.BASE_MIDI) + 24
                idx = max(mid2pat.MIN_INDEX, min(mid2pat.MAX_INDEX, idx))
                row.append((idx + 2, (ch_wave[ch] << 4) | mid2pat.CH_VOL[ch]))
            else:
                row.append((0, 0))
            previous[ch] = pitch
        if has_drums:
            last = mid2pat.CHANNELS - 1
            row.append((mid2pat.DRUM_NOTE,
                        (ch_wave[last] << 4) | mid2pat.CH_VOL[last]) if hit else (0, 0))
        cells.append(row)

    buf = render(cells, speed, frac, ch_wave, args.seconds)
    peak = max(1.0, max(abs(x) for x in buf))
    with wave.open(args.wav, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(x / peak * 26000)) for x in buf))

    print("%.1f BPM, speed %d+%d/256, %s, transpose %+d"
          % (bpm, speed, frac, "drums on noise" if has_drums else "no drums", shift))
    print("wrote %s (%.1f s)" % (args.wav, len(buf) / float(RATE)))


if __name__ == "__main__":
    main()
