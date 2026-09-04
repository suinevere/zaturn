/*----------------------
 | map_model.c
 | Description: Implements map_model.h. See docs/ZORK1_MAP_RECON.md for why the
 |   layout is a graph walk rather than a coordinate table: the original Saturn
 |   release has no per-room coordinates and recomputes its map on every open,
 |   which is what made it place a room differently depending on the route
 |   taken to it.
 | Author: suinevere
 ----------------------*/
#include "map_model.h"
#include "map_atlas.h"
#include "map_marks.h"

/*----------------------
 | DX / DY
 | Description: The grid step for each RM_* direction, in room units. The eight
 |   compass directions take the eight neighbouring cells. UP, DOWN, IN and OUT
 |   take a step of two, on the vertical axis for UP/DOWN and the horizontal for
 |   IN/OUT, so that none of the four can land on a cell a compass direction
 |   also wants and no two of them can land on each other.
 |
 |   That is the correction, and it is worth naming what it replaces: these four
 |   used to step one cell, which made UP identical to north and DOWN and IN both
 |   identical to south. Climbing a staircase placed the room above you exactly
 |   where walking north would have, and if the room also had a north exit the
 |   two contested one cell and the placement search flung one of them away. The
 |   header claimed a level change the table did not make.
 |
 |   The cost is that a stair link spans two grid steps rather than one, so the
 |   view has to be willing to draw a link that long. map_view's draw_once is,
 |   for colinear pairs whose intervening cell is empty.
 | Author: suinevere
 ----------------------*/
static const signed char DX[RM_DIR_N] = { 0, 1,-1, 0, 1,-1, 1,-1, 0, 0, 2,-2 };
static const signed char DY[RM_DIR_N] = {-1, 0, 0, 1,-1,-1, 1, 1,-2, 2, 0, 0 };

/*----------------------
 | OPP
 | Description: The direction facing back down each RM_* exit, used to read a
 |   move backwards out of the room arrived in when the room departed from is
 |   not on hand to read it forwards.
 | Author: suinevere
 ----------------------*/
static const signed char OPP[RM_DIR_N] = {
    RM_S, RM_W, RM_E, RM_N, RM_SW, RM_SE, RM_NW, RM_NE,
    RM_DOWN, RM_UP, RM_OUT, RM_IN
};

/*----------------------
 | g_vis / g_x / g_y / g_cur / g_have_cur / g_prev / g_have_prev
 | Description: The placed set and its coordinates, the current room, and the
 |   previous prompt's snapshot, which is the only thing that can say which way
 |   the player just moved.
 | Author: suinevere
 ----------------------*/
static unsigned char  g_vis[MAP_ROOM_MAX];
static short          g_x[MAP_ROOM_MAX];
static short          g_y[MAP_ROOM_MAX];
static unsigned short g_cur;
static int            g_have_cur;
static RoomModel      g_prev;
static int            g_have_prev;

/*----------------------
 | g_revealed
 | Description: Which of the placed rooms were placed by the reveal rather than
 |   walked into. g_vis alone cannot say -- it is one bit meaning "on the map" --
 |   and the difference is what decides whether a room survives leaving Easy and
 |   whether it goes into a save.
 |
 |   Kept alongside g_vis rather than as a second value inside it so that every
 |   existing test of g_vis stays a plain truth test. A revealed room is placed
 |   in every way that matters to the view; the flag is only ever consulted when
 |   the reveal is taken back or the map is written out.
 | Author: suinevere
 ----------------------*/
static unsigned char  g_revealed[MAP_ROOM_MAX];

/*----------------------
 | g_frame_set / g_frame_x / g_frame_y
 | Description: The translation from the atlas's coordinates into this session's,
 |   and whether it has been established yet.
 |
 |   The atlas numbers its cells absolutely and the walk numbers them from
 |   wherever the first room happened to land, so the two have to be reconciled
 |   or an authored room would sit an arbitrary distance from a walked one. The
 |   reconciliation is deferred to the first authored room actually entered: if
 |   nothing is placed yet the atlas frame is simply adopted, and if the walk has
 |   already placed rooms the offset is chosen so that first authored room lands
 |   where the walk would have put it. Everything authored afterwards is placed
 |   through the same offset, so the atlas's internal geometry survives whole
 |   however the player entered it.
 | Author: suinevere
 ----------------------*/
static int   g_frame_set;
static short g_frame_x;
static short g_frame_y;

/*----------------------
 | g_dest / g_kind
 | Description: Each placed room's destinations and whether each was a flat or
 |   a vertical exit, kept because a link is a property of the story rather
 |   than of the route walked and the view asks about pairs the player may
 |   never have travelled in that direction.
 | Author: suinevere
 ----------------------*/
static unsigned short g_dest[MAP_ROOM_MAX][RM_DIR_N];
static unsigned char  g_kind[MAP_ROOM_MAX][RM_DIR_N];

