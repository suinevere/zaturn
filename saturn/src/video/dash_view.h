/*----------------------
 | dash_view.h
 | Description: The input dashboard's hardware half: NBG2 bring-up, and the
 |   vblank flush that copies dash_map's shadow into the pattern name table.
 |   Both builds link this now. The netbin carried the printed-ASCII fallback
 |   instead until the panel was measured at 5,328 bytes there; see
 |   docs/superpowers/specs/2026-08-29-netbin-dashboard-design.md. That fallback
 |   is still the path a failed VRAM allocation takes, in either build.
 | Author: suinevere
 | Dependencies: dash_map.h, dash_tiles.h, srl.hpp
 ----------------------*/
#ifndef DASH_VIEW_H
#define DASH_VIEW_H

#include "dash_map.h"

/*----------------------
 | dash_init
 | Description: Allocates NBG2's character patterns and pattern name table in
 |   VRAM bank B0, uploads the tile set, claims and loads its palette, orders the
 |   layers, and subscribes the flush to OnAfterSync. Call once, after
 |   text_map_init. A second call is a no-op. Bank B0 is named explicitly rather
 |   than left to SRL's auto-allocator: AutoAllocateMap would try A0 first, which
 |   in the CD build is full -- the wallpaper's 512x256 8bpp bitmap owns the
 |   whole bank -- and then fall to B1, where SRL's own NBG3 font lives
 |   untracked. The netbin has no wallpaper and so has A0 free, but it is named
 |   B0 there too rather than split into two paths: B1 is the wrong answer in
 |   both builds, and one allocation site is worth more than a bank nobody is
 |   competing for.
 | Author: suinevere
 | Dependencies: srl.hpp, dash_map.h, dash_tiles.h
 | Globals: g_ready, g_cell, g_map_vram, g_char_base
 | Params: N/A
 | Returns: true when the layer is up, false when either allocation failed
 ----------------------*/
bool dash_init(void);

/*----------------------
 | dash_set
 | Description: Asks for a variant at a base row. Forwards to dash_build, which
 |   is idempotent, so a renderer may call this unconditionally every frame. A
 |   no-op when the layer never came up.
 | Author: suinevere
 | Dependencies: dash_map.h
 | Globals: g_ready
 | Params: variant -- one of the DASH_* values; base_row -- screen row of the
 |   panel's first row
 | Returns: N/A
 ----------------------*/
void dash_set(int variant, int base_row);

/*----------------------
 | dash_ready
 | Description: Whether the dashboard is drawing. Renderers print their ASCII
 |   borders when this is 0, which is what keeps a failed allocation looking
 |   exactly like the build before this feature existed.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ready
 | Params: N/A
 | Returns: 1 when NBG2 is up, 0 otherwise
 ----------------------*/
int dash_ready(void);

/*----------------------
 | dash_tint
 | Description: Recolours the marble to carry the background's hue, by writing
 |   the layer's sixteen CRAM entries from dash_tiles.c's ramp scaled toward
 |   `bg555`. The tiles are 4bpp indices, so the whole look is those sixteen
 |   words and nothing in VRAM moves. Each entry gives up half its distance to
 |   the background's hue and half its distance to the background's brightness,
 |   so a blue ground makes a blue-grey marble, an amber one a warm marble, and
 |   a dark ground a dark one -- see DASH_TINT_* and DASH_LEVEL_* in
 |   dash_view.cxx for why it takes two terms rather than one.
 |   Remembers its argument, so dash_init can re-apply it whichever order the
 |   two run in.
 | Author: suinevere
 | Dependencies: dash_tiles.h, SRL
 | Globals: g_tint_bg
 | Params: bg555 -- the background colour to tint toward
 | Returns: N/A
 ----------------------*/
void dash_tint(unsigned short bg555);

/*----------------------
 | dash_tint_current
 | Description: The background dash_tint was last handed, so a screen that
 |   tints the layer for its own ground can put back what it found rather than
 |   leaving its colour on the gamepad strip and every menu box for the rest of
 |   the session. Zero before the first dash_tint, which is what dash_init
 |   starts from and so is a valid thing to restore.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_tint_bg
 | Params: N/A
 | Returns: the last background handed to dash_tint
 ----------------------*/
