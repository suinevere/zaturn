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

        /* Nine fits with a cell to spare and shows no marker; cells hold the
           candidates in order and the spare cell is null. */
        cp_fill(c, 9, 0, &w);
        assert(w.n == 9 && w.more == 0);
        for (j = 0; j < 9; j++) assert(strcmp(w.word[j], c[j]) == 0);
        assert(w.word[9] == 0);

        /* Ten fills every cell exactly, in order, and still shows no marker. */
        cp_fill(c, 10, 0, &w);
        assert(w.n == 10 && w.more == 0);
        for (j = 0; j < 10; j++) assert(strcmp(w.word[j], c[j]) == 0);

        /* Eleven cannot fit, so the last cell becomes the marker; page 0
           carries candidates 0..8 in order and page 1 carries 9..10, with
           the cells past n left null. */
        cp_fill(c, 11, 0, &w);
        assert(w.n == 9 && w.more == 1);
        for (j = 0; j < 9; j++) assert(strcmp(w.word[j], c[j]) == 0);
        assert(w.word[9] == 0);
        cp_fill(c, 11, 1, &w);
        assert(w.n == 2 && w.more == 0);
        assert(strcmp(w.word[0], c[9]) == 0);
        assert(strcmp(w.word[1], c[10]) == 0);
        for (j = 2; j < CP_WORD_CELLS; j++) assert(w.word[j] == 0);

        assert(cp_pages(9)  == 1);
        assert(cp_pages(10) == 1);
        assert(cp_pages(11) == 2);
        assert(cp_pages(19) == 3);

        /* Walking every page of nineteen candidates concatenates back to
           candidates 0..18 in order, with no gaps and no repeats, and each
           page leaves its unused cells null. */
        {
            int idx = 0;
            int pg, pages = cp_pages(19);
            for (pg = 0; pg < pages; pg++) {
                cp_fill(c, 19, pg, &w);
                for (j = 0; j < w.n; j++) {
                    assert(strcmp(w.word[j], c[idx]) == 0);
                    idx++;
                }
                for (j = w.n; j < CP_WORD_CELLS; j++) assert(w.word[j] == 0);
            }
            assert(idx == 19);
        }
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

    printf("test_command_panel ok\n");
    return 0;
}
