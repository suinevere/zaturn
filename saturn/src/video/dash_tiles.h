/*----------------------
 | dash_tiles.h
 | Description: The generated dashboard tile set and palette. The data is
 |   produced by tools/gen_dash_tiles.py into dash_tiles.c; this only declares
 |   it, so both the Saturn build and the host tests can reach it.
 | Author: suinevere
 | Dependencies: dash_map.h (DT_N)
 ----------------------*/
#ifndef DASH_TILES_H
#define DASH_TILES_H

#include "dash_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | dash_tile_data / dash_palette
 | Description: DT_N tiles of 8x8 4bpp pixels, two per byte with the left pixel
 |   in the high nibble, and the sixteen Saturn RGB555 words that colour them.
 |   Palette word 0 is zero, which is the transparent entry.
 | Author: suinevere
 ----------------------*/
extern const unsigned char  dash_tile_data[DT_N][32];
extern const unsigned short dash_palette[16];

/*----------------------
 | DASH_PAL_ACCENT
 | Description: The one palette entry that is a colour rather than a step of
 |   the stone ramp, and the one write_palette copies to CRAM untouched instead
 |   of bending toward the background's hue and brightness. Only the map's
 |   crosshair is drawn in it. The slot is free because nothing else can reach
 |   it: the marble caps its veins two steps below and every frame, rule and
 |   mark names an entry on either side of it.
 |
 |   Owned by tools/gen_dash_tiles.py, which sets dash_palette[] from its own
 |   PAL_ACCENT; the two are held together by tests/test_dash_accent.py, since
 |   a drift between them would not fail to build -- it would quietly tint the
 |   cursor back into the paper, or leave a stone tile untinted.
 | Author: suinevere
 ----------------------*/
#define DASH_PAL_ACCENT 14

#ifdef __cplusplus
}
#endif
#endif /* DASH_TILES_H */
