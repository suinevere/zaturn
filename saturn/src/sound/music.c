/*----------------------
 | music.c
 | Description: The platform-independent music engine: the mix-mode state
 |   machine (Dynamic / Override / Sequential / Random), the loop-end /
 |   debounce logic that decides when to change track, and the per-turn
 |   decision that resolves a room's authored scene -- or an in-text danger/
 |   triumph moment -- into a CD-DA track. It owns no hardware -- a backend
 |   play callback (set by the Saturn client, or by the host tests) does the
 |   actual playing, and is_playing / is_short callbacks report drive state --
 |   so this file builds and unit-tests on the host.
 | Author: suinevere
 | Dependencies: music.h (mix modes, track bounds, pool accessor),
 |   scene/scene_map.h (scene_of_room, scene_game_index, scene_track_mask),
 |   scene/presentation.h (pres_of_room), event_scan.h (event_scan, EV_*),
 |   string.h
 ----------------------*/
#include "music.h"
#include "scene/scene_map.h"
#include "scene/presentation.h"
#include "event_scan.h"
#include <string.h>

/*----------------------
 | MUSIC_POOL_NEUTRAL
 | Description: The pool selector for the neutral fallback, one past the last
 |   EV_* id so it can never be mistaken for an event. music_data.c defines
 |   the identical constant; the two files agree on this without either
 |   owning the other's data.
 | Author: suinevere
 ----------------------*/
#define MUSIC_POOL_NEUTRAL EVENT_N

/*----------------------
 | CAT_KIND_NONE / CAT_KIND_SCENE / CAT_KIND_EVENT
 | Description: What a tracked category actually is. SC_* scene ids and EV_*
 |   event ids are separate vocabularies that both start at 0 -- comparing or
 |   storing them as one bare int would make SC_FOREST indistinguishable from
 |   EV_DANGER, which is exactly the ambiguity TC_* never had. Every place
 |   that remembers "what is sounding / pending / armed" carries one of these
 |   alongside the numeric id so the two vocabularies can never collide.
 | Author: suinevere
 ----------------------*/
enum { CAT_KIND_NONE = 0, CAT_KIND_SCENE = 1, CAT_KIND_EVENT = 2, CAT_KIND_ROOM = 3 };

/*----------------------
 | CAT_KIND_ROOM / g_base_kind
 | Description: The kind of the room's own category. On CAT_KIND_SCENE the
 |   category is an SC_* index and a pool supplies the track; on CAT_KIND_ROOM it
 |   IS the track, taken from the story's authored table. Making the track the
 |   category is what lets the existing unchanged-target branch stop the music
 |   restarting between two rooms that share one.
 | Author: suinevere
 ----------------------*/
static int g_base_kind = CAT_KIND_SCENE;

/*----------------------
 | g_rng
 | Description: LCG state, so track picks vary in play yet stay deterministic
 |   under a fixed seed (the host tests seed it).
 | Author: suinevere
 ----------------------*/
static unsigned int g_rng = 0x1234567u;

/*----------------------
 | music_seed
 | Description: Sets the LCG seed, forcing a nonzero value so the sequence never
 |   degenerates.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_rng
 | Params: s -- requested seed (0 is coerced to 1)
 | Returns: N/A
 ----------------------*/
void music_seed(unsigned int s) { g_rng = s ? s : 1u; }

/*----------------------
 | rng_next / rng_next_pub
 | Description: Advances the LCG and returns a 15-bit value. rng_next_pub is a
 |   thin wrapper used where a distinct call site reads clearer.
 | Author: suinevere
 ----------------------*/
static unsigned int rng_next(void) { g_rng = g_rng * 1103515245u + 12345u; return (g_rng >> 16) & 0x7fffu; }
static unsigned int rng_next_pub(void) { return rng_next(); }

/*----------------------
 | music_category_track
 | Description: Picks a uniformly random track from a pool.
 | Author: suinevere
 | Dependencies: music.h (music_track_pool)
 | Globals: g_rng (via rng_next)
 | Params: category -- EV_DANGER, EV_TRIUMPH, or MUSIC_POOL_NEUTRAL
 | Returns: a track number from the pool, or 0 if the pool is empty
 ----------------------*/
int music_category_track(int category) {
    const unsigned char* p; int n = music_track_pool(category, &p);
    if (n <= 0) return 0;
    return p[rng_next() % (unsigned)n];
}

/*----------------------
 | music_track_from_mask
 | Description: The r-th set bit of a scene's track mask, as a CD-DA track
 |   number. Bit i is track i + MUSIC_TRACK_MIN, so bits 0..30 name tracks
 |   2..32 -- the disc's whole run, exactly. The obvious encoding, bit i is
 |   track i, cannot reach track 32 at all and wastes bits 0 and 1 on track
 |   numbers that do not exist; that silent ceiling was harmless only while
 |   every mask was zero.
 |   r is reduced modulo the number of set bits, so any value works, and an
 |   empty mask answers 0 (no track) rather than dividing by zero.
 |
 |   A mask rather than a list because the disc carries 31 tracks and one 32-bit
 |   word holds a whole scene's playlist with no allocation and no length field.
 | Author: suinevere
 | Dependencies: music.h (MUSIC_TRACK_MIN)
 | Globals: N/A
 | Params: mask -- one bit per track; r -- any value
 | Returns: a track number, or 0 when the mask is empty
 ----------------------*/
int music_track_from_mask(unsigned long mask, unsigned int r) {
    int n = 0, i;
    for (i = 0; i < 32; i++) if (mask & (1UL << i)) n++;
    if (n == 0) return 0;
    r %= (unsigned int) n;
    for (i = 0; i < 32; i++) {
        if (!(mask & (1UL << i))) continue;
        if (r == 0) return i + MUSIC_TRACK_MIN;
        r--;
    }
    return 0;
}

