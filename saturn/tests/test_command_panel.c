/*----------------------
 | test_command_panel.c
 | Description: Host test for the command panel's state machine: focus movement
 |   across the three modules, slot progression as a sentence fills, the
 |   preposition slot opening only when the caller says the grammar wants one,
 |   and Back unwinding a word at a time. Asserts the assembled command string,
 |   which is the panel's only output. No SRL or Saturn code is involved.
 | Author: suinevere
 | Dependencies: ../src/input/command_panel.h and command_panel.c, assert.h,
 |   string.h, stdio.h
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe \
 |          saturn/tests/test_command_panel.c saturn/src/input/command_panel.c \
 |          && /tmp/tcp.exe
 ----------------------*/
#include "../src/input/command_panel.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    CommandPanel p;

    cp_reset(&p);
    assert(p.box == CP_BOX_WORD);
    assert(p.slot == CP_SLOT_VERB);
    assert(p.line_len == 0);

    cp_focus(&p, -1);
    assert(p.box == CP_BOX_TRAVEL);
    cp_focus(&p, -1);
    assert(p.box == CP_BOX_TRAVEL);
    cp_focus(&p, +1);
    cp_focus(&p, +1);
    assert(p.box == CP_BOX_CMD);
    cp_focus(&p, +1);
    assert(p.box == CP_BOX_CMD);

    /* Two-slot command: verb then noun, no preposition wanted. */
    cp_reset(&p);
    cp_pick(&p, "take", 0);
    assert(p.slot == CP_SLOT_NOUN);
    assert(strcmp(p.line, "take") == 0);
    cp_pick(&p, "lamp", 0);
    assert(p.slot == CP_SLOT_DONE);
    assert(strcmp(p.line, "take lamp") == 0);
    assert(p.submitted == 1);

    /* Four-slot command: the caller reports the grammar wants a preposition. */
    cp_reset(&p);
    cp_pick(&p, "put", 0);
    cp_pick(&p, "coffin", 1);
    assert(p.slot == CP_SLOT_PREP);
    assert(p.submitted == 0);
    cp_pick(&p, "in", 0);
    assert(p.slot == CP_SLOT_NOUN2);
    cp_pick(&p, "boat", 0);
    assert(strcmp(p.line, "put coffin in boat") == 0);
    assert(p.submitted == 1);

    /* Back unwinds one word and one slot at a time. */
    cp_reset(&p);
    cp_pick(&p, "put", 0);
    cp_pick(&p, "coffin", 1);
    cp_back(&p);
    assert(strcmp(p.line, "put") == 0);
    assert(p.slot == CP_SLOT_NOUN);
    cp_back(&p);
    assert(p.line_len == 0);
    assert(p.slot == CP_SLOT_VERB);
    assert(p.box == CP_BOX_WORD);
    cp_back(&p);
    assert(p.box == CP_BOX_TRAVEL);

    /* Travel submits a whole command in one pick, whatever slot was showing. */
    cp_reset(&p);
    cp_focus(&p, -1);
    assert(p.box == CP_BOX_TRAVEL);
    cp_pick(&p, "north", 0);
    assert(strcmp(p.line, "north") == 0);
    assert(p.slot == CP_SLOT_DONE);
    assert(p.submitted == 1);

    /* The cursor is clamped to the module it is walking, never wrapped. */
    cp_reset(&p);
    cp_move(&p, -1, 10);
    assert(p.cursor == 0);
    cp_move(&p, 4, 10);
    assert(p.cursor == 4);
    cp_move(&p, 99, 10);
    assert(p.cursor == 9);
    cp_move(&p, 1, 0);
    assert(p.cursor == 0);

    /* A travel pick submits in one step; Back afterward unwinds to an empty
       line at the verb slot. */
    cp_reset(&p);
    cp_focus(&p, -1);
    cp_pick(&p, "north", 0);
    cp_back(&p);
    assert(p.line_len == 0);
    assert(p.slot == CP_SLOT_VERB);

    {
        static char names[32][4];
        static const char *c[32];
        CommandWords w;
        int i, j;

        for (i = 0; i < 32; i++) {
            names[i][0] = (char)('a' + (i % 26));
            names[i][1] = (char)('0' + (i / 26));
            names[i][2] = '\0';
            c[i] = names[i];
        }

        /* Nine fits with a cell to spare; cells hold the candidates in order
           and the spare cell is null. Every cell carries a word now -- none is
           given up to a paging marker. */
        cp_fill(c, 9, 0, &w);
        assert(w.n == 9 && w.top == 0 && w.rows == 5);
        for (j = 0; j < 9; j++) assert(strcmp(w.word[j], c[j]) == 0);
        assert(w.word[9] == 0);

        /* Ten fills every cell exactly, in order. */
        cp_fill(c, 10, 0, &w);
        assert(w.n == 10 && w.rows == 5);
        for (j = 0; j < 10; j++) assert(strcmp(w.word[j], c[j]) == 0);

        /* Eleven needs a sixth row, which the window reaches by scrolling one
           row rather than turning a page: the second row of the scrolled window
           is the first list row repeated one place up. */
        cp_fill(c, 11, 0, &w);
        assert(w.n == 10 && w.top == 0 && w.rows == 6);
        for (j = 0; j < 10; j++) assert(strcmp(w.word[j], c[j]) == 0);
        cp_fill(c, 11, 1, &w);
        assert(w.n == 9 && w.top == 1 && w.rows == 6);
        for (j = 0; j < 9; j++) assert(strcmp(w.word[j], c[2 + j]) == 0);
        assert(w.word[9] == 0);

        assert(cp_word_rows(0)  == 0);
        assert(cp_word_rows(9)  == 5);
        assert(cp_word_rows(10) == 5);
        assert(cp_word_rows(11) == 6);
        assert(cp_word_rows(19) == 10);

        /* A scroll past the end is pulled back so the window always sits on
           real candidates. */
        cp_fill(c, 19, 99, &w);
        assert(w.top == 5 && w.n == 9);
        assert(strcmp(w.word[0], c[10]) == 0);
        cp_fill(c, 9, 3, &w);
        assert(w.top == 0);

        /* Every candidate is reachable by scrolling a row at a time, in order,
           with nothing skipped between one window and the next. */
        {
            int top, seen[19], k;
            for (k = 0; k < 19; k++) seen[k] = 0;
            for (top = 0; top <= cp_word_rows(19) - 1; top++) {
                cp_fill(c, 19, top, &w);
                for (j = 0; j < w.n; j++) seen[w.top * CP_WORD_COLS + j] = 1;
                for (j = w.n; j < CP_WORD_CELLS; j++) assert(w.word[j] == 0);
            }
            for (k = 0; k < 19; k++) assert(seen[k]);
        }
    }

    /* Spreadsheet movement: left and right stay on their row and report the
       edge instead of running into the next one, up and down scroll against the
       window's edge, and the cursor never lands on an empty cell. */
    {
        static const char *c[19];
        static char names[19][4];
        int i;
        for (i = 0; i < 19; i++) {
            names[i][0] = (char)('a' + i); names[i][1] = '\0';
            c[i] = names[i];
        }
        (void) c;

        cp_reset(&p);
        p.cursor = 0;
        p.top = 0;

        /* Right from column 0 lands on column 1 of the same row; right again
           reports the edge without moving. */
        assert(cp_word_move(&p, 1, 0, 19) == 0 && p.cursor == 1);
        assert(cp_word_move(&p, 1, 0, 19) == 1 && p.cursor == 1);
        /* Left from column 0 reports the other edge -- it does not wrap up to
           the end of the row above. */
        assert(cp_word_move(&p, -1, 0, 19) == 0 && p.cursor == 0);
        assert(cp_word_move(&p, -1, 0, 19) == -1 && p.cursor == 0);

        /* Down through the window, then one more press scrolls a row instead of
           stopping, leaving the cursor on the bottom row. */
        for (i = 0; i < CP_WORD_ROWS - 1; i++) assert(cp_word_move(&p, 0, 1, 19) == 0);
        assert(p.cursor == (CP_WORD_ROWS - 1) * CP_WORD_COLS && p.top == 0);
        assert(cp_word_move(&p, 0, 1, 19) == 0);
        assert(p.top == 1 && p.cursor == (CP_WORD_ROWS - 1) * CP_WORD_COLS);

        /* It stops at the bottom of the list rather than scrolling into blanks. */
        for (i = 0; i < 20; i++) cp_word_move(&p, 0, 1, 19);
        assert(p.top == cp_word_rows(19) - CP_WORD_ROWS);
        /* Nineteen is odd, so the last row has one cell; the cursor cannot sit
           on the empty one beside it. */
        assert(cp_word_move(&p, 1, 0, 19) == 1);

        /* And back up to the top the same way. */
        for (i = 0; i < 20; i++) cp_word_move(&p, 0, -1, 19);
        assert(p.top == 0 && p.cursor / CP_WORD_COLS == 0);

        /* Arriving from either side lands on the row asked for, in the column
           nearest the edge it came through. */
        cp_word_enter(&p, 2, 0, 19);
        assert(p.box == CP_BOX_WORD && p.cursor == 2 * CP_WORD_COLS);
        cp_word_enter(&p, 2, 1, 19);
        assert(p.cursor == 2 * CP_WORD_COLS + 1);
        cp_word_enter(&p, 99, 1, 19);
        assert(p.cursor / CP_WORD_COLS == CP_WORD_ROWS - 1);

        /* An empty list leaves the cursor at rest rather than out of range. */
        cp_word_enter(&p, 3, 1, 0);
        assert(p.cursor == 0);
        assert(cp_word_move(&p, 0, 1, 0) == 0 && p.cursor == 0 && p.top == 0);
    }

    /* The overlay fills a noun slot, but is a viewer only when a verb is what
       the panel is waiting for -- picking a held object cannot start a
       sentence. */
    cp_reset(&p);
    cp_overlay_open(&p);
    assert(p.overlay == 1);
    assert(cp_overlay_takes_noun(&p) == 0);
    cp_overlay_close(&p);
    assert(p.overlay == 0);
    assert(p.line_len == 0);

    cp_pick(&p, "take", 0);
    assert(p.slot == CP_SLOT_NOUN);
    cp_overlay_open(&p);
    assert(cp_overlay_takes_noun(&p) == 1);
    cp_pick(&p, "lamp", 0);
    assert(strcmp(p.line, "take lamp") == 0);
    assert(p.overlay == 0);

    cp_reset(&p);
    assert(p.slot == CP_SLOT_VERB);
    cp_overlay_open(&p);
    assert(cp_overlay_takes_noun(&p) == 0);
    cp_pick(&p, "lamp", 0);
    assert(p.line_len == 0);
    assert(p.slot == CP_SLOT_VERB);
    assert(p.overlay == 0);

    cp_reset(&p);
    cp_pick(&p, "take", 0);
    assert(p.slot == CP_SLOT_NOUN);
    cp_overlay_open(&p);
    assert(cp_overlay_takes_noun(&p) == 1);
    cp_pick(&p, "lamp", 0);
    assert(strcmp(p.line, "take lamp") == 0);
    assert(p.slot == CP_SLOT_DONE);
    assert(p.overlay == 0);

    cp_reset(&p);
    cp_pick(&p, "take", 0);
    assert(p.slot == CP_SLOT_NOUN);
    cp_overlay_open(&p);
    assert(cp_overlay_takes_noun(&p) == 1);
    cp_pick(&p, 0, 0);
    assert(p.overlay == 0);
    assert(strcmp(p.line, "take") == 0);
    assert(p.slot == CP_SLOT_NOUN);

    /* cp_submit sends a line the grammar chain has not finished with. */
    cp_reset(&p);
    cp_pick(&p, "read", 0);
    assert(p.slot == CP_SLOT_NOUN);
    assert(p.submitted == 0);
    cp_submit(&p);
    assert(p.submitted == 1);
    assert(p.slot == CP_SLOT_DONE);
    assert(strcmp(p.line, "read") == 0);

    /* An empty line is not a command, so the button does nothing on one. */
    cp_reset(&p);
    cp_submit(&p);
    assert(p.submitted == 0);
    assert(p.slot == CP_SLOT_VERB);
    assert(p.line_len == 0);

    /* Backing up after a send takes the submit back with the word. */
    cp_reset(&p);
    cp_pick(&p, "read", 0);
    cp_submit(&p);
    assert(p.submitted == 1);
    cp_back(&p);
    assert(p.submitted == 0);
    assert(p.line_len == 0);

    printf("test_command_panel ok\n");
    return 0;
}
