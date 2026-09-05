#!/usr/bin/env python3
"""/*----------------------
 | gen_track_mood.py
 | Description: GENERATES tools/assets/track_mood.json -- what each of the
 |     disc's CD-DA tracks actually sounds like, measured off the ripped audio
 |     rather than described from memory.
 |
 |     All thirty-one are ambient, which is exactly why prose about them is
 |     worthless: "brooding" and "eerie" fit half the disc. Numbers do not.
 |     Every track is read as the raw 44.1 kHz 16-bit stereo PCM a CD-DA track
 |     is, and reduced to eight measurements -- loudness, brightness, where the
 |     energy sits low and high, how tonal against how noisy, how much the
 |     spectrum moves, how often it moves sharply, and how wide the stereo
 |     image is. The mood words this emits are then thresholds on those
 |     numbers, ranked WITHIN the disc rather than against absolute values,
 |     because "dark" only means anything next to the rest of the same score.
 |
 |     The rip is not in the repository and never will be -- it is 250 MB of
 |     Activision's audio. This reads whatever the caller points it at and
 |     commits only the derived numbers, which is the same bargain
 |     gen_map_atlas.py makes with Infocom's maps.
 | Author: suinevere
 | Dependencies: argparse, json, numpy, pathlib, re, sys
 | Globals: ROOT, OUT, RATE, WINDOW, HOP
 | Run: python tools/gen_track_mood.py --dir "cd/Zork I - ... (Japan)"
 ----------------------*/"""
import argparse
import json
import pathlib
import re
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "tools" / "assets" / "track_mood.json"

RATE = 44100
WINDOW = 4096
HOP = 2048


def read_pcm(path):
    """/*----------------------
     | read_pcm
     | Description: One CD-DA track as (mono, left, right) float arrays. A
     |     ripped audio track holds no headers and no sector framing -- the
     |     2352 bytes of a CD-DA sector are 588 stereo frames and nothing else
     |     -- so the file is read as int16 pairs directly. An odd trailing byte
     |     is dropped rather than trusted.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: N/A
     | Params: path -- the .bin track
     | Returns: (mono, left, right) as float32 in -1..1
     ----------------------*/"""
    raw = np.fromfile(path, dtype="<i2")
    if raw.size % 2:
        raw = raw[:-1]
    st = raw.reshape(-1, 2).astype(np.float32) / 32768.0
    left, right = st[:, 0], st[:, 1]
    return (left + right) * 0.5, left, right