/*----------------------
 | MUSIC_TEXT_MAX
 | Description: Capacity of the per-turn text buffer event_scan reads.
 | Author: suinevere
 ----------------------*/
#define MUSIC_TEXT_MAX 512

/*----------------------
 | engine state (g_play .. g_turn_len)
 | Description: The core engine state. g_play is the backend play callback;
 |   g_release/g_serial/g_game_idx identify the loaded game (g_game_idx is the
 |   cached scene_game_index row, -1 when the game carries no scene map);
 |   g_have_room/g_cur_room track the last room seen; g_base_cat is that
 |   room's authored scene (from scene_of_room, -1 when the room carries
 |   none) and g_event_cat a per-room danger/triumph override (-1 = none) --
 |   kept apart from g_base_cat because the two are different vocabularies
 |   (see CAT_KIND_*). g_active_track is what is sounding (0 = nothing yet);
 |   g_turn_text/g_turn_len accumulate the current turn's output for
 |   event_scan.
 | Author: suinevere
 ----------------------*/
static music_play_fn g_play = 0;
static unsigned int  g_release = 0;
static char          g_serial[8] = {0};
static int           g_game_idx = -1;
static int           g_have_room = 0;
static unsigned int  g_cur_room = 0;
static int           g_base_cat = -1;
static int           g_event_cat = -1;
static int           g_active_track = 0;
static char          g_turn_text[MUSIC_TEXT_MAX];
static int           g_turn_len = 0;

/*----------------------
 | mix state (g_mix_mode .. g_isshort)
 | Description: Mix-mode selection and its bookkeeping. g_mix_mode is the active
 |   MIX_*; g_override_track/g_seq_track carry the Override and Sequential
 |   positions; g_active_kind/g_active_cat are the category currently sounding
 |   (CAT_KIND_NONE/-1 = nothing chosen yet), with g_pending_kind/g_pending_cat/
 |   g_pending_track/g_pending_frames the debounced switch waiting to commit.
 |   MUSIC_DEBOUNCE_FRAMES (1.5s @ 60fps) is how long a new room must hold
 |   before its track -- and its picture -- take over; g_debounce_frames is
 |   the runtime override (host tests shorten it). g_isplaying/g_isshort are the
 |   drive-state callbacks.
 | Author: suinevere
 ----------------------*/
static int g_mix_mode = MIX_DYNAMIC;
static int g_override_track = 10;
static int g_seq_track = MUSIC_TRACK_MIN;
static int g_active_kind = CAT_KIND_NONE;
static int g_active_cat = -1;
static int g_pending_kind = CAT_KIND_NONE;
static int g_pending_cat = -1;
static int g_pending_track = 0;
static int g_pending_frames = 0;
/* 1 when the pending commit is a same-category rotation rather than a mood
   change, so commit_pending knows which subscriber to tell. */
static int g_pending_rotate = 0;
/* 90 frames = 1.5s at 60Hz (1.8s on a 50Hz PAL machine, accepted: the frame
   counter is the engine's only clock). This is the "stopped long enough to mean
   it" threshold, and it now gates the background picture as well as the track. */
#define MUSIC_DEBOUNCE_FRAMES 90
static int g_debounce_frames = MUSIC_DEBOUNCE_FRAMES;
static int (*g_isplaying)(void) = 0;
static int (*g_isshort)(int) = 0;

/*----------------------
 | g_await_play
 | Description: Seen-playing latch. Armed whenever the engine issues a play, it
 |   gates loop-end detection until is_playing() has first gone true. The CD block
 |   spends several frames in SEEK right after PlaySingle where is_playing() reads
 |   0 before the track has actually started, which would otherwise be misread as
 |   loop-end (runaway advance/re-roll/re-pick).
 | Author: suinevere
 ----------------------*/
static int g_await_play = 0;

/*----------------------
 | music_set_isplaying / music_set_isshort / music_set_debounce_frames
 | Description: Install the drive-state callbacks and override the debounce
 |   length (clamped to >= 0).
 | Author: suinevere
 ----------------------*/
void music_set_isplaying(int (*fn)(void)) { g_isplaying = fn; }
void music_set_isshort(int (*fn)(int)) { g_isshort = fn; }
void music_set_debounce_frames(int n) { g_debounce_frames = (n < 0) ? 0 : n; }

/*----------------------
 | g_cat_fn / music_set_category_fn / notify_cat
 | Description: The category-change subscriber and the one place that calls it.
 |   The callback's contract is scene-only: it announces the active SCENE, for
 |   choosing a background picture, and nothing else. notify_cat takes the kind
 |   alongside the value and fires g_cat_fn only when kind is CAT_KIND_SCENE --
 |   an event taking over the track (CAT_KIND_EVENT) is deliberately silent to
 |   the subscriber, which holds whatever picture it is already showing. Events
 |   carry no picture of their own, and passing one through as a bare int would
 |   be indistinguishable from a real scene on the other end (see CAT_KIND_*).
 |   Suppressing it here, in the one place that knows events have no picture,
 |   is simpler than pushing that policy onto every subscriber. Optional: with
 |   nothing installed the engine behaves exactly as it did before, which is
 |   what the pre-existing host tests assume.
 | Author: suinevere
 ----------------------*/
static void (*g_cat_fn)(int) = 0;
void music_set_category_fn(void (*fn)(int cat)) { g_cat_fn = fn; }
static void notify_cat(int kind, int cat) { if (kind == CAT_KIND_SCENE && g_cat_fn) g_cat_fn(cat); }

/*----------------------
 | g_room_fn / music_set_room_fn
 | Description: The per-room subscriber. Separate from g_cat_fn because a
 |   category on the authored path is a track, and rooms sharing a track share a
 |   category while needing different pictures.
 | Author: suinevere
 ----------------------*/
static void (*g_room_fn)(unsigned int) = 0;
void music_set_room_fn(void (*fn)(unsigned int obj)) { g_room_fn = fn; }

