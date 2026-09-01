/*----------------------
 | title.cxx
 | Description: Title screen, the NBG0 picture layer and its fades, the one TGA
 |   read left on the disc (the boot logo), CD directory juggling, and the boot
 |   sequence random seed.
 |
 |   The Low Work RAM art cache that used to live here is gone with the TGA
 |   backgrounds it held. Room art is CGL now -- room_art.cxx keeps one archive
 |   resident and hands decoded frames to title_bg_show_raw -- so the only file
 |   this reads is SUINE.TGA, once, under a black screen.
 | Author: suinevere
 | Dependencies: app_state.h, display.h,
 |   menu.h, soft_reset.h, game_catalog.h (the Z3 directory record
 |   cd_restore_z3 re-applies), online.h, boot_music.h, sound/music.h,
 |   text_map.h, bg_dim.h, SRL
 ----------------------*/
#include "title.h"
#include "bg_dim.h"
#include "app_state.h"
#include "display.h"
#include "sound/music.h"       /* music_start_menu */
#include "menu.h"
#include "console_view.h"
#include "input.h"
#include "soft_reset.h"
#include "saturn_keyboard.h"
#include "game_catalog.h"
#include "online.h"
#include <srl.hpp>
#include "text_map.h"

#include "boot_music.h"

/*----------------------
 | SOFT_RESET_HOLD
 | Description: Frames the reset chord must be held on the title screen before it
 |   triggers a hard NMI reboot.
 | Author: suinevere
 ----------------------*/
#define SOFT_RESET_HOLD 60

/*----------------------
 | g_root_dirnames / g_root_tbl / g_root_dir_valid
 | Description: The CD root directory record captured right after GFS_Reset, so
 |   cd_enter_root can return to it; the flag guards against an unread record.
 | Author: suinevere
 ----------------------*/
static GfsDirName g_root_dirnames[SRL_MAX_CD_FILES];
static GfsDirTbl  g_root_tbl;
static bool       g_root_dir_valid = false;

/*----------------------
 | g_z3_tbl (extern)
 | Description: The Z3 directory record, defined in game_catalog.cxx (scan_z3_folder
 |   captures it) and re-applied here by cd_restore_z3. It lives there because
 |   game_catalog.h cannot name a GfsDirTbl without dragging SRL into a C-safe
 |   header, so only the validity flag comes from the header.
 | Author: suinevere
 ----------------------*/
extern GfsDirTbl g_z3_tbl;

/*----------------------
 | title_draw_art
 | Description: Draws the title screen text art.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_draw_art(void) {
    text_print(13, 12, "Z - A T U R N");
    text_print(4, 15, "Saturn port (c) 2026 by Suinevere");
}

/*----------------------
 | cd_capture_root
 | Description: Climbs to the root and snapshots its directory record, so that
 |   cd_enter_root() can return there later.
 |
 |   The climb is not decoration. GFS_LoadDir(0, tbl) does NOT mean "load the root":
 |   directory ids are relative to the CURRENT directory and 0 is its own "." entry,
 |   so on its own that call snapshots wherever the drive happens to be standing. At
 |   a cold boot that is the root and it is right by luck; called again after a game
 |   has left the drive in /Z3, it captures /Z3 and every later name lookup resolves
 |   against the wrong directory, silently, for the rest of the session.
 |
 |   SRL::Cd::ChangeDir(nullptr) is the supported way up: it walks ".." until a
 |   directory's own address equals its parent's, which is only true at the root.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_root_tbl, g_root_dirnames, g_root_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void cd_capture_root(void) {
    if (SRL::Cd::ChangeDir((const char *) nullptr) < 0) {
        g_root_dir_valid = false;
        return;
    }
    GFS_DIRTBL_TYPE(&g_root_tbl)    = GFS_DIR_NAME;
    GFS_DIRTBL_DIRNAME(&g_root_tbl) = g_root_dirnames;
    GFS_DIRTBL_NDIR(&g_root_tbl)    = SRL_MAX_CD_FILES;
    // >= 0: GFS_LoadDir returns an error code, not a count of records.
    g_root_dir_valid = GFS_LoadDir(0, &g_root_tbl) >= 0;
}

/*----------------------
 | cd_enter_root
 | Description: Re-points the CD to the root directory captured by cd_capture_root.
 |   Used to make directory changes idempotent.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_root_tbl, g_root_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void cd_enter_root(void) {
    if (g_root_dir_valid) GFS_SetDir(&g_root_tbl);
}

/*----------------------
 | cd_enter_tga
 | Description: Steps the CD into /TGA, where the one TGA on the disc lives.
 |
 |   There is exactly one: SUINE.TGA, the boot logo. The room backgrounds are
 |   Zork I's own CGL archives under /BG, decoded on the Saturn by cgl.c, and
 |   the title screen shows one of those too -- so nothing here reads a picture
 |   that is not the logo, and the folder walk this used to do (a mood name
 |   before the slash, one step further into its subfolder) has nothing left to
 |   walk.
 |
 |   The working directory is process-wide, so the caller owes a cd_enter_root()
 |   once the open -- or the failed attempt at one -- is done: left standing in
 |   /TGA, a game catalog scan that runs GFS_LoadDir on /Z3 finds nothing (see
 |   game_catalog.cxx).
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: true when a bare filename can now be opened in /TGA
 ----------------------*/
