/*----------------------
 | song_bank_cd.cxx
 | Description: Implementation of the disc half of the song bank. A .cxx and not
 |   a .c for the same reason music_source.cxx is: the CD build finds its
 |   sources with `find src/ -name '*.cxx'` and the netbin lists its own, so the
 |   extension is what keeps a file that needs SRL and a drive out of a build
 |   that has neither.
 | Author: suinevere
 | Dependencies: SRL, song_bank_cd.h, song_bank.h, music_synth_data.h, title.h
 | Globals: g_buffer
 ----------------------*/
#include <srl.hpp>
#include "song_bank_cd.h"
#include "video/title.h"

extern "C" {
#include "song_bank.h"
#include "music_synth_data.h"
}

/*----------------------
 | PAT_DIR
 | Description: Where the catalogue lives: the same /BG the area archives and
 |   OITEM.CZ share, rather than a directory of its own for one file.
 | Author: suinevere
 ----------------------*/
#define PAT_DIR "BG"

/*----------------------
 | g_buffer
 | Description: The one Low Work RAM claim this module makes, held for the
 |   session: the header sector first, then the slot every tune is read into.
 |   One allocation and not two because the two are the same lifetime, and
 |   because a second Malloc between the area archives' loads and frees is a
 |   second chance to fragment the heap they come out of.
 | Author: suinevere
 ----------------------*/
static unsigned char *g_buffer = nullptr;

/*----------------------
 | pat_dir_restore
 | Description: Puts the CD back where it was, the same two-step item_art.cxx
 |   and room_art.cxx use: cd_restore_z3 alone is a no-op before the catalogue
 |   scan has captured /Z3, so climbing to root first makes the restore correct
 |   on every path that can reach here.
 | Author: suinevere
 | Dependencies: title.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void pat_dir_restore(void) {
    cd_enter_root();
    cd_restore_z3();
}

/*----------------------
 | pat_read
 | Description: The reader the bank calls when it wants a tune. Re-enters /BG
 |   every time rather than holding the file open across the session: the
 |   directory moves under every other loader in this build, and a stale handle
 |   would read whatever is at those sectors now.
 | Author: suinevere
 | Dependencies: SRL, title.h
 | Globals: N/A
 | Params: sector -- sectors to skip from the file's start; bytes -- how many to
 |   read; dest -- long-aligned destination
 | Returns: bytes read, or 0
 ----------------------*/
static int pat_read(int sector, int bytes, void *dest) {
    int got = 0;
    cd_enter_root();
    if (SRL::Cd::ChangeDir(PAT_DIR) >= 0) {
        SRL::Cd::File f(MUSIC_PAT_FILE);
        if (f.Exists()) {
            int32_t n = f.LoadBytes((size_t) sector, (int32_t) bytes, dest);
            if (n > 0) got = (int) n;
        }
    }
    pat_dir_restore();
    return got;
}

int song_bank_cd_open(void) {
    const unsigned long want = MUSIC_PAT_HEADER_BYTES + MUSIC_PAT_SLOT_BYTES;

    if (g_buffer != nullptr) return 1;
    /* Asked before claiming anything. The other Low Work RAM claimants are
       measured against each other in tests/test_lwram_budget.py and this one is
       in that sum, but the sum is what SHOULD fit -- a disc whose archives grew
       since is a disc where the music gives way, not one where the room art
       fails to load. 4 KB of slack, matching what item_art asks for. */
    if (SRL::Memory::LowWorkRam::GetFreeSpace() < want + 4096) return 0;

    g_buffer = (unsigned char *) SRL::Memory::LowWorkRam::Malloc(want);
    if (g_buffer == nullptr) return 0;
    /* LoadBytes writes long words; an unaligned destination is an address
       error rather than a wrong picture. item_art checks the same thing. */
    if (((unsigned int) g_buffer & 3) != 0) {
        SRL::Memory::LowWorkRam::Free(g_buffer);
        g_buffer = nullptr;
        return 0;
    }

    if (pat_read(0, MUSIC_PAT_HEADER_BYTES, g_buffer) <= 0
        || !song_bank_bind(g_buffer, MUSIC_PAT_HEADER_BYTES,
                           g_buffer + MUSIC_PAT_HEADER_BYTES,
                           MUSIC_PAT_SLOT_BYTES, pat_read)) {
        SRL::Memory::LowWorkRam::Free(g_buffer);
        g_buffer = nullptr;
        return 0;
    }
    return 1;
}