/*----------------------
 | g_rot_fn / music_set_rotate_fn / notify_rotate / g_same_cat_rooms
 | Description: The same-category rotation subscriber, and the room counter that
 |   triggers it. g_same_cat_rooms counts rooms entered since the last commit
 |   while the mood held; at MUSIC_ROTATE_ROOMS the engine cycles to another track
 |   in the category it is already in and tells the art to follow. Same
 |   scene-only contract as notify_cat: fires only when kind is CAT_KIND_SCENE.
 |
 |   This is a separate signal from notify_cat because the client's job differs:
 |   on a category change it resolves that mood's picture, on a rotation it has to
 |   pick a DIFFERENT picture for the mood it is already showing. Collapsing them
 |   would make the second look like a no-op, which is exactly what it must not be.
 | Author: suinevere
 ----------------------*/
static void (*g_rot_fn)(int) = 0;
static int g_same_cat_rooms = 0;
void music_set_rotate_fn(void (*fn)(int cat)) { g_rot_fn = fn; }
static void notify_rotate(int kind, int cat) { if (kind == CAT_KIND_SCENE && g_rot_fn) g_rot_fn(cat); }

/*----------------------
 | g_paused / g_pause_fn / g_resume_fn / music_set_pausefns
 | Description: The hold latch and the backend calls behind it. Under a stop
 |   music_tick does nothing at all: a stopped drive reports not-playing, which is
 |   the same reading as loop-end, and without this latch every frame of an open
 |   menu would look like a track ending and start another one. A duck reads the
 |   latch for "is something holding the music" but does NOT park the tick -- see
 |   music_tick for which half it withholds and why.
 | Author: suinevere
 ----------------------*/
static int g_paused = 0;
static void (*g_pause_fn)(void) = 0;
static void (*g_resume_fn)(void) = 0;

/*----------------------
 | g_hold_kind / g_duck_fn / g_unduck_fn / music_set_duckfns
 | Description: Which hold music_resume has to undo. A hold is either a stop
 |   (HOLD_STOP, the drive seeked away) or a duck (HOLD_DUCK, the track still
 |   running quietly), and the two are lifted by different backend calls. Recording
 |   the kind is what lets every existing music_resume() caller stay as it is.
 | Author: suinevere
 ----------------------*/
#define HOLD_NONE 0
#define HOLD_STOP 1
#define HOLD_DUCK 2

static int g_hold_kind = HOLD_NONE;
static void (*g_duck_fn)(void) = 0;
static void (*g_unduck_fn)(void) = 0;

void music_set_pausefns(void (*pause_fn)(void), void (*resume_fn)(void)) {
    g_pause_fn = pause_fn; g_resume_fn = resume_fn;
}

void music_set_duckfns(void (*duck_fn)(void), void (*unduck_fn)(void)) {
    g_duck_fn = duck_fn; g_unduck_fn = unduck_fn;
}

/*----------------------
 | music_pause / music_duck / music_resume / music_is_paused
 | Description: Hold the music for as long as something is over the game, and put it
 |   back. Two holds: pause stops the drive where it stands and picks the same track
 |   up from that frame, duck leaves it running and only drops the volume. Duck is
 |   for anything the player is looking at with the game still under it -- an open
 |   menu -- and pause for the one case that actually wants the drive quiet and out
 |   of the way, the loading screen and its own PCM jingle. Either is lifted by
 |   music_resume, which reads back the kind rather than making callers track it.
 |
 |   All three are idempotent, and both holds are a no-op when nothing is playing, so
 |   a caller can bracket a menu without first asking what the music is doing. Resume
 |   re-arms the seen-playing latch after a pause, because the drive has to seek back
 |   before is_playing() means anything again -- without that, the first tick after a
 |   resume reads the seek as loop-end. A duck never stopped, so it skips that.
 |
 |   Both holds set the same latch, so music_tick stays parked either way. That is
 |   load-bearing for duck as well as pause: a tick under an open menu can commit a
 |   category change, and that reaches for the room's picture -- a disc read, which
 |   would silence the very track the duck is keeping alive.
 |
 |   music_is_paused reports the latch so a nested page can put back what it found
 |   rather than guess. The Sound options page is the case that needs it: it resumes
 |   unconditionally on entry because every row on it is judged by ear, and it is
 |   reached both from the in-game Options menu (which holds the drive for as long as
 |   the menu is up) and from the main menu (which does not). Neither an
 |   unconditional pause nor an unconditional resume on the way out is right for
 |   both, and pausing the main menu's music because an in-game page needed to is
 |   the more visible half of that mistake.
 | Author: suinevere
 | Dependencies: N/A (calls the backend via g_pause_fn / g_resume_fn)
 | Globals: g_paused, g_active_track, g_await_play
 | Params: N/A
 | Returns: music_is_paused -- 1 while the drive is held, else 0
 ----------------------*/
void music_pause(void) {
    if (g_hold_kind == HOLD_STOP) return;   // already stopped; do not seek twice
    if (g_active_track <= 0) return;        // nothing playing to hold
    // A duck in force is upgraded rather than treated as "already held". The two
    // are not interchangeable: only a stop actually takes the drive out of the
    // way, and the caller that asks for one -- the loading screen -- is about to
    // read the story off the disc. A soft reset out of the in-game Options menu
    // leaves a duck standing across the longjmp, and no-oping here would let that
    // read contend with a CD-DA track that was never stopped.
    g_paused = 1;
    g_hold_kind = HOLD_STOP;
    if (g_pause_fn) g_pause_fn();
}

void music_duck(void) {
    if (g_paused || g_active_track <= 0) return;
    g_paused = 1;
    g_hold_kind = HOLD_DUCK;
    if (g_duck_fn) g_duck_fn();
}