static bool cd_enter_tga(void) {
    cd_enter_root();
    return SRL::Cd::ChangeDir("TGA") >= 0;
}

/*----------------------
 | cd_restore_z3
 | Description: Re-points the CD at the Z3 directory so story-file opens resolve.
 |   Exported rather than local to this file because it is the restore every
 |   post-selection CD detour owes: from the moment game_select() returns, a bare
 |   SRL::Cd::File("XXX.Z3") -- or its sibling .BLB -- has to resolve, and
 |   anything that steps out of /Z3 in between has to step back. A no-op before
 |   the catalogue scan has captured the record, which is what makes it safe to
 |   call unconditionally.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_z3_tbl, g_z3_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void cd_restore_z3(void) {
    if (g_z3_dir_valid) GFS_SetDir(&g_z3_tbl);
}

/*----------------------
 | g_nbg0_loaded / nbg0_note_loaded
 | Description: The name of the picture currently uploaded to NBG0, and the only
 |   way to record a new one. room_art.cxx compares its area stem against this to
 |   decide whether the frame it was asked for is already on screen and the
 |   decode and upload can be skipped entirely.
 |
 |   Every path that blits NBG0 has to record it here, the logo path included --
 |   a note about VRAM that VRAM does not agree with is invisible until a later
 |   request for that same name short-circuits onto the wrong picture.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_nbg0_loaded
 | Params: file -- the picture just uploaded, truncated to fit
 | Returns: N/A
 ----------------------*/
static char g_nbg0_loaded[16] = "";

static void nbg0_note_loaded(const char *file) {
    int k = 0;
    for (; file[k] && k < (int) sizeof(g_nbg0_loaded) - 1; k++) g_nbg0_loaded[k] = file[k];
    g_nbg0_loaded[k] = '\0';
}

/*----------------------
 | bitmap_read_end
 | Description: Restores the CD directory after a bitmap load.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void bitmap_read_end(void) {
    cd_restore_z3();
}

/*----------------------
 | RawBitmap
 | Description: An IBitmap that borrows its pixel plane rather than owning it,
 |   because the plane usually belongs to the image cache and has to outlive the
 |   upload. The palette is still owned: SRL::Bitmap::Palette deletes the color
 |   array it was handed, so every RawBitmap gets its own throwaway copy of the
 |   cached colors.
 | Author: suinevere
 ----------------------*/
struct RawBitmap final : SRL::Bitmap::IBitmap {
    uint8_t                *Pixels;
    SRL::Bitmap::Palette   *Pal;
    uint16_t                W, H;
    RawBitmap() : Pixels(nullptr), Pal(nullptr), W(0), H(0) {}
    ~RawBitmap() override {
        if (Pal != nullptr) delete Pal;
    }
    uint8_t *GetData() override { return Pixels; }
    SRL::Bitmap::BitmapInfo GetInfo() const override {
        return SRL::Bitmap::BitmapInfo(W, H, Pal);
    }
};

/*----------------------
 | TgaImage
 | Description: One decoded picture: the 8bpp pixel plane (top-down, leading
 |   partial sector already shifted off) plus its 256-entry palette. Pixels is
 |   the allocation base, so freeing it frees the plane.
 |
 |   Always a throwaway now. There used to be a nine-slot Low Work RAM cache of
 |   these, with a reusable plane capacity and an LRU clock, because the room
 |   backgrounds were TGAs read off the disc one mood at a time and a miss
 |   stopped the CD-DA. The backgrounds are CGL archives decoded by room_art.cxx
 |   today and the only TGA left on the disc is the boot logo, read once under a
 |   black screen with no music playing off the drive -- so there is nothing left
 |   for a cache to save.
 | Author: suinevere
 ----------------------*/
struct TgaImage {
    uint8_t               *Pixels;
    SRL::Types::HighColor *Colors;
    uint16_t               W, H;
};

/*----------------------
 | tga_image_free
 | Description: Releases both blocks of a decoded picture and blanks it. Both
 |   come from High Work RAM: nothing here competes for the megabyte that the
 |   boot jingle, the room archives and the typeahead trie share.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: img -- the picture to release
 | Returns: N/A
 ----------------------*/
static void tga_image_free(TgaImage *img) {
    if (img == nullptr) return;
    // The generic free, not the zone's: a decode can land in either zone now
    // (see tga_decode's `low`), and SRL routes a pointer to whichever owns it.
    if (img->Pixels != nullptr) SRL::Memory::Free(img->Pixels);
    if (img->Colors != nullptr) SRL::Memory::Free(img->Colors);
    img->Pixels = nullptr;
    img->Colors = nullptr;
}