/*----------------------
 | g_cond
 | Description: Which of each placed room's exits are conditional, one bit per
 |   direction. Kept beside g_kind rather than folded into it because g_kind is
 |   the value map_model_link returns and widening it would change that
 |   function's contract. A short and not a char because RM_DIR_N is twelve.
 | Author: suinevere
 ----------------------*/
static unsigned short g_cond[MAP_ROOM_MAX];

/*----------------------
 | g_bag
 | Description: Which of each placed room's exits carry a baggage-limit mark,
 |   one bit per direction, the same shape as g_cond and for the same reason:
 |   this is a fact map_marks supplies at record_exits time and nothing in the
 |   story's own exit graph can rederive it, so it has to be held rather than
 |   recomputed on every map_model_exits call.
 | Author: suinevere
 ----------------------*/
static unsigned short g_bag[MAP_ROOM_MAX];

/*----------------------
 | map_model_reset
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_revealed, g_cond, g_bag, g_cur, g_have_cur, g_have_prev
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void map_model_reset(void) {
    int i;
    for (i = 0; i < MAP_ROOM_MAX; i++) { g_vis[i] = 0; g_revealed[i] = 0; g_x[i] = 0; g_y[i] = 0; g_cond[i] = 0; g_bag[i] = 0; { int d; for (d = 0; d < RM_DIR_N; d++) { g_dest[i][d] = 0; g_kind[i][d] = 0; } } }
    g_cur = 0;
    g_have_cur = 0;
    g_have_prev = 0;
    g_frame_set = 0;
    g_frame_x = 0;
    g_frame_y = 0;
}

/*----------------------
 | in_range
 | Description: Whether an object number is one the position table covers.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: room -- object number
 | Returns: 1 when addressable, 0 otherwise
 ----------------------*/
static int in_range(unsigned short room) {
    return room != 0 && room < MAP_ROOM_MAX;
}

/*----------------------
 | dir_from_prev
 | Description: Which direction of the previous room leads to `room`, taking the
 |   first match so the result is stable when a room is reachable two ways.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_prev, g_have_prev
 | Params: room -- the room now occupied
 | Returns: an RM_* index, or MAP_DIR_UNKNOWN
 ----------------------*/
static int dir_from_prev(unsigned short room) {
    int d;
    if (!g_have_prev) return MAP_DIR_UNKNOWN;
    for (d = 0; d < RM_DIR_N; d++)
        if (g_prev.exits[d] != RM_EXIT_NONE && g_prev.dest[d] == room) return d;
    return MAP_DIR_UNKNOWN;
}

/*----------------------
 | dir_to_cur
 | Description: Which direction the player must have travelled to arrive in the
 |   room `m` describes, read backwards: if that room has an exit leading to the
 |   room already current, the move that got here was the opposite of it. Takes
 |   the first match, so it is stable when two exits lead back.
 |
 |   This is the fallback for the case dir_from_prev cannot serve. A restore
 |   leaves no previous snapshot to read a move forwards out of, so the first
 |   move after one used to infer nothing and place its destination due south
 |   whatever way the player actually went -- and since a placed room never
 |   moves, that error was permanent. Zork's exits are reciprocal often enough
 |   that reading the arrival backwards recovers the real direction in most of
 |   those cases. Where it does not, the due-south fallback still stands.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cur, g_have_cur
 | Params: m -- the snapshot of the room just arrived in
 | Returns: an RM_* index, or MAP_DIR_UNKNOWN
 ----------------------*/
static int dir_to_cur(const RoomModel *m) {
    int d;
    if (!g_have_cur) return MAP_DIR_UNKNOWN;
    for (d = 0; d < RM_DIR_N; d++)
        if (m->exits[d] != RM_EXIT_NONE && m->dest[d] == g_cur) return OPP[d];
    return MAP_DIR_UNKNOWN;
}

/*----------------------
 | cell_taken
 | Description: Whether a placed room other than `self` already holds a cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_x, g_y
 | Params: x, y -- the cell; self -- the room allowed to hold it
 | Returns: 1 when occupied, 0 otherwise
 ----------------------*/
static int cell_taken(int x, int y, unsigned short self) {
    int i, sp = 0, has_sp = map_atlas_page(self, &sp);
    for (i = 0; i < MAP_ROOM_MAX; i++) {
        int ip = 0;
        if (!g_vis[i] || i == (int) self) continue;
        if (g_x[i] != x || g_y[i] != y) continue;
        /* Two floors may stand on the same cell. Only one is ever drawn --
           gather() and extent() both filter on the page -- so a cell is owed to
           be unique within a floor and not across the whole table, and the
           atlas slides the floors over each other on purpose so that a
           staircase comes out at the coordinate it went in at.

           Asked of the table and not of map_model_page, which for a room the
           atlas does not cover runs a breadth-first walk: this is called once
           per ring cell of a contested placement, and a walk in that loop would
           be paid hundreds of times over. A room the atlas does not place keeps
           the whole-table rule, which is the conservative half and the one it
           had before. */
        if (has_sp && map_atlas_page((unsigned short) i, &ip) && ip != sp)
            continue;
        return 1;
    }
    return 0;
}

