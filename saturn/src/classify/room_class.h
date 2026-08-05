/*----------------------
 | room_class.h
 | Description: Room and event classification: the keyword row type, the two
 |   keyword-table accessors, and the two classifiers that turn a turn's text
 |   into a TC_* category.
 |
 |   Split out of music.c because the category is not a sound concern -- it
 |   drives the background picture as much as the CD-DA track, and it only lived
 |   beside the engine because music consumed it first. music.c keeps the per-room
 |   memo cache and the playback decisions; everything about reading text lives
 |   here.
 |
 |   Includes music.h for the TC_* ids and TEXT_NUM_CATEGORIES rather than moving
 |   them: the category id is the row index of music_data.c's CATEGORY_POOL and
 |   display.c's CATEGORY_IMAGE, so the enum stays where both of those already
 |   reach it.
 | Author: suinevere
 | Dependencies: music.h (TC_*, TEXT_NUM_CATEGORIES)
 ----------------------*/
#ifndef ROOM_CLASS_H
#define ROOM_CLASS_H

#include "music.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | KT_* / KT_NUM_TIERS
 | Description: How reliably a keyword names the room itself. Structure is what
 |   the player is standing in (cave, house, ship); Biome is the general natural
 |   area (forest, desert, sea); Feature is a thing that sits inside one of those
 |   (tree, boulder, rug).
 |
 |   The tier is a property of the WORD, not of the category it votes for -- both
 |   "cave" and "house" are Structure though they vote for different moods. It is
 |   compared before any count, so one Structure hit beats any number of Biome
 |   hits: a lake in a cave is a cave, by construction rather than by tuning a
 |   weight until it happens to come out right.
 | Author: suinevere
 ----------------------*/
enum { KT_STRUCTURE = 0, KT_BIOME = 1, KT_FEATURE = 2 };
#define KT_NUM_TIERS 3

/*----------------------
 | GN_* / GN_ANY
 | Description: Which genres a keyword votes in, as a bitmask. Most words mean
 |   the same thing everywhere and carry GN_ANY; a handful do not, and get one
 |   row per reading. "ship" is TC_NAUTICAL in a fantasy or period game and
 |   TC_SCIFI in a space one, which no single-answer table could express.
 |
 |   While a game's genre is unresolved, a row whose mask is not GN_ANY does not
 |   vote at all. Abstaining leaves the room to its other evidence; guessing puts
 |   sailing-ship art on a starship.
 | Author: suinevere
 ----------------------*/
#define GN_FANTASY 0x01
#define GN_SCIFI   0x02
#define GN_MODERN  0x04
#define GN_ANY     0xFF

/*----------------------
 | TextKeyword
 | Description: One keyword -> text-category mapping row, used by both the room
 |   and event tables. `tier` is unused on event rows (KT_FEATURE by convention),
 |   and `genre` is GN_ANY on every event row.
 | Author: suinevere
 ----------------------*/
typedef struct {
    const char*   word;
    unsigned char cat;
    unsigned char tier;
    unsigned char genre;
} TextKeyword;

/*----------------------
 | GenreKeyword
 | Description: One marker word and the single genre it identifies. Separate from
 |   TextKeyword because a marker votes for a genre, not for a mood.
 | Author: suinevere
 ----------------------*/
typedef struct {
    const char*   word;
    unsigned char genre;
} GenreKeyword;

/*----------------------
 | table accessors (room_class_data.c)
 | Description: text_keywords returns the room-keyword table
 |   (TC_WILDERNESS..TC_PLACE_LAST) and its length; text_events returns the
 |   event-word table (TC_DANGER/TC_TRIUMPH) and its length. text_neg_phrases and
 |   text_pos_phrases return the spatial-modifier tables a sentence is checked
 |   against before its keyword hits are counted.
 | Author: suinevere
 ----------------------*/
const TextKeyword* text_keywords(int* n);
const TextKeyword* text_events(int* n);
const char* const* text_neg_phrases(int* n);
const char* const* text_pos_phrases(int* n);

/*----------------------
 | text_genre_keywords / text_game_genre
 | Description: genre_keywords returns the marker table used to infer an unlisted
 |   game's genre and its length; game_genre returns a story's authored genre
 |   mask, or 0 when it is not listed and inference should run instead.
 | Author: suinevere
 ----------------------*/
const GenreKeyword* text_genre_keywords(int* n);
unsigned char       text_game_genre(unsigned int release, const char* serial);

/*----------------------
 | classifiers
 | Description: text_classify_room returns a room's mood from its text -- one of
 |   TC_WILDERNESS..TC_PLACE_LAST, or TC_NEUTRAL when nothing matched.
 |   text_scan_event returns an event category from turn text, or -1.
 | Author: suinevere
 ----------------------*/
int text_classify_room(const char* text);
int text_scan_event(const char* text);

/*----------------------
 | room_class_note_title / room_class_reset
 | Description: note_title records the authoritative room name the interpreter
 |   read off the location object, to be weighted as the title for the next
 |   classification instead of the first printed line; NULL or "" falls back to
 |   that first line. reset clears it, so one game's room cannot leak into the
 |   next.
 | Author: suinevere
 ----------------------*/
void room_class_note_title(const char* title);
void room_class_reset(void);

/*----------------------
 | room_class_set_game / room_class_genre_locked
 | Description: set_game identifies the loaded story so its authored genre can be
 |   looked up; an unlisted game starts unresolved and infers from marker words
 |   instead. genre_locked reports whether the genre has settled, which is the
 |   signal to discard any category memoized while ambiguous words were still
 |   abstaining.
 | Author: suinevere
 ----------------------*/
void room_class_set_game(unsigned int release, const char* serial);
int  room_class_genre_locked(void);

#ifdef __cplusplus
}
#endif
#endif /* ROOM_CLASS_H */