/*----------------------
 | tga_decode
 | Description: Reads an uncompressed 8bpp colour-mapped TGA off the CD and
 |   decodes it into a freshly allocated plane: palette expanded to 256 entries,
 |   rows flipped to top-down, and the leading partial sector shifted off the
 |   front. This is the only function here that touches the disc, so it is also
 |   the only one that interrupts CD audio -- so every caller reads under a black
 |   screen with no track playing.
 |
 |   `low` picks the zone. A throwaway picture takes High Work RAM, where it is
 |   allocated and freed inside one call and competes with nothing. A picture
 |   held for the session takes Low Work RAM instead: the C heap is ~194 KB
 |   against a story image of up to 129 KB, so 78 KB held there is 78 KB the next
 |   story cannot have, while LWRAM is a separate megabyte with room for it
 |   beside the trie, the area archive and the item container (see
 |   tests/test_lwram_budget.py). Slower to fill -- 16 bits wide, behind the SCU
 |   -- which costs one DMA at load and nothing afterwards.
 |
 |   Steps into /TGA through cd_enter_tga before opening, and calls
 |   cd_enter_root() on every exit that follows -- success, every validation
 |   failure, every allocation or read failure. The working directory is
 |   process-wide, and a return that skipped it would leave the CD standing in
 |   /TGA for whatever runs next (see cd_enter_tga).
 |
 |   One mode. It used to have two, the second decoding into a cache slot's
 |   already-owned buffers; there is no cache any more, so every call allocates
 |   its own plane at exactly the size the picture needs and the caller owns it.
 | Author: suinevere
 | Dependencies: SRL, cd_enter_tga, cd_enter_root
 | Globals: N/A
 | Params: file -- a bare /TGA filename, e.g. "SUINE.TGA"; out -- filled on
 |   success; low -- true to decode into Low Work RAM rather than the C heap
 | Returns: true on success; on failure out is left blank and nothing is
 |   allocated
 ----------------------*/
static bool tga_decode(const char *file, TgaImage *out, bool low) {
    out->Pixels = nullptr;
    out->Colors = nullptr;
    out->W      = 0;
    out->H      = 0;

    if (!cd_enter_tga()) { cd_enter_root(); return false; }

    SRL::Cd::File f(file);
    if (!f.Exists()) { cd_enter_root(); return false; }

    static uint32_t hdrbuf[512];
    uint8_t *const hdr = (uint8_t *) hdrbuf;
    const int32_t ss = (f.Size.SectorSize > 0) ? f.Size.SectorSize : 2048;
    if (ss > (int32_t) sizeof(hdrbuf)) { cd_enter_root(); return false; }
    if (f.LoadBytes(0, ss, hdr) <= 0) { cd_enter_root(); return false; }

    const int idlen    = hdr[0];
    const int cmaptype = hdr[1];
    const int imgtype  = hdr[2];
    const int cmaplen  = hdr[5] | (hdr[6] << 8);
    const int cmapbits = hdr[7];
    const int w        = hdr[12] | (hdr[13] << 8);
    const int h        = hdr[14] | (hdr[15] << 8);
    const int bpp      = hdr[16];
    const int topdown  = (hdr[17] >> 5) & 1;

    if (cmaptype != 1 || imgtype != 1 || bpp != 8)      { cd_enter_root(); return false; }
    if (cmaplen <= 0 || cmaplen > 256)                  { cd_enter_root(); return false; }
    if (cmapbits != 24 && cmapbits != 32)               { cd_enter_root(); return false; }
    if (w <= 0 || h <= 0 || w > 1024 || h > 512)        { cd_enter_root(); return false; }

    const int      cmapbytes = cmaplen * (cmapbits / 8);
    const int      pixoff    = 18 + idlen + cmapbytes;
    const uint32_t npix      = (uint32_t) w * (uint32_t) h;
    if (pixoff > ss)                                    { cd_enter_root(); return false; }
    if ((uint32_t) pixoff + npix > (uint32_t) f.Size.Bytes) { cd_enter_root(); return false; }

    const uint32_t skip    = (uint32_t) (pixoff % ss);
    const uint32_t span    = skip + npix;
    const uint32_t palsize = 256 * sizeof(SRL::Types::HighColor);
    const uint32_t zone_free = low ? (uint32_t) SRL::Memory::LowWorkRam::GetFreeSpace()
                                   : (uint32_t) SRL::Memory::HighWorkRam::GetFreeSpace();
    if (zone_free < span + palsize + 4096) {
        cd_enter_root();
        return false;
    }

    SRL::Types::HighColor *colors = (SRL::Types::HighColor *)
        (low ? SRL::Memory::LowWorkRam::Malloc(palsize)
             : SRL::Memory::HighWorkRam::Malloc(palsize));
    if (colors == nullptr) { cd_enter_root(); return false; }
    for (int i = 0; i < 256; i++) {
        SRL::Types::HighColor c;
        c.Opaque = (i == 0) ? 0 : 1;
        if (i < cmaplen) {
            const uint8_t *e = hdr + 18 + idlen + i * (cmapbits / 8);
            c.Blue = e[0] >> 3; c.Green = e[1] >> 3; c.Red = e[2] >> 3;
        } else {
            c.Blue = 0; c.Green = 0; c.Red = 0;
        }
        colors[i] = c;
    }

    uint8_t *pix = (uint8_t *) (low ? SRL::Memory::LowWorkRam::Malloc(span)
                                    : SRL::Memory::HighWorkRam::Malloc(span));
    // LoadBytes wants a long-aligned destination; refuse rather than corrupt.
    if (pix == nullptr || ((unsigned int) pix & 3) != 0) {
        if (pix != nullptr) SRL::Memory::Free(pix);
        SRL::Memory::Free(colors);
        cd_enter_root();
        return false;
    }
    if (f.LoadBytes((size_t) (pixoff / ss), (int32_t) span, pix) <= 0) {
        SRL::Memory::Free(pix);
        SRL::Memory::Free(colors);
        cd_enter_root();
        return false;
    }
    if (skip > 0) for (uint32_t i = 0; i < npix; i++) pix[i] = pix[skip + i];

    if (!topdown) {
        static uint8_t rowbuf[1024];
        for (int y = 0; y < h / 2; y++) {
            uint8_t *a = pix + (uint32_t) y * (uint32_t) w;
            uint8_t *b = pix + (uint32_t) (h - 1 - y) * (uint32_t) w;
            for (int i = 0; i < w; i++) rowbuf[i] = a[i];
            for (int i = 0; i < w; i++) a[i]      = b[i];
            for (int i = 0; i < w; i++) b[i]      = rowbuf[i];
        }
    }

    out->Pixels = pix;
    out->Colors = colors;
    out->W      = (uint16_t) w;
    out->H      = (uint16_t) h;
    cd_enter_root();
    return true;
}

