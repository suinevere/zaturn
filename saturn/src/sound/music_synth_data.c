/*----------------------
 | music_synth_data.c
 | Description: The shipped loop -- a slow four-pattern figure in A minor,
 |   bass on the triangle, lead on the pulse, a sparse square accent. Note
 |   bytes are a semitone index plus two, since zero has to mean "hold";
 |   index 24 sounds C at octave 0, and twelve indices make an octave. One
 |   source line is one row of three channels, which is the only layout in
 |   which a miscounted pattern is visible to a reader.
 | Author: suinevere
 | Dependencies: music_synth_data.h, synth.h, tracker.h
 ----------------------*/
#include "music_synth_data.h"
#include "synth.h"

/*----------------------
 | R / X / N / V
 | Description: Cell shorthands. R holds whatever is sounding, X keys off,
 |   N packs a note from a semitone and an octave, V packs a waveform index
 |   with a volume.
 | Author: suinevere
 ----------------------*/
#define R  0
#define X  1
#define N(semi, oct) ((unsigned char)(((oct) + 2) * 12 + (semi) + 2))
#define V(wave, vol) ((unsigned char)(((wave) << 4) | (vol)))

/*----------------------
 | C / D / E / F / G / A / B
 | Description: Semitone numbers within an octave, C being 0.
 | Author: suinevere
 ----------------------*/
#define C  0
#define D  2
#define E  4
#define F  5
#define G  7
#define A  9
#define B 11

/*----------------------
 | HOLD
 | Description: A row in which no channel changes -- three held cells. Most
 |   rows are this, and spelling it once keeps the note rows legible.
 | Author: suinevere
 ----------------------*/
#define HOLD { R, 0 }, { R, 0 }, { R, 0 }

/*----------------------
 | MUSIC_CELLS
 | Description: Four patterns of sixteen rows across three channels: bass,
 |   lead, accent. Am - F - G - Am, four bars of one chord each.
 | Author: suinevere
 ----------------------*/
static const TrackerCell MUSIC_CELLS[MUSIC_SYNTH_PATTERNS * MUSIC_SYNTH_ROWS * MUSIC_SYNTH_CHANNELS] = {
    /* pattern 0 -- Am */
    { N(A, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(A, 1), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    HOLD,
    HOLD,
    HOLD,
    { R, 0 },                                { N(C, 2), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    HOLD,
    HOLD,
    HOLD,
    { N(E, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(E, 2), V(SYNTH_WAVE_PULSE, 4) }, { N(A, 0), V(SYNTH_WAVE_SQUARE, 3) },
    HOLD,
    HOLD,
    HOLD,
    { R, 0 },                                { N(C, 2), V(SYNTH_WAVE_PULSE, 4) }, { X, 0 },
    HOLD,
    HOLD,
    { X, 0 },                                { X, 0 },                            { R, 0 },

    /* pattern 1 -- F */
    { N(F, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(C, 2), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    HOLD,
    HOLD,
    HOLD,
    { R, 0 },                                { N(A, 1), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    HOLD,
    HOLD,
    HOLD,
    { N(C, 0), V(SYNTH_WAVE_TRIANGLE, 6) },  { N(F, 1), V(SYNTH_WAVE_PULSE, 4) }, { N(C, 0), V(SYNTH_WAVE_SQUARE, 3) },
    HOLD,
    HOLD,
    HOLD,
    { R, 0 },                                { N(A, 1), V(SYNTH_WAVE_PULSE, 4) }, { X, 0 },
    HOLD,
    HOLD,
    { X, 0 },                                { X, 0 },                            { R, 0 },

    /* pattern 2 -- G */
    { N(G, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(D, 2), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    HOLD,
    HOLD,
    HOLD,
    { R, 0 },                                { N(G, 1), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    HOLD,
    HOLD,
    HOLD,
    { N(D, 0), V(SYNTH_WAVE_TRIANGLE, 6) },  { N(B, 1), V(SYNTH_WAVE_PULSE, 4) }, { N(D, 0), V(SYNTH_WAVE_SQUARE, 3) },
    HOLD,
    HOLD,
    HOLD,
    { R, 0 },                                { N(G, 1), V(SYNTH_WAVE_PULSE, 4) }, { X, 0 },
    HOLD,
    HOLD,
    { X, 0 },                                { X, 0 },                            { R, 0 },

    /* pattern 3 -- Am, resolving */
    { N(A, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(A, 1), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    HOLD,
    HOLD,
    HOLD,
    { R, 0 },                                { N(E, 2), V(SYNTH_WAVE_PULSE, 4) }, { R, 0 },
    HOLD,
    HOLD,
    HOLD,
    { N(E, -1), V(SYNTH_WAVE_TRIANGLE, 6) }, { N(C, 2), V(SYNTH_WAVE_PULSE, 4) }, { N(A, 0), V(SYNTH_WAVE_SQUARE, 3) },
    HOLD,
    HOLD,
    HOLD,
    { R, 0 },                                { N(A, 1), V(SYNTH_WAVE_PULSE, 5) }, { X, 0 },
    HOLD,
    HOLD,
    { X, 0 },                                { X, 0 },                            { R, 0 },
};

/*----------------------
 | music_cells_length_check
 | Description: Fails the build if the initialiser above is not exactly the
 |   declared length. C pads a short initialiser with zeros, which would be a
 |   truncated tune that every runtime test still passes, so the count is
 |   asserted where it cannot be missed.
 | Author: suinevere
 ----------------------*/
typedef char music_cells_length_check[
    (sizeof(MUSIC_CELLS) / sizeof(MUSIC_CELLS[0])
     == MUSIC_SYNTH_PATTERNS * MUSIC_SYNTH_ROWS * MUSIC_SYNTH_CHANNELS) ? 1 : -1];

/*----------------------
 | MUSIC_ORDER
 | Description: The pattern play order. Each pattern is played once and the
 |   song returns to the first.
 | Author: suinevere
 ----------------------*/
static const unsigned char MUSIC_ORDER[] = { 0, 1, 2, 3 };

/*----------------------
 | MUSIC_SONG
 | Description: The song the tracker walks. Speed 9 holds each row for nine
 |   V-blanks, so a sixteen-row pattern lasts about 2.4 seconds and the whole
 |   loop about ten.
 | Author: suinevere
 ----------------------*/
static const TrackerSong MUSIC_SONG = {
    MUSIC_CELLS,
    MUSIC_SYNTH_ROWS,
    MUSIC_SYNTH_CHANNELS,
    MUSIC_ORDER,
    MUSIC_SYNTH_PATTERNS,
    0,
    9
};

const TrackerSong *music_synth_song(void) {
    return &MUSIC_SONG;
}
