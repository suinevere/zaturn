/*----------------------
 | options_blob.h
 | Description: The sound block inside the MOJOOPTS save blob, split out here
 |   because options.cxx pulls in srl.hpp and so cannot be reached by the host
 |   tests -- and this is the one part of that file where a mistake corrupts
 |   every field behind it rather than just its own.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef OPTIONS_BLOB_H
#define OPTIONS_BLOB_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | OPTS_SOUND_BLOCK_BYTES
 | Description: The block's width, unchanged from the dead form it replaces.
 |   Every field after it in the blob is positioned by counting from here, so
 |   this number is not free to grow: reclaiming or adding a byte would
 |   silently misparse every blob already written.
 | Author: suinevere
 ----------------------*/
#define OPTS_SOUND_BLOCK_BYTES 3

/*----------------------
 | OPTS_SOUND_SENTINEL
 | Description: Marks the block as carrying a synth level. 10 because display
 |   sentinels run 1-4, 6, 8 and 9 and gameplay uses 5 and 7, so this value can
 |   be mistaken for neither. Sentinel 1 is the dead form -- a mix mode and a
 |   track number -- and is still recognised so an older save is skipped
 |   rather than misread.
 | Author: suinevere
 ----------------------*/
#define OPTS_SOUND_SENTINEL 10

/*----------------------
 | opts_sound_block_encode
 | Description: Writes the three-byte sound block.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: buf -- at least OPTS_SOUND_BLOCK_BYTES writable; synth_level -- 0..7
 | Returns: N/A
 ----------------------*/
void opts_sound_block_encode(unsigned char *buf, int synth_level);

/*----------------------
 | opts_sound_block_decode
 | Description: Reads the three-byte sound block. Leaves *synth_level untouched
 |   for the dead form, an absent block, or an out-of-range value, so a blob
 |   that never carried a level comes back at the compiled default rather than
 |   at whatever those bytes happened to hold.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: buf -- at least OPTS_SOUND_BLOCK_BYTES readable; synth_level -- out
 | Returns: 1 when a level was read, 0 otherwise
 ----------------------*/
int opts_sound_block_decode(const unsigned char *buf, int *synth_level);

#ifdef __cplusplus
}
#endif
#endif /* OPTIONS_BLOB_H */
