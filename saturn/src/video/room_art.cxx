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
 | g_cur_image
 | Description: The 1-based image index last successfully shown by this module,
 |   or -1 for none. room_art_show compares its resolved image against this
 |   before touching the CGL decode or NBG0 -- several rooms across the maze
 |   share a frame, and re-decoding + re-uploading 76.8 KB for a picture that
 |   is already on screen is wasted work on every step through them.
 | Author: suinevere
 ----------------------*/
static int g_cur_image = -1;

/*----------------------
 | g_room / g_have_room
 | Description: The room last handed to this module, drawn or not. Noted on
 |   every room change rather than only on the ones that draw, because the
 |   Palette can become Dynamic long after the player stopped moving and
 |   room_art_reshow has to know what to put up.
 | Author: suinevere
 ----------------------*/
static unsigned int g_room = 0;
static bool         g_have_room = false;

/*----------------------
 | ART_DIR
 | Description: The disc directory holding the area archives.
 | Author: suinevere
 ----------------------*/
#define ART_DIR "BG"

/*----------------------
 | art_dir_restore
 | Description: Puts the CD back where it was before this module stepped into
 |   /BG. cd_restore_z3 alone is not enough on the title screen's path: the
 |   catalogue scan has not captured /Z3 yet there, so that call is a no-op and
 |   the drive would be left standing in /BG for whatever ran next. Climbing to
 |   root first makes the restore correct in both phases and costs nothing in
 |   the gameplay one, where cd_restore_z3 then steps straight back into /Z3.
 | Author: suinevere
 | Dependencies: title.h (cd_enter_root, cd_restore_z3)
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void art_dir_restore(void) {
    cd_enter_root();
    cd_restore_z3();
}

/*----------------------
 | frame_of
 | Description: Resolves one room to its image index.
 | Author: suinevere
 | Dependencies: scene/presentation.h
 | Globals: g_have_game, g_release, g_serial
 | Params: obj -- the room's object number; image -- filled on success
 | Returns: true when the room is authored
 ----------------------*/
static bool frame_of(unsigned int obj, int *image) {
    Presentation p;
    if (!g_have_game) return false;
    if (!pres_of_room(g_release, g_serial, obj, &p)) return false;
    *image = (int) p.image;
    return true;
}

/*----------------------
 | load_area
 | Description: Reads one area's archive into Low Work RAM. Frees whatever
 |   archive was resident first, unconditionally, before the disc is even
 |   touched -- so two archives are never resident at once, but it also means a
 |   failed load below leaves nothing resident rather than falling back to what
 |   was there. Also frees the block it just allocated if that block is not
 |   long-aligned or the read comes up short. g_area stays -1 until the read
 |   fully succeeds.
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
    if (SRL::Cd::ChangeDir(ART_DIR) < 0) { art_dir_restore(); return false; }

    {
        SRL::Cd::File f(name);
        if (!f.Exists()) { art_dir_restore(); return false; }
        {
            const uint32_t bytes = (uint32_t) f.Size.Bytes;
            if (bytes == 0 || SRL::Memory::LowWorkRam::GetFreeSpace()
                              < bytes + CGL_FRAME_BYTES + 4096) {
                art_dir_restore();
                return false;
            }
            g_archive = (unsigned char *) SRL::Memory::LowWorkRam::Malloc(bytes);
            if (g_archive == nullptr) { art_dir_restore(); return false; }
            // LoadBytes wants a long-aligned destination; refuse rather than corrupt.
            if (((unsigned int) g_archive & 3) != 0) {
                SRL::Memory::LowWorkRam::Free(g_archive);
                g_archive = nullptr;
                art_dir_restore();
                return false;
            }
            if (f.LoadBytes(0, (int32_t) bytes, g_archive) <= 0) {
                SRL::Memory::LowWorkRam::Free(g_archive);
                g_archive = nullptr;
                art_dir_restore();
                return false;
            }
            g_archive_len = bytes;
            g_area = area;
        }
    }
    art_dir_restore();
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
 | nbg0_shows_area
 | Description: Whether the picture NBG0 actually holds came from this area's
 |   archive, by comparing title_bg_loaded_file() against the area stem
 |   frame_put tags every upload with.
 | Author: suinevere
 | Dependencies: title.h, scene/presentation.h
 | Globals: N/A
 | Params: area -- the area index to test
 | Returns: true when NBG0 is showing a frame from that area
 ----------------------*/
