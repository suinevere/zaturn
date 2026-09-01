/*----------------------
 | saturn_backup.h
 | Description: Save/load the game blob to Saturn backup devices (internal RAM,
 |   cartridge) via the SGL/BIOS backup driver: init, presence check, write, read,
 |   delete, and comment-only info. Implemented in saturn_backup.cxx.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef SATURN_BACKUP_H
#define SATURN_BACKUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SATURN_BUP_CONSOLE / SATURN_BUP_CARTRIDGE
 | Description: Device numbers for the BIOS backup driver (0-based, matching Jo
 |   Engine): 0 = internal console backup, 1 = cartridge. sega_bup.h's
 |   BUP_MAIN_UNIT(1)/BUP_CURTRIDGE(2) are for the separate SBL library and are off
 |   by one for this driver.
 | Author: suinevere
 ----------------------*/
#define SATURN_BUP_CONSOLE    0
#define SATURN_BUP_CARTRIDGE  1

/*----------------------
 | saturn_bup_init
 | Description: Initializes the SGL/BIOS backup driver. Call once at boot.
 | Author: suinevere
 ----------------------*/
void saturn_bup_init(void);

/*----------------------
 | saturn_bup_present
 | Description: 1 if the device is present and usable (formatting it first if
 |   present but unformatted); used to hide the cartridge option when absent.
 | Author: suinevere
 ----------------------*/
int  saturn_bup_present(int device);

/*----------------------
 | SATURN_BUP_ABSENT / SATURN_BUP_MEASURED / SATURN_BUP_UNFORMATTED
 | Description: saturn_bup_space's three answers. UNFORMATTED is not folded into
 |   ABSENT because a never-formatted device cannot be asked for its geometry
 |   without formatting it -- which no probe in this file may do -- and is by
 |   definition empty, so a caller asking whether a first save fits should read it
 |   as room enough rather than as a failure.
 | Author: suinevere
 ----------------------*/
#define SATURN_BUP_ABSENT      0
#define SATURN_BUP_MEASURED    1
#define SATURN_BUP_UNFORMATTED 2

/*----------------------
 | saturn_bup_space
 | Description: Asks a device what a record of `bytes` would cost it and what it
 |   has left, both counted in that device's own blocks -- internal RAM and a
 |   cartridge do not share a block size, so the two numbers are only ever
 |   comparable against each other. Read-only: it never formats.
 | Author: suinevere
 ----------------------*/
int  saturn_bup_space(int device, uint32_t bytes, uint32_t *out_need,
                      uint32_t *out_free);

/*----------------------
 | SATURN_BUP_LIST_MAX
 | Description: The most filenames saturn_bup_list will report in one call, and
 |   the size of the driver table it builds to do it. A device holding more files
 |   than this cannot be listed exhaustively, which is why the call says so (see
 |   saturn_bup_list) rather than quietly returning a prefix.
 | Author: suinevere
 ----------------------*/
#define SATURN_BUP_LIST_MAX 32

/*----------------------
 | saturn_bup_list
 | Description: Names every file on a device, so a caller can decide what is its
 |   own without asking after each candidate by name. Read-only; never formats.
 | Author: suinevere
 ----------------------*/
int  saturn_bup_list(int device, char out[][12], int max);

/*----------------------
 | saturn_bup_write / saturn_bup_read / saturn_bup_info / saturn_bup_delete
 | Description: write stores len bytes under name+comment (overwriting a same-named
 |   file); read loads a named save into `data` (sized for the stored blob); info
 |   copies a save's comment (<=10 chars, NUL-terminated) into out_comment; delete
 |   removes a named file, which is how a caller retires a companion record whose
 |   own write failed rather than leave a stale one paired with a fresh save. Each
 |   returns 1 on success, 0 otherwise; delete reports 0 for a file that was not
 |   there, which is the same outcome the caller wanted.
 | Author: suinevere
 ----------------------*/
int  saturn_bup_write(int device, const char *name, const char *comment,
                      const uint8_t *data, uint32_t len);
int  saturn_bup_read(int device, const char *name, uint8_t *data);
int  saturn_bup_info(int device, const char *name, char *out_comment);
int  saturn_bup_delete(int device, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_BACKUP_H */
