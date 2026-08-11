/*----------------------
 | test_room_model.c
 | Description: Host test for the room model's static decode against the shipped
 |   Zork I image. Covers the direction-word to property-number map, the
 |   contiguous-run sanity gate that rejects a non-ZILCH story, the
 |   dictionary lookup the verb filter depends on, two malformed-header
 |   cases: an all-zero image (fails on the null pointer checks before the
 |   dictionary is ever touched) and a header with plausible in-range
 |   dict/obj/glob pointers but a dictionary separator-count byte of 0xFF
 |   (fails only on the g_dict + g_story[g_dict] + 4 > len guard -- without
 |   that guard, dict_entry_len/dict_count/dict_first would derive their
 |   values from bytes far past the end of the buffer) -- and the exit walk
 |   for object 81, "North of House": north, east, west, southeast and
 |   southwest are one-byte unconditional exits, south is the two-byte form
 |   that only prints the boarded-windows refusal, and the rest are absent --
 |   and a hand-built minimal story, valid enough to pass room_model_bind, whose
 |   sole room object has a one-byte north exit property positioned so its
 |   declared data byte falls exactly one past the bound story length (a
 |   recognizable value sits there in memory but outside g_len), proving the
 |   property walk stops instead of reading it. Reads saturn/zork1.dat
 |   directly; no SRL or Saturn code is involved.
 | Author: suinevere
 | Dependencies: ../src/engine/room_model.h and room_model.c, assert.h, stdio.h,
 |   stdlib.h, saturn/zork1.dat
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/trm.exe \
 |          saturn/tests/test_room_model.c saturn/src/engine/room_model.c \
 |          && /tmp/trm.exe
 ----------------------*/
#include "../src/engine/room_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned char *g_story;
static unsigned int   g_len;

static void load_story(void) {
    FILE *f = fopen("saturn/zork1.dat", "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_story = (unsigned char *) malloc((size_t) n);
    assert(g_story != NULL);
    assert(fread(g_story, 1, (size_t) n, f) == (size_t) n);
    fclose(f);
    g_len = (unsigned int) n;
}

int main(void) {
    load_story();

    assert(room_model_bind(g_story, g_len) == 1);
    assert(room_model_available() == 1);

    assert(room_model_dir_prop(RM_N)    == 31);
    assert(room_model_dir_prop(RM_E)    == 30);
    assert(room_model_dir_prop(RM_W)    == 29);
    assert(room_model_dir_prop(RM_S)    == 28);
    assert(room_model_dir_prop(RM_NE)   == 27);
    assert(room_model_dir_prop(RM_NW)   == 26);
    assert(room_model_dir_prop(RM_SE)   == 25);
    assert(room_model_dir_prop(RM_SW)   == 24);
    assert(room_model_dir_prop(RM_UP)   == 23);
    assert(room_model_dir_prop(RM_DOWN) == 22);
    assert(room_model_dir_prop(RM_IN)   == 21);
    assert(room_model_dir_prop(RM_OUT)  == 20);

    assert(room_model_has_word("open")    == 1);
    assert(room_model_has_word("mailbox") == 1);
    assert(room_model_has_word("photosynthesis") == 0);

    room_model_bind(g_story, g_len);
    room_model_refresh_room(81);
    {
        const RoomModel *m = room_model_get();
        assert(m->room == 81);
        assert(m->exits[RM_N]  == RM_EXIT_OPEN && m->dest[RM_N]  == 75);
        assert(m->exits[RM_E]  == RM_EXIT_OPEN && m->dest[RM_E]  == 79);
        assert(m->exits[RM_W]  == RM_EXIT_OPEN && m->dest[RM_W]  == 180);
        assert(m->exits[RM_SE] == RM_EXIT_OPEN && m->dest[RM_SE] == 79);
        assert(m->exits[RM_SW] == RM_EXIT_OPEN && m->dest[RM_SW] == 180);
        assert(m->exits[RM_S]  == RM_EXIT_BLOCKED);
        assert(m->exits[RM_NE] == RM_EXIT_NONE);
        assert(m->exits[RM_NW] == RM_EXIT_NONE);
        assert(m->exits[RM_UP] == RM_EXIT_NONE);
        assert(m->exits[RM_IN] == RM_EXIT_NONE);
    }

    /* Object 180 is "West of House". Its children are the door (181) and the
       mailbox (160), read straight from the object tree -- so they are known to
       be here without a word of text having been printed. */
    room_model_refresh_room(180);
    {
        const RoomModel *m = room_model_get();
        int saw_door = 0, saw_box = 0, i;
        assert(m->nhere == 2);
        for (i = 0; i < m->nhere; i++) {
            if (m->here[i] == 181) saw_door = 1;
            if (m->here[i] == 160) saw_box  = 1;
        }
        assert(saw_door == 1 && saw_box == 1);
    }

    /* Object 81 is "North of House" and holds nothing. */
    room_model_refresh_room(81);
    assert(room_model_get()->nhere == 0);

    /* The player is unknown until a room change lets the model intersect two
       rooms' child sets; nothing above depended on knowing it. */
    assert(room_model_player() == 0);

    {
        unsigned char img[160];
        int i;
        for (i = 0; i < 160; i++) img[i] = 0;

        img[0x08] = 0x00; img[0x09] = 16;
        img[0x0a] = 0x00; img[0x0b] = 64;
        img[0x0c] = 0x00; img[0x0d] = 48;

        img[16] = 0;
        img[17] = 6;
        img[18] = 0; img[19] = 4;

        img[20] = 0x4E; img[21] = 0x97; img[22] = 0x65; img[23] = 0xA0;
        img[24] = 0x10; img[25] = 31;
        img[26] = 0x28; img[27] = 0xD8; img[28] = 0x64; img[29] = 0x00;
        img[30] = 0x10; img[31] = 30;
        img[32] = 0x71; img[33] = 0x58; img[34] = 0x64; img[35] = 0x00;
        img[36] = 0x10; img[37] = 29;
        img[38] = 0x62; img[39] = 0x9A; img[40] = 0x65; img[41] = 0xA0;
        img[42] = 0x10; img[43] = 28;

        img[133] = 0x00; img[134] = 140;

        img[140] = 0;
        img[141] = 31;
        img[142] = 99;

        assert(room_model_bind(img, 142) == 1);
        assert(room_model_available() == 1);
        room_model_refresh_room(1);
        {
            const RoomModel *m = room_model_get();
            assert(m->exits[RM_N] == RM_EXIT_NONE);
            assert(m->dest[RM_N] == 0);
        }
    }

    unsigned char junk[64];
    for (int i = 0; i < 64; i++) junk[i] = 0;
    assert(room_model_bind(junk, sizeof junk) == 0);
    assert(room_model_available() == 0);

    unsigned char bad_dict[128];
    for (int i = 0; i < 128; i++) bad_dict[i] = 0;
    bad_dict[0x08] = 0; bad_dict[0x09] = 20;
    bad_dict[0x0a] = 0; bad_dict[0x0b] = 30;
    bad_dict[0x0c] = 0; bad_dict[0x0d] = 100;
    bad_dict[20] = 0xFF;
    assert(room_model_bind(bad_dict, sizeof bad_dict) == 0);
    assert(room_model_available() == 0);

    printf("test_room_model ok\n");
    return 0;
}
