/*----------------------
 | room_model.h
 | Description: A model of the room the player is standing in, decoded from the
 |   live story image: which directions lead somewhere, what objects are here,
 |   and what is being carried. Exits come from the room object's direction
 |   properties, whose numbers are recovered from the dictionary's direction
 |   entries rather than hardcoded, so the decode works for any ZILCH-compiled
 |   v3 story and reports itself unavailable for anything else. Pure C -- no SRL,
 |   no console, no trie. Implemented in room_model.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef ROOM_MODEL_H
#define ROOM_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | RM_DIR_N / RM_HERE_MAX / RM_CARRIED_MAX
 | Description: The twelve directions the panel offers, and the fixed capacities
 |   for a room's visible objects and the player's carried items. Fixed rather
 |   than grown because the C heap is already carrying the story image and the
 |   typeahead trie.
 | Author: suinevere
 ----------------------*/
#define RM_DIR_N        12
#define RM_HERE_MAX     24
#define RM_CARRIED_MAX  16

/*----------------------
 | RM_N .. RM_OUT
 | Description: Direction indices, in the order the compass rose reads them.
 | Author: suinevere
 ----------------------*/
enum { RM_N = 0, RM_E, RM_W, RM_S, RM_NE, RM_NW, RM_SE, RM_SW,
       RM_UP, RM_DOWN, RM_IN, RM_OUT };

/*----------------------
 | RM_EXIT_NONE .. RM_EXIT_MAYBE
 | Description: What the room object says about a direction. NONE means the
 |   property is absent (no exit at all) or is the two-byte refusal-message form;
 |   OPEN is the one-byte unconditional form; MAYBE is any longer form --
 |   conditional, door or routine -- which cannot be resolved without running
 |   story code.
 | Author: suinevere
 ----------------------*/
enum { RM_EXIT_NONE = 0, RM_EXIT_BLOCKED, RM_EXIT_OPEN, RM_EXIT_MAYBE };

/*----------------------
 | RoomModel
 | Description: One prompt's snapshot: the room object, each direction's state
 |   and destination, the objects present, and the objects carried.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned short room;
    unsigned char  exits[RM_DIR_N];
    unsigned short dest[RM_DIR_N];
    unsigned short here[RM_HERE_MAX];
    int            nhere;
    unsigned short carried[RM_CARRIED_MAX];
    int            ncarried;
} RoomModel;

/*----------------------
 | room_model_bind
 | Description: Reads the story header, recovers the direction-word to
 |   property-number map from the dictionary, and gates on the recovered set
 |   being a contiguous run ending at 31 -- ZILCH allocates direction properties
 |   first, from the top down, so anything else means the story was not built
 |   that way and nothing here can be trusted. Call once per story load.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_story, g_len, g_obj, g_glob, g_dict, g_prop, g_available
 | Params: story -- the live story image; len -- its length in bytes
 | Returns: 1 if the model is available for this story, 0 otherwise
 ----------------------*/
int room_model_bind(const unsigned char *story, unsigned int len);

/*----------------------
 | room_model_available
 | Description: Whether the last bind produced a usable model.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_available
 | Params: N/A
 | Returns: 1 when available, 0 otherwise
 ----------------------*/
int room_model_available(void);

/*----------------------
 | room_model_dir_prop
 | Description: The property number recovered for a direction.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_prop
 | Params: dir -- one of the RM_* direction indices
 | Returns: the property number, or 0 when dir is out of range or unresolved
 ----------------------*/
int room_model_dir_prop(int dir);

/*----------------------
 | room_model_dir_word
 | Description: A direction's canonical word.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: dir -- one of the RM_* direction indices
 | Returns: the word, or "" when dir is out of range
 ----------------------*/
const char *room_model_dir_word(int dir);

/*----------------------
 | room_model_has_word
 | Description: Whether the story's dictionary accepts `text`, comparing only
 |   the first six characters because a v3 entry holds four text bytes, which is
 |   six Z-characters -- the parser cannot tell longer words apart either. Exists
 |   so the verb filter works on Hard, where no typeahead trie is built at all.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_story, g_dict
 | Params: text -- the word to look for
 | Returns: 1 if present, 0 otherwise
 ----------------------*/
int room_model_has_word(const char *text);

/*----------------------
 | room_model_refresh_room
 | Description: Rebuilds the snapshot for a given room object -- the entry the
 |   host tests drive, and what room_model_refresh calls once it has read the
 |   room out of global 0.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_model
 | Params: room -- the room object number
 | Returns: N/A
 ----------------------*/
void room_model_refresh_room(unsigned short room);

/*----------------------
 | room_model_get
 | Description: The current snapshot. Valid but empty before the first refresh
 |   and whenever the model is unavailable.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_model
 | Params: N/A
 | Returns: the snapshot, never NULL
 ----------------------*/
const RoomModel *room_model_get(void);

/*----------------------
 | room_model_refresh
 | Description: Reads the current room out of global 0 -- which the v3
 |   specification defines as the room the status line names -- and rebuilds the
 |   snapshot for it. Call once per prompt.
 | Author: suinevere
 | Dependencies: room_model_refresh_room
 | Globals: g_glob, g_model
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void room_model_refresh(void);

/*----------------------
 | room_model_player
 | Description: The object the player inhabits, or 0 while it is still unknown.
 |   There is no specified way to find it, so it is identified by intersecting
 |   the child sets of two consecutive rooms -- only the player follows the
 |   player -- which converges on the first room change. Until then carried items
 |   are simply absent.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_player
 | Params: N/A
 | Returns: the object number, or 0
 ----------------------*/
unsigned short room_model_player(void);

/*----------------------
 | room_model_object_word
 | Description: An object's first parser synonym, read straight from its own
 |   property list rather than decoded from a short name. The synonym property
 |   number varies by game and is detected rather than hardcoded: a property
 |   qualifies only when every one of its 16-bit values lands exactly on a
 |   dictionary entry boundary within range. Returns the dictionary's
 |   truncated six-character form, not the object's full-length name.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: obj -- object number; out -- receives up to six characters plus a
 |   NUL; max -- out's capacity
 | Returns: 1 and fills out on success, 0 and empties out otherwise
 ----------------------*/
int room_model_object_word(unsigned short obj, char *out, int max);

#ifdef __cplusplus
}
#endif
#endif /* ROOM_MODEL_H */
