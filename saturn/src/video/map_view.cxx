/*----------------------
 | map_view.cxx
 | Description: Implements map_view.h.
 | Author: suinevere
 | Dependencies: map_model.h, dash_map.h, room_model.h, text_map.h, dash_view.h,
 |   title.h, room_art.h, display.h, app_state.h, input.h, saturn_keyboard.h,
 |   soft_reset.h, console_view.h, menu.h
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
#include "map_view.h"

/*----------------------
 | MAP_CELLS / MAP_ROOMS_W / MAP_ROOMS_H / MAP_CX / MAP_CY / MAP_TOP
 | Description: One room is four text cells -- the original's 32-pixel step
 |   over an 8x8 font -- so a 320x224 screen shows ten rooms by seven, and the
 |   player sits at the middle one. Seven rooms of four rows is exactly the 28
 |   rows the screen has, so MAP_TOP is zero and the map fills it; it exists as
 |   a name rather than a bare 0 because the ground and the marks must agree on
 |   it, and they did not in an earlier draft. Named MAP_ROOMS_W/H rather than
 |   MAP_VIEW_W/H because the latter collides with this file's own header
 |   guard (map_view.h defines MAP_VIEW_H as its include guard).
 | Author: suinevere
 ----------------------*/
#define MAP_CELLS    4
#define MAP_ROOMS_W  10
#define MAP_ROOMS_H  7
#define MAP_CX       5
#define MAP_CY       3
#define MAP_TOP      0

/*----------------------
 | MAP_REVEAL_ALL
 | Description: Set to draw every room the authored table covers rather than
 |   only the rooms the player has walked into. A development aid for checking
 |   the placements against Infocom's drawing without playing to each room, and
 |   the one switch to clear to get the explored-only map back.
 | Author: suinevere
 ----------------------*/
#define MAP_REVEAL_ALL 1

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
 | occupied
 | Description: Whether one of the gathered rooms sits at a viewport offset.
 |   Asking the gathered set rather than the model is complete for the question
 |   a link needs to ask, because gather() takes every placed room whose offset
 |   falls inside the viewport and a link between two of them runs entirely
 |   inside it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_dxs, g_dys
 | Params: n -- how many rooms gather() returned; dx, dy -- the offset to test
 | Returns: 1 when a room holds that cell, 0 otherwise
 ----------------------*/
static int occupied(int n, int dx, int dy)
{
    int i;
    for (i = 0; i < n; i++)
        if (g_dxs[i] == (short) dx && g_dys[i] == (short) dy) return 1;
    return 0;
}

/*----------------------
 | cell_is_mark
 | Description: Whether a text cell is the one a room's mark occupies. Only
 |   cells on the four-cell grid can be, so the two divisions are guarded by the
 |   remainders rather than performed on every cell of a link.
 | Author: suinevere
 | Dependencies: occupied
 | Globals: N/A
 | Params: cx, cy -- the cell; n -- how many rooms gather() returned
 | Returns: 1 when a room mark holds the cell, 0 otherwise
 ----------------------*/
static int cell_is_mark(int cx, int cy, int n)
{
    int gy = cy - MAP_TOP;
    if (cx % MAP_CELLS != 0 || gy % MAP_CELLS != 0) return 0;
    return occupied(n, cx / MAP_CELLS - MAP_CX, gy / MAP_CELLS - MAP_CY);
}

/*----------------------
 | MAP_EDGE_STAIR
 | Description: The fifth bit of an edge cell, beside the four DT_EDGE_* sides:
 |   set when a vertical exit -- a staircase rather than a walk -- laid the line.
 |   Kept out of the low nibble so the nibble still indexes the tile set.
 | Author: suinevere
 ----------------------*/
#define MAP_EDGE_STAIR 16

/*----------------------
 | g_edge
 | Description: Which sides of each cell a link leaves through, accumulated over
 |   every link before anything is painted. Two lines crossing a cell leave five
 |   or six bits between them and the cell draws as a T or a crossing, which a
 |   renderer painting each link as it walked could not do: it would only ever
 |   see one line at a time and would overwrite the first with the second.
 |
 |   A whole viewport of bytes rather than a sparse list because the render pass
 |   then costs one sweep with no lookup, and 1120 bytes is nothing beside the
 |   story image the heap is already carrying.
 | Author: suinevere
 ----------------------*/
