/*----------------------
 | rose_draw.cxx
 | Description: The rose-row draw call described in rose_draw.h, moved here from
 |   command_view.cxx so console_view can draw a rose without pulling the command
 |   panel's 10 KB of image and 15 KB of .bss into a build that never shows it.
 | Author: suinevere
 | Dependencies: rose_draw.h, command_rose.h, command_view.h, text_map.h
 ----------------------*/
#include "rose_draw.h"
#include "command_rose.h"
#include "command_view.h"
#include "text_map.h"

void cv_draw_rose_row(int row, const unsigned char *exits, int y, int sel) {
    char buf[CR_COLS + 1];
    int srow, scol, slen;
    cr_row(exits, row, buf);
    text_print_dim(CV_TRAVEL_X, y, buf);
    if (sel < 0 || !cr_dir_cell(sel, &srow, &scol, &slen) || srow != row) return;
    if (buf[scol] == ' ') return;
    {
        char label[5];
        int i;
        for (i = 0; i < slen && i < (int) sizeof label - 1; i++) label[i] = buf[scol + i];
        label[i] = '\0';
        text_print(CV_TRAVEL_X + scol, y, label);
    }
}
