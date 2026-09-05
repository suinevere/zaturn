#!/usr/bin/env python3
"""/*----------------------
 | gen_synth_moods.py
 | Description: GENERATES tools/assets/music/track_songs.json -- which of the
 |     synth's tunes stands in for each of the disc's CD-DA tracks when a build
 |     has no CD audio to play.
 |
 |     The disc's thirty-one tracks were measured once by gen_track_mood.py and
 |     the numbers live in tools/assets/track_mood.json. This renders each tune
 |     in tools/assets/music/songs.json through the offline model of the chip,
 |     measures it with gen_track_mood's own measure_signal -- the same code, so
 |     the two cannot drift -- and matches each track to the nearest tune.
 |
 |     Matched on rank, not on absolute value. Four voices of NES pulse are
 |     brighter and thinner than any of Activision's ambient recordings, so
 |     absolute distance would put every track on whichever tune happened to be
 |     the darkest and say nothing. Both sets are turned into z-scores within
 |     their own population first, so the question asked is "which tune is as
 |     bright, as busy and as heavy FOR A TUNE as this track is FOR A TRACK",
 |     which is the same thing track_mood.json's own mood words mean by
 |     "bright" -- a rank within the disc.
 |
 |     Two of the ten measurements are dropped and neither is a judgement call.
 |     loudness_db is the render's own normalisation, which is a constant of the
 |     preview and not of the tune; width is stereo spread, and the model
 |     renders mono, so every tune would read 0.000 and the whole match would
 |     tilt toward whichever tracks are narrowest.
 | Author: suinevere
 | Dependencies: numpy, gen_track_mood (measure_signal), tools/assets/mid2pat.py,
 |     tools/assets/preview.py
 | Globals: ROOT, MANIFEST, MOODS, OUT, DIMS, SECONDS
 | Run: python tools/gen_synth_moods.py
 |      python tools/gen_synth_moods.py --report   (print the table, write nothing)
 ----------------------*/"""
import argparse
import json
import pathlib
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "assets"))
sys.path.insert(0, str(ROOT / "tools"))

import mid2pat                      # noqa: E402
import preview                      # noqa: E402
from gen_track_mood import measure_signal   # noqa: E402

MANIFEST = ROOT / "tools" / "assets" / "music" / "songs.json"
MOODS = ROOT / "tools" / "assets" / "track_mood.json"
OUT = ROOT / "tools" / "assets" / "music" / "track_songs.json"

# The measurements that carry across from a ripped orchestral track to four
# square waves. See the file header for why loudness_db and width do not.
DIMS = ("brightness_hz", "rolloff_hz", "weight", "air",
        "tonality", "motion", "pulse_per_min")

# Long enough for the slowest tune here to state itself twice and for the pulse
# count -- which is per minute -- to rest on more than a handful of events.
SECONDS = 45.0


def render_tune(song):
    """/*----------------------
     | render_tune
     | Description: One manifest entry as a float signal, through the same
     |     offline model tools/assets/songs.bat plays. It is a model and not the
     |     chip -- no SCSP envelope rates, no interpolation -- which is right
     |     here: what is being measured is which notes and how many, and those
     |     are the converter's, not the machine's.
     | Author: suinevere
     | Dependencies: mid2pat, preview, numpy
     | Globals: SECONDS
     | Params: song -- a settings dict from mid2pat.load_manifest
     | Returns: a float32 array in -1..1
     ----------------------*/"""
    got = mid2pat.convert(song["midi"], grid=song["grid"], speed_arg=0,
                          max_rows=song["max_rows"], no_drums=False,
                          bpm_override=song["bpm"], fold=song["fold"],
                          drum_tab=song["drums_tab"], tab_beats=song["tab_beats"])
    buf = preview.render(got["cells"], got["speed"], got["frac"],
                         got["ch_wave"], SECONDS)
    sig = np.asarray(buf, dtype=np.float32)
    peak = float(np.max(np.abs(sig))) or 1.0
    return sig / peak


def z_scores(rows):
    """/*----------------------
     | z_scores
     | Description: A population of measurement dicts as an array of z-scores,
     |     one row per member and one column per DIMS entry. A dimension with no
     |     spread at all becomes zeros rather than a division by zero, which is
     |     what a measurement that says the same thing about everything is
     |     worth.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: DIMS
     | Params: rows -- measurement dicts
     | Returns: an array shaped (len(rows), len(DIMS))
     ----------------------*/"""
    raw = np.array([[float(r[d]) for d in DIMS] for r in rows], dtype=np.float64)
    mean = raw.mean(axis=0)
    sd = raw.std(axis=0)
    sd[sd == 0] = 1.0
    return (raw - mean) / sd


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", action="store_true",
                    help="print the match and write nothing")
    ap.add_argument("--manifest", default=str(MANIFEST))
    args = ap.parse_args(argv)

    songs, default_index = mid2pat.load_manifest(args.manifest)
    disc = json.loads(MOODS.read_text(encoding="utf-8"))["tracks"]
    tracks = sorted(disc, key=int)

    measured = []
    for s in songs:
        sig = render_tune(s)
        m = measure_signal(sig, sig, sig)
        if m is None:
            raise SystemExit("%s rendered to silence" % s["id"])
        measured.append(m)
        print("%-12s %5d Hz bright  %5d Hz rolloff  weight %.3f  air %.3f  "
              "tonality %.3f  motion %.3f  %5.1f pulses/min"
              % (s["id"], m["brightness_hz"], m["rolloff_hz"], m["weight"],
                 m["air"], m["tonality"], m["motion"], m["pulse_per_min"]))

    tz = z_scores(measured)
    dz = z_scores([disc[t] for t in tracks])

    mapping, lines = {}, []
    for i, t in enumerate(tracks):
        d = np.sqrt(((tz - dz[i]) ** 2).sum(axis=1))
        pick = int(np.argmin(d))
        mapping[t] = songs[pick]["id"]
        lines.append("track %2s -> %-12s (distance %.2f)  %s"
                     % (t, songs[pick]["id"], d[pick],
                        ", ".join(disc[t].get("mood", []))))

    for line in lines:
        print(line)
    used = sorted(set(mapping.values()))
    print("%d tracks over %d of %d tunes; unused: %s"
          % (len(mapping), len(used), len(songs),
             ", ".join(s["id"] for s in songs if s["id"] not in used) or "none"))

    if args.report:
        return 0

    doc = {
        "_comment": [
            "GENERATED by tools/gen_synth_moods.py -- which synth tune stands in",
            "for each CD-DA track on a build with no CD audio. Matched on rank",
            "within each population rather than on absolute measurement; see that",
            "script's header for why, and for the two measurements it drops.",
            "",
            "Hand-editable, and meant to be: a match is the nearest tune in seven",
            "numbers, not a judgement about what a room should sound like. Change",
            "a value here and re-run mid2pat.py --manifest to put it in the build.",
            "A track named by no entry falls to the manifest's default tune.",
        ],
        "min_track": int(tracks[0]),
        "max_track": int(tracks[-1]),
        "tracks": mapping,
        "measured": {s["id"]: m for s, m in zip(songs, measured)},
    }
    OUT.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
    print("wrote %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
