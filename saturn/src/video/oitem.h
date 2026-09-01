/*----------------------
 | oitem.h
 | Description: Decoder for the Zork I (Saturn, Japan) item-picture container.
 |   OITEM.CZ is a flat chain of 4-byte-aligned Okumura-LZSS records: nineteen
 |   64x80 8bpp pictures followed by nineteen 256-entry RGB555 CLUTs, one per
 |   picture. Pure logic: no SRL, no disc, no VDP2, so the host tests link it
 |   with plain gcc and the port can be proved before it ever runs on hardware.
 |   The record offsets are generated rather than scanned -- see
 |   scene/oitem_records.inc -- because scanning means decompressing every
 |   earlier record to reach record n.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef OITEM_H
#define OITEM_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | OITEM_PIC_N / OITEM_PIC_BYTES / OITEM_PAL_BYTES / OITEM_WIDTH / OITEM_HEIGHT
 | Description: The picture count and geometry from oitem_records.inc, copied
 |   here so a caller can size a buffer without pulling the record table into
 |   its translation unit. Deliberately left unguarded: oitem.c includes the
 |   .inc ahead of this header, so if the two ever disagree the preprocessor's
 |   redefinition diagnostic is the drift check -- a guard here would silence
 |   exactly that, since an identical redefinition is silent but a differing
 |   one is not.
 | Author: suinevere
 ----------------------*/
#define OITEM_PIC_N     19
#define OITEM_PIC_BYTES 5120
#define OITEM_PAL_BYTES 512

#define OITEM_WIDTH  64
#define OITEM_HEIGHT 80

/*----------------------
 | oitem_count
 | Description: How many item pictures the container holds.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: OITEM_PIC_N
 ----------------------*/
int oitem_count(void);

/*----------------------
 | oitem_decode
 | Description: Expands one picture and its own palette out of the archive.
 |   On refusal clut is always untouched, but pixels may hold a partial
 |   decode -- cgl_lzss writes as it unpacks and only reports the short count
 |   afterwards -- so a caller must not read pixels after a 0 return; every
 |   caller instead reads a 0 as "hold the picture already showing".
 | Author: suinevere
 | Dependencies: cgl.h, scene/oitem_records.inc
 | Globals: OITEM_RECORDS
 | Params: archive -- the whole OITEM.CZ; archive_len -- its byte length;
 |   picture -- 0..OITEM_PIC_N-1; pixels -- receives OITEM_PIC_BYTES;
 |   clut -- receives 256 Saturn CRAM words
 | Returns: 1 on success, 0 on refusal
 ----------------------*/
int oitem_decode(const unsigned char *archive, unsigned long archive_len,
                 int picture, unsigned char *pixels, unsigned short *clut);

#ifdef __cplusplus
}
#endif
#endif /* OITEM_H */
