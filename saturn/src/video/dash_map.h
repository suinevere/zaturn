/*----------------------
 | dash_map.h
 | Description: The input dashboard's tile vocabulary, its per-variant geometry,
 |   and the work-RAM shadow of the NBG2 pattern-name map. Pure logic -- no SRL
 |   and no VRAM; dash_view.cxx owns every write to hardware. Implemented in
 |   dash_map.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef DASH_MAP_H
#define DASH_MAP_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | DASH_COLS / DASH_ROWS
 | Description: The shadow's shape: the 40 columns a 320-pixel screen shows, and
 |   32 rows covering the 30 the program draws on with room to spare, matching
 |   text_map's TEXT_ROWS. The hardware map's pitch is 64 cells; dash_view
 |   supplies that when it flushes.
 | Author: suinevere
 ----------------------*/
#define DASH_COLS 40
#define DASH_ROWS 32

/*----------------------
 | DT_BLANK .. DT_N
 | Description: The tile set, in the order dash_tiles.c stores it. DT_BLANK is
 |   fully transparent and is what every cell outside the panel holds, so the
 |   wallpaper shows through. DT_FIELD0 begins the 4x4 marble patch, addressed by
 |   the low two bits of the cell's screen coordinates, which gives the stone a
 |   32-pixel repeat instead of an 8-pixel one. Each module is its own box with a
 |   four-pixel frame, and every frame tile carries the field's own marble right
 |   up behind the highlight, so the horizontal runs come one tile per x & 3
 |   phase and the vertical ones one per y & 3. The divider cell holds the frame
 |   of the module to its left and the two-pixel gap; the module to its right
 |   opens with DT_MODLEFT in the cell after it.
 |
 |   The DT_BOX_* set is the same bevel over transparency instead of over
 |   marble, for the menu boxes: no field, so nothing to hold in register, so
 |   one tile per edge rather than four.
 | Author: suinevere
 ----------------------*/
enum {
    DT_BLANK = 0,
    DT_FIELD0 = 1,
    DT_TOP0 = 17, DT_TOP1, DT_TOP2, DT_TOP3,
    DT_BOTTOM0 = 21, DT_BOTTOM1, DT_BOTTOM2, DT_BOTTOM3,
    DT_LEFT0 = 25, DT_LEFT1, DT_LEFT2, DT_LEFT3,
    DT_RIGHT0 = 29, DT_RIGHT1, DT_RIGHT2, DT_RIGHT3,
    DT_MODLEFT0 = 33, DT_MODLEFT1, DT_MODLEFT2, DT_MODLEFT3,
    DT_DIVIDER0 = 37, DT_DIVIDER1, DT_DIVIDER2, DT_DIVIDER3,
    DT_CORNER_TL = 41, DT_CORNER_TR, DT_CORNER_BL, DT_CORNER_BR,
    DT_TOP_MODLEFT = 45, DT_BOTTOM_MODLEFT,
    DT_TOP_DIVIDER = 47, DT_BOTTOM_DIVIDER,
    DT_RULE0 = 49, DT_RULE1, DT_RULE2, DT_RULE3,
    DT_RULE_MODLEFT = 53, DT_RULE_RIGHT,
    DT_BOX_TOP = 55, DT_BOX_BOTTOM, DT_BOX_LEFT, DT_BOX_RIGHT,
    DT_BOX_TL, DT_BOX_TR, DT_BOX_BL, DT_BOX_BR,
    DT_GROUND, DT_ROOM, DT_ROOM_HERE, DT_LINK_H, DT_LINK_V, DT_LINK_STAIR,
    DT_N
};

/*----------------------
 | DASH_NONE .. DASH_VARIANT_N
 | Description: The fixed panel shapes, the nothing-painted state the shadow
 |   starts in, and the runtime rectangle. PANEL and GAMEKB are the two in-game
 |   gamepad strips and OVERLAY is PANEL without its dividers. BOX is the odd
 |   one out: its geometry is not in the table but supplied per call by
 |   dash_box, and it paints a bevel with nothing inside it rather than a
 |   marble field.
 | Author: suinevere
 ----------------------*/
enum { DASH_NONE = 0, DASH_PANEL, DASH_GAMEKB, DASH_OVERLAY,
       DASH_BOX, DASH_VARIANT_MAP, DASH_VARIANT_N };

/*----------------------
 | dash_build
 | Description: Repaints the shadow for `variant` with its top row at
 |   `base_row`, clearing whatever rectangle was painted before. Idempotent: a
 |   call naming the variant and base already showing returns without touching
 |   the shadow or the dirty span, so a renderer may call it every frame. An
 |   out-of-range variant is ignored.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map, g_variant, g_base, g_dirty_top, g_dirty_bottom, g_touched
 | Params: variant -- one of the DASH_* values; base_row -- screen row of the
 |   panel's first row
 | Returns: N/A
 ----------------------*/
void dash_build(int variant, int base_row);

