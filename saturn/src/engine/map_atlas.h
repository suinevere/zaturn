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

#ifdef __cplusplus
}
#endif
#endif /* MAP_ATLAS_H */
