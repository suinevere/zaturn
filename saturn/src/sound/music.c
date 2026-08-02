/*----------------------
 | music.c
 | Description: The platform-independent music engine: room-text classification
 |   into mood categories, the mix-mode state machine (Dynamic / Override /
 |   Sequential / Random), and the loop-end / debounce logic that decides when to
 |   change track. It owns no hardware -- a backend play callback (set by the
 |   Saturn client, or by the host tests) does the actual playing, and is_playing
 |   / is_short callbacks report drive state -- so this file builds and unit-tests
 |   on the host.
 | Author: suinevere
 | Dependencies: music.h (categories, mix modes, track bounds, keyword/pool/
 |   room-category data accessors), string.h
 ----------------------*/
#include "music.h"
#include <string.h>

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
 | Description: Picks a uniformly random track from a category's pool.
 | Author: suinevere
 | Dependencies: music.h (music_category_pool)
 | Globals: g_rng (via rng_next)
 | Params: category -- MC_* category id
 | Returns: a track number from the pool, or 0 if the pool is empty
 ----------------------*/
int music_category_track(int category) {
    const unsigned char* p; int n = music_category_pool(category, &p);
    if (n <= 0) return 0;
    return p[rng_next() % (unsigned)n];
}

/*----------------------
 | lc
 | Description: Lowercases one ASCII byte.
 | Author: suinevere
 ----------------------*/
static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/*----------------------
 | has_word
 | Description: Case-insensitive whole-word search: true when `word` (stored
 |   lowercase) occurs in `text` bounded on both sides by a non-alphabetic char or
 |   a string end, so "cave" does not match "caverns".
 | Author: suinevere
 | Dependencies: string.h
 | Globals: N/A
 | Params: text -- haystack; word -- lowercase needle
 | Returns: 1 on a whole-word match, 0 otherwise
 ----------------------*/
