/*----------------------
 | music.h
 | Description: The music engine's interface: the text categories, mix modes, and
 |   the track bounds; the tunable data-table accessors (music_data.c); the
 |   platform-independent engine (music.c); the pure classifiers exposed for
 |   tests; and the Saturn CD-DA backend (music_cdda.cxx). Every CD-DA track the
 |   client plays -- the title and menu track as much as the in-game one -- goes
 |   through this engine, so the MUSIC_DYN_LOOPS cycle rule holds in the menus and
 |   in Sound Options too, not only at the prompt.
 |
 |   The category is named for where it comes from (the text on screen) rather
 |   than what it drives, because it now drives two things: the CD-DA track, and
 |   the background picture. The display subscribes via music_set_category_fn
 |   rather than re-deriving the mood from the same text, so a picture cannot end
 |   up describing a mood the music has already left. The TC_* names and the
 |   text_* classifiers are that shared vocabulary; the music_* names either side
 |   of them (music_category_pool, music_category_track) are the part that is
 |   genuinely about tracks.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef MUSIC_H
#define MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | TEXT_NUM_CATEGORIES / TC_* / MIX_* / MUSIC_TRACK_MIN / MUSIC_TRACK_MAX
 | Description: The text categories (TC_NEUTRAL..TC_TRIUMPH) and their count -- the
 |   mood read off the screen, which selects both the track and the background
 |   picture; the Audio Mix modes from Sound Options; and the track bounds.
 |   TC_NEUTRAL..TC_PLACE_LAST are places, classified from the room's text and
 |   memoized per room; TC_DANGER and TC_TRIUMPH are moments, scanned from each
 |   turn's text, and carry no picture of their own. MUSIC_TRACK_MAX is
 |   the ceiling for Sequential/Random and the override clamp -- a fixed offer, not
 |   a detected count (playing a missing track is a harmless no-op); the Sound
 |   Options track selector instead lists the disc's real tracks from
 |   music_cdda_audio_tracks().
 | Author: suinevere
 ----------------------*/
#define TEXT_NUM_CATEGORIES 15
enum {
    TC_NEUTRAL = 0, TC_WILDERNESS, TC_UNDERGROUND, TC_WATER, TC_NAUTICAL,
    TC_TOWN, TC_DUNGEON, TC_DESERT, TC_MAGIC, TC_SCIFI, TC_HORROR,
    TC_MYSTERY, TC_HOUSE, TC_DANGER, TC_TRIUMPH
};

/*----------------------
 | TC_PLACE_LAST
 | Description: The last category a room can classify as, so the two event
 |   categories after it are excluded by construction rather than by everyone
 |   remembering to stop before them.
 |
 |   Worth a name because it has already moved once. TC_HOUSE was appended at the
 |   END of the places rather than slotted next to TC_TOWN where it reads more
 |   naturally, deliberately: the category index is the row index of
 |   music_data.c's CATEGORY_POOL and display.c's CATEGORY_IMAGE, and inserting in
 |   the middle would have silently repointed every table row after TC_TOWN.
 |   Appending costs one out-of-order enum entry and shifts nothing.
 | Author: suinevere
 ----------------------*/
#define TC_PLACE_LAST TC_HOUSE
enum { MIX_DYNAMIC = 0, MIX_OVERRIDE = 1, MIX_SEQUENTIAL = 2, MIX_RANDOM = 3 };
#define MUSIC_TRACK_MIN 2
#define MUSIC_TRACK_MAX 33

/*----------------------
 | TextKeyword
 | Description: One keyword -> text-category mapping row (used by both the
 |   room-keyword and event-keyword tables).
 | Author: suinevere
 ----------------------*/
typedef struct { const char* word; unsigned char cat; } TextKeyword;

