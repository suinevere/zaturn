/*----------------------
 | test_overlay_layout.c
 | Description: The inventory overlay's two geometries -- the plain seven-row
 |   strip every story gets and the twelve-row strip with a picture module that
 |   only a story with item art gets -- checked for the arithmetic that is easy
 |   to get wrong by one and impossible to see wrong on screen. The picture
 |   module is the tight case: its interior is the picture plus exactly one cell
 |   of frame on every side, so a column or a row out anywhere puts the frame
 |   through the picture or leaves a strip of bare marble beside it. Run from
 |   the repository root:
 |   gcc -O2 -I saturn/src -o /tmp/tovl saturn/tests/test_overlay_layout.c && /tmp/tovl
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include "video/panel_layout.h"
#include "video/item_art.h"

#define PIC_W 64
#define PIC_H 80

/*----------------------
 | SCREEN_ROWS / OVERLAY_TOP_ROW
 | Description: The display's height in cells, and the row the tall overlay's
 |   first content row lands on: the input line, the two frame rows and the
 |   strip's seven come off the bottom, then the tall shape's rise lifts the
 |   frame, and the content starts one row below that. item_art.h hardcodes the
 |   pixel form of the row below that one -- the first row of the picture
 |   itself, since the row above it is the picture's frame -- which is what the
 |   check below holds it to.
 | Author: suinevere
 ----------------------*/
#define SCREEN_ROWS 30
#define OVERLAY_TOP_ROW (SCREEN_ROWS - (1 + CV_STRIP_ROWS + 2) - CV_OVERLAY_RISE + 2)

static int fails = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

int main(void) {
    int right = CV_OVERLAY_X + CV_OVERLAY_W - 1;
    int listN = CV_OVERLAY_LIST_X + CV_OVERLAY_LIST_W - 1;
    int paneN = CV_OVERLAY_PANE_X + CV_OVERLAY_PANE_W - 1;
    int picN  = CV_OVERLAY_PIC_X + CV_OVERLAY_PIC_W - 1;

    /* The box is the strip, so there is no second frame inside it. */
    check(CV_OVERLAY_X == 0 && CV_OVERLAY_W == 40, "the box is the whole strip");
    check(right == 39, "the right frame is column 39");

    /* List, divider, module-left frame, picture module, right frame. */
    check(CV_OVERLAY_LIST_X == CV_OVERLAY_X + 1, "the list opens against the left frame");
    check(listN == CV_OVERLAY_DIV_X - 1, "the list runs up to the divider");
    check(CV_OVERLAY_PANE_X == CV_OVERLAY_DIV_X + 2,
          "the divider closes the list and the next column opens the picture module");
    check(paneN == right - 1, "the picture module ends against the right frame");
    check(CV_OVERLAY_LIST_W + 2 + CV_OVERLAY_PANE_W == CV_OVERLAY_W - 2,
          "list, seam and picture module fill the interior exactly");

    /* Every content row is a list row, because the frame rows are the strip's. */
    check(CV_OVERLAY_ROWS == CV_OVERLAY_TALL_ROWS, "the tall box lists every content row");
    check(CV_OVERLAY_SHORT_LIST == CV_STRIP_ROWS, "the plain box lists every content row");
    check(CV_OVERLAY_RISE == CV_OVERLAY_TALL_ROWS - CV_STRIP_ROWS,
          "the rise is the height the strip gained");

    /* The picture is the module's interior less one cell of frame all round. */
    check(CV_OVERLAY_PIC_X == CV_OVERLAY_PANE_X + 1, "one frame column left of the picture");
    check(picN == paneN - 1, "one frame column right of the picture");
    check(CV_OVERLAY_PIC_ROWS == CV_OVERLAY_TALL_ROWS - 2,
          "one frame row above and below the picture");
    check(CV_OVERLAY_PIC_W * 8 == PIC_W, "the picture's columns are its own width");
    check(CV_OVERLAY_PIC_ROWS * 8 == PIC_H, "the picture's rows are its own height");

    /* And item_art writes it exactly there. */
    check(ITEM_ART_X == CV_OVERLAY_PIC_X * 8, "the picture starts at its own column");
    check(ITEM_ART_Y == (OVERLAY_TOP_ROW + 1) * 8, "the picture starts a frame row down");
    check(ITEM_ART_X + PIC_W == (picN + 1) * 8, "the picture ends against its right frame");
    check(ITEM_ART_Y + PIC_H == (OVERLAY_TOP_ROW + 1 + CV_OVERLAY_PIC_ROWS) * 8,
          "the picture ends against its bottom frame");

    printf(fails ? "%d FAILURES\n" : "all pass\n", fails);
    return fails ? 1 : 0;
}
