/*----------------------
 | saturn_backup.cxx
 | Description: A thin wrapper over the SGL BUP (backup) library for saving,
 |   loading and deleting the game blob on the internal RAM, cartridge, or other
 |   backup devices. Handles one-time driver init, presence checks that never
 |   format (so a probe cannot wipe a cartridge), and formatting only at write
 |   time.
 | Author: suinevere
 | Dependencies: SRL, sega_bup.h (the SGL BUP API), saturn_backup.h
 ----------------------*/
#include <srl.hpp>
#include <sega_bup.h>
#include "saturn_backup.h"

/*----------------------
 | bup_lib / bup_work / bup_cfg / bup_ready
 | Description: The BUP driver workspaces (16 KB library + 8 KB work, sizes per Jo
 |   Engine's backup init), the per-device config table, and a one-time init flag.
 | Author: suinevere
 ----------------------*/
static uint32_t  bup_lib[16384 / 4];
static uint32_t  bup_work[8192 / 4];
static BupConfig bup_cfg[3];
static int       bup_ready = 0;

/*----------------------
 | copy_name
 | Description: Copies a filename into the BUP driver's fixed 12-byte field,
 |   zero-padded and truncated to 11 chars.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out -- the 12-byte destination; name -- the source filename
 | Returns: N/A
 ----------------------*/
static void copy_name(uint8_t out[12], const char *name) {
    int i;
    for (i = 0; i < 12; i++) out[i] = 0;
    for (i = 0; i < 11 && name[i]; i++) out[i] = (uint8_t) name[i];
}

/*----------------------
 | saturn_bup_init
 | Description: Initializes the BUP driver once; subsequent calls are no-ops.
 | Author: suinevere
 | Dependencies: sega_bup.h
 | Globals: bup_lib, bup_work, bup_cfg, bup_ready
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void saturn_bup_init(void) {
    if (bup_ready) return;
    BUP_Init((uint32_t *) bup_lib, bup_work, bup_cfg);
    bup_ready = 1;
}

/*----------------------
 | saturn_bup_present
 | Description: True when a device answers BUP_Stat (whether formatted or not);
 |   absent devices return BUP_NON/error. Never formats, so a mere presence check
 |   cannot wipe a cartridge.
 | Author: suinevere
 | Dependencies: sega_bup.h
 | Globals: bup_ready
 | Params: device -- the backup device id
 | Returns: 1 if present, 0 otherwise
 ----------------------*/
extern "C" int saturn_bup_present(int device) {
    if (!bup_ready) saturn_bup_init();
    BupStat st;
    int32_t s = BUP_Stat((uint32_t) device, 1, &st);
    return (s == 0 || s == BUP_UNFORMAT) ? 1 : 0;
}

/*----------------------
 | saturn_bup_space
 | Description: Reports what a `bytes`-sized record costs on a device and how many
 |   blocks that device has free, using the driver's own geometry rather than any
 |   assumption about it -- internal RAM and a cartridge report different block
 |   sizes, so a block count only means anything beside the free count from the
 |   same device.
 |
 |   The cost is the record's blocks plus one, because a file never packs into
 |   exactly its data's worth: its first block also carries the 30-byte directory
 |   header and the chain of block numbers that follows it. Rounding the overhead
 |   up to a whole block overstates the cost by at most one block and never
 |   understates it, which is the direction a "will this fit?" answer has to err
 |   in.
 |
 |   BUP_Stat is asked with the real size rather than a token 1, because the
 |   driver's free figures are computed against the size being asked about.
 | Author: suinevere
 | Dependencies: sega_bup.h
 | Globals: bup_ready
 | Params: device -- backup device id; bytes -- the record's data length;
 |   out_need -- receives the blocks that record would cost; out_free -- receives
 |   the blocks currently free
 | Returns: SATURN_BUP_MEASURED with both counts set, SATURN_BUP_UNFORMATTED for a
 |   device present but never formatted (counts left at 0), or SATURN_BUP_ABSENT
 ----------------------*/
extern "C" int saturn_bup_space(int device, uint32_t bytes, uint32_t *out_need,
                                uint32_t *out_free) {
    *out_need = 0;
    *out_free = 0;
    if (!bup_ready) saturn_bup_init();
    BupStat st;
    int32_t s = BUP_Stat((uint32_t) device, bytes, &st);
    if (s == BUP_UNFORMAT) return SATURN_BUP_UNFORMATTED;
    if (s != 0 || st.blocksize == 0) return SATURN_BUP_ABSENT;
    *out_need = (bytes + st.blocksize - 1) / st.blocksize + 1;
    *out_free = st.freeblock;
    return SATURN_BUP_MEASURED;
}

/*----------------------
 | saturn_bup_list
 | Description: Lists a device's filenames in one driver call, using BUP_Dir's "*"
 |   wildcard, so a caller can match them against names it knows rather than
 |   probing for each one in turn -- 160 name lookups against a full cartridge is
 |   seconds of boot, and this is one.
 |
 |   Returns 0 both for a device that holds nothing and for a driver that did not
 |   honour the wildcard, which a caller cannot tell apart and does not need to: an
 |   empty directory is also the cheapest thing to probe by name, so falling back
 |   on 0 costs nothing in the case that is genuinely empty.
 |
 |   A full table is also reported as 0. Listing the first `max` of more files than
 |   that would let a caller conclude "not here" from a list that never reached the
 |   end, and a wrong negative here is a warning shown to someone who did not need
 |   it.
 | Author: suinevere
 | Dependencies: sega_bup.h
 | Globals: bup_ready
 | Params: device -- backup device id; out -- receives NUL-terminated names of up
 |   to 11 chars; max -- capacity of out, capped at SATURN_BUP_LIST_MAX
 | Returns: the number of names written, or 0 if none were, the device is absent,
 |   or the device holds more files than the table could take
 ----------------------*/
