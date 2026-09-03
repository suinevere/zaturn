/*----------------------
 | map_view.cxx
 | Description: Implements map_view.h.
 | Author: suinevere
 | Dependencies: map_model.h, map_atlas.h, map_layout.h, map_edges.h, dash_map.h,
 |   room_model.h, text_map.h, dash_view.h, title.h, room_art.h, display.h,
 |   app_state.h, input.h, saturn_keyboard.h, soft_reset.h, console_view.h,
 |   party.h, menu.h
 ----------------------*/
#include <srl.hpp>
#include "map_model.h"
#include "map_atlas.h"
#include "dash_map.h"
#include "room_model.h"
#include "text_map.h"
#include "dash_view.h"
#include "title.h"
#include "room_art.h"
#include "display.h"
#include "app_state.h"
#include "input.h"
#include "saturn_keyboard.h"
#include "soft_reset.h"
#include "console_view.h"
#include "menu.h"
#include "party.h"
#include "map_layout.h"
#include "map_edges.h"
#include "../scene/presentation.h"
#include "map_view.h"

/*----------------------
 | MAP_VIS_MAX
 | Description: The most rooms the viewport can show at once -- one per grid
 |   cell -- and so the size of the hoisted walk's arrays.
 | Author: suinevere
 ----------------------*/
#define MAP_VIS_MAX  (MAP_ROOMS_W * MAP_ROOMS_H)

/*----------------------
 | MAP_GROUND_555
 | Description: The tan the ground is tinted to, as a VDP2 BGR555 word. The
 |   tiles are 4bpp indices into palette 1, so the whole ground is these
 |   sixteen CRAM entries and nothing in VRAM moves -- the same arithmetic the
 |   marble chrome already uses to sit on a coloured background.
 | Author: suinevere
 ----------------------*/
#define MAP_GROUND_555 0x2B5Eu

/*----------------------
 | MAP_BG_FILE / MAP_BG_TAG
 | Description: The sheet the map is drawn on when the story does not name one,
 |   and the name NBG0 records it under. A bare /TGA filename, because that is
 |   the directory title.cxx steps into; the tag is what title_bg_loaded_file
 |   answers while the map is up, and is deliberately not a CGL area stem so
 |   room_art's nbg0_shows_area can never mistake the sheet for a room picture
 |   it left there.
 |     The tag stays "MAP" for all four sheets. It says which LAYER holds what,
 |   not which file: only one sheet is ever held at a time, dropped on the way
 |   back to the title before another game can ask for a different one.
 | Author: suinevere
 ----------------------*/
#define MAP_BG_FILE "MAP.TGA"
#define MAP_BG_TAG  "MAP"

/*----------------------
 | MAP_BACK_555
 | Description: MAP_GROUND_555 as a colour rather than as a tint target, which
 |   is a different thing by one bit. dash_tint takes the tint apart into three
 |   channels and puts the opaque bit back itself when it writes CRAM
 |   (dash_view.cxx's write_palette ORs 0x8000), so the constant it is given does
 |   not carry one. A HighColor does: every colour SRL defines has bit 15 set --
 |   HighColor::Colors::Black is 0x8000, not 0 -- and a back-screen colour handed
 |   over without it comes out black, which is exactly what the first build of
 |   this did.
 | Author: suinevere
 ----------------------*/
#define MAP_BACK_555 ((unsigned short) (MAP_GROUND_555 | 0x8000u))


/*----------------------
 | MAP_ROW_PLAYERS / MAP_ROW_STATUS / MAP_ROW_HELP / MAP_TEXT_LEFT /
 | MAP_TEXT_COLS
 | Description: The text rows written over the map and the band they may run
 |   in. All five are derived from the grid rather than written down, because
 |   the point of them is to stay on the paper the grid was inset to reach: the
 |   roster opens on the grid's first row and takes one row per occupied seat,
 |   so it can reach four, and the status and help rows are the two directly
 |   below the grid -- rows twenty-four and twenty-five, the last two of
 |   MAP.TGA's solid band. They used to be twenty-six and twenty-seven, which
 |   is where the sheet is torn, so the floor number on the right of the status
 |   row was printed on black.
 |
 |   MAP_TEXT_LEFT/COLS are the drawing box's own columns, gutter included,
 |   which is narrower than the forty a 320-pixel screen shows and much
 |   narrower than text_map's 64-cell pitch -- printing outside it writes cells
 |   that are either off the paper or off the screen.
 | Author: suinevere
 ----------------------*/
#define MAP_ROW_PLAYERS MAP_TOP
#define MAP_ROW_STATUS  (MAP_CELL_H)
#define MAP_ROW_HELP    (MAP_CELL_H + 1)
#define MAP_TEXT_LEFT   MAP_CLIP_X0
#define MAP_TEXT_COLS   (MAP_CELL_W - MAP_CLIP_X0)

/*----------------------
 | MAP_FLASH_SHIFT
 | Description: How long each half of a player mark's pulse lasts, as a power of
 |   two frames -- sixteen, so a little over a quarter second each way at 60Hz.
 |   The pulse is why this screen repaints anything per frame at all: everything
 |   else it draws is settled by draw_once and held by dash_map_hold.
 | Author: suinevere
 ----------------------*/
