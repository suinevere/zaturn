#!/usr/bin/env python3
"""Convert a MIDI file into the SH-2 synth's pattern table (music_synth_data.c).

The synth has four monophonic SCSP voices, four generated waveforms plus the
chip's own noise generator, and no effects, so a full arrangement has to be
reduced before it will fit. What this does:

  * routes MIDI channel 10 (the General MIDI drum channel, 0-based 9) to the
    noise voice, because its note numbers are drum sounds rather than pitches
    and playing them as melody produces gibberish;
  * quantises every note onset to a fixed row grid (default: sixteenth notes);
  * at each row keeps the outer voices of what is left -- lowest to the bass
    channel, highest to the lead, the next inward to the accompaniment -- since
    the outer parts carry the melody and the bass, and inner parts are the first
    thing a human arranger drops;
  * emits a note only where a channel's pitch changes, so a held note costs one
    cell and the rest of its duration is free;
  * collapses identical patterns and references them from the order list, which
    is most of the size win on music that repeats.

Timing. A row lasts a whole number of V-blanks but music does not: at 165 BPM a
sixteenth note is 5.45 frames, and rounding to 5 or 6 runs the piece 9 per cent
fast or slow. The row length is therefore emitted as whole frames plus a
fraction in 256ths, which the tracker carries between rows.

Pitch. The waveform is 256 samples at the SCSP's 44100 Hz, so OCT 0 / FNS 0
sounds 172.27 Hz -- 23 cents flat of F3, and not placeable exactly on a
semitone. The whole piece is therefore uniformly 23 cents flat, which is
inaudible on its own and leaves the music in tune with itself. MIDI note 53 is
the engine's octave 0, semitone 0. The engine cannot represent an octave below
-2, so the piece is transposed up in whole octaves until its lowest note fits.

Register and voicing. Three tonal voices is fewer than a sequence usually
assumes, and two options exist for saying so. --fold-octaves collapses a note
doubled at the octave to one voice, which is what a sequencer's thickening of a
bass line costs here; --bpm overrides the file's declared tempo, for a sequence
transcribed at the wrong speed. Both were needed for the Shadowgate entryway
theme and both are off by default.

Usage:
  python tools/assets/mid2pat.py IN.mid OUT.c --name "Title" --source "Credit"
"""
import argparse, struct, sys, pathlib

ROWS_PER_PATTERN = 16
CHANNELS = 4
DRUM_MIDI_CHANNEL = 9

# Waveform and volume per synth channel, following how an NES is scored: the
# bass on the triangle, the lead on the 50% square, the harmony on a 25% pulse,
# and drums on the noise generator when the piece has any (a 12.5% pulse
# otherwise). The lead's duty is measured, not chosen: a recording of the NES
# Shadowgate entryway theme has odd harmonics only, at 0.29 / 0.18 / 0.13 of
# the fundamental, which is the 50% square's 0.33 / 0.20 / 0.14 and not the
# 25% pulse, whose second harmonic at 0.71 the original does not have at all.
# Its bass matches the triangle's 0.11 / 0.04 / 0.02 exactly.
CH_WAVE_TONAL = [2, 3, 1, 0]
CH_WAVE_DRUMS = [2, 3, 1, 4]
# Swept against the octave-band profile of a recording of the NES original
# rather than chosen: bass one DISDL step above the lead, harmony one below,
# drums with the bass. DISDL is three bits in roughly 6 dB steps, so the old
# [6, 4, 3, 5] put the bass at four times the lead, which measured 51 per cent
# of our energy in the bass octave against the original's 30 and left the
# melody band at 11 against its 23. The drum sits at full output with a fine trim
# on top of it -- see SCSP_NOISE_TRIM -- because DISDL's steps are 6 dB and the
# right level is between two of them: 6 measures 4.6 / 4.7 per cent in the 2-4
# and 4-8 kHz bands against the original's 6.6 / 6.3, and 7 measures 8.7 / 9.6,
# equally wrong either way. Every level here is measured on the chip rather than
# in the model, and re-measured after anything that changes what a voice
# carries: this was swept five times, and the first four answers were wrong
# because the voices were not yet carrying what they carry now -- the last one
# because most of the drum strikes were not sounding at all.
CH_VOL = [6, 5, 4, 7]
WAVE_NAMES = ["SYNTH_WAVE_PULSE12", "SYNTH_WAVE_PULSE25",
              "SYNTH_WAVE_TRIANGLE", "SYNTH_WAVE_PULSE50", "SYNTH_WAVE_NOISE"]
