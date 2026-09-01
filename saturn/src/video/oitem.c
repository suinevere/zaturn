/*----------------------
 | oitem.c
 | Description: See oitem.h. The container layout; the codec itself is
 |   cgl_lzss and the palette conversion is cgl_palette, both reused unchanged
 |   because the two formats share them exactly.
 | Author: suinevere
 | Dependencies: oitem.h, cgl.h, scene/oitem_records.inc
 | Globals: OITEM_RECORDS
 ----------------------*/
#include "scene/oitem_records.inc"
#include "video/oitem.h"
#include "video/cgl.h"

/*----------------------
 | oitem_count
 | Description: See oitem.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: OITEM_PIC_N
 ----------------------*/
int oitem_count(void) { return OITEM_PIC_N; }

/*----------------------
 | rec_ok
 | Description: Whether a record index names a record wholly inside the
 |   archive. Checked before either decode rather than trusting the generated
 |   table against whatever bytes were actually read off the disc.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: OITEM_RECORDS
 | Params: idx -- record index; archive_len -- bytes available
 | Returns: 1 when the record fits
 ----------------------*/
static int rec_ok(int idx, unsigned long archive_len) {
    if (idx < 0 || idx >= OITEM_RECORD_N) return 0;
    return OITEM_RECORDS[idx].offset + OITEM_RECORDS[idx].length <= archive_len;
}

/*----------------------
 | oitem_decode
 | Description: See oitem.h.
 | Author: suinevere
 | Dependencies: cgl.h
 | Globals: OITEM_RECORDS
 | Params: archive, archive_len, picture, pixels, clut -- see oitem.h
 | Returns: 1 on success, 0 on refusal
 ----------------------*/
int oitem_decode(const unsigned char *archive, unsigned long archive_len,
                 int picture, unsigned char *pixels, unsigned short *clut) {
    static unsigned char pal[OITEM_PAL_BYTES];
    const OitemRecord *pr;
    const OitemRecord *cr;

    if (archive == 0 || pixels == 0 || clut == 0) return 0;
    if (picture < 0 || picture >= OITEM_PIC_N) return 0;
    if (!rec_ok(picture, archive_len)) return 0;
    if (!rec_ok(picture + OITEM_PIC_N, archive_len)) return 0;

    pr = &OITEM_RECORDS[picture];
    cr = &OITEM_RECORDS[picture + OITEM_PIC_N];

    if (cgl_lzss(archive + cr->offset, cr->length, pal,
                 (unsigned long) OITEM_PAL_BYTES)
        != (unsigned long) OITEM_PAL_BYTES) return 0;

    if (cgl_lzss(archive + pr->offset, pr->length, pixels,
                 (unsigned long) OITEM_PIC_BYTES)
        != (unsigned long) OITEM_PIC_BYTES) return 0;

    cgl_palette(pal, clut);
    return 1;
}
