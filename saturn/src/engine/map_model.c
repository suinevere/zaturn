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

/*----------------------
 | DX / DY
 | Description: The grid step for each RM_* direction, in room units. UP, DOWN,
 |   IN and OUT get a vertical step so a staircase reads as a level change
 |   rather than as a north exit; where that collides with a real neighbour the
 |   placement search resolves it.
 | Author: suinevere
 ----------------------*/
static const signed char DX[RM_DIR_N] = { 0, 1,-1, 0, 1,-1, 1,-1, 0, 0, 0, 0 };
static const signed char DY[RM_DIR_N] = {-1, 0, 0, 1,-1,-1, 1, 1,-1, 1, 1,-1 };

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
 | map_model_reset
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_cur, g_have_cur, g_have_prev
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void map_model_reset(void) {
    int i;
    for (i = 0; i < MAP_ROOM_MAX; i++) { g_vis[i] = 0; g_x[i] = 0; g_y[i] = 0; { int d; for (d = 0; d < RM_DIR_N; d++) { g_dest[i][d] = 0; g_kind[i][d] = 0; } } }
    g_cur = 0;
    g_have_cur = 0;
    g_have_prev = 0;
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
 | cell_taken
 | Description: Whether a placed room other than `self` already holds a cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_vis, g_x, g_y
 | Params: x, y -- the cell; self -- the room allowed to hold it
 | Returns: 1 when occupied, 0 otherwise
 ----------------------*/
static int cell_taken(int x, int y, unsigned short self) {
    int i;
    for (i = 0; i < MAP_ROOM_MAX; i++)
        if (g_vis[i] && i != (int) self && g_x[i] == x && g_y[i] == y) return 1;
    return 0;
}

/*----------------------
 | SPX / SPY / MAP_SPIRAL_MAX
 | Description: The order a contested placement searches outward in, and how far
 |   it will go before giving up and stacking. Fixed here rather than left to
 |   the implementation so the host tests pin an order that was chosen, not one
 |   that happened.
 |
 |   Eight rays, not eight rings: at radius r the search probes only the eight
 |   cells (SPX[k]*r, SPY[k]*r), so at r=2 it looks at (0,-2) and (2,-2) and
 |   never at (1,-2). A contested room can therefore be flung eight cells out
 |   while a nearer cell off the rays sat free. Deterministic either way, which
 |   is what the tests pin; the geometry is the cost of keeping it that cheap.
 | Author: suinevere
 ----------------------*/
static const signed char SPX[8] = { 0, 1, 0,-1, 1, 1,-1,-1 };
static const signed char SPY[8] = {-1, 0, 1, 0,-1, 1, 1,-1 };
#define MAP_SPIRAL_MAX 8

/*----------------------
 | place
 | Description: Puts a room at the target cell, or at the nearest free cell in
 |   the fixed search order when the target is taken, and marks it placed. The
 |   first room to hold a cell keeps it.
 | Author: suinevere
 | Dependencies: cell_taken
 | Globals: g_vis, g_x, g_y
 | Params: room -- object number; tx, ty -- the wanted cell
 | Returns: N/A
 ----------------------*/
static void place(unsigned short room, int tx, int ty) {
    int r, k, x = tx, y = ty;
    if (cell_taken(tx, ty, room)) {
        int done = 0;
        for (r = 1; r <= MAP_SPIRAL_MAX && !done; r++) {
            for (k = 0; k < 8 && !done; k++) {
                int cx = tx + SPX[k] * r, cy = ty + SPY[k] * r;
                if (!cell_taken(cx, cy, room)) { x = cx; y = cy; done = 1; }
            }
        }
    }
    g_x[room] = (short) x;
    g_y[room] = (short) y;
    g_vis[room] = 1;
}

/*----------------------
 | record_exits
 | Description: Copies one snapshot's exits and destinations into the placed
 |   room's own row, which is what map_model_link answers from. Shared by the
 |   live path and by the rebind a restore needs, so the two cannot disagree
 |   about what counts as a vertical exit.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_dest, g_kind
 | Params: room -- an in-range object number; m -- the snapshot to read
 | Returns: N/A
 ----------------------*/
static void record_exits(unsigned short room, const RoomModel *m) {
    int d;
    for (d = 0; d < RM_DIR_N; d++) {
        g_dest[room][d] = m->dest[d];
        g_kind[room][d] = (unsigned char)
            (m->exits[d] == RM_EXIT_NONE ? MAP_LINK_NONE
             : (d >= RM_UP ? MAP_LINK_VERT : MAP_LINK_FLAT));
    }
}

/*----------------------
 | map_model_enter
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: dir_from_prev, place, record_exits
 | Globals: g_vis, g_cur, g_have_cur, g_prev, g_have_prev
 | Params: m -- the snapshot, never null
 | Returns: N/A
 ----------------------*/
void map_model_enter(const RoomModel *m) {
    unsigned short room = m->room;
    if (!in_range(room)) return;

    if (!g_vis[room]) {
        int d = dir_from_prev(room);
        int tx = 0, ty = 0;
        if (g_have_cur && g_vis[g_cur]) {
            tx = g_x[g_cur];
            ty = g_y[g_cur];
            if (d != MAP_DIR_UNKNOWN) { tx += DX[d]; ty += DY[d]; }
            else                      { ty += 1; }
        }
        place(room, tx, ty);
    }

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
 | map_model_serialize
 | Description: See map_model.h.
 | Author: suinevere
 | Dependencies: map_model_count
 | Globals: g_vis, g_x, g_y, g_cur
 | Params: out -- receives the blob; max -- its capacity
 | Returns: bytes written, or 0
 ----------------------*/
unsigned int map_model_serialize(unsigned char *out, unsigned int max) {
    unsigned int n = 0;
    int i, cnt = map_model_count();
    if (max < 4u + 6u * (unsigned int) cnt) return 0;
    out[n++] = (unsigned char) MAP_BLOB_MAGIC;
    out[n++] = (unsigned char) cnt;
    out[n++] = (unsigned char) (g_cur >> 8);
    out[n++] = (unsigned char) (g_cur & 0xFF);
    for (i = 0; i < MAP_ROOM_MAX; i++) {
        if (!g_vis[i]) continue;
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