WAVE_NOISE = 4

BASE_MIDI = 53
MIN_INDEX = 0
MAX_INDEX = (7 + 2) * 12 + 11
# The note the percussion voice is struck at, and it is not arbitrary any more:
# the noise waveform is a slice of the 2A03's shift register at two samples per
# bit, so the note picks the rate that sequence is clocked at, which is what the
# NES does with its sixteen periods. Index 10 is semitone 10 of octave -2, which
# plays the table at 0.446 samples per output sample -- 9825 bits per second.
# Measured on the chip, not in the model, and the two disagree: the offline
# model picks 9.9 kbit/s and the SCSP picks 15.6, because the chip interpolates
# between table samples and that darkens the result further. Index 18 is
# semitone 6 of octave -1, which is 15,592 bits per second. Recording six rates
# and scoring each against the NES original over 1.5-15 kHz: 0.149 here, against
# 0.179 either side of it and 0.49 for the chip's own noise generator.
DRUM_NOTE = 20


def read_midi(path):
    """Return (division, tempo_us_per_quarter, [(tick, pitch, on, channel)])."""
    buf = pathlib.Path(path).read_bytes()
    if buf[:4] != b"MThd":
        raise SystemExit("%s is not a MIDI file" % path)
    _, ntrk, division = struct.unpack(">HHH", buf[8:14])
    p = 14
    events = []
    tempo = 500000
    for _ in range(ntrk):
        if p >= len(buf) or buf[p:p + 4] != b"MTrk":
            break
        length = struct.unpack(">I", buf[p + 4:p + 8])[0]
        p += 8
        end = p + length
        tick = 0
        running = 0
        while p < end:
            delta = 0
            while True:
                c = buf[p]
                p += 1
                delta = (delta << 7) | (c & 0x7F)
                if not c & 0x80:
                    break
            tick += delta
            status = buf[p]
            if status & 0x80:
                p += 1
                running = status
            else:
                status = running
            high, chan = status & 0xF0, status & 0x0F
            if high in (0x90, 0x80):
                pitch, vel = buf[p], buf[p + 1]
                p += 2
                events.append((tick, pitch, high == 0x90 and vel > 0, chan))
            elif high in (0xA0, 0xB0, 0xE0):
                p += 2
            elif high in (0xC0, 0xD0):
                p += 1
            elif status == 0xFF:
                meta = buf[p]
                p += 1
                length2 = 0
                while True:
                    c = buf[p]
                    p += 1
                    length2 = (length2 << 7) | (c & 0x7F)
                    if not c & 0x80:
                        break
                if meta == 0x51 and length2 == 3:
                    tempo = (buf[p] << 16) | (buf[p + 1] << 8) | buf[p + 2]
                p += length2
            elif status in (0xF0, 0xF7):
                length2 = 0
                while True:
                    c = buf[p]
                    p += 1
                    length2 = (length2 << 7) | (c & 0x7F)
                    if not c & 0x80:
                        break
                p += length2
            else:
                p += 1
        p = end
    events.sort(key=lambda e: (e[0], not e[2]))
    return division, tempo, events