#define MAP_FLASH_SHIFT 4

/*----------------------
 | MAP_FLASH_MAX
 | Description: The most marks that can be pulsing at once -- one per seat, and
 |   one more for the local player, who has a mark whether or not the server has
 |   given them a seat.
 | Author: suinevere
 ----------------------*/
#define MAP_FLASH_MAX (PARTY_SEATS + 1)

/*----------------------
 | g_ids / g_dxs / g_dys
 | Description: The rooms inside the viewport and their offsets from the
 |   player, gathered once so the pairwise link walk that follows scans
 |   nothing. At file scope rather than on the stack because seventy entries of
 |   three arrays is more than a menu-depth stack wants to carry.
 | Author: suinevere
 ----------------------*/
static unsigned short g_ids[MAP_VIS_MAX];
static short          g_dxs[MAP_VIS_MAX];
static short          g_dys[MAP_VIS_MAX];

/*----------------------
 | g_slot
 | Description: Which gathered entry each object number landed in, or -1. The
 |   exit walk asks this once per exit; a scan of g_ids instead would put an
 |   O(n) search inside a loop that already runs n times, which is the shape
 |   that cost this screen a dozen frames a redraw once before.
 | Author: suinevere
 ----------------------*/
static short g_slot[MAP_ROOM_MAX];

/*----------------------
 | g_flash_x / g_flash_y / g_flash_tile / g_flash_n
 | Description: The cells holding a player's mark, gathered by draw_once so the
 |   frame loop can pulse them without repeating the room and link walk. Each
 |   alternates between its own tile and DT_ROOM, so a pulsing mark reads as a
 |   room that is being pointed at rather than as one blinking out of existence.
 |
 |   A mark the crosshair is over is left out of this list: the pick has to win
 |   its own cell, and a mark that pulsed under the cursor would spend half its
 |   time denying it had been picked.
 | Author: suinevere
 ----------------------*/
static short         g_flash_x[MAP_FLASH_MAX];
static short         g_flash_y[MAP_FLASH_MAX];
static unsigned char g_flash_tile[MAP_FLASH_MAX];
static int           g_flash_n;

/*----------------------
 | gather
 | Description: Fills g_ids/g_dxs/g_dys with every placed room inside the
 |   viewport, in one pass over the object-number space. map_model_offset is
 |   constant time, where map_model_room_at costs an O(MAP_ROOM_MAX) scan per
 |   call -- which mattered, because draw_once used to nest that scan inside a
 |   pairwise loop and so spent about a dozen frames between one menu_sync and
 |   the next, long enough to starve the looping PCM hand-off.
 |
 |   Rooms on another floor are skipped here rather than at paint time, which is
 |   what keeps the links honest: a link is only drawn between two gathered
 |   rooms, so a staircase leaving this floor simply has no far end to draw to
 |   and no line is invented for it.
 | Author: suinevere
 | Dependencies: map_model.h
 | Globals: g_ids, g_dxs, g_dys, g_slot
 | Params: sx, sy -- the scroll offset in rooms; page -- the floor being shown
 | Returns: how many rooms were gathered
 ----------------------*/
static int gather(int sx, int sy, int page)
{
    int r, n = 0;
    for (r = 0; r < MAP_ROOM_MAX; r++) g_slot[r] = -1;
    for (r = 1; r < MAP_ROOM_MAX && n < MAP_VIS_MAX; r++) {
        int dx = 0, dy = 0;
        if (!map_model_offset((unsigned short) r, &dx, &dy)) continue;
        if (map_model_page((unsigned short) r) != page) continue;
        dx -= sx;
        dy -= sy;
        if (!map_layout_visible(dx, dy)) continue;
        g_ids[n] = (unsigned short) r;
        g_dxs[n] = (short) dx;
        g_dys[n] = (short) dy;
        g_slot[r] = (short) n;
        n++;
    }
    return n;
}

/*----------------------
 | extent
 | Description: The bounding box of every placed room on one floor, as offsets
 |   from the player. It is what the crosshair is clamped to, so the cursor
 |   cannot be walked off into empty ground and lost -- at either limit it sits
 |   on the outermost room of that floor rather than past it.
 | Author: suinevere
 | Dependencies: map_model.h
 | Globals: N/A
 | Params: page -- the floor to measure; x0, x1, y0, y1 -- receive the box; all
 |   zero when the floor holds nothing placed
 | Returns: N/A
 ----------------------*/
static void extent(int page, int *x0, int *x1, int *y0, int *y1)
{
    int r, first = 1;
    *x0 = *x1 = *y0 = *y1 = 0;
    for (r = 1; r < MAP_ROOM_MAX; r++) {
        int dx = 0, dy = 0;
        if (!map_model_offset((unsigned short) r, &dx, &dy)) continue;
        if (map_model_page((unsigned short) r) != page) continue;
        if (first) { *x0 = *x1 = dx; *y0 = *y1 = dy; first = 0; continue; }
        if (dx < *x0) *x0 = dx;
        if (dx > *x1) *x1 = dx;
        if (dy < *y0) *y0 = dy;
        if (dy > *y1) *y1 = dy;
    }
}

