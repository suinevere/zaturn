#!/usr/bin/env python3
"""/*----------------------
 | voicecmp.py
 | Description: Scores every tune in the catalogue against a rip of the NES
 |     original of the same piece, band by band, so the voicing can be argued
 |     about with numbers.
 |
 |     Every voicing constant this project ships was swept against ONE
 |     recording -- the entryway theme. CH_WAVE_TONAL, CH_VOL,
 |     DRUM_UNACCENTED_DROP, DRUM_DARK_SEMITONES, NOISE_AMP, DRUM_NOTE: all of
 |     them measured, all of them measured against the same thirty seconds of
 |     one tune. That is the shape of an answer that fits its evidence and
 |     nothing else, and there was no way to find out until the rest of the
 |     soundtrack was to hand. There is now: nes_refs.json says which rip each
 |     tune is a sequence of, and this renders each one through the offline
 |     model and compares the two as a share of energy per octave.
 |
 |     Shares and not levels, because a rip and a render agree about nothing
 |     absolute -- different mastering, different encoder, different chip. The
 |     bands run from 20 Hz to Nyquist with no gaps, deliberately: a band set
 |     that starts at 55 Hz lets a lane be "fixed" by pushing it below the
 |     lowest band, which is what the first sweep here reported before the
 |     hole was closed.
 |
 |     It is the offline model and not the chip, so it says nothing about
 |     envelopes, interpolation or voice stealing -- see the header of
 |     preview.py. What it does say is which tunes disagree with their own
 |     original and by how much, which is the list to work down.
 | Author: suinevere
 | Dependencies: argparse, json, numpy, pathlib, subprocess, sys, ffmpeg,
 |     mid2pat.py, preview.py
 | Globals: HERE, MUSIC, RATE, NFFT, HOP, EDGES, LABELS, SECONDS, OCTAVES
 | Run: python tools/assets/voicecmp.py --dir "...\\Shadowgate Soundtrack"
 |      python tools/assets/voicecmp.py --dir ... --sweep
 ----------------------*/"""
import argparse
import json
import pathlib
import subprocess
import sys

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
MUSIC = HERE / "music"
sys.path.insert(0, str(HERE))
import mid2pat  # noqa: E402
import preview  # noqa: E402

RATE = 44100
NFFT = 8192
HOP = 4096
# Octaves from 20 Hz to Nyquist, and no gaps at either end. See the header:
# energy that leaves the measured range must show up somewhere or a sweep will
# find "fixes" that only move a voice out of sight.
EDGES = [20, 55, 110, 220, 440, 880, 1760, 3520, 7040, 14080, 22050]
LABELS = ["<55", "55-110", "110-220", "220-440", "440-880", "880-1k7",
          "1k7-3k5", "3k5-7k", "7k-14k", ">14k"]
SECONDS = 45.0
# What a sweep is allowed to propose. One octave either way per lane: a fan
# sequence writes a voice an octave out, it does not write it four octaves out,
# and a wider search finds arithmetic rather than music.
OCTAVES = (-1, 0, 1)


def decode(path):
    """/*----------------------
     | decode
     | Description: One rip as mono float PCM at the synth's own rate.
     | Author: suinevere
     | Dependencies: subprocess, numpy, ffmpeg on PATH
     | Globals: RATE
     | Params: path -- an audio file ffmpeg can read
     | Returns: float32 in -1..1
     ----------------------*/"""
    out = subprocess.run(
        ["ffmpeg", "-v", "quiet", "-i", str(path), "-ac", "1",
         "-ar", str(RATE), "-f", "s16le", "-"],
        check=True, stdout=subprocess.PIPE).stdout
    return np.frombuffer(out, dtype="<i2").astype(np.float32) / 32768.0


def bands(signal):
    """/*----------------------
     | bands
     | Description: The share of a signal's energy in each octave band. Shares
     |     rather than levels, so the comparison survives the rip and the
     |     render being mastered differently, which they are.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: RATE, NFFT, HOP, EDGES, SECONDS
     | Params: signal -- mono float PCM, or the model's own float list
     | Returns: an array of len(EDGES)-1 fractions summing to 1
     ----------------------*/"""
    sig = np.asarray(signal, dtype=np.float32)[:int(SECONDS * RATE)]
    if len(sig) < NFFT:
        return np.zeros(len(EDGES) - 1)
    win = np.hanning(NFFT).astype(np.float32)
    frames = 1 + (len(sig) - NFFT) // HOP
    idx = np.arange(NFFT)[None, :] + HOP * np.arange(frames)[:, None]
    spec = np.abs(np.fft.rfft(sig[idx] * win, axis=1)) ** 2
    freq = np.fft.rfftfreq(NFFT, 1.0 / RATE)
    total = np.array([spec[:, (freq >= EDGES[k]) & (freq < EDGES[k + 1])].sum()
                      for k in range(len(EDGES) - 1)])
    return total / total.sum() if total.sum() else total


