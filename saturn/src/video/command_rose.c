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
    { 2, 2, 3,  9, 1 },   /* RM_E    */
    { 2, 0, 3,  3, 1 },   /* RM_W    */
    { 3, 1, 5,  6, 1 },   /* RM_S    */
    { 1, 2, 1,  8, 2 },   /* RM_NE   */
    { 1, 0, 1,  3, 2 },   /* RM_NW   */
    { 3, 2, 5,  8, 2 },   /* RM_SE   */
    { 3, 0, 5,  3, 2 },   /* RM_SW   */
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
            spoke(out, 5, '\\', exits[RM_NW]);
            spoke(out, 6, '|',  exits[RM_N]);
            spoke(out, 7, '/',  exits[RM_NE]);
            break;
        case 3:
            put(out, RM_W, exits);
            put(out, RM_E, exits);
            spoke(out, 5, '-', exits[RM_W]);
            spoke(out, 7, '-', exits[RM_E]);
            out[6] = '+';
            break;
        case 4:
            spoke(out, 5, '/',  exits[RM_SW]);
            spoke(out, 6, '|',  exits[RM_S]);
            spoke(out, 7, '\\', exits[RM_SE]);
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
 | CR_POS
 | Description: Where each direction sits, as a point a press can be aimed at.
 |   The row is the drawn one; the column is the grid column widened to 5 so the
 |   three columns stand roughly as far apart as the drawn rose has them, which
 |   is what makes an equal-length diagonal press score as a diagonal. Grid
 |   column rather than drawn column because the labels are ragged -- "in" starts
 |   a column further right than "ne" does -- and a press should not be able to
 |   walk sideways along a column just because its labels are not flush.
 | Author: suinevere
 ----------------------*/
typedef struct { signed char x, y; } CrPoint;

static const CrPoint CR_POS[RM_DIR_N] = {
    {  5, 1 },   /* N    */
    { 10, 3 },   /* E    */
    {  0, 3 },   /* W    */
    {  5, 5 },   /* S    */
    { 10, 1 },   /* NE   */
    {  0, 1 },   /* NW   */
    { 10, 5 },   /* SE   */
    {  0, 5 },   /* SW   */
    {  0, 0 },   /* UP   */
    {  0, 6 },   /* DOWN */
    { 10, 0 },   /* IN   */
    { 10, 6 }    /* OUT  */
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

/*----------------------
 | cr_better_toward / cr_better_away
 | Description: The two comparisons cr_move ranks candidates by.
 |
 |   toward: of two directions the press points at, the better one is the one
 |   whose bearing from the cursor more nearly matches the press -- cos of the
 |   angle between them, compared as dot^2/len^2 cross-multiplied so no square
 |   root or float is needed -- and, between two equally aimed ones, the nearer.
 |   Alignment before distance because a press is a bearing, not a reach: from
 |   west, up-and-right should find north rather than north-east even though both
 |   are up and to the right, and should find north-east rather than east even
 |   though east is closer.
 |
 |   away: used only when the press points at nothing, where the cursor wraps to
 |   the far side. The better one is the one furthest against the press, then the
 |   one least off its axis.
 |
 |   Both fall back to `hi_col` to settle an exact tie: the column the press
 |   itself leans toward, so an up press between two equal candidates takes the
 |   left one and a down press takes the right one. Arbitrary, but fixed, and it
 |   keeps a press and its opposite from both landing on the same cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: dot/len2/vx -- the candidate's projection onto the press, its squared
 |   distance, and its column offset; b* -- the same for the incumbent; hi_col --
 |   1 to prefer the right-hand candidate on a tie
 | Returns: 1 when the candidate beats the incumbent
 ----------------------*/
static int cr_better_toward(int dot, int len2, int vx, int bdot, int blen2, int bvx,
                            int hi_col) {
    int a = dot * dot * blen2;
    int b = bdot * bdot * len2;
    if (a != b)         return a > b;
    if (len2 != blen2)  return len2 < blen2;
    return hi_col ? (vx > bvx) : (vx < bvx);
}

static int cr_better_away(int dot, int vx, int bdot, int bvx, int hi_col) {
    int ax = (vx < 0) ? -vx : vx;
    int bx = (bvx < 0) ? -bvx : bvx;
    if (dot != bdot) return dot < bdot;
    if (ax != bx)    return ax < bx;
    return hi_col ? (vx > bvx) : (vx < bvx);
}

int cr_move(const unsigned char *exits, int dir, int dx, int dy, int *out) {
    int c, best = -1;
    int bdot = 0, blen2 = 0, bvx = 0;
    int hi_col;

    if (out) *out = dir;
    if (dir < 0 || dir >= RM_DIR_N) return 0;
    if (dx == 0 && dy == 0) return 0;
    hi_col = (dx > 0) || (dx == 0 && dy > 0);

    /* Everything the press points at, best bearing first. */
    for (c = 0; c < RM_DIR_N; c++) {
        int vx, vy, dot, len2;
        if (c == dir || !shown(exits[c])) continue;
        vx = CR_POS[c].x - CR_POS[dir].x;
        vy = CR_POS[c].y - CR_POS[dir].y;
        dot = vx * dx + vy * dy;
        if (dot <= 0) continue;
        len2 = vx * vx + vy * vy;
        if (best < 0 || cr_better_toward(dot, len2, vx, bdot, blen2, bvx, hi_col)) {
            best = c; bdot = dot; blen2 = len2; bvx = vx;
        }
    }
    if (best >= 0) { if (out) *out = best; return 0; }

    /* Nothing that way. A press with any rightward lean leaves the module --
       travel is the leftmost of the three, so right is the only side with
       somewhere to go, and this is what guarantees the cursor can always get
       out of a rose however few exits it has. */
    if (dx > 0) return 1;
    if (dy == 0) return 0;

    /* A vertical press wraps to the far side instead, so the short columns can
       be ridden round rather than dead-ending at the poles. */
    for (c = 0; c < RM_DIR_N; c++) {
        int vx, vy, dot;
        if (c == dir || !shown(exits[c])) continue;
        vx = CR_POS[c].x - CR_POS[dir].x;
        vy = CR_POS[c].y - CR_POS[dir].y;
        dot = vx * dx + vy * dy;
        if (best < 0 || cr_better_away(dot, vx, bdot, bvx, hi_col)) {
            best = c; bdot = dot; bvx = vx;
        }
    }
    if (best >= 0 && out) *out = best;
    return 0;
}

/*----------------------
 | CR_DIR_WORD
 | Description: The direction spellings cr_dir_word returns, in RM_* index order.
 | Author: suinevere
 ----------------------*/
static const char *CR_DIR_WORD[RM_DIR_N] = {
    "north", "east", "west", "south", "ne", "nw", "se", "sw",
    "up", "down", "in", "out"
};

const char *cr_dir_word(int dir) {
    if (dir < 0 || dir >= RM_DIR_N) return "";
    return CR_DIR_WORD[dir];
}