/*----------------------
 | data-table accessors (music_data.c)
 | Description: text_keywords / text_events return the room-keyword
 |   (TC_WILDERNESS..TC_PLACE_LAST) and event-word (TC_DANGER/TC_TRIUMPH) tables
 |   and their lengths; music_category_pool returns a category's track pool (*out)
 |   and size; text_game_room_category returns a game's authored room category, or
 |   -1 if none. Named rather than numbered because the numbers shifted when
 |   TC_HOUSE was added, and this line read "cats 1..11" for a while afterwards.
 | Author: suinevere
 ----------------------*/
const TextKeyword* text_keywords(int* n);
const TextKeyword* text_events(int* n);
int music_category_pool(int category, const unsigned char** out);
int text_game_room_category(unsigned int release, const char* serial,
                             unsigned int room);

/*----------------------
 | music_play_fn / MUSIC_DYN_LOOPS
 | Description: The backend play callback: play CD-DA `track` (0 = stop), repeating
 |   forever when loop is 1 and once when 0. MUSIC_DYN_LOOPS is how many times a
 |   Dynamic-mode track plays before the engine moves to another track in the same
 |   category. Those passes are counted by the engine, which replays the track
 |   one-shot each time, rather than being asked for in one go: the CD block does
 |   have a repeat-count field, and handing it 3 there would have been gapless, but
 |   on hardware it yielded a single pass and an immediate cycle. Re-issuing costs a
 |   seek between passes and buys a rule that actually holds.
 | Author: suinevere
 ----------------------*/
typedef void (*music_play_fn)(int track, int loop);
#define MUSIC_DYN_LOOPS 3

/*----------------------
 | MUSIC_ROTATE_ROOMS
 | Description: How many rooms of one unbroken mood pass before the engine moves
 |   to another track in that category anyway -- and tells the art to move with
 |   it. MUSIC_DYN_LOOPS counts playthroughs, which only advances while the player
 |   stands still; this counts rooms, which only advances while they walk. The two
 |   run alongside each other and whichever comes first wins, so neither exploring
 |   nor idling can hold one track indefinitely.
 | Author: suinevere
 ----------------------*/
#define MUSIC_ROTATE_ROOMS 3

/*----------------------
 | engine (music.c)
 | Description: The platform-independent engine. reset clears state; set_backend/
 |   set_game/note_output/on_turn feed it the backend, loaded game, turn text, and
 |   room; refresh re-asserts the current track; seed seeds the pool RNG;
 |   category_track picks a random pool track; set_mix/start/tick drive the mix
 |   state machine per frame; set_isplaying/set_isshort install the drive-state
 |   callbacks; set_debounce_frames tunes the room-switch debounce.
 |
 |   set_category_fn subscribes to the active text category, which is how the
 |   background art follows the room without re-deriving the mood from the same
 |   text on its own clock. set_rotate_fn is its sibling for the case the category
 |   does NOT change: after MUSIC_ROTATE_ROOMS rooms of one mood the engine moves
 |   to another track in that same category, and the art is expected to move with
 |   it. They are separate calls rather than one with a flag because the client
 |   does genuinely different work -- resolve a new mood's picture, versus pick a
 |   different picture for the mood it is already in. set_fade_fn / set_fade_frames add a ramp that brackets
 |   a Dynamic commit: a commit issues a fresh play, so the audio has to be down
 |   before it happens and come up after, and one counter driving both the picture
 |   and the volume is what keeps them in step. set_fade_frames(0) is the default
 |   and skips the ramp entirely.
 | Author: suinevere
 ----------------------*/
void music_reset(void);
void music_set_backend(music_play_fn play);
void music_set_game(unsigned int release, const char* serial);
void music_note_output(const char* str, unsigned int len);
void music_on_turn(unsigned int room);

/*----------------------
 | music_note_room_title
 | Description: Hands the engine the room name the interpreter decoded from the
 |   location object, to be used as the title for the next classification instead
 |   of the first line of printed text; call it before music_on_turn. Passing NULL
 |   or "" falls back to that first line.
 | Author: suinevere
 ----------------------*/
