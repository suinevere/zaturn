/*----------------------
 | items.c
 | Description: See items.h.
 | Author: suinevere
 | Dependencies: items.h, game_items.inc
 | Globals: GAME_ITEM_MAP
 ----------------------*/
#include "scene/game_items.inc"
#include "scene/items.h"

/*----------------------
 | game_index
 | Description: The GAME_ITEM_MAP row for one story, by release and 6-char
 |   serial. The serial is compared over exactly six characters rather than as
 |   a string, because the caller's copy comes out of a story header and is not
 |   guaranteed NUL-terminated.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: GAME_ITEM_MAP
 | Params: release -- Z-machine release; serial -- 6-char serial
 | Returns: the row index, or -1
 ----------------------*/
static int game_index(unsigned int release, const char *serial) {
    int i, j;
    if (serial == 0) return -1;
    for (i = 0; i < ITEM_GAME_N; i++) {
        if (GAME_ITEM_MAP[i].release != release) continue;
        for (j = 0; j < 6; j++)
            if (GAME_ITEM_MAP[i].serial[j] != serial[j]) break;
        if (j == 6) return i;
    }
    return -1;
}

/*----------------------
 | items_available
 | Description: See items.h.
 | Author: suinevere
 | Dependencies: game_index
 | Globals: N/A
 | Params: release, serial -- the story identity
 | Returns: 1 when the story has a table
 ----------------------*/
int items_available(unsigned int release, const char *serial) {
    return game_index(release, serial) >= 0 ? 1 : 0;
}

/*----------------------
 | items_picture_of
 | Description: See items.h.
 | Author: suinevere
 | Dependencies: game_index
 | Globals: GAME_ITEM_MAP
 | Params: release, serial, obj -- see items.h
 | Returns: the 0-based picture index, or -1
 ----------------------*/
int items_picture_of(unsigned int release, const char *serial, unsigned int obj) {
    int g = game_index(release, serial);
    int i;
    if (g < 0) return -1;
    for (i = 0; i < (int) GAME_ITEM_MAP[g].count; i++)
        if (GAME_ITEM_MAP[g].items[i].obj == obj)
            return (int) GAME_ITEM_MAP[g].items[i].picture;
    return -1;
}