/*----------------------
 | put_uint
 | Description: One small unsigned decimal into a buffer, answering where it
 |   stopped. This screen shows two numbers -- a floor and a floor count -- and
 |   nothing else in it formats anything, so a digit loop is the whole
 |   requirement and pulling in a printf for it would not be.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out -- the buffer; at -- where to start; v -- the value
 | Returns: the index one past the last digit written
 ----------------------*/
static int put_uint(char *out, int at, unsigned int v)
{
    char tmp[6];
    int n = 0;
    do { tmp[n++] = (char) ('0' + (v % 10u)); v /= 10u; }
    while (v != 0u && n < (int) sizeof tmp);
    while (n > 0) out[at++] = tmp[--n];
    return at;
}

/*----------------------
 | peer_seat
 | Description: Which other player is standing in a room, if any. The local
 |   player is skipped even when the server has not said which seat is theirs:
 |   map_model_current answers for them first at the one call site, so a seat
 |   matching it would only ever repaint the mark that already won.
 | Author: suinevere
 | Dependencies: party.h
 | Globals: N/A
 | Params: room -- object number
 | Returns: the seat, or -1 when nobody else is there
 ----------------------*/
static int peer_seat(unsigned short room)
{
    int i;
    if (room == 0) return -1;
    for (i = 0; i < PARTY_SEATS; i++) {
        unsigned short rm = 0;
        if (i == party_self()) continue;
        if (!party_seat(i, &rm, 0)) continue;
        if (rm == room) return i;
    }
    return -1;
}

/*----------------------
 | paint_knight
 | Description: Stands the figure beside the local player's own mark, two cells
 |   wide by three tall with one cell of clearance so a link leaving west still
 |   shows where it goes. It goes to the right of the mark instead when the left
 |   would run off the viewport: dash_map_paint drops cells it cannot place, so
 |   the alternative is not a knight that hangs over the edge but half a knight,
 |   which reads as a drawing fault rather than as a figure.
 |
 |   It is painted after the links and before the crosshair, so it covers a
 |   groove running under it and the cursor covers it. A figure that a link was
 |   drawn through would look like part of the map.
 | Author: suinevere
 | Dependencies: dash_map.h, map_layout.h
 | Globals: N/A
 | Params: mx, my -- the cell holding the player's mark
 | Returns: N/A
 ----------------------*/
static void paint_knight(int mx, int my)
{
    int kx, ky, tx, ty;
    map_layout_knight(mx, my, DT_KNIGHT_W, &kx, &ky);
    for (ty = 0; ty < DT_KNIGHT_H; ty++)
        for (tx = 0; tx < DT_KNIGHT_W; tx++)
            dash_map_paint(kx + tx, ky + ty,
                           (unsigned char) (DT_KNIGHT0 + ty * DT_KNIGHT_W + tx));
}

/*----------------------
 | draw_players
 | Description: Writes the roster into the top-left corner: one row per seat
 |   the server has told us about, naming who is in the game and the room they
 |   are standing in.
 |
 |   An empty roster is not an error and is the ordinary state on a disc, which
 |   has no server to hear from. It falls back to the one line the local player
 |   deserves either way, labelled rather than named because offline there is
 |   nobody to have a name.
 |
 |   Every room name comes from the story image the client already holds, so
 |   naming where somebody else is standing costs no traffic and works for a
 |   room this map has never drawn -- which on a difficulty that shows only what
 |   has been walked into is most of them.
 | Author: suinevere
 | Dependencies: party.h, room_model.h, text_map.h, map_model.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void draw_players(void)
{
    char line[MAP_TEXT_COLS - 2];
    int i, row = MAP_ROW_PLAYERS;

    if (party_count() == 0) {
        static const char lbl[] = "Player: ";
        int k = 0;
        while (lbl[k] != '\0') { line[k] = lbl[k]; k++; }
        room_model_object_name(map_model_current(), line + k,
                               (int) sizeof line - k);
        text_print_str(MAP_TEXT_LEFT, row, line);
        return;
    }

    for (i = 0; i < PARTY_SEATS; i++) {
        unsigned short rm = 0;
        const char *nm = 0;
        int k = 0;
        if (!party_seat(i, &rm, &nm)) continue;
        while (nm[k] != '\0' && k < PARTY_NAME_MAX - 1) { line[k] = nm[k]; k++; }
        line[k++] = ':';
        line[k++] = ' ';
        room_model_object_name(rm, line + k, (int) sizeof line - k);
        text_print_str(MAP_TEXT_LEFT, row++, line);
    }
}

/*----------------------
 | edge_stub
 | Description: Draws the short run that says a passage leaves this room toward
 |   one the viewport does not reach. gather() only collects rooms inside the
 |   viewport, and the link pass can only join two gathered rooms, so an exit
 |   whose far end has scrolled off drew nothing at all: step the crosshair one
 |   room and every passage back the way you came vanished rather than running
 |   to the edge. This lays MAP_GUTTER cells of the same dashed-or-solid run a
 |   link is made of, into the margin the grid is inset by.
 |
 |   Only for a room on the floor being shown. An exit to another floor is not
 |   a passage running off the edge of this one, and the U/D pass below already
 |   gives it a letter and a stub of its own; drawing this as well would put two
 |   marks on one exit.
 |
 |   The run carries the exit's own decoration, not the forced dash a stub to
 |   another floor takes: this far end is a room on this floor that has merely
 |   scrolled off, and the passage to it is as solid or as conditional as one
 |   drawn end to end.
 |
 |   The direction is the axis the far room actually left the viewport by, not
 |   the larger of its two offsets: a room one step north-east that is off the
 |   top but not off the right side is reached northward, and a stub pointing
 |   east at a column still on screen would name a passage that is not there.
 |   Off a corner, both are true and the longer leg wins.
 | Author: suinevere
 | Dependencies: map_model.h, map_edges.h, map_layout.h
 | Globals: g_dxs, g_dys
 | Params: cx, cy -- the room's mark cell; i -- its slot, for its own offset;
 |   ex -- the exit; sx, sy -- the view's offset in rooms; page -- the floor
 | Returns: N/A
 ----------------------*/
