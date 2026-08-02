/*----------------------
 | title.cxx
 | Description: Title screen, background art, TGA loading and its Low Work RAM
 |   cache, CD directory juggling, and the boot sequence random seed.
 | Author: suinevere
 | Dependencies: app_state.h, display.h, menu.h, soft_reset.h, game_catalog.h
 |   (the Z3 directory record cd_restore_z3 re-applies), SRL
 ----------------------*/
#include "title.h"
#include "app_state.h"
#include "display.h"
#include "menu.h"
#include "console_view.h"
#include "input.h"
#include "soft_reset.h"
#include "saturn_keyboard.h"
#include "game_catalog.h"
#include <srl.hpp>
#include <string.h>

/*----------------------
 | SOFT_RESET_HOLD
 | Description: Frames the reset chord must be held on the title screen before it
 |   triggers a hard NMI reboot.
 | Author: suinevere
 ----------------------*/
#define SOFT_RESET_HOLD 60

/*----------------------
 | g_image_name / g_image_ptr
 | Description: The registered background filenames (owned storage) and a pointer
 |   array over them, handed to the display system as the selectable image list.
 | Author: suinevere
 ----------------------*/
static char        g_image_name[DISP_IMAGE_MAX][16];
static const char *g_image_ptr[DISP_IMAGE_MAX];

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
 | g_tga_dirnames / g_tga_tbl / g_tga_dir_valid
 | Description: The /TGA directory record captured by display_scan_images, so
 |   cd_enter_tga can switch to the background-art folder; the flag guards it.
 | Author: suinevere
 ----------------------*/
static GfsDirName g_tga_dirnames[SRL_MAX_CD_FILES];
static GfsDirTbl  g_tga_tbl;
static bool       g_tga_dir_valid = false;

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
    SRL::Debug::Print(13, 12, "Z - A T U R N");
    SRL::Debug::Print(4, 15, "Saturn port (c) 2026 by Suinevere");
}

/*----------------------
 | tga_name_is_usable
 | Description: Checks if a filename ends with exactly ".TGA".
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: name -- the filename to check
 | Returns: true if the name ends in .TGA, false otherwise
 ----------------------*/
static bool tga_name_is_usable(const char *name) {
    if (!name || !name[0] || name[0] == '.') return false;
    for (int i = 0; name[i]; i++) {
        if (name[i] == '.' && name[i+1] == 'T' && name[i+2] == 'G' && name[i+3] == 'A'
            && name[i+4] == '\0') return true;
    }
    return false;
}

