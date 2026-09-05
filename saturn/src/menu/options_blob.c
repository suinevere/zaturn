/*----------------------
 | options_blob.c
 | Description: Implementation of the MOJOOPTS sound block.
 | Author: suinevere
 | Dependencies: options_blob.h
 ----------------------*/
#include "options_blob.h"

void opts_sound_block_encode(unsigned char *buf, int synth_level) {
    if (synth_level < 0) synth_level = 0;
    if (synth_level > 7) synth_level = 7;
    buf[0] = OPTS_SOUND_SENTINEL;
    buf[1] = (unsigned char) synth_level;
    buf[2] = 0;
}

int opts_sound_block_decode(const unsigned char *buf, int *synth_level) {
    if (buf[0] != OPTS_SOUND_SENTINEL) return 0;
    if (buf[1] > 7) return 0;
    *synth_level = (int) buf[1];
    return 1;
}
