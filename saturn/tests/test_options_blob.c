/* The sound block in the MOJOOPTS blob.

   The blob is strictly positional -- every field after this block is located
   by counting from it -- and it already carries a dead three-byte block:
   sentinel 1, then a mix mode and a track number, both settings long gone and
   kept only so the count still works. The synth level moves into those bytes
   under a new sentinel, which is why the width must not change and why a
   sentinel-1 blob must still be skipped exactly as before.

   Getting this wrong does not lose the music level. It silently misparses
   every block behind it -- the controller mapping, the gameplay block, the
   display block -- which is why the width is asserted as hard as the value.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/menu -o /tmp/t_blob \
         saturn/tests/test_options_blob.c saturn/src/menu/options_blob.c \
         && /tmp/t_blob
*/
#include "../src/menu/options_blob.h"
#include <stdio.h>
#include <assert.h>

static void test_round_trip(void) {
    unsigned char buf[8] = { 0 };
    int level = -1;
    opts_sound_block_encode(buf, 6);
    assert(opts_sound_block_decode(buf, &level) == 1);
    assert(level == 6);
}

static void test_block_is_exactly_three_bytes(void) {
    unsigned char buf[8];
    for (int i = 0; i < 8; i++) buf[i] = 0xAA;
    opts_sound_block_encode(buf, 3);
    assert(buf[OPTS_SOUND_BLOCK_BYTES] == 0xAA);
    assert(OPTS_SOUND_BLOCK_BYTES == 3);
}

static void test_legacy_sentinel_one_is_skipped_not_read(void) {
    /* An old blob's mix mode and track number must not be mistaken for a
       level. Decode reports "not mine" and leaves the caller's value alone. */
    unsigned char buf[3] = { 1, 2, 5 };
    int level = 4;
    assert(opts_sound_block_decode(buf, &level) == 0);
    assert(level == 4);
}

static void test_an_absent_block_leaves_the_default(void) {
    unsigned char buf[3] = { 0, 0, 0 };
    int level = 4;
    assert(opts_sound_block_decode(buf, &level) == 0);
    assert(level == 4);
}

static void test_out_of_range_level_is_rejected(void) {
    unsigned char buf[3] = { 10, 99, 0 };
    int level = 4;
    assert(opts_sound_block_decode(buf, &level) == 0);
    assert(level == 4);
}

static void test_every_valid_level_survives(void) {
    for (int l = 0; l <= 7; l++) {
        unsigned char buf[3] = { 0 };
        int got = -1;
        opts_sound_block_encode(buf, l);
        assert(opts_sound_block_decode(buf, &got) == 1);
        assert(got == l);
    }
}

static void test_encoded_sentinel_is_ten(void) {
    /* 10 is chosen because display sentinels use 1-4, 6, 8 and 9 and gameplay
       uses 5 and 7, so it can be mistaken for none of them. */
    unsigned char buf[3] = { 0 };
    opts_sound_block_encode(buf, 0);
    assert(buf[0] == 10);
}

static void test_out_of_range_input_is_clamped_not_stored_raw(void) {
    /* A caller handing in a level past the slider's top must not write a byte
       that decode would then reject, which would silently lose the setting. */
    unsigned char buf[3] = { 0 };
    int got = -1;
    opts_sound_block_encode(buf, 99);
    assert(opts_sound_block_decode(buf, &got) == 1);
    assert(got == 7);
}

int main(void) {
    test_round_trip();
    test_block_is_exactly_three_bytes();
    test_legacy_sentinel_one_is_skipped_not_read();
    test_an_absent_block_leaves_the_default();
    test_out_of_range_level_is_rejected();
    test_every_valid_level_survives();
    test_encoded_sentinel_is_ten();
    test_out_of_range_input_is_clamped_not_stored_raw();
    printf("test_options_blob: all passed\n");
    return 0;
}
