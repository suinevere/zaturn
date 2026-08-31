/*----------------------
 | music_data.c
 | Description: The tunable data the music engine reads: the neutral CD-DA
 |   track pool used whenever a scene has no authored tracks, and the two
 |   event pools (danger/triumph) still driven by event_scan. Per-room mood is
 |   no longer table data here -- it lives in scene/game_presentation.inc,
 |   generated from the story's own authored per-room table rather
 |   than hand-picked per category.
 | Author: suinevere
 | Dependencies: music.h (music_track_pool's declaration), event_scan.h
 |   (EVENT_N, EV_*)
 ----------------------*/
#include "music.h"
#include "event_scan.h"

/*----------------------
 | P_NEUTRAL / P_LOSE / P_WIN
 | Description: The three surviving track pools. P_NEUTRAL is what plays when
 |   a scene's authored mask is zero (nothing chosen for it yet, or a room
 |   with no scene at all); P_LOSE backs the death banner and P_WIN the
 |   story ending itself. Track numbers are CD-DA tracks 2..32.
 | Author: suinevere
 ----------------------*/
static const unsigned char P_NEUTRAL[] = {4,5,6,10,11,12,16,22,24,28,30};
static const unsigned char P_LOSE[]  = {4,5,6,10,11,12,13,14,15,16,17,22,24,27,28,30};
static const unsigned char P_WIN[] = {4,5,6,9,10,11,12,16,22,24,25,28,29,30};

/*----------------------
 | MUSIC_POOL_NEUTRAL / CATEGORY_POOL
 | Description: music_track_pool's "category" is now a pool selector, not a
 |   text category: EV_LOSE and EV_WIN pick the event pools directly,
 |   and MUSIC_POOL_NEUTRAL -- one past the last EV_* id, so it can never be
 |   mistaken for one -- picks the neutral fallback. music.c defines the
 |   identical constant so the two files agree without either owning the
 |   other's data.
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