/*----------------------
 | tga_blit_nbg0
 | Description: Uploads an already-decoded image to VDP2 NBG0. Touches no CD, so
 |   it is safe to call with music playing.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: img -- the decoded image
 | Returns: true on success, false if the throwaway palette could not be made
 ----------------------*/
static bool tga_blit_nbg0(const TgaImage *img) {
    // SRL::Bitmap::Palette deletes the colors it is handed, so hand it a copy
    // and leave the cached palette alone.
    SRL::Types::HighColor *colors = new SRL::Types::HighColor[256];
    if (colors == nullptr) return false;
    for (int i = 0; i < 256; i++) colors[i] = img->Colors[i];

    RawBitmap bmp;
    bmp.Pixels = img->Pixels;
    bmp.W      = img->W;
    bmp.H      = img->H;
    bmp.Pal    = new SRL::Bitmap::Palette(colors, 256);
    if (bmp.Pal == nullptr) { delete[] colors; return false; }

    SRL::VDP2::NBG0::LoadBitmap(&bmp);
    return true;
}

/*----------------------
 | title_bg_show_oneoff
 | Description: Reads and decodes a TGA fresh into High Work RAM, uploads it to
 |   NBG0, and frees the buffer immediately. The one picture route that reads a
 |   TGA off the disc, and the boot logo is the one picture that takes it: every
 |   other background on this disc is a CGL frame that room_art.cxx decodes and
 |   hands to title_bg_show_raw below.
 |
 |   The name is what is left of a distinction that no longer exists -- it used
 |   to mean "a one-off, as opposed to a cached picture", back when the room
 |   backgrounds were TGAs held in a Low Work RAM cache this deliberately did
 |   not touch.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: file -- bare /TGA filename of the image to load
 | Returns: true if the requested display was applied; false on failure
 ----------------------*/
bool title_bg_show_oneoff(const char *file) {
    TgaImage oneoff = { nullptr, nullptr, 0, 0 };
    bool decoded = tga_decode(file, &oneoff, false);
    bitmap_read_end();
    if (!decoded) return false;

    bool ok = tga_blit_nbg0(&oneoff);
    tga_image_free(&oneoff);
    if (!ok) return false;

    SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer1);
    nbg0_note_loaded(file);
    SRL::VDP2::NBG0::ScrollEnable();
    return true;
}

/*----------------------
 | title_bg_show_raw
 | Description: See title.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: pixels, clut, w, h, tag -- see title.h
 | Returns: true on success
 ----------------------*/
