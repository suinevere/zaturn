#!/usr/bin/env python3
"""/*----------------------
 | match_nes_rips.py
 | Description: GENERATES tools/assets/music/nes_refs.json -- which recording
 |     of the Shadowgate NES soundtrack each fan MIDI in the catalogue is a
 |     sequence of, decided by measurement rather than by reading filenames.
 |
 |     Filenames on both sides name a tune for a human and not for the record.
 |     The MIDIs are "sgover", "sghalls", "sglitrod", "shadow7"; the rips are
 |     "Game Over", "Hall of Mirrors", "Twilight", "Subterranean Cavern". Three
 |     of the manifest's titles were wrong before this ran, one MIDI turned out
 |     to be a second sequence of a piece already in the catalogue, and one
 |     matches nothing at all. The only thing that can say which is which is
 |     the notes.
 |
 |     Both sides are reduced to a pitch-class (chroma) sequence -- the rip by
 |     FFT, the MIDI by walking its note events -- and correlated over every
 |     transposition, a range of tempo ratios and every time offset, each lag
 |     normalised by its own overlap so a short sequence is not penalised for
 |     the part of a long rip it does not cover. Chroma because the two sides
 |     share their notes and share nothing else: one is a 2A03 through an mp3
 |     encoder and the other is a sequencer's idea of the same tune.
 |
 |     The rips are not in the repository and never will be -- they are the
 |     soundtrack of a commercial game. This reads whatever the caller points
 |     it at and commits only the derived mapping, which is the same bargain
 |     gen_track_mood.py makes with Activision's audio and gen_map_atlas.py
 |     with Infocom's maps.
 | Author: suinevere
 | Dependencies: argparse, json, numpy, pathlib, subprocess, sys, ffmpeg,
 |     tools/assets/mid2pat.py
 | Globals: ROOT, MUSIC, OUT, RATE, HOP, NFFT, FPS, SECONDS, MINOVER, RATIOS
 | Run: python tools/match_nes_rips.py --dir "C:\\...\\Shadowgate Soundtrack"
 ----------------------*/"""
import argparse
import json
import pathlib
import subprocess
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
MUSIC = ROOT / "tools" / "assets" / "music"
OUT = MUSIC / "nes_refs.json"
sys.path.insert(0, str(ROOT / "tools" / "assets"))
import mid2pat  # noqa: E402

RATE = 22050
HOP = 1024
NFFT = 4096
FPS = RATE / HOP
# Long enough to hold a whole loop of the longest rip, so a MIDI can be placed
# anywhere in it; the cost is linear and the whole run is a couple of minutes.
SECONDS = 180.0
# A claim has to be built on this many seconds of overlap. Without it the best
# lag is a two-second corner of one tune sitting on a two-second corner of
# another, which correlates near 1.0 and means nothing.
MINOVER = 20.0
RATIOS = np.geomspace(0.70, 1.45, 25)
# What a match looks like. Every confirmed pair in the catalogue scores above
# the first and every rejected one below the second; between them is a report
# that says so rather than an answer.
CLAIM = 0.30
DENY = 0.25


