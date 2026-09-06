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
import argparse, statistics, struct, sys, pathlib

ROWS_PER_PATTERN = 16
CHANNELS = 4
DRUM_MIDI_CHANNEL = 9
DRUM_SNARE_NOTE = 38          # GM acoustic snare, the "sharp tap" of the pair
# A drum tablature, when one is given, replaces the sequence's drum channel
# outright: one line a bar, four beats a bar, four slots a beat, and the
# characters h (closed hat), s (snare), k (kick) and . (rest). Slashes are
# decoration and are stripped, so a bar may be written "sss./h.ss/s.hh/.h.h"
# or as sixteen characters. A trailing "xN" repeats the bar. The drums are the
# one part of this arrangement not taken from the MIDI, because the fan
# sequence's drum channel plays about fifty strikes a statement more than the
# recording does and the owner is authoring the part by ear instead.
TAB_ROWS_PER_BEAT = 8
TAB_KINDS = {"h": "hat", "s": "snare", "k": "kick"}
# A slot written as three of the same letter is a triplet: three strikes on
# consecutive thirty-seconds rather than one on the sixteenth, which is the
# fast figure this arrangement is built on and the one the owner asked to keep.
# It runs half a slot into the next one, so the next slot has to be a rest.

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
# melody band at 11 against its 23. The drum came off full output when its
# envelope was slowed to the length of the original's: a strike that lasts fifty
# milliseconds instead of ten carries far more energy, and at 7 it buried the
# tune. The trim between this step and the next is not a register but the
# amplitude the noise table is written at; see NOISE_AMP in
# tools/assets/genwaves.py. Every level here is measured on the chip rather than
# in the model, and re-measured after anything that changes what a voice
# carries: this has now been swept six times, and every earlier answer was wrong
# for a reason that was not the level.
CH_VOL = [6, 5, 4, 6]
# A drum strike that does not land on an eighth note is emitted one DISDL step
# down. Measured, not stylistic: over the rows this arrangement strikes, the
# high-band flux of a recording of the NES original sits 8.0 dB below the eighth
# for a strike on the sixteenth between two of them and 9.4 dB below for one on a
# thirty-second, while ours sat at 0.3 and 1.7 -- flat. That is what "too much
# hi-hat" is. The sequence does carry the accent in its velocities, but only as
# 100 against 96 and 83, which is 1.6 dB and cannot survive a 6 dB step, so the
# accent is taken from the beat instead. One step is the nearest the chip has to
# the 8 dB measured; there is no per-strike trim finer than DISDL.
DRUM_UNACCENTED_DROP = 1
# The drum notes that ring rather than stop. Everything the sequence writes is
# played by one voice with one envelope, so an open hat and a crash sounded
# exactly like a closed hat -- the owner's report was that ours is "all one
# tone". These two keep the same waveform and rate and take the long envelope
# instead, which is the only thing the NES could have varied either: one noise
# channel, one period per hit, and a decay it can make as long as it likes.
# The two taps. This arrangement's recurring figure is three strikes carrying a
# closed hat and then one or two carrying only a snare -- "three bright taps
# then a quick sharp tap", as the owner put it -- and on a machine with one
# noise channel the difference between them is the rate the shift register is
# clocked at, which is what the sixteen NES periods are for. Measured on a
# recording of the original as the 8-16 kHz share against the 2-5 kHz share of
# each strike: a row carrying a hat reads 0.27 and a row carrying only a snare
# reads 0.15, while ours read 0.28 for both, which is the "all one tone" the
# owner heard. The snare-only rows are therefore struck at a lower note.
#
# How much lower was swept on the chip, scored inside 2-8 kHz where this
# engine's noise actually lives -- the 8-16 kHz band is mostly the pulse
# voices' harmonics here, because the shift register's own band ends at
# 7.8 kHz, and scoring there moved almost nothing. Separation of hat from
# snare, as centroid and as the 5-8 kHz share against the 2-3.5 kHz share:
# the original is +74 Hz and 1.13x; 0 / 3 / 6 / 10 semitones give -53 Hz 0.96x,
# -50 Hz 0.95x, -26 Hz 0.99x and +61 Hz 1.13x. The measured difference between
# the two taps is small -- this is one noise channel, not two drums.
DRUM_DARK_SEMITONES = 10
# A snare inside a fast figure -- a triplet or a double, anything with another
# strike on the row beside it -- is darkened less, so it keeps some crunch. Ten
# semitones down suits a snare standing alone and makes a triplet dull, which is
# what the owner heard: the fast figures are the ones carrying the character and
# they lose most by being taken off the top of the shift register's range.
DRUM_FAST_SEMITONES = 3
WAVE_NAMES = ["SYNTH_WAVE_PULSE12", "SYNTH_WAVE_PULSE25",
              "SYNTH_WAVE_TRIANGLE", "SYNTH_WAVE_PULSE50", "SYNTH_WAVE_NOISE"]
WAVE_NOISE = 4
WAVE_TRIANGLE = 2
# What separates a part carrying two lines from a part carrying one thickened.
# Measured over the catalogue: where a part sounds two notes at once, sglake and
# sgmirror hold them 23 to 36 semitones apart on every such row and sgbanqet 13
# to 19, while sgmirror's neighbours in the same files stay monophonic and the
# genuine chord parts -- shadow8's channel 1 at 5 to 6 semitones, sghalls'
# channel 6 at 10 -- never reach an octave at all. Twelve separates them with
# nothing near the line, and a majority is asked for so one stray overlap at a
# note-off cannot split a part that is really one line.
WIDE_PART_SEMITONES = 12
WIDE_PART_SHARE = 0.6
# Where a separated line's part number comes from. MIDI channels are 0 to 15,
# so 16 upward belongs to nobody and a separated line can carry a number that
# orders and compares like any other part's.
SPLIT_PART_BASE = 16

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
    raw = pathlib.Path(path).read_bytes()
    if raw[:4] != b"MThd":
        raise SystemExit("%s is not a MIDI file" % path)
    _, ntrk, division = struct.unpack(">HHH", raw[8:14])
    # A file may declare more than it holds. sgdragon.mid says eight tracks,
    # carries five, and gives the last of them a length that ends 290 bytes past
    # the end of the file. Two guards, because the declaration is wrong in two
    # ways: every track's end is clamped to what is really there, so the walk
    # stops on the file rather than on the claim, and the buffer is padded so an
    # event straddling that end reads zeros instead of raising. A tune missing
    # its last bars is worth more than no tune.
    have = len(raw)
    buf = raw + bytes(4)
    p = 14
    events = []
    tempo = 500000
    for _ in range(ntrk):
        if p >= have or buf[p:p + 4] != b"MTrk":
            break
        length = struct.unpack(">I", buf[p + 4:p + 8])[0]
        p += 8
        end = min(p + length, have)
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