/*----------------------
 | cd_capture_root
 | Description: Snapshots the root directory record straight after GFS_Reset()
 |   so that cd_enter_root() can return to it later.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_root_tbl, g_root_dirnames, g_root_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void cd_capture_root(void) {
    GFS_DIRTBL_TYPE(&g_root_tbl)    = GFS_DIR_NAME;
    GFS_DIRTBL_DIRNAME(&g_root_tbl) = g_root_dirnames;
    GFS_DIRTBL_NDIR(&g_root_tbl)    = SRL_MAX_CD_FILES;
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
 | Description: Sets the CD current directory to /TGA where the background bitmaps
 |   are stored.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_tga_tbl, g_tga_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void cd_enter_tga(void) {
    if (g_tga_dir_valid) GFS_SetDir(&g_tga_tbl);
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
 | display_scan_images
 | Description: Scans the disc's TGA folder once at boot and registers every usable
 |   .TGA file found with the display system, which is what lets display.c resolve a
 |   category pool's filename to a slot. SUINE.TGA is deliberately skipped even
 |   though it lives in the same folder -- it is the boot splash logo (see
 |   splash.cxx), not a room background, and registering it would put the logo in
 |   the rotation on a disc whose art was otherwise complete.
 |
 |   The registered list is names only and costs no picture memory; how many
 |   pictures are actually decoded and held is TGA_CACHE_SLOTS, which is a different
 |   and much smaller number. So DISP_IMAGE_MAX only has to be at least the number
 |   of files in /TGA -- past that the scan stops and the tail of the folder, in
 |   alphabetical order, becomes unreachable to every category that names it.
 | Author: suinevere
 | Dependencies: display.c, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void display_scan_images(void) {
    GfsDirName *dirnames = g_tga_dirnames;
    GfsDirTbl  *tblp     = &g_tga_tbl;
    int found = 0;

    g_tga_dir_valid = false;

    cd_enter_root();

    int32_t fid = GFS_NameToId((int8_t *) "TGA");
    if (fid >= 0) {
        GFS_DIRTBL_TYPE(tblp)    = GFS_DIR_NAME;
        GFS_DIRTBL_DIRNAME(tblp) = dirnames;
        GFS_DIRTBL_NDIR(tblp)    = SRL_MAX_CD_FILES;
        int32_t count = GFS_LoadDir(fid, tblp);
        g_tga_dir_valid = count >= 0;
        for (int32_t i = 0; count > 0 && i < count && found < DISP_IMAGE_MAX; i++) {
            char nm[16];
            int j = 0;
            for (; j < GFS_FNAME_LEN && j < (int) sizeof(nm) - 1; j++) {
                char c = (char) dirnames[i].fname[j];
                if (c == '\0' || c == ';') break;
                nm[j] = c;
            }
            nm[j] = '\0';
            if (!tga_name_is_usable(nm)) continue;
            if (strcmp(nm, "SUINE.TGA") == 0) continue;  // boot splash logo, not a selectable background
            int k = 0;
            for (; nm[k] && k < (int) sizeof(g_image_name[0]) - 1; k++) g_image_name[found][k] = nm[k];
            g_image_name[found][k] = '\0';
            g_image_ptr[found] = g_image_name[found];
            found++;
        }
    }

    cd_enter_root();
    display_set_images(found > 0 ? g_image_ptr : NULL, found);
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
 | Description: One decoded background: the 8bpp pixel plane (top-down, leading
 |   partial sector already shifted off) plus its 256-entry palette. Pixels is the
 |   allocation base, so freeing it frees the plane. LowRam records which allocator
 |   the two blocks came from -- a cached image lives in Low Work RAM, a one-off the
 |   cache could not take lives in High Work RAM.
 |
 |   Cap is what makes a cache slot different from a one-off. A one-off's plane is
 |   allocated to the exact size that picture needs and freed with it, so Cap is 0.
 |   A cache slot's plane is allocated once at TGA_PLANE_MAX and then reused for
 |   every picture that passes through it, so Cap is that capacity and the block
 |   outlives any one image -- which is what keeps the zone from fragmenting as
 |   pictures come and go (see the TGA_CACHE_SLOTS box).
 |
 |   LastUse is the cache clock reading at the last hit, and is what eviction picks
 |   on. Meaningless on a one-off.
 | Author: suinevere
 ----------------------*/
struct TgaImage {
    uint8_t               *Pixels;
    SRL::Types::HighColor *Colors;
    uint32_t               Bytes;    /* what this image cost its allocator */
    uint32_t               Cap;      /* plane capacity if this is a reusable slot, else 0 */
    uint16_t               W, H;
    bool                   LowRam;
    uint32_t               LastUse;
    char                   Name[16];
};

