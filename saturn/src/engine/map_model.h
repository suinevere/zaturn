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
 | Description: Records a room the player is standing in, placing it on first
 |   entry and never moving it afterwards.
 |
 |   Two rules decide where. If map_atlas covers the room, it goes where the
 |   atlas says -- that is the default wherever somebody has drawn the geography,
 |   and it is what makes a compass direction mean what it says. Otherwise it is
 |   stepped one cell from the room just left, in the direction travelled, which
 |   is the original Saturn release's rule and the fallback for everything
 |   unauthored: a story nobody has mapped, a maze the atlas deliberately omits,
 |   or a room past the edge of the drawn region. A room placed by the fallback
 |   still links to what it was reached from, so walking off the atlas draws a
 |   line onward rather than stopping the map.
 |
 |   Contested cells are resolved by the placement search, and the first room to
 |   hold a cell keeps it -- including against the atlas, so an authored room
 |   arriving late takes the nearest free cell rather than evicting a walked one.
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
 | MAP_EXIT_COND / MAP_EXIT_ONEWAY / MAP_EXIT_SELF
 | Description: What the exit graph already implies about a passage, beside
 |   which way it runs. COND is RM_EXIT_MAYBE and draws dashed; ONEWAY is an
 |   exit with no way back and draws an arrowhead; SELF is an exit whose
 |   destination is the room it left.
 |
 |   A blocked exit back counts as a way back, so a shut door is not one-way.
 |   Counting it would make the arrowhead appear and vanish as the player
 |   opens and closes things, and a mark that flickers teaches nothing.
 | Author: suinevere
 ----------------------*/
#define MAP_EXIT_COND   1
#define MAP_EXIT_ONEWAY 2
#define MAP_EXIT_SELF   4

/*----------------------
 | MapExit
 | Description: One exit out of one room, as the map needs to draw it. The
 |   direction is kept rather than collapsed because a caller has to say U or D,
 |   which map_model_link's three-value answer cannot carry.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned short dest;
    unsigned char  dir;
    unsigned char  kind;
    unsigned char  flags;
} MapExit;

/*----------------------
 | map_model_exits
 | Description: Every exit out of a placed room, with the flags the graph
 |   implies. This is the shape the map draws from: a pairwise question cannot
 |   reach a self-loop or an exit whose far end is on another floor, because
 |   neither has a second room on this one to be a pair with.
 |
 |   ONEWAY is set only when the destination is itself placed. An unplaced room
 |   has no exits on record, so reading its silence as "no way back" would
 |   arrow every passage the moment it was first walked.
 | Author: suinevere
 | Dependencies: map_model_visited
 | Globals: g_dest, g_kind, g_cond
 | Params: room -- object number; out -- receives the exits; max -- its length
 | Returns: how many exits were written, 0 when the room is not placed
 ----------------------*/
int map_model_exits(unsigned short room, MapExit *out, int max);

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
 | map_model_step
 | Description: The unit step a direction moves in, for a caller working in
 |   cells rather than in rooms. Not DX/DY themselves: those are placement
 |   steps in room units and the vertical four are two apart, which is the
 |   spacing the layout wants and not a direction.
 |
 |   Up, down, in and out have no direction on a flat drawing and answer with
 |   the step for north, so a mark annotating one of them lands above the room
 |   rather than nowhere.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: DX, DY
 | Params: dir -- an RM_* index; dx, dy -- receive the step, each -1, 0 or 1
 | Returns: N/A
 ----------------------*/
void map_model_step(int dir, int *dx, int *dy);

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
 | map_model_reveal_atlas
 | Description: Places every room the authored table covers, whether or not the
 |   player has been there, and re-derives the exits so the links draw. A room
 |   already placed keeps its cell, so this never moves anything -- including the
 |   room the player is standing in, which is what keeps the reveal registered
 |   with the walked half of the map.
 |
 |   This is Easy difficulty's whole map. Every room it places is flagged as
 |   revealed rather than walked, which is what lets map_model_clear_reveal take
 |   them back when the difficulty stops asking for them and what keeps them out
 |   of the save blob. Walking into a revealed room afterwards makes it a walked
 |   one, so exploration done under a reveal survives its removal.
 |
 |   Does nothing at all when no table is bound, which is every story nobody
 |   drew -- so Easy on those stories is the explored-only map by falling
 |   through rather than by a second code path.
 | Author: suinevere
 | Dependencies: map_atlas.h, room_model.h
 | Globals: g_vis, g_revealed, g_x, g_y
 | Params: N/A
 | Returns: how many rooms it placed that were not placed before
 ----------------------*/