def spectra(mono):
    """/*----------------------
     | spectra
     | Description: The magnitude spectrogram, Hann-windowed. Frames whose
     |     energy is below a floor are dropped before any average is taken:
     |     several of these tracks open and close on digital silence, and a
     |     silent frame has no brightness to contribute but would drag every
     |     mean toward zero and make a track read darker the longer its fade.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: WINDOW, HOP, RATE
     | Params: mono -- the mono signal
     | Returns: (magnitudes, freqs, frame_rms) with silence already dropped
     ----------------------*/"""
    n = 1 + max(0, (len(mono) - WINDOW) // HOP)
    idx = np.arange(WINDOW)[None, :] + HOP * np.arange(n)[:, None]
    frames = mono[idx] * np.hanning(WINDOW).astype(np.float32)
    rms = np.sqrt(np.mean(frames ** 2, axis=1) + 1e-20)
    keep = rms > (rms.max() * 0.02)
    frames, rms = frames[keep], rms[keep]
    mag = np.abs(np.fft.rfft(frames, axis=1)).astype(np.float32)
    return mag, np.fft.rfftfreq(WINDOW, 1.0 / RATE), rms


def measure(path):
    """/*----------------------
     | measure
     | Description: One track's eight numbers. Brightness is the spectral
     |     centroid, weight and air are the share of energy below 250 Hz and
     |     above 4 kHz, tonality is one minus spectral flatness, motion is the
     |     mean positive frame-to-frame spectral change, and pulse counts how
     |     often that change spikes -- which is what separates a drone from
     |     something with events in it, and is the one feature that tells these
     |     ambient pieces apart at all.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: RATE, HOP
     | Params: path -- the .bin track
     | Returns: a dict of measurements
     ----------------------*/"""
    return measure_signal(*read_pcm(path))


def measure_signal(mono, left, right):
    """/*----------------------
     | measure_signal
     | Description: measure's body, taking the signal rather than a path, so
     |     something that is not a ripped track can be put on the same scale --
     |     tools/gen_synth_moods.py renders each of the synth's own tunes and
     |     measures it here. Splitting it out rather than writing a second
     |     measurement is the point: two implementations of "how bright is this"
     |     would drift, and the whole use of these numbers is comparing one
     |     recording against another.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: RATE, HOP
     | Params: mono, left, right -- float arrays in -1..1; a mono source passes
     |     the same array three times, which reads a width of 0
     | Returns: a dict of measurements, or None if the signal is all silence
     ----------------------*/"""
    mag, freqs, rms = spectra(mono)
    if not len(mag):
        return None
    energy = mag.sum(axis=1) + 1e-20

    centroid = float(np.median((mag * freqs).sum(axis=1) / energy))
    csum = np.cumsum(mag, axis=1)
    roll = freqs[np.argmax(csum >= (0.85 * csum[:, -1:]), axis=1)]
    low = float(np.median(mag[:, freqs < 250].sum(axis=1) / energy))
    air = float(np.median(mag[:, freqs > 4000].sum(axis=1) / energy))
    flat = float(np.median(np.exp(np.mean(np.log(mag + 1e-9), axis=1)) /
                           (np.mean(mag, axis=1) + 1e-9)))

    d = np.diff(mag, axis=0)
    flux = np.sqrt((np.maximum(d, 0) ** 2).sum(axis=1))
    med = float(np.median(flux))
    mad = float(np.median(np.abs(flux - med))) or 1e-9
    spikes = int(np.count_nonzero(flux > med + 4.0 * mad))
    minutes = (len(mono) / RATE) / 60.0

    side = float(np.sqrt(np.mean((left - right) ** 2)))
    mid = float(np.sqrt(np.mean((left + right) ** 2))) + 1e-9

    return {
        "seconds": round(len(mono) / RATE, 1),
        "loudness_db": round(float(20 * np.log10(np.median(rms) + 1e-9)), 1),
        "brightness_hz": round(centroid),
        "rolloff_hz": round(float(np.median(roll))),
        "weight": round(low, 3),
        "air": round(air, 3),
        "tonality": round(1.0 - flat, 3),
        "motion": round(float(np.median(flux) / (float(np.median(energy)) + 1e-9)), 3),
        "pulse_per_min": round(spikes / minutes, 1) if minutes else 0.0,
        "width": round(side / mid, 3),
    }


def moods(rows):
    """/*----------------------
     | moods
     | Description: Two or three words per track, from where it ranks against
     |     the rest of the disc. Ranked, never thresholded absolutely: every
     |     one of these is a quiet ambient loop mastered in 1996 by the same
     |     hands, so an absolute cutoff for "bright" would put all of them on
     |     one side of it and describe nothing.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: N/A
     | Params: rows -- track number -> measurements
     | Returns: track number -> a list of mood words
     ----------------------*/"""
    def rank(field):
        vals = sorted(r[field] for r in rows.values())
        return {t: vals.index(r[field]) / max(1, len(vals) - 1)
                for t, r in rows.items()}

    bright, weight, air = rank("brightness_hz"), rank("weight"), rank("air")
    tonal, pulse, width = rank("tonality"), rank("pulse_per_min"), rank("width")

    out = {}
    for t in rows:
        w = []
        w.append("dark" if bright[t] < 0.34 else "bright" if bright[t] > 0.66 else "even")
        if weight[t] > 0.7:
            w.append("heavy")
        elif weight[t] < 0.3:
            w.append("thin")
        if air[t] > 0.7:
            w.append("airy")
        w.append("still" if pulse[t] < 0.34 else "restless" if pulse[t] > 0.66 else "drifting")
        if tonal[t] > 0.75:
            w.append("melodic")
        elif tonal[t] < 0.25:
            w.append("noisy")
        if width[t] > 0.8:
            w.append("wide")
        out[t] = w
    return out


def main(argv):
    """/*----------------------
     | main
     | Description: Measures every track in the rip and writes the catalogue.
     | Author: suinevere
     | Dependencies: argparse, json, re
     | Globals: OUT
     | Params: argv -- command-line arguments
     | Returns: 0, or 2 when the rip is not where it was said to be
     |
     | Note: track 1 is skipped -- on this disc it is the Mode-1 data track,
     |   and read as PCM it measures as the brightest, noisiest thing on the
     |   disc, which would drag every rank that follows.
     ----------------------*/"""
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True,
                    help="the ripped disc directory holding (Track NN).bin files")
    ap.add_argument("--out", default=str(OUT))
    args = ap.parse_args(argv)

    src = pathlib.Path(args.dir)
    if not src.is_dir():
        print(f"gen_track_mood: {src} is not a directory", file=sys.stderr)
        return 2

    found = {}
    for p in src.glob("*.bin"):
        m = re.search(r"\(Track (\d+)\)", p.name)
        if m and int(m.group(1)) >= 2:
            found[int(m.group(1))] = p
    if not found:
        print(f"gen_track_mood: no (Track NN).bin files under {src}", file=sys.stderr)
        return 2

    rows = {}
    for t in sorted(found):
        r = measure(found[t])
        if r is None:
            print(f"  track {t}: silent throughout, skipped")
            continue
        rows[t] = r
        print(f"  track {t:2d}  {r['seconds']:6.1f}s  {r['brightness_hz']:5d} Hz  "
              f"weight {r['weight']:.3f}  pulse {r['pulse_per_min']:5.1f}/min")

    word = moods(rows)
    out = {
        "_comment": "GENERATED by tools/gen_track_mood.py from the ripped CD-DA. "
                    "mood words are ranks within this disc, not absolute values.",
        "tracks": {str(t): dict(rows[t], mood=word[t]) for t in sorted(rows)},
    }
    pathlib.Path(args.out).write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
    print(f"Wrote {args.out}: {len(rows)} tracks")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