static unsigned char g_edge[MAP_ROOMS_H * MAP_CELLS][MAP_ROOMS_W * MAP_CELLS];

/*----------------------
 | mark_step
 | Description: Records one cell-to-cell step of a route, setting the side it
 |   leaves the first cell by and the facing side it enters the second by, so the
 |   two tiles meet. Ignores a step either end of which is off the viewport,
 |   which no candidate route should produce but which would corrupt the
 |   neighbouring static if one ever did.
 | Author: suinevere
 | Dependencies: dash_map.h
 | Globals: g_edge
 | Params: x, y -- the cell stepped from; nx, ny -- the cell stepped to, always
 |   one cell away in exactly one axis; stair -- nonzero for a vertical exit
 | Returns: N/A
 ----------------------*/
static void mark_step(int x, int y, int nx, int ny, int stair)
{
    int out, in;
    if (x < 0 || y < 0 || nx < 0 || ny < 0) return;
    if (x >= MAP_ROOMS_W * MAP_CELLS || nx >= MAP_ROOMS_W * MAP_CELLS) return;
    if (y >= MAP_ROOMS_H * MAP_CELLS || ny >= MAP_ROOMS_H * MAP_CELLS) return;

    if      (nx > x) { out = DT_EDGE_E; in = DT_EDGE_W; }
    else if (nx < x) { out = DT_EDGE_W; in = DT_EDGE_E; }
    else if (ny > y) { out = DT_EDGE_S; in = DT_EDGE_N; }
    else             { out = DT_EDGE_N; in = DT_EDGE_S; }

    if (stair) { out |= MAP_EDGE_STAIR; in |= MAP_EDGE_STAIR; }
    g_edge[y][x]   |= (unsigned char) out;
    g_edge[ny][nx] |= (unsigned char) in;
}

/*----------------------
 | trace
 | Description: Walks an orthogonal route through a list of corner points,
 |   either recording it or only testing it. In test mode it stops at the first
 |   cell short of the far end that a room mark holds and answers 0; in record
 |   mode it marks every step and answers 1.
 |
 |   The two modes share one body deliberately: the route that gets drawn has to
 |   be the one that was checked, and two separate walkers would eventually
 |   disagree about which cells that is.
 | Author: suinevere
 | Dependencies: mark_step, cell_is_mark
 | Globals: N/A
 | Params: pts -- corner points as x, y pairs; npts -- how many points; n -- how
 |   many rooms gather() returned; stair -- nonzero for a vertical exit; record
 |   -- nonzero to mark, zero to only test
 | Returns: 1 when the route touches no room mark between its ends, 0 otherwise
 ----------------------*/
static int trace(const short *pts, int npts, int n, int stair, int record)
{
    int ex = pts[(npts - 1) * 2], ey = pts[(npts - 1) * 2 + 1];
    int i, x = pts[0], y = pts[1];
    for (i = 1; i < npts; i++) {
        int tx = pts[i * 2], ty = pts[i * 2 + 1];
        while (x != tx || y != ty) {
            int px = x, py = y;
            if      (x < tx) x++;
            else if (x > tx) x--;
            else if (y < ty) y++;
            else             y--;
            if (record) mark_step(px, py, x, y, stair);
            else if (!(x == ex && y == ey) && cell_is_mark(x, y, n)) return 0;
        }
    }
    return 1;
}