int map_model_reveal_atlas(void);

/*----------------------
 | map_model_clear_reveal
 | Description: Un-places every room map_model_reveal_atlas placed and the
 |   player has not since walked into, leaving the explored map exactly as it
 |   would have been had the reveal never run.
 |
 |   Without this a reveal would be permanent: a placed room never moves and
 |   g_vis has no memory of how it came to be set, so one open of the map on
 |   Easy would leave the whole drawing on the Medium map for the rest of the
 |   session and in every save taken after it.
 |
 |   What it cannot undo is a cell a reveal cost somebody else. An unauthored
 |   room walked into while the reveal was up contests its cells like any other,
 |   and having been flung to the nearest free one it stays there after the
 |   reveal is gone, since a placed room never moves. That is a room or two off
 |   in a map that had no authored position for them anyway, and undoing it would
 |   mean re-running every placement in walk order.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_revealed
 | Params: N/A
 | Returns: how many rooms it took back
 ----------------------*/
int map_model_clear_reveal(void);

/*----------------------
 | MAP_BLOB_MAGIC / MAP_BLOB_MAX
 | Description: The serialised map's leading byte, so a foreign or stale blob
 |   is refused rather than decoded into nonsense, and the largest blob a full
 |   table produces: four header bytes and six per placed room.
 | Author: suinevere
 ----------------------*/
/*----------------------
 | map_model_pages / map_model_page
 | Description: How many floors the map has, and which one a room is on. A
 |   floor is a page of the authored drawing -- the publisher split the map
 |   where the geography did -- so a story with no authored table has exactly
 |   one floor and every room is on it.
 |
 |   A room the table does not cover still gets an answer, and it is not
 |   page 0. Zork I's fifteen Maze rooms are the case: deliberately absent from
 |   the atlas, placed by the walk, and reached only from the dungeon, so
 |   answering "the floor nearest where the walk put it" puts them underground
 |   where the player found them, while a bare 0 would strand them on the
 |   surface among the forests. Nearest is measured on the Chebyshev distance to
 |   each floor's bounding box, which is zero for every room inside one.
 |
 |   Nothing is stored per room: the answer is derived from the position the
 |   model already holds, so a restored map answers the same as a walked one and
 |   the save blob does not grow a field.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_frame_x, g_frame_y
 | Params: page takes an object number
 | Returns: pages returns 1 or more; page returns 0 to pages-1
 ----------------------*/
int map_model_pages(void);
int map_model_page(unsigned short room);

#define MAP_BLOB_MAGIC 0x4Du
#define MAP_BLOB_MAX   (4u + 6u * MAP_ROOM_MAX)

/*----------------------
 | map_model_serialize
 | Description: Writes the walked set and the current room into out. Rooms that
 |   are on the map only because Easy revealed them are left out: a save is a
 |   record of where the player has been, and writing a reveal into one would
 |   make it permanent -- restoring it on any difficulty would hand back a map
 |   the player never explored, which no later clear_reveal could tell from
 |   honest exploration.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_revealed, g_x, g_y, g_cur
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
 |   which the blob never carried, so the first move after a restore cannot be
 |   read forwards out of the room departed from. It is instead read backwards
 |   out of the room arrived in -- if that room has an exit leading to the room
 |   the blob restored as current, the move was the opposite of it. Zork's
 |   exits are reciprocal often enough that this recovers the direction in most
 |   cases. Where the arrival has no way back, the destination is still placed
 |   due south whatever direction was travelled, and since a placed room never
 |   moves that cell is wrong permanently.
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