static int has_word(const char* text, const char* word) {
    int wl = (int) strlen(word);
    for (const char* p = text; *p; p++) {
        int i = 0;
        while (i < wl && p[i] && lc(p[i]) == word[i]) i++;
        if (i == wl) {
            char before = (p == text) ? ' ' : p[-1];
            char after  = p[wl];
            int lb = !((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z'));
            int la = !((after  >= 'a' && after  <= 'z') || (after  >= 'A' && after  <= 'Z'));
            if (lb && la) return 1;
        }
    }
    return 0;
}

/*----------------------
 | TEXT_TITLE_MAX / TEXT_TITLE_WEIGHT
 | Description: How much of the room title is read, and how much more a keyword
 |   found in it counts for than the same keyword found in the description.
 |
 |   The title is what the room IS; the description is what can be seen from it,
 |   and the two disagree constantly. Zork I's opening room is titled "West of
 |   House" and described as an open field with a boarded white house in it -- one
 |   wilderness word against one town word, which the flat count below scored 1-1
 |   and broke by enum order, silently handing every house on the map to the
 |   forest. "North of House" and "Behind House" were worse: they mention trees,
 |   a path and a forest, so the house lost outright 2-1 in its own room.
 |
 |   Weighting the title fixes those without a per-game table, because a title is
 |   authored to name the place while a description is authored to be walked
 |   around in. 2 is deliberately modest -- a title word ends up worth 3 (once as
 |   part of the text, twice as the title), so two agreeing description words can
 |   still outvote a title that is merely a direction, e.g. "Up a Tree" in a room
 |   that is really about the forest around it.
 | Author: suinevere
 ----------------------*/
#define TEXT_TITLE_MAX    64
#define TEXT_TITLE_WEIGHT 2

/*----------------------
 | g_room_title
 | Description: The room name the interpreter decoded for this turn, empty when
 |   nothing supplied one.
 | Author: suinevere
 ----------------------*/
static char g_room_title[TEXT_TITLE_MAX];

/*----------------------
 | music_note_room_title
 | Description: Records the authoritative room name for the turn about to be
 |   classified, which the interpreter reads off the location object rather than
 |   guessing from printed text.
 |
 |   It exists because the printed text lies on turn one. Zork I opens with its
 |   banner -- "ZORK I: The Great Underground Empire" -- above the room, so the
 |   first-line heuristic below read that as the title, handed "underground" the
 |   title weight, and put West of House in a bunker. Any game whose banner names a
 |   place does the same, and so does any turn that prints something before the
 |   room description.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room_title
 | Params: title -- the room name, truncated to TEXT_TITLE_MAX-1; NULL clears it
 | Returns: N/A
 ----------------------*/
void music_note_room_title(const char* title) {
    int i = 0;
    if (title) for (; title[i] && i < TEXT_TITLE_MAX - 1; i++) g_room_title[i] = title[i];
    g_room_title[i] = 0;
}

/*----------------------
 | text_room_title
 | Description: Copies the first non-blank line of a turn's text into `out` (at
 |   most TEXT_TITLE_MAX-1 chars), which on the turn a room is entered is the room
 |   title the interpreter just printed above the description.
 |
 |   "On the turn a room is entered" is the whole of the contract, and it holds
 |   because music_on_turn only classifies when the room number changed -- so the
 |   buffer being read is the one that opened with the title. On any other turn
 |   this would return the first line of whatever the game said, which is why
 |   nothing else calls it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: text -- the turn's output; out -- TEXT_TITLE_MAX bytes of destination
 | Returns: N/A
 ----------------------*/
static void text_room_title(const char* text, char* out) {
    int i = 0, n = 0;
    while (text[i] == '\n' || text[i] == '\r' || text[i] == ' ' || text[i] == '\t') i++;
    while (text[i] && text[i] != '\n' && text[i] != '\r' && n < TEXT_TITLE_MAX - 1)
        out[n++] = text[i++];
    out[n] = 0;
}

/*----------------------
 | text_classify_room
 | Description: Scores room text against the keyword table and returns the
 |   category with the most keyword hits, defaulting to TC_NEUTRAL on a tie at
 |   zero. Keywords in the room's title count for TEXT_TITLE_WEIGHT more than the
 |   same word in the description -- see that box. This is the fallback when a game
 |   has no per-room category map.
 | Author: suinevere
 | Dependencies: music.h (text_keywords, TC_* / TEXT_NUM_CATEGORIES)
 | Globals: N/A
 | Params: text -- the turn's text, opening with the room title (NULL -> TC_NEUTRAL)
 | Returns: the winning TC_* category
 ----------------------*/
int text_classify_room(const char* text) {
    if (!text) return TC_NEUTRAL;
    char firstline[TEXT_TITLE_MAX];
    const char* title;
    int counts[TEXT_NUM_CATEGORIES];
    for (int i = 0; i < TEXT_NUM_CATEGORIES; i++) counts[i] = 0;
    if (g_room_title[0] != 0) {
        title = g_room_title;
    } else {
        text_room_title(text, firstline);
        title = firstline;
    }
    int nk = 0; const TextKeyword* kw = text_keywords(&nk);
    for (int i = 0; i < nk; i++) {
        /* The title is part of the text, so a title word scores in both passes --
           1 + TEXT_TITLE_WEIGHT -- while a description-only word scores 1. */
        if (has_word(text,  kw[i].word)) counts[kw[i].cat]++;
        if (has_word(title, kw[i].word)) counts[kw[i].cat] += TEXT_TITLE_WEIGHT;
    }
    /* Starts past TC_NEUTRAL on purpose: it is the nothing-matched answer, not
       something a keyword can vote for, so a hit scored into it could never be
       acted on. TC_HOUSE exists precisely so the domestic words have a real
       category to win -- see the keyword block in music_data.c. */
    int best = TC_NEUTRAL, bestn = 0;
    for (int c = TC_WILDERNESS; c <= TC_PLACE_LAST; c++)
        if (counts[c] > bestn) { bestn = counts[c]; best = c; }
    return best;
}

/*----------------------
 | text_scan_event
 | Description: Looks for an event keyword (combat, death, etc.) in the turn text,
 |   returning the first match's category so an event track can override the
 |   room's base mood for that turn.
 | Author: suinevere
 | Dependencies: music.h (text_events)
 | Globals: N/A
 | Params: text -- the turn's output text (NULL -> no event)
 | Returns: the event category, or -1 when none is present
 ----------------------*/
int text_scan_event(const char* text) {
    if (!text) return -1;
    int ne = 0; const TextKeyword* ev = text_events(&ne);
    for (int i = 0; i < ne; i++) if (has_word(text, ev[i].word)) return ev[i].cat;
    return -1;
}

/*----------------------
 | MUSIC_TEXT_MAX
 | Description: Capacity of the per-turn text buffer the classifiers read.
 | Author: suinevere
 ----------------------*/
#define MUSIC_TEXT_MAX 512

/*----------------------
 | engine state (g_play .. g_turn_len)
 | Description: The core engine state. g_play is the backend play callback; the
 |   room block (g_release/g_serial identify the loaded game for its per-room
 |   category map; g_have_room/g_cur_room track the last classified room;
 |   g_base_cat is that room's mood and g_event_cat a per-room event override,
 |   -1 = none); g_active_track is what is sounding (0 = nothing yet); g_room_cache
 |   memoizes classify results (0 = unseen, else cat+1); g_turn_text/g_turn_len
 |   accumulate the current turn's output for classification.
 | Author: suinevere
 ----------------------*/
static music_play_fn g_play = 0;
static unsigned int  g_release = 0;
static char          g_serial[8] = {0};
static int           g_have_room = 0;
static unsigned int  g_cur_room = 0;
static int           g_base_cat = TC_NEUTRAL;
static int           g_event_cat = -1;
static int           g_active_track = 0;
static unsigned char g_room_cache[256];
static char          g_turn_text[MUSIC_TEXT_MAX];
static int           g_turn_len = 0;

/*----------------------
 | mix state (g_mix_mode .. g_isshort)
 | Description: Mix-mode selection and its bookkeeping. g_mix_mode is the active
 |   MIX_*; g_override_track/g_seq_track carry the Override and Sequential
 |   positions; g_active_cat is the category currently sounding (-1 = none), with
 |   g_pending_cat/g_pending_track/g_pending_frames the debounced switch waiting
 |   to commit. MUSIC_DEBOUNCE_FRAMES (1.5s @ 60fps) is how long a new room must
 |   hold before its track -- and its picture -- take over; g_debounce_frames is
 |   the runtime override (host tests shorten it). g_isplaying/g_isshort are the
 |   drive-state callbacks.
 | Author: suinevere
 ----------------------*/
static int g_mix_mode = MIX_DYNAMIC;
static int g_override_track = 10;
static int g_seq_track = MUSIC_TRACK_MIN;
static int g_active_cat = -1;
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
 |   The active category is the only thing that decides which track sounds, and it
 |   is now also what decides which picture shows; publishing it here rather than
 |   letting the client re-derive it from the same text keeps the two on one
 |   event, so a picture cannot end up describing a mood the music has already
 |   left. Optional: with nothing installed the engine behaves exactly as it did
 |   before, which is what the pre-existing host tests assume.
 | Author: suinevere
 ----------------------*/
static void (*g_cat_fn)(int) = 0;
void music_set_category_fn(void (*fn)(int cat)) { g_cat_fn = fn; }
static void notify_cat(int cat) { if (g_cat_fn) g_cat_fn(cat); }

/*----------------------
 | g_rot_fn / music_set_rotate_fn / notify_rotate / g_same_cat_rooms
 | Description: The same-category rotation subscriber, and the room counter that
 |   triggers it. g_same_cat_rooms counts rooms entered since the last commit
 |   while the mood held; at MUSIC_ROTATE_ROOMS the engine cycles to another track
 |   in the category it is already in and tells the art to follow.
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
static void notify_rotate(int cat) { if (g_rot_fn) g_rot_fn(cat); }

/*----------------------
 | g_paused / g_pause_fn / g_resume_fn / music_set_pausefns
 | Description: The pause latch and the backend calls behind it. While g_paused is
 |   set, music_tick does nothing at all: a paused drive reports not-playing, which
 |   is the same reading as loop-end, and without this latch every frame of an open
 |   menu would look like a track ending and start another one.
 | Author: suinevere
 ----------------------*/
static int g_paused = 0;
static void (*g_pause_fn)(void) = 0;
static void (*g_resume_fn)(void) = 0;

void music_set_pausefns(void (*pause_fn)(void), void (*resume_fn)(void)) {
    g_pause_fn = pause_fn; g_resume_fn = resume_fn;
}

/*----------------------
 | music_pause / music_resume / music_is_paused
 | Description: Stop the drive where it stands and pick the same track up from that
 |   frame. Both are idempotent, and pause is a no-op when nothing is playing, so a
 |   caller can bracket a menu with them without first asking what the music is
 |   doing. Resume re-arms the seen-playing latch because the drive has to seek back
 |   before is_playing() means anything again -- without that, the first tick after
 |   a resume reads the seek as loop-end.
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
    if (g_paused || g_active_track <= 0) return;
    g_paused = 1;
    if (g_pause_fn) g_pause_fn();
}

void music_resume(void) {
    if (!g_paused) return;
    g_paused = 0;
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
 | Description: Chooses a track from `cat`'s pool, preferring a non-short one and,
 |   where the pool allows, one other than what is sounding now -- a category
 |   change should be audible, so it cycles off the current track instead of
 |   possibly re-rolling it. Falls back to any long track if the only long option
 |   is the current one, then to any track at all.
 | Author: suinevere
 | Dependencies: music.h (music_category_pool)
 | Globals: g_active_track (read)
 | Params: cat -- MC_* category to pick from
 | Returns: a track number, or 0 if the pool is empty
 ----------------------*/
static int pick_prefer_long(int cat) {
    const unsigned char* p; int n = music_category_pool(cat, &p);
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
 | music_set_backend
 | Description: Installs the backend play callback (track, loop) the engine drives.
 | Author: suinevere
 ----------------------*/
void music_set_backend(music_play_fn play) { g_play = play; }

/*----------------------
 | music_set_game
 | Description: Records the loaded game's release number and (truncated) serial so
 |   music_on_turn can consult that game's per-room category map.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_release, g_serial
 | Params: release -- Z-machine release word; serial -- 6-char serial (may be short)
 | Returns: N/A
 ----------------------*/
void music_set_game(unsigned int release, const char* serial) {
    g_release = release;
    for (int i = 0; i < 6 && serial && serial[i]; i++) g_serial[i] = serial[i];
    g_serial[6] = 0;
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
 | Globals: g_active_cat, g_pending_cat, g_pending_track
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void commit_pending(void) {
    int t = g_pending_track;
    int rotate = g_pending_rotate;
    g_active_cat = g_pending_cat;
    g_pending_cat = -1; g_pending_track = 0; g_pending_rotate = 0;
    g_same_cat_rooms = 0;          // the walk that earned this rotation is spent
    if (rotate) notify_rotate(g_active_cat);
    else        notify_cat(g_active_cat);
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
 | Description: Clears all room / mix / latch state back to first-boot values and
 |   tells the backend to stop. Called for a new game and on soft-reset re-entry
 |   so a stale engine cannot leak a track into the menu.
 | Author: suinevere
 | Dependencies: N/A (stops via g_play)
 | Globals: nearly all engine/mix state
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_reset(void) {
    g_have_room = 0; g_cur_room = 0; g_base_cat = TC_NEUTRAL; g_event_cat = -1;
    g_room_title[0] = 0;
    g_active_track = 0; g_turn_len = 0; g_turn_text[0] = 0;
    for (int i = 0; i < 256; i++) g_room_cache[i] = 0;
    g_active_cat = -1; g_pending_cat = -1; g_pending_track = 0; g_pending_frames = 0;
    g_pending_rotate = 0; g_same_cat_rooms = 0;
    g_seq_track = MUSIC_TRACK_MIN;
    g_await_play = 0;
    g_dyn_pass = 0;
    g_paused = 0;   // a soft reset can land here with a menu still nominally open
    if (g_phase != MP_IDLE) fade_emit(255);   // mid-ramp: nothing else would lift it
    g_phase = MP_IDLE; g_fade_i = 0;
    // Announce the neutral mood so the track comes off whatever the last room
    // set. The WALLPAPER deliberately does not follow: TC_NEUTRAL carries no art
    // (see CATEGORY_IMAGE in display.c), so the picture holds. That is fine on
    // every path that reaches here -- main.cxx shows the title picture explicitly
    // straight afterwards, and at game start the loading screen is still up.
    notify_cat(TC_NEUTRAL);
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
 | Globals: g_mix_mode, g_active_cat, g_pending_*, g_active_track, g_seq_track
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_start(void) {
    g_active_cat = -1; g_pending_cat = -1; g_pending_track = 0;
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
 |   MUSIC_DYN_LOOPS passes, cycles on through TC_NEUTRAL -- the pool that means "no
 |   particular mood", which is exactly what a menu is. Deliberately does NOT clear
 |   an established category the way music_start does: called from Sound Options
 |   mid-game, the room the player is standing in still sets the mood, and only a
 |   screen reached before any room has been seen falls back to neutral.
 | Author: suinevere
 | Dependencies: N/A (plays via g_play)
 | Globals: g_mix_mode, g_active_cat, g_pending_*, g_active_track, g_seq_track
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_start_menu(void) {
    g_pending_cat = -1; g_pending_track = 0;
    switch (g_mix_mode) {
        case MIX_OVERRIDE:
        case MIX_SEQUENTIAL:
        case MIX_RANDOM:
            music_start();
            break;
        case MIX_DYNAMIC: default:
            if (g_active_cat < 0) { g_active_cat = TC_NEUTRAL; notify_cat(TC_NEUTRAL); }
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
 |   the mood has moved. pick_prefer_long steers off the current track where the
 |   pool has a second long option, so the cycle is audible. This runs wherever
 |   music_tick does, so the passes keep counting in the in-game menus as well as at
 |   the prompt. Override repeats on its own, so there is nothing to do.
 | Author: suinevere
 | Dependencies: N/A (plays via g_play, reads g_isplaying)
 | Globals: g_pending_*, g_await_play, g_active_track, g_active_cat, g_dyn_pass,
 |   g_seq_track, g_mix_mode
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_tick(void) {
    if (g_paused) return;
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
                fade_out_step();       // the ramp starts on this frame, not the next
            } else {
                commit_pending();
            }
        }
        return;
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
        } else if (g_mix_mode == MIX_DYNAMIC && g_active_cat >= 0) {
            if (g_dyn_pass < MUSIC_DYN_LOOPS) play_dyn(g_active_track, g_dyn_pass + 1);
            else                              play_dyn(pick_prefer_long(g_active_cat), 1);
        }
    }
}

/*----------------------
 | music_note_output
 | Description: Appends up to MUSIC_TEXT_MAX-1 bytes of the turn's output text to
 |   the classification buffer, stopping at a NUL, so music_on_turn can read the
 |   full turn.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_turn_text, g_turn_len
 | Params: str -- output text; len -- its length
 | Returns: N/A
 ----------------------*/
void music_note_output(const char* str, unsigned int len) {
    for (unsigned int i = 0; i < len && str[i]; i++) {
        if (g_turn_len < MUSIC_TEXT_MAX - 1) g_turn_text[g_turn_len++] = str[i];
    }
    g_turn_text[g_turn_len] = 0;
}

/*----------------------
 | music_on_turn
 | Description: The Dynamic-mode decision made once per turn (a no-op that just
 |   clears the buffer in other modes). Determines the target category -- an event
 |   keyword overrides the room's base mood, which comes from the game's per-room
 |   map, else a memoized keyword classification. If the target already sounds it
 |   keeps the stream; on the very first switch it plays immediately; otherwise it
 |   arms a debounced pending switch (restarting the countdown when the target
 |   changes), so brief passes through a room do not thrash the music.
 | Author: suinevere
 | Dependencies: music.h (text_game_room_category)
 | Globals: g_mix_mode, g_turn_text, g_cur_room, g_have_room, g_base_cat,
 |   g_event_cat, g_room_cache, g_active_cat, g_active_track, g_pending_*
 | Params: room -- the current room number
 | Returns: N/A
 ----------------------*/
void music_on_turn(unsigned int room) {
    if (g_mix_mode != MIX_DYNAMIC) { g_turn_len = 0; g_turn_text[0] = 0; return; }

    int event_cat = text_scan_event(g_turn_text);
    int room_changed = (!g_have_room || room != g_cur_room);
    if (room_changed) {
        int base = text_game_room_category(g_release, g_serial, room);
        if (base < 0) {
            unsigned char cached = (room < 256) ? g_room_cache[room] : 0;
            if (cached) base = cached - 1;
            else { base = text_classify_room(g_turn_text); if (room < 256) g_room_cache[room] = (unsigned char)(base + 1); }
        }
        g_cur_room = room; g_have_room = 1; g_base_cat = base; g_event_cat = -1;
    }
    if (event_cat >= 0) g_event_cat = event_cat;

    int target = (g_event_cat >= 0) ? g_event_cat : g_base_cat;
    if (target != g_active_cat) {
        /* A real mood change. It supersedes any rotation that was waiting: the
           point of the rotation was to relieve an unchanging mood, and the mood
           just changed. */
        if (g_active_track == 0) {
            g_active_cat = target; g_same_cat_rooms = 0;
            notify_cat(target); play_dyn(pick_prefer_long(target), 1);
        } else if (target != g_pending_cat || g_pending_rotate) {
            g_pending_cat    = target;
            g_pending_track  = pick_prefer_long(target);
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
            g_pending_cat = -1; g_pending_track = 0;   /* they came back; drop it */
        }
        if (room_changed) {
            g_same_cat_rooms++;
            if (g_pending_rotate) {
                g_pending_frames = g_debounce_frames;  /* still walking: keep settling */
            } else if (g_same_cat_rooms >= MUSIC_ROTATE_ROOMS) {
                /* Three rooms of one mood. Move to another track in the same
                   category -- pick_prefer_long steers off what is sounding, so
                   the change is audible -- and tell the art to move too. */
                g_pending_cat    = g_active_cat;
                g_pending_track  = pick_prefer_long(g_active_cat);
                g_pending_rotate = 1;
                g_pending_frames = g_debounce_frames;
            }
        }
    }
    g_turn_len = 0; g_turn_text[0] = 0;
}