/*----------------------
 | dash_box
 | Description: Paints an arbitrary rectangle as a bevelled frame with a
 |   transparent interior, for the menu boxes -- the DT_BOX_* tiles rather than
 |   the panel's marble ones, so whatever the menu draws inside is untouched.
 |   Takes the same rectangle menu_frame does, in the same units. Idempotent on
 |   the whole rectangle, not just its top row, so a page may call it every
 |   frame. Clears whatever was painted before, exactly as dash_build does: one
 |   thing is on this layer at a time. Ignored for w or h below 2, which has no
 |   frame to draw.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map, g_variant, g_base, g_box, g_dirty_top, g_dirty_bottom,
 |   g_touched
 | Params: x -- left column; y -- top row; w -- width in cells; h -- height
 | Returns: N/A
 ----------------------*/
void dash_box(int x, int y, int w, int h);

/*----------------------
 | dash_box_hold
 | Description: Keeps whatever box is on the layer up for one more frame, and
 |   does nothing at all if a box is not what is on it. A menu that has finished
 |   drawing does not draw again -- menu_message paints once and menu_wait then
 |   holds the screen until a key arrives -- but dash_frame_end takes the layer
 |   down on any frame nobody claims it, so somebody has to keep claiming.
 |
 |   The question it answers is "is a box currently painted", asked of the layer
 |   itself. That is deliberately narrower than "is a menu open": online_mode
 |   holds one MenuBacking for a whole telnet session, so a caller keyed on the
 |   backing refcount goes on re-claiming a box the dial screen drew for the
 |   rest of the session, fighting the gamepad strip for the layer every frame.
 |   Keyed on the layer, a strip claim simply ends the hold.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_variant, g_touched
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void dash_box_hold(void);

/*----------------------
 | dash_map_begin
 | Description: Claims the layer for the map and clears it, exactly as
 |   dash_build and dash_box do: one thing is on this layer at a time. Call
 |   once per frame the map is on screen, then paint into it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map, g_variant, g_base, g_box, g_dirty_top, g_dirty_bottom,
 |   g_touched
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void dash_map_begin(void);

/*----------------------
 | dash_map_hold
 | Description: Keeps the map's claim on the layer up for one more frame, and
 |   does nothing at all if a map is not what is on it. dash_map's own contract
 |   is that dash_map_begin is called once per frame the map is on screen; a
 |   caller that draws the map once and then holds the screen -- rather than
 |   redrawing every frame -- must keep claiming some other way, the same
 |   problem dash_box_hold solves for a menu box. The map's counterpart is
 |   separate because it is keyed on DASH_VARIANT_MAP rather than DASH_BOX: a
 |   caller re-running the whole dash_map_begin/dash_map_paint pass every frame
 |   just to keep the claim alive would also re-run whatever it costs to
 |   recompute what to paint, which for the map is not free.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_variant, g_touched
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void dash_map_hold(void);

/*----------------------
 | dash_map_paint
 | Description: Sets one cell of the shadow. Out-of-range coordinates are
 |   dropped rather than faulting, so a caller clipping at the screen edge
 |   needs no bounds test of its own.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map, g_dirty_top, g_dirty_bottom
 | Params: x -- column; y -- row; tile -- a DT_* index
 | Returns: N/A
 ----------------------*/
void dash_map_paint(int x, int y, unsigned char tile);

/*----------------------
 | dash_cell
 | Description: The tile index the shadow holds at (x, y). Out-of-range
 |   coordinates read as DT_BLANK rather than faulting, so a caller clipping at
 |   the screen edge needs no bounds test of its own.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map
 | Params: x -- cell column; y -- cell row
 | Returns: the tile index, or DT_BLANK when off the shadow
 ----------------------*/
unsigned char dash_cell(int x, int y);

/*----------------------
 | dash_input_up
 | Description: Whether what is painted right now is one of the game's input
 |   strips, rather than a menu box or nothing. The wallpaper's vertical offset
 |   keys off this and nothing else: the offset exists to compensate for the
 |   strip's marble, and the NBG0 window that hides the scrolled plane's wrap is
 |   armed by the same two renderers that draw that marble, so the offset must
 |   never outlive it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_variant
 | Params: N/A
 | Returns: 1 for DASH_PANEL, DASH_GAMEKB or DASH_OVERLAY, else 0
 ----------------------*/
int dash_input_up(void);

/*----------------------
 | dash_dirty_top / dash_dirty_bottom / dash_dirty_clear
 | Description: The span of rows changed since the last clear, and the call that
 |   closes it. The span is empty when bottom is below top, which is the state a
 |   fresh shadow and a just-flushed one both hold.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_dirty_top, g_dirty_bottom
 | Params: N/A
 | Returns: top and bottom return row numbers; clear returns N/A
 ----------------------*/
int  dash_dirty_top(void);
int  dash_dirty_bottom(void);
void dash_dirty_clear(void);

/*----------------------
 | dash_frame_end
 | Description: Closes a frame. When no dash_build call arrived during it, takes
 |   the panel down by building DASH_NONE. A printed border vanished for free
 |   when a menu cleared the text rows; a cell layer does not, so the panel
 |   expires instead of relying on every screen that leaves the console view to
 |   remember to hide it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_touched
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void dash_frame_end(void);

/*----------------------
 | dash_reset
 | Description: Blanks the shadow, forgets the painted variant, and empties the
 |   dirty span. Used at init and by tests.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_map, g_variant, g_base, g_dirty_top, g_dirty_bottom, g_touched
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void dash_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* DASH_MAP_H */