void music_resume(void) {
    if (!g_paused) return;
    int kind = g_hold_kind;
    g_paused = 0;
    g_hold_kind = HOLD_NONE;
    if (kind == HOLD_DUCK) {
        if (g_unduck_fn) g_unduck_fn();
        return;
    }
    g_await_play = 1;
    if (g_resume_fn) g_resume_fn();
}

int music_is_paused(void) { return g_paused; }

/*----------------------
 | trk_is_short
 | Description: True when the is_short callback marks `t` as a short/play-once
 |   track; false when no callback is installed.
 | Author: suinevere
 ----------------------*/
static int trk_is_short(int t) { return g_isshort ? g_isshort(t) : 0; }

/*----------------------
 | g_dyn_pass
 | Description: Which pass of the current Dynamic track is sounding, 1..
 |   MUSIC_DYN_LOOPS (0 = no Dynamic track yet). The engine counts the passes
 |   itself because the drive cannot be asked to; see music.h.
 | Author: suinevere
 ----------------------*/
static int g_dyn_pass = 0;

/*----------------------
 | play_dyn
 | Description: Plays a Dynamic-mode track one-shot, records it as active with which
 |   pass this is, and arms the seen-playing latch. One-shot whether the track is
 |   long or short: every Dynamic track gets the same MUSIC_DYN_LOOPS passes, and
 |   the end of each is what music_tick counts. (Track length still steers which
 |   track gets chosen: see pick_prefer_long.)
 | Author: suinevere
 | Dependencies: N/A (calls the backend via g_play)
 | Globals: g_active_track, g_dyn_pass, g_await_play, g_play
 | Params: track -- track number to play; pass -- which pass through it this is
 | Returns: N/A
 ----------------------*/
static void play_dyn(int track, int pass) {
    g_active_track = track;
    g_dyn_pass = pass;
    g_await_play = 1;
    if (g_play) g_play(track, 0);
}

/*----------------------
 | pick_prefer_long
 | Description: Chooses a track from a pool, preferring a non-short one and,
 |   where the pool allows, one other than what is sounding now -- a category
 |   change should be audible, so it cycles off the current track instead of
 |   possibly re-rolling it. Falls back to any long track if the only long option
 |   is the current one, then to any track at all.
 | Author: suinevere
 | Dependencies: music.h (music_track_pool)
 | Globals: g_active_track (read)
 | Params: cat -- EV_DANGER, EV_TRIUMPH, or MUSIC_POOL_NEUTRAL
 | Returns: a track number, or 0 if the pool is empty
 ----------------------*/
static int pick_prefer_long(int cat) {
    const unsigned char* p; int n = music_track_pool(cat, &p);
    if (n <= 0) return 0;
    int longs[64], m = 0;
    for (int i = 0; i < n && m < 64; i++)
        if (!trk_is_short(p[i]) && p[i] != g_active_track) longs[m++] = p[i];
    if (m == 0)
        for (int i = 0; i < n && m < 64; i++) if (!trk_is_short(p[i])) longs[m++] = p[i];
    if (m > 0) return longs[rng_next_pub() % (unsigned)m];
    return music_category_track(cat);
}

/*----------------------
 | pick_dynamic_track
 | Description: Chooses a track for a target the room-scene lookup or the
 |   event scan named. An event still comes from its pool the way a room mood
 |   always did (pick_prefer_long). A scene with authored tracks draws the
 |   r-th set bit of its mask (music_track_from_mask); one with none authored
 |   -- the same as no scene at all, CAT_KIND_NONE -- falls back to the
 |   neutral pool.
 | Author: suinevere
 | Dependencies: scene/scene_map.h (scene_track_mask), music_track_from_mask,
 |   pick_prefer_long
 | Globals: g_game_idx (read)
 | Params: kind -- CAT_KIND_SCENE, CAT_KIND_EVENT, or CAT_KIND_NONE; cat --
 |   the SC_* or EV_* value, meaningless when kind is CAT_KIND_NONE
 | Returns: a track number
 ----------------------*/
static int pick_dynamic_track(int kind, int cat) {
    if (kind == CAT_KIND_ROOM) return cat;
    if (kind == CAT_KIND_EVENT) return pick_prefer_long(cat);
    if (kind == CAT_KIND_SCENE) {
        unsigned long mask = scene_track_mask(cat);
        if (mask) return music_track_from_mask(mask, rng_next_pub());
    }
    return pick_prefer_long(MUSIC_POOL_NEUTRAL);
}

/*----------------------
 | music_set_backend
 | Description: Installs the backend play callback (track, loop) the engine drives.
 | Author: suinevere
 ----------------------*/
void music_set_backend(music_play_fn play) { g_play = play; }

/*----------------------
 | music_set_game
 | Description: Records the loaded game's release number and (truncated)
 |   serial, and resolves and caches its scene-table row index so
 |   music_on_turn does not have to look it up every turn.
 | Author: suinevere
 | Dependencies: scene/scene_map.h (scene_game_index)
 | Globals: g_release, g_serial, g_game_idx
 | Params: release -- Z-machine release word; serial -- 6-char serial (may be short)
 | Returns: N/A
 ----------------------*/
void music_set_game(unsigned int release, const char* serial) {
    g_release = release;
    for (int i = 0; i < 6 && serial && serial[i]; i++) g_serial[i] = serial[i];
    g_serial[6] = 0;
    g_game_idx = scene_game_index(g_release, g_serial);
}

/*----------------------
 | fade state (MP_* / g_phase / g_fade_frames / g_fade_i / g_fade_fn)
 | Description: The transition around a Dynamic commit. A commit issues a fresh
 |   one-shot play, so the audio has to already be down when it happens and come
 |   up after -- the ramp brackets the commit rather than running beside it, which
 |   is why the engine owns the phase instead of the client fading on
 |   notification. One counter drives both the picture and the volume, so they
 |   cannot drift apart. g_fade_frames of 0 is the default and skips the phases
 |   entirely, reproducing the instant commit the pre-existing tests assert.
 |
 |   These are ticked one step per music_tick, never looped: title.cxx's fades are
 |   blocking `for i ... Synchronize()` ramps, and running one of those from a
 |   commit would stall the interpreter for the whole fade every time a room's
 |   mood changed.
 | Author: suinevere
 ----------------------*/
