/*----------------------
 | music_data.c
 | Description: The tunable data the music engine reads: the neutral CD-DA
 |   track pool a story with no authored table falls back to, the two ending
 |   pools event_scan and music_on_win draw from, and the list of tracks a
 |   random draw is not allowed to land on.
 |
 |   A game gets its music one of two ways now. If it has a row in
 |   scene/game_presentation.inc, every room names its own track and nothing
 |   here is consulted at all. If it does not, every room of it shares the
 |   neutral pool and the track is drawn at random. Per-room mood is no longer
 |   table data here -- it lives in the generated presentation table, measured
 |   off the original disc rather than hand-picked per category.
 | Author: suinevere
 | Dependencies: music.h (music_track_pool and music_track_reserved's
 |   declarations), event_scan.h (EVENT_N, EV_*)
 ----------------------*/
#include "music.h"
#include "event_scan.h"

/*----------------------
 | RESERVED
 | Description: The tracks a random draw may never land on, one bit per CD-DA
 |   track. They are the disc's spoken-for sounds, measured in
 |   docs/ZORK1_AUDIO_MAP.md: 13-17 the villain and danger cues, 19 death, 21-24
 |   and 26-29 the rank fanfares, 25 the take sting, and 30 the ending theme.
 |   Hearing one at random is not a variety win -- it spends a sound the cues
 |   need, and a room that opens on the victory fanfare has told the player
 |   something that did not happen.
 |
 |   32 is in the list for a different reason: it is the same recording as 30
 |   with a volume-table entry of 0, so drawing it plays four and a quarter
 |   minutes of nothing.
 |
 |   The ending pools below are deliberately allowed to hold reserved tracks --
 |   an ending playing the ending theme is the point. Only the neutral draw is
 |   filtered; see music.c's pool_allows.
 | Author: suinevere
 ----------------------*/
/* Bit i is track i + MUSIC_TRACK_MIN, the same encoding music_track_from_mask
   uses -- tracks 2..32 in bits 0..30, which is the whole disc and fits a
   32-bit word. Numbering the bits by track number instead would need bit 32,
   which a 32-bit long does not have. */
#define BIT(t) (1uL << ((t) - MUSIC_TRACK_MIN))
static const unsigned long RESERVED =
    BIT(13) | BIT(14) | BIT(15) | BIT(16) | BIT(17) |   /* villain and danger */
    BIT(19) |                                            /* death */
    BIT(21) | BIT(22) | BIT(23) | BIT(24) |              /* rank fanfares */
    BIT(26) | BIT(27) | BIT(28) | BIT(29) |
    BIT(25) |                                            /* the take sting */
    BIT(30) |                                            /* the ending theme */
    BIT(32);                                             /* muted copy of 30 */
#undef BIT

/*----------------------
 | P_NEUTRAL / P_LOSE / P_WIN
 | Description: The three surviving track pools. P_NEUTRAL is what a story with
 |   no authored per-room table plays, and holds every track that is not
 |   reserved and not silent -- the room themes the original looped, plus the
 |   two its cutscene scripts used and the one nothing on the disc ever asked
 |   for. P_LOSE backs the death banner and P_WIN the story ending itself, for
 |   the stories that have no cue table to name those outright.
 | Author: suinevere
 ----------------------*/
static const unsigned char P_NEUTRAL[] = {2,3,4,5,6,7,8,9,10,11,12,18,20,31};
static const unsigned char P_LOSE[]    = {19,13,14,15,16,17};
static const unsigned char P_WIN[]     = {30};

/*----------------------
 | MUSIC_POOL_NEUTRAL / CATEGORY_POOL
 | Description: music_track_pool's "category" is a pool selector, not a text
 |   category: EV_LOSE and EV_WIN pick the ending pools directly, and
 |   MUSIC_POOL_NEUTRAL -- one past the last EV_* id, so it can never be
 |   mistaken for one -- picks the fallback. music.c defines the identical
 |   constant so the two files agree without either owning the other's data.
 | Author: suinevere
 ----------------------*/
#define MUSIC_POOL_NEUTRAL EVENT_N
#define POOL(a) { a, (unsigned char)(sizeof(a)/sizeof((a)[0])) }
static const struct { const unsigned char* p; unsigned char n; } CATEGORY_POOL[EVENT_N + 1] = {
    POOL(P_LOSE), POOL(P_WIN), POOL(P_NEUTRAL),
};
#undef POOL

/*----------------------
 | music_track_pool
 | Description: Returns a pool and its length, or an empty result for an
 |   out-of-range selector.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_POOL
 | Params: category -- EV_LOSE, EV_WIN, or MUSIC_POOL_NEUTRAL; out --
 |   receives the pool pointer (may be NULL)
 | Returns: the pool length (0 when out of range)
 ----------------------*/
int music_track_pool(int category, const unsigned char** out) {
    int n = (int)(sizeof CATEGORY_POOL / sizeof CATEGORY_POOL[0]);
    if (category < 0 || category >= n) { if (out) *out = 0; return 0; }
    if (out) *out = CATEGORY_POOL[category].p;
    return CATEGORY_POOL[category].n;
}

/*----------------------
 | music_track_reserved
 | Description: Whether a track is spoken for by a cue or an ending. See
 |   RESERVED. A track outside the disc's 2..32 answers 0, which is the safe
 |   reading -- a track that is not on the disc is not one of its reserved
 |   sounds.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: RESERVED
 | Params: track -- CD-DA track number
 | Returns: 1 when reserved, 0 otherwise
 ----------------------*/
int music_track_reserved(int track) {
    if (track < MUSIC_TRACK_MIN || track > MUSIC_TRACK_MAX - 1) return 0;
    return ((RESERVED >> (track - MUSIC_TRACK_MIN)) & 1uL) ? 1 : 0;
}