def render(record, octaves=None):
    """/*----------------------
     | render
     | Description: One tune through the offline model, optionally with a
     |     whole-octave offset per lane that the manifest does not carry --
     |     which is how a sweep asks "would this be better one octave up"
     |     without editing the manifest to find out.
     | Author: suinevere
     | Dependencies: mid2pat, preview
     | Globals: SECONDS
     | Params: record -- a settings dict from load_manifest; octaves -- a list
     |     of octave counts per lane, or None for the manifest's own
     | Returns: the rendered float buffer
     ----------------------*/"""
    if octaves is not None:
        record = dict(record, octaves=list(octaves))
    song = mid2pat.convert_song(record)
    return preview.render(song["cells"], song["speed"], song["frac"],
                          song["ch_wave"], SECONDS), song


def refs(directory):
    """/*----------------------
     | refs
     | Description: The measured tune-to-rip mapping, resolved to files in the
     |     folder the caller points at. A song whose rip is null in
     |     nes_refs.json, or whose rip is not in that folder, is skipped rather
     |     than guessed at.
     | Author: suinevere
     | Dependencies: json, pathlib
     | Globals: MUSIC
     | Params: directory -- where the rips are
     | Returns: {song id: path to its rip}
     ----------------------*/"""
    path = MUSIC / "nes_refs.json"
    if not path.exists():
        raise SystemExit("no %s -- run tools/match_nes_rips.py first" % path)
    doc = json.loads(path.read_text(encoding="utf-8"))
    root = pathlib.Path(directory)
    found = {p.stem: p for p in root.iterdir() if p.is_file()}
    out = {}
    for sid, entry in doc["songs"].items():
        rip = entry.get("rip")
        if rip and rip in found:
            out[sid] = found[rip]
    if not out:
        raise SystemExit("none of the rips named in nes_refs.json are in %s"
                         % directory)
    return out


def sweep(record, reference):
    """/*----------------------
     | sweep
     | Description: The whole-octave offset per lane that puts a tune's band
     |     profile nearest its original's, by coordinate descent over one
     |     octave either way. Two passes, because moving the bass changes which
     |     octave the harmony should be in.
     |
     |     Percussion is not swept: that lane's note is the shift register's
     |     clock rate, calibrated at DRUM_NOTE against this same recording set,
     |     and moving it is a different drum rather than a transposition.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: OCTAVES
     | Params: record -- a settings dict; reference -- the rip's band profile
     | Returns: (offsets, L1 before, L1 after)
     ----------------------*/"""
    def cost(offsets):
        buf, song = render(record, offsets)
        return float(np.abs(bands(buf) - reference).sum()), song

    start = list(record.get("octaves") or [0] * mid2pat.CHANNELS)
    start += [0] * (mid2pat.CHANNELS - len(start))
    before, song = cost(start)
    lanes = mid2pat.CHANNELS - 1 if song["has_drums"] else mid2pat.CHANNELS
    best, current = before, list(start)
    for _ in range(2):
        for lane in range(lanes):
            for value in OCTAVES:
                trial = list(current)
                trial[lane] = value
                got, _ = cost(trial)
                if got < best - 1e-4:
                    best, current = got, trial
    return current, before, best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True,
                    help="a folder of soundtrack rips ffmpeg can read")
    ap.add_argument("--manifest", default=str(MUSIC / "songs.json"))
    ap.add_argument("--song", help="just this one")
    ap.add_argument("--sweep", action="store_true",
                    help="also search for a better octave per lane, and print "
                         "what it found. Writes nothing: an offset belongs in "
                         "songs.json only once its whole profile is seen to "
                         "converge, band by band, on the original's.")
    args = ap.parse_args()

    songs, _ = mid2pat.load_manifest(args.manifest)
    paired = refs(args.dir)
    errors = []
    print("%-14s%s" % ("", "".join("%9s" % s for s in LABELS)))
    for record in songs:
        sid = record["id"]
        if sid not in paired or (args.song and sid != args.song):
            continue
        reference = bands(decode(paired[sid]))
        buf, _ = render(record)
        ours = bands(buf)
        err = float(np.abs(ours - reference).sum())
        errors.append((err, sid))
        print("%-14s%s" % (sid + " nes", "".join("%9.3f" % v for v in reference)))
        print("%-14s%s  L1=%.3f" % ("  ours",
                                    "".join("%9.3f" % v for v in ours), err))
        if args.sweep:
            offsets, before, after = sweep(record, reference)
            got, _ = render(record, offsets)
            print("%-14s%s  L1=%.3f  octaves %s"
                  % ("  swept", "".join("%9.3f" % v for v in bands(got)),
                     after, offsets[:3]))
    print()
    for err, sid in sorted(errors, reverse=True):
        print("  %-14s L1 %.3f%s" % (sid, err, "   <-- worst" if
                                     (err, sid) == max(errors) else ""))
    if errors:
        print("  %-14s L1 %.3f" % ("mean",
                                   sum(e for e, _ in errors) / len(errors)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
