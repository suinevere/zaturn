/*----------------------
 | save_blob.c
 | Description: See save_blob.h.
 | Author: suinevere
 | Dependencies: string.h, save_blob.h
 | Globals: N/A
 ----------------------*/
#include <string.h>
#include "save_blob.h"

/*----------------------
 | rd32
 | Description: A little-endian 32-bit field out of the header, which is how
 |   opcode_save writes every multi-byte value in it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- the field's first byte
 | Returns: the value
 ----------------------*/
static unsigned int rd32(const unsigned char *p) {
    return (unsigned int) p[0] | ((unsigned int) p[1] << 8)
         | ((unsigned int) p[2] << 16) | ((unsigned int) p[3] << 24);
}

/*----------------------
 | save_blob_len
 | Description: See save_blob.h.
 | Author: suinevere
 | Dependencies: string.h (memcmp), rd32
 | Globals: N/A
 | Params: blob, cap -- see save_blob.h
 | Returns: the story blob's length, or 0
 ----------------------*/
unsigned int save_blob_len(const unsigned char *blob, unsigned int cap) {
    unsigned int spoff, rle, len;
    if (blob == 0 || cap < SAVE_BLOB_HEADER) return 0;
    if (memcmp(blob, "MZSV1", 5) != 0) return 0;
    spoff = rd32(blob + 11);
    rle   = rd32(blob + 17);
    /* Each half is checked against cap before they are added, so a pair of
       lengths large enough to wrap cannot come out looking small. */
    if (rle > cap || spoff > cap / 2u) return 0;
    len = SAVE_BLOB_HEADER + rle + spoff * 2u;
    return (len <= cap) ? len : 0;
}