static void edge_stub(int cx, int cy, int i, const MapExit *ex,
                      int sx, int sy, int page) {
    int dx = 0, dy = 0, ox, oy;

    if (map_model_page(ex->dest) != page) return;
    if (!map_model_offset(ex->dest, &dx, &dy)) return;
    dx -= sx;
    dy -= sy;

    ox = (dx < MAP_DX_MIN) ? -1 : (dx > MAP_DX_MAX) ? 1 : 0;
    oy = (dy < MAP_DY_MIN) ? -1 : (dy > MAP_DY_MAX) ? 1 : 0;
    if (ox != 0 && oy != 0) {
        int ax = dx - g_dxs[i], ay = dy - g_dys[i];
        if (ax < 0) ax = -ax;
        if (ay < 0) ay = -ay;
        if (ay > ax) ox = 0; else oy = 0;
    }
    if (ox == 0 && oy == 0) return;

    map_edges_offview(cx, cy, ox, oy, ex->flags);
}

/*----------------------
 | draw_once
 | Description: Paints one whole frame of the map: every room of one floor the
 |   viewport reaches, the links between them, the figure beside the local
 |   player, the crosshair, and the four rows of text around them -- the roster
 |   at the top, the picked room and the floor number along the bottom. The
 |   ground is not among them: it is the parchment behind this layer, and every
 |   cell nothing is painted into shows it through. Clears the text layer
 |   first,
 |   because the caller is the Options menu, which redraws its title and every
 |   row each frame and would otherwise leave them lit over a map whose box
 |   border dash_map_begin has just blanked.
 |
 |   Every pair of gathered rooms the story links is joined, at whatever
 |   distance -- map_edges_link's header (map_edges.h) has why that is not the
 |   rule it used to be. Links are routed into map_edges first and the whole
 |   layer swept from it afterwards, so a cell two links cross knows about both
 |   and draws as a crossing; painting each link as it was routed would have
 |   left whichever came last. Marks go down after that again, so a mark always
 |   wins its own cell.
 |
 |   The walk is gather() and then a per-room enumeration of what it returned,
 |   both bounded by the viewport rather than by the placed set, which is what
 |   keeps this inside one frame; it used to nest a pairwise scan inside a
 |   pairwise loop and spent about a dozen frames between one menu_sync and the
 |   next, long enough to starve the looping PCM hand-off.
 |
 |   Only one floor is drawn. The floors of an authored table are separate
 |   drawings the publisher split because the geography did, and the table
 |   stacks them into one coordinate space only because it has nowhere else to
 |   put them; showing them all at once shows a tall strip with empty ground
 |   between the parts. A story with no authored table has exactly one floor,
 |   so nothing is hidden from one.
 |
 |   A second walk runs after the links are in, placing the self-loop, U/D and
 |   arrowhead glyphs map_layout_glyph finds room for. A vertical exit whose
 |   destination is on this floor now also labels its own mark from this same
 |   walk, preferring the step toward that destination; one leaving the floor
 |   lays a dashed stub first, since its own run is not already drawn. It has
 |   to come after: the glyph pass reads map_edges_layer to see which cells the
 |   lines already claimed, and asks first before the stub is laid so laying it
 |   cannot push its own letter onto a diagonal by marking its own two cells
 |   occupied. Where the search finds nothing free, this draws neither the stub
 |   nor the letter -- the same declining-is-honest rule the rest of the
 |   placement follows.
 |
 |   Paint order is links (including the arrows and dashes map_edges_tile now
 |   folds in), then marks, then the figure, then the crosshair, and each step
 |   is allowed to cover the one before it. That ordering is the whole priority
 |   rule: a mark wins its own cell over a groove crossing it, the figure wins
 |   over a groove running under it -- a link drawn through it would read as
 |   part of the map -- and the cursor wins over everything, because a cursor
 |   showing the map through itself is harder to find than the room it is
 |   pointing at.
 |
 |   Called on open, on each cursor step that moves the view, and on each floor
 |   change; not per frame. map_view_show holds the NBG2 claim between those with
 |   dash_map_hold and repaints only the pulsing marks, whose cells this leaves
 |   in g_flash_*; the text this writes needs no such upkeep, since text_map has
 |   no per-frame expiry.
 | Author: suinevere
 | Dependencies: map_model.h, dash_map.h, text_map.h, room_model.h, party.h,
 |   menu.h, map_edges.h, map_layout.h, gather, paint_knight, draw_players,
 |   peer_seat, put_uint
 | Globals: g_ids, g_dxs, g_dys, g_flash_x, g_flash_y, g_flash_tile, g_flash_n
 | Params: sx, sy -- the scroll offset in rooms, zero with the player centred;
 |   page -- the floor to draw; hx, hy -- the crosshair, in the same offsets
 |   from the player that map_model_offset answers in
 | Returns: N/A
 ----------------------*/