enum { MP_IDLE = 0, MP_FADE_OUT, MP_FADE_IN };
static int  g_phase = MP_IDLE;
static int  g_fade_frames = 0;
static int  g_fade_i = 0;
static void (*g_fade_fn)(int) = 0;

void music_set_fade_fn(void (*fn)(int level)) { g_fade_fn = fn; }
void music_set_fade_frames(int n) { g_fade_frames = (n < 0) ? 0 : n; }

static void fade_emit(int level) { if (g_fade_fn) g_fade_fn(level); }

/*----------------------
 | commit_pending
 | Description: Takes the pending category, announces it, and starts its track.
 |   Shared by the instant path and the bottom of a fade so the two cannot drift.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_active_kind, g_active_cat, g_pending_kind, g_pending_cat, g_pending_track
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void commit_pending(void) {
    int t = g_pending_track;
    int rotate = g_pending_rotate;
    g_active_kind = g_pending_kind;
    g_active_cat = g_pending_cat;
    g_pending_kind = CAT_KIND_NONE; g_pending_cat = -1; g_pending_track = 0; g_pending_rotate = 0;
    g_same_cat_rooms = 0;          // the walk that earned this rotation is spent
    if (rotate) notify_rotate(g_active_kind, g_active_cat);
    else        notify_cat(g_active_kind, g_active_cat);
    play_dyn(t, 1);
}

/*----------------------
 | fade_out_step
 | Description: One frame of the ramp down, committing when it reaches the bottom.
 |   Factored out because the frame the settle expires on takes a step too --
 |   arming the phase and waiting for the next tick would leave one frame at full
 |   brightness after the engine has already decided to move, which reads as a
 |   hitch before the fade rather than the start of it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_fade_i, g_fade_frames, g_phase
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void fade_out_step(void) {
    if (g_fade_i > 0) g_fade_i--;
    fade_emit((255 * g_fade_i) / g_fade_frames);
    if (g_fade_i <= 0) {
        commit_pending();          // swap at the bottom, where nothing shows it
        g_phase = MP_FADE_IN;
    }
}

/*----------------------
 | music_transition_active
 | Description: Whether a Dynamic mood change is armed or part-way through its
 |   fade, so a caller can run it to completion before drawing anything.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pending_cat, g_phase
 | Params: N/A
 | Returns: nonzero while a switch is still owed some frames
 ----------------------*/
int music_transition_active(void) {
    return (g_pending_cat >= 0 || g_phase != MP_IDLE) ? 1 : 0;
}

/*----------------------
 | music_transition_skip_fade
 | Description: Commits an armed mood change outright, with no ramp either side.
 |   For the one moment a ramp has nothing to do: the opening room, where the
 |   screen is still held black for the reveal, so the twenty fields down and the
 |   twenty back up would be spent fading black into black.
 |
 |   Commits through commit_pending, the same call the bottom of a fade makes, so
 |   the picture is notified before the track starts exactly as it always is and
 |   the room's own background is up before anything is revealed. Refuses if a
 |   ramp is already in flight, because that ramp owns g_fade_i and finishing it
 |   from underneath would leave the counter describing a phase that has gone.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pending_cat, g_pending_frames, g_phase
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_transition_skip_fade(void) {
    if (g_phase != MP_IDLE) return;
    if (g_pending_cat < 0) return;
    g_pending_frames = 0;
    commit_pending();
}

/*----------------------
 | music_transition_flush
 | Description: Drops the remaining settle so an armed mood change starts fading on
 |   the next tick instead of waiting out MUSIC_DEBOUNCE_FRAMES.
 |
 |   The settle exists to stop fast movement through a corridor from thrashing the
 |   music, and it is still what governs a change noticed mid-turn. This is for the
 |   one moment it has nothing to protect against: the interpreter has finished the
 |   turn and is about to block for input, so the player has by definition stopped.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pending_frames
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_transition_flush(void) {
    if (g_pending_cat >= 0) g_pending_frames = 0;
}

/*----------------------
 | music_reset
 | Description: Clears play state back to first-boot values and tells the backend
 |   to stop. Does NOT clear which game is loaded -- g_release/g_serial/g_game_idx
 |   survive it on purpose, because main.cxx calls music_set_game before
 |   music_reset on every load, and that is where they are derived. Called for a
 |   new game and on soft-reset re-entry so a stale engine cannot leak a track
 |   into the menu.
 | Author: suinevere
 | Dependencies: N/A (stops via g_play)
 | Globals: nearly all engine/mix state except g_release/g_serial/g_game_idx
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_reset(void) {
    g_have_room = 0; g_cur_room = 0; g_base_cat = -1; g_event_cat = -1;
    g_active_track = 0; g_turn_len = 0; g_turn_text[0] = 0;
    g_active_kind = CAT_KIND_NONE; g_active_cat = -1;
    g_pending_kind = CAT_KIND_NONE; g_pending_cat = -1; g_pending_track = 0; g_pending_frames = 0;
    g_pending_rotate = 0; g_same_cat_rooms = 0;
    g_seq_track = MUSIC_TRACK_MIN;
    g_await_play = 0;
    g_dyn_pass = 0;
    g_paused = 0;   // a soft reset can land here with a menu still nominally open
    g_hold_kind = HOLD_NONE;
    if (g_phase != MP_IDLE) fade_emit(255);   // mid-ramp: nothing else would lift it
    g_phase = MP_IDLE; g_fade_i = 0;
    // Nothing to announce: notify_cat only fires for CAT_KIND_SCENE, and there
    // is no active scene right after a reset, so the subscriber hears nothing
    // and holds whatever picture it is already showing.
    notify_cat(CAT_KIND_NONE, -1);
    if (g_play) g_play(0, 0);
}

/*----------------------
 | music_refresh
 | Description: Re-issues the active track to the backend with the right repeat flag
 |   for the current mix mode (Override repeats forever; everything else is one-shot,
 |   Dynamic included, since the engine counts its own passes). Used to re-assert
 |   playback after something else touched the drive. The re-issue restarts the
 |   track, so it counts as the first of that track's passes -- the honest reading of
 |   "play this three times" once the stream has been interrupted anyway.
 | Author: suinevere
 | Dependencies: N/A (plays via g_play)
 | Globals: g_active_track, g_mix_mode, g_dyn_pass, g_play
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_refresh(void) {
    if (g_active_track > 0 && g_play) {
        int loop = (g_mix_mode == MIX_OVERRIDE) ? 1 : 0;
        if (g_mix_mode == MIX_DYNAMIC) g_dyn_pass = 1;
        g_await_play = 1;   // the drive seeks first; without this the next tick reads that as loop-end
        g_play(g_active_track, loop);
    }
}

/*----------------------
 | music_set_mix
 | Description: Selects the mix mode and, when a valid track is given, records it
 |   as the Override/Sequential start track.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_mix_mode, g_override_track
 | Params: mode -- MIX_*; override_track -- start track (ignored if out of range)
 | Returns: N/A
 ----------------------*/
