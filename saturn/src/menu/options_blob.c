/*----------------------
 | options_blob.c
 | Description: Implementation of the MOJOOPTS sound block.
 | Author: suinevere
 | Dependencies: options_blob.h
 ----------------------*/
#include "options_blob.h"

void opts_sound_block_encode(unsigned char *buf, int synth_level, int source) {
    if (synth_level < 0) synth_level = 0;
    if (synth_level > 7) synth_level = 7;
    buf[0] = OPTS_SOUND_SENTINEL;
    buf[1] = (unsigned char) synth_level;
    buf[2] = (unsigned char) (source ? 1 : 0);
}

int opts_sound_block_decode(const unsigned char *buf, int *synth_level, int *source) {
    if (buf[0] != OPTS_SOUND_SENTINEL) return 0;
    if (buf[1] > 7) return 0;
    *synth_level = (int) buf[1];
    /* The third byte was written as a hard zero by every version of this block
       before the source was a choice, and zero is CD -- so an older blob comes
       back as the source it was actually played with, and no sentinel had to
       move. A value that is neither 0 nor 1 is a byte from somewhere else and
       is left alone rather than trusted. */
    if (source && buf[2] <= 1) *source = (int) buf[2];
    return 1;
}