void music_note_room_title(const char* title);

/*----------------------
 | music_transition_active / music_transition_flush
 | Description: active reports whether a Dynamic mood change is still owed frames
 |   (armed, or mid-fade); flush drops its remaining settle so it begins at once.
 |   Together they let the client run a room's picture-and-track change to
 |   completion before that room's text is drawn, rather than printing the text and
 |   changing the mood underneath it a second and a half later.
 | Author: suinevere
 ----------------------*/
int  music_transition_active(void);
void music_transition_flush(void);
void music_refresh(void);   /* re-assert the current room's track (after a preview) */
void music_seed(unsigned int s);            /* seed the track-pool RNG */
int  music_category_track(int category);    /* random track from the category pool; 0 if none */
void music_set_mix(int mode, int override_track);      /* mix mode + selected/override track */
void music_start(void);                                /* assert playback for the current mode */
void music_start_menu(void);                           /* ...for the menus, which have no room */
void music_tick(void);                                 /* per frame: commit/advance/re-pick */
void music_pause(void);                                /* hold the drive where it is */
void music_resume(void);                               /* ...and pick the track up there */
int  music_is_paused(void);                            /* 1 while held, so a page can put it back */
void music_set_isplaying(int (*fn)(void));             /* backend: 1 = CD-DA still playing */
void music_set_isshort(int (*fn)(int track));          /* backend: 1 = track plays once */
void music_set_pausefns(void (*pause_fn)(void), void (*resume_fn)(void));
void music_set_debounce_frames(int n);                 /* room-switch debounce length */
void music_set_category_fn(void (*fn)(int cat));       /* announce category changes */
void music_set_rotate_fn(void (*fn)(int cat));         /* ...and same-category rotations */
void music_set_fade_fn(void (*fn)(int level));         /* 0 = black/quiet, 255 = normal */
void music_set_fade_frames(int n);                     /* ramp length; 0 = instant commit */

/*----------------------
 | classifiers (music.c, exposed for tests)
 | Description: classify_room returns a room's mood from its text -- one of
 |   TC_WILDERNESS..TC_PLACE_LAST, or TC_NEUTRAL when nothing matched; scan_event
 |   returns an event category (TC_DANGER/TC_TRIUMPH) from turn text, or -1.
 |   Spelled with the names rather than raw indices on purpose: the numbers moved
 |   when TC_HOUSE was added, and this comment said "1..11" for a while after.
 | Author: suinevere
 ----------------------*/
int text_classify_room(const char* text);
int text_scan_event(const char* text);

/*----------------------
 | CD-DA backend (music_cdda.cxx)
 | Description: play_mode starts a track (0 = stop), repeating forever or once per
 |   music_play_fn's loop flag. There is deliberately no play-this-looped wrapper
 |   beside it: everything that starts CD-DA goes through the engine, so that the
 |   cycle rule cannot be bypassed by calling the backend directly -- which is
 |   exactly how the menu track came to repeat one track forever.
 |   set_level/set_volume set the 0..7 output level (set_volume never restarts the
 |   track); pause/resume stop the drive and pick the same track up from the frame
 |   it stopped on; is_playing/is_short report drive state; audio_tracks lists the disc's
 |   real audio track numbers (*out) and count; has_audio is 1 if the disc carries
 |   any; current_track is the last track handed to the CD block (0 = none).
 | Author: suinevere
 ----------------------*/
void music_cdda_play_mode(int track, int loop);
void music_cdda_pause(void);
void music_cdda_resume(void);
void music_set_level(int level);
void music_set_volume(int level);
int  music_cdda_is_playing(void);
int  music_cdda_is_short(int track);
int  music_cdda_audio_tracks(const unsigned char** out);
int  music_cdda_has_audio(void);
int  music_cdda_current_track(void);

#ifdef __cplusplus
}
#endif
#endif /* MUSIC_H */
