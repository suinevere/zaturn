/*----------------------
 | text_map.h
 | Description: The text layer's interface: a work-RAM shadow of the VDP2 text
 |   tilemap plus the print/clear calls that write it, flushed to VRAM once per
 |   frame inside vblank. Every screen in the program draws through this instead
 |   of SRL::Debug::Print, which stores straight into the live tilemap and tears.
 |
 |   VDP2 re-reads a cell's pattern name on every scanline of that cell's row, so
 |   a store landing while the beam is inside the row shows the old glyph above
 |   the beam and the new one below it -- a letter cut in half. Every draw loop in
 |   this program runs after its Synchronize() returns, which is to say across the
 |   whole of active display, and the ones that clear a row before repainting it
 |   leave that row blank for the crossing. Rows near the top lose the race
 |   outright and never appear. Shadowing the map moves all of that off the beam:
 |   the frame is composed in RAM whenever the code gets round to it, and the
 |   hardware only ever sees a finished frame copied in during blanking.
 | Author: suinevere
 | Dependencies: srl.hpp (SRL::string for the format overload)
 ----------------------*/
#ifndef TEXT_MAP_H
#define TEXT_MAP_H

#include <srl.hpp>

/*----------------------
 | TEXT_ROWS / TEXT_COLS / TEXT_CLEAR_COLS
 | Description: The shadow's shape. TEXT_COLS is the hardware map's 64-cell pitch
 |   (only the leftmost 40 are on screen at 320px); TEXT_ROWS covers the 30 rows
 |   (0..29) the program draws on with room to spare. TEXT_CLEAR_COLS is how wide
 |   text_clear_line blanks, matching the 44 columns SRL::Debug::PrintClearLine
 |   cleared so no caller loses coverage it used to have.
 | Author: suinevere
 ----------------------*/
#define TEXT_ROWS       32
#define TEXT_COLS       64
#define TEXT_CLEAR_COLS 44

/*----------------------
 | TEXT_FORMAT_MAX
 | Description: The format buffer, matching the SRL_DEBUG_MAX_PRINT_LENGTH the
 |   makefile passes so a line that fit through SRL::Debug::Print still fits here.
 |   A longer result is truncated rather than retried in a bigger buffer the way
 |   SRL did it, since the map is only TEXT_COLS wide and the tail had nowhere to
 |   land anyway.
 | Author: suinevere
 ----------------------*/
#define TEXT_FORMAT_MAX 64