static void draw_once(int sx, int sy, int page, int hx, int hy) {
    int n, i;
    int hvx = hx - sx, hvy = hy - sy;
    int hcx = map_layout_cell(hx, sx, MAP_CX, MAP_LEFT);
    int hcy = map_layout_cell(hy, sy, MAP_CY, MAP_TOP);
    unsigned short hover = 0;

    menu_clear();
    dash_map_begin();
    g_flash_n = 0;

    // No ground is painted. The map's ground is the parchment on NBG0, or the
    // tan back colour where there is no parchment, and every cell this layer
    // does not claim is DT_BLANK already -- dash_map_begin clears it to that.
    // Paving the viewport with DT_GROUND, which is what this used to do, would
    // hide whichever of the two is behind it.
    n = gather(sx, sy, page);

    map_edges_reset();
    for (i = 0; i < n; i++)
        map_edges_mark(map_layout_cell(g_dxs[i], 0, MAP_CX, MAP_LEFT),
                       map_layout_cell(g_dys[i], 0, MAP_CY, MAP_TOP));

    for (i = 0; i < n; i++) {
        MapExit ex[RM_DIR_N];
        int k, ne = map_model_exits(g_ids[i], ex, RM_DIR_N);
        for (k = 0; k < ne; k++) {
            int j, lo, hi, arrow;
            if (ex[k].flags & MAP_EXIT_SELF) continue;
            j = g_slot[ex[k].dest];
            if (j < 0) {
                edge_stub(map_layout_cell(g_dxs[i], 0, MAP_CX, MAP_LEFT),
                          map_layout_cell(g_dys[i], 0, MAP_CY, MAP_TOP),
                          i, &ex[k], sx, sy, page);
                continue;
            }
            if (!(ex[k].flags & MAP_EXIT_ONEWAY) && ex[k].dest < g_ids[i])
                continue;
            if (g_ids[i] < ex[k].dest) { lo = i; hi = j; } else { lo = j; hi = i; }
            arrow = !(ex[k].flags & MAP_EXIT_ONEWAY) ? 0
                    : (ex[k].dest == g_ids[hi]) ? 1 : 2;
            map_edges_link(map_layout_cell(g_dxs[lo], 0, MAP_CX, MAP_LEFT),
                           map_layout_cell(g_dys[lo], 0, MAP_CY, MAP_TOP),
                           map_layout_cell(g_dxs[hi], 0, MAP_CX, MAP_LEFT),
                           map_layout_cell(g_dys[hi], 0, MAP_CY, MAP_TOP),
                           map_model_link(g_ids[i], ex[k].dest),
                           ex[k].flags, arrow);
        }
    }

    for (i = 0; i < n; i++) {
        MapExit ex[RM_DIR_N];
        int cx = map_layout_cell(g_dxs[i], 0, MAP_CX, MAP_LEFT);
        int cy = map_layout_cell(g_dys[i], 0, MAP_CY, MAP_TOP);
        int k, gx, gy, ne = map_model_exits(g_ids[i], ex, RM_DIR_N);
        const unsigned short (*layer)[MAP_CELL_W] =
            (const unsigned short (*)[MAP_CELL_W]) map_edges_layer();

        for (k = 0; k < ne; k++) {
            int up, dy;
            if (ex[k].flags & MAP_EXIT_SELF) {
                int sdx, sdy;
                map_model_step(ex[k].dir, &sdx, &sdy);
                if (map_layout_glyph(cx, cy, sdx, sdy, layer, &gx, &gy))
                    map_edges_glyph(gx, gy, MAP_EDGE_LOOP);
                continue;
            }
            if (ex[k].kind != MAP_LINK_VERT) continue;
            up = (ex[k].dir & 1) == 0;
            if (g_slot[ex[k].dest] >= 0) {
                int j = g_slot[ex[k].dest];
                int pdx = g_dxs[j] - g_dxs[i];
                int pdy = g_dys[j] - g_dys[i];
                pdx = (pdx > 0) - (pdx < 0);
                pdy = (pdy > 0) - (pdy < 0);
                if (map_layout_glyph(cx, cy, pdx, pdy, layer, &gx, &gy))
                    map_edges_glyph(gx, gy, up ? MAP_EDGE_UP : MAP_EDGE_DOWN);
                continue;
            }
            if (map_model_visited(ex[k].dest) &&
                map_model_page(ex[k].dest) == page) continue;
            dy = up ? -1 : 1;
            if (!map_layout_glyph(cx, cy, 0, dy, layer, &gx, &gy)) continue;
            if (gx == cx && gy == cy + 2 * dy &&
                map_layout_cell_free(cx, cy + dy, layer))
                map_edges_stub(cx, cy, 0, dy, ex[k].flags);
            map_edges_glyph(gx, gy, up ? MAP_EDGE_UP : MAP_EDGE_DOWN);
        }
    }

    for (i = MAP_CLIP_Y0; i < MAP_CELL_H; i++) {
        int c;
        for (c = MAP_CLIP_X0; c < MAP_CELL_W; c++) {
            unsigned char t = map_edges_tile(c, i);
            if (t) dash_map_paint(c, i, t);
        }
    }

    for (i = 0; i < n; i++) {
        int cx = map_layout_cell(g_dxs[i], 0, MAP_CX, MAP_LEFT);
        int cy = map_layout_cell(g_dys[i], 0, MAP_CY, MAP_TOP);
        int picked = (g_dxs[i] == (short) hvx && g_dys[i] == (short) hvy);
        unsigned char tile = DT_ROOM;
        int flash = 0;

        if (g_ids[i] == map_model_current())  { tile = DT_ROOM_HERE; flash = 1; }
        else if (peer_seat(g_ids[i]) >= 0)    { tile = DT_ROOM_PEER; flash = 1; }
        /* The pick recolours an ordinary room and leaves a player's mark alone.
           The crosshair opens sitting on the local player, so a pick that
           overrode every mark would hide the one thing the screen is for and
           stop its pulse before the player had touched anything -- the four
           brackets say what is picked in either case, and they are drawn last
           so they say it over whatever the mark turned out to be. */
        if (picked) {
            hover = g_ids[i];
            if (tile == DT_ROOM) tile = DT_ROOM_SEL;
        }

        dash_map_paint(cx, cy, tile);
        if (flash && g_flash_n < MAP_FLASH_MAX) {
            g_flash_x[g_flash_n] = (short) cx;
            g_flash_y[g_flash_n] = (short) cy;
            g_flash_tile[g_flash_n] = tile;
            g_flash_n++;
        }
        if (g_ids[i] == map_model_current()) paint_knight(cx, cy);
    }

    dash_map_paint(hcx - 1, hcy - 1, DT_XHAIR_TL);
    dash_map_paint(hcx + 1, hcy - 1, DT_XHAIR_TR);
    dash_map_paint(hcx - 1, hcy + 1, DT_XHAIR_BL);
    dash_map_paint(hcx + 1, hcy + 1, DT_XHAIR_BR);

    {
        char nm[MAP_TEXT_COLS - 2];
        char pg[8];
        int pages = map_model_pages();
        int k;

        draw_players();

        if (hover != 0 && room_model_object_name(hover, nm, (int) sizeof nm))
            text_print_str(MAP_TEXT_LEFT, MAP_ROW_STATUS, nm);

        k = put_uint(pg, 0, (unsigned int) (page + 1));
        pg[k++] = '/';
        k = put_uint(pg, k, (unsigned int) pages);
        pg[k] = '\0';
        text_print_str(MAP_TEXT_LEFT + MAP_TEXT_COLS - k, MAP_ROW_STATUS, pg);

        text_print_str(MAP_TEXT_LEFT, MAP_ROW_HELP,
                       (pages > 1) ? "D-pad: pick  L/R: floor  A/B/C: back"
                                   : "D-pad: pick     A/B/C: back");
        text_flush();
    }
}