/*----------------------
 | MAP_SPIRAL_MAX
 | Description: How far out a contested placement will search before giving up
 |   and stacking. The ring at radius r holds 8r cells, so radius 8 sweeps a
 |   17x17 block -- 289 cells against at most MAP_ROOM_MAX-1 = 255 rooms that
 |   could be occupying them. The search therefore cannot fail, which is the
 |   reason for this number rather than a smaller one.
 | Author: suinevere
 ----------------------*/
#define MAP_SPIRAL_MAX 8

/*----------------------
 | ring_cell
 | Description: The `i`th cell of the square ring at Chebyshev radius `r` about
 |   the origin, walking clockwise from due north. 8r cells to a ring, each
 |   visited once, so a caller stepping i from 0 to 8r-1 sees the whole ring in
 |   a fixed order and never the same cell twice.
 |
 |   Rings, not rays. This used to probe only the eight cells (SPX[k]*r,
 |   SPY[k]*r), which at r=2 looked at (0,-2) and (2,-2) and never at (1,-2):
 |   a contested room could be flung eight cells clear while a free cell sat one
 |   step away off the rays, and a room that far out is past anything the view
 |   will draw a link to. A full ring finds the genuinely nearest free cell, so
 |   the overwhelming majority of contests now settle at radius one, where the
 |   link survives.
 |
 |   The rotation by r is what puts due north at index 0. The four sides are
 |   walked from the top-left corner, and (0,-r) sits r cells along the first of
 |   them, so subtracting that offset makes index 0 land on it. Radius one then
 |   probes N, NE, E, SE, S, SW, W, NW in that order, which keeps the northward
 |   bias the previous order had and the tests pin.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: r -- radius, at least 1; i -- position in [0, 8r); x, y -- receive
 |   the cell, relative to the ring's centre
 | Returns: N/A
 ----------------------*/
static void ring_cell(int r, int i, int *x, int *y) {
    int side = 2 * r, t;
    i = (i + r) % (8 * r);
    t = i % side;
    switch (i / side) {
    case 0:  *x = t - r; *y = -r;    break;
    case 1:  *x = r;     *y = t - r; break;
    case 2:  *x = r - t; *y = r;     break;
    default: *x = -r;    *y = r - t; break;
    }
}

/*----------------------
 | place
 | Description: Puts a room at the target cell, or at the nearest free cell in
 |   the fixed search order when the target is taken, and marks it placed. The
 |   first room to hold a cell keeps it.
 | Author: suinevere
 | Dependencies: cell_taken, ring_cell
 | Globals: g_vis, g_x, g_y
 | Params: room -- object number; tx, ty -- the wanted cell
 | Returns: N/A
 ----------------------*/
static void place(unsigned short room, int tx, int ty) {
    int r, i, x = tx, y = ty;
    if (cell_taken(tx, ty, room)) {
        int done = 0;
        for (r = 1; r <= MAP_SPIRAL_MAX && !done; r++) {
            for (i = 0; i < 8 * r && !done; i++) {
                int cx, cy;
                ring_cell(r, i, &cx, &cy);
                cx += tx;
                cy += ty;
                if (!cell_taken(cx, cy, room)) { x = cx; y = cy; done = 1; }
            }
        }
    }
    g_x[room] = (short) x;
    g_y[room] = (short) y;
    g_vis[room] = 1;
}

/*----------------------
 | atlas_target
 | Description: Where an authored room belongs in this session's coordinates, or
 |   nothing when the atlas does not cover it. Establishes the frame on the first
 |   authored room, anchoring it to `wx`, `wy` -- what the walk would have chosen
 |   -- when the walk has already placed something, and adopting the atlas's own
 |   numbering when it has not.
 | Author: suinevere
 | Dependencies: map_atlas.h
 | Globals: g_frame_set, g_frame_x, g_frame_y
 | Params: room -- object number; wx, wy -- the walk's target, and whether it
 |   means anything is `anchored`; tx, ty -- receive the cell
 | Returns: 1 when the atlas covers the room, 0 otherwise
 ----------------------*/
static int atlas_target(unsigned short room, int wx, int wy, int anchored,
                        int *tx, int *ty) {
    int ax, ay;
    if (!map_atlas_pos(room, &ax, &ay)) return 0;
    if (!g_frame_set) {
        g_frame_x = (short) (anchored ? wx - ax : 0);
        g_frame_y = (short) (anchored ? wy - ay : 0);
        g_frame_set = 1;
    }
    *tx = ax + g_frame_x;
    *ty = ay + g_frame_y;
    return 1;
}

