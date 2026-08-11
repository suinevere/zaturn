/*----------------------
 | glyph_invert.h
 | Description: Reverse-video glyph support: the 4bpp tile transform that turns
 |   a font tile into its inverse, and the cache that lends each character one of
 |   the 32 tile slots font 0 already owns at character codes 0x00..0x1F (control
 |   codes this program never prints). Pure logic -- no VRAM, no SRL; text_map
 |   owns the writes. Implemented in glyph_invert.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef GLYPH_INVERT_H
#define GLYPH_INVERT_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | GI_SLOT_N / GI_TILE_BYTES
 | Description: How many scratch slots exist (character codes 0x00..0x1F) and
 |   the byte size of one 8x8 4bpp tile.
 | Author: suinevere
 ----------------------*/
#define GI_SLOT_N     32
#define GI_TILE_BYTES 32

/*----------------------
 | gi_invert_tile
 | Description: Writes the reverse-video form of one 8x8 4bpp tile: pixel value
 |   0 (the transparent background) becomes 1 (the ink colour) and value 1
 |   becomes 2 (the highlight letter colour), so the cell paints a solid block
 |   with the letter punched out. Other values pass through untouched.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: src -- GI_TILE_BYTES source tile; dst -- GI_TILE_BYTES destination
 | Returns: N/A
 ----------------------*/
void gi_invert_tile(const unsigned char *src, unsigned char *dst);

/*----------------------
 | gi_begin_frame
 | Description: Opens a new allocation generation. Slots claimed in an earlier
 |   generation become reusable; slots re-requested this generation keep their
 |   character and need no tile rewrite.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_gen
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void gi_begin_frame(void);

/*----------------------
 | gi_slot_for
 | Description: The scratch slot holding `c`'s inverted glyph, claiming one if
 |   it does not already hold it. Sets *is_new when the caller must write the
 |   tile; clears it when the slot already carries that character.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_slot, g_gen
 | Params: c -- character to invert; is_new -- (out) 1 if the tile must be written
 | Returns: the slot index in 0..GI_SLOT_N-1, or -1 when every slot is spoken for
 |   this generation
 ----------------------*/
int gi_slot_for(char c, int *is_new);

/*----------------------
 | gi_reset
 | Description: Forgets every slot assignment, so the next generation starts
 |   from an empty cache. Used at init and by tests.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_slot, g_gen
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void gi_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* GLYPH_INVERT_H */
