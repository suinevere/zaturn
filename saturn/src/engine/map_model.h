/*----------------------
 | map_model.h
 | Description: The in-game map's model: which rooms have been seen, and where
 |   each one sits on a grid whose unit is one room. A position is assigned the
 |   first time a room is entered and never moved again, so the map does not
 |   rearrange itself between openings. Pure C -- no SRL, no VRAM, no console.
 |   Implemented in map_model.c.
 | Author: suinevere
 | Dependencies: room_model.h
 ----------------------*/
#ifndef MAP_MODEL_H
#define MAP_MODEL_H

#include "room_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | MAP_ROOM_MAX
 | Description: How many object numbers the position table covers. Rooms are
 |   keyed by Z-machine object number, not by the original Saturn game's 0-109
 |   room index, so this is sized for a story's objects rather than for Zork's
 |   room count. Fixed rather than grown because the C heap is already carrying
 |   the story image and the typeahead trie; a room above the cap is dropped
 |   from the map rather than allowed to grow it.
 |
 |   256 is exact rather than generous: a v3 object number is one byte, so
 |   1..255 is the entire object space and nothing can fall above the cap. That
 |   is also what makes the serialised room count safe to write as a single
 |   byte. A v5 port would break both -- it would need this raised and the blob
 |   format widened, in that order.
 | Author: suinevere
 ----------------------*/
#define MAP_ROOM_MAX 256

/*----------------------
 | MAP_DIR_UNKNOWN
 | Description: Returned by the direction inference when no exit of the
 |   previous room led to the room now occupied -- a door, a conditional exit,
 |   a teleport, or the first room of all.
 | Author: suinevere
 ----------------------*/
#define MAP_DIR_UNKNOWN (-1)

/*----------------------
 | map_model_reset
 | Description: Forgets every room and position. Call on story load and on any
 |   restore the map cannot be matched to.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_x, g_y, g_cur, g_have_cur, g_prev
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void map_model_reset(void);

/*----------------------
 | map_model_enter
 | Description: Records one prompt. Infers the direction travelled by matching
 |   the new room against the previous snapshot's dest[], places the room if it
 |   is new, and makes it current. Placing is idempotent: a room already on the
 |   grid keeps its cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_x, g_y, g_cur, g_have_cur, g_prev
 | Params: m -- the snapshot room_model_refresh just produced, never null
 | Returns: N/A
 ----------------------*/
void map_model_enter(const RoomModel *m);

/*----------------------
 | map_model_visited
 | Description: Whether a room has been entered and placed.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis
 | Params: room -- object number
 | Returns: 1 when placed, 0 otherwise or when room is out of range
 ----------------------*/
int map_model_visited(unsigned short room);

/*----------------------
 | map_model_pos
 | Description: A placed room's grid cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_x, g_y
 | Params: room -- object number; x, y -- receive the cell, untouched on failure
 | Returns: 1 when the room is placed, 0 otherwise
 ----------------------*/
int map_model_pos(unsigned short room, int *x, int *y);

/*----------------------
 | map_model_current
 | Description: The room the player is in, as of the last map_model_enter.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cur
 | Params: N/A
 | Returns: the object number, or 0 before the first entry
 ----------------------*/
unsigned short map_model_current(void);

/*----------------------
 | map_model_offset
 | Description: A placed room's cell relative to the room the player is in, in
 |   room units. The player is always the origin, which is what lets the view
 |   nail the figure to the centre of the screen and scroll the map under it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_x, g_y, g_cur, g_have_cur
 | Params: room -- object number; dx, dy -- receive the offset, untouched on
 |   failure
 | Returns: 1 when both the room and the player are placed, 0 otherwise
 ----------------------*/
int map_model_offset(unsigned short room, int *dx, int *dy);

/*----------------------
 | map_model_count
 | Description: How many rooms are placed.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis
 | Params: N/A
 | Returns: the count, 0 or more
 ----------------------*/
int map_model_count(void);

/*----------------------
 | map_model_room_at
 | Description: The index'th placed room in ascending object order, so the view
 |   can walk the set without scanning MAP_ROOM_MAX slots itself.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis
 | Params: index -- 0 to map_model_count()-1; room -- receives the object
 |   number, untouched on failure
 | Returns: 1 on success, 0 when index is out of range
 ----------------------*/
