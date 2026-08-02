/*----------------------
 | msc_dir.cxx
 | Description: See msc_dir.h.
 | Author: suinevere
 | Dependencies: msc_dir.h, SRL
 ----------------------*/
#include "msc_dir.h"
#include <srl.hpp>

/*----------------------
 | cd_enter_msc
 | Description: See msc_dir.h.
 | Author: suinevere
 | Dependencies: SGL (GFS), SRL
 | Globals: N/A
 | Params: N/A
 | Returns: true once the CD directory is /MSC, false if it is absent
 ----------------------*/
extern "C" bool cd_enter_msc(void) {
    static GfsDirName dirnames[SRL_MAX_CD_FILES];
    static GfsDirTbl  tbl;
    int32_t fid = GFS_NameToId((int8_t *) "MSC");
    if (fid < 0) return false;
    GFS_DIRTBL_TYPE(&tbl)    = GFS_DIR_NAME;
    GFS_DIRTBL_DIRNAME(&tbl) = dirnames;
    GFS_DIRTBL_NDIR(&tbl)    = SRL_MAX_CD_FILES;
    if (GFS_LoadDir(fid, &tbl) < 0) return false;
    GFS_SetDir(&tbl);
    return true;
}