/*----------------------
 | map_view_show
 | Description: See map_view.h. Holds itself the way every full-screen page in
 |   menu_pages.cxx does (credits_page is the closest analog): a loop that polls
 |   input, checks for an exit, and otherwise advances the frame -- not a single
 |   draw followed by menu_wait's generic block, which does not re-touch
 |   dash_map's NBG2 claim and so would lose it a frame after draw_once painted
 |   it.
 |
 |   The D-pad moves a crosshair rather than the map. It is the same
 |   pad_repeat_update/pad_fired pair every other page uses for held movement, so
 |   the delay before it runs on matches the rest of the interface, and the room
 |   under it is named along the bottom. The view follows the cursor instead of
 |   being steered: it only moves when the cursor would otherwise leave the
 |   viewport, and then by exactly enough to keep it inside. That is what lets
 |   one control do both jobs -- a map that scrolled under a fixed cursor would
 |   need a second binding to reach a room the scroll clamp had stopped short of.
 |
 |   extent() clamps the cursor to the rooms placed on the floor being shown, so
 |   it cannot be walked off into empty ground and lost, and the view is clamped
 |   only by following it.
 |
 |   L and R change floor. They are free here -- A, B, C and Start are all back,
 |   and the D-pad is the cursor -- and they wrap, since a two-floor game would
 |   otherwise need the player to remember which way they had come. A floor
 |   change recentres both cursor and view on that floor's own extent, because
 |   the offsets that put the player in the middle of one floor point at nothing
 |   on another.
 |
 |   Neither the cursor nor the floor is carried across an open. The player is
 |   centred and the floor is theirs each time the screen appears, however far
 |   it was scrolled when it last closed -- there is no position to restore
 |   because none is kept.
 |
 |   draw_once runs on open and on each step that changes something, and not on
 |   frames where nothing moved. What does run every frame is the pulse: the
 |   marks draw_once left in g_flash_* alternate with DT_ROOM every sixteen
 |   frames, so a player's own room and everybody else's beat against a map that
 |   is otherwise still. It is done by repainting those few cells rather than by
 |   redrawing, because redrawing is the room and link walk and that is not free.
 |
 |   dash_tint rewrites the sixteen CRAM entries every NBG2 tile draws from, so
 |   the tan is captured on the way in and put back on the way out; without that
 |   the gamepad strip and every menu box wear it for the rest of the session.
 |
 |   Difficulty decides how much of the map there is. Easy reveals the whole
 |   authored table on open, which is the reveal the development switch used to
 |   force; Medium takes it back and draws only what the player has walked into,
 |   still placed where the atlas says. Both consult the same table, so Easy on a
 |   story nobody drew is Medium by falling through map_model_reveal_atlas rather
 |   than by testing for the table here. Hard never reaches this function at all
 |   -- options_menu drops the Map row -- and this deliberately does not check for
 |   it a second time: a page that silently closed itself would read as a broken
 |   menu rather than as a disabled feature.
 |
 |   The clear on Medium is not tidiness. A placed room never moves and the model
 |   has no other memory of how it came to be placed, so without it one open on
 |   Easy would leave the whole drawing on the map for the rest of the session,
 |   through every later difficulty change.
 |
 |   The wallpaper is replaced for the map's duration by the parchment and
 |   restored by asking room_art for the room again, which re-uploads because
 |   the parchment has taken the layer's recorded name with it. None of that is compiled into the netbin, which has neither room art
 |   nor a title background to put back -- the three symbols it would need
 |   (title_bg_hide, room_art_available, room_art_reshow) are the only ones in
 |   this file that build does not already link, which is why they are the only
 |   thing guarded rather than the file being split.
 |
 |   The wallpaper used to be restored by re-showing title_bg_loaded_file() by
 |   name, which was wrong twice over: for a CGL frame that name is an area stem
 |   and no file, so the picture never came back, and on a game with no art at
 |   all the name was still the boot logo's -- so closing the map put the
 |   SUINEVERE logo up behind the game and left it there.
 | Author: suinevere
 | Dependencies: draw_once, extent, map_model.h (map_model_reveal_atlas,
 |   map_model_clear_reveal, map_model_page, map_model_pages), dash_map.h,
 |   dash_view.h, menu.h, input.h, saturn_keyboard.h, soft_reset.h,
 |   console_view.h, title.h, room_art.h, display.h, app_state.h
 | Globals: g_difficulty, g_flash_x, g_flash_y, g_flash_tile, g_flash_n
 | Params: N/A
 | Returns: N/A
 ----------------------*/
