/*----------------------
 | tracker.c
 | Description: Implementation of the sequencer. The tick counter counts down
 |   to zero rather than up to speed, so a row plays on the tick that reaches
 |   it and the first tick after a start plays row zero. The fractional part of
 |   the row length accumulates in g_frac and spends itself as a whole extra
 |   frame whenever it passes 256, which is how a 5.45-frame row is played by a
 |   counter that only understands whole frames.
 | Author: suinevere
 | Dependencies: tracker.h
 ----------------------*/
#include "tracker.h"

static const TrackerSong *g_song;
static TrackerSink        g_sink;
static int                g_playing;
static int                g_order;
static int                g_row;
static int                g_countdown;
static int                g_frac;

void tracker_start(const TrackerSong *song, TrackerSink sink) {
    g_song = song;
    g_sink = sink;
    g_order = 0;
    g_row = 0;
    g_countdown = 0;
    g_frac = 0;
    g_playing = (song != 0 && sink != 0 && song->order_len > 0 && song->rows > 0);
}

void tracker_stop(void) {
    g_playing = 0;
}

int tracker_playing(void) {
    return g_playing;
}

void tracker_hold(int frames) {
    if (frames > 0) g_countdown = frames;
}

void tracker_tick(void) {
    if (!g_playing) return;

    if (g_countdown > 0) {
        g_countdown--;
        return;
    }

    const TrackerSong *s = g_song;
    int pattern = s->order[g_order];
    const TrackerCell *row = s->cells + ((pattern * s->rows + g_row) * s->channels);

    for (int c = 0; c < s->channels; c++) {
        TrackerPair p = s->pairs[row[c]];
        if (p.note == 0) continue;
        if (p.note == 1) {
            g_sink(c, -1, 0, 0, 0);
        } else {
            int index = p.note - 2;
            g_sink(c, index % 12, index / 12 - 2, p.wv >> 4, p.wv & 0x0F);
        }
    }

    g_countdown = (s->speed > 0) ? s->speed - 1 : 0;
    g_frac += s->speed_frac;
    if (g_frac >= 256) {
        g_frac -= 256;
        g_countdown++;
    }
    g_row++;
    if (g_row >= s->rows) {
        g_row = 0;
        g_order++;
        if (g_order >= s->order_len) g_order = s->loop_to;
    }
}