/*----------------------
 | LWRAM_TOTAL / TGA_CACHE_SLOTS / TGA_PLANE_MAX / TGA_PAL_BYTES /
 |   TGA_SLOT_BYTES / TGA_CACHE_FLOOR
 | Description: The background-art cache: how many decoded pictures Low Work RAM
 |   holds at once, and what one costs. The Saturn plays CD-DA off the same head it
 |   reads data with, so any read stops the music -- which is what this cache
 |   exists to avoid, and what it can no longer avoid completely.
 |
 |   It used to hold every picture on the disc, decoded once during the boot
 |   splash. That worked at eight pictures (~580 KB) and does not at thirty-seven
 |   (~2.6 MB) -- LWRAM is 1 MB in total and the art is not its only resident. So
 |   the cache is now a fixed set of slots filled on demand, least-recently-used
 |   evicted, and a picture that misses costs one CD read. The read is not free but
 |   it is well placed: the engine only ever changes the wallpaper at the bottom of
 |   a fade, where the screen is already black and the CD-DA track is being swapped
 |   anyway (see commit_pending in sound/music.c), so the drive is interrupted at
 |   the one moment in a session when nothing is depending on it.
 |
 |   Slots are allocated once, at TGA_PLANE_MAX each, and then reused rather than
 |   freed and re-taken. Deliberate: the shipped pictures differ by a few hundred
 |   bytes (the colour map's length varies), so free-and-reallocate would leave
 |   TLSF holes slightly too small for the next picture and the cache would decay
 |   into misses over a long session -- a failure that shows up as the music
 |   skipping more and more the longer you play, which is close to undiagnosable
 |   from a bug report. TGA_PLANE_MAX is a 320x224 plane plus one sector, the
 |   leading partial sector tga_decode shifts off; a picture larger than that
 |   cannot take a slot and falls back to the one-off path, which is correct rather
 |   than merely tolerable, since nothing on the disc is larger (tools/make_tga.py
 |   enforces 320x224).
 |
 |   The other LWRAM residents are why the slot count is what it is. The typeahead
 |   trie, up to ~318 KB for Wishbringer, is allocated during the splash and lives
 |   for the whole session. LOADCD.PCM, the loading screen's cue, peaks at ~172 KB
 |   (LOADING_MUSIC_MAX_BYTES in sound/loading_music.cxx) and is freed before the
 |   game starts. The save/restore scratch is ~47 KB while a save is written. Eight
 |   slots is ~580 KB, which leaves those three fighting over ~440 KB -- so the
 |   cache also refuses to grow a slot that would drop the zone's free space below
 |   TGA_CACHE_FLOOR. That check, not the slot count, is what actually protects
 |   them: it adapts to whichever game is loaded instead of assuming the worst-case
 |   trie on every disc.
 |
 |   SPLASH.PCM used to belong in this list, as the one resident held WHILE the
 |   cache was being filled. It is not any more -- nothing fills the cache during
 |   the splash now -- so the jingle has the zone to itself and its
 |   BOOT_MUSIC_MAX_SECONDS cap is no longer load-bearing against the art.
 | Author: suinevere
 ----------------------*/
#define LWRAM_TOTAL       (1024u * 1024u)
#define TGA_CACHE_SLOTS   8
#define TGA_PLANE_MAX     (320u * 224u + 2048u)
#define TGA_PAL_BYTES     (256u * sizeof(SRL::Types::HighColor))
#define TGA_SLOT_BYTES    (TGA_PLANE_MAX + TGA_PAL_BYTES)
#define TGA_CACHE_FLOOR   (256u * 1024u)   /* left for LOADCD.PCM + save scratch */

/*----------------------
 | g_cache / g_cache_count / g_cache_clock
 | Description: The decoded-background cache: the slots, how many have been
 |   allocated so far (it grows on demand and never shrinks), and a monotonic
 |   counter stamped into TgaImage::LastUse on every hit so eviction can tell which
 |   slot has gone longest unwanted.
 |
 |   Plain statics, so a soft reset returns to a cache the longjmp left intact --
 |   the same property the game catalogue relies on. Nothing needs re-reading.
 | Author: suinevere
 ----------------------*/
static TgaImage g_cache[TGA_CACHE_SLOTS];
static int      g_cache_count = 0;
static uint32_t g_cache_clock = 0;

/*----------------------
 | tga_alloc
 | Description: Allocates from Low or High Work RAM, whichever the caller asked
 |   for, so one decoder can serve both the cache and a one-off load.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: bytes -- allocation size; low -- true for Low Work RAM
 | Returns: the allocation, or nullptr
 ----------------------*/
static void *tga_alloc(uint32_t bytes, bool low) {
    return low ? SRL::Memory::LowWorkRam::Malloc(bytes)
               : SRL::Memory::HighWorkRam::Malloc(bytes);
}

/*----------------------
 | tga_free
 | Description: Returns a block to the zone tga_alloc took it from. Null-safe.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: p -- the block; low -- true if it came from Low Work RAM
 | Returns: N/A
 ----------------------*/