extern "C" int saturn_bup_list(int device, char out[][12], int max) {
    if (!bup_ready) saturn_bup_init();
    if (max > SATURN_BUP_LIST_MAX) max = SATURN_BUP_LIST_MAX;
    if (max <= 0) return 0;

    BupStat st;
    int32_t s = BUP_Stat((uint32_t) device, 1, &st);
    if (s != 0) return 0;

    BupDir  tbl[SATURN_BUP_LIST_MAX];
    uint8_t pattern[12];
    copy_name(pattern, "*");
    int32_t n = BUP_Dir((uint32_t) device, pattern, (uint16_t) max, tbl);
    if (n <= 0 || n >= max) return 0;

    for (int32_t i = 0; i < n; i++) {
        int j = 0;
        for (; j < 11 && tbl[i].filename[j]; j++) out[i][j] = (char) tbl[i].filename[j];
        out[i][j] = 0;
    }
    return (int) n;
}

/*----------------------
 | saturn_bup_write
 | Description: Writes a named save to a device, formatting it first only if it is
 |   present but unformatted (so formatting happens at write time, never on a
 |   probe). Fills a BupDir with the name, comment, English language tag, and data
 |   size, and overwrites any existing file of the same name.
 | Author: suinevere
 | Dependencies: sega_bup.h
 | Globals: bup_ready
 | Params: device -- backup device id; name -- save filename; comment -- display
 |   comment; data -- blob bytes; len -- blob length
 | Returns: 1 on success, 0 on failure
 ----------------------*/
extern "C" int saturn_bup_write(int device, const char *name, const char *comment,
                                const uint8_t *data, uint32_t len) {
    if (!bup_ready) saturn_bup_init();
    BupStat st;
    int32_t s = BUP_Stat((uint32_t) device, 1, &st);
    if (s == BUP_UNFORMAT) { BUP_Format((uint32_t) device); s = BUP_Stat((uint32_t) device, 1, &st); }
    if (s != 0) return 0;
    BupDir dir;
    copy_name(dir.filename, name);
    int i;
    for (i = 0; i < 11; i++) dir.comment[i] = 0;
    for (i = 0; i < 10 && comment[i]; i++) dir.comment[i] = (uint8_t) comment[i];
    dir.language = BUP_ENGLISH;
    dir.date = 0;
    dir.datasize = len;
    dir.blocksize = 0;
    s = BUP_Write((uint32_t) device, &dir, (uint8_t *) data, 0);
    return (s == 0) ? 1 : 0;
}

/*----------------------
 | saturn_bup_read
 | Description: Reads a named save off a present device into `data` (the caller
 |   sizes the buffer to the record).
 | Author: suinevere
 | Dependencies: sega_bup.h
 | Globals: N/A
 | Params: device -- backup device id; name -- save filename; data -- destination
 | Returns: 1 on success, 0 if absent or unreadable
 ----------------------*/
extern "C" int saturn_bup_read(int device, const char *name, uint8_t *data) {
    if (!saturn_bup_present(device)) return 0;
    uint8_t fn[12];
    copy_name(fn, name);
    int32_t s = BUP_Read((uint32_t) device, fn, data);
    return (s == 0) ? 1 : 0;
}

/*----------------------
 | saturn_bup_delete
 | Description: Removes a named file from a present device. For a companion
 |   record whose write failed: leaving the old one behind pairs a stale map
 |   with a fresh save, and a later restore has no way to tell that it is stale
 |   and loads it as authoritative.
 | Author: suinevere
 | Dependencies: sega_bup.h
 | Globals: N/A
 | Params: device -- backup device id; name -- save filename
 | Returns: 1 when the file is gone, 0 when the device is absent or the driver
 |   refused (which includes a file that was never there)
 ----------------------*/
extern "C" int saturn_bup_delete(int device, const char *name) {
    if (!saturn_bup_present(device)) return 0;
    uint8_t fn[12];
    copy_name(fn, name);
    return (BUP_Delete((uint32_t) device, fn) == 0) ? 1 : 0;
}

/*----------------------
 | saturn_bup_info
 | Description: Reads a save's display comment (into a 10-char + NUL buffer)
 |   without loading its data, so the slot picker can show what a slot holds.
 | Author: suinevere
 | Dependencies: sega_bup.h
 | Globals: N/A
 | Params: device -- backup device id; name -- save filename; out_comment --
 |   receives the comment (blanked when absent)
 | Returns: 1 if the file exists, 0 otherwise
 ----------------------*/
extern "C" int saturn_bup_info(int device, const char *name, char *out_comment) {
    out_comment[0] = 0;
    if (!saturn_bup_present(device)) return 0;
    uint8_t fn[12];
    copy_name(fn, name);
    BupDir dir;
    int32_t n = BUP_Dir((uint32_t) device, fn, 1, &dir);
    if (n <= 0) return 0;
    int i;
    for (i = 0; i < 10; i++) out_comment[i] = (char) dir.comment[i];
    out_comment[10] = 0;
    return 1;
}
