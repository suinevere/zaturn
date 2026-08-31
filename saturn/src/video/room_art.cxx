/*----------------------
 | room_art.cxx
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: SRL, cgl.h, room_art.h, title.h, scene/presentation.h
 | Globals: g_release, g_serial, g_have_game, g_area, g_archive, g_archive_len,
 |   g_pixels, g_clut
 ----------------------*/
#include <srl.hpp>
#include "video/cgl.h"
#include "video/room_art.h"
#include "video/title.h"
#include "scene/presentation.h"

/*----------------------
 | g_release / g_serial / g_have_game
 | Description: The running story's identity, and whether it carries authored
 |   art at all. Held here so the room subscriber does not have to.
 | Author: suinevere
 ----------------------*/
static unsigned int g_release = 0;
static char         g_serial[7] = { 0 };
static bool         g_have_game = false;

/*----------------------
 | g_area / g_archive / g_archive_len
 | Description: The resident area (-1 when none), its bytes and their length.
 |   Plain statics, so a soft reset returns to what the longjmp left intact --
 |   the same property title.cxx's cache relies on.
 | Author: suinevere
 ----------------------*/
static int            g_area = -1;
static unsigned char *g_archive = nullptr;
static unsigned long  g_archive_len = 0;

/*----------------------
 | g_pixels / g_clut
 | Description: The decode target and the palette the current frame produced.
 |   One target, reused: only one picture is ever on screen, and a second buffer
 |   would cost 76.8 KB the biggest archive needs.
 | Author: suinevere
 ----------------------*/
static unsigned char  *g_pixels = nullptr;
static unsigned short  g_clut[256];

/*----------------------
 | ART_DIR
 | Description: The disc directory holding the area archives.
 | Author: suinevere
 ----------------------*/
#define ART_DIR "BG"

/*----------------------
 | frame_of
 | Description: Resolves one room to its frame: the area, and where the record
 |   lies inside that area's archive.
 | Author: suinevere
 | Dependencies: scene/presentation.h
 | Globals: g_have_game, g_release, g_serial
 | Params: obj -- the room's object number; area, offset, length -- filled on
 |   success
 | Returns: true when the room is authored
 ----------------------*/
static bool frame_of(unsigned int obj, int *area,
                     unsigned long *offset, unsigned long *length) {
    Presentation p;
    if (!g_have_game) return false;
    if (!pres_of_room(g_release, g_serial, obj, &p)) return false;
    return pres_frame((int) p.image, area, offset, length) == 1;
}

/*----------------------
 | load_area
 | Description: Reads one area's archive into Low Work RAM, replacing whatever
 |   was resident. Leaves g_area at -1 and frees nothing new on any failure, so
 |   a failed read cannot leave a half-loaded archive claiming to be an area.
 | Author: suinevere
 | Dependencies: SRL, title.h (cd_enter_root, cd_restore_z3)
 | Globals: g_area, g_archive, g_archive_len
 | Params: area -- the area index to make resident
 | Returns: true when the archive is resident
 ----------------------*/
static bool load_area(int area) {
    const char *stem = pres_area_name(area);
    char name[16];
    int i = 0;

    if (stem == nullptr) return false;

    if (g_archive != nullptr) {
        SRL::Memory::LowWorkRam::Free(g_archive);
        g_archive = nullptr;
        g_archive_len = 0;
    }
    g_area = -1;

    while (stem[i] != '\0' && i < 8) { name[i] = stem[i]; i++; }
    name[i++] = '.'; name[i++] = 'C'; name[i++] = 'G'; name[i++] = 'L';
    name[i] = '\0';

    cd_enter_root();
    if (SRL::Cd::ChangeDir(ART_DIR) < 0) { cd_restore_z3(); return false; }

    {
        SRL::Cd::File f(name);
        if (!f.Exists()) { cd_restore_z3(); return false; }
        {
            const uint32_t bytes = (uint32_t) f.Size.Bytes;
            if (bytes == 0 || SRL::Memory::LowWorkRam::GetFreeSpace()
                              < bytes + CGL_FRAME_BYTES + 4096) {
                cd_restore_z3();
                return false;
            }
            g_archive = (unsigned char *) SRL::Memory::LowWorkRam::Malloc(bytes);
            if (g_archive == nullptr) { cd_restore_z3(); return false; }
            if (f.LoadBytes(0, (int32_t) bytes, g_archive) <= 0) {
                SRL::Memory::LowWorkRam::Free(g_archive);
                g_archive = nullptr;
                cd_restore_z3();
                return false;
            }
            g_archive_len = bytes;
            g_area = area;
        }
    }
    cd_restore_z3();
    return true;
}

/*----------------------
 | room_art_set_game
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: scene/presentation.h
 | Globals: g_release, g_serial, g_have_game
 | Params: release, serial -- the story identity
 | Returns: N/A
 ----------------------*/
void room_art_set_game(unsigned int release, const char *serial) {
    int i;
    g_release = release;
    for (i = 0; i < 6; i++) g_serial[i] = serial ? serial[i] : '\0';
    g_serial[6] = '\0';
    g_have_game = (serial != nullptr) && (pres_game_index(release, g_serial) >= 0);
}

/*----------------------
 | room_art_available
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_have_game
 | Params: N/A
 | Returns: 1 when the story has authored art
 ----------------------*/
int room_art_available(void) { return g_have_game ? 1 : 0; }

/*----------------------
 | room_art_needs_disc
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_area
 | Params: obj -- the room's object number
 | Returns: 1 when a disc read is required
 ----------------------*/
int room_art_needs_disc(unsigned int obj) {
    int area;
    unsigned long off, len;
    if (!frame_of(obj, &area, &off, &len)) return 0;
    return (area == g_area) ? 0 : 1;
}

/*----------------------
 | room_art_show
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: SRL, cgl.h, title.h
 | Globals: g_area, g_archive, g_archive_len, g_pixels, g_clut
 | Params: obj -- the room's object number
 | Returns: 1 when a new picture was applied
 ----------------------*/
int room_art_show(unsigned int obj) {
    int area;
    unsigned long off, len;

    if (!frame_of(obj, &area, &off, &len)) return 0;
    if (area != g_area && !load_area(area)) return 0;
    if (g_archive == nullptr || off + len > g_archive_len) return 0;

    if (g_pixels == nullptr) {
        g_pixels = (unsigned char *) SRL::Memory::LowWorkRam::Malloc(CGL_FRAME_BYTES);
        if (g_pixels == nullptr) return 0;
    }

    cgl_palette(g_archive + off, g_clut);
    if (cgl_decode(g_archive + off, len, g_pixels, CGL_FRAME_BYTES)
        != (unsigned long) CGL_FRAME_BYTES) return 0;

    return title_bg_show_raw(g_pixels, g_clut, CGL_WIDTH, CGL_HEIGHT,
                             pres_area_name(area)) ? 1 : 0;
}

/*----------------------
 | room_art_release
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_area, g_archive, g_archive_len, g_pixels, g_have_game
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void room_art_release(void) {
    if (g_archive != nullptr) SRL::Memory::LowWorkRam::Free(g_archive);
    if (g_pixels != nullptr)  SRL::Memory::LowWorkRam::Free(g_pixels);
    g_archive = nullptr;
    g_pixels = nullptr;
    g_archive_len = 0;
    g_area = -1;
    g_have_game = false;
}
