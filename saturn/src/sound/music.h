/*----------------------
 | music.h
 | Description: The music engine's interface: mix modes and the track bounds;
 |   the tunable data-table accessor (music_data.c); the platform-independent
 |   engine (music.c); and the Saturn CD-DA backend (music_cdda.cxx). Every
 |   CD-DA track the client plays -- the title and menu track as much as the
 |   in-game one -- goes through this engine, so the MUSIC_DYN_LOOPS cycle
 |   rule holds in the menus and in Sound Options too, not only at the prompt.
 |
 |   The category argument music_track_pool and music_category_track take
 |   is a pool index rather than anything this header defines; music.h stopped
 |   owning that vocabulary once room mood moved from a text classifier to the
 |   story's own authored per-room table. The display subscribes
 |   to room changes via music_set_room_fn rather than re-deriving the
 |   mood on its own clock, so a picture cannot end up describing a mood the
 |   music has already left.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef MUSIC_H
#define MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | MUSIC_TRACK_MIN / MUSIC_TRACK_MAX
 | Description: The CD-DA track bounds -- a fixed offer, not a detected count
 |   (playing a track the disc does not carry is a harmless no-op). MIN is also
 |   what bit 0 of a scene's track mask names; see music_track_from_mask. The
 |   disc's real track list, where one is wanted, comes from
 |   music_cdda_audio_tracks().
 |
 |   There is no mix mode any more. Repeat, Sequential and Random were removed
 |   along with the track selector that fed them: the music is the dynamic
 |   engine or, at Music level 0, nothing.
 | Author: suinevere
 ----------------------*/
#define MUSIC_TRACK_MIN 2
#define MUSIC_TRACK_MAX 33

/*----------------------
 | music_track_pool (music_data.c)
 | Description: Returns a pool's track list (*out) and size. `category` is a
 |   pool selector -- EV_DANGER, EV_TRIUMPH, or the neutral fallback one past
 |   the last EV_* id -- not a room mood; see music_data.c's own comment.
 | Author: suinevere
 ----------------------*/
int music_track_pool(int category, const unsigned char** out);

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
 |   set_category_fn subscribes to the active SCENE, which is how the
 |   background art follows the room without re-deriving the mood on its own
 |   clock. The contract is scene-only: the callback fires with an SC_* value
 |   and nothing else -- an event (danger/triumph) taking over the track never
 |   reaches it, because an event carries no picture and the subscriber is
 |   expected to hold whatever it is already showing while one plays. set_rotate_fn
 |   is set_category_fn's sibling for the case the scene does NOT change: after
 |   MUSIC_ROTATE_ROOMS rooms of one mood the engine moves to another track in
 |   that same scene, and the art is expected to move with it -- same scene-only
 |   contract. They are separate calls rather than one with a flag because the
 |   client does genuinely different work -- resolve a new mood's picture, versus
 |   pick a different picture for the mood it is already in. set_fade_fn / set_fade_frames add a ramp that brackets
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
 | music_on_win
 | Description: The story ended itself -- call once, from the interpreter's
 |   run loop when its quit flag is set. Plays the win pool immediately.
 |   Losing needs no counterpart: it arrives through music_on_turn, because
 |   death is a turn like any other and play continues after it.
 | Author: suinevere
 ----------------------*/
void music_on_win(void);

/*----------------------
 | music_track_from_mask
 | Description: The r-th set bit of a pool mask, as a CD-DA track number. r is reduced modulo the
 |   number of set bits, so any value is legal and an empty mask answers 0
 |   rather than dividing by zero.
 | Author: suinevere
 ----------------------*/
int music_track_from_mask(unsigned long mask, unsigned int r);

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

/*----------------------
 | music_set_art_fn / music_art_change
 | Description: The picture's half of a room change, split across the two ends of
 |   the transition it belongs to. music_set_room_fn still fires the moment the
 |   room changes, and the client answers music_art_change with whether that room's
 |   picture differs from the one on screen. A yes arms the ramp by itself, so a
 |   walk into a new scene fades out, swaps and fades back in even when the mood
 |   and the track do not move -- before this, only a mood change ramped, and a
 |   picture that changed without one simply appeared. A no cancels an arm that a
 |   previous room in the same settle had earned.
 |
 |   The art_fn is called at the bottom of the ramp, where the screen is black and
 |   an area read costs nothing anyone can see, and before the new track is issued.
 |   The client must not put the picture up from the room_fn itself: doing that is
 |   what made a room change show the new picture at full brightness, then darken
 |   it, then light the same picture back up.
 | Author: suinevere
 ----------------------*/
void music_set_art_fn(void (*fn)(void));
void music_art_change(int changed);

/*----------------------
 | music_transition_skip_fade
 | Description: Commits an armed mood change with no ramp at all -- for a caller
 |   that knows there is nothing on screen for a ramp to move, which in practice
 |   means the opening room before the game has been revealed. The picture is
 |   still swapped and the track still started, in that order; only the fade is
 |   skipped. A no-op if nothing is armed, or if a ramp is already running.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_transition_skip_fade(void);
void music_refresh(void);   /* re-assert the current room's track (after a preview) */
void music_seed(unsigned int s);            /* seed the track-pool RNG */
int  music_category_track(int category);    /* random track from the category pool; 0 if none */
void music_start(void);                                /* clear the mix; the first room starts it */
void music_start_menu(void);                           /* ...for the menus, which have no room */
void music_tick(void);                                 /* per frame: commit/advance/re-pick */
void music_pause(void);                                /* hold the drive where it is */
void music_duck(void);                                 /* ...or leave it running, just quieter */
void music_resume(void);                               /* lift either hold, whichever was taken */
int  music_is_paused(void);                            /* 1 while held, so a page can put it back */
void music_set_isplaying(int (*fn)(void));             /* backend: 1 = CD-DA still playing */
void music_set_isshort(int (*fn)(int track));          /* backend: 1 = track plays once */
void music_set_pausefns(void (*pause_fn)(void), void (*resume_fn)(void));
void music_set_duckfns(void (*duck_fn)(void), void (*unduck_fn)(void));
void music_set_debounce_frames(int n);                 /* room-switch debounce length */

/*----------------------
 | music_set_room_fn
 | Description: Subscribes to every room change, for stories with an authored
 |   per-room presentation. set_category_fn cannot serve this: on that path the
 |   category is the track, so two rooms sharing a track are one category and
 |   the picture would never change between them. The picture needs the room,
 |   which is what this hands over.
 |
 |   Fired on the room change itself rather than at the debounced commit, because
 |   the client needs the room early enough to answer music_art_change with it. The
 |   picture it resolves is not put up there -- see music_set_art_fn.
 | Author: suinevere
 ----------------------*/
void music_set_room_fn(void (*fn)(unsigned int obj));
void music_set_fade_fn(void (*fn)(int level, int audio)); /* level: 0 = black/quiet, 255 = normal;
                                                          audio: 1 when the track is being re-issued
                                                          under this ramp and its volume must ride it */
void music_set_fade_frames(int n);                     /* ramp length; 0 = instant commit */

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
void music_cdda_duck(void);
void music_cdda_unduck(void);
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
