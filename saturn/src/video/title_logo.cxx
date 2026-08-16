/*----------------------
 | title_logo.cxx
 | Description: Uploads and draws the Z-ATURN masonry logo described in
 |   title_logo.h. The art itself is generated -- see tools/make_logo.py and the
 |   title_logo.inc it writes.
 | Author: suinevere
 | Dependencies: title_logo.h, text_map.h, srl.hpp
 ----------------------*/
#include <srl.hpp>

#include "title_logo.h"
#include "text_map.h"

#include "title_logo.inc"

/*----------------------
 | LOGO_TILE_BASE
 | Description: The VDP2 character number the logo's first tile is written at.
 |
 |   The text layer addresses 1024 characters from VDP2_VRAM_B1 + 0x18000 (see
 |   TEXT_FONT_TILES in text_map.cxx), and almost none of them are spoken for:
 |   SGL's font 0 occupies 672..767, glyph_invert's reverse-video scratch slots
 |   640..671, and 768 upward is the tilemap itself. Everything below 640 is
 |   free, and the allocator cannot reach it either -- VDP2::VRAM stops its last
 |   bank at 0x18000. 64 leaves a margin at the bottom of that block in case a
 |   second SGL font is ever loaded, which would land far above this at 608.
 | Author: suinevere
 ----------------------*/
#define LOGO_TILE_BASE 64

static_assert(LOGO_TILE_BASE + LOGO_TILE_N <= 640,
              "title logo overruns the free character block below the SGL font");
static_assert(LOGO_CELLS_W == TITLE_LOGO_COLS && LOGO_CELLS_H == TITLE_LOGO_ROWS,
              "title_logo.h footprint disagrees with the generated art");

/*----------------------
 | LOGO_TILE_ADDR
 | Description: Where one character number's 32-byte pattern lives, in the same
 |   terms text_map.cxx's hl_tile uses -- that one adds the font's 640 because it
 |   is handed a character code, this one is handed the character number itself.
 | Author: suinevere
 ----------------------*/
#define LOGO_TILE_ADDR(n) \
    ((volatile unsigned char *) (VDP2_VRAM_B1 + 0x18000 + (n) * 0x20))

/*----------------------
 | g_installed
 | Description: Whether the tiles and colours have reached the hardware. A plain
 |   static, so a soft reset returns to a screen whose art is still uploaded --
 |   nothing between here and there rewrites either region.
 | Author: suinevere
 ----------------------*/
static bool g_installed = false;

extern "C" void title_logo_install(void)
{
    if (g_installed) return;

    for (int t = 0; t < LOGO_TILE_N; t++)
    {
        volatile unsigned char *dst = LOGO_TILE_ADDR(LOGO_TILE_BASE + t);
        for (int i = 0; i < 32; i++) dst[i] = LOGO_TILES[t][i];
    }

    // Palette 0's entries LOGO_PAL_FIRST.. -- the same uncached CRAM mirror
    // text_set_color (options.cxx) writes entries 1, 2 and 15 through, and
    // deliberately disjoint from them so recolouring the console text cannot
    // recolour the logo.
    volatile unsigned short *cram = (volatile unsigned short *) VDP2_COLRAM;
    for (int i = 0; i < LOGO_PAL_N; i++) cram[LOGO_PAL_FIRST + i] = LOGO_PAL[i];

    g_installed = true;
}

extern "C" void title_logo_draw(int x, int y)
{
    title_logo_install();

    for (int r = 0; r < LOGO_CELLS_H; r++)
    {
        unsigned short row[LOGO_CELLS_W];
        for (int c = 0; c < LOGO_CELLS_W; c++)
            row[c] = (unsigned short) (LOGO_TILE_BASE + LOGO_CELL[r][c]);
        text_put_cells(x, y + r, row, LOGO_CELLS_W);
    }
}
