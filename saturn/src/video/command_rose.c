/*----------------------
 | command_rose.c
 | Description: The rose composition and cursor grid described in command_rose.h.
 | Author: suinevere
 | Dependencies: command_rose.h, room_model.h
 ----------------------*/
#include "command_rose.h"
#include "room_model.h"

/*----------------------
 | CrCell / CR_CELL
 | Description: Every direction's place in both geometries at once: its logical
 |   grid cell and the drawn row, column and width of its label. One table
 |   rather than two so a layout change cannot move the label without moving the
 |   cursor target with it. Order is the RM_* enum's, so CR_CELL[dir] indexes
 |   directly.
 | Author: suinevere
 ----------------------*/
typedef struct {
    signed char grow, gcol;   /* logical grid cell */
    signed char row, col;     /* drawn row and first column */
    signed char len;          /* label width */
} CrCell;

static const CrCell CR_CELL[RM_DIR_N] = {
    { 1, 1, 1,  6, 1 },   /* RM_N    */
    { 2, 2, 3, 10, 1 },   /* RM_E    */
    { 2, 0, 3,  2, 1 },   /* RM_W    */
    { 3, 1, 5,  6, 1 },   /* RM_S    */
    { 1, 2, 1,  9, 2 },   /* RM_NE   */
    { 1, 0, 1,  2, 2 },   /* RM_NW   */
    { 3, 2, 5,  9, 2 },   /* RM_SE   */
    { 3, 0, 5,  2, 2 },   /* RM_SW   */
    { 0, 0, 0,  0, 2 },   /* RM_UP   */
    { 4, 0, 6,  0, 4 },   /* RM_DOWN */
    { 0, 2, 0, 11, 2 },   /* RM_IN   */
    { 4, 2, 6, 10, 3 }    /* RM_OUT  */
};

/*----------------------
 | CR_LABEL
 | Description: The lowercase text of each direction's label, in RM_* order.
 |   Lowercase is the stored form because `put` uppercases an open exit and
 |   leaves a conditional one alone.
 | Author: suinevere
 ----------------------*/
static const char *const CR_LABEL[RM_DIR_N] = {
    "n", "e", "w", "s", "ne", "nw", "se", "sw", "up", "down", "in", "out"
};

/*----------------------
 | shown / put
 | Description: Whether a direction should appear at all (open or conditional),
 |   and writing a direction's label at its own drawn position, uppercased when
 |   the exit is decoded open and left lowercase when it is only possible.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CR_CELL, CR_LABEL
 | Params: st -- an RM_EXIT_* state; out -- the row; dir -- an RM_* index;
 |   exits -- the exit states
 | Returns: shown returns 1 when the direction appears
 ----------------------*/
static int shown(unsigned char st) {
    return st == RM_EXIT_OPEN || st == RM_EXIT_MAYBE;
}

