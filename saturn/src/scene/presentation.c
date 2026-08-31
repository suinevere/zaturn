/*----------------------
 | presentation.c
 | Description: See presentation.h.
 | Author: suinevere
 | Dependencies: string.h, presentation.h, game_presentation.inc
 | Globals: GAME_PRES_MAP, IMAGE_FRAME, PRES_AREA
 ----------------------*/
#include <string.h>

// Ahead of presentation.h: its typedef/count block is guarded on PRES_FRAME_N
// so it steps aside once the .inc below has already defined the same names.
#include "game_presentation.inc"
#include "presentation.h"

/*----------------------
 | pres_game_index
 | Description: See presentation.h.
 | Author: suinevere
 | Dependencies: string.h (memcmp)
 | Globals: GAME_PRES_MAP
 | Params: release, serial -- the story identity
 | Returns: the row index, or -1
 ----------------------*/
int pres_game_index(unsigned int release, const char *serial) {
    int i;
    if (serial == 0) return -1;
    for (i = 0; i < PRES_GAME_N; i++) {
        const GamePresMap *g = &GAME_PRES_MAP[i];
        if (g->release == release && memcmp(g->serial, serial, 6) == 0) return i;
    }
    return -1;
}

/*----------------------
 | pres_of_room
 | Description: See presentation.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: GAME_PRES_MAP
 | Params: release, serial, obj, out -- see presentation.h
 | Returns: 1 when the room is authored, 0 otherwise
 ----------------------*/
int pres_of_room(unsigned int release, const char *serial, unsigned int obj,
                 Presentation *out) {
    int g = pres_game_index(release, serial);
    if (g < 0 || obj >= 256 || out == 0) return 0;
    {
        const Presentation *p = &GAME_PRES_MAP[g].rooms[obj];
        if (p->image == 0) return 0;
        *out = *p;
        return 1;
    }
}

/*----------------------
 | pres_frame
 | Description: See presentation.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: IMAGE_FRAME
 | Params: image, area, offset, length -- see presentation.h
 | Returns: 1 on success, 0 when image is out of range
 ----------------------*/
int pres_frame(int image, int *area, unsigned long *offset, unsigned long *length) {
    if (image < 1 || image > PRES_FRAME_N) return 0;
    if (area)   *area   = (int) IMAGE_FRAME[image - 1].area;
    if (offset) *offset = IMAGE_FRAME[image - 1].offset;
    if (length) *length = IMAGE_FRAME[image - 1].length;
    return 1;
}

/*----------------------
 | pres_area_name
 | Description: See presentation.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: PRES_AREA
 | Params: area -- 0..PRES_AREA_N-1
 | Returns: the stem, or NULL
 ----------------------*/
const char *pres_area_name(int area) {
    if (area < 0 || area >= PRES_AREA_N) return 0;
    return PRES_AREA[area];
}