/*----------------------
 | record_exits
 | Description: Copies one snapshot's exits and destinations into the placed
 |   room's own row, which is what map_model_link answers from, then applies
 |   any scanned mark for each direction on top. This is the single chokepoint
 |   the live path and the restore rebind both pass through, which is why the
 |   marks are applied here and nowhere else: has_reverse, map_model_link and
 |   map_model_exits all read this one row, so applying a mark anywhere but
 |   here would leave some of them reading the story's raw, uncorrected
 |   answer.
 |
 |   A mark is applied only where the story left the exit conditional. That is
 |   the whole branch's rule -- the drawing may resolve a passage only where
 |   every exit on it is RM_EXIT_MAYBE -- and it was until now enforced only in
 |   the generator. A retraction clears the kind, the destination and the
 |   conditional bit, so a table row aimed at an exit the story states outright
 |   would delete geography the game asserts, on the strength of a scanned
 |   line. Every shipped row targets a conditional exit and the guard costs
 |   nothing today; it is what stops a future misread from being obeyed.
 |
 |   A retraction and a supplied destination are mutually exclusive per row in
 |   the generated table, but the two directions of one conditional passage
 |   are not: supplying Studio's missing destination without retracting the
 |   Kitchen's backward exit would let has_reverse start seeing a way back
 |   that the game does not permit, silently deleting the one-way arrow the
 |   correction exists to draw. Both sides of such a pair are always present
 |   in the same bound table and are applied on whichever visit reaches each
 |   room, so the two edits land together regardless of walk order.
 | Author: suinevere
 | Dependencies: map_marks_for
 | Globals: g_dest, g_kind, g_cond, g_bag
 | Params: room -- an in-range object number; m -- the snapshot to read
 | Returns: N/A
 ----------------------*/
static void record_exits(unsigned short room, const RoomModel *m) {
    int d;
    g_cond[room] = 0;
    g_bag[room] = 0;
    for (d = 0; d < RM_DIR_N; d++) {
        unsigned char mdest = 0, mflags = 0;
        int marked = map_marks_for(room, d, &mdest, &mflags);
        g_dest[room][d] = m->dest[d];
        g_kind[room][d] = (unsigned char)
            (m->exits[d] == RM_EXIT_NONE ? MAP_LINK_NONE
             : (MAP_DIR_VERT(d) ? MAP_LINK_VERT : MAP_LINK_FLAT));
        if (m->exits[d] == RM_EXIT_MAYBE)
            g_cond[room] |= (unsigned short) (1u << d);
        if (!marked || m->exits[d] != RM_EXIT_MAYBE) continue;
        if (mflags & MARK_RETRACT) {
            g_kind[room][d] = MAP_LINK_NONE;
            g_dest[room][d] = 0;
            g_cond[room] &= (unsigned short) ~(1u << d);
            continue;
        }
        if (mdest != 0) g_dest[room][d] = mdest;
        if (mflags & MARK_BAGGAGE)
            g_bag[room] |= (unsigned short) (1u << d);
    }
}

/*----------------------
 | map_model_enter
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: dir_from_prev, dir_to_cur, atlas_target, place, record_exits
 | Globals: g_vis, g_revealed, g_cur, g_have_cur, g_prev, g_have_prev
 | Params: m -- the snapshot, never null
 | Returns: N/A
 ----------------------*/
void map_model_enter(const RoomModel *m) {
    unsigned short room = m->room;
    if (!in_range(room)) return;

    if (!g_vis[room]) {
        int d = dir_from_prev(room);
        int tx = 0, ty = 0, anchored = 0, ax, ay;
        if (d == MAP_DIR_UNKNOWN) d = dir_to_cur(m);
        if (g_have_cur && g_vis[g_cur]) {
            tx = g_x[g_cur];
            ty = g_y[g_cur];
            if (d != MAP_DIR_UNKNOWN) { tx += DX[d]; ty += DY[d]; }
            else                      { ty += 1; }
            anchored = 1;
        }
        if (atlas_target(room, tx, ty, anchored, &ax, &ay)) { tx = ax; ty = ay; }
        place(room, tx, ty);
    }

    g_revealed[room] = 0;
    record_exits(room, m);

    g_cur = room;
    g_have_cur = 1;
    g_prev = *m;
    g_have_prev = 1;
}

/*----------------------
 | map_model_visited
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: in_range
 | Globals: g_vis
 | Params: room -- object number
 | Returns: 1 when placed, 0 otherwise
 ----------------------*/
int map_model_visited(unsigned short room) {
    return in_range(room) && g_vis[room];
}

/*----------------------
 | map_model_pos
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_visited
 | Globals: g_x, g_y
 | Params: room -- object number; x, y -- receive the cell
 | Returns: 1 when placed, 0 otherwise
 ----------------------*/
int map_model_pos(unsigned short room, int *x, int *y) {
    if (!map_model_visited(room)) return 0;
    *x = g_x[room];
    *y = g_y[room];
    return 1;
}

