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
 |   (only the leftmost 40 are on screen at 320px); TEXT_ROWS covers the 29 rows
 |   (0..28) the program draws on with room to spare. TEXT_CLEAR_COLS is how wide
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
