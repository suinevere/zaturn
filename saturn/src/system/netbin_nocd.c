/*----------------------
 | netbin_nocd.c
 | Description: Satisfies the one CD symbol the netbin's link needs so that
 |   SGL's filesystem never comes with it. SRL::Core::Initialize calls
 |   SRL::Cd::Initialize unconditionally (srl_core.hpp:89), whose only outgoing
 |   call is GFS_Init (srl_cd.hpp:629). That single undefined symbol was pulling
 |   LIBCD.A(GFS.O), and GFS.O in turn pulled GFS_BUF, GFS_CDB, GFS_CDC,
 |   GFS_CDF, GFS_DIR, GFS_TRN and eight CDC_* members -- 23 KB of CD-ROM
 |   filesystem, drive control and DMA in a build that opens no files and has no
 |   disc. Defining GFS_Init here means the linker resolves it from our object
 |   and never reaches into the archive.
 |
 |   This file is netbin-only and must stay out of the CD build, where it would
 |   collide with the real GFS_Init; makefile:NETBIN_ONLY_SOURCES filters it out
 |   of the find glob and tests/test_netbin_sources.py gates that.
 |
 |   If a future netbin ever needs a real file read, deleting this file is the
 |   whole fix -- the archive member comes straight back.
 | Author: suinevere
 | Dependencies: none (the signature is copied from modules/sgl/INC/sega_gfs.h:344
 |   rather than including it, so this file pulls in no SGL headers)
 ----------------------*/

/*----------------------
 | GFS_Init
 | Description: Reports that no CD filesystem came up. SRL::Cd::Initialize reads
 |   the result as `isInitialized = (GFS_Init(...) <= 2)`, so a value above 2
 |   leaves SRL::Cd correctly marked uninitialized rather than pretending a disc
 |   is mounted -- every later Cd call then refuses instead of walking into
 |   filesystem code that is not linked. Nothing in this build calls one.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: open_max -- concurrent file handles SRL asked for; work -- SRL's GFS
 |   work area; dirtbl -- SRL's directory table. All three are ignored.
 | Returns: 3, which SRL reads as "not initialized"
 ----------------------*/
int GFS_Init(int open_max, void *work, void *dirtbl) {
    (void) open_max;
    (void) work;
    (void) dirtbl;
    return 3;
}