/*----------------------
 | map_model_current
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cur, g_have_cur
 | Params: N/A
 | Returns: the current object number, or 0
 ----------------------*/
unsigned short map_model_current(void) {
    return g_have_cur ? g_cur : (unsigned short) 0;
}

/*----------------------
 | map_model_offset
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_visited
 | Globals: g_x, g_y, g_cur, g_have_cur
 | Params: room -- object number; dx, dy -- receive the offset
 | Returns: 1 on success, 0 otherwise
 ----------------------*/
int map_model_offset(unsigned short room, int *dx, int *dy) {
    if (!g_have_cur || !map_model_visited(g_cur)) return 0;
    if (!map_model_visited(room)) return 0;
    *dx = g_x[room] - g_x[g_cur];
    *dy = g_y[room] - g_y[g_cur];
    return 1;
}

/*----------------------
 | map_model_count
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis
 | Params: N/A
 | Returns: the number of placed rooms
 ----------------------*/
int map_model_count(void) {
    int i, n = 0;
    for (i = 0; i < MAP_ROOM_MAX; i++) if (g_vis[i]) n++;
    return n;
}

/*----------------------
 | map_model_room_at
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis
 | Params: index -- position in the placed set; room -- receives the object
 | Returns: 1 on success, 0 when index is out of range
 ----------------------*/
int map_model_room_at(int index, unsigned short *room) {
    int i, n = 0;
    if (index < 0) return 0;
    for (i = 0; i < MAP_ROOM_MAX; i++) {
        if (!g_vis[i]) continue;
        if (n == index) { *room = (unsigned short) i; return 1; }
        n++;
    }
    return 0;
}

/*----------------------
 | map_model_link
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_visited
 | Globals: g_dest, g_kind
 | Params: a, b -- object numbers
 | Returns: the link kind
 ----------------------*/
int map_model_link(unsigned short a, unsigned short b) {
    int d, best = MAP_LINK_NONE;
    if (!map_model_visited(a) || !map_model_visited(b)) return MAP_LINK_NONE;
    for (d = 0; d < RM_DIR_N; d++) {
        if (g_kind[a][d] != MAP_LINK_NONE && g_dest[a][d] == b) {
            if (g_kind[a][d] == MAP_LINK_FLAT) return MAP_LINK_FLAT;
            best = MAP_LINK_VERT;
        }
        if (g_kind[b][d] != MAP_LINK_NONE && g_dest[b][d] == a) {
            if (g_kind[b][d] == MAP_LINK_FLAT) return MAP_LINK_FLAT;
            best = MAP_LINK_VERT;
        }
    }
    return best;
}

/*----------------------
 | map_model_step
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: DX, DY
 | Params: dir -- an RM_* index; dx, dy -- receive the step
 | Returns: N/A
 ----------------------*/
void map_model_step(int dir, int *dx, int *dy) {
    if (dir < 0 || dir >= RM_DIR_N || dir >= RM_UP) { *dx = 0; *dy = -1; return; }
    *dx = DX[dir];
    *dy = DY[dir];
}

/*----------------------
 | has_reverse
 | Description: Whether b has any exit at all leading to a, in any state but
 |   RM_EXIT_NONE. g_kind is already RM_EXIT_NONE-filtered, so a blocked exit
 |   answers yes, which is what keeps a shut door off the one-way arrow.
 |
 |   No is not the same as "there is no way back" -- see reverse_unknown, which
 |   map_model_exits asks as well before it draws the arrowhead.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_dest, g_kind
 | Params: a, b -- object numbers
 | Returns: 1 when b leads back to a, 0 otherwise
 ----------------------*/
static int has_reverse(unsigned short a, unsigned short b) {
    int d;
    for (d = 0; d < RM_DIR_N; d++)
        if (g_kind[b][d] != MAP_LINK_NONE && g_dest[b][d] == a) return 1;
    return 0;
}

/*----------------------
 | reverse_unknown
 | Description: Whether b holds a passage whose far end the story never states,
 |   which makes "b has no way back to a" unprovable rather than false.
 |
 |   A v3 direction property three bytes long is a routine that decides at run
 |   time, and it carries no destination at all -- room_model records it as
 |   RM_EXIT_MAYBE with a destination of zero, which is this test. Every door
 |   the game opens with a verb rather than a step is one: Zork I's trap door,
 |   its grating and its chimney, and The Lurking Horror's Terminal Room, whose
 |   only two exits are both routines. Reading the absence of a decodable
 |   reverse as evidence of a one-way passage put an arrowhead on all of them --
 |   383 across the thirty-one stories, 246 of which rest on a room in this
 |   state.
 |
 |   A refusal message -- a two-byte property -- is not this. It says there is
 |   no passage that way, which is an assertion, so it must not veto; that is
 |   why the conditional bit is tested rather than the destination alone.
 |
 |   Same rule the baggage-limit scan already runs under: the drawing may
 |   resolve a passage only where the graph has actually asserted something. The
 |   previous pass reached it one room at a time, correcting the chimney by hand
 |   in Zork I's marks table; this is that correction as a rule.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_dest, g_kind, g_cond
 | Params: b -- object number
 | Returns: 1 when b has a passage of unstated destination, 0 otherwise
 ----------------------*/
