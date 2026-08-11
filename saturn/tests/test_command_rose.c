/*----------------------
 | test_command_rose.c
 | Description: Host test for the compass rose's row composition. The rose is
 |   drawn from the decoded exit states alone, so an absent exit erases both its
 |   label and its spoke, a conditional one lowercases it, and the in and out
 |   words take the vertical spokes when they are available. Asserts the exact
 |   13-column rows, which is what keeps the module inside its 40-column strip.
 | Author: suinevere
 | Dependencies: ../src/video/command_rose.h and command_rose.c,
 |   ../src/engine/room_model.h, assert.h, string.h, stdio.h
 | Build: gcc -std=c11 -Wall -Wextra -I saturn/src/engine -o /tmp/tcr.exe \
 |          saturn/tests/test_command_rose.c saturn/src/video/command_rose.c \
 |          && /tmp/tcr.exe
 |   The -I saturn/src/engine is needed because command_rose.c includes
 |   "room_model.h" unqualified, which the real build resolves through
 |   makefile:34's -I for every src subdirectory.
 ----------------------*/
#include "../src/video/command_rose.h"
#include "../src/engine/room_model.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    unsigned char e[RM_DIR_N];
    char row[CR_COLS + 1];
    int i;

    /* Nothing available: every row is blank but the centre marker. */
    for (i = 0; i < RM_DIR_N; i++) e[i] = RM_EXIT_NONE;
    cr_row(e, 0, row); assert(strcmp(row, "             ") == 0);
    cr_row(e, 1, row); assert(strcmp(row, "             ") == 0);
    cr_row(e, 2, row); assert(strcmp(row, "      +      ") == 0);
    cr_row(e, 3, row); assert(strcmp(row, "             ") == 0);
    cr_row(e, 4, row); assert(strcmp(row, "             ") == 0);

    /* North of House: n, e, w, se and sw open; s blocked; nothing else. */
    for (i = 0; i < RM_DIR_N; i++) e[i] = RM_EXIT_NONE;
    e[RM_N] = e[RM_E] = e[RM_W] = e[RM_SE] = e[RM_SW] = RM_EXIT_OPEN;
    e[RM_S] = RM_EXIT_BLOCKED;
    cr_row(e, 0, row); assert(strcmp(row, "      N      ") == 0);
    cr_row(e, 1, row); assert(strcmp(row, "      |      ") == 0);
    cr_row(e, 2, row); assert(strcmp(row, "W --  +  -- E") == 0);
    cr_row(e, 3, row); assert(strcmp(row, "   /     \\   ") == 0);
    cr_row(e, 4, row); assert(strcmp(row, "SW         SE") == 0);

    /* Up and down flank the poles; in and out take the vertical spokes. */
    for (i = 0; i < RM_DIR_N; i++) e[i] = RM_EXIT_NONE;
    e[RM_N] = e[RM_S] = e[RM_UP] = e[RM_DOWN] = RM_EXIT_OPEN;
    e[RM_IN] = e[RM_OUT] = RM_EXIT_OPEN;
    cr_row(e, 0, row); assert(strcmp(row, "   ^  N  ^   ") == 0);
    cr_row(e, 1, row); assert(strcmp(row, "     IN      ") == 0);
    cr_row(e, 3, row); assert(strcmp(row, "     OUT     ") == 0);
    cr_row(e, 4, row); assert(strcmp(row, "   v  S  v   ") == 0);

    /* A conditional exit is lowercased rather than promised. */
    for (i = 0; i < RM_DIR_N; i++) e[i] = RM_EXIT_NONE;
    e[RM_NE] = RM_EXIT_MAYBE;
    cr_row(e, 0, row); assert(strcmp(row, "           ne") == 0);

    printf("test_command_rose ok\n");
    return 0;
}
