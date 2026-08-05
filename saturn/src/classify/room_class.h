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
 | TextKeyword
 | Description: One keyword -> text-category mapping row, used by both the room
 |   and event tables. `tier` is unused on event rows (KT_FEATURE by convention).
 | Author: suinevere
 ----------------------*/
typedef struct {
    const char*   word;
    unsigned char cat;
    unsigned char tier;
} TextKeyword;

/*----------------------
 | table accessors (room_class_data.c)
 | Description: text_keywords returns the room-keyword table
 |   (TC_WILDERNESS..TC_PLACE_LAST) and its length; text_events returns the
 |   event-word table (TC_DANGER/TC_TRIUMPH) and its length.
 | Author: suinevere
 ----------------------*/
const TextKeyword* text_keywords(int* n);
const TextKeyword* text_events(int* n);

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

#ifdef __cplusplus
}
#endif
#endif /* ROOM_CLASS_H */