static int reverse_unknown(unsigned short b) {
    int d;
    for (d = 0; d < RM_DIR_N; d++)
        if (g_kind[b][d] != MAP_LINK_NONE && g_dest[b][d] == 0 &&
            (g_cond[b] & (unsigned short) (1u << d)) != 0) return 1;
    return 0;
}

/*----------------------
 | map_model_exits
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_visited, has_reverse, reverse_unknown
 | Globals: g_dest, g_kind, g_cond, g_bag
 | Params: room -- object number; out -- receives the exits; max -- its length
 | Returns: how many exits were written
 ----------------------*/
int map_model_exits(unsigned short room, MapExit *out, int max) {
    int d, n = 0;
    if (room >= MAP_ROOM_MAX || !map_model_visited(room)) return 0;
    for (d = 0; d < RM_DIR_N && n < max; d++) {
        unsigned short dest = g_dest[room][d];
        if (g_kind[room][d] == MAP_LINK_NONE) continue;
        if (dest >= MAP_ROOM_MAX) continue;
        /* A staircase the story decides by running code names no destination,
           and it used to be dropped here with every other destination-less
           exit. It still says there is a way up, which is all a U or a D
           claims -- the glyph pass already draws one for a staircase whose far
           end is merely off the viewport, and this is the same drawing with
           less known about it. A FLAT exit with no destination is not the
           same: it has its direction already, no far end for a run to reach
           and no glyph of its own, so letting one through would give the link
           pass a stub to lay toward a room it cannot find. */
        if (dest == 0 && g_kind[room][d] != MAP_LINK_VERT) continue;
        out[n].dest  = dest;
        out[n].dir   = (unsigned char) d;
        out[n].kind  = g_kind[room][d];
        out[n].flags = 0;
        if (g_cond[room] & (unsigned short) (1u << d))
            out[n].flags |= MAP_EXIT_COND;
        if (g_bag[room] & (unsigned short) (1u << d))
            out[n].flags |= MAP_EXIT_BAGGAGE;
        if (dest == room)
            out[n].flags |= MAP_EXIT_SELF;
        else if (map_model_visited(dest) && !has_reverse(room, dest) &&
                 !reverse_unknown(dest))
            out[n].flags |= MAP_EXIT_ONEWAY;
        n++;
    }
    return n;
}

/*----------------------
 | map_model_rebind_exits
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: record_exits, room_model.h
 | Globals: g_vis, g_cur, g_have_cur
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void map_model_rebind_exits(void) {
    int i;
    for (i = 1; i < MAP_ROOM_MAX; i++) {
        const RoomModel *m;
        if (!g_vis[i]) continue;
        room_model_refresh_room((unsigned short) i);
        m = room_model_get();
        if (m->room != (unsigned short) i) continue;
        record_exits((unsigned short) i, m);
    }
    if (g_have_cur && in_range(g_cur)) room_model_refresh_room(g_cur);
}

/*----------------------
 | map_model_reveal_atlas
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_atlas.h, atlas_target, place, map_model_rebind_exits
 | Globals: g_vis, g_revealed, g_x, g_y
 | Params: N/A
 | Returns: how many rooms it placed that were not placed before
 ----------------------*/
int map_model_reveal_atlas(void) {
    int i, n = map_atlas_count(), added = 0;
    for (i = 0; i < n; i++) {
        unsigned short r = 0;
        int tx = 0, ty = 0;
        if (!map_atlas_room_at(i, &r)) continue;
        if (!in_range(r) || g_vis[r]) continue;
        if (!atlas_target(r, 0, 0, 0, &tx, &ty)) continue;
        place(r, tx, ty);
        g_revealed[r] = 1;
        added++;
    }
    if (added) map_model_rebind_exits();
    return added;
}

/*----------------------
 | map_model_pages
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_atlas.h
 | Globals: N/A
 | Params: N/A
 | Returns: the floor count, at least 1
 ----------------------*/
int map_model_pages(void) {
    int n = map_atlas_pages();
    return (n > 0) ? n : 1;
}

/*----------------------
 | g_seen / g_queue
 | Description: page_via_routes' visited set and its queue, at file scope
 |   because 256 rooms of each is more than the menu-depth stack wants to carry
 |   and the walk is never reentrant -- it runs inside one map_model_page call.
 | Author: suinevere
 ----------------------*/
static unsigned char  g_seen[MAP_ROOM_MAX];
static unsigned short g_queue[MAP_ROOM_MAX];

