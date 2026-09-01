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
 |   marble, one tile per edge rather than four because there is no field to
 |   hold in register. Nothing paints it any more -- menu boxes take the marble
 |   path with everything else (see cell_at) -- but the tiles stay in the set
 |   rather than being cut, because every index after them is a literal in
 |   dash_tiles.c and removing eight would renumber the map tiles for 256 bytes
 |   of VRAM.
 |
 |   The DT_PIC_* set is the inventory overlay's second frame: the same bead
 |   over the same marble as the module frames, but facing inward, so it closes
 |   hard against the item picture rather than around the module. It goes last
 |   for the same numbering reason the DT_BOX_* set stays.
 |
 |   After it come the map's cursor and its figure. DT_ROOM_SEL is the room the
 |   crosshair is over and DT_ROOM_PEER another player's room, both differing
 |   from the two older marks in shape as well as value because four marks is
 |   one more than a single tinted ramp can separate by lightness alone.
 |   DT_XHAIR_* are the reticle's four corners, which go in the cells diagonally
 |   around the picked mark. DT_KNIGHT0 opens six tiles holding one 16x24
 |   drawing, row-major two wide by three tall, painted beside the player's own
 |   room; they are on the ground field rather than on transparency, since NBG2
 |   is one layer and a transparent pixel is a hole through the map.
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
    DT_GROUND, DT_ROOM, DT_ROOM_HERE,
    DT_LINK0,
    DT_LINK_STAIR = DT_LINK0 + 16,
    DT_PIC_TOP0, DT_PIC_TOP1, DT_PIC_TOP2, DT_PIC_TOP3,
    DT_PIC_BOTTOM0, DT_PIC_BOTTOM1, DT_PIC_BOTTOM2, DT_PIC_BOTTOM3,
    DT_PIC_LEFT0, DT_PIC_LEFT1, DT_PIC_LEFT2, DT_PIC_LEFT3,
    DT_PIC_RIGHT0, DT_PIC_RIGHT1, DT_PIC_RIGHT2, DT_PIC_RIGHT3,
    DT_PIC_TL, DT_PIC_TR, DT_PIC_BL, DT_PIC_BR,
    DT_ROOM_SEL, DT_ROOM_PEER,
    DT_XHAIR_TL, DT_XHAIR_TR, DT_XHAIR_BL, DT_XHAIR_BR,
    DT_KNIGHT0,
    DT_N = DT_KNIGHT0 + 6
};

/*----------------------
 | DT_KNIGHT_W / DT_KNIGHT_H
 | Description: The knight's size in cells. Named here rather than left as
 |   literals at the one call site because the tile generator asserts the source
 |   drawing is exactly this many cells, and the two have to agree.
 | Author: suinevere
 ----------------------*/
#define DT_KNIGHT_W 2
#define DT_KNIGHT_H 3

/*----------------------
 | DT_EDGE_N .. DT_EDGE_W / DT_LINK_H / DT_LINK_V
 | Description: The bits that index the link tiles, one per side the groove
 |   leaves a cell through, and names for the two masks that come out straight.
 |   DT_LINK0 + mask is the tile: two opposite sides give a straight run, two
 |   adjacent an elbow, three a T and all four a crossing, so a caller that
 |   accumulates which way a line enters and leaves each cell never has to
 |   choose a shape.
 | Author: suinevere
 ----------------------*/
#define DT_EDGE_N 1
#define DT_EDGE_E 2
#define DT_EDGE_S 4
#define DT_EDGE_W 8
#define DT_LINK_H (DT_LINK0 + (DT_EDGE_E | DT_EDGE_W))
#define DT_LINK_V (DT_LINK0 + (DT_EDGE_N | DT_EDGE_S))

/*----------------------
 | DASH_NONE .. DASH_VARIANT_N
 | Description: The fixed panel shapes, the nothing-painted state the shadow
 |   starts in, and the runtime rectangle. PANEL and GAMEKB are the two in-game
 |   gamepad strips and OVERLAY is PANEL without its dividers. OVERLAY_TALL is
 |   OVERLAY five rows taller and split once, for the overlay's own picture
 |   module, which only a story with item art draws; that module's interior
 |   carries a second frame of its own, closing hard against the picture.
 |   BOX is OVERLAY's shape with its rectangle supplied per call by dash_box
 |   rather than read from the table, since a menu is sized and placed at
 |   runtime; it paints the same bevel and marble field every other variant
 |   does.
 | Author: suinevere
 ----------------------*/
enum { DASH_NONE = 0, DASH_PANEL, DASH_GAMEKB, DASH_OVERLAY, DASH_OVERLAY_TALL,
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
 | Description: Paints an arbitrary rectangle as a bevelled frame over a marble
 |   field, for the menu boxes -- the same stone the gamepad panel is made of, so
 |   a menu is a slab rather than an outline with the wallpaper showing through
 |   it. dash_tint carries the player's background hue into that stone, which is
 |   what the flat colour behind a transparent box used to do.
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
 | dash_hold_painted
 | Description: Keeps whatever is on the layer up for one more frame, whichever
 |   of the variants it is -- box, map, panel, keyboard or overlay -- and reports
 |   whether there was anything to keep. The variant-specific holds beside this
 |   one each answer for their own picture and are no use to a caller that does
 |   not know which is up; asking one of them while another is painted is worse
 |   than asking none, since dash_hold repaints the strip over whatever it finds.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_variant, g_touched
 | Params: N/A
 | Returns: 1 if something was painted and is now held, 0 if the layer is empty
 ----------------------*/
int dash_hold_painted(void);

/*----------------------
 | dash_hold_latch
 | Description: Holds whatever is painted across frames nobody claims it on,
 |   until it is switched back off. dash_hold_painted answers for one frame and
 |   needs a caller on each of them; this is for the stretch where there is no
 |   such caller -- a menu box whose owner has already returned, waiting on the
 |   text flush that turns its window off. Without it the marble expires on the
 |   first unclaimed frame while the window is still suppressing the picture, so
 |   the box goes hollow -- backdrop colour and the menu's own letters, sitting
 |   there until the flush finally arrives.
 | Author: suinevere
 ----------------------*/
void dash_hold_latch(int on);

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
 | Returns: 1 for DASH_PANEL, DASH_GAMEKB, DASH_OVERLAY or DASH_OVERLAY_TALL,
 |   else 0
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