/*----------------------
 | paint_link
 | Description: Records the route joining two room marks, choosing one that
 |   passes through no other room.
 |
 |   That choice is the point of this function, and it is what the previous
 |   version got wrong. It routed every pair the same way -- half the horizontal,
 |   then the vertical, then the rest -- and where a third room happened to sit
 |   on that path the line ran in one side of it and out the other, which reads
 |   as two links rather than as one passing by. Against the shipped table that
 |   happened to fourteen of Zork I's links: North of House to Behind House ran
 |   straight through the Kitchen, and Forest (3) to Forest (1) through four
 |   rooms in a row. Skipping the paint on the mark itself, which is what it did,
 |   does not help at all -- the line still arrives and leaves.
 |
 |   Four routes are tried in order and the first clean one is taken: the two L
 |   shapes, then a dogleg bending at the midpoint of whichever axis is long
 |   enough to have one. Every candidate stays inside the rectangle spanned by
 |   the two rooms, so none can wander off the viewport. Over the twenty rooms of
 |   the shipped table the two L shapes alone answer all twenty-nine links and
 |   the doglegs have never been needed; they are there because a table drawn for
 |   another story will not have that property. If nothing is clean the first
 |   candidate is drawn anyway, on the grounds that a map missing a link is worse
 |   than one drawing an ambiguous link.
 |
 |   Nothing is painted here. The route is accumulated into g_edge and draw_once
 |   paints the layer in one sweep afterwards, which is what lets a cell two
 |   lines cross come out as a crossing rather than as whichever was drawn last.
 | Author: suinevere
 | Dependencies: trace
 | Globals: g_edge
 | Params: ax, ay -- the source mark's cell; bx, by -- the destination mark's;
 |   n -- how many rooms gather() returned; kind -- MAP_LINK_FLAT or
 |   MAP_LINK_VERT
 | Returns: N/A
 ----------------------*/
static void paint_link(int ax, int ay, int bx, int by, int n, int kind)
{
    int stair = (kind == MAP_LINK_VERT);
    int mx = (ax + bx) / 2, my = (ay + by) / 2;
    short cand[4][8];
    int len[4], i, ncand = 2;

    cand[0][0] = (short) ax; cand[0][1] = (short) ay;
    cand[0][2] = (short) bx; cand[0][3] = (short) ay;
    cand[0][4] = (short) bx; cand[0][5] = (short) by;
    len[0] = 3;

    cand[1][0] = (short) ax; cand[1][1] = (short) ay;
    cand[1][2] = (short) ax; cand[1][3] = (short) by;
    cand[1][4] = (short) bx; cand[1][5] = (short) by;
    len[1] = 3;

    if (mx != ax && mx != bx) {
        cand[ncand][0] = (short) ax; cand[ncand][1] = (short) ay;
        cand[ncand][2] = (short) mx; cand[ncand][3] = (short) ay;
        cand[ncand][4] = (short) mx; cand[ncand][5] = (short) by;
        cand[ncand][6] = (short) bx; cand[ncand][7] = (short) by;
        len[ncand] = 4;
        ncand++;
    }
    if (my != ay && my != by) {
        cand[ncand][0] = (short) ax; cand[ncand][1] = (short) ay;
        cand[ncand][2] = (short) ax; cand[ncand][3] = (short) my;
        cand[ncand][4] = (short) bx; cand[ncand][5] = (short) my;
        cand[ncand][6] = (short) bx; cand[ncand][7] = (short) by;
        len[ncand] = 4;
        ncand++;
    }

    for (i = 0; i < ncand; i++) {
        if (!trace(cand[i], len[i], n, stair, 0)) continue;
        trace(cand[i], len[i], n, stair, 1);
        return;
    }
    trace(cand[0], len[0], n, stair, 1);
}

/*----------------------
 | gather
 | Description: Fills g_ids/g_dxs/g_dys with every placed room inside the
 |   viewport, in one pass over the object-number space. map_model_offset is
 |   constant time, where map_model_room_at costs an O(MAP_ROOM_MAX) scan per
 |   call -- which mattered, because draw_once used to nest that scan inside a
 |   pairwise loop and so spent about a dozen frames between one menu_sync and
 |   the next, long enough to starve the looping PCM hand-off.
 | Author: suinevere
 | Dependencies: map_model.h
 | Globals: g_ids, g_dxs, g_dys
 | Params: N/A
 | Returns: how many rooms were gathered
 ----------------------*/
static int gather(int sx, int sy)
{
    int r, n = 0;
    for (r = 1; r < MAP_ROOM_MAX && n < MAP_VIS_MAX; r++) {
        int dx = 0, dy = 0;
        if (!map_model_offset((unsigned short) r, &dx, &dy)) continue;
        dx -= sx;
        dy -= sy;
        if (dx < -MAP_CX || dx >= MAP_ROOMS_W - MAP_CX) continue;
        if (dy < -MAP_CY || dy >= MAP_ROOMS_H - MAP_CY) continue;
        g_ids[n] = (unsigned short) r;
        g_dxs[n] = (short) dx;
        g_dys[n] = (short) dy;
        n++;
    }
    return n;
}

