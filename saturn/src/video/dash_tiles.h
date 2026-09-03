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
 |   of bending toward the background's hue and brightness. The map's crosshair
 |   is the only thing drawn in it, and it is red on every sheet and in every
 |   party -- the cursor is the one mark on the screen whose colour a reader
 |   never has to work out. The slot is free because nothing else can reach it:
 |   the marble caps its veins two steps below and every frame, rule and mark
 |   names an entry on either side of it.
 |
 |   Owned by tools/gen_dash_tiles.py, which sets dash_palette[] from its own
 |   PAL_ACCENT; the two are held together by tests/test_dash_accent.py, since
 |   a drift between them would not fail to build -- it would quietly tint the
 |   cursor back into the paper, or leave a stone tile untinted.
 | Author: suinevere
 ----------------------*/
#define DASH_PAL_ACCENT 14

/*----------------------
 | DASH_PAL_PARTY0 .. DASH_PAL_PARTY3
 | Description: The four seats' colours on the map, the local player's first.
 |   BORROWED, not reserved: they are ordinary points of the stone ramp in
 |   dash_palette and carry a colour only for as long as the map screen is up,
 |   which is between dash_map_ink and the dash_tint that closes the screen.
 |
 |   Borrowing is what there is. The accent is the only entry nothing on the
 |   stone reaches and it is spent on the crosshair, which has to stay red
 |   whatever the party is doing; the map needs four more, one per seat. It is
 |   safe because the map paints no stone: outside these four its own tiles
 |   reach entries 0, 1, 2, 12, 13, 14 and 15 and nothing else, so 3..11 are
 |   unreachable for as long as it is drawn. Entry 4 is reachable by nothing
 |   anywhere; 3 is a groove and 5 and 6 are marble body, which is why the
 |   restore on the way out is part of the design rather than tidiness.
 |
 |   Kept in step with PAL_PARTY in tools/gen_dash_tiles.py by
 |   tests/test_dash_accent.py, for the reason the accent is: a drift would
 |   build and link and quietly paint one player's figure in stone grey.
 | Author: suinevere
 ----------------------*/
#define DASH_PAL_PARTY0 3
#define DASH_PAL_PARTY1 4
#define DASH_PAL_PARTY2 5
#define DASH_PAL_PARTY3 6

#ifdef __cplusplus
}
#endif
#endif /* DASH_TILES_H */
