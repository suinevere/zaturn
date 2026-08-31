/*----------------------
 | map_view.cxx
 | Description: Implements map_view.h.
 | Author: suinevere
 | Dependencies: map_model.h, dash_map.h, room_model.h, text_map.h, dash_view.h,
 |   title.h, input.h, saturn_keyboard.h, soft_reset.h, console_view.h, menu.h
 ----------------------*/
#include <srl.hpp>
#include "map_model.h"
#include "dash_map.h"
#include "room_model.h"
#include "text_map.h"
#include "dash_view.h"
#include "title.h"
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
 | paint_link
 | Description: Paints the cells joining a room at (cx, cy) to another one or
 |   two grid steps away, with ddx and ddy already normalised so that room is
 |   the left-hand or upper end of the pair. A room is four cells from the next,
 |   so a colinear link fills every cell of the gap and reads as one groove
 |   running mark to mark rather than as a dash floating in it. A diagonal pair
 |   -- which Zork's forest is full of, and which the collision search also
 |   produces -- gets a single cell at the midpoint instead: the tile set has no
 |   diagonal glyph, and the netbin carries dash_tiles.c under a hard size gate,
 |   so adding one is not free.
 |
 |   Two steps is a case, not an edge case: UP and DOWN step two cells so a
 |   staircase cannot land on the cell north or south already wants, so every
 |   stair link in the map is this length. The caller is what guarantees a
 |   two-step run is colinear and that the cell it passes through is empty; a
 |   longer run through an occupied cell would read as joining whatever sits in
 |   it.
 | Author: suinevere
 | Dependencies: dash_map.h
 | Globals: N/A
 | Params: cx, cy -- the source room's cell; ddx -- 0, 1 or 2; ddy -- -2 to 2,
 |   never zero at the same time as ddx, and zero whenever ddx is 2; kind --
 |   MAP_LINK_FLAT or MAP_LINK_VERT
 | Returns: N/A
 ----------------------*/
static void paint_link(int cx, int cy, int ddx, int ddy, int kind)
{
    int k;
    if (ddy == 0) {
        unsigned char t = (unsigned char)
            (kind == MAP_LINK_VERT ? DT_LINK_STAIR : DT_LINK_H);
        for (k = 1; k < ddx * MAP_CELLS; k++) dash_map_paint(cx + k, cy, t);
        return;
    }
    if (ddx == 0) {
        unsigned char t = (unsigned char)
            (kind == MAP_LINK_VERT ? DT_LINK_STAIR : DT_LINK_V);
        for (k = 1; k < ddy * MAP_CELLS; k++) dash_map_paint(cx, cy + k, t);
        return;
    }
    dash_map_paint(cx + MAP_CELLS / 2, cy + ddy * (MAP_CELLS / 2),
                   (unsigned char)
                   (kind == MAP_LINK_VERT ? DT_LINK_STAIR : DT_LINK_H));
}