/*----------------------
 | extent
 | Description: The bounding box of every placed room, as offsets from the
 |   player. It is what the scroll is clamped to, so the view cannot be walked
 |   off into empty ground and lost -- at either limit the centre of the
 |   viewport sits on the outermost room rather than past it.
 | Author: suinevere
 | Dependencies: map_model.h
 | Globals: N/A
 | Params: x0, x1, y0, y1 -- receive the box; all zero when nothing is placed
 | Returns: N/A
 ----------------------*/
static void extent(int *x0, int *x1, int *y0, int *y1)
{
    int r, first = 1;
    *x0 = *x1 = *y0 = *y1 = 0;
    for (r = 1; r < MAP_ROOM_MAX; r++) {
        int dx = 0, dy = 0;
        if (!map_model_offset((unsigned short) r, &dx, &dy)) continue;
        if (first) { *x0 = *x1 = dx; *y0 = *y1 = dy; first = 0; continue; }
        if (dx < *x0) *x0 = dx;
        if (dx > *x1) *x1 = dx;
        if (dy < *y0) *y0 = dy;
        if (dy > *y1) *y1 = dy;
    }
}

/*----------------------
 | draw_once
 | Description: Paints one whole frame of the map at a scroll offset: the
 |   ground, every placed room the viewport reaches, the links between them, and
 |   the current room's name along the bottom. Clears the text layer first,
 |   because the caller is the Options menu, which redraws its title and every
 |   row each frame and would otherwise leave them lit over a map whose box
 |   border dash_map_begin has just blanked.
 |
 |   Every pair of gathered rooms the story links is joined, at whatever
 |   distance -- paint_link's header has why that is not the rule it used to be.
 |   Links are routed into g_edge first and the whole layer painted from it
 |   afterwards, so a cell two links cross knows about both and draws as a
 |   crossing; painting each link as it was routed would have left whichever came
 |   last. Marks go down after that again, so a mark always wins its own cell.
 |
 |   The walk is gather() and then a pairwise pass over what it returned, both
 |   bounded by the viewport rather than by the placed set, which is what keeps
 |   this inside one frame; it used to nest the scan inside the pairwise loop and
 |   spent about a dozen frames between one menu_sync and the next, long enough
 |   to starve the looping PCM hand-off.
 |
 |   Called on open and again on each scroll step, and on nothing else.
 |   map_view_show holds the NBG2 claim between those with dash_map_hold, which
 |   re-touches the layer without repainting it; the text this writes needs no
 |   such upkeep, since text_map has no per-frame expiry.
 | Author: suinevere
 | Dependencies: map_model.h, dash_map.h, text_map.h, room_model.h, menu.h,
 |   gather, paint_link
 | Globals: g_ids, g_dxs, g_dys
 | Params: sx, sy -- the scroll offset in rooms, zero with the player centred
 | Returns: N/A
 ----------------------*/
