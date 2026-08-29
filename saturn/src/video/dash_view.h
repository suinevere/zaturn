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

#endif /* DASH_VIEW_H */
