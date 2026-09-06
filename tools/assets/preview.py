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

# The pitched voices' release, kept in step with SCSP_EG_SUSTAINED_RR in
# saturn/src/sound/scsp.h. Both numbers are here rather than one, because a
# host cannot read a C header and a model that silently disagrees with the chip
# is worse than no model: the release was the chip's maximum rate for as long as
# this synth has existed and the preview never showed it, since the preview had
# no release at all. saturn/tests/test_release_envelope.py fails if they part.
#
# The rate is turned into a time on the chip's own scale -- four steps to a
# factor of two, and rate 17 measured at a 4 ms half-life for the drum.
SUSTAINED_RR = 7
PERC_D1R_RATE = 17
PERC_D1R_HALFLIFE_S = 0.004


def release_halflife(rate):
    """/*----------------------
     | release_halflife
     | Description: How long a released note takes to fall by half at a given
     |     SCSP rate, on the geometric scale scsp.h records: four steps to a
     |     factor of two, anchored on the one rate this project has measured.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: PERC_D1R_RATE, PERC_D1R_HALFLIFE_S
     | Params: rate -- an SCSP envelope rate, 0 slowest to 31 fastest
     | Returns: seconds
     ----------------------*/"""
    return PERC_D1R_HALFLIFE_S * 2.0 ** ((PERC_D1R_RATE - rate) / 4.0)


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
    releasing = [False] * nvoices
    # Per-sample factor for the pitched release, from the rate above.
    release_step = 0.5 ** (1.0 / (release_halflife(SUSTAINED_RR) * RATE))
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
                # Released, not silenced. The chip runs its release rate down
                # from wherever the note was; the model does the same, so a rest
                # is a note ending rather than a note stopping.
                releasing[ch] = True
                continue
            index = note - 2
            semi, octv = index % 12, index // 12 - 2
            # A pitched voice already sounding is NOT re-struck: scsp_key_on
            # clears KYONB and sets it again without a KYONEX between, so the
            # chip sees no transition and the new pitch takes effect where the
            # note stands. That is a hammer-on, and it is what the NES original
            # of this catalogue does to ornament a held note -- measured on the
            # rip at 13.6 s, the lead moves 500.2 to 458.5 Hz and back with the
            # level flat to within a percent across the move.
            #
            # The model used to restart the phase and the envelope on every
            # note, so it re-attacked where the chip does not: it agreed with
            # the chip about a fault they did not share, in the other
            # direction from the release. Percussion is exempt, because there
            # scsp_key_on does issue the key-off and the settle, deliberately.
            held = (wave_of[ch] != mid2pat.WAVE_NOISE
                    and wv >> 4 != mid2pat.WAVE_NOISE
                    and amp[ch] > 0.0 and env[ch] > 0.0005
                    and not releasing[ch])
            wave_of[ch] = wv >> 4
            vol = wv & 0x0F
            # DISDL is three bits in roughly 6 dB steps; 7 is full output.
            amp[ch] = 0.0 if vol == 0 else 2.0 ** (min(7, vol) - 7)
            # Table samples per output sample. The engine's OCT 0 semitone 0 is
            # one table sample per output sample whatever the table's length.
            step[ch] = 2.0 ** (semi / 12.0 + octv)
            if held:
                continue                    # pitch moved, envelope carries on
            env[ch] = 1.0
            releasing[ch] = False
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
                if releasing[ch]:
                    env[ch] *= release_step
                elif wave_of[ch] == mid2pat.WAVE_NOISE:
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
    ap.add_argument("midi", nargs="?")
    ap.add_argument("wav")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--song",
                    help="render a tune from a manifest by its id, with the "
                         "settings recorded there, instead of a MIDI file and "
                         "the options below")
    ap.add_argument("--manifest",
                    default=str(pathlib.Path(__file__).resolve().parent
                                / "music" / "songs.json"),
                    help="where --song looks; defaults to music/songs.json")
    mid2pat.add_shared_arguments(ap)
    args = ap.parse_args()

    if args.song:
        songs, _ = mid2pat.load_manifest(args.manifest)
        picked = [s for s in songs if s["id"] == args.song]
        if not picked:
            raise SystemExit("no song '%s' in %s -- have: %s"
                             % (args.song, args.manifest,
                                ", ".join(s["id"] for s in songs)))
        s = picked[0]
        print("%s -- %s" % (s["id"], s["name"]))
        song = mid2pat.convert_song(s)
    else:
        if not args.midi:
            raise SystemExit("give a MIDI file, or --song ID")
        song = mid2pat.convert(args.midi, args.grid, 0, args.max_rows,
                               args.no_drums, args.bpm, args.fold_octaves,
                               args.drums_tab, args.tab_beats)

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
