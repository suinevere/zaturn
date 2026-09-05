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

Usage:
  python tools/assets/mid2pat.py IN.mid OUT.c --name "Title" --source "Credit"
"""
import argparse, struct, sys, pathlib

ROWS_PER_PATTERN = 16
CHANNELS = 4
DRUM_MIDI_CHANNEL = 9

# Waveform and volume per synth channel: bass on the triangle, lead on the
# pulse, inner part on the square, and the last channel on the chip's noise
# generator when the piece has drums (saw otherwise).
CH_WAVE_TONAL = [2, 1, 0, 3]
CH_WAVE_DRUMS = [2, 1, 0, 4]
CH_VOL = [6, 4, 3, 5]
WAVE_NAMES = ["SYNTH_WAVE_SQUARE", "SYNTH_WAVE_PULSE", "SYNTH_WAVE_TRIANGLE",
              "SYNTH_WAVE_SAW", "SYNTH_WAVE_NOISE"]

BASE_MIDI = 53
MIN_INDEX = 0
MAX_INDEX = (7 + 2) * 12 + 11
# Any note byte will do for the noise voice -- the synth ignores pitch when the
# waveform is the noise generator -- but it must be >= 2 to read as a key-on.
DRUM_NOTE = 26


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
    """Per row: the set of sounding pitches, and whether a drum was struck."""
    ticks_per_row = max(1, (division * 4) // grid)
    total = (max(e[0] for e in events) // ticks_per_row) + 1
    tonal, hits = [], []
    live, active = {}, set()
    idx = 0
    for row in range(total):
        limit = (row + 1) * ticks_per_row
        hit = False
        while idx < len(events) and events[idx][0] < limit:
            _, pitch, on, chan = events[idx]
            idx += 1
            if chan == DRUM_MIDI_CHANNEL:
                if on and drums:
                    hit = True
                continue
            if on:
                active.add(pitch)
                live[pitch] = live.get(pitch, 0) + 1
            else:
                live[pitch] = live.get(pitch, 0) - 1
                if live[pitch] <= 0:
                    active.discard(pitch)
        tonal.append(set(active))
        hits.append(hit)
    return tonal, hits


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("midi")
    ap.add_argument("out")
    ap.add_argument("--name", default="")
    ap.add_argument("--source", default="")
    ap.add_argument("--grid", type=int, default=16, help="notes per whole note")
    ap.add_argument("--speed", type=int, default=0,
                    help="whole V-blanks per row; 0 derives it from the tempo")
    ap.add_argument("--max-rows", type=int, default=0, help="0 = whole piece")
    ap.add_argument("--no-drums", action="store_true",
                    help="drop MIDI channel 10 instead of playing it as noise")
    args = ap.parse_args()

    division, tempo, events = read_midi(args.midi)
    if not events:
        raise SystemExit("no notes found")

    has_drums = (not args.no_drums
                 and any(e[3] == DRUM_MIDI_CHANNEL and e[2] for e in events))
    tonal_slots = CHANNELS - 1 if has_drums else CHANNELS
    ch_wave = CH_WAVE_DRUMS if has_drums else CH_WAVE_TONAL

    bpm = 60000000.0 / tempo
    frames = (60.0 / bpm) * (4.0 / args.grid) * 60.0
    if args.speed:
        speed, frac = args.speed, 0
    else:
        speed = max(1, int(frames))
        frac = int(round((frames - speed) * 256))
        if frac > 255:
            speed, frac = speed + 1, 0

    tonal, hits = grid_rows(division, events, args.grid, has_drums)
    if args.max_rows:
        tonal, hits = tonal[:args.max_rows], hits[:args.max_rows]
    while len(tonal) % ROWS_PER_PATTERN:
        tonal.append(set())
        hits.append(False)

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
    for pitches, hit in zip(tonal, hits):
        if len(pitches) > tonal_slots:
            thick += 1
        picked = assign(pitches, tonal_slots)
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
    fields = {
        "name": args.name or pathlib.Path(args.midi).stem,
        "source": args.source,
        "patterns": len(patterns),
        "rows": ROWS_PER_PATTERN,
        "channels": CHANNELS,
        "speed": speed,
        "frac": frac,
        "shift": shift,
        "grid": args.grid,
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
          % (bpm, frames, speed, frac, len(cells) * (speed + frac / 256.0) / 60.0))
    print("drums=%s  tonal voices=%d  transpose=%+d semitones"
          % ("noise voice" if has_drums else "none", tonal_slots, shift))
    print("rows with more tonal parts than channels: %d of %d" % (thick, len(cells)))
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
 | Description: %(name)s, converted from MIDI by tools/assets/mid2pat.py --
 |   do not hand-edit, re-run the converter.
 |   Credit: %(source)s
 |   Reduced to %(channels)d monophonic voices on a 1/%(grid)d note grid, keeping the
 |   outer parts at each row, and transposed %(shift)+d semitones so the lowest note
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