static bool nbg0_shows_area(int area) {
    const char *tag = pres_area_name(area);
    const char *loaded;
    int i = 0;
    if (tag == nullptr) return false;
    loaded = title_bg_loaded_file();
    for (; tag[i] != '\0'; i++) if (loaded[i] != tag[i]) return false;
    return loaded[i] == '\0';
}

/*----------------------
 | frame_put
 | Description: Puts one 1-based image on NBG0, reading its area archive first
 |   if a different one is resident. The whole room picture path -- the title
 |   screen used to come through here too, for a frame picked at random, and now
 |   shows its own TITLE.TGA instead.
 |
 |   Skips the decode and the upload when this image is the one already showing
 |   -- verified against what NBG0 actually records, not only against
 |   g_cur_image, so a picture another caller has taken the layer over for is
 |   never mistaken for still being resident.
 | Author: suinevere
 | Dependencies: SRL, cgl.h, title.h, scene/presentation.h
 | Globals: g_area, g_archive, g_archive_len, g_pixels, g_clut, g_cur_image
 | Params: image -- 1-based image index
 | Returns: 1 when that image is on screen, 0 on any refusal
 ----------------------*/
static int frame_put(int image) {
    int area;
    unsigned long off, len;

    if (pres_frame(image, &area, &off, &len) != 1) return 0;

    if (image == g_cur_image && nbg0_shows_area(area)) {
        SRL::VDP2::NBG0::ScrollEnable();
        return 1;
    }

    if (area != g_area && !load_area(area)) return 0;
    if (g_archive == nullptr || off + len > g_archive_len) return 0;

    if (g_pixels == nullptr) {
        g_pixels = (unsigned char *) SRL::Memory::LowWorkRam::Malloc(CGL_FRAME_BYTES);
        if (g_pixels == nullptr) return 0;
    }

    cgl_palette(g_archive + off, g_clut);
    if (cgl_decode(g_archive + off, len, g_pixels, CGL_FRAME_BYTES)
        != (unsigned long) CGL_FRAME_BYTES) return 0;

    if (!title_bg_show_raw(g_pixels, g_clut, CGL_WIDTH, CGL_HEIGHT,
                           pres_area_name(area))) return 0;
    g_cur_image = image;
    return 1;
}

/*----------------------
 | room_art_show
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: frame_put, scene/presentation.h
 | Globals: g_have_game, g_release, g_serial
 | Params: obj -- the room's object number
 | Returns: 1 when the room's picture is on screen (freshly shown or already
 |   there), 0 on failure, which holds whatever was showing before
 ----------------------*/
int room_art_show(unsigned int obj) {
    int image;

    room_art_note_room(obj);

    if (!frame_of(obj, &image)) return 0;
    return frame_put(image);
}

/*----------------------
 | room_art_changes_for
 | Description: See room_art.h. Mirrors frame_put's short-circuit exactly --
 |   g_cur_image and what NBG0 records -- rather than restating the rule, so the
 |   two cannot drift into a transition that ramps for a picture that never moves,
 |   or skips one that does.
 | Author: suinevere
 | Dependencies: frame_of, nbg0_shows_area, scene/presentation.h
 | Globals: g_cur_image
 | Params: obj -- the room's object number
 | Returns: 1 when showing that room would put a different picture up, 0 otherwise
 ----------------------*/
int room_art_changes_for(unsigned int obj) {
    int image, area;
    unsigned long off, len;

    if (!frame_of(obj, &image)) return 0;
    if (pres_frame(image, &area, &off, &len) != 1) return 0;
    return (image == g_cur_image && nbg0_shows_area(area)) ? 0 : 1;
}

/*----------------------
 | room_art_note_room
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room, g_have_room
 | Params: obj -- the room's object number
 | Returns: N/A
 ----------------------*/
void room_art_note_room(unsigned int obj) {
    g_room = obj;
    g_have_room = true;
}

/*----------------------
 | room_art_reshow
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: room_art_show
 | Globals: g_room, g_have_room
 | Params: N/A
 | Returns: 1 when a picture is on screen, 0 otherwise
 ----------------------*/
int room_art_reshow(void) {
    if (!g_have_room) return 0;
    return room_art_show(g_room);
}

/*----------------------
 | room_art_release
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_area, g_archive, g_archive_len, g_pixels, g_have_game, g_cur_image,
 |   g_room, g_have_room
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
    g_cur_image = -1;
    g_room = 0;
    g_have_room = false;
}