def grid_rows(division, events, grid, drums):
    """Per row: what each MIDI channel is sounding, and whether a drum was hit.

    Kept per channel rather than merged into one set, because which part a note
    belongs to is information the merge throws away and the octave fold needs:
    a bass pedalling on G3 under a melody on G4 is two parts an octave apart,
    not one part doubled.
    """
    ticks_per_row = max(1, (division * 4) // grid)
    # Round to the nearest row rather than the one a note starts inside. A hit
    # one tick early -- which is how this sequence writes some of its 32nd-note
    # drum figures, at tick 14 of a 15-tick row -- otherwise falls back onto the
    # row before it and lands on top of the hit already there, so a fast double
    # is heard as a single strike, and only sometimes.
    half = ticks_per_row // 2
    total = ((max(e[0] for e in events) + half) // ticks_per_row) + 1
    parts, hits = [], []
    live, active = {}, {}
    idx = 0
    for row in range(total):
        limit = (row + 1) * ticks_per_row - half
        hit = False
        while idx < len(events) and events[idx][0] < limit:
            _, pitch, on, chan = events[idx]
            idx += 1
            if chan == DRUM_MIDI_CHANNEL:
                if on and drums:
                    hit = True
                continue
            if on:
                active.setdefault(chan, set()).add(pitch)
                live[(chan, pitch)] = live.get((chan, pitch), 0) + 1
            else:
                live[(chan, pitch)] = live.get((chan, pitch), 0) - 1
                if live[(chan, pitch)] <= 0:
                    active.get(chan, set()).discard(pitch)
        parts.append({c: set(p) for c, p in active.items() if p})
        hits.append(hit)
    return parts, hits


def sounding(parts_row):
    """Every pitch in a row, whichever part it came from."""
    out = set()
    for pitches in parts_row.values():
        out |= pitches
    return out


def assign(pitches, slots):
    """Outer voices first: lowest, highest, then inward."""
    if not pitches or slots <= 0:
        return [None] * slots
    ordered = sorted(pitches)
    picks = [ordered[0]]
    if len(ordered) > 1:
        picks.append(ordered[-1])
    inner = [p for p in ordered if p not in picks]
    while len(picks) < slots and inner:
        picks.append(inner[-1])
        inner.pop()
    while len(picks) < slots:
        picks.append(None)
    return picks[:slots]


def fold_octaves(pitches, mode):
    """Collapse exact octave doublings to one note, keeping the named member.

    A pitch and the same pitch an octave away are one line played twice, and on
    three monophonic voices the second copy costs a whole part. The NES original
    of the Shadowgate entryway theme plays its bass line once, at G3; the fan
    sequence of it doubles every bass note an octave below, which is where our
    render put most of its energy and the original has almost none.

    Called with one MIDI channel's notes, never with a whole row merged. Two
    parts an octave apart are not a doubling, and folding across them eats the
    lower one: that tune's bass pedals on G3 under a melody that sits on G4, so
    a merged fold deleted the bass wherever the two lined up and left the melody
    as the lowest note in the row -- which then went to the bass voice, moving
    the tune onto the triangle and back several times a bar.
    """
    if mode == "off" or len(pitches) < 2:
        return pitches
    keep = set(pitches)
    for p in pitches:
        other = p + 12 if mode == "up" else p - 12
        if other in pitches:
            keep.discard(p)
    return keep


def plan_parts(parts, slots):
    """Which source part each voice should follow, or None to reduce by pitch.

    One voice per part is what an arranger does, and it is the only reduction
    that keeps a line on the same voice from one row to the next. Reducing by
    pitch order instead re-decides every row, so two parts that cross -- or a
    melody and its own delayed echo, which cross constantly -- get swapped
    between voices several times a bar. That is what the Shadowgate sequence
    does: its second pulse part is the first delayed by three sixteenths, and
    taking the higher of the two each row made the lead hop between the line and
    its echo.

    It only applies when the parts really are lines. A part sounding two notes
    at once is a reduction problem in itself, and a piece with more parts than
    voices has to drop one; both keep the pitch-order rule. Call after folding
    octaves, since that is what makes an octave-doubled bass monophonic.
    """
    heard = {}
    for row in parts:
        for chan, pitches in row.items():
            if len(pitches) > 1:
                return None
            heard.setdefault(chan, []).extend(pitches)
    heard = {c: v for c, v in heard.items() if v}
    if not heard or len(heard) > slots:
        return None
    median = {c: sorted(v)[len(v) // 2] for c, v in heard.items()}
    # The lowest part takes the bass voice; the rest fill the melodic voices
    # from the top down. Parts of equal register are ordered by channel, which
    # is the order a sequence writes its lead in before its answering line.
    ranked = sorted(median, key=lambda c: (median[c], c))
    lanes = [ranked[0]] + sorted(ranked[1:], key=lambda c: (-median[c], c))
    return (lanes + [None] * slots)[:slots]


def echo_delay(parts, lead, answer, max_shift=16):
    """The row delay at which one part is another repeated, or None.

    Two melodic parts can be two instruments or one instrument heard twice. The
    NES answers a pulse line with the same line delayed, played on its other
    pulse channel at the same duty, and a recording of the entryway theme
    measures both as 50% squares -- h2 0.14 / h3 0.31 against the square's
    0.00 / 0.33, where a 25% pulse would show h2 0.71. Giving the answer a duty
    of its own turns an echo into a second instrument shadowing the first, which
    is heard as a chorus on the lead rather than as a repeat of it.
    """
    a = [max(r[lead]) if r.get(lead) else None for r in parts]
    b = [max(r[answer]) if r.get(answer) else None for r in parts]
    for k in range(1, max_shift + 1):
        pairs = [(a[i - k], b[i]) for i in range(k, len(b))]
        pairs = [q for q in pairs if q != (None, None)]
        if len(pairs) >= 16 and all(x == y for x, y in pairs):
            return k
    return None


def convert(midi, grid=16, speed_arg=0, max_rows=0, no_drums=False,
            bpm_override=0.0, fold="off"):
    """Run the whole reduction and return everything both callers need.

    The offline preview renders from this, and the build emits C from it, so
    what is heard in a second is what the disc will play. They used to each
    walk the events themselves, which is a drift the preview cannot report.
    """
    division, tempo, events = read_midi(midi)
    if not events:
        raise SystemExit("no notes found")

    has_drums = (not no_drums
                 and any(e[3] == DRUM_MIDI_CHANNEL and e[2] for e in events))
    tonal_slots = CHANNELS - 1 if has_drums else CHANNELS
    ch_wave = CH_WAVE_DRUMS if has_drums else CH_WAVE_TONAL

    bpm = bpm_override if bpm_override > 0 else 60000000.0 / tempo
    frames = (60.0 / bpm) * (4.0 / grid) * 60.0
    if speed_arg:
        speed, frac = speed_arg, 0
    else:
        speed = max(1, int(frames))
        frac = int(round((frames - speed) * 256))
        if frac > 255:
            speed, frac = speed + 1, 0

    parts, hits = grid_rows(division, events, grid, has_drums)
    parts = [{c: fold_octaves(p, fold) for c, p in row.items()} for row in parts]
    if max_rows:
        parts, hits = parts[:max_rows], hits[:max_rows]
    while len(parts) % ROWS_PER_PATTERN:
        parts.append({})
        hits.append(False)
    lanes = plan_parts(parts, tonal_slots)
    tonal = [sounding(row) for row in parts]

    # A part that is another part repeated is the same instrument heard twice,
    # so it takes that instrument's waveform rather than one of its own.
    ch_wave, echoes = list(ch_wave), []
    if lanes:
        for j in range(2, tonal_slots):
            for i in range(1, j):
                if lanes[i] is None or lanes[j] is None:
                    continue
                delay = echo_delay(parts, lanes[i], lanes[j])
                if delay:
                    ch_wave[j] = ch_wave[i]
                    echoes.append((lanes[j], lanes[i], delay))
                    break

    played = [p for r in tonal for p in r]
    lowest = min(played) if played else BASE_MIDI
    highest = max(played) if played else BASE_MIDI
    shift = 0
    while (lowest + shift - BASE_MIDI) + 24 < MIN_INDEX:
        shift += 12
    while (highest + shift - BASE_MIDI) + 24 > MAX_INDEX:
        shift -= 12

    cells = []
    previous = [None] * CHANNELS
    thick = 0
    for row, pitches, hit in zip(parts, tonal, hits):
        if len(pitches) > tonal_slots:
            thick += 1
        picked = ([max(row[c]) if c is not None and row.get(c) else None for c in lanes]
                  if lanes else assign(pitches, tonal_slots))
        row = []
        for ch in range(tonal_slots):
            pitch = picked[ch]
            if pitch is None:
                row.append((1, 0) if previous[ch] is not None else (0, 0))
            elif pitch != previous[ch]:
                index = (pitch + shift - BASE_MIDI) + 24
                index = max(MIN_INDEX, min(MAX_INDEX, index))
                row.append((index + 2, (ch_wave[ch] << 4) | CH_VOL[ch]))
            else:
                row.append((0, 0))
            previous[ch] = pitch
        if has_drums:
            last = CHANNELS - 1
            row.append((DRUM_NOTE, (ch_wave[last] << 4) | CH_VOL[last])
                       if hit else (0, 0))
        cells.append(row)

    return {"cells": cells, "speed": speed, "frac": frac, "bpm": bpm,
            "frames": frames,
            "has_drums": has_drums, "tonal_slots": tonal_slots,
            "lanes": lanes, "echoes": echoes,
            "ch_wave": ch_wave, "shift": shift, "thick": thick}


def add_shared_arguments(ap):
    """The options that change the conversion, so both entry points take them."""
    ap.add_argument("--grid", type=int, default=16, help="notes per whole note")
    ap.add_argument("--bpm", type=float, default=0.0,
                    help="override the file's tempo; 0 uses what it declares")
    ap.add_argument("--max-rows", type=int, default=0, help="0 = whole piece")
    ap.add_argument("--no-drums", action="store_true",
                    help="drop MIDI channel 10 instead of playing it as noise")
    ap.add_argument("--fold-octaves", choices=("off", "up", "down"), default="off",
                    help="collapse exact octave doublings, keeping the upper "
                         "member (up) or the lower (down)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("midi")
    ap.add_argument("out")
    ap.add_argument("--name", default="")
    ap.add_argument("--source", default="")
    ap.add_argument("--speed", type=int, default=0,
                    help="whole V-blanks per row; 0 derives it from the tempo")
    add_shared_arguments(ap)
    args = ap.parse_args()

    song = convert(args.midi, args.grid, args.speed, args.max_rows,
                   args.no_drums, args.bpm, args.fold_octaves)
    cells = song["cells"]
    speed, frac, bpm = song["speed"], song["frac"], song["bpm"]
    has_drums, ch_wave, shift = song["has_drums"], song["ch_wave"], song["shift"]
    reduction = ("one voice per part, following MIDI channels %s"
                 % ", ".join(str(c) for c in song["lanes"] if c is not None)
                 if song["lanes"] else
                 "outer voices by pitch at each row, the parts not being single lines")
    for answer, lead, delay in song["echoes"]:
        reduction += ("; channel %d is channel %d repeated %d rows later and takes "
                      "its waveform" % (answer, lead, delay))

    patterns, order, seen = [], [], {}
    for i in range(0, len(cells), ROWS_PER_PATTERN):
        block = tuple(tuple(c) for r in cells[i:i + ROWS_PER_PATTERN] for c in r)
        if block not in seen:
            seen[block] = len(patterns)
            patterns.append(block)
        order.append(seen[block])
    if len(patterns) > 255 or len(order) > 255:
        raise SystemExit("too long: %d patterns / %d order entries (max 255 each) "
                         "-- use a coarser --grid or --max-rows"
                         % (len(patterns), len(order)))

    body = []
    for n, block in enumerate(patterns):
        body.append("    /* pattern %d */" % n)
        for r in range(ROWS_PER_PATTERN):
            row = block[r * CHANNELS:(r + 1) * CHANNELS]
            body.append("    " + " ".join("{ %3d, 0x%02X }," % (a, b) for a, b in row))

    out = pathlib.Path(args.out)
    command = " ".join(["tools/assets/mid2pat.py"]
                       + [a if " " not in a else '"%s"' % a for a in sys.argv[1:]])
    fields = {
        "command": command,
        "name": args.name or pathlib.Path(args.midi).stem,
        "source": args.source,
        "patterns": len(patterns),
        "rows": ROWS_PER_PATTERN,
        "channels": CHANNELS,
        "speed": speed,
        "frac": frac,
        "shift": shift,
        "grid": args.grid,
        "reduction": reduction,
        "cells": "\n".join(body),
        "order": ",\n    ".join(", ".join("%d" % o for o in order[i:i + 16])
                                for i in range(0, len(order), 16)),
        "order_len": len(order),
        "waves": ", ".join(WAVE_NAMES[w] for w in ch_wave),
        "drums": ("channel 4 is the chip's noise generator, struck from MIDI channel 10"
                  if has_drums else "no percussion in the source"),
    }
    out.write_text(TEMPLATE % fields, encoding="utf-8")
    out.with_suffix(".h").write_text(HEADER % fields, encoding="utf-8")

    size = len(patterns) * ROWS_PER_PATTERN * CHANNELS * 2 + len(order)
    print("rows=%d patterns=%d (deduped from %d)" % (len(cells), len(patterns), len(order)))
    print("tempo=%.1f BPM -> %.3f frames/row -> speed=%d + %d/256 -> %.0f s total"
          % (bpm, song["frames"], speed, frac,
             len(cells) * (speed + frac / 256.0) / 60.0))
    print("drums=%s  tonal voices=%d  transpose=%+d semitones"
          % ("noise voice" if has_drums else "none", song["tonal_slots"], shift))
    print("reduction=%s" % reduction)
    for answer, lead, delay in song["echoes"]:
        print("MIDI channel %d is channel %d repeated %d rows later: same instrument"
              % (answer, lead, delay))
    print("rows with more tonal parts than channels: %d of %d"
          % (song["thick"], len(cells)))
    print("pattern data = %d bytes" % size)
    print("wrote %s and %s" % (out, out.with_suffix(".h")))
    return 0


HEADER = '''/*----------------------
 | music_synth_data.h
 | Description: The one loop the synth plays, in both builds. Data only: the
 |   engine reads it and never reaches back. Generated by
 |   tools/assets/mid2pat.py alongside music_synth_data.c -- do not hand-edit,
 |   because the .c asserts its own length against these three numbers.
 |   Current tune: %(name)s
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
 | MUSIC_SYNTH_PATTERNS / MUSIC_SYNTH_ROWS / MUSIC_SYNTH_CHANNELS
 | Description: The shape of the cell array. The order list indexes into the
 |   pattern count, so the two have to agree or the tracker reads past the
 |   table. All three are here rather than only in the .c file because the
 |   file asserts its own initialiser length against them at compile time:
 |   C zero-fills a short initialiser without complaint, which would be a
 |   silently truncated tune that every test still passes.
 | Author: suinevere
 ----------------------*/
#define MUSIC_SYNTH_PATTERNS %(patterns)d
#define MUSIC_SYNTH_ROWS     %(rows)d
#define MUSIC_SYNTH_CHANNELS %(channels)d

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
'''

TEMPLATE = '''/*----------------------
 | music_synth_data.c
 | Description: %(name)s, converted from MIDI -- do not hand-edit, re-run the
 |   converter. The settings are not guessable from the result, so the exact
 |   command is here:
 |     %(command)s
 |   Credit: %(source)s
 |   Reduced to %(channels)d monophonic voices on a 1/%(grid)d note grid --
 |   %(reduction)s -- and transposed %(shift)+d semitones so the lowest note
 |   is representable. Identical patterns are collapsed and referenced from the
 |   order list, which is where most of the size saving is.
 | Author: suinevere
 ----------------------*/
#include "music_synth_data.h"
#include "synth.h"

/*----------------------
 | MUSIC_CELLS
 | Description: %(patterns)d patterns of %(rows)d rows across %(channels)d channels
 |   (%(waves)s); %(drums)s. A note byte is a semitone index plus two; 0 holds
 |   and 1 keys off.
 | Author: suinevere
 ----------------------*/
static const TrackerCell MUSIC_CELLS[MUSIC_SYNTH_PATTERNS * MUSIC_SYNTH_ROWS * MUSIC_SYNTH_CHANNELS] = {
%(cells)s
};

/*----------------------
 | music_cells_length_check
 | Description: Fails the build if the initialiser above is not exactly the
 |   declared length. C pads a short initialiser with zeros, which would be a
 |   truncated tune that every runtime test still passes.
 | Author: suinevere
 ----------------------*/
typedef char music_cells_length_check[
    (sizeof(MUSIC_CELLS) / sizeof(MUSIC_CELLS[0])
     == MUSIC_SYNTH_PATTERNS * MUSIC_SYNTH_ROWS * MUSIC_SYNTH_CHANNELS) ? 1 : -1];

/*----------------------
 | MUSIC_ORDER
 | Description: The pattern play order, %(order_len)d entries long.
 | Author: suinevere
 ----------------------*/
static const unsigned char MUSIC_ORDER[] = {
    %(order)s
};

/*----------------------
 | MUSIC_SONG
 | Description: The song the tracker walks, at %(speed)d + %(frac)d/256 V-blanks
 |   per row -- the fraction is what keeps the tempo honest on a 60 Hz tick.
 | Author: suinevere
 ----------------------*/
static const TrackerSong MUSIC_SONG = {
    MUSIC_CELLS,
    MUSIC_SYNTH_ROWS,
    MUSIC_SYNTH_CHANNELS,
    MUSIC_ORDER,
    %(order_len)d,
    0,
    %(speed)d,
    %(frac)d
};

const TrackerSong *music_synth_song(void) {
    return &MUSIC_SONG;
}
'''

if __name__ == "__main__":
    sys.exit(main())