/*----------------------
 | page_via_routes
 | Description: The floor of the nearest room the atlas places, walking only
 |   exits that do not change floor.
 |
 |   A floor is now one vertical step of the story's routes inside one drawn
 |   sheet, so the storeys of a building stand on the building's own footprint
 |   and the floors' boxes overlap. That is what the old rule here could not
 |   survive: it gave an unplaced room the floor whose box it was nearest, which
 |   was unambiguous only while each floor had a band of rows to itself. Inside
 |   a shared footprint every candidate is distance zero and the first index
 |   won, so every room the atlas does not place -- twenty of The Lurking
 |   Horror's seventy-one -- would have piled onto one floor.
 |
 |   Up and down are the only exits excluded. In and out are not: walking into a
 |   building puts you on its ground floor. That is the same rule the generator
 |   partitions by, and deliberately not the same as record_exits' MAP_LINK_VERT,
 |   which styles in and out as a stair because that is what they look like on a
 |   drawing -- a different question from which floor they land on.
 | Author: suinevere
 | Dependencies: map_atlas.h
 | Globals: g_seen, g_queue, g_kind, g_dest, g_vis
 | Params: room -- object number; page -- receives the floor
 | Returns: 1 when a placed room was reached, 0 otherwise
 ----------------------*/
static int page_via_routes(unsigned short room, int *page) {
    int head = 0, tail = 0, d, i;

    for (i = 0; i < MAP_ROOM_MAX; i++) g_seen[i] = 0;
    if (!in_range(room)) return 0;
    g_seen[room] = 1;
    g_queue[tail++] = room;

    while (head < tail) {
        unsigned short cur = g_queue[head++];
        for (d = 0; d < RM_DIR_N; d++) {
            unsigned short next = g_dest[cur][d];
            if (d == RM_UP || d == RM_DOWN) continue;
            if (g_kind[cur][d] == MAP_LINK_NONE) continue;
            if (!in_range(next) || next == 0 || g_seen[next]) continue;
            if (map_atlas_page(next, page)) return 1;
            g_seen[next] = 1;
            if (tail < MAP_ROOM_MAX) g_queue[tail++] = next;
        }
    }
    return 0;
}

/*----------------------
 | map_model_climb
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_visited
 | Globals: g_dest, g_kind
 | Params: room -- object number; up -- nonzero for the floor above; dest --
 |   receives the room the stair reaches
 | Returns: 1 when the room has that staircase and its far end is placed
 ----------------------*/
int map_model_climb(unsigned short room, int up, unsigned short *dest) {
    int d = up ? RM_UP : RM_DOWN;
    unsigned short far;
    if (room >= MAP_ROOM_MAX || !map_model_visited(room)) return 0;
    far = g_dest[room][d];
    if (g_kind[room][d] == MAP_LINK_NONE) return 0;
    if (far == 0 || far >= MAP_ROOM_MAX || far == room) return 0;
    if (!map_model_visited(far)) return 0;
    *dest = far;
    return 1;
}

/*----------------------
 | map_model_nearest
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_offset, map_model_page
 | Globals: N/A
 | Params: page -- the floor; x, y -- the cell to search from; nx, ny -- receive
 |   the room's offsets
 | Returns: 1 when the floor holds a placed room, 0 otherwise
 ----------------------*/
int map_model_nearest(int page, int x, int y, int *nx, int *ny) {
    int r, best = -1;
    for (r = 1; r < MAP_ROOM_MAX; r++) {
        int dx = 0, dy = 0, ax, ay, d;
        if (!map_model_offset((unsigned short) r, &dx, &dy)) continue;
        if (map_model_page((unsigned short) r) != page) continue;
        ax = dx - x; if (ax < 0) ax = -ax;
        ay = dy - y; if (ay < 0) ay = -ay;
        d = (ax > ay) ? ax : ay;
        if (best < 0 || d < best) { best = d; *nx = dx; *ny = dy; }
    }
    return best >= 0;
}

/*----------------------
 | map_model_page
 | Description: See map_model.h. An unplaced room takes the floor of the nearest
 |   room the atlas does place along level exits -- see page_via_routes -- and
 |   only where that finds nothing does it fall back to the floor whose box it
 |   is nearest. The boxes are in the table's coordinates and the room's
 |   position is in the model's, so the frame offset atlas_target established is
 |   added to the box rather than subtracted from the room -- same arithmetic,
 |   but it keeps the room's own coordinates untouched for the comparison a
 |   reader is checking.
 | Author: suinevere
 | Dependencies: map_atlas.h, map_model_pos, page_via_routes
 | Globals: g_frame_set, g_frame_x, g_frame_y
 | Params: room -- object number
 | Returns: 0 to map_model_pages() - 1
 ----------------------*/