bool title_bg_show_raw(const unsigned char *pixels, const unsigned short *clut,
                       int w, int h, const char *tag) {
    if (pixels == nullptr || clut == nullptr || w <= 0 || h <= 0) return false;

    SRL::Types::HighColor *colors = new SRL::Types::HighColor[256];
    if (colors == nullptr) return false;
    for (int i = 0; i < 256; i++) colors[i] = SRL::Types::HighColor(clut[i]);

    RawBitmap bmp;
    bmp.Pixels = const_cast<uint8_t *>(pixels);
    bmp.W      = (uint16_t) w;
    bmp.H      = (uint16_t) h;
    bmp.Pal    = new SRL::Bitmap::Palette(colors, 256);
    if (bmp.Pal == nullptr) { delete[] colors; return false; }

    SRL::VDP2::NBG0::LoadBitmap(&bmp);
    SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer1);
    nbg0_note_loaded(tag ? tag : "");
    SRL::VDP2::NBG0::ScrollEnable();
    return true;
}

/*----------------------
 | g_held
 | Description: One decoded picture kept for the rest of the session, so a
 |   screen that shows the same wallpaper every time it opens pays the disc once
 |   rather than once per open. The map's parchment is the only user: it is
 |   opened and closed repeatedly with CD audio playing, and a read per open
 |   would stop the track every time -- which is the whole reason the room
 |   pictures stopped being TGAs.
 |
 |   Deliberately one slot and not a cache. There is exactly one picture that
 |   wants this; the nine-slot LRU that used to live here went with the room
 |   backgrounds, and a second one would be a cache again.
 | Author: suinevere
 ----------------------*/
static TgaImage g_held = { nullptr, nullptr, 0, 0 };

/*----------------------
 | title_bg_hold
 | Description: See title.h.
 | Author: suinevere
 | Dependencies: tga_decode, bitmap_read_end
 | Globals: g_held
 | Params: file -- bare /TGA filename
 | Returns: true when the picture is held, false when it could not be read
 ----------------------*/
bool title_bg_hold(const char *file) {
    if (g_held.Pixels != nullptr) return true;
    TgaImage img = { nullptr, nullptr, 0, 0 };
    bool decoded = tga_decode(file, &img, true);   // held for the session: LWRAM
    bitmap_read_end();
    if (!decoded) return false;
    g_held = img;
    return true;
}

/*----------------------
 | title_bg_held
 | Description: See title.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_held
 | Params: N/A
 | Returns: true when a picture is held
 ----------------------*/
bool title_bg_held(void) {
    return g_held.Pixels != nullptr;
}

/*----------------------
 | title_bg_show_held
 | Description: See title.h. Touches no CD, so it is safe with a track playing
 |   -- which is the point of holding the picture in the first place.
 | Author: suinevere
 | Dependencies: tga_blit_nbg0, nbg0_note_loaded, SRL
 | Globals: g_held
 | Params: tag -- the name to record as loaded, for title_bg_loaded_file
 | Returns: true when the picture is on NBG0
 ----------------------*/
bool title_bg_show_held(const char *tag) {
    if (g_held.Pixels == nullptr) return false;
    if (!tga_blit_nbg0(&g_held)) return false;
    SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer1);
    nbg0_note_loaded(tag ? tag : "");
    SRL::VDP2::NBG0::ScrollEnable();
    return true;
}

/*----------------------
 | title_bg_drop_held
 | Description: See title.h.
 | Author: suinevere
 | Dependencies: tga_image_free
 | Globals: g_held
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_bg_drop_held(void) {
    tga_image_free(&g_held);
}

/*----------------------
 | title_bg_hide
 | Description: Hides the title background image by disabling scroll on VDP2 NBG0.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_bg_hide(void) {
    SRL::VDP2::NBG0::ScrollDisable();
}

/*----------------------
 | g_bg_shift
 | Description: The wallpaper's current vertical offset in pixels, so the
 |   per-frame caller costs a comparison rather than a register write.
 | Author: suinevere
 ----------------------*/
static int g_bg_shift = 0;

/*----------------------
 | title_bg_set_shift
 | Description: See title.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_bg_shift
 | Params: y -- pixels to raise the picture by
 | Returns: N/A
 ----------------------*/
void title_bg_set_shift(int y) {
    if (y < 0)   y = 0;
    if (y > 255) y = 255;
    if (y == g_bg_shift) return;
    g_bg_shift = y;
    SRL::Math::Types::Vector2D pos((int16_t) 0, (int16_t) y);
    SRL::VDP2::NBG0::SetPosition(pos);
}

/*----------------------
 | title_bg_loaded_file
 | Description: See title.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_nbg0_loaded
 | Params: N/A
 | Returns: the filename currently uploaded to NBG0, or ""
 ----------------------*/
const char *title_bg_loaded_file(void) {
    return g_nbg0_loaded;
}

/*----------------------
 | g_dyn_faded
 | Description: Whether the picture layer currently sits on colour offset
 |   channel B. Declared up here rather than beside title_bg_apply because the
 |   screen-wide fades above it drive that layer too.
 | Author: suinevere
 ----------------------*/
static bool g_dyn_faded = false;

