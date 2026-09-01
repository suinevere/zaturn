/*----------------------
 | items.h
 | Description: Runtime lookup for the per-object item-picture table generated
 |   by tools/gen_items.py. Zork I is the only game with a table: OITEM.CZ's
 |   nineteen pictures are portraits of its own treasures, not a pool other
 |   stories could be assigned from. A game with no table has no pane at all,
 |   which is what keeps thirty games from carrying eight columns of dead
 |   black.
 |     game_items.inc itself is included only by items.c, the way
 |   game_presentation.inc is included only by presentation.c.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef ITEMS_H
#define ITEMS_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | items_available
 | Description: Whether this story has an authored item-picture table. The one
 |   call that decides whether the inventory overlay takes its tall geometry
 |   with a picture pane or its plain one.
 | Author: suinevere
 | Dependencies: game_items.inc
 | Globals: GAME_ITEM_MAP
 | Params: release -- Z-machine release; serial -- 6-char serial, not
 |   guaranteed NUL-terminated
 | Returns: 1 when the story has a table, 0 otherwise
 ----------------------*/
int items_available(unsigned int release, const char *serial);

/*----------------------
 | items_picture_of
 | Description: The picture one object gets. An unbound object -- which is most
 |   of them, and deliberately includes the broken egg and the broken canary --
 |   returns -1, which the pane reads as "blank plate" rather than as an error.
 | Author: suinevere
 | Dependencies: game_items.inc
 | Globals: GAME_ITEM_MAP
 | Params: release, serial -- the story identity; obj -- the object number
 | Returns: the 0-based picture index, or -1
 ----------------------*/
int items_picture_of(unsigned int release, const char *serial, unsigned int obj);

#ifdef __cplusplus
}
#endif
#endif /* ITEMS_H */