static void tga_free(void *p, bool low) {
    if (p == nullptr) return;
    if (low) SRL::Memory::LowWorkRam::Free(p);
    else     SRL::Memory::HighWorkRam::Free(p);
}

/*----------------------
 | tga_free_space
 | Description: Free space in whichever zone the caller is about to allocate in.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: low -- true for Low Work RAM
 | Returns: free bytes in that zone
 ----------------------*/
static uint32_t tga_free_space(bool low) {
    return low ? (uint32_t) SRL::Memory::LowWorkRam::GetFreeSpace()
               : (uint32_t) SRL::Memory::HighWorkRam::GetFreeSpace();
}

/*----------------------
 | tga_image_free
 | Description: Releases both blocks of a decoded image and blanks it, so the
 |   slot reads as empty afterwards.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: img -- the image to release
 | Returns: N/A
 ----------------------*/
static void tga_image_free(TgaImage *img) {
    if (img == nullptr) return;
    tga_free(img->Pixels, img->LowRam);
    tga_free(img->Colors, img->LowRam);
    img->Pixels = nullptr;
    img->Colors = nullptr;
    img->Bytes  = 0;
    img->Cap    = 0;
}

/*----------------------
 | tga_decode
 | Description: Reads an uncompressed 8bpp colour-mapped TGA off the CD and
 |   decodes it into RAM: palette expanded to 256 entries, rows flipped to
 |   top-down, and the leading partial sector shifted off the front. This is the
 |   only function here that touches the disc, so it is also the only one that
 |   interrupts CD audio.
 |
 |   Two modes, told apart by out->Cap on entry. Zero means "allocate": the plane
 |   is taken at exactly the size this picture needs and the caller owns it. Non-
 |   zero means out->Pixels and out->Colors are already-owned buffers of that
 |   capacity and this decodes into them, allocating nothing -- which is how a
 |   cache slot is refilled without ever returning its memory to the allocator.
 |   A picture too big for the capacity is refused rather than truncated, and the
 |   slot keeps whatever it held.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: file -- filename on the disc; low -- true to allocate in Low Work RAM
 |   (ignored when reusing); limit -- most bytes the pixel plane may take; out --
 |   filled on success, Cap read on entry and preserved
 | Returns: true on success; on failure out is left blank (allocating mode, nothing
 |   allocated) or untouched apart from Name (reusing mode)
 ----------------------*/
