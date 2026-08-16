/*----------------------
 | title_logo.h
 | Description: The Z-ATURN masonry logo that overlays the title screen, in the
 |   style of the ZORK cover lettering kept at tools/assets/logo/zork-logo.png.
 |   The stones are that reference's own: tools/make_logo.py traces the picture
 |   and samples its masonry into these letterforms, rather than drawing bricks,
 |   because the character of the thing is in stones no two of which are the same
 |   size or shape. Solid black letters with the joints cut THROUGH them in
 |   white, a white channel round the word and a black stroke as heavy as the
 |   mortar outside that -- all three weights measured off the reference.
 |
 |   It is not a picture on NBG0. The title screen spends that layer on a room
 |   photograph, so the logo rides the text layer instead -- 8x8 4bpp character
 |   patterns written into the free block below the SGL font, named by ordinary
 |   pattern names through text_put_cells. Three things follow from that and all
 |   three are the reason it is done this way: the photograph shows through the
 |   logo's transparent surround, the logo fades with the title because the
 |   screen-wide fade already drives NBG3, and it costs no CD read and no Work
 |   RAM -- the art is under 7 KB of const in the binary.
 |
 |   Two colours, in CRAM entries 3 and 4. NBG3 is COL_TYPE_16 on palette 0,
 |   where entry 1 is the console font's colour (the Options page rewrites it),
 |   2 is the reverse-video punch-out, 15 is the block cursor and 0 is
 |   transparency -- so 3 upward is what the art may use, and this design wants
 |   almost none of it.
 | Author: suinevere
 | Dependencies: text_map.h, SRL
 ----------------------*/
#ifndef TITLE_LOGO_H
#define TITLE_LOGO_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | TITLE_LOGO_COLS / TITLE_LOGO_ROWS
 | Description: The logo's footprint in text cells, so a caller can centre it
 |   without including the generated table.
 | Author: suinevere
 ----------------------*/
#define TITLE_LOGO_COLS 38
#define TITLE_LOGO_ROWS 12

/*----------------------
 | title_logo_install
 | Description: Uploads the logo's character patterns into VDP2 VRAM and its
 |   two colours into CRAM. Idempotent, and cheap to call again -- it does
 |   the work once and then returns immediately, the way install_block_glyph
 |   does.
 |
 |   Must run before the first title_logo_draw reaches VRAM, which it does
 |   naturally: the tiles are written here and now, while the cells that name
 |   them only leave the text shadow at the next vblank flush.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_logo_install(void);

/*----------------------
 | title_logo_draw
 | Description: Writes the logo's TITLE_LOGO_ROWS rows of cells into the text
 |   shadow with its top-left cell at (x, y). Calls title_logo_install itself,
 |   so a caller only ever needs this one. Safe to call every frame: the shadow
 |   skips cells whose value has not changed, so a title screen redrawing itself
 |   in its wait loop costs no VRAM traffic after the first frame.
 | Author: suinevere
 | Dependencies: text_map.h
 | Globals: N/A
 | Params: x -- left cell column; y -- top cell row
 | Returns: N/A
 ----------------------*/
void title_logo_draw(int x, int y);

#ifdef __cplusplus
}
#endif

#endif
