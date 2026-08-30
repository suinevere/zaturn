/*----------------------
 | cgl.h
 | Description: Decoder for the Zork I (Saturn, Japan) room-background archives.
 |   A B*.CGL is a chain of 4-byte-aligned records, each a 256-entry RGB555
 |   little-endian CLUT followed by an Okumura-LZSS stream that expands to one
 |   320x240 8bpp frame. Pure logic: no SRL, no disc, no VDP2, so the host tests
 |   link it with plain gcc and the port can be proved before it ever runs on
 |   hardware.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef CGL_H
#define CGL_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | CGL_PAL_BYTES / CGL_WIDTH / CGL_HEIGHT / CGL_FRAME_BYTES / CGL_RING
 | Description: The fixed geometry of a CGL record and the size of the LZSS
 |   ring. Every frame on the disc is 320x240, which is why the client runs at
 |   320x240 rather than cropping.
 | Author: suinevere
 ----------------------*/
#define CGL_PAL_BYTES   512
#define CGL_WIDTH       320
#define CGL_HEIGHT      240
#define CGL_FRAME_BYTES (CGL_WIDTH * CGL_HEIGHT)
#define CGL_RING        4096

/*----------------------
 | cgl_decode
 | Description: Decompresses one record's LZSS stream into dst. Refuses rather
 |   than truncating: a null argument, a record too short to hold a palette and
 |   a size header, a declared size of zero, or a declared size larger than
 |   dst_cap all return 0 with dst untouched, which every caller reads as "hold
 |   the picture already showing".
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ring
 | Params: rec -- the record, starting at its CLUT; rec_len -- its byte length;
 |   dst -- destination for the 8bpp pixels; dst_cap -- capacity of dst
 | Returns: bytes written, or 0 on refusal
 ----------------------*/
unsigned long cgl_decode(const unsigned char *rec, unsigned long rec_len,
                         unsigned char *dst, unsigned long dst_cap);

/*----------------------
 | cgl_palette
 | Description: Converts a record's 256-entry CLUT to Saturn CRAM words. The two
 |   formats share a channel layout -- red in bits 0-4, green 5-9, blue 10-14 --
 |   so the whole conversion is a little-endian read and the opaque bit, with no
 |   channel arithmetic at all.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: rec -- the record, starting at its CLUT; out -- 256 words to write
 | Returns: N/A
 ----------------------*/
void cgl_palette(const unsigned char *rec, unsigned short *out);

#ifdef __cplusplus
}
#endif
#endif /* CGL_H */