static void put(char *out, int dir, const unsigned char *exits) {
    const char *text = CR_LABEL[dir];
    int col = CR_CELL[dir].col;
    int i;
    if (!shown(exits[dir])) return;
    for (i = 0; text[i] && col + i < CR_COLS; i++) {
        char c = text[i];
        if (exits[dir] == RM_EXIT_OPEN && c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
        out[col + i] = c;
    }
}

/*----------------------
 | spoke
 | Description: Draws one direction's spoke character when that direction is
 |   available. Separate from `put` because a spoke is a fixed glyph at a fixed
 |   cell rather than a label, and because the corner directions have none.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out -- the row; col -- inner column; c -- the glyph; st -- the state
 | Returns: N/A
 ----------------------*/
static void spoke(char *out, int col, char c, unsigned char st) {
    if (shown(st)) out[col] = c;
}

void cr_row(const unsigned char *exits, int row, char *out) {
    int i;
    for (i = 0; i < CR_COLS; i++) out[i] = ' ';
    out[CR_COLS] = '\0';

    switch (row) {
        case 0:
            put(out, RM_UP, exits);
            put(out, RM_IN, exits);
            break;
        case 1:
            put(out, RM_NW, exits);
            put(out, RM_N,  exits);
            put(out, RM_NE, exits);
            break;
        case 2:
            spoke(out, 4, '\\', exits[RM_NW]);
            spoke(out, 6, '|',  exits[RM_N]);
            spoke(out, 8, '/',  exits[RM_NE]);
            break;
        case 3:
            put(out, RM_W, exits);
            put(out, RM_E, exits);
            spoke(out, 4, '-', exits[RM_W]);
            spoke(out, 8, '-', exits[RM_E]);
            out[6] = '+';
            break;
        case 4:
            spoke(out, 4, '/',  exits[RM_SW]);
            spoke(out, 6, '|',  exits[RM_S]);
            spoke(out, 8, '\\', exits[RM_SE]);
            break;
        case 5:
            put(out, RM_SW, exits);
            put(out, RM_S,  exits);
            put(out, RM_SE, exits);
            break;
        case 6:
            put(out, RM_DOWN, exits);
            put(out, RM_OUT,  exits);
            break;
        default:
            break;
    }
}

int cr_grid_dir(int grow, int gcol) {
    int d;
    if (grow < 0 || grow >= CR_GRID_ROWS || gcol < 0 || gcol >= CR_GRID_COLS) return -1;
    for (d = 0; d < RM_DIR_N; d++)
        if (CR_CELL[d].grow == grow && CR_CELL[d].gcol == gcol) return d;
    return -1;
}

int cr_dir_cell(int dir, int *row, int *col, int *len) {
    if (dir < 0 || dir >= RM_DIR_N) return 0;
    if (row) *row = CR_CELL[dir].row;
    if (col) *col = CR_CELL[dir].col;
    if (len) *len = CR_CELL[dir].len;
    return 1;
}

int cr_dir_row(int dir) {
    if (dir < 0 || dir >= RM_DIR_N) return -1;
    return CR_CELL[dir].row;
}

/*----------------------
 | CR_GRID_ROW_Y
 | Description: The drawn row each logical grid row sits on -- the spoke rows
 |   between them carry no labels and so no cursor targets.
 | Author: suinevere
 ----------------------*/
static const signed char CR_GRID_ROW_Y[CR_GRID_ROWS] = { 0, 1, 3, 5, 6 };

/*----------------------
 | cr_row_pick
 | Description: One row of the search cr_enter runs: the first available
 |   direction on grid row `r`, taken from the edge focus arrived through.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: exits -- the exit states; r -- grid row; from_right -- 1 to prefer
 |   the right-hand column
 | Returns: an RM_* index, or -1
 ----------------------*/
static int cr_row_pick(const unsigned char *exits, int r, int from_right) {
    int c;
    for (c = 0; c < CR_GRID_COLS; c++) {
        int col = from_right ? (CR_GRID_COLS - 1 - c) : c;
        int d = cr_grid_dir(r, col);
        if (d >= 0 && shown(exits[d])) return d;
    }
    return -1;
}

/*----------------------
 | CR_UP / CR_DOWN / CR_LEFT / CR_RIGHT / CR_LEAVE
 | Description: The four presses, as CR_STEP's column index, and the marker for
 |   a step that carries focus out of the module.
 | Author: suinevere
 ----------------------*/
enum { CR_UP, CR_DOWN, CR_LEFT, CR_RIGHT, CR_STEP_N };
#define CR_LEAVE (-2)

/*----------------------
 | CR_STEP
 | Description: Where each press goes from each direction, in RM_* row order and
 |   up/down/left/right column order. Written out rather than derived from the
 |   grid because the rose is not a rectangle: the corners have no neighbour
 |   across from them, the middle row has no centre cell, and each column wraps
 |   end to end, so every plausible rule produced at least one press that went
 |   somewhere the shape did not suggest. A table has no such cases -- it is the
 |   shape.
 |
 |   Only the right-hand column leaves; the travel module is the leftmost of the
 |   three, so a press against its left edge has nowhere to go and rides the
 |   column round instead.
 | Author: suinevere
 ----------------------*/
static const signed char CR_STEP[RM_DIR_N][CR_STEP_N] = {
    /*            up        down      left      right   */
    /* N    */ { RM_S,     RM_S,     RM_NW,    RM_NE    },
    /* E    */ { RM_NE,    RM_SE,    RM_W,     CR_LEAVE },
    /* W    */ { RM_NW,    RM_SW,    RM_UP,    RM_E     },
    /* S    */ { RM_N,     RM_N,     RM_SW,    RM_SE    },
    /* NE   */ { RM_IN,    RM_E,     RM_N,     CR_LEAVE },
    /* NW   */ { RM_UP,    RM_W,     RM_UP,    RM_N     },
    /* SE   */ { RM_E,     RM_OUT,   RM_S,     CR_LEAVE },
    /* SW   */ { RM_W,     RM_DOWN,  RM_UP,    RM_S     },
    /* UP   */ { RM_DOWN,  RM_NW,    RM_DOWN,  RM_NW    },
    /* DOWN */ { RM_SW,    RM_UP,    RM_OUT,   RM_SW    },
    /* IN   */ { RM_OUT,   RM_NE,    RM_NE,    CR_LEAVE },
    /* OUT  */ { RM_SE,    RM_IN,    RM_SE,    CR_LEAVE }
};

int cr_enter(const unsigned char *exits, int want_row, int from_right) {
    int want = 0, r, spread, d;

    for (r = 1; r < CR_GRID_ROWS; r++) {
        int dh = CR_GRID_ROW_Y[r]    - want_row;
        int db = CR_GRID_ROW_Y[want] - want_row;
        if (dh < 0) dh = -dh;
        if (db < 0) db = -db;
        if (dh < db) want = r;
    }

    for (spread = 0; spread < CR_GRID_ROWS; spread++) {
        d = cr_row_pick(exits, want - spread, from_right);
        if (d >= 0) return d;
        if (spread == 0) continue;
        d = cr_row_pick(exits, want + spread, from_right);
        if (d >= 0) return d;
    }
    return -1;
}

int cr_move(const unsigned char *exits, int dir, int dx, int dy, int *out) {
    int step, at, hops;

    if (out) *out = dir;
    if (dir < 0 || dir >= RM_DIR_N) return 0;

    if      (dx < 0) step = CR_LEFT;
    else if (dx > 0) step = CR_RIGHT;
    else if (dy < 0) step = CR_UP;
    else if (dy > 0) step = CR_DOWN;
    else return 0;

    /* A step onto a direction the room does not offer is not refused, it is
       taken again from there in the same direction -- so a press walks over the
       missing ones and lands on the next real neighbour along its own line,
       rather than stopping dead or jumping somewhere off the line entirely. The
       walk ends when it comes back to where it started, which is the only way a
       room with a single exit can be left alone. */
    at = dir;
    for (hops = 0; hops < RM_DIR_N; hops++) {
        int next = CR_STEP[at][step];
        if (next == CR_LEAVE) return 1;
        if (next < 0 || next >= RM_DIR_N) return 0;
        if (shown(exits[next])) { if (out) *out = next; return 0; }
        if (next == dir) return 0;
        at = next;
    }
    return 0;
}