static bool tga_decode(const char *file, bool low, uint32_t limit, TgaImage *out) {
    /* Captured before the blanking below, which would otherwise drop the very
       buffers this is meant to refill. Both blocks have to be there, not just a
       capacity: half a slot would blank one pointer and then write through the
       other, leaking it. */
    const bool reusing = (out->Cap > 0 && out->Pixels != nullptr
                          && out->Colors != nullptr);
    const uint32_t               reuse_cap = reusing ? out->Cap    : 0;
    uint8_t               *const reuse_pix = reusing ? out->Pixels : nullptr;
    SRL::Types::HighColor *const reuse_col = reusing ? out->Colors : nullptr;

    if (!reusing) {
        out->Pixels = nullptr;
        out->Colors = nullptr;
        out->Cap    = 0;
        out->Bytes  = 0;
        out->LowRam = low;
    }
    out->W       = 0;
    out->H       = 0;
    out->Name[0] = '\0';

    SRL::Cd::File f(file);
    if (!f.Exists()) return false;

    static uint32_t hdrbuf[512];
    uint8_t *const hdr = (uint8_t *) hdrbuf;
    const int32_t ss = (f.Size.SectorSize > 0) ? f.Size.SectorSize : 2048;
    if (ss > (int32_t) sizeof(hdrbuf)) return false;
    if (f.LoadBytes(0, ss, hdr) <= 0) return false;

    const int idlen    = hdr[0];
    const int cmaptype = hdr[1];
    const int imgtype  = hdr[2];
    const int cmaplen  = hdr[5] | (hdr[6] << 8);
    const int cmapbits = hdr[7];
    const int w        = hdr[12] | (hdr[13] << 8);
    const int h        = hdr[14] | (hdr[15] << 8);
    const int bpp      = hdr[16];
    const int topdown  = (hdr[17] >> 5) & 1;

    if (cmaptype != 1 || imgtype != 1 || bpp != 8)      return false;
    if (cmaplen <= 0 || cmaplen > 256)                  return false;
    if (cmapbits != 24 && cmapbits != 32)               return false;
    if (w <= 0 || h <= 0 || w > 1024 || h > 512)        return false;

    const int      cmapbytes = cmaplen * (cmapbits / 8);
    const int      pixoff    = 18 + idlen + cmapbytes;
    const uint32_t npix      = (uint32_t) w * (uint32_t) h;
    if (pixoff > ss)                                    return false;
    if ((uint32_t) pixoff + npix > (uint32_t) f.Size.Bytes) return false;

    const uint32_t skip    = (uint32_t) (pixoff % ss);
    const uint32_t span    = skip + npix;
    const uint32_t palsize = 256 * sizeof(SRL::Types::HighColor);
    if (reuse_pix != nullptr) {
        /* Refusing rather than truncating: a short plane would render as a band of
           the previous picture across the bottom of this one. */
        if (span > reuse_cap) return false;
    } else {
        if (span + palsize > limit)                     return false;
        if (tga_free_space(low) < span + palsize + 4096) return false;
    }

    SRL::Types::HighColor *colors = reuse_col;
    if (colors == nullptr) colors = (SRL::Types::HighColor *) tga_alloc(palsize, low);
    if (colors == nullptr) return false;
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

    uint8_t *pix = reuse_pix;
    if (pix == nullptr) pix = (uint8_t *) tga_alloc(span, low);
    // LoadBytes wants a long-aligned destination; refuse rather than corrupt.
    if (pix == nullptr || ((unsigned int) pix & 3) != 0) {
        if (reuse_pix == nullptr) { tga_free(pix, low); tga_free(colors, low); }
        return false;
    }
    if (f.LoadBytes((size_t) (pixoff / ss), (int32_t) span, pix) <= 0) {
        if (reuse_pix == nullptr) { tga_free(pix, low); tga_free(colors, low); }
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
    // A reused slot already paid for its buffers when they were taken and does not
    // pay again per picture, so its cost stays whatever the slot cost.
    if (reuse_pix == nullptr) out->Bytes = span + palsize;
    out->W      = (uint16_t) w;
    out->H      = (uint16_t) h;
    int k = 0;
    for (; file[k] && k < (int) sizeof(out->Name) - 1; k++) out->Name[k] = file[k];
    out->Name[k] = '\0';
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
    if (bmp.Pal == nullptr) { delete colors; return false; }

    SRL::VDP2::NBG0::LoadBitmap(&bmp);
    return true;
}

/*----------------------
 | tga_name_eq
 | Description: Compares two disc filenames, bounded by the cache's name field.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a, b -- the names to compare
 | Returns: true if they match
 ----------------------*/
static bool tga_name_eq(const char *a, const char *b) {
    for (int i = 0; i < (int) sizeof(g_cache[0].Name); i++) {
        if (a[i] != b[i])  return false;
        if (a[i] == '\0')  return true;
    }
    return true;
}

/*----------------------
 | tga_cache_find
 | Description: Looks up an already-decoded image by disc filename.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cache, g_cache_count
 | Params: file -- the disc filename
 | Returns: the cached image, or nullptr if it is not held
 ----------------------*/
static const TgaImage *tga_cache_find(const char *file) {
    for (int i = 0; i < g_cache_count; i++) {
        if (g_cache[i].Pixels != nullptr && tga_name_eq(g_cache[i].Name, file)) {
            g_cache[i].LastUse = ++g_cache_clock;
            return &g_cache[i];
        }
    }
    return nullptr;
}

/*----------------------
 | tga_cache_slot
 | Description: The slot the next picture should be decoded into: a newly grown one
 |   while the cache is still below TGA_CACHE_SLOTS and Low Work RAM can spare it,
 |   otherwise the least recently used of the ones already held.
 |
 |   Growth is checked against live free space rather than a fixed budget, because
 |   the other residents of the zone are not fixed either -- the typeahead trie is
 |   whatever the loaded game's vocabulary needs. Refusing to grow is not a
 |   failure: the cache simply stays smaller and evicts more often.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_cache, g_cache_count
 | Params: N/A
 | Returns: the slot to use, or nullptr if none could be had
 ----------------------*/
static TgaImage *tga_cache_slot(void) {
    if (g_cache_count < TGA_CACHE_SLOTS) {
        const uint32_t free_now = tga_free_space(true);
        if (free_now >= TGA_SLOT_BYTES + TGA_CACHE_FLOOR) {
            TgaImage *slot = &g_cache[g_cache_count];
            slot->Pixels = (uint8_t *) tga_alloc(TGA_PLANE_MAX, true);
            slot->Colors = (SRL::Types::HighColor *) tga_alloc(TGA_PAL_BYTES, true);
            // LoadBytes wants a long-aligned destination, and a slot that cannot
            // give it one is worse than no slot: it would fail every decode
            // forever. Hand the memory straight back and fall through to eviction.
            if (slot->Pixels != nullptr && slot->Colors != nullptr
                && ((unsigned int) slot->Pixels & 3) == 0) {
                slot->Cap     = TGA_PLANE_MAX;
                slot->Bytes   = TGA_SLOT_BYTES;
                slot->LowRam  = true;
                slot->Name[0] = '\0';
                g_cache_count++;
                return slot;
            }
            tga_free(slot->Pixels, true);
            tga_free(slot->Colors, true);
            slot->Pixels = nullptr;
            slot->Colors = nullptr;
        }
    }

    if (g_cache_count <= 0) return nullptr;
    TgaImage *lru = &g_cache[0];
    for (int i = 1; i < g_cache_count; i++)
        if (g_cache[i].LastUse < lru->LastUse) lru = &g_cache[i];
    return lru;
}

/*----------------------
 | tga_cache_admit
 | Description: Decodes an image from the CD into a cache slot, growing the cache
 |   or evicting its least recently used picture to find one. The caller must
 |   already be in the TGA directory. Reads the disc, so it stops CD audio.
 |
 |   Evicting the picture currently on screen is harmless and not guarded against:
 |   tga_blit_nbg0 copies into VRAM, so once a picture has been uploaded the RAM
 |   copy is only of interest to the next request for that same file.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_cache, g_cache_clock
 | Params: file -- the disc filename
 | Returns: the newly cached image, or nullptr if no slot could be had or the file
 |   is unreadable
 ----------------------*/
static const TgaImage *tga_cache_admit(const char *file) {
    TgaImage *slot = tga_cache_slot();
    if (slot == nullptr) return nullptr;

    // Cap is set, so this decodes into the slot's own buffers and allocates
    // nothing. It clears Name before it can fail, which is what stops a slot
    // holding the previous picture's pixels under the previous picture's name
    // from being handed out as a hit afterwards.
    if (!tga_decode(file, true, TGA_SLOT_BYTES, slot)) return nullptr;

    slot->LastUse = ++g_cache_clock;
    return slot;
}

/*----------------------
 | title_bg_show
 | Description: Shows a TGA image on VDP2 NBG0 behind the title text (menus and
 |   gameplay stay on solid black). Serves it from the Low Work RAM cache on a hit,
 |   which keeps the CD idle and the music playing; on a miss it reads the disc,
 |   which stops CD-DA for the length of the read, and takes a cache slot so the
 |   same picture is free next time. Accepts only uncompressed 8bpp colour-mapped
 |   TGA. Returns false if the load fails or the format is unsupported, so the
 |   caller can fall back to a colour background.
 |
 |   Callers that can be reached with music playing should treat this as
 |   disc-touching and cover it -- either by picking their moment (the engine
 |   changes the wallpaper only at the bottom of a fade) or by pausing the track
 |   around it (the Options menu does).
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: file -- filename of the TGA image to load
 | Returns: true if the requested display was applied; false if it fell back
 ----------------------*/
bool title_bg_show(const char *file) {
    static char loaded[16] = "";
    bool same = true;
    for (int i = 0; i < (int) sizeof(loaded); i++) {
        if (loaded[i] != file[i]) { same = false; break; }
        if (loaded[i] == '\0') break;
    }
    if (!same) {
        // A cached image uploads straight from RAM with no CD access. A miss reads
        // the disc and takes a slot; the one-off below is the last resort for when
        // even a slot could not be had (Low Work RAM too full to grow the cache and
        // nothing in it to evict), and is decoded into High Work RAM and dropped
        // again. Cap 0 marks it as owning its own exact-fit plane rather than
        // borrowing a slot's.
        TgaImage        oneoff   = { nullptr, nullptr, 0, 0, 0, 0, false, 0, { '\0' } };
        bool            borrowed = false;
        const TgaImage *img      = tga_cache_find(file);
        if (img == nullptr) {
            cd_enter_tga();
            img = tga_cache_admit(file);
            if (img == nullptr && tga_decode(file, false, 0xFFFFFFFFu, &oneoff)) {
                img = &oneoff; borrowed = true;
            }
            bitmap_read_end();
        }
        if (img == nullptr) return false;

        bool ok = tga_blit_nbg0(img);
        if (borrowed) tga_image_free(&oneoff);
        if (!ok) {
            return false;
        }
        SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer1);
        SRL::Debug::PrintClearLine(20);
        SRL::Debug::PrintClearLine(21);
        int k = 0;
        for (; file[k] && k < (int) sizeof(loaded) - 1; k++) loaded[k] = file[k];
        loaded[k] = '\0';
    }
    SRL::VDP2::NBG0::ScrollEnable();
    return true;
}