void music_set_mix(int mode, int override_track) {
    g_mix_mode = mode;
    if (override_track >= MUSIC_TRACK_MIN && override_track <= MUSIC_TRACK_MAX)
        g_override_track = override_track;
}

/*----------------------
 | play_seq_current / play_random_now
 | Description: Start the current Sequential track, or a fresh random track,
 |   one-shot (so music_tick advances on loop-end) and arm the seen-playing latch.
 | Author: suinevere
 ----------------------*/
static void play_seq_current(void) {
    g_active_track = g_seq_track;
    g_await_play = 1;
    if (g_play) g_play(g_seq_track, 0);
}
static void play_random_now(void) {
    int t = MUSIC_TRACK_MIN + (int)(rng_next_pub() % (unsigned)(MUSIC_TRACK_MAX - MUSIC_TRACK_MIN + 1));
    g_active_track = t;
    g_await_play = 1;
    if (g_play) g_play(t, 0);
}

/*----------------------
 | music_start
 | Description: Begins playback for the selected mix mode: Override plays its
 |   track looped (honoring repeat even if short), Sequential and Random start
 |   their first one-shot track, and Dynamic waits -- the first music_on_turn
 |   drives it off the room.
 | Author: suinevere
 | Dependencies: N/A (plays via g_play)
 | Globals: g_mix_mode, g_active_kind, g_pending_*, g_active_track, g_seq_track
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_start(void) {
    g_active_kind = CAT_KIND_NONE; g_active_cat = -1;
    g_pending_kind = CAT_KIND_NONE; g_pending_cat = -1; g_pending_track = 0;
    switch (g_mix_mode) {
        case MIX_OVERRIDE:
            g_active_track = g_override_track;
            g_await_play = 1;
            if (g_play) g_play(g_override_track, 1);
            break;
        case MIX_SEQUENTIAL:
            g_seq_track = g_override_track; play_seq_current(); break;
        case MIX_RANDOM:
            play_random_now(); break;
        case MIX_DYNAMIC: default:
            break;
    }
}

/*----------------------
 | music_start_menu
 | Description: Begins playback for a screen that has no room to classify -- the
 |   title, the menus, Sound Options. Identical to music_start except for Dynamic,
 |   which has nothing to key off and would otherwise sit silent waiting for a
 |   music_on_turn that a menu never sends. It opens on the track the player
 |   selected in Sound Options (music_set_mix records it) and, once that has had its
 |   MUSIC_DYN_LOOPS passes, cycles on through the neutral pool -- which means "no
 |   particular mood", exactly what a menu is. Deliberately does NOT clear an
 |   established category the way music_start does: called from Sound Options
 |   mid-game, the room the player is standing in still sets the mood (guarded by
 |   g_active_track rather than the category, since "nothing has played yet this
 |   session" is what actually distinguishes a fresh boot from a mid-game preview),
 |   and only a screen reached before any room has been seen falls back to neutral.
 | Author: suinevere
 | Dependencies: N/A (plays via g_play)
 | Globals: g_mix_mode, g_active_kind, g_active_cat, g_pending_*, g_active_track,
 |   g_seq_track
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_start_menu(void) {
    g_pending_kind = CAT_KIND_NONE; g_pending_cat = -1; g_pending_track = 0;
    switch (g_mix_mode) {
        case MIX_OVERRIDE:
        case MIX_SEQUENTIAL:
        case MIX_RANDOM:
            music_start();
            break;
        case MIX_DYNAMIC: default:
            if (g_active_track == 0) {
                g_active_kind = CAT_KIND_NONE; g_active_cat = -1;
                notify_cat(CAT_KIND_NONE, -1);   // no scene: subscriber hears nothing
            }
            play_dyn(g_override_track, 1);
            break;
    }
}

/*----------------------
 | music_tick
 | Description: One engine frame. First commits a pending Dynamic category switch
 |   once its debounce elapses -- immediately when no fade is configured, else by
 |   ramping down over g_fade_frames, committing at the bottom where the swap is
 |   inaudible and invisible, and ramping back up. Then, while the seen-playing
 |   latch is armed, it
 |   ignores is_playing() until the just-issued track has actually started (this
 |   clears the CD seek window that would otherwise read as loop-end). On a real
 |   loop-end it advances Sequential (wrapping at MUSIC_TRACK_MAX), re-rolls
 |   Random, and for Dynamic either replays the same track for another pass or, once
 |   it has had MUSIC_DYN_LOOPS of them, cycles to another track in the category it
 |   is already in -- the player has heard that one enough by then, whether or not
 |   the mood has moved. pick_dynamic_track steers off the current track where the
 |   source allows, so the cycle is audible. This runs wherever music_tick does, so
 |   the passes keep counting in the in-game menus as well as at the prompt.
 |   Override repeats on its own, so there is nothing to do.
 | Author: suinevere
 | Dependencies: N/A (plays via g_play, reads g_isplaying)
 | Globals: g_pending_*, g_await_play, g_active_track, g_active_kind, g_active_cat,
 |   g_dyn_pass, g_seq_track, g_mix_mode
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_tick(void) {
    // A stopped drive reports not-playing, which is indistinguishable from
    // loop-end, so under that hold there is nothing this can safely read.
    if (g_hold_kind == HOLD_STOP) return;
    // A duck stopped nothing, so the readings are honest and the loop-end branch
    // at the bottom still has to run -- a track ending under an open menu is a
    // real ending, and nothing else will service it. Only the mood machinery is
    // withheld: committing a category announces it, and the client answers by
    // fetching that room's picture off the disc, which would cut the very track
    // the duck is keeping alive. It stays armed and lands on the resume.
    if (g_hold_kind != HOLD_DUCK) {
        if (g_phase == MP_FADE_OUT) { fade_out_step(); return; }
        if (g_phase == MP_FADE_IN) {
            if (g_fade_i < g_fade_frames) g_fade_i++;
            fade_emit((255 * g_fade_i) / g_fade_frames);
            if (g_fade_i >= g_fade_frames) g_phase = MP_IDLE;
            return;
        }
        if (g_pending_cat >= 0) {
            if (g_pending_frames > 0) g_pending_frames--;
            if (g_pending_frames <= 0) {
                if (g_fade_frames > 0) {
                    g_phase = MP_FADE_OUT; g_fade_i = g_fade_frames;
                    fade_out_step();   // the ramp starts on this frame, not the next
                } else {
                    commit_pending();
                }
            }
            return;
        }
    }
    if (g_await_play) {
        if (g_isplaying && g_isplaying()) g_await_play = 0;
        return;
    }
    if (g_active_track > 0 && g_isplaying && !g_isplaying()) {
        if (g_mix_mode == MIX_SEQUENTIAL) {
            g_seq_track = (g_seq_track >= MUSIC_TRACK_MAX) ? MUSIC_TRACK_MIN : g_seq_track + 1;
            play_seq_current();
        } else if (g_mix_mode == MIX_RANDOM) {
            play_random_now();
        } else if (g_mix_mode == MIX_OVERRIDE) {
            // Override repeats on the drive, so this is only reached after a resume
            // handed back the tail of a pass rather than an endless one.
            g_active_track = g_override_track;
            g_await_play = 1;
            if (g_play) g_play(g_override_track, 1);
        } else if (g_mix_mode == MIX_DYNAMIC) {
            if (g_active_kind == CAT_KIND_EVENT && g_base_kind == CAT_KIND_ROOM) {
                g_event_cat = -1;
                g_active_kind = CAT_KIND_ROOM;
                g_active_cat = g_base_cat;
                play_dyn(g_base_cat, 1);
            } else if (g_dyn_pass < MUSIC_DYN_LOOPS) {
                play_dyn(g_active_track, g_dyn_pass + 1);
            } else {
                play_dyn(pick_dynamic_track(g_active_kind, g_active_cat), 1);
            }
        }
    }
}

/*----------------------
 | music_note_output
 | Description: Appends the turn's output text to the classification buffer,
 |   stopping at a NUL, so music_on_turn can read the turn. When a turn's total
 |   output exceeds the MUSIC_TEXT_MAX-1 capacity, the buffer keeps the NEWEST
 |   bytes and drops the oldest: the room description is always the last thing
 |   printed before the prompt, while a dream, cutscene or banner prints
 |   earlier in the same turn, so trimming from the front is what throws away
 |   the part that is not the room. Uses memmove/memcpy rather than a
 |   byte-at-a-time shift -- O(n) per call instead of O(n^2) over a turn's
 |   worth of appends, which matters on a 28MHz SH-2.
 | Author: suinevere
 | Dependencies: string.h (memmove, memcpy)
 | Globals: g_turn_text, g_turn_len
 | Params: str -- output text; len -- its length
 | Returns: N/A
 ----------------------*/