/*----------------------
 | g_screen_fade
 | Description: Whether a screen-wide fade (title, splash, menu, loading screen)
 |   currently owns the picture's brightness -- true from title_bg_fade_engage
 |   until title_bg_fade_reset. While it is set, title_bg_dyn_fade records the
 |   level it was handed but writes nothing: a room transition's ramp, or a music
 |   callback's brightness restore, must not lift a blackout the fade above it
 |   put there. That is not hypothetical -- a soft reset taken mid-transition
 |   runs title_bg_fade_arm() and then music_reset(), whose restore calls
 |   title_bg_dyn_fade(255) and would show the outgoing room's picture at full
 |   brightness under black text, in the window that blackout exists to cover.
 | Author: suinevere
 ----------------------*/
static bool g_screen_fade = false;

static void title_bg_apply(int level);

/*----------------------
 | title_fade_set
 | Description: Writes one brightness offset to colour offset channel A (the
 |   text art) and drives the picture to the matching level on channel B, where
 |   the player's held wallpaper dim is composed in. Two channels rather than one
 |   because a layer can only sit on one of them, and the picture has to stay on
 |   the channel that knows about the dim -- otherwise every fade would drop the
 |   dim for its duration and pop it back at the bright end.
 | Author: suinevere
 | Dependencies: SRL (VDP2), bg_dim.h
 | Globals: N/A
 | Params: v -- -255 (black) to 0 (normal)
 | Returns: N/A
 ----------------------*/
static void title_fade_set(int v) {
    SRL::VDP2::ColorOffset off((int16_t) v, (int16_t) v, (int16_t) v);
    SRL::VDP2::SetColorOffsetA(off);
    title_bg_apply(255 + v);
}

void title_bg_fade_level(int v) {
    if (v < -255) v = -255;
    if (v > 0)    v = 0;
    title_fade_set(v);
}

/*----------------------
 | title_fade_engage / title_bg_fade_engage
 | Description: Points NBG3 -- the Z-ATURN text art over the title picture, and
 |   the console text under every menu -- at colour offset channel A, takes NBG2
 |   (the input dashboard and the menu borders) onto the same channel, and marks
 |   the screen-wide fade as owning the picture's brightness (see g_screen_fade).
 |
 |   NBG2 rides with the text rather than with the picture because it is chrome,
 |   not scenery: channel B carries the player's held wallpaper dim, and a border
 |   on B would dim whenever the wallpaper did. On A it tracks the text it frames,
 |   which is the only thing that keeps a box and its contents fading as one.
 |
 |   NBG0 is deliberately not taken. The picture layer stays on channel B with
 |   title_bg_apply for the whole session, because that is the only channel that
 |   composes the player's held wallpaper dim; title_fade_set drives it there in
 |   step with channel A instead. The two layers still fade as a unit -- the text
 |   popping to full brightness against a still-black image is exactly the
 |   half-faded look this avoids -- they just do it through one channel each.
 |
 |   This is also where SRL's NBG3-on-channel-B seed (srl_vdp2.hpp:334) is
 |   cleared, since UseColorOffset registers a scroll on one channel and clears
 |   it from the other. main() arms a fade before anything else on both the cold-
 |   boot and the soft-reset path, so the seed is always gone before
 |   title_bg_apply first claims channel B and can never drag the text along with
 |   the picture.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_screen_fade
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void title_fade_engage(void) {
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    SRL::VDP2::NBG2::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    g_screen_fade = true;
}

void title_bg_fade_engage(void) { title_fade_engage(); }

/*----------------------
 | title_bg_fade_arm
 | Description: See title.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_bg_fade_arm(void) {
    title_fade_engage();
    title_fade_set(-255);
}

/*----------------------
 | title_bg_fade_in_ex / title_bg_fade_in
 | Description: See title.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: frames -- ramp length in vblank fields; step -- optional per-frame
 |   callback receiving 0 (black) to 255 (full), or nullptr
 | Returns: N/A
 ----------------------*/
void title_bg_fade_in_ex(int frames, TitleFadeStep step) {
    if (frames <= 0) frames = 1;      /* the divisor below is not optional */
    title_fade_engage();
    for (int i = 0; i <= frames; i++) {
        const int level = (255 * i) / frames;
        title_fade_set(-255 + level);
        /* Before the Synchronize, not after: the callback's job is to have the
           sound at this brightness's level by the time this frame is shown. */
        if (step != nullptr) step(level);
        SRL::Core::Synchronize();
    }
    title_bg_fade_reset();
}

void title_bg_fade_in(int frames) { title_bg_fade_in_ex(frames, nullptr); }

/*----------------------
 | title_bg_fade_out
 | Description: See title.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: frames -- ramp length in vblank fields
 | Returns: N/A
 ----------------------*/
void title_bg_fade_out(int frames) {
    title_fade_engage();
    for (int i = 0; i <= frames; i++) {
        title_fade_set(-(255 * i) / frames);
        SRL::Core::Synchronize();
    }
}