/*----------------------
 | occupied
 | Description: Whether one of the gathered rooms sits at an offset from the
 |   player. Used to decide whether a two-step link may run through the cell
 |   between its ends.
 |
 |   Asking the gathered set rather than the model is complete for that
 |   question: gather() takes every placed room whose offset falls inside the
 |   viewport, and the midpoint of two cells that are both inside it is inside
 |   it too.
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
static int gather(void)
{
    int r, n = 0;
    for (r = 1; r < MAP_ROOM_MAX && n < MAP_VIS_MAX; r++) {
        int dx = 0, dy = 0;
        if (!map_model_offset((unsigned short) r, &dx, &dy)) continue;
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
 | draw_once
 | Description: Paints one frame of the map: the ground, every placed room
 |   within the viewport, the links between them, and the current room's name
 |   along the bottom. Clears the text layer first, because the caller is the
 |   Options menu, which redraws its title and every row each frame and would
 |   otherwise leave them lit over a map whose box border dash_map_begin has
 |   just blanked. Called exactly once, when map_view_show opens the screen;
 |   the walk is gather() and then a pairwise pass over what it returned, both
 |   bounded by the viewport rather than by the placed set. Pairs one grid step
 |   apart are joined in any of the eight directions; pairs two apart only when
 |   they are colinear and the cell between them holds no room, since there is
 |   no line renderer here and a mark laid across an occupied cell would read as
 |   joining whatever sits in it. Two steps is what a staircase now is, UP and
 |   DOWN having been given a step of two so they cannot land where north and
 |   south already want to be, so this is the ordinary case for a stair rather
 |   than a concession to the collision search. Anything further apart is still
 |   left unlinked. map_view_show keeps
 |   the NBG2 claim alive across the frames after this one with dash_map_hold,
 |   which re-touches the layer without repainting it; the text labels this
 |   writes need no such upkeep, since text_map has no per-frame expiry and
 |   what it prints persists until something overwrites it.
 | Author: suinevere
 | Dependencies: map_model.h, dash_map.h, text_map.h, room_model.h, menu.h,
 |   gather, paint_link, occupied
 | Globals: g_ids, g_dxs, g_dys
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void draw_once(void) {
    int n, i, j;

    menu_clear();
    dash_map_begin();

    for (i = 0; i < MAP_ROOMS_H * MAP_CELLS; i++) {
        int c;
        for (c = 0; c < MAP_ROOMS_W * MAP_CELLS; c++)
            dash_map_paint(c, MAP_TOP + i, DT_GROUND);
    }

    n = gather();

    for (i = 0; i < n; i++) {
        int cx = (MAP_CX + g_dxs[i]) * MAP_CELLS;
        int cy = MAP_TOP + (MAP_CY + g_dys[i]) * MAP_CELLS;
        dash_map_paint(cx, cy,
                       g_ids[i] == map_model_current() ? DT_ROOM_HERE : DT_ROOM);
    }

    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            int ddx = g_dxs[j] - g_dxs[i];
            int ddy = g_dys[j] - g_dys[i];
            int src = i, ady, kind;
            if (ddx < 0 || (ddx == 0 && ddy < 0)) {
                src = j; ddx = -ddx; ddy = -ddy;
            }
            ady = ddy < 0 ? -ddy : ddy;
            if (ddx == 0 && ady == 0) continue;
            if (ddx > 2 || ady > 2) continue;
            if (ddx > 1 && ady != 0) continue;
            if (ady > 1 && ddx != 0) continue;
            if ((ddx > 1 || ady > 1)
                && occupied(n, g_dxs[src] + ddx / 2, g_dys[src] + ddy / 2))
                continue;
            kind = map_model_link(g_ids[i], g_ids[j]);
            if (kind == MAP_LINK_NONE) continue;
            paint_link((MAP_CX + g_dxs[src]) * MAP_CELLS,
                       MAP_TOP + (MAP_CY + g_dys[src]) * MAP_CELLS,
                       ddx, ddy, kind);
        }
    }

    {
        char nm[40];
        text_print_str(2, 1, "MAP");
        if (room_model_object_name(map_model_current(), nm, (int) sizeof nm))
            text_print_str(2, 26, nm);
        text_print_str(2, 27, "Any button or key: back");
        text_flush();
    }
}

/*----------------------
 | map_view_show
 | Description: See map_view.h. Holds itself the way every full-screen page in
 |   menu_pages.cxx does (credits_page is the closest analog): a loop that
 |   polls input, checks for an exit, and otherwise advances the frame -- not
 |   a single draw followed by menu_wait's generic block, which does not
 |   re-touch dash_map's NBG2 claim and so would lose it a frame after
 |   draw_once painted it. draw_once runs exactly once, before the loop;
 |   dash_map_hold keeps the claim alive on every frame after that without
 |   repeating draw_once's room/link walk. dash_tint rewrites the sixteen CRAM
 |   entries every NBG2 tile draws from, so the tan is captured on the way in
 |   and put back on the way out; without that the gamepad strip and every menu
 |   box wear it for the rest of the session.
 | Author: suinevere
 | Dependencies: draw_once, dash_map.h, dash_view.h, menu.h, input.h,
 |   saturn_keyboard.h, soft_reset.h, console_view.h, title.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void map_view_show(void) {
    MenuBacking backing;
    char was[64];
    int i = 0;
    unsigned short tint = dash_tint_current();
    const char *cur = title_bg_loaded_file();
    while (cur[i] && i < (int) sizeof was - 1) { was[i] = cur[i]; i++; }
    was[i] = 0;

    title_bg_hide();
    dash_tint(MAP_GROUND_555);
    draw_once();
    menu_sync();
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        bool back = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
                    g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START) ||
                    ke.kind != SATURN_KEY_NONE;
        if (back) break;

        dash_map_hold();
        menu_sync();
    }

    text_clear_line(1);
    text_clear_line(26);
    text_clear_line(27);
    text_flush();
    dash_tint(tint);
    if (was[0]) title_bg_show(was);
}
