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

#ifdef __cplusplus
}
#endif
#endif /* DASH_TILES_H */
