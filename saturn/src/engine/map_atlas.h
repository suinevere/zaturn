/*----------------------
 | map_atlas.h
 | Description: Authored room positions for stories whose geography somebody has
 |   drawn, so the map can place those rooms where they actually are rather than
 |   where a graph walk guesses. Pure data and lookup -- map_model.c decides when
 |   to consult it and owns every position it finally stores. Implemented in
 |   map_atlas.c.
 |
 |   Why this exists: Zork's exits are not Euclidean, so walking them at a fixed
 |   step cannot make west mean west. An authored table can, because a person
 |   with the whole graph in front of them resolved the contradictions once.
 |   docs/ZORK1_MAP_RECON.md has the measurement that ruled out the original
 |   Saturn release keeping any such table of its own.
 |
 |   Rooms are grouped into floors, one per page of the drawing: the publisher
 |   split the map where the geography did, so above ground, the dungeon and the
 |   coal mine are separate drawings. The table stacks them into one coordinate
 |   space because it has nowhere else to put them, and records which floor each
 |   room came off so a caller can show one at a time.
 |
 |   What it deliberately does not cover: mazes. A maze is drawn for legibility
 |   rather than geography -- Infocom's own map puts its fifteen maze rooms in an
 |   arbitrary planar embedding -- so those rooms are simply absent here and fall
 |   through to the walk, which is the honest answer for a space that has no
 |   layout to be faithful to.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef MAP_ATLAS_H
#define MAP_ATLAS_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | MAP_ATLAS_PAGE_MAX
 | Description: The most floors one table may declare, and so the size of the
 |   cached bounding boxes. Eight against the four the deepest of the shipped
 |   drawings actually uses; a table past this is bound with its extra floors
 |   folded onto the last, which is wrong in a visible way rather than a silent
 |   one. tools/gen_map_atlas.py has no page cap of its own -- pages come from
 |   however many sheets the publisher printed -- so this is the only bound.
 | Author: suinevere
 ----------------------*/
#define MAP_ATLAS_PAGE_MAX 8

/*----------------------
 | map_atlas_bind
 | Description: Selects the authored table matching a story image, by the
 |   release number and serial in its Z-machine header, and forgets any table
 |   bound before. Nothing else identifies a build closely enough: object
 |   numbers are assigned by the compiler, so two releases of the same game
 |   number their rooms differently and a table keyed on the title alone would
 |   place them somewhere plausible and wrong.
 |
 |   Binding no table is a normal outcome, not a failure. It is what every story
 |   nobody has drawn gets, and it leaves the map exactly as it behaves without
 |   this module.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cells, g_n
 | Params: story -- the story image, may be null; len -- its length
 | Returns: the number of authored rooms bound, 0 when none matched
 ----------------------*/
int map_atlas_bind(const unsigned char *story, unsigned int len);

/*----------------------
 | map_atlas_pos
 | Description: The authored cell for a room, in the table's own coordinates.
 |   Those are absolute within a table and mean nothing between tables; the
 |   caller is what reconciles them with wherever it had already placed things.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cells, g_n
 | Params: room -- object number; x, y -- receive the cell
 | Returns: 1 when the room is in the bound table, 0 otherwise
 ----------------------*/
int map_atlas_pos(unsigned short room, int *x, int *y);

/*----------------------
 | map_atlas_count
 | Description: How many rooms the bound table covers, for a caller that wants
 |   to report or test which of the two layout rules is in force.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_n
 | Params: N/A
 | Returns: the authored room count, 0 when no table is bound
 ----------------------*/
int map_atlas_count(void);

/*----------------------
 | map_atlas_room_at
 | Description: The object number at a position in the bound table, so a caller
 |   can walk everything the table covers without knowing what is in it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cells, g_n
 | Params: index -- position in the table; room -- receives the object number
 | Returns: 1 on success, 0 when index is out of range
 ----------------------*/
int map_atlas_room_at(int index, unsigned short *room);

/*----------------------
 | map_atlas_pages
 | Description: How many floors the bound table spans. One is the ordinary
 |   answer for a small game drawn on a single sheet; zero means no table is
 |   bound, which a caller should read as "one floor, and it is not mine".
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pages
 | Params: N/A
 | Returns: the floor count, 0 when no table is bound
 ----------------------*/
int map_atlas_pages(void);

/*----------------------
 | map_atlas_page
 | Description: Which floor a room was drawn on.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cells, g_n
 | Params: room -- object number; page -- receives the floor, 0-based
 | Returns: 1 when the room is in the bound table, 0 otherwise
 ----------------------*/
int map_atlas_page(unsigned short room, int *page);

/*----------------------
 | map_atlas_page_box
 | Description: The bounding box of one floor, in the table's own coordinates
 |   and inclusive at both ends. Computed once at bind rather than per call,
 |   because the caller asking is a renderer deciding which floor a cell belongs
 |   to and a scan of the whole table per cell is not free.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_box, g_pages
 | Params: page -- the floor; x0, y0, x1, y1 -- receive the box
 | Returns: 1 when the floor exists and holds at least one room, 0 otherwise
 ----------------------*/
int map_atlas_page_box(int page, int *x0, int *y0, int *x1, int *y1);

/*----------------------
 | map_atlas_pages_overlap
 | Description: Whether any two floors' boxes intersect. They do not in any
 |   table the generator produces -- it gives each drawn page its own band of
 |   rows below the last -- so this exists to catch a table that stops being
 |   true of, rather than to handle one that already is: a caller may draw every
 |   floor at once only while this answers no.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_box, g_pages
 | Params: N/A
 | Returns: 1 when two floors share ground, 0 otherwise
 ----------------------*/
int map_atlas_pages_overlap(void);

#ifdef __cplusplus
}
#endif
#endif /* MAP_ATLAS_H */