/*----------------------
 | title_bg_show_oneoff
 | Description: Always reads and decodes fresh into a High Work RAM buffer,
 |   uploads it to NBG0, and frees the buffer immediately -- unlike
 |   title_bg_show, it never calls tga_cache_admit, so it cannot take one of the
 |   TGA_CACHE_SLOTS cache slots or evict a room background out of one. Intended
 |   for images outside the registered set, such as the boot splash.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: file -- filename of the TGA image to load
 | Returns: true if the requested display was applied; false on failure
 ----------------------*/
bool title_bg_show_oneoff(const char *file) {
    TgaImage oneoff = { nullptr, nullptr, 0, 0, 0, 0, false, 0, { '\0' } };
    cd_enter_tga();
    bool decoded = tga_decode(file, false, 0xFFFFFFFFu, &oneoff);
    bitmap_read_end();
    if (!decoded) return false;

    bool ok = tga_blit_nbg0(&oneoff);
    tga_image_free(&oneoff);
    if (!ok) return false;

    SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer1);
    SRL::VDP2::NBG0::ScrollEnable();
    return true;
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
 | title_fade_set
 | Description: Writes one brightness level to VDP2 color offset channel A,
 |   the single register every title-screen fade step goes through. `v` runs
 |   -255 (black) to 0 (unmodified). SetColorOffsetA takes a non-const
 |   reference, so the ColorOffset must be a named local rather than a
 |   temporary.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: v -- signed color offset, -255..0
 | Returns: N/A
 ----------------------*/