void music_note_output(const char* str, unsigned int len) {
    unsigned int chunk;
    int cap = MUSIC_TEXT_MAX - 1;
    for (chunk = 0; chunk < len && str[chunk]; chunk++) ;

    if ((int) chunk >= cap) {
        memcpy(g_turn_text, str + (chunk - (unsigned int) cap), (size_t) cap);
        g_turn_len = cap;
    } else {
        int overflow = g_turn_len + (int) chunk - cap;
        if (overflow > 0) {
            memmove(g_turn_text, g_turn_text + overflow, (size_t) (g_turn_len - overflow));
            g_turn_len -= overflow;
        }
        memcpy(g_turn_text + g_turn_len, str, (size_t) chunk);
        g_turn_len += (int) chunk;
    }
    g_turn_text[g_turn_len] = 0;
}

/*----------------------
 | music_on_turn
 | Description: The Dynamic-mode decision made once per turn (a no-op that just
 |   clears the buffer in other modes). obj is the room's Z-machine object number:
 |   scene_of_room looks up its authored scene directly, so there is no
 |   classification left to memoize. event_scan's result overrides the room's
 |   scene for the rest of the room's stay, same as before, but is kept in its own
 |   slot (g_event_cat) rather than folded into g_base_cat -- an SC_* and an EV_*
 |   value are different vocabularies that both start at 0.
 |
 |   scene_of_room returning -1 means "this room has no authored scene"; per its
 |   contract that is "hold whatever is showing", so with no event either the turn
 |   changes nothing at all rather than falling back to a category. If the target
 |   already sounds it keeps the stream; on the very first switch it plays
 |   immediately; otherwise it arms a debounced pending switch (restarting the
 |   countdown when the target changes), so brief passes through a room do not
 |   thrash the music.
 | Author: suinevere
 | Dependencies: scene/scene_map.h (scene_of_room), event_scan.h (event_scan)
 | Globals: g_mix_mode, g_turn_text, g_cur_room, g_have_room, g_base_cat,
 |   g_event_cat, g_active_kind, g_active_cat, g_active_track, g_pending_*,
 |   g_same_cat_rooms
 | Params: obj -- the current room's Z-machine object number
 | Returns: N/A
 ----------------------*/