def read_drum_tab(path, beats_per_bar=4):
    """Parse a drum tablature into one kind-or-None per thirty-second row.

    A beat is written as four slots (sixteenths) or as eight (thirty-seconds),
    and which one is in use is read from the group's own length, so a part with
    a fast figure in it can be written out literally rather than through a
    token. At four slots a beat, three of the same letter is one slot struck
    three times on consecutive thirty-seconds -- the triplet this arrangement
    is built on. Slashes are beat boundaries and are not optional at four slots
    a beat: a beat ending "hh" beside one starting "h" is otherwise three
    letters in a row and cannot be told from a triplet.
    """
    rows = []
    for raw in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        repeat = 1
        if "x" in line:
            body, _, count = line.rpartition("x")
            if count.strip().isdigit():
                line, repeat = body, int(count.strip())
        groups = [g for g in line.split("/") if g.strip()] if "/" in line else [line]
        if len(groups) != beats_per_bar:
            raise SystemExit("drum tab: %r has %d beats, need %d a bar"
                             % (raw.strip(), len(groups), beats_per_bar))
        bar = []
        for gi, group in enumerate(groups):
            chars = [c for c in group if c in TAB_KINDS or c == "."]
            beat = [None] * TAB_ROWS_PER_BEAT
            if len(chars) == TAB_ROWS_PER_BEAT:
                for i, c in enumerate(chars):
                    beat[i] = TAB_KINDS.get(c)
            else:
                slots, i = [], 0
                while i < len(chars):
                    c = chars[i]
                    if c in TAB_KINDS and chars[i:i + 3] == [c, c, c]:
                        slots.append((TAB_KINDS[c], 3)); i += 3
                    else:
                        slots.append((TAB_KINDS.get(c), 1)); i += 1
                if len(slots) != TAB_ROWS_PER_BEAT // 2:
                    raise SystemExit(
                        "drum tab: %r beat %d reads as %d slots, need %d "
                        "(a triplet counts as one) or %d written out"
                        % (raw.strip(), gi + 1, len(slots),
                           TAB_ROWS_PER_BEAT // 2, TAB_ROWS_PER_BEAT))
                for j, (kind, span) in enumerate(slots):
                    if kind is None:
                        continue
                    for k in range(span):
                        r = j * 2 + k
                        if r < TAB_ROWS_PER_BEAT:
                            beat[r] = kind
                        # A triplet in the last slot runs past the beat; it is
                        # truncated rather than refused, because a bar may well
                        # end on one and whatever the next beat opens with
                        # should win.
            bar.extend(beat)
        want = beats_per_bar * TAB_ROWS_PER_BEAT
        bar = bar[:want]
        for _ in range(repeat):
            rows.extend(bar)
    return rows


def tab_hits(rows_of_kinds, rows, grid):
    """The tablature is already one entry a thirty-second, which is the grid the
    converter runs on; anything coarser gets the strike on its first row."""
    step = max(1, 32 // grid)
    note = {"snare": DRUM_SNARE_NOTE, "hat": 42, "kick": 36}
    out = []
    for r in range(rows):
        src = r * step
        kind = rows_of_kinds[src] if src < len(rows_of_kinds) else None
        out.append({note[kind]} if kind else set())
    return out


def grid_rows(division, events, grid, drums):
    """Per row: what each MIDI channel is sounding, and whether a drum was hit.

    Kept per channel rather than merged into one set, because which part a note
    belongs to is information the merge throws away and the octave fold needs:
    a bass pedalling on G3 under a melody on G4 is two parts an octave apart,
    not one part doubled. A row's drums come back as the set of drum notes on
    it rather than a flag, because open hat and crash are struck differently
    from the rest; an empty set is falsy, so a caller that only asks whether
    the row is struck reads the same as before.
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
        hit = set()
        while idx < len(events) and events[idx][0] < limit:
            _, pitch, on, chan = events[idx]
            idx += 1
            if chan == DRUM_MIDI_CHANNEL:
                if on and drums:
                    hit.add(pitch)
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


def separate_wide_parts(parts):
    """/*----------------------
     | separate_wide_parts
     | Description: Give each line of a part that is really two a part of its
     |     own. A sequencer writing a melody and a bass on one MIDI channel
     |     leaves a part that sounds two notes at once, which plan_parts cannot
     |     follow; the members are two or three octaves apart and which of them
     |     a row's lowest note is depends on whether the bass is resting, so
     |     the pitch-order fallback puts the melody on the bass voice whenever
     |     it is. In sglake.mid that reached 1228 Hz on the NES triangle, whose
     |     32-step staircase lives in a 256-sample table and has 1.1 samples a
     |     stair at that pitch -- no staircase, and aliasing in its place.
     |
     |     The split is at the midpoint of the two registers, taken from the
     |     medians of the members rather than from any one row, so a line that
     |     crosses briefly does not change part halfway through a phrase.
     |
     |     Must run AFTER fold_octaves. A raw sequence doubling its bass at the
     |     octave looks exactly like two lines on one channel to this, and
     |     castle-halls -- which every voicing constant was swept against --
     |     is one: separated before the fold, its triangle goes from eleven
     |     semitones to forty-six.
     | Author: suinevere
     | Dependencies: statistics
     | Globals: WIDE_PART_SEMITONES, WIDE_PART_SHARE, SPLIT_PART_BASE
     | Params: parts -- one dict of part number to pitch set per row
     | Returns: new rows, or the same object when nothing wanted separating
     ----------------------*/"""
    cuts = {}
    for chan in {c for row in parts for c in row}:
        gaps = [max(p) - min(p) for row in parts
                for p in (row.get(chan) or (),) if len(p) > 1]
        if not gaps:
            continue
        wide = [g for g in gaps if g >= WIDE_PART_SEMITONES]
        if len(wide) < WIDE_PART_SHARE * len(gaps):
            continue
        lows = [min(p) for row in parts
                for p in (row.get(chan) or (),) if len(p) > 1]
        highs = [max(p) for row in parts
                 for p in (row.get(chan) or (),) if len(p) > 1]
        cuts[chan] = (statistics.median(lows) + statistics.median(highs)) / 2.0
    if not cuts:
        return parts
    out = []
    for row in parts:
        made = {}
        for chan, pitches in row.items():
            if chan not in cuts:
                made[chan] = pitches
                continue
            lower = {p for p in pitches if p <= cuts[chan]}
            upper = {p for p in pitches if p > cuts[chan]}
            if lower:
                made[chan] = lower
            if upper:
                made[chan + SPLIT_PART_BASE] = upper
        out.append(made)
    return out


def plan_lanes(parts, slots):
    """/*----------------------
     | plan_lanes
     | Description: The parts to reduce from and the lane plan for them. One
     |     call rather than two because the repair changes both: a part that
     |     cannot be planned may be two lines written on one channel, and
     |     separating them is only worth doing if the result can then be
     |     planned -- so the parts a tune is converted from depend on whether
     |     the plan succeeded.
     |
     |     The repair is attempted only where the plan already failed. Applied
     |     unconditionally it damages tunes whose parts were fine, and one of
     |     them is the tune every voicing constant was measured against.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: parts -- one dict per row; slots -- tonal voices available
     | Returns: (parts to use, lane plan or None)
     ----------------------*/"""
    lanes = plan_parts(parts, slots)
    if lanes is not None:
        return parts, lanes
    separated = separate_wide_parts(parts)
    if separated is parts:
        return parts, None
    lanes = plan_parts(separated, slots)
    return (separated, lanes) if lanes is not None else (parts, None)


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
            bpm_override=0.0, fold="off", drum_tab=None, tab_beats=4,
            octaves=None):
    """Run the whole reduction and return everything both callers need.

    The offline preview renders from this, and the build emits C from it, so
    what is heard in a second is what the disc will play. They used to each
    walk the events themselves, which is a drift the preview cannot report.
    """
    division, tempo, events = read_midi(midi)
    if not events:
        raise SystemExit("no notes found")

    has_drums = (not no_drums
                 and (bool(drum_tab)
                      or any(e[3] == DRUM_MIDI_CHANNEL and e[2] for e in events)))
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

    parts, hits = grid_rows(division, events, grid, has_drums and not drum_tab)
    parts = [{c: fold_octaves(p, fold) for c, p in row.items()} for row in parts]
    if max_rows:
        parts, hits = parts[:max_rows], hits[:max_rows]
    while len(parts) % ROWS_PER_PATTERN:
        parts.append({})
        hits.append(set())
    if drum_tab:
        hits = tab_hits(read_drum_tab(drum_tab, tab_beats), len(parts), grid)
    parts, lanes = plan_lanes(parts, tonal_slots)
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
    # Per-lane octave corrections, from the manifest. shift above moves the
    # whole tune to fit the note range and is arithmetic; this moves one voice
    # because a fan sequence wrote that voice in the wrong octave, and it is
    # measured against a recording of the NES original by tools/voicecmp.py.
    # Tonal lanes only: the percussion lane's note is the shift register's
    # clock rate and is calibrated at DRUM_NOTE, so moving it is not a
    # transposition but a different drum.
    lane_oct = [0] * CHANNELS
    for ch in range(min(tonal_slots, len(octaves or ()))):
        lane_oct[ch] = 12 * int(octaves[ch])

    cells = []
    previous = [None] * CHANNELS
    thick = 0
    for r, (row, pitches, hit) in enumerate(zip(parts, tonal, hits)):
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
                index = (pitch + shift + lane_oct[ch] - BASE_MIDI) + 24
                index = max(MIN_INDEX, min(MAX_INDEX, index))
                row.append((index + 2, (ch_wave[ch] << 4) | CH_VOL[ch]))
            else:
                row.append((0, 0))
            previous[ch] = pitch
        if has_drums:
            last = CHANNELS - 1
            # A strike with another beside it is part of a fast run -- a triplet
            # or a double -- and is treated differently twice over. It keeps the
            # accent, because the beat rule exists for a lone strike off the beat
            # and applied to a triplet it takes the second and third strikes 6 dB
            # down and the figure stops reading as a triplet at all: measured
            # against the recording, the original's runs 2.73 / 1.93 / 1.19 where
            # ours ran 2.52 / 0.67 / 1.08. And it is darkened less, because ten
            # semitones suits a snare standing alone and makes a fast figure dull.
            fast = ((r + 1 < len(hits) and hits[r + 1])
                    or (r > 0 and hits[r - 1]))
            on_beat = fast or (r % max(1, grid // 8)) == 0
            vol = CH_VOL[last] - (0 if on_beat else DRUM_UNACCENTED_DROP)
            vol = max(0, min(7, vol))
            snare = hit and not (hit - {DRUM_SNARE_NOTE})
            drop = (DRUM_FAST_SEMITONES if fast else DRUM_DARK_SEMITONES) if snare else 0
            note = DRUM_NOTE - drop
            row.append((note, (ch_wave[last] << 4) | vol)
                       if hit else (0, 0))
        cells.append(row)

    return {"cells": cells, "speed": speed, "frac": frac, "bpm": bpm,
            "frames": frames,
            "has_drums": has_drums, "tonal_slots": tonal_slots,
            "lanes": lanes, "echoes": echoes,
            "ch_wave": ch_wave, "shift": shift, "thick": thick,
            "octaves": [o // 12 for o in lane_oct]}


def pack_patterns(cells):
    """/*----------------------
     | pack_patterns
     | Description: Collapses identical sixteen-row blocks and returns the
     |     unique ones plus the order list that references them. This is where
     |     most of the size saving on repeating music comes from -- the
     |     entryway theme's 384 rows are 20 patterns and 24 order entries -- and
     |     it is a function rather than a loop inside the emitter because the
     |     manifest path packs twelve tunes with it.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: ROWS_PER_PATTERN
     | Params: cells -- rows of per-channel (note, wv) pairs
     | Returns: (patterns, order)
     ----------------------*/"""
    patterns, order, seen = [], [], {}
    for i in range(0, len(cells), ROWS_PER_PATTERN):
        block = tuple(tuple(c) for r in cells[i:i + ROWS_PER_PATTERN] for c in r)
        if block not in seen:
            seen[block] = len(patterns)
            patterns.append(block)
        order.append(seen[block])
    return patterns, order


def check_length(patterns, order, what=""):
    """/*----------------------
     | check_length
     | Description: Refuses a tune the tracker cannot address. Both counts are
     |     unsigned char in TrackerSong, so 255 is the ceiling and exceeding it
     |     would wrap silently into a song that plays the wrong patterns rather
     |     than one that fails to build.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: patterns, order -- pack_patterns' two returns; what -- named in
     |     the message, since the manifest path converts twelve files
     | Returns: N/A
     ----------------------*/"""
    if len(patterns) > 255 or len(order) > 255:
        raise SystemExit("%s too long: %d patterns / %d order entries (max 255 "
                         "each) -- use a coarser grid or a max_rows"
                         % (what or "tune", len(patterns), len(order)))


def add_shared_arguments(ap):
    """The options that change the conversion, so both entry points take them."""
    ap.add_argument("--grid", type=int, default=16, help="notes per whole note")
    ap.add_argument("--bpm", type=float, default=0.0,
                    help="override the file's tempo; 0 uses what it declares")
    ap.add_argument("--max-rows", type=int, default=0, help="0 = whole piece")
    ap.add_argument("--drums-tab",
                    help="a drum tablature to play instead of the sequence's "
                         "drum channel; one line a bar, beats separated by /")
    ap.add_argument("--tab-beats", type=int, default=4,
                    help="beats in a tablature bar: 3 for 3/4, 4 for 4/4")
    ap.add_argument("--no-drums", action="store_true",
                    help="drop MIDI channel 10 instead of playing it as noise")
    ap.add_argument("--fold-octaves", choices=("off", "up", "down"), default="off",
                    help="collapse exact octave doublings, keeping the upper "
                         "member (up) or the lower (down)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("midi", nargs="?")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--manifest",
                    help="a songs.json; converts every tune in it into one "
                         "file, and takes each tune's settings from there "
                         "rather than from the options below")
    ap.add_argument("--out", dest="out_opt",
                    help="where --manifest writes; the .h goes beside it")
    ap.add_argument("--pat",
                    help="also write the whole catalogue as a disc file, which "
                         "is how the CD build carries tunes it has no heap for")
    ap.add_argument("--name", default="")
    ap.add_argument("--source", default="")
    ap.add_argument("--speed", type=int, default=0,
                    help="whole V-blanks per row; 0 derives it from the tempo")
    add_shared_arguments(ap)
    args = ap.parse_args()

    if args.manifest:
        if not args.out_opt:
            raise SystemExit("--manifest needs --out")
        return emit_manifest(args.manifest, args.out_opt, args.pat)
    if not args.midi or not args.out:
        raise SystemExit("give a MIDI file and an output, or --manifest and --out")

    song = convert(args.midi, args.grid, args.speed, args.max_rows,
                   args.no_drums, args.bpm, args.fold_octaves, args.drums_tab,
                   args.tab_beats)
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

    patterns, order = pack_patterns(cells)
    check_length(patterns, order, args.midi)

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


def convert_song(record):
    """/*----------------------
     | convert_song
     | Description: One tune, converted with every setting its manifest record
     |     carries. Four callers used to spell the same eight arguments out --
     |     the emitter, the preview, the mood measurement and the tests -- and a
     |     field added to the manifest reached whichever of them was remembered.
     |     This is the one spelling.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: record -- a settings dict from load_manifest
     | Returns: what convert returns
     ----------------------*/"""
    return convert(record["midi"], grid=record["grid"], speed_arg=0,
                   max_rows=record["max_rows"], no_drums=False,
                   bpm_override=record["bpm"], fold=record["fold"],
                   drum_tab=record["drums_tab"], tab_beats=record["tab_beats"],
                   octaves=record.get("octaves"))


def _octaves(entry):
    """/*----------------------
     | _octaves
     | Description: One whole-octave correction per tonal lane, from a
     |     manifest entry. A fan sequence writing a voice in the wrong octave
     |     is a fault of that sequence and not of the conversion, so it is
     |     recorded per tune beside grid and bpm rather than guessed at
     |     conversion time. Written short: [1] moves only the bass.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: CHANNELS
     | Params: entry -- one songs.json record
     | Returns: a list of CHANNELS ints, each an octave count
     ----------------------*/"""
    got = entry.get("octaves") or []
    if not isinstance(got, list) or len(got) > CHANNELS:
        raise SystemExit("%s: octaves must be a list of at most %d numbers"
                         % (entry.get("id"), CHANNELS))
    out = []
    for v in got:
        if not isinstance(v, int) or isinstance(v, bool) or abs(v) > 3:
            raise SystemExit("%s: an octave correction is a whole number of "
                             "octaves within +/-3, not %r" % (entry.get("id"), v))
        out.append(v)
    return out + [0] * (CHANNELS - len(out))


def load_manifest(path):
    """/*----------------------
     | load_manifest
     | Description: Reads songs.json and fills in every default, so the emitter
     |     below sees one complete record per tune and the manifest can stay as
     |     short as an id, a name and a file. Paths inside it are relative to
     |     the manifest's own folder, which is what makes the file movable.
     | Author: suinevere
     | Dependencies: json
     | Globals: N/A
     | Params: path -- the songs.json
     | Returns: (list of settings dicts, index of the default tune)
     ----------------------*/"""
    import json
    root = pathlib.Path(path).resolve().parent
    doc = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    songs = []
    for s in doc["songs"]:
        midi = root / s["midi"]
        if not midi.exists():
            raise SystemExit("%s: no such MIDI file" % midi)
        tab = s.get("drums_tab")
        songs.append({
            "id": s["id"],
            "name": s.get("name") or s["id"],
            "source": s.get("source") or ("%s, a fan sequence of Shadowgate "
                                          "(NES, 1989), music by Hiroyuki Masuno"
                                          % s["midi"]),
            "midi": str(midi),
            "grid": int(s.get("grid", 32)),
            "bpm": float(s.get("bpm", 0.0)),
            "fold": s.get("fold", "off"),
            "octaves": _octaves(s),
            "max_rows": int(s.get("max_rows", 0)),
            "drums_tab": str(root / tab) if tab else None,
            "tab_beats": int(s.get("tab_beats", 4)),
            "cd": bool(s.get("cd", False)),
        })
    ids = [s["id"] for s in songs]
    if len(set(ids)) != len(ids):
        raise SystemExit("two songs share an id: %s" % ids)
    want = doc.get("default", ids[0])
    if want not in ids:
        raise SystemExit("default '%s' is not one of the songs" % want)
    # The tunes the CD build carries come first, so that build can be the
    # netbin's catalogue cut short at a number rather than a different table --
    # every offset below is a prefix of the same two arrays. Stable within each
    # group, so the manifest's order is still the order the ids are numbered in
    # as long as nothing moves between the groups.
    songs.sort(key=lambda s: not s["cd"])
    ids = [s["id"] for s in songs]
    if not songs[0]["cd"]:
        raise SystemExit("no song is marked \"cd\": true -- the CD build needs "
                         "at least the default one")
    if not songs[ids.index(want)]["cd"]:
        raise SystemExit("the default '%s' is not marked \"cd\": true, so the "
                         "CD build would not carry it" % want)
    return songs, ids.index(want)


def load_track_map(manifest_path, ids, default_index):
    """/*----------------------
     | load_track_map
     | Description: Reads the CD-DA track to tune table that
     |     tools/gen_synth_moods.py measures, from track_songs.json beside the
     |     manifest. Missing file means every track falls to the default tune,
     |     which is what the build did before there was more than one -- so the
     |     mapping is an improvement on the fallback rather than a requirement
     |     of it, and a fresh checkout builds without running the measurement.
     | Author: suinevere
     | Dependencies: json
     | Globals: N/A
     | Params: manifest_path -- the songs.json; ids -- song ids in emit order;
     |     default_index -- what an unmapped track plays
     | Returns: (min_track, max_track, [song index per track])
     ----------------------*/"""
    import json
    p = pathlib.Path(manifest_path).resolve().parent / "track_songs.json"
    if not p.exists():
        return 2, 32, [default_index] * 31
    doc = json.loads(p.read_text(encoding="utf-8"))
    lo, hi = int(doc.get("min_track", 2)), int(doc.get("max_track", 32))
    table = []
    for t in range(lo, hi + 1):
        want = doc.get("tracks", {}).get(str(t))
        if want is None:
            table.append(default_index)
        elif want not in ids:
            raise SystemExit("track %d maps to '%s', which is not a song" % (t, want))
        else:
            table.append(ids.index(want))
    return lo, hi, table


PAT_MAGIC = b"PAT\x1a"
PAT_VERSION = 1
PAT_SECTOR = 2048
PAT_HEADER_BYTES = 512
PAT_DIR_ENTRY = 12


def song_record(entry):
    """/*----------------------
     | song_record
     | Description: One tune as the bytes the console loads into its slot:
     |     a two-word count, then the cells, then the order list. Self-describing
     |     because the record is read on its own, sectors away from the directory
     |     that pointed at it, and a record that had to be trusted to match a
     |     directory entry would fail silently rather than loudly.
     |
     |     Big-endian, because the SH-2 is, and the console casts these bytes to
     |     TrackerCell in place rather than copying them out.
     | Author: suinevere
     | Dependencies: struct
     | Globals: ROWS_PER_PATTERN, CHANNELS
     | Params: entry -- an emit_manifest entry carrying blocks and order_list
     | Returns: bytes, padded to a whole number of sectors
     ----------------------*/"""
    blocks, order = entry["blocks"], entry["order_list"]
    out = bytearray()
    out += struct.pack(">HH", len(blocks), len(order))
    for block in blocks:
        for cell in block:
            out += struct.pack(">B", entry["pair_index"][cell])
    out += bytes(order)
    while len(out) % PAT_SECTOR:
        out += b"\x00"
    return bytes(out)


def write_pat(path, entries, default_index, track_min, track_song):
    """/*----------------------
     | write_pat
     | Description: Writes MUSIC.PAT -- the whole catalogue as a disc file, so
     |     the CD build can carry twelve tunes without carrying them in .rodata.
     |     That distinction is the entire point: on that target __heap_start
     |     follows .rodata, so a linked tune is taken straight out of the heap
     |     the story loads into, and a tune on the disc is not.
     |
     |     One header sector, then one record per tune, each starting on a sector
     |     of its own. Sector alignment is not tidiness: SRL's LoadBytes takes a
     |     SECTOR offset, not a byte offset, so a record that did not start on
     |     one could not be read without reading everything before it.
     |
     |     The header carries the directory, the track-to-tune table and the ids,
     |     so the menu can list every tune and the engine can answer for every
     |     track with only the first sector resident.
     | Author: suinevere
     | Dependencies: struct, pathlib
     | Globals: PAT_*
     | Params: path -- where to write; entries -- emit_manifest's entries;
     |     default_index -- the tune played when nothing chose one; track_min --
     |     the first CD-DA track the table answers for; track_song -- one tune
     |     index per track
     | Returns: (file bytes, largest record bytes)
     ----------------------*/"""
    records = [song_record(e) for e in entries]
    sectors, at = [], 1
    for r in records:
        sectors.append(at)
        at += len(r) // PAT_SECTOR

    ids = bytearray()
    id_rel = []
    for e in entries:
        id_rel.append(len(ids))
        ids += e["id"].encode("ascii") + b"\x00"

    slot = max(len(r) for r in records)
    directory = bytearray()
    for i, e in enumerate(entries):
        # The record's real length, not its padded one: the console reads whole
        # sectors but only the leading bytes mean anything, and the slot is
        # sized from the padded maximum below.
        real = 4 + len(e["blocks"]) * ROWS_PER_PATTERN * CHANNELS + e["order_len"]
        directory += struct.pack(">HHHBBBB", sectors[i], len(records[i]) // PAT_SECTOR,
                                 real, e["order_len"], 0, e["speed"], e["frac"])
        directory += struct.pack(">H", id_rel[i])

    tracks = bytes(track_song)
    # 4 magic + seven words of shape + five of layout. Written out rather than
    # measured because the directory offset has to be inside the header that
    # names it; the assertion below is what keeps the two honest.
    dir_off = 28
    trk_off = dir_off + len(directory)
    id_off = trk_off + len(tracks)
    head = bytearray()
    head += PAT_MAGIC
    head += struct.pack(">HHHHHHH", PAT_VERSION, len(entries), ROWS_PER_PATTERN,
                        CHANNELS, default_index, track_min,
                        track_min + len(tracks) - 1)
    head += struct.pack(">HHHHH", slot, dir_off, trk_off, id_off, 0)
    if len(head) != dir_off:
        raise SystemExit("PAT header is %d bytes, not the declared %d"
                         % (len(head), dir_off))
    head += directory + tracks + ids
    if len(head) > PAT_HEADER_BYTES:
        raise SystemExit("PAT header sector is %d bytes, over the %d the console "
                         "reads -- fewer tunes, or shorter ids"
                         % (len(head), PAT_HEADER_BYTES))
    head += b"\x00" * (PAT_SECTOR - len(head))

    pathlib.Path(path).write_bytes(bytes(head) + b"".join(records))
    return PAT_SECTOR + sum(len(r) for r in records), slot


def order_text(order_all, cut):
    """/*----------------------
     | order_text
     | Description: The order list as C, cut where the CD build's copy ends. The
     |     cut has to be written out here rather than left to the chunking,
     |     because sixteen entries to a line will not in general land on the
     |     boundary and a #if in the middle of an initialiser line does not
     |     compile.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: order_all -- every tune's order entries; cut -- how many of them
     |     the CD build carries
     | Returns: the initialiser body
     ----------------------*/"""
    def rows(seq):
        return [", ".join("%d" % o for o in seq[i:i + 16])
                for i in range(0, len(seq), 16)]
    head = rows(order_all[:cut])
    tail = rows(order_all[cut:])
    if not tail:
        return ",\n    ".join(head)
    return (",\n    ".join(head) + ",\n"
            "#if MUSIC_SYNTH_SONGS > MUSIC_SYNTH_SONGS_CD\n    "
            + ",\n    ".join(tail) + "\n#endif")


def emit_manifest(manifest_path, out_path, pat_path=None):
    """/*----------------------
     | emit_manifest
     | Description: Converts every tune in the manifest and writes them into one
     |     music_synth_data.c. The cells and the order lists of all of them live
     |     in two flat arrays and each song points at its own offset inside
     |     them, rather than each tune getting arrays of its own: TrackerSong
     |     holds a pointer, so an offset base costs nothing at run time, and it
     |     leaves two length assertions to write instead of two per tune.
     | Author: suinevere
     | Dependencies: json, pathlib
     | Globals: ROWS_PER_PATTERN, CHANNELS, WAVE_NAMES
     | Params: manifest_path -- the songs.json; out_path -- the .c to write,
     |     whose .h is written beside it
     | Returns: 0
     ----------------------*/"""
    songs, default_index = load_manifest(manifest_path)
    cd_songs = sum(1 for s in songs if s["cd"])
    cell_lines, order_all, entries = [], [], []
    cell_off = 0
    cd_cells = cd_order = 0

    # Converted first, emitted second, because a cell is written as an index
    # into a table of every distinct (note, wv) pair in the whole catalogue --
    # and that table cannot be known until the last tune has been converted.
    converted = []
    for s in songs:
        got = convert_song(s)
        patterns, order = pack_patterns(got["cells"])
        check_length(patterns, order, s["id"])
        converted.append((got, patterns, order))

    pairs, pair_index = [], {}
    for _, patterns, _ in converted:
        for block in patterns:
            for cell in block:
                if cell not in pair_index:
                    pair_index[cell] = len(pairs)
                    pairs.append(cell)
    if len(pairs) > 256:
        raise SystemExit(
            "the catalogue uses %d distinct (note, wv) pairs and a cell is one "
            "byte, so at most 256 can be named. Nothing here truncates a tune "
            "to fit: either drop one from the manifest or widen TrackerCell in "
            "saturn/src/sound/tracker.h and this emitter together."
            % len(pairs))

    for n_song, s in enumerate(songs):
        got, patterns, order = converted[n_song]
        # Where the CD build's copy of each array ends. Everything past here is
        # inside the #if below, and both builds read the same prefix.
        if n_song == cd_songs:
            cd_cells, cd_order = cell_off, len(order_all)
            cell_lines.append("#if MUSIC_SYNTH_SONGS > MUSIC_SYNTH_SONGS_CD")
        cell_lines.append("    /* %s -- %d patterns of %d rows, from cell %d */"
                          % (s["id"], len(patterns), ROWS_PER_PATTERN, cell_off))
        for n, block in enumerate(patterns):
            cell_lines.append("    /* %s pattern %d */" % (s["id"], n))
            for r in range(ROWS_PER_PATTERN):
                row = block[r * CHANNELS:(r + 1) * CHANNELS]
                cell_lines.append("    " + " ".join("%3d," % pair_index[c]
                                                    for c in row))
        seconds = len(got["cells"]) * (got["speed"] + got["frac"] / 256.0) / 60.0
        entries.append({
            "id": s["id"], "name": s["name"], "source": s["source"],
            "cell_off": cell_off, "order_off": len(order_all),
            "order_len": len(order), "patterns": len(patterns),
            "speed": got["speed"], "frac": got["frac"], "seconds": seconds,
            "waves": ", ".join(WAVE_NAMES[w] for w in got["ch_wave"]),
            "drums": "drums" if got["has_drums"] else "no drums",
            "rows": len(got["cells"]),
            "blocks": patterns, "order_list": order,
            "pair_index": pair_index,
        })
        cell_off += len(patterns) * ROWS_PER_PATTERN * CHANNELS
        order_all.extend(order)
    if cd_songs == len(songs):
        cd_cells, cd_order = cell_off, len(order_all)
    else:
        cell_lines.append("#endif")

    lo, hi, track_song = load_track_map(manifest_path,
                                        [e["id"] for e in entries], default_index)

    song_rows, name_rows, id_rows = [], [], []
    for i, e in enumerate(entries):
        if i == cd_songs and cd_songs != len(entries):
            song_rows.append("#if MUSIC_SYNTH_SONGS > MUSIC_SYNTH_SONGS_CD")
            name_rows.append("#if MUSIC_SYNTH_SONGS > MUSIC_SYNTH_SONGS_CD")
            id_rows.append("#if MUSIC_SYNTH_SONGS > MUSIC_SYNTH_SONGS_CD")
        song_rows.append("    /* %d %-12s %3d patterns, %3d order, %5.1f s, %s */"
                         % (i, e["id"] + ":", e["patterns"], e["order_len"],
                            e["seconds"], e["drums"]))
        song_rows.append("    { MUSIC_CELLS + %6d, MUSIC_PAIRS, MUSIC_SYNTH_ROWS, "
                         "MUSIC_SYNTH_CHANNELS, MUSIC_ORDER + %4d, %3d, 0, %3d, %3d },"
                         % (e["cell_off"], e["order_off"], e["order_len"],
                            e["speed"], e["frac"]))
        name_rows.append('    "%s",' % e["name"])
        id_rows.append('    "%s",' % e["id"])
    if cd_songs != len(entries):
        song_rows.append("#endif")
        name_rows.append("#endif")
        id_rows.append("#endif")

    catalogue = "\n".join(" |     %-4s %-12s %-26s %s"
                          % ("disc" if songs[i]["cd"] else "net",
                             e["id"], e["name"], e["source"])
                          for i, e in enumerate(entries))
    fields = {
        # Relative, and deliberately not the paths this run was given: a
        # regeneration launched from a .bat passes absolute ones, and writing
        # those into the file makes every rebuild on another machine a diff.
        "command": ("tools/assets/mid2pat.py --manifest "
                    "tools/assets/music/songs.json "
                    "--out saturn/src/sound/music_synth_data.c"),
        "songs": len(entries),
        "cells": "\n".join(cell_lines),
        "cell_total": cell_off,
        "pair_total": len(pairs),
        "pairs": "\n".join(
            "    " + " ".join("{ %3d, 0x%02X }," % (n, w)
                              for n, w in pairs[i:i + 6])
            for i in range(0, len(pairs), 6)),
        "order": order_text(order_all, cd_order),
        "order_total": len(order_all),
        "rows": ROWS_PER_PATTERN,
        "channels": CHANNELS,
        "default": default_index,
        "default_id": entries[default_index]["id"],
        "default_name": entries[default_index]["name"],
        "song_table": "\n".join(song_rows),
        "names": "\n".join(name_rows),
        "ids": "\n".join(id_rows),
        "id_width": max(len(e["id"]) for e in entries),
        "catalogue": catalogue,
        "songs_cd": cd_songs,
        "cells_cd": cd_cells,
        "order_cd": cd_order,
        "track_min": lo,
        "track_max": hi,
        "track_count": hi - lo + 1,
        "track_table": ",\n    ".join(
            ", ".join("%d" % t for t in track_song[i:i + 16])
            for i in range(0, len(track_song), 16)),
    }

    pat_bytes, slot = 0, 0
    if pat_path:
        pat_bytes, slot = write_pat(pat_path, entries, default_index, lo, track_song)
    fields["pat_slot"] = slot
    fields["pat_header"] = PAT_HEADER_BYTES
    fields["pat_sector"] = PAT_SECTOR
    fields["pat_name"] = pathlib.Path(pat_path).name if pat_path else "MUSIC.PAT"

    out = pathlib.Path(out_path)
    out.write_text(MANIFEST_TEMPLATE % fields, encoding="utf-8")
    out.with_suffix(".h").write_text(MANIFEST_HEADER % fields, encoding="utf-8")

    def image_bytes(cells, order, count):
        return cells + len(pairs) * 2 + order + count * 12 + len(track_song)
    for i, e in enumerate(entries):
        print("%-4s %-12s rows=%5d patterns=%3d order=%3d %6.1f s  %s"
              % ("disc" if songs[i]["cd"] else "net", e["id"], e["rows"],
                 e["patterns"], e["order_len"], e["seconds"], e["drums"]))
    print("netbin: %d songs, %d cells, %d order entries -- about %d bytes of image"
          % (len(entries), cell_off, len(order_all),
             image_bytes(cell_off, len(order_all), len(entries))))
    print("CD:     %d songs, %d cells, %d order entries -- about %d bytes of image"
          % (cd_songs, cd_cells, cd_order,
             image_bytes(cd_cells, cd_order, cd_songs)))
    print("default is %s; CD-DA tracks %d-%d mapped to %d distinct tunes"
          % (entries[default_index]["id"], lo, hi, len(set(track_song))))
    print("wrote %s and %s" % (out, out.with_suffix(".h")))
    if pat_path:
        print("wrote %s -- %d bytes on the disc, largest tune %d, so the CD "
              "build holds %d bytes of Low Work RAM and no heap"
              % (pat_path, pat_bytes, slot, PAT_HEADER_BYTES + slot))
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

MANIFEST_HEADER = '''/*----------------------
 | music_synth_data.h
 | Description: The tunes the synth can play, in both builds. Data only: the
 |   engine reads it and never reaches back. Generated by
 |   tools/assets/mid2pat.py from tools/assets/music/songs.json -- do not
 |   hand-edit, because the .c asserts its own array lengths against the
 |   numbers here.
 |
 |   The catalogue, in emit order:
%(catalogue)s
 |
 |   Default: %(default_name)s.
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
 | MUSIC_SYNTH_SONGS_CD
 | Description: How many tunes the CD build carries, which is fewer, and why
 |   there are two counts at all.
 |
 |   The catalogue is pattern data in .rodata, and on the CD target
 |   __heap_start follows .rodata: every byte of music comes straight off the
 |   HWRAM heap the story is loaded into. Measured, the full catalogue is 55,424
 |   bytes and takes that heap to 98,016, which is below the 129,704 bytes
 |   LURKING.Z3 needs to load at all -- the disc's largest story simply stops
 |   working. The netbin loads no story and has 130 KB of its own image budget
 |   spare, so it carries all of them.
 |
 |   The CD build's tunes are the first MUSIC_SYNTH_SONGS_CD of the catalogue,
 |   never a scattered subset, so both builds read the same prefix of the same
 |   two arrays and a song index means the same thing in each. Which tunes those
 |   are is the "cd": true flag in songs.json.
 |
 |   This costs the CD build nothing it had: the synth is its fallback for a
 |   disc that carries no CD-DA, and a disc that does carry it has thirty-one
 |   real tracks and never reaches here.
 | Author: suinevere
 ----------------------*/
#define MUSIC_SYNTH_SONGS_CD %(songs_cd)d

/*----------------------
 | MUSIC_SYNTH_SONGS / MUSIC_SYNTH_ROWS / MUSIC_SYNTH_CHANNELS
 | Description: How many tunes this build has and the shape of a pattern. Every
 |   tune shares the row and channel counts, because the tracker reads them off
 |   the song it is given and a tune with a shape of its own would only mean a
 |   second set of numbers to keep in step.
 | Author: suinevere
 ----------------------*/
#ifdef NETBIN
#define MUSIC_SYNTH_SONGS    %(songs)d
#else
#define MUSIC_SYNTH_SONGS    MUSIC_SYNTH_SONGS_CD
#endif
#define MUSIC_SYNTH_ROWS     %(rows)d
#define MUSIC_SYNTH_CHANNELS %(channels)d

/*----------------------
 | MUSIC_SYNTH_CELLS / MUSIC_SYNTH_ORDER
 | Description: The lengths of the two flat arrays every tune points into. They
 |   are here rather than only in the .c because the file asserts its own
 |   initialiser lengths against them at compile time: C zero-fills a short
 |   initialiser without complaint, which would be a silently truncated
 |   catalogue that every test still passes -- and with two builds carrying
 |   different lengths, that assertion is now also what catches a prefix cut in
 |   the wrong place.
 | Author: suinevere
 ----------------------*/
#ifdef NETBIN
#define MUSIC_SYNTH_CELLS    %(cell_total)d
#define MUSIC_SYNTH_ORDER    %(order_total)d
#else
#define MUSIC_SYNTH_CELLS    %(cells_cd)d
#define MUSIC_SYNTH_ORDER    %(order_cd)d
#endif

/*----------------------
 | MUSIC_SYNTH_PAIRS
 | Description: How many distinct (note, wv) pairs the whole catalogue uses, and
 |   so how long the table every cell indexes is.
 |
 |   Outside the #if above, unlike the cell and order counts. Those are cut to a
 |   prefix on the CD target because that build links one tune; this one is not,
 |   because that build still plays the other eleven off /BG/MUSIC.PAT and their
 |   cells are indices into the whole table. Cut it to the CD prefix and every
 |   disc tune reads pairs that are not there.
 | Author: suinevere
 ----------------------*/
#define MUSIC_SYNTH_PAIRS    %(pair_total)d

/*----------------------
 | MUSIC_SYNTH_DEFAULT
 | Description: The tune played where nothing has chosen one -- the boot menus,
 |   the netbin, and any CD-DA track the mood table does not reach.
 | Author: suinevere
 ----------------------*/
#define MUSIC_SYNTH_DEFAULT  %(default)d

/*----------------------
 | MUSIC_PAT_FILE / MUSIC_PAT_HEADER_BYTES / MUSIC_PAT_SLOT_BYTES
 | Description: The disc copy of this catalogue, and the two sizes a loader has
 |   to know before it has read anything.
 |
 |   The CD build carries only the first MUSIC_SYNTH_SONGS_CD tunes in .rodata
 |   and reads the rest out of /BG/MUSIC.PAT one at a time. That is not a
 |   convenience: on that target __heap_start follows .rodata, so a linked tune
 |   comes straight out of the heap the story is loaded into, and the whole
 |   catalogue there stopped the disc's largest story loading at all. A tune on
 |   the disc costs Low Work RAM instead, which the story does not use.
 |
 |   One at a time and not all at once, also measured: the catalogue is bigger
 |   than the Low Work RAM left in the worst in-game case, and one slot the size
 |   of the largest tune is not.
 |
 |   HEADER_BYTES is what the loader reads of the first sector -- the directory,
 |   the track table and the ids -- and the generator refuses to write a header
 |   longer than it. SLOT_BYTES is the largest tune's record padded to whole
 |   sectors, which is the buffer every tune is read into.
 | Author: suinevere
 ----------------------*/
#define MUSIC_PAT_FILE         "%(pat_name)s"
#define MUSIC_PAT_HEADER_BYTES %(pat_header)d
#define MUSIC_PAT_SLOT_BYTES   %(pat_slot)d
#define MUSIC_PAT_SECTOR       %(pat_sector)d

/*----------------------
 | MUSIC_SYNTH_TRACK_MIN / MUSIC_SYNTH_TRACK_MAX
 | Description: The CD-DA track numbers music_synth_song_for_track answers for.
 |   They are the disc's own range, so a caller can hand it whatever the room
 |   table named without checking first.
 | Author: suinevere
 ----------------------*/
#define MUSIC_SYNTH_TRACK_MIN %(track_min)d
#define MUSIC_SYNTH_TRACK_MAX %(track_max)d

/*----------------------
 | music_synth_song_count
 | Description: How many tunes the build carries.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: MUSIC_SYNTH_SONGS
 ----------------------*/
int music_synth_song_count(void);

/*----------------------
 | music_synth_song_at
 | Description: One tune by index. An index outside the catalogue returns the
 |   default rather than nothing, because a bad index should sound like the
 |   wrong tune and not like a silent build.
 | Author: suinevere
 | Dependencies: tracker.h
 | Globals: N/A
 | Params: index -- 0..MUSIC_SYNTH_SONGS-1
 | Returns: a song valid for the life of the program
 ----------------------*/
const TrackerSong *music_synth_song_at(int index);

/*----------------------
 | music_synth_pairs
 | Description: The catalogue's (note, wv) pair table, which every cell in both
 |   the linked tunes and the ones on /BG/MUSIC.PAT is an index into.
 | Author: suinevere
 ----------------------*/
const TrackerPair *music_synth_pairs(void);

/*----------------------
 | music_synth_song_name
 | Description: A tune's title, for anything that shows the player what is
 |   playing.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: index -- 0..MUSIC_SYNTH_SONGS-1
 | Returns: a string valid for the life of the program
 ----------------------*/
const char *music_synth_song_name(int index);

/*----------------------
 | MUSIC_SYNTH_ID_MAX
 | Description: The longest id, so a caller can reserve a column for one. The
 |   Sound page's Test Track row is why this exists: it has 12 columns of value
 |   after a padded label, which an id fits and a title does not.
 | Author: suinevere
 ----------------------*/
#define MUSIC_SYNTH_ID_MAX %(id_width)d

/*----------------------
 | music_synth_song_id
 | Description: A tune's short name -- its id in songs.json, which is what the
 |   preview scripts take on the command line and what a menu row has room for.
 |   The title is music_synth_song_name.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: index -- 0..MUSIC_SYNTH_SONGS-1
 | Returns: a string valid for the life of the program
 ----------------------*/
const char *music_synth_song_id(int index);

/*----------------------
 | music_synth_song_for_track
 | Description: Which tune stands in for a CD-DA track. The table is measured
 |   by tools/gen_synth_moods.py -- each of the disc's tracks matched to the
 |   tune that measures nearest to it -- so a room that names a track gets the
 |   closest thing the synth has rather than the same loop everywhere.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: track -- a CD-DA track number
 | Returns: a song index, the default for a track outside the disc's range
 ----------------------*/
int music_synth_song_for_track(int track);

/*----------------------
 | music_synth_song
 | Description: The default tune. Kept so callers that only ever wanted "the
 |   music" do not have to name one.
 | Author: suinevere
 | Dependencies: tracker.h
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

MANIFEST_TEMPLATE = '''/*----------------------
 | music_synth_data.c
 | Description: %(songs)d tunes converted from MIDI -- do not hand-edit, re-run the
 |   converter. The settings are not guessable from the result, so they live in
 |   tools/assets/music/songs.json and the command is:
 |     %(command)s
 |
 |   Each is reduced to %(channels)d monophonic voices, transposed in whole octaves
 |   until its lowest note is representable -- and, where the manifest carries an
 |   "octaves" correction, by one more octave on one voice, because the sequence
 |   wrote that voice in the wrong one -- then packed by collapsing identical
 |   patterns. All of them share the two arrays below and point at their own
 |   offset inside them, which is why the cell array is one block and not
 |   %(songs)d of them.
 | Author: suinevere
 ----------------------*/
#include "music_synth_data.h"
#include "synth.h"

/*----------------------
 | MUSIC_CELLS
 | Description: Every tune's patterns end to end, %(cell_total)d cells of them. A note
 |   byte is a semitone index plus two; 0 holds whatever is sounding and 1 keys
 |   off. wv packs the waveform in the high nibble and the volume in the low.
 | Author: suinevere
 ----------------------*/
static const TrackerPair MUSIC_PAIRS[MUSIC_SYNTH_PAIRS] = {
%(pairs)s
};

/*----------------------
 | music_pairs_length_check
 | Description: The same guard the cells and the order list carry. A short pair
 |   table is the worst of the three: C zero-fills it, every missing index reads
 |   as note 0, and note 0 means "hold whatever is sounding" -- so the tune plays
 |   at the right tempo with notes quietly missing from it.
 | Author: suinevere
 ----------------------*/
typedef char music_pairs_length_check[
    (sizeof(MUSIC_PAIRS) / sizeof(MUSIC_PAIRS[0]) == MUSIC_SYNTH_PAIRS) ? 1 : -1];

/*----------------------
 | music_synth_pairs
 | Description: The pair table, for the disc path. A tune read off /BG/MUSIC.PAT
 |   carries indices and not pairs, so song_bank has to get the table from the
 |   image whichever tune is resident.
 | Author: suinevere
 ----------------------*/
const TrackerPair *music_synth_pairs(void) { return MUSIC_PAIRS; }

static const TrackerCell MUSIC_CELLS[MUSIC_SYNTH_CELLS] = {
%(cells)s
};

/*----------------------
 | music_cells_length_check
 | Description: Fails the build if the initialiser above is not exactly the
 |   declared length. C pads a short initialiser with zeros, which would be a
 |   truncated catalogue that every runtime test still passes.
 | Author: suinevere
 ----------------------*/
typedef char music_cells_length_check[
    (sizeof(MUSIC_CELLS) / sizeof(MUSIC_CELLS[0]) == MUSIC_SYNTH_CELLS) ? 1 : -1];

/*----------------------
 | MUSIC_ORDER
 | Description: Every tune's play order end to end, %(order_total)d entries. Each index
 |   is relative to its own tune's first pattern, which is what lets the songs
 |   below carry an offset base instead of an absolute one.
 | Author: suinevere
 ----------------------*/
static const unsigned char MUSIC_ORDER[MUSIC_SYNTH_ORDER] = {
    %(order)s
};

/*----------------------
 | music_order_length_check
 | Description: The same guard for the order list -- a short one would loop a
 |   tune early rather than fail.
 | Author: suinevere
 ----------------------*/
typedef char music_order_length_check[
    (sizeof(MUSIC_ORDER) / sizeof(MUSIC_ORDER[0]) == MUSIC_SYNTH_ORDER) ? 1 : -1];

/*----------------------
 | MUSIC_SONGS
 | Description: The catalogue. Each entry is speed whole V-blanks plus a
 |   fraction in 256ths per row, which is what keeps a tempo honest on a 60 Hz
 |   tick, and loops back to its own order 0.
 | Author: suinevere
 ----------------------*/
static const TrackerSong MUSIC_SONGS[MUSIC_SYNTH_SONGS] = {
%(song_table)s
};

/*----------------------
 | MUSIC_NAMES
 | Description: The titles, in the same order.
 | Author: suinevere
 ----------------------*/
static const char *const MUSIC_NAMES[MUSIC_SYNTH_SONGS] = {
%(names)s
};

/*----------------------
 | MUSIC_IDS
 | Description: The songs.json ids, in the same order -- what a menu row shows
 |   and what songs.bat takes.
 | Author: suinevere
 ----------------------*/
static const char *const MUSIC_IDS[MUSIC_SYNTH_SONGS] = {
%(ids)s
};

/*----------------------
 | MUSIC_TRACK_SONG
 | Description: One tune index per CD-DA track, tracks %(track_min)d to %(track_max)d.
 | Author: suinevere
 ----------------------*/
static const unsigned char MUSIC_TRACK_SONG[%(track_count)d] = {
    %(track_table)s
};

int music_synth_song_count(void) {
    return MUSIC_SYNTH_SONGS;
}

const TrackerSong *music_synth_song_at(int index) {
    if (index < 0 || index >= MUSIC_SYNTH_SONGS) index = MUSIC_SYNTH_DEFAULT;
    return &MUSIC_SONGS[index];
}

const char *music_synth_song_name(int index) {
    if (index < 0 || index >= MUSIC_SYNTH_SONGS) index = MUSIC_SYNTH_DEFAULT;
    return MUSIC_NAMES[index];
}

const char *music_synth_song_id(int index) {
    if (index < 0 || index >= MUSIC_SYNTH_SONGS) index = MUSIC_SYNTH_DEFAULT;
    return MUSIC_IDS[index];
}

int music_synth_song_for_track(int track) {
    int song;
    if (track < MUSIC_SYNTH_TRACK_MIN || track > MUSIC_SYNTH_TRACK_MAX)
        return MUSIC_SYNTH_DEFAULT;
    song = (int)MUSIC_TRACK_SONG[track - MUSIC_SYNTH_TRACK_MIN];
    /* The table is measured against the whole catalogue, and the CD build
       carries a prefix of it. A track matched to a tune that build does not
       have falls to the default rather than indexing off the end -- which is
       the one thing that could not be caught by reading the generated file,
       because the table is correct and the array is short. */
    return song < MUSIC_SYNTH_SONGS ? song : MUSIC_SYNTH_DEFAULT;
}

const TrackerSong *music_synth_song(void) {
    return &MUSIC_SONGS[MUSIC_SYNTH_DEFAULT];
}
'''

if __name__ == "__main__":
    sys.exit(main())
