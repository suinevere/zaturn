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
 |   must not be handed to another character. pending marks a slot whose tile
 |   has not yet reached VRAM -- set when gi_slot_for claims or reassigns a
 |   slot, cleared when gi_pending_next hands it to the caller.
 | Author: suinevere
 ----------------------*/
typedef struct { char ch; unsigned int gen; int used; int pending; } GiSlot;
static GiSlot g_slot[GI_SLOT_N];
static unsigned int g_gen = 1;

/*----------------------
 | gi_invert_tile
 | Description: Writes the reverse-video form of one 8x8 4bpp tile: pixel value
 |   0 (the transparent background) becomes 1 (the ink colour) and value 1
 |   becomes 2 (the highlight letter colour), so the cell paints a solid block
 |   with the letter punched out. Other values pass through untouched.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: src -- GI_TILE_BYTES source tile; dst -- GI_TILE_BYTES destination
 | Returns: N/A
 ----------------------*/
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

/*----------------------
 | gi_begin_frame
 | Description: Opens a new allocation generation. Slots claimed in an earlier
 |   generation become reusable; slots re-requested this generation keep their
 |   character and need no tile rewrite. Callers must drain every pending slot
 |   (gi_pending_next) before calling this -- opening the next generation
 |   first would make a slot still awaiting its tile write reclaimable before
 |   that write happens.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_gen
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void gi_begin_frame(void) {
    g_gen++;
}

/*----------------------
 | gi_slot_for
 | Description: The scratch slot holding `c`'s inverted glyph, claiming one if
 |   it does not already hold it. Performs no VRAM access itself: claiming or
 |   reassigning a slot only marks it pending, for gi_pending_next to hand off
 |   later. Sets *is_new when the slot was newly claimed or reassigned this
 |   call; clears it when the slot already carried `c` this generation.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_slot, g_gen
 | Params: c -- character to invert; is_new -- (out, may be NULL) 1 if the
 |   slot was newly claimed or reassigned
 | Returns: the slot index in 0..GI_SLOT_N-1, or -1 when every slot is spoken
 |   for this generation
 ----------------------*/
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
            g_slot[i].pending = 1;
            if (is_new) *is_new = 1;
            return i;
        }
    }
    if (is_new) *is_new = 0;
    return -1;
}

/*----------------------
 | gi_pending_next
 | Description: Pops one scratch slot still awaiting its tile write -- the
 |   deferred half of what gi_slot_for used to perform inline, before the fix
 |   that moved every glyph-invert VRAM write into the vblank flush alongside
 |   the map copy, so no tile write can land mid-frame. Drains in slot order;
 |   call repeatedly until it returns 0.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_slot
 | Params: slot -- (out) the slot index; ch -- (out) the character it now holds
 | Returns: 1 if a pending slot was returned, 0 when none remain
 ----------------------*/
int gi_pending_next(int *slot, char *ch) {
    int i;
    for (i = 0; i < GI_SLOT_N; i++) {
        if (g_slot[i].used && g_slot[i].pending) {
            g_slot[i].pending = 0;
            if (slot) *slot = i;
            if (ch) *ch = g_slot[i].ch;
            return 1;
        }
    }
    return 0;
}

/*----------------------
 | gi_reset
 | Description: Forgets every slot assignment, so the next generation starts
 |   from an empty cache. Used at init and by tests.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_slot, g_gen
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void gi_reset(void) {
    int i;
    for (i = 0; i < GI_SLOT_N; i++) {
        g_slot[i].used = 0;
        g_slot[i].ch = 0;
        g_slot[i].gen = 0;
        g_slot[i].pending = 0;
    }
    g_gen = 1;
}