/*----------------------
 | title_bg_fade_reset
 | Description: See title.h. Instantly restores full brightness, releases NBG3
 |   and NBG2 from channel A, and hands the picture's brightness back to the room
 |   transitions -- the end of every screen-wide fade, whoever ran it. The
 |   title_fade_set(0) is what re-lights: it drives the picture to level 255,
 |   which composes to the held wallpaper dim rather than to nothing, so the dim
 |   is still in force the frame after a fade ends. NBG0 is not released here --
 |   title_bg_apply owns that, and lets the layer go only when the composed
 |   value is neutral.
 | Author: suinevere
 | Dependencies: SRL, bg_dim.h
 | Globals: g_screen_fade
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_bg_fade_reset(void) {
    title_fade_set(0);
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
    SRL::VDP2::NBG2::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
    g_screen_fade = false;
}

/*----------------------
 | title_bg_dim_set / title_bg_dim_get
 | Description: See title.h. The value lives in bg_dim.c; this pair is the half
 |   that touches VDP2, re-applying the new offset immediately at the ramp level
 |   bg_dim_last_level() recorded rather than a hardcoded 255 -- display_apply()
 |   calls this unconditionally, including from the bottom of a room-transition
 |   fade where the screen is held at level 0, and forcing 255 there would
 |   re-light the outgoing picture to full brightness for the CD read that
 |   follows. At rest (255, the Options page's resting level) this re-applies at
 |   255 exactly as before, so live preview is unchanged.
 | Author: suinevere
 | Dependencies: bg_dim.h
 | Globals: N/A
 | Params: offset -- -255..+255, clamped by bg_dim_set
 | Returns: get returns the held offset; set returns N/A
 ----------------------*/
void title_bg_dim_set(int offset) {
    bg_dim_set(offset);
    title_bg_dyn_fade(bg_dim_last_level());
}

int title_bg_dim_get(void) { return bg_dim_get(); }

/*----------------------
 | title_bg_dyn_fade
 | Description: Dims the wallpaper alone, for the in-game transition between one
 |   room mood's picture and the next. `level` runs 0 (black) to 255 (normal), but
 |   the resting state at 255 is now the held dim from bg_dim.c rather than
 |   unmodified brightness -- bg_dim_effective composes the two, additively and
 |   clamped once, so a lightening hold still dips toward black through the ramp
 |   instead of scaling around its own resting point.
 |
 |   This cannot go through channel A: that carries NBG3, the player's text, which
 |   in game would blink out mid-sentence. The picture gets channel B on NBG0
 |   alone, leaving channel A to the text and to every screen-wide fade.
 |
 |   Swallowed while a screen-wide fade is up (g_screen_fade): the level is still
 |   recorded, so title_bg_dim_set replays at the right place afterwards, but
 |   nothing is written -- see g_screen_fade for the blackout this protects.
 |
 |   Engage/disengage happen only at the ends of a ramp, because UseColorOffset
 |   calls slColOffsetOn(0) and re-registers; the per-frame steps in between are
 |   just SetColorOffsetB value writes. Channel B is released only when the
 |   composed value is neutral -- level 255 with no hold set -- so a held dim keeps
 |   the channel claimed at rest.
 |
 |   Every call records `level` via bg_dim_note_level before anything else, so
 |   title_bg_dim_set can re-apply a changed hold at the level actually showing
 |   instead of assuming 255 -- see title_bg_dim_set.
 | Author: suinevere
 | Dependencies: SRL, bg_dim.h
 | Globals: g_dyn_faded, g_screen_fade
 | Params: level -- 0 (black) to 255 (unmodified)
 | Returns: N/A
 ----------------------*/
void title_bg_dyn_fade(int level) {
    if (g_screen_fade) {
        if (level < 0)   level = 0;
        if (level > 255) level = 255;
        bg_dim_note_level(level);
        return;
    }
    title_bg_apply(level);
}

/*----------------------
 | title_bg_apply
 | Description: title_bg_dyn_fade's write half, with no g_screen_fade guard --
 |   the one path that touches colour offset channel B. Called directly by
 |   title_fade_set, which IS the screen-wide fade and so must not be swallowed
 |   by it.
 | Author: suinevere
 | Dependencies: SRL, bg_dim.h
 | Globals: g_dyn_faded
 | Params: level -- 0 (black) to 255 (unmodified)
 | Returns: N/A
 ----------------------*/
static void title_bg_apply(int level) {
    int v;
    if (level < 0)   level = 0;
    if (level > 255) level = 255;
    bg_dim_note_level(level);   /* so a later title_bg_dim_set replays at this level */

    v = bg_dim_effective(level);

    if (v == 0) {
        if (g_dyn_faded) {
            SRL::VDP2::ColorOffset clear(0, 0, 0);
            SRL::VDP2::SetColorOffsetB(clear);
            SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
            g_dyn_faded = false;
        }
        return;
    }

    if (!g_dyn_faded) {
        SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetB);
        g_dyn_faded = true;
    }
    SRL::VDP2::ColorOffset off((int16_t) v, (int16_t) v, (int16_t) v);
    SRL::VDP2::SetColorOffsetB(off);
}