int map_model_room_at(int index, unsigned short *room);

/*----------------------
 | MAP_LINK_NONE .. MAP_LINK_VERT
 | Description: How two rooms are joined. VERT covers UP, DOWN, IN and OUT, so
 |   a staircase can be drawn as a level change rather than as a road.
 | Author: suinevere
 ----------------------*/
#define MAP_LINK_NONE 0
#define MAP_LINK_FLAT 1
#define MAP_LINK_VERT 2

/*----------------------
 | map_model_link
 | Description: How room a is joined to room b, if at all. Asked of the story's
 |   exits rather than of the route walked, so a link shows as soon as both
 |   ends are placed.
 | Author: suinevere
 | Dependencies: map_model_visited
 | Globals: g_dest, g_kind
 | Params: a, b -- object numbers
 | Returns: MAP_LINK_VERT, MAP_LINK_FLAT, or MAP_LINK_NONE
 ----------------------*/
int map_model_link(unsigned short a, unsigned short b);

/*----------------------
 | map_model_rebind_exits
 | Description: Refills every placed room's exits and destinations from the
 |   story's own static exit properties. This is what a deserialised map has
 |   none of: the blob carries positions only, so until this runs every
 |   map_model_link after a restore answers MAP_LINK_NONE and the map draws as
 |   marks with no trail between them -- and since the links ARE the trail,
 |   that is a map of nothing. One pass over the placed set, rather than four
 |   more bytes per room in every one of the backup slots. Leaves the room
 |   model's own snapshot on the current room. Harmless when the room model is
 |   unavailable, which reports every room as having no exits at all.
 | Author: suinevere
 | Dependencies: room_model.h (room_model_refresh_room, room_model_get)
 | Globals: g_vis, g_dest, g_kind, g_cur, g_have_cur
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void map_model_rebind_exits(void);

/*----------------------
 | MAP_BLOB_MAGIC / MAP_BLOB_MAX
 | Description: The serialised map's leading byte, so a foreign or stale blob
 |   is refused rather than decoded into nonsense, and the largest blob a full
 |   table produces: four header bytes and six per placed room.
 | Author: suinevere
 ----------------------*/
#define MAP_BLOB_MAGIC 0x4Du
#define MAP_BLOB_MAX   (4u + 6u * MAP_ROOM_MAX)

/*----------------------
 | map_model_serialize
 | Description: Writes the placed set and the current room into out.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_x, g_y, g_cur
 | Params: out -- receives the blob; max -- its capacity
 | Returns: the number of bytes written, or 0 when max is too small
 ----------------------*/
unsigned int map_model_serialize(unsigned char *out, unsigned int max);

/*----------------------
 | map_model_deserialize
 | Description: Replaces the model with a previously serialised one. Refuses a
 |   blob whose magic or length does not match, whose room is outside the
 |   table, or whose current room is not in its own placed set -- the last of
 |   which would otherwise leave map_model_offset answering 0 for everything
 |   and the view painting bare ground. Leaves the model empty rather than
 |   half-filled, because a half-restored map is worse than none.
 |
 |   Two things it does not restore. Exits: call map_model_rebind_exits after
 |   a success or nothing will draw a link. And the previous prompt's snapshot,
 |   which the blob never carried: the first move after a restore therefore
 |   infers MAP_DIR_UNKNOWN and places the destination due south whatever
 |   direction was actually travelled. A placed room never moves, so that one
 |   cell is wrong permanently. Known, not fixed; the alternative is storing a
 |   snapshot the save format has no room for.
 | Author: suinevere
 | Dependencies: map_model_reset
 | Globals: g_vis, g_x, g_y, g_cur, g_have_cur, g_have_prev
 | Params: in -- the blob; len -- its length
 | Returns: 1 on success, 0 when refused
 ----------------------*/
int map_model_deserialize(const unsigned char *in, unsigned int len);

/*----------------------
 | map_model_serialize_len
 | Description: The length a serialised map claims, read from its own header,
 |   for a reader that is handed a buffer without a length -- which is what
 |   saturn_bup_read gives back.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: in -- at least four bytes of a blob
 | Returns: the claimed length, or 0 when the magic does not match
 ----------------------*/
unsigned int map_model_serialize_len(const unsigned char *in);

#ifdef __cplusplus
}
#endif
#endif /* MAP_MODEL_H */