def decode(path):
    """/*----------------------
     | decode
     | Description: One rip as mono float PCM at RATE, through ffmpeg. The
     |     rips are mp3 and nothing here should care what container they are
     |     in, so the decode is delegated rather than written.
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


def chroma_audio(sig):
    """/*----------------------
     | chroma_audio
     | Description: A recording as one twelve-bin pitch-class vector per frame.
     |     Bins below 55 Hz and above 4 kHz are dropped: the first is where a
     |     rip's rumble lives and the second is where its harmonics stop
     |     naming a pitch and start naming a timbre.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: RATE, HOP, NFFT, FPS, SECONDS
     | Params: sig -- mono float PCM
     | Returns: (frames, 12) float32 of energy per pitch class
     ----------------------*/"""
    win = np.hanning(NFFT).astype(np.float32)
    frames = min(1 + max(0, (len(sig) - NFFT) // HOP), int(SECONDS * FPS))
    if frames <= 0:
        return np.zeros((0, 12), np.float32)
    idx = np.arange(NFFT)[None, :] + HOP * np.arange(frames)[:, None]
    spec = np.abs(np.fft.rfft(sig[idx] * win, axis=1)) ** 2
    freqs = np.fft.rfftfreq(NFFT, 1.0 / RATE)
    keep = (freqs > 55.0) & (freqs < 4000.0)
    pc = np.rint(12.0 * np.log2(freqs[keep] / 440.0) + 69.0).astype(int) % 12
    mag = spec[:, keep]
    out = np.zeros((frames, 12), np.float32)
    for k in range(12):
        m = pc == k
        if m.any():
            out[:, k] = mag[:, m].sum(axis=1)
    return out


def chroma_midi(path, bpm=0.0):
    """/*----------------------
     | chroma_midi
     | Description: A MIDI as the same twelve-bin sequence, one bin held for as
     |     long as its note sounds. Channel 9 is dropped -- percussion carries
     |     no pitch and would smear all twelve bins evenly.
     | Author: suinevere
     | Dependencies: numpy, mid2pat.read_midi
     | Globals: FPS, SECONDS
     | Params: path -- the MIDI; bpm -- override, 0 for the file's own tempo
     | Returns: (frames, 12) float32 of voices sounding per pitch class
     ----------------------*/"""
    division, tempo, events = mid2pat.read_midi(path)
    if bpm:
        tempo = 60000000.0 / bpm
    spt = (tempo / 1e6) / division
    frames = int(SECONDS * FPS)
    out = np.zeros((frames, 12), np.float32)
    live = {}
    for tick, pitch, on, chan in events:
        if chan == mid2pat.DRUM_MIDI_CHANNEL:
            continue
        t = tick * spt
        if on:
            live.setdefault((chan, pitch), t)
            continue
        start = live.pop((chan, pitch), None)
        if start is None:
            continue
        a = int(start * FPS)
        b = max(int(min(t, SECONDS) * FPS), a + 1)
        if a < frames:
            out[a:min(b, frames), pitch % 12] += 1.0
    for (chan, pitch), start in live.items():
        a = int(start * FPS)
        if a < frames:
            out[a:, pitch % 12] += 1.0
    last = max([0.0] + [tick * spt for tick, _, _, _ in events])
    return out[:max(1, min(frames, int(np.ceil(last * FPS))))]


def unit(c):
    """/*----------------------
     | unit
     | Description: Each frame scaled to unit length, so a loud passage and a
     |     quiet one carrying the same chord weigh the same.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: N/A
     | Params: c -- a chroma sequence
     | Returns: the same shape, each row of length 1 (or 0 where it was 0)
     ----------------------*/"""
    e = np.sqrt((c ** 2).sum(axis=1, keepdims=True))
    e[e == 0] = 1.0
    return c / e


def resample(c, n):
    """/*----------------------
     | resample
     | Description: A chroma sequence stretched to n frames, which is how one
     |     tempo ratio is tried.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: N/A
     | Params: c -- a chroma sequence; n -- frames wanted
     | Returns: (n, 12) float
     ----------------------*/"""
    if len(c) == 0:
        return np.zeros((n, 12), np.float32)
    src, dst = np.linspace(0.0, 1.0, len(c)), np.linspace(0.0, 1.0, n)
    return np.stack([np.interp(dst, src, c[:, k]) for k in range(12)], axis=1)


def lagged(a, b):
    """/*----------------------
     | lagged
     | Description: The best correlation of two chroma sequences over every
     |     time offset, each lag divided by the energy actually overlapping at
     |     that lag rather than by both sequences' whole norms. Without that
     |     division a MIDI covering half a rip scores half of what it deserves,
     |     which is what buried three of the matches on the first attempt.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: FPS, MINOVER
     | Params: a, b -- mean-removed chroma sequences
     | Returns: (correlation, overlapping seconds at that lag)
     ----------------------*/"""
    n, m = len(a), len(b)
    size = 1
    while size < n + m:
        size *= 2
    num = np.fft.irfft(np.fft.rfft(a, size, axis=0) *
                       np.fft.rfft(b[::-1], size, axis=0),
                       size, axis=0).sum(axis=1)
    ones_n, ones_m = np.ones(n), np.ones(m)
    da = np.fft.irfft(np.fft.rfft((a ** 2).sum(1), size) *
                      np.fft.rfft(ones_m, size), size)
    db = np.fft.irfft(np.fft.rfft(ones_n, size) *
                      np.fft.rfft((b ** 2).sum(1)[::-1], size), size)
    ov = np.fft.irfft(np.fft.rfft(ones_n, size) *
                      np.fft.rfft(ones_m, size), size)
    valid = ov >= MINOVER * FPS
    if not valid.any():
        valid = ov >= 0.5 * ov.max()
    r = np.where(valid, num / np.sqrt(np.maximum(da, 1e-9) *
                                      np.maximum(db, 1e-9)), -1.0)
    k = int(r.argmax())
    return float(r[k]), float(ov[k] / FPS)


def score(ca, cb):
    """/*----------------------
     | score
     | Description: How much one rip and one MIDI are the same piece, over
     |     every transposition and tempo ratio as well as every offset. A fan
     |     sequence may be in another key and is rarely at the machine's tempo,
     |     and neither says anything about whether it is the same tune.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: RATIOS
     | Params: ca -- the rip's chroma; cb -- the MIDI's
     | Returns: (correlation, {ratio, semitones, overlap})
     ----------------------*/"""
    best, detail = -1.0, None
    a = unit(ca)
    a = a - a.mean(axis=0, keepdims=True)
    for ratio in RATIOS:
        m = int(len(cb) * ratio)
        if m < 32:
            continue
        stretched = unit(resample(cb, m))
        for semitones in range(12):
            b = np.roll(stretched, semitones, axis=1)
            b = b - b.mean(axis=0, keepdims=True)
            value, overlap = lagged(a, b)
            if value > best:
                best = value
                detail = {"tempo_ratio": round(float(ratio), 3),
                          "semitones": semitones,
                          "overlap_s": round(overlap, 1)}
    return best, detail


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True,
                    help="a folder of soundtrack rips ffmpeg can read")
    ap.add_argument("--manifest", default=str(MUSIC / "songs.json"))
    ap.add_argument("--report", action="store_true",
                    help="print the whole matrix and write nothing")
    args = ap.parse_args()

    rips = sorted(p for p in pathlib.Path(args.dir).iterdir()
                  if p.suffix.lower() in (".mp3", ".wav", ".flac", ".ogg"))
    if not rips:
        raise SystemExit("no audio in %s" % args.dir)
    songs, _ = mid2pat.load_manifest(args.manifest)

    audio = {p.stem: chroma_audio(decode(p)) for p in rips}
    midi = {s["id"]: chroma_midi(s["midi"], s["bpm"]) for s in songs}

    names, ids = list(audio), list(midi)
    matrix = np.zeros((len(names), len(ids)))
    details = {}
    for i, name in enumerate(names):
        for j, sid in enumerate(ids):
            matrix[i, j], details[(name, sid)] = score(audio[name], midi[sid])
        print("  %-40s %s" % (name[:40], ids[int(matrix[i].argmax())]),
              file=sys.stderr, flush=True)

    if args.report:
        print("%-40s" % "" + "".join("%9s" % s[:8] for s in ids))
        for i, name in enumerate(names):
            print("%-40s" % name[:40] + "".join("%9.3f" % v for v in matrix[i]))
        return 0

    out = {"_comment": [
        "Which rip of the Shadowgate NES soundtrack each MIDI in songs.json is",
        "a sequence of. GENERATED by tools/match_nes_rips.py; the rips are not",
        "in the repository and only this mapping is.",
        "score is the chroma correlation of the two, over transposition, tempo",
        "and offset; above %.2f is a claim and below %.2f is a denial." % (CLAIM, DENY),
    ], "songs": {}}
    for j, sid in enumerate(ids):
        i = int(matrix[:, j].argmax())
        value = float(matrix[i, j])
        runner = float(np.sort(matrix[:, j])[-2]) if len(names) > 1 else 0.0
        out["songs"][sid] = {
            "rip": names[i] if value >= CLAIM else None,
            "score": round(value, 3),
            "runner_up": round(runner, 3),
            "detail": details[(names[i], sid)],
        }
    claimed = [v["rip"] for v in out["songs"].values() if v["rip"]]
    out["rips_no_song_claims"] = [n for n in names if n not in claimed]
    # A rip two songs both claim is one piece sequenced twice, which costs the
    # netbin an image slot for a tune it already carries. Reported rather than
    # resolved: which of the two to keep is a listening call.
    out["rips_claimed_twice"] = sorted({n for n in claimed
                                        if claimed.count(n) > 1})
    OUT.write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
    print("wrote %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