/*----------------------
 | g_dyn_faded
 | Description: Whether title_bg_dyn_fade currently holds NBG0 on colour offset
 |   channel B. Declared up here rather than beside its function because
 |   title_fade_engage has to clear it: the two fade systems share NBG0 and
 |   whichever engaged last owns it.
 | Author: suinevere
 ----------------------*/
static bool g_dyn_faded = false;

/*----------------------
 | title_fade_set
 | Description: Writes one brightness offset to colour offset channel A.
 | Author: suinevere
 | Dependencies: SRL (VDP2)
 | Globals: N/A
 | Params: v -- -255 (black) to 0 (normal)
 | Returns: N/A
 ----------------------*/
static void title_fade_set(int v) {
    SRL::VDP2::ColorOffset off((int16_t) v, (int16_t) v, (int16_t) v);
    SRL::VDP2::SetColorOffsetA(off);
}

/*----------------------
 | title_fade_engage
 | Description: Points both title-screen layers -- NBG0 (the title picture)
 |   and NBG3 (the Z-ATURN text art drawn over it) -- at color offset channel
 |   A, so one SetColorOffsetA write dims the whole title screen as a unit.
 |   Sharing one channel rather than driving NBG3 from channel B is deliberate:
 |   the two always fade together and never need separate values, and the text
 |   popping to full brightness against a still-black image is exactly the
 |   half-faded look this avoids.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void title_fade_engage(void) {
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    // Two fade systems share NBG0 and the last engage owns it. Claiming the layer
    // for channel A drops title_bg_dyn_fade's claim on channel B, so its next
    // disengage does not reach in and pull NBG0 back off A. That is not
    // hypothetical: a soft reset taken mid-transition runs title_bg_fade_arm()
    // (main.cxx:150) and then music_reset(), whose brightness restore calls
    // title_bg_dyn_fade(255) -- which, still believing it held the layer, would
    // undo the blackout for the wallpaper alone and show the previous room's
    // picture at full brightness under black text, in the window that comment
    // says is not meant to be seen.
    g_dyn_faded = false;
}

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
 | Description: See title.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_bg_fade_reset(void) {
    title_fade_set(0);
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
}

/*----------------------
 | title_bg_dyn_fade
 | Description: Dims the wallpaper alone, for the in-game transition between one
 |   room mood's picture and the next. `level` runs 0 (black) to 255 (normal).
 |
 |   This cannot go through title_fade_engage: that points NBG0 AND NBG3 at
 |   channel A so the title screen dims as a unit, which in game would blink the
 |   player's text out mid-sentence. The wallpaper gets channel B on NBG0 alone,
 |   leaving channel A entirely to the screen-wide page and title fades.
 |
 |   NBG3 has to be cleared off channel B explicitly first. SRL seeds
 |   OffsetBScrolls to NBG3ON (srl_vdp2.hpp:334), and UseColorOffset re-registers
 |   BOTH masks from those bitfields on every call -- so engaging NBG0 on B without
 |   this would drag the text onto B as well and fade exactly what must stay lit.
 |
 |   Engage/disengage happen only at the ends of a ramp, because UseColorOffset
 |   calls slColOffsetOn(0) and re-registers; the per-frame steps in between are
 |   just SetColorOffsetB value writes.
 |
 |   A screen-wide fade taking NBG0 releases this claim: title_fade_engage clears
 |   g_dyn_faded, so a ramp interrupted by one (a soft reset mid-transition) does
 |   not later disengage a layer it no longer holds. Nothing restores NBG3 to
 |   channel B afterwards, which is deliberate -- SRL seeds it there and no fade
 |   in this file wants it, so the seed is cleared for the session and inert.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_dyn_faded
 | Params: level -- 0 (black) to 255 (unmodified)
 | Returns: N/A
 ----------------------*/