extern "C" {

/*----------------------
 | text_map_init
 | Description: Blanks the shadow, subscribes the flush to SRL::Core::OnAfterSync
 |   so every existing Synchronize() call site pushes the frame during vblank, and
 |   pushes one blank frame. Call once, after SRL::Core::Initialize and before
 |   anything draws; a second call is a no-op.
 | Author: suinevere
 | Dependencies: srl.hpp
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void text_map_init(void);

/*----------------------
 | text_print_str
 | Description: Writes one unformatted string into the shadow at (x, y), clipped
 |   to the map rather than wrapped. Cells whose value does not change are left
 |   alone, so a screen redrawn unchanged every frame costs no VRAM traffic.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_shadow, g_dirty_top, g_dirty_bottom
 | Params: x -- left cell column; y -- cell row; s -- the text
 | Returns: N/A
 ----------------------*/
void text_print_str(int x, int y, const char *s);

/*----------------------
 | TEXT_DIM_PAL / TEXT_DIM_CRAM
 | Description: The 4bpp palette number a dimmed cell names in its pattern name,
 |   and the CRAM entry its ink reads from. NBG3's font is palette 0 (entries
 |   0..15) and the dashboard tiles are palette 1 (16..31), so 2 is the first
 |   free one; a 1-word pattern name carries the palette in bits 15..12, which
 |   is all it takes to give some cells a second ink with no second font and no
 |   tile writes at all. Named here beside the printer so whoever writes the
 |   entry (text_set_color) and whoever names the palette cannot drift apart.
 | Author: suinevere
 ----------------------*/
#define TEXT_DIM_PAL  2
#define TEXT_DIM_CRAM (TEXT_DIM_PAL * 16 + 1)

/*----------------------
 | TEXT_PARTY_PAL0 / TEXT_PARTY_PALS / TEXT_PARTY_CRAM
 | Description: Four more 4bpp palettes, one per seat, for text that has to be
 |   drawn in a player's own colour rather than the one the display settings
 |   chose -- the map's roster, whose names name the same people its shields and
 |   figures do. They follow the dim ink at bank 3, and each carries its ink in
 |   entry 1 exactly as palette 0 does, so a coloured cell differs from a plain
 |   one by its palette bank alone: no second font, no tile writes, and one
 |   bit-or per cell. Entries 48..111 of CRAM; the wallpaper starts at 256.
 | Author: suinevere
 ----------------------*/
#define TEXT_PARTY_PAL0  3
#define TEXT_PARTY_PALS  4
#define TEXT_PARTY_CRAM(slot) (((TEXT_PARTY_PAL0) + (slot)) * 16 + 1)

/*----------------------
 | text_print_dim
 | Description: text_print_str into the dim palette: the same glyphs, drawn from
 |   CRAM entry TEXT_DIM_CRAM instead of entry 1. Costs one bit-or per cell and
 |   no VRAM traffic beyond the map itself.
 |
 |   This is the whole of how selection is shown, everywhere: what is not
 |   selected is printed dim and what is selected is printed plain, so the
 |   cursor is the one thing on the screen at the brightness the player chose
 |   for text. The alternative -- reverse video, a solid block of ink with the
 |   letter punched out of it -- is gone. It had to build an inverted tile per
 |   distinct character on screen and could run out of the 32 slots it borrowed;
 |   it read as a black bar rather than as emphasis on the six presets whose
 |   text colour is black; and it did not match the menus, which have always
 |   selected by brightness.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_shadow
 | Params: x, y -- cell position; s -- the string
 | Returns: N/A
 ----------------------*/
void text_print_dim(int x, int y, const char *s);

/*----------------------
 | text_print_ink
 | Description: text_print_str into one of the four party palettes, so a line
 |   can be drawn in a seat's own colour. `slot` outside 0..TEXT_PARTY_PALS-1
 |   falls back to the plain ink rather than reaching a palette nobody has
 |   written, which is what a seat past the colours the map tells apart wants.
 |   The colours themselves come from text_set_party_ink.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_shadow
 | Params: x, y -- cell position; s -- the string; slot -- 0..3, or anything
 |   else for the plain ink
 | Returns: N/A
 ----------------------*/
void text_print_ink(int x, int y, const char *s, int slot);

/*----------------------
 | text_set_party_ink
 | Description: Writes one party palette's glyph entry. Separate from
 |   text_set_color because these are not the player's display settings: they
 |   are whose-is-whose, chosen by whatever screen is showing several people at
 |   once, and nothing rewrites them behind its back.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- 0..TEXT_PARTY_PALS-1; rgb555 -- the ink, without its MSB
 | Returns: N/A
 ----------------------*/
void text_set_party_ink(int slot, unsigned short rgb555);

/*----------------------
 | text_clear_line
 | Description: Blanks TEXT_CLEAR_COLS cells of one row in the shadow.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_shadow, g_dirty_top, g_dirty_bottom
 | Params: y -- cell row
 | Returns: N/A
 ----------------------*/
void text_clear_line(int y);

/*----------------------
 | text_flush
 | Description: Copies the rows changed since the last flush into the VDP2
 |   tilemap and drops the dirty range. Registered on OnAfterSync, so callers do
 |   not normally invoke it; call it directly only to push a frame from code that
 |   draws and then blocks without reaching another Synchronize().
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_shadow, g_dirty_top, g_dirty_bottom
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void text_flush(void);

/*----------------------
 | text_on_flush
 | Description: Registers one callback to run at the vblank that next carries
 |   changed rows to VRAM, then forgets it; NULL cancels a pending one. For
 |   hardware state whose lifetime is the drawn frame's rather than the calling
 |   scope's -- see MenuBacking in menu.cxx, whose backing window has to outlive
 |   the guard object by however long the box it backs stays on screen. One slot
 |   rather than a list: a second registration replaces the first.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_on_flush
 | Params: fn -- the callback, or NULL to cancel a pending one
 | Returns: N/A
 ----------------------*/
void text_on_flush(void (*fn)(void));

}

/*----------------------
 | text_print
 | Description: Prints at (x, y): the plain overload takes the string as-is, the
 |   template one formats through SRL::string first. Split the same way
 |   SRL::Debug::Print is, so a bare string containing a '%' is still printed
 |   literally rather than read as a conversion.
 | Author: suinevere
 | Dependencies: srl.hpp, text_print_str
 | Globals: N/A
 | Params: x -- left cell column; y -- cell row; text -- string or format;
 |   args -- format arguments
 | Returns: N/A
 ----------------------*/
inline void text_print(int x, int y, const char *text)
{
    text_print_str(x, y, text);
}

template <typename ...Args>
inline void text_print(int x, int y, const char *text, Args...args)
{
    char buffer[TEXT_FORMAT_MAX];
    SRL::string stringObj;

    if (stringObj.snprintfEx(buffer, TEXT_FORMAT_MAX, text, args ...) > 0)
    {
        text_print_str(x, y, buffer);
    }
}

#endif