/*----------------------
 | music_on_win
 | Description: The story ended itself. Plays the win pool at once, without
 |   the settle a room change gets: the settle exists so walking through a
 |   corridor does not thrash the mix, and there is no next room to walk to.
 |
 |   Called from mojo_run when the interpreter's quit flag is set, which for
 |   this build can only be the story's own ending routine -- a typed "quit"
 |   is intercepted by soft_reset before it ever reaches the interpreter.
 |   Losing arrives the other way, through music_on_turn's event scan, because
 |   death is a turn like any other and the game carries on after it.
 | Author: suinevere
 | Dependencies: pick_dynamic_track, play_dyn
 | Globals: g_mix_mode, g_active_kind, g_active_cat, g_event_cat,
 |   g_pending_*, g_same_cat_rooms
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_on_win(void) {
    g_event_cat = EV_WIN;
    if (g_mix_mode != MIX_DYNAMIC) return;
    g_pending_kind = CAT_KIND_NONE; g_pending_cat = -1;
    g_pending_track = 0; g_pending_rotate = 0; g_pending_frames = 0;
    g_active_kind = CAT_KIND_EVENT; g_active_cat = EV_WIN;
    g_same_cat_rooms = 0;
    play_dyn(pick_dynamic_track(CAT_KIND_EVENT, EV_WIN), 1);
}

void music_on_turn(unsigned int obj) {
    if (g_mix_mode != MIX_DYNAMIC) { g_turn_len = 0; g_turn_text[0] = 0; return; }

    int event_cat = event_scan(g_turn_text);
    int room_changed = (!g_have_room || obj != g_cur_room);
    if (room_changed) {
        Presentation p;
        int base;
        if (pres_of_room(g_release, g_serial, obj, &p)) {
            base = (int) p.track;
            g_base_kind = CAT_KIND_ROOM;
        } else {
            base = scene_of_room(g_release, g_serial, obj);
            g_base_kind = CAT_KIND_SCENE;
        }
        g_cur_room = obj; g_have_room = 1; g_base_cat = base; g_event_cat = -1;
        if (g_room_fn) g_room_fn(obj);
    }
    if (event_cat >= 0) g_event_cat = event_cat;

    int target_kind = (g_event_cat >= 0) ? CAT_KIND_EVENT
                     : (g_base_cat  >= 0) ? g_base_kind : CAT_KIND_NONE;
    if (target_kind == CAT_KIND_NONE) { g_turn_len = 0; g_turn_text[0] = 0; return; }
    int target = (g_event_cat >= 0) ? g_event_cat : g_base_cat;

    if (target_kind != g_active_kind || target != g_active_cat) {
        /* A real mood change. It supersedes any rotation that was waiting: the
           point of the rotation was to relieve an unchanging mood, and the mood
           just changed. */
        if (g_active_track == 0) {
            g_active_kind = target_kind; g_active_cat = target; g_same_cat_rooms = 0;
            notify_cat(target_kind, target); play_dyn(pick_dynamic_track(target_kind, target), 1);
        } else if (target != g_pending_cat || target_kind != g_pending_kind || g_pending_rotate) {
            g_pending_kind   = target_kind;
            g_pending_cat    = target;
            g_pending_track  = pick_dynamic_track(target_kind, target);
            g_pending_rotate = 0;
            g_pending_frames = g_debounce_frames;
        } else if (room_changed) {
            /* Same target, but they moved again. The rule is "stopped in one room
               for 1.5s", not "1.5s since the mood first changed" -- without this,
               walking a corridor of same-mood rooms commits shortly after arriving
               in one of them rather than after settling in it. */
            g_pending_frames = g_debounce_frames;
        }
    } else {
        /* The mood they are in is the one already sounding. */
        if (g_pending_cat >= 0 && !g_pending_rotate) {
            g_pending_kind = CAT_KIND_NONE; g_pending_cat = -1; g_pending_track = 0;   /* they came back; drop it */
        }
        if (room_changed) {
            g_same_cat_rooms++;
            if (g_pending_rotate) {
                g_pending_frames = g_debounce_frames;  /* still walking: keep settling */
            } else if (g_active_kind != CAT_KIND_ROOM
                       && g_same_cat_rooms >= MUSIC_ROTATE_ROOMS) {
                /* Three rooms of one scene. Move to another track in the same
                   category -- pick_dynamic_track steers off what is sounding, so
                   the change is audible -- and tell the art to move too.
                   A scene authored with a single track has nowhere to rotate:
                   the pick comes back as the track already playing, and
                   committing it would fade out and restart the same music
                   every third room. Leave it looping. */
                int next = pick_dynamic_track(g_active_kind, g_active_cat);
                if (next != g_active_track) {
                    g_pending_kind   = g_active_kind;
                    g_pending_cat    = g_active_cat;
                    g_pending_track  = next;
                    g_pending_rotate = 1;
                    g_pending_frames = g_debounce_frames;
                } else {
                    g_same_cat_rooms = 0;
                }
            }
        }
    }
    g_turn_len = 0; g_turn_text[0] = 0;
}