void title_bg_dyn_fade(int level) {
    if (level < 0)   level = 0;
    if (level > 255) level = 255;

    if (level >= 255) {
        if (g_dyn_faded) {
            SRL::VDP2::ColorOffset clear(0, 0, 0);
            SRL::VDP2::SetColorOffsetB(clear);
            SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
            g_dyn_faded = false;
        }
        return;
    }

    if (!g_dyn_faded) {
        // Take NBG3 off both channels BEFORE putting NBG0 on B -- see above.
        SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
        SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetB);
        g_dyn_faded = true;
    }
    SRL::VDP2::ColorOffset off((int16_t)(level - 255), (int16_t)(level - 255),
                               (int16_t)(level - 255));
    SRL::VDP2::SetColorOffsetB(off);
}

/*----------------------
 | title_and_seed
 | Description: Displays the title screen with a "Press any button" prompt and waits
 |   for user input. Returns a random seed based on the number of elapsed frames.
 |   Also handles soft reset chords while waiting on this screen.
 | Author: suinevere
 | Dependencies: console_view.h, input.h, SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: a random seed integer
 ----------------------*/
int title_and_seed(void) {
    int frames = 0;
    int reset_hold = 0;
    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
    SRL::Core::Synchronize();
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
        SRL::Debug::Print(8, 18, "Press any button to begin");
        menu_sync();   // not a bare Synchronize: the title track needs the mixer ticked
        frames++;
    }
    return frames | 1;
}