/*----------------------
 | map_view_preload
 | Description: See map_view.h.
 | Author: suinevere
 | Dependencies: title.h (title_bg_hold)
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void map_view_preload(unsigned int release, const char *serial) {
#ifndef NETBIN
    const char *sheet = pres_map_bg(release, serial);
    title_bg_hold((sheet != nullptr) ? sheet : MAP_BG_FILE);
#else
    (void) release; (void) serial;
#endif
}

extern "C" void map_view_show(void) {
    MenuBacking backing;
    int sx = 0, sy = 0, hx = 0, hy = 0;
    int pages, page, frame = 0, phase = -1;
    unsigned short tint = dash_tint_current();
    bool parchment = false;
#ifndef NETBIN
    const bool had_art = (g_display.palette == DISP_PAL_DYNAMIC)
                         && room_art_available() != 0;

    // The parchment goes on NBG0 rather than into the tile layer because NBG2
    // has one plane and the marks have to sit over the paper, not carry a patch
    // of it each. NBG0 is already the layer below NBG2 -- priorities are 1 and 2
    // -- so it needs no reordering, and room_art puts the room back on it when
    // the map closes.
    //
    // Read once and held for the rest of the session. The map is opened and
    // closed repeatedly with a CD-DA track playing, and tga_decode is the one
    // thing here that touches the drive; a read per open would stop the music
    // every time, which is the reason the room pictures stopped being TGAs.
    // Asked, never read. The read is map_view_preload's, done under the loading
    // ramp before CD-DA has started, because a data seek silences the track
    // whatever else is going on -- and a track that was never held reads to the
    // engine as one that ended, so it restarts from the top rather than
    // resuming. That is the cut in and out this used to make on the first open.
    // Reading here was worse than once, too: tga_decode reads the header before
    // it checks the heap, so on a story too large to hold the picture the seek
    // happened on every open and still put no paper up.
    if (title_bg_held()) parchment = title_bg_show_held(MAP_BG_TAG);
    if (!parchment) title_bg_hide();
#endif
    // The menu that opened this armed the VDP2 window that suppresses NBG0
    // inside a box -- MenuBacking's constructor switches it on and every
    // menu_frame aims it at whatever box is being drawn -- and nothing re-aims
    // it for a full-screen page that draws no box. Left on, it cuts the last
    // menu's rectangle out of the parchment and the back colour shows through:
    // nineteen cells by fifteen of black in the middle of the sheet. It was
    // invisible for as long as the map paved itself with opaque ground, and
    // appeared the moment it stopped.
    image_window_off();

    // Where there is no parchment -- the netbin, which has no drive, and a disc
    // whose MAP.TGA would not read -- the back colour is the ground instead, so
    // the marks sit on flat tan rather than on black.
    //
    // Not touched when there is one. The sheet's torn edges are transparent by
    // design and are drawn to read against a dark ground; filling that with tan
    // would flatten the edge the picture is shaped around.
    // The override alongside the colour, or the fade below undoes this on its
    // first frame: every ramp recomputes the backdrop from the player's own
    // background setting, so the tan lasted until the screen started coming up
    // and the map arrived on their colour instead. Nothing saw it while a
    // parchment covered the ground; on a story too large to hold one it was the
    // entire screen.
    if (!parchment) {
        SRL::VDP2::SetBackColor(SRL::Types::HighColor(MAP_BACK_555));
        menu_back_override(MAP_BACK_555);
    }
    dash_tint(MAP_GROUND_555);
    if (g_difficulty == DIFF_EASY) map_model_reveal_atlas();
    else                           map_model_clear_reveal();

    pages = map_model_pages();
    page = map_model_page(map_model_current());
    if (page >= pages) page = pages - 1;
    if (page < 0)      page = 0;

    draw_once(sx, sy, page, hx, hy);
    menu_sync();
    // The Options row that opens this fades to black first, the same as it does
    // for every other page it can reach, so the map has to bring the screen back
    // up itself or it is drawn where nobody can see it. Guarded on
    // g_menu_page_fade like menu_pages.cxx's page_fade_in/out, and after the
    // first menu_sync for the reason those are: menu_fade_in reveals a frame
    // that is already composed.
    if (g_menu_page_fade > 0) menu_fade_in(g_menu_page_fade);
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        bool back = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
                    g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START) ||
                    ke.kind != SATURN_KEY_NONE;
        if (back) break;

        pad_repeat_update();
        {
            int nx = hx, ny = hy, np = page, x0, x1, y0, y1;
            if (pad_fired(Button::Left))  nx--;
            if (pad_fired(Button::Right)) nx++;
            if (pad_fired(Button::Up))    ny--;
            if (pad_fired(Button::Down))  ny++;
            if (pad_fired(Button::L))     np--;
            if (pad_fired(Button::R))     np++;
            if (np < 0)       np = pages - 1;
            if (np >= pages)  np = 0;

            if (np != page) {
                page = np;
                extent(page, &x0, &x1, &y0, &y1);
                hx = (x0 + x1) / 2;
                hy = (y0 + y1) / 2;
                sx = hx;
                sy = hy;
                draw_once(sx, sy, page, hx, hy);
                phase = -1;
            } else if (nx != hx || ny != hy) {
                extent(page, &x0, &x1, &y0, &y1);
                if (nx < x0) nx = x0;
                if (nx > x1) nx = x1;
                if (ny < y0) ny = y0;
                if (ny > y1) ny = y1;
                if (nx != hx || ny != hy) {
                    hx = nx;
                    hy = ny;
                    map_layout_follow(hx, hy, &sx, &sy);
                    draw_once(sx, sy, page, hx, hy);
                    phase = -1;
                }
            }
        }

        dash_map_hold();
        {
            int ph = (frame >> MAP_FLASH_SHIFT) & 1, i;
            if (ph != phase) {
                phase = ph;
                for (i = 0; i < g_flash_n; i++)
                    dash_map_paint(g_flash_x[i], g_flash_y[i],
                                   ph ? g_flash_tile[i] : DT_ROOM);
            }
        }
        frame++;
        menu_sync();
    }

    if (g_menu_page_fade > 0) menu_fade_out(g_menu_page_fade);
    {
        int r;
        for (r = MAP_ROW_PLAYERS; r < MAP_ROW_PLAYERS + PARTY_SEATS; r++)
            text_clear_line(r);
    }
    text_clear_line(MAP_ROW_STATUS);
    text_clear_line(MAP_ROW_HELP);
    text_flush();
    dash_tint(tint);
    if (!parchment) {
        // After the fade-out above, so that ramp takes the tan down rather than
        // the player's colour, and before the page underneath fades itself back
        // in on its own.
        menu_back_override(0);
        // Back to what SRL::Core::Initialize set in both builds (main.cxx:361,
        // main_netbin.cxx:251), so this restores a known value rather than a
        // guess.
        SRL::VDP2::SetBackColor(SRL::Types::HighColor::Colors::Black);
    }
    // And the window back on for the page underneath, which is where it came
    // from: this screen is only ever reached from a menu, so a MenuBacking is
    // always up around it and wants its box suppressing the image again. Its
    // rectangle is still aimed where that page last put it, and the page aims
    // it again on its next menu_frame either way. If this were ever the
    // outermost backing instead, the destructor below re-defers the switch-off
    // and undoes this, which is also right.
    image_window_on();
#ifndef NETBIN
    // room_art_reshow re-uploads rather than trusting what is on the layer --
    // its own check is against the area NBG0 records, and the parchment
    // records MAP -- so the room comes back whether or not it was there before.
    // A game with no art has nothing to put back, so the parchment is taken
    // down instead of being left standing behind the console.
    if (had_art)         room_art_reshow();
    else if (parchment)  title_bg_hide();
#endif
}
