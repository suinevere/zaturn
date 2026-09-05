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
    opts_sound_block_encode(buf, 6, 0);
    assert(opts_sound_block_decode(buf, &level, 0) == 1);
    assert(level == 6);
}

static void test_block_is_exactly_three_bytes(void) {
    unsigned char buf[8];
    for (int i = 0; i < 8; i++) buf[i] = 0xAA;
    opts_sound_block_encode(buf, 3, 0);
    assert(buf[OPTS_SOUND_BLOCK_BYTES] == 0xAA);
    assert(OPTS_SOUND_BLOCK_BYTES == 3);
}

static void test_legacy_sentinel_one_is_skipped_not_read(void) {
    /* An old blob's mix mode and track number must not be mistaken for a
       level. Decode reports "not mine" and leaves the caller's value alone. */
    unsigned char buf[3] = { 1, 2, 5 };
    int level = 4;
    assert(opts_sound_block_decode(buf, &level, 0) == 0);
    assert(level == 4);
}

static void test_an_absent_block_leaves_the_default(void) {
    unsigned char buf[3] = { 0, 0, 0 };
    int level = 4;
    assert(opts_sound_block_decode(buf, &level, 0) == 0);
    assert(level == 4);
}

static void test_out_of_range_level_is_rejected(void) {
    unsigned char buf[3] = { 10, 99, 0 };
    int level = 4;
    assert(opts_sound_block_decode(buf, &level, 0) == 0);
    assert(level == 4);
}

static void test_every_valid_level_survives(void) {
    for (int l = 0; l <= 7; l++) {
        unsigned char buf[3] = { 0 };
        int got = -1;
        opts_sound_block_encode(buf, l, 0);
        assert(opts_sound_block_decode(buf, &got, 0) == 1);
        assert(got == l);
    }
}

static void test_encoded_sentinel_is_ten(void) {
    /* 10 is chosen because display sentinels use 1-4, 6, 8 and 9 and gameplay
       uses 5 and 7, so it can be mistaken for none of them. */
    unsigned char buf[3] = { 0 };
    opts_sound_block_encode(buf, 0, 0);
    assert(buf[0] == 10);
}

static void test_the_source_round_trips_in_the_spare_byte(void) {
    for (int src = 0; src <= 1; src++) {
        unsigned char buf[3] = { 0 };
        int level = -1, got = -1;
        opts_sound_block_encode(buf, 5, src);
        assert(opts_sound_block_decode(buf, &level, &got) == 1);
        assert(level == 5);
        assert(got == src);
    }
}

static void test_a_blob_written_before_the_source_existed_reads_as_cd(void) {
    /* The third byte was a hard zero for the whole life of this block, and CD is
       0 -- which is the entire reason the source could be added without moving
       the sentinel or the width. Every cartridge already written has to come
       back playing what it was playing. */
    unsigned char buf[3] = { OPTS_SOUND_SENTINEL, 5, 0 };
    int level = -1, src = 1;
    assert(opts_sound_block_decode(buf, &level, &src) == 1);
    assert(level == 5);
    assert(src == 0);
}

static void test_a_third_byte_that_is_neither_source_is_left_alone(void) {
    /* Not a source, so it is a byte from somewhere else -- a misaligned blob,
       or one written by something this does not know about. The caller keeps
       what it had rather than being handed a number as a setting. */
    unsigned char buf[3] = { OPTS_SOUND_SENTINEL, 5, 200 };
    int level = -1, src = 1;
    assert(opts_sound_block_decode(buf, &level, &src) == 1);
    assert(level == 5);
    assert(src == 1);
}

static void test_a_null_source_pointer_is_accepted(void) {
    /* The level is the older field and there are callers that only want it. */
    unsigned char buf[3] = { 0 };
    int level = -1;
    opts_sound_block_encode(buf, 2, 1);
    assert(opts_sound_block_decode(buf, &level, 0) == 1);
    assert(level == 2);
}

static void test_out_of_range_input_is_clamped_not_stored_raw(void) {
    /* A caller handing in a level past the slider's top must not write a byte
       that decode would then reject, which would silently lose the setting. */
    unsigned char buf[3] = { 0 };
    int got = -1;
    opts_sound_block_encode(buf, 99, 0);
    assert(opts_sound_block_decode(buf, &got, 0) == 1);
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
    test_the_source_round_trips_in_the_spare_byte();
    test_a_blob_written_before_the_source_existed_reads_as_cd();
    test_a_third_byte_that_is_neither_source_is_left_alone();
    test_a_null_source_pointer_is_accepted();
    test_out_of_range_input_is_clamped_not_stored_raw();
    printf("test_options_blob: all passed\n");
    return 0;
}
