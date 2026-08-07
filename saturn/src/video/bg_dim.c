/*----------------------
 | bg_dim.c
 | Description: See bg_dim.h.
 | Author: suinevere
 | Dependencies: bg_dim.h
 ----------------------*/
#include "bg_dim.h"

/*----------------------
 | g_bg_hold
 | Description: The wallpaper offset the player chose, held across rooms.
 |   Negative darkens, positive lightens -- VDP2's colour offset is signed, and a
 |   black text preset wants a lighter wallpaper as much as a white one wants a
 |   darker.
 | Author: suinevere
 ----------------------*/
static int g_bg_hold = 0;

/*----------------------
 | bg_dim_compose
 | Description: See bg_dim.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: level -- 0 (black) to 255 (unmodified); hold -- -255..+255
 | Returns: the per-channel offset, -255..+255
 ----------------------*/
int bg_dim_compose(int level, int hold) {
    int v = hold + (level - 255);
    if (v < -255) v = -255;
    if (v >  255) v =  255;
    return v;
}

/*----------------------
 | bg_dim_set
 | Description: See bg_dim.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_bg_hold
 | Params: offset -- -255..+255, clamped
 | Returns: N/A
 ----------------------*/
void bg_dim_set(int offset) {
    if (offset < -255) offset = -255;
    if (offset >  255) offset =  255;
    g_bg_hold = offset;
}

/*----------------------
 | bg_dim_get
 | Description: See bg_dim.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_bg_hold
 | Params: N/A
 | Returns: the held offset
 ----------------------*/
int bg_dim_get(void) { return g_bg_hold; }

/*----------------------
 | bg_dim_effective
 | Description: See bg_dim.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_bg_hold
 | Params: level -- 0 (black) to 255 (unmodified)
 | Returns: the per-channel offset, -255..+255
 ----------------------*/
int bg_dim_effective(int level) { return bg_dim_compose(level, g_bg_hold); }
