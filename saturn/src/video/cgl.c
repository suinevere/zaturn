/*----------------------
 | cgl.c
 | Description: See cgl.h. The LZSS is the Okumura variant the disc uses for
 |   *.CGZ and *.SLD alike, ported from analysis/zork_cgl.py.
 | Author: suinevere
 | Dependencies: cgl.h
 | Globals: g_ring
 ----------------------*/
#include "cgl.h"

/*----------------------
 | g_ring
 | Description: The LZSS window. A file-scope static rather than a local because
 |   4 KiB is more stack than a Saturn frame should carry, and nothing here is
 |   re-entrant.
 | Author: suinevere
 ----------------------*/
static unsigned char g_ring[CGL_RING];

/*----------------------
 | cgl_decode
 | Description: See cgl.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ring
 | Params: rec, rec_len, dst, dst_cap -- see cgl.h
 | Returns: bytes written, or 0 on refusal
 ----------------------*/
unsigned long cgl_decode(const unsigned char *rec, unsigned long rec_len,
                         unsigned char *dst, unsigned long dst_cap) {
    unsigned long size, out = 0, i;
    unsigned int flags = 0, nbits = 0, r = CGL_RING - 18;

    if (rec == 0 || dst == 0) return 0;
    if (rec_len < (unsigned long) CGL_PAL_BYTES + 4) return 0;

    size = (unsigned long) rec[CGL_PAL_BYTES]
         | ((unsigned long) rec[CGL_PAL_BYTES + 1] << 8)
         | ((unsigned long) rec[CGL_PAL_BYTES + 2] << 16)
         | ((unsigned long) rec[CGL_PAL_BYTES + 3] << 24);
    if (size == 0 || size > dst_cap) return 0;

    for (i = 0; i < CGL_RING; i++) g_ring[i] = 0;

    i = (unsigned long) CGL_PAL_BYTES + 4;
    while (i < rec_len && out < size) {
        unsigned int bit;
        if (nbits == 0) {
            flags = rec[i++];
            nbits = 8;
            if (i >= rec_len) break;
        }
        bit = flags & 1u; flags >>= 1; nbits--;
        if (bit) {
            unsigned char c = rec[i++];
            dst[out++] = c;
            g_ring[r] = c; r = (r + 1u) & (CGL_RING - 1u);
        } else {
            unsigned int off, len, k;
            if (i + 1 >= rec_len) break;
            off = ((unsigned int) (rec[i + 1] & 0xf0u) << 4) | (unsigned int) rec[i];
            len = (unsigned int) (rec[i + 1] & 0x0fu) + 3u;
            i += 2;
            for (k = 0; k < len && out < size; k++) {
                unsigned char c = g_ring[(off + k) & (CGL_RING - 1u)];
                dst[out++] = c;
                g_ring[r] = c; r = (r + 1u) & (CGL_RING - 1u);
            }
        }
    }
    return out;
}

/*----------------------
 | cgl_palette
 | Description: See cgl.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: rec -- the record; out -- 256 words to write
 | Returns: N/A
 ----------------------*/
void cgl_palette(const unsigned char *rec, unsigned short *out) {
    int i;
    if (rec == 0 || out == 0) return;
    for (i = 0; i < 256; i++) {
        unsigned int v = (unsigned int) rec[i * 2]
                       | ((unsigned int) rec[i * 2 + 1] << 8);
        out[i] = (unsigned short) (0x8000u | (v & 0x7fffu));
    }
}