static void draw_once(int sx, int sy) {
    int n, i, j;

    menu_clear();
    dash_map_begin();

    for (i = 0; i < MAP_ROOMS_H * MAP_CELLS; i++) {
        int c;
        for (c = 0; c < MAP_ROOMS_W * MAP_CELLS; c++)
            dash_map_paint(c, MAP_TOP + i, DT_GROUND);
    }

    n = gather(sx, sy);

    for (i = 0; i < MAP_ROOMS_H * MAP_CELLS; i++) {
        int c;
        for (c = 0; c < MAP_ROOMS_W * MAP_CELLS; c++) g_edge[i][c] = 0;
    }

    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            int kind = map_model_link(g_ids[i], g_ids[j]);
            if (kind == MAP_LINK_NONE) continue;
            paint_link((MAP_CX + g_dxs[i]) * MAP_CELLS,
                       MAP_TOP + (MAP_CY + g_dys[i]) * MAP_CELLS,
                       (MAP_CX + g_dxs[j]) * MAP_CELLS,
                       MAP_TOP + (MAP_CY + g_dys[j]) * MAP_CELLS,
                       n, kind);
        }
    }

    for (i = 0; i < MAP_ROOMS_H * MAP_CELLS; i++) {
        int c;
        for (c = 0; c < MAP_ROOMS_W * MAP_CELLS; c++) {
            int e = g_edge[i][c], mask = e & 15;
            if (mask == 0) continue;
            if (cell_is_mark(c, i, n)) continue;
            if ((e & MAP_EDGE_STAIR) && mask == (DT_EDGE_N | DT_EDGE_S))
                dash_map_paint(c, i, DT_LINK_STAIR);
            else
                dash_map_paint(c, i, (unsigned char) (DT_LINK0 + mask));
        }
    }

    for (i = 0; i < n; i++) {
        int cx = (MAP_CX + g_dxs[i]) * MAP_CELLS;
        int cy = MAP_TOP + (MAP_CY + g_dys[i]) * MAP_CELLS;
        dash_map_paint(cx, cy,
                       g_ids[i] == map_model_current() ? DT_ROOM_HERE : DT_ROOM);
    }

    {
        char nm[40];
        text_print_str(2, 1, "MAP");
        if (room_model_object_name(map_model_current(), nm, (int) sizeof nm))
            text_print_str(2, 26, nm);
        text_print_str(2, 27, "D-pad: scroll   A/B/C: back");
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
 |   The D-pad scrolls the map a room at a time, through the same
 |   pad_repeat_update/pad_fired pair every other page uses for held movement, so
 |   the delay before it runs on matches the rest of the interface. The offset
 |   starts at zero on every open and is never carried across one, so the player
 |   is centred each time the screen appears however far it was scrolled when it
 |   last closed -- there is no scroll position to restore because none is kept.
 |   extent() clamps it to the rooms actually placed, so the view cannot be
 |   walked off into empty ground.
 |
 |   draw_once runs on open and on each step that changes the offset, and not on
 |   frames where nothing moved: dash_map_hold keeps the claim alive on those
 |   without repeating the room and link walk. dash_tint rewrites the sixteen
 |   CRAM entries every NBG2 tile draws from, so the tan is captured on the way
 |   in and put back on the way out; without that the gamepad strip and every
 |   menu box wear it for the rest of the session.
 |
 |   The wallpaper is hidden for the map's duration and restored by asking
 |   room_art for the room again, which is free when NBG0 still holds that
 |   frame. It used to be restored by re-showing title_bg_loaded_file() by name,
 |   which was wrong twice over: for a CGL frame that name is an area stem and
 |   no file, so the picture never came back, and on a game with no art at all
 |   the name was still the boot logo's -- so closing the map put the SUINEVERE
 |   logo up behind the game and left it there.
 | Author: suinevere
 | Dependencies: draw_once, extent, dash_map.h, dash_view.h, menu.h, input.h,
 |   saturn_keyboard.h, soft_reset.h, console_view.h, title.h, room_art.h,
 |   display.h, app_state.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void map_view_show(void) {
    MenuBacking backing;
    int sx = 0, sy = 0;
    unsigned short tint = dash_tint_current();
    const bool had_art = (g_display.palette == DISP_PAL_DYNAMIC)
                         && room_art_available() != 0;

    title_bg_hide();
    dash_tint(MAP_GROUND_555);
#if MAP_REVEAL_ALL
    map_model_reveal_atlas();
#endif
    draw_once(sx, sy);
    menu_sync();
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
            int nx = sx, ny = sy, x0, x1, y0, y1;
            if (pad_fired(Button::Left))  nx--;
            if (pad_fired(Button::Right)) nx++;
            if (pad_fired(Button::Up))    ny--;
            if (pad_fired(Button::Down))  ny++;
            extent(&x0, &x1, &y0, &y1);
            if (nx < x0) nx = x0;
            if (nx > x1) nx = x1;
            if (ny < y0) ny = y0;
            if (ny > y1) ny = y1;
            if (nx != sx || ny != sy) {
                sx = nx;
                sy = ny;
                draw_once(sx, sy);
            }
        }

        dash_map_hold();
        menu_sync();
    }

    text_clear_line(1);
    text_clear_line(26);
    text_clear_line(27);
    text_flush();
    dash_tint(tint);
    if (had_art) room_art_reshow();
}
