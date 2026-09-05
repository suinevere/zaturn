/*----------------------
 | tracker.h
 | Description: The sequencer: walks pattern data one tick at a time and emits
 |   note events to a sink. It never touches the SCSP, which is what lets the
 |   host tests assert on the event sequence instead of on hardware writes.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef TRACKER_H
#define TRACKER_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | TrackerCell
 | Description: One channel's cell in one row. note: 0 holds whatever is
 |   sounding, 1 keys off, 2 and above is a semitone index offset by two so
 |   that zero can mean "nothing here". wv packs the waveform index in the high
 |   nibble and the volume (0..7) in the low one.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned char note;
    unsigned char wv;
} TrackerCell;

/*----------------------
 | TrackerSong
 | Description: A song: a flat cell array addressed as
 |   cells[(pattern * rows + row) * channels + channel], an order list of
 |   pattern indices, the order index to jump back to when the order runs out,
 |   and how long a row is held: speed whole V-blanks plus speed_frac 256ths of
 |   one. The fraction exists because a row lasts an integer number of frames
 |   but music does not: at 165 BPM a sixteenth note is 5.45 frames, and
 |   rounding that to 5 or 6 runs the piece 9 per cent fast or slow. The
 |   remainder is carried between rows instead, so the average row length is
 |   right even though no single row is.
 | Author: suinevere
 ----------------------*/
typedef struct {
    const TrackerCell   *cells;
    unsigned char        rows;
    unsigned char        channels;
    const unsigned char *order;
    unsigned char        order_len;
    unsigned char        loop_to;
    unsigned char        speed;
    unsigned char        speed_frac;
} TrackerSong;

/*----------------------
 | TrackerSink
 | Description: Where note events go. semitone is -1 for a key off, in which
 |   case octave, wave and vol carry nothing.
 | Author: suinevere
 ----------------------*/
typedef void (*TrackerSink)(int channel, int semitone, int octave, int wave, int vol);

/*----------------------
 | tracker_start
 | Description: Begins the song at order 0, row 0. The next tracker_tick plays
 |   that row, so a caller gets a note on the first tick rather than the second.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: song -- must outlive playback; sink -- where events go
 | Returns: N/A
 ----------------------*/
void tracker_start(const TrackerSong *song, TrackerSink sink);

/*----------------------
 | tracker_stop
 | Description: Stops playback. Emits nothing -- silencing the voices is the
 |   caller's, since only it knows which ones are sounding.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void tracker_stop(void);

/*----------------------
 | tracker_tick
 | Description: Advances one tick. Every `speed` ticks a row is played and the
 |   position advances, wrapping through the order list to loop_to at the end.
 |   A no-op when stopped, so a V-blank handler can call it unconditionally.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void tracker_tick(void);

/*----------------------
 | tracker_playing
 | Description: Whether a song is running.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: nonzero while playing
 ----------------------*/
int tracker_playing(void);

#ifdef __cplusplus
}
#endif
#endif /* TRACKER_H */
