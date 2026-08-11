/*----------------------
 | command_rose.c
 | Description: The rose composition described in command_rose.h.
 | Author: suinevere
 | Dependencies: command_rose.h, room_model.h
 ----------------------*/
#include "command_rose.h"
#include "room_model.h"

/*----------------------
 | shown / put
 | Description: Whether a direction should appear at all (open or conditional),
 |   and writing a label into the row at a column, uppercased when the exit is
 |   decoded open and left lowercase when it is only possible.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- an RM_EXIT_* state; out -- the row; col -- inner column;
 |   text -- the label
 | Returns: shown returns 1 when the direction appears
 ----------------------*/
static int shown(unsigned char st) {
    return st == RM_EXIT_OPEN || st == RM_EXIT_MAYBE;
}

static void put(char *out, int col, const char *text, unsigned char st) {
    int i;
    if (!shown(st)) return;
    for (i = 0; text[i] && col + i < CR_COLS; i++) {
        char c = text[i];
        if (st == RM_EXIT_OPEN && c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
        out[col + i] = c;
    }
}

void cr_row(const unsigned char *exits, int row, char *out) {
    int i;
    for (i = 0; i < CR_COLS; i++) out[i] = ' ';
    out[CR_COLS] = '\0';

    switch (row) {
        case 0:
            put(out, 0,  "nw", exits[RM_NW]);
            put(out, 11, "ne", exits[RM_NE]);
            put(out, 6,  "n",  exits[RM_N]);
            if (shown(exits[RM_UP])) { out[CR_UP_L] = '^'; out[CR_UP_R] = '^'; }
            break;
        case 1:
            if (shown(exits[RM_NW])) out[3] = '\\';
            if (shown(exits[RM_NE])) out[9] = '/';
            if (shown(exits[RM_IN]))      put(out, 5, "in", exits[RM_IN]);
            else if (shown(exits[RM_N]))  out[6] = '|';
            break;
        case 2:
            put(out, 0,  "w", exits[RM_W]);
            put(out, 12, "e", exits[RM_E]);
            if (shown(exits[RM_W])) { out[2] = '-'; out[3] = '-'; }
            if (shown(exits[RM_E])) { out[9] = '-'; out[10] = '-'; }
            out[6] = '+';
            break;
        case 3:
            if (shown(exits[RM_SW])) out[3] = '/';
            if (shown(exits[RM_SE])) out[9] = '\\';
            if (shown(exits[RM_OUT]))     put(out, 5, "out", exits[RM_OUT]);
            else if (shown(exits[RM_S]))  out[6] = '|';
            break;
        case 4:
            put(out, 0,  "sw", exits[RM_SW]);
            put(out, 11, "se", exits[RM_SE]);
            put(out, 6,  "s",  exits[RM_S]);
            if (shown(exits[RM_DOWN])) { out[CR_UP_L] = 'v'; out[CR_UP_R] = 'v'; }
            break;
        default:
            break;
    }
}