int map_model_page(unsigned short room) {
    int p = 0, x = 0, y = 0, i, n, best = 0, bestd = -1;

    if (map_atlas_page(room, &p)) return p;
    if (page_via_routes(room, &p)) return p;
    if (!g_frame_set) return 0;
    if (!map_model_pos(room, &x, &y)) return 0;

    n = map_atlas_pages();
    for (i = 0; i < n; i++) {
        int x0, y0, x1, y1, dx = 0, dy = 0, d;
        if (!map_atlas_page_box(i, &x0, &y0, &x1, &y1)) continue;
        x0 += g_frame_x; x1 += g_frame_x;
        y0 += g_frame_y; y1 += g_frame_y;
        if      (x < x0) dx = x0 - x;
        else if (x > x1) dx = x - x1;
        if      (y < y0) dy = y0 - y;
        else if (y > y1) dy = y - y1;
        d = (dx > dy) ? dx : dy;
        if (bestd < 0 || d < bestd) { bestd = d; best = i; }
    }
    return best;
}

/*----------------------
 | map_model_clear_reveal
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_revealed
 | Params: N/A
 | Returns: how many rooms it took back
 ----------------------*/
int map_model_clear_reveal(void) {
    int i, dropped = 0;
    for (i = 0; i < MAP_ROOM_MAX; i++) {
        if (!g_revealed[i]) continue;
        g_revealed[i] = 0;
        g_vis[i] = 0;
        dropped++;
    }
    return dropped;
}

/*----------------------
 | walked_count
 | Description: How many placed rooms the player actually walked into. The
 |   header count and the row loop below must agree about which rooms go in the
 |   blob or the reader takes the wrong number of bytes, so both ask this
 |   question the same way rather than one of them asking map_model_count.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_revealed
 | Params: N/A
 | Returns: the count, 0 or more
 ----------------------*/
static int walked_count(void) {
    int i, n = 0;
    for (i = 0; i < MAP_ROOM_MAX; i++) if (g_vis[i] && !g_revealed[i]) n++;
    return n;
}

/*----------------------
 | map_model_serialize
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: walked_count
 | Globals: g_vis, g_revealed, g_x, g_y, g_cur
 | Params: out -- receives the blob; max -- its capacity
 | Returns: bytes written, or 0
 ----------------------*/
unsigned int map_model_serialize(unsigned char *out, unsigned int max) {
    unsigned int n = 0;
    int i, cnt = walked_count();
    if (max < 4u + 6u * (unsigned int) cnt) return 0;
    out[n++] = (unsigned char) MAP_BLOB_MAGIC;
    out[n++] = (unsigned char) cnt;
    out[n++] = (unsigned char) (g_cur >> 8);
    out[n++] = (unsigned char) (g_cur & 0xFF);
    for (i = 0; i < MAP_ROOM_MAX; i++) {
        if (!g_vis[i] || g_revealed[i]) continue;
        out[n++] = (unsigned char) (i >> 8);
        out[n++] = (unsigned char) (i & 0xFF);
        out[n++] = (unsigned char) ((unsigned short) g_x[i] >> 8);
        out[n++] = (unsigned char) ((unsigned short) g_x[i] & 0xFF);
        out[n++] = (unsigned char) ((unsigned short) g_y[i] >> 8);
        out[n++] = (unsigned char) ((unsigned short) g_y[i] & 0xFF);
    }
    return n;
}

/*----------------------
 | map_model_deserialize
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_reset, in_range
 | Globals: g_vis, g_x, g_y, g_cur, g_have_cur
 | Params: in -- the blob; len -- its length
 | Returns: 1 on success, 0 when refused
 ----------------------*/
int map_model_deserialize(const unsigned char *in, unsigned int len) {
    unsigned int n = 4, k;
    int cnt;
    if (len < 4u || in[0] != (unsigned char) MAP_BLOB_MAGIC) return 0;
    cnt = in[1];
    if (len != 4u + 6u * (unsigned int) cnt) return 0;
    map_model_reset();
    g_cur = (unsigned short) ((in[2] << 8) | in[3]);
    g_have_cur = 1;
    for (k = 0; k < (unsigned int) cnt; k++) {
        unsigned short r = (unsigned short) ((in[n] << 8) | in[n + 1]);
        short px = (short) ((in[n + 2] << 8) | in[n + 3]);
        short py = (short) ((in[n + 4] << 8) | in[n + 5]);
        n += 6;
        if (!in_range(r)) { map_model_reset(); return 0; }
        g_vis[r] = 1; g_x[r] = px; g_y[r] = py;
    }
    if (!in_range(g_cur) || !g_vis[g_cur]) { map_model_reset(); return 0; }
    return 1;
}

/*----------------------
 | map_model_serialize_len
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: in -- at least four bytes of a blob
 | Returns: the claimed length, or 0 when the magic does not match
 ----------------------*/
unsigned int map_model_serialize_len(const unsigned char *in) {
    if (in[0] != (unsigned char) MAP_BLOB_MAGIC) return 0;
    return 4u + 6u * (unsigned int) in[1];
}
