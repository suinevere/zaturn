/*----------------------
 | glyph_invert.c
 | Description: The reverse-video tile transform and the scratch-slot cache
 |   described in glyph_invert.h.
 | Author: suinevere
 | Dependencies: glyph_invert.h
 ----------------------*/
#include "glyph_invert.h"

/*----------------------
 | GiSlot / g_slot / g_gen
 | Description: One scratch slot's occupant and the generation it was last
 |   asked for, plus the current generation. A slot whose gen is not the current
 |   one is free to reclaim; matching it means the slot is in use this frame and
 |   must not be handed to another character.
 | Author: suinevere
 ----------------------*/
typedef struct { char ch; unsigned int gen; int used; } GiSlot;
static GiSlot g_slot[GI_SLOT_N];
static unsigned int g_gen = 1;

void gi_invert_tile(const unsigned char *src, unsigned char *dst) {
    int i;
    for (i = 0; i < GI_TILE_BYTES; i++) {
        unsigned char hi = (unsigned char) ((src[i] >> 4) & 0x0f);
        unsigned char lo = (unsigned char) (src[i] & 0x0f);
        if (hi == 0) hi = 1; else if (hi == 1) hi = 2;
        if (lo == 0) lo = 1; else if (lo == 1) lo = 2;
        dst[i] = (unsigned char) ((hi << 4) | lo);
    }
}

void gi_begin_frame(void) {
    g_gen++;
}

int gi_slot_for(char c, int *is_new) {
    int i;
    for (i = 0; i < GI_SLOT_N; i++) {
        if (g_slot[i].used && g_slot[i].ch == c) {
            g_slot[i].gen = g_gen;
            if (is_new) *is_new = 0;
            return i;
        }
    }
    for (i = 0; i < GI_SLOT_N; i++) {
        if (!g_slot[i].used || g_slot[i].gen != g_gen) {
            g_slot[i].used = 1;
            g_slot[i].ch   = c;
            g_slot[i].gen  = g_gen;
            if (is_new) *is_new = 1;
            return i;
        }
    }
    if (is_new) *is_new = 0;
    return -1;
}

void gi_reset(void) {
    int i;
    for (i = 0; i < GI_SLOT_N; i++) { g_slot[i].used = 0; g_slot[i].ch = 0; g_slot[i].gen = 0; }
    g_gen = 1;
}