/*----------------------
 | TITLE_JINGLE_FADE_FRAMES
 | Description: How long the splash jingle takes to fade once a button is pressed,
 |   45 fields being three quarters of a second.
 |
 |   It fades HERE and nowhere earlier. The splash's own exit ramps the picture
 |   alone, so the jingle carries across the image fade and the whole title screen
 |   at full level; this is the first and only place it comes down, which is what
 |   makes the boot read as one piece of music rather than two.
 | Author: suinevere
 ----------------------*/
#define TITLE_JINGLE_FADE_FRAMES 45

/*----------------------
 | title_drain_input
 | Description: Throws away input that arrived while the caller was blocked, so the
 |   wait loop starts from what the player is doing now rather than what they did
 |   during a load.
 |
 |   The block above it is CD work with no Synchronize in it, which means no field
 |   ever ticked and nothing sampled the pads. Two things are then stale at once and
 |   they need opposite treatment. The keyboard is a QUEUE, so a key struck during
 |   the load is still sitting in it and the loop's first poll would read it as the
 |   press that dismisses the title -- the screen would appear and vanish in the
 |   same breath. The pads are EDGES against the last sample taken, which is now
 |   minutes old, so a button held across the load reads as freshly pressed. Drain
 |   the one, and re-baseline the other by spending two fields: the first re-samples
 |   the pads, the second gives WasPressed a previous-frame state that matches.
 |
 |   Runs even when the block above found everything cached and blocked for no time
 |   at all. What it drops then is the press that skipped the splash, which the
 |   splash has already consumed and which would otherwise dismiss the title in the
 |   same breath -- so the case that looks like it does not need this is the one that
 |   most does.
 | Author: suinevere
 | Dependencies: input.h, saturn_keyboard.h, SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void title_drain_input(void) {
    for (int field = 0; field < 2; field++) {
        int guard = 0;
        while (guard < 64 && saturn_keyboard_poll().kind != SATURN_KEY_NONE) guard++;
        SRL::Core::Synchronize();
    }
}

/*----------------------
 | title_and_seed
 | Description: Finishes the boot's loading, then displays the title screen with a
 |   "Press any button" prompt and waits for input, returning a seed made from the
 |   frames the player took. Also handles soft reset chords while waiting.
 |
 |   The art goes up first and the prompt only once the loading is done, because the
 |   prompt is a promise that pressing does something. Everything called here is
 |   idempotent, which is what lets the splash do as much or as little as it likes:
 |   on an unskipped logo the block below finds its work already done and the prompt
 |   lands in the same frame as the art, and on a skipped one it does all of it and
 |   the player waits, looking at the title.
 |
 |   No cold-boot/return distinction, because the splash runs in full on both.
 | Author: suinevere
 | Dependencies: console_view.h, input.h, online.h, boot_music.h, SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: a random seed integer
 ----------------------*/
int title_and_seed(void) {
    int frames = 0;
    int reset_hold = 0;
    for (int r = 0; r < console_screen_rows(); r++) text_clear_line(r);
    title_draw_art();
    menu_sync();

    preload_game_catalog();
    title_drain_input();

    text_print(8, 18, "Press any button to begin");
    menu_sync();

    for (;;) {
        reset_hold = soft_reset_chord_held() ? (reset_hold + 1) : 0;
        if (reset_hold >= SOFT_RESET_HOLD) {
            slNMIRequest();
            while (1) {}
        }
        bool advance =
            g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
            g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START) ||
            (saturn_keyboard_poll().kind != SATURN_KEY_NONE);
        if (advance) break;
        title_draw_art();
        text_print(8, 18, "Press any button to begin");
        menu_sync();   // not a bare Synchronize: the title track needs the mixer ticked
        frames++;
    }

    // Only when there is something to fade. boot_music_set_level moves the driver's
    // MASTER volume, and boot_music_stop's restore of it rides on the scrub's sixty
    // frames -- which boot_music_stop skips when no channel is open. Ramping with
    // nothing playing therefore walks the master down to zero and issues the restore
    // in the same field, where this driver drops it (see boot_master_restore), and
    // the machine is silent from there on: no loading cue on the next game, no PCM
    // at all. CD-DA is separate hardware, which is why the menu track still plays
    // and the fault looks like it belongs to the loading screen.
    if (boot_music_playing()) {
        for (int i = TITLE_JINGLE_FADE_FRAMES; i >= 0; i--) {
            boot_music_set_level((BOOT_MUSIC_LEVEL_MAX * i) / TITLE_JINGLE_FADE_FRAMES);
            SRL::Core::Synchronize();
        }
    }
    boot_music_stop();
    preload_game_catalog();
    music_start_menu();
    return frames | 1;
}
