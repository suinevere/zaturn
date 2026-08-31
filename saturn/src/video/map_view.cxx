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
 | Description: Paints the cells joining a room at (cx, cy) to a neighbour one
 |   grid step away, with ddx and ddy already normalised so that room is the
 |   left-hand or upper end of the pair. A room is four cells from the next, so
 |   an orthogonal link fills all three cells of the gap and reads as one
 |   groove running mark to mark rather than as a dash floating in it. A
 |   diagonal pair -- which Zork's forest is full of, and which the collision
 |   spiral also produces -- gets a single cell at the midpoint instead: the
 |   tile set has no diagonal glyph, and the netbin carries dash_tiles.c under
 |   a hard size gate, so adding one is not free.
 | Author: suinevere
 | Dependencies: dash_map.h
 | Globals: N/A
 | Params: cx, cy -- the source room's cell; ddx -- 0 or 1; ddy -- -1, 0 or 1,
 |   never zero at the same time as ddx; kind -- MAP_LINK_FLAT or MAP_LINK_VERT
 | Returns: N/A
 ----------------------*/
static void paint_link(int cx, int cy, int ddx, int ddy, int kind)
{
    int k;
    if (ddy == 0) {
        unsigned char t = (unsigned char)
            (kind == MAP_LINK_VERT ? DT_LINK_STAIR : DT_LINK_H);
        for (k = 1; k < MAP_CELLS; k++) dash_map_paint(cx + k, cy, t);
        return;
    }
    if (ddx == 0) {
        unsigned char t = (unsigned char)
            (kind == MAP_LINK_VERT ? DT_LINK_STAIR : DT_LINK_V);
        for (k = 1; k < MAP_CELLS; k++) dash_map_paint(cx, cy + k, t);
        return;
    }
    dash_map_paint(cx + MAP_CELLS / 2, cy + ddy * (MAP_CELLS / 2),
                   (unsigned char)
                   (kind == MAP_LINK_VERT ? DT_LINK_STAIR : DT_LINK_H));
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
 |   bounded by the viewport rather than by the placed set. Only pairs one grid
 |   step apart are joined: a pair the collision spiral pushed further than that
 |   is left unlinked, because there is no line renderer here and a mark laid
 |   between two rooms several cells apart would read as joining whatever else
 |   sits between them. map_view_show keeps
 |   the NBG2 claim alive across the frames after this one with dash_map_hold,
 |   which re-touches the layer without repainting it; the text labels this
 |   writes need no such upkeep, since text_map has no per-frame expiry and
 |   what it prints persists until something overwrites it.
 | Author: suinevere
 | Dependencies: map_model.h, dash_map.h, text_map.h, room_model.h, menu.h,
 |   gather, paint_link
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
            int src = i, kind;
            if (ddx < 0 || (ddx == 0 && ddy < 0)) {
                src = j; ddx = -ddx; ddy = -ddy;
            }
            if (ddx > 1 || ddy > 1 || ddy < -1) continue;
            if (ddx == 0 && ddy == 0) continue;
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
