/*----------------------
 | save_blob.h
 | Description: Where the story blob in a save record ends. One backup record per
 |   slot now holds the Z-machine save and the map after it (see saturn_glue.h),
 |   and this is what finds the seam between them.
 |
 |   Its own file, and pure C, because it is the one piece of that scheme that can
 |   be got wrong quietly: a length too long hands map_model_deserialize the middle
 |   of a Z-machine stack, and a length too short loses the map. saturn_glue.cxx
 |   cannot be built on the host, so the logic lives here where a test can reach
 |   it. Implemented in save_blob.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef SAVE_BLOB_H
#define SAVE_BLOB_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SAVE_BLOB_HEADER
 | Description: opcode_save's fixed header: magic "MZSV1", dynlen(2), pc(4),
 |   sp(4), bp(2), rle_len(4). The delta follows it and the used stack follows
 |   that, two bytes an entry. SAVE_BLOB_MAX in saturn_glue.h is derived from the
 |   same layout.
 | Author: suinevere
 ----------------------*/
#define SAVE_BLOB_HEADER 21u

/*----------------------
 | save_blob_len
 | Description: The length of the story blob at the front of `blob`, read out of
 |   its own header. Refuses rather than guessing: a blob without the magic, or
 |   whose declared lengths run past `cap`, answers 0, which every caller reads
 |   as "no seam here" -- leaving the story half untouched and the map half
 |   unread. A save with nothing appended answers its own whole length, which is
 |   the correct answer and simply leaves no tail to look at.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: blob -- the record's first byte, or null; cap -- how many bytes of it
 |   are readable
 | Returns: the story blob's length in bytes, or 0 when it cannot be trusted
 ----------------------*/
unsigned int save_blob_len(const unsigned char *blob, unsigned int cap);

#ifdef __cplusplus
}
#endif
#endif /* SAVE_BLOB_H */