unsigned short dash_tint_current(void);

/*----------------------
 | dash_map_ink
 | Description: Gives the map's own two colours to CRAM, untinted: the ink every
 |   passage, arrow, glyph and stub is drawn in, and the fill in the middle of
 |   an ordinary location mark. Which two depends on the sheet -- see
 |   DASH_PAL_LINE in dash_tiles.h.
 |
 |   Written straight rather than through the ramp for the reason the accent
 |   already was: these are colours, not stone, and bending them toward the
 |   paper they have to be found on is exactly what would hide them.
 |
 |   Both are ramp entries the rest of the time, so this is a loan that the next
 |   dash_tint calls in. Call it AFTER the dash_tint that sets the map's ground,
 |   since that rewrites all sixteen entries from the ramp, and let the
 |   dash_tint on the way out put the stone back.
 | Author: suinevere
 | Dependencies: dash_tiles.h, SRL
 | Globals: N/A
 | Params: line, fill -- packed RGB555 with or without the opaque bit, which is
 |   set here either way
 | Returns: N/A
 ----------------------*/
void dash_map_ink(unsigned short line, unsigned short fill);

/*----------------------
 | dash_map_party
 | Description: Gives the map's four seat colours to CRAM, untinted: the four
 |   borrowed slots the figures and the shared-room shield are drawn in, the
 |   local player's first. The crosshair is not among them -- it keeps the
 |   accent, which is red on every sheet and needs no writing here.
 |
 |   Separate from dash_map_ink because the two answer different questions. The
 |   ink is a property of the paper -- what reads as a drawing on it -- and the
 |   seat colours are a property of the party, which the paper has no say in
 |   beyond the one clash rule the caller applies.
 |
 |   Same loan, same terms: call it after the map's dash_tint and let the
 |   dash_tint on the way out put the stone back.
 | Author: suinevere
 | Dependencies: dash_tiles.h, SRL
 | Globals: N/A
 | Params: self -- the local player's colour; p0, p1, p2 -- the other three
 |   seats', in seat order. All packed RGB555 with or without the opaque bit,
 |   which is set here either way
 | Returns: N/A
 ----------------------*/
void dash_map_party(unsigned short self, unsigned short p0,
                    unsigned short p1, unsigned short p2);

/*----------------------
 | dash_hold
 | Description: Claims the dashboard panel for one frame with the variant and
 |   top-edge row the strip's renderers would have asked for, choosing between
 |   the two gamepad variants exactly as render_command_panel and
 |   render_game_keyboard do. A no-op outside a running game, and a no-op with
 |   a real keyboard in hand, since that case gets no panel at all.
 |
 |   dash_frame_end takes the panel down on any frame no renderer drew, which is
 |   what keeps the marble out from behind menus and the title screen. That
 |   expiry reads "stopped drawing" as "stopped being displayed", and this call
 |   exists for the frames where those part company: a caller Synchronizes with
 |   the console view still on screen but no renderer between them, and the
 |   panel would otherwise blank out from under text that never left the screen.
 | Author: suinevere
 | Dependencies: dash_map.h, console_view.h, app_state.h
 | Globals: g_in_game, g_kbd_visible, g_cmd_mode
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void dash_hold(void);

/*----------------------
 | dash_hold_any
 | Description: Claims the layer for one frame with whatever is already on it,
 |   and paints the gameplay strip if nothing is on it at all. The
 |   variant-specific holds are not interchangeable and picking the wrong one is
 |   worse than picking none: dash_hold paints the strip, so running it while a
 |   menu box or the map is up replaces what it was called to preserve.
 |
 |   Every frame a caller spends without drawing wants this rather than one of
 |   them: a fade ramp and a modal wait both hold the screen for many frames
 |   with no renderer between them, and either can be sitting over a menu box,
 |   over the map, or over the gamepad strip depending on where it was entered
 |   from. Holding only the box is what left an in-game menu's last frame as a
 |   black rectangle with the menu's text still lit on it -- the strip's marble
 |   expired, the image-suppressing window did not.
 | Author: suinevere
 | Dependencies: dash_map.h (dash_hold_painted)
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void dash_hold_any(void);

#endif /* DASH_VIEW_H */
