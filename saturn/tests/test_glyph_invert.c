/*----------------------
 | test_glyph_invert.c
 | Description: Host test for the inverted-glyph transform and its scratch-slot
 |   cache. The transform maps a 4bpp font tile's background (pixel value 0) to
 |   the ink colour and its ink (value 1) to CRAM entry 2, producing reverse
 |   video. The cache hands each character one of the 32 control-code tile slots
 |   font 0 already owns, reusing a slot across frames so a steady selection
 |   costs no VRAM writes. No SRL or Saturn code is involved.
 | Author: suinevere
 | Dependencies: ../src/video/glyph_invert.h and glyph_invert.c, assert.h, stdio.h
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/tgi.exe \
 |          saturn/tests/test_glyph_invert.c saturn/src/video/glyph_invert.c \
 |          && /tmp/tgi.exe
 ----------------------*/
#include "../src/video/glyph_invert.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    unsigned char src[GI_TILE_BYTES], dst[GI_TILE_BYTES];
    for (int i = 0; i < GI_TILE_BYTES; i++) src[i] = 0x01;
    gi_invert_tile(src, dst);
    for (int i = 0; i < GI_TILE_BYTES; i++) assert(dst[i] == 0x12);

    for (int i = 0; i < GI_TILE_BYTES; i++) src[i] = 0x10;
    gi_invert_tile(src, dst);
    for (int i = 0; i < GI_TILE_BYTES; i++) assert(dst[i] == 0x21);

    gi_reset();
    gi_begin_frame();
    int is_new = 0;
    int a = gi_slot_for('A', &is_new);
    assert(a >= 0 && a < GI_SLOT_N && is_new == 1);
    int a2 = gi_slot_for('A', &is_new);
    assert(a2 == a && is_new == 0);

    int b = gi_slot_for('B', &is_new);
    assert(b != a && is_new == 1);

    gi_begin_frame();
    int a3 = gi_slot_for('A', &is_new);
    assert(a3 == a && is_new == 0);

    gi_reset();
    gi_begin_frame();
    for (int i = 0; i < GI_SLOT_N; i++) {
        int s = gi_slot_for((char) ('a' + i), &is_new);
        assert(s >= 0 && is_new == 1);
    }
    assert(gi_slot_for('!', &is_new) == -1);

    printf("test_glyph_invert ok\n");
    return 0;
}
