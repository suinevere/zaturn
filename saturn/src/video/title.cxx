/*----------------------
 | title.cxx
 | Description: Title screen, background art, TGA loading and its Low Work RAM
 |   cache, CD directory juggling, and the boot sequence random seed.
 | Author: suinevere
 | Dependencies: app_state.h, display.h, scene/scene_map.h, scene/title_art.inc,
 |   menu.h, soft_reset.h, game_catalog.h (the Z3 directory record
 |   cd_restore_z3 re-applies), online.h, boot_music.h, sound/music.h,
 |   text_map.h, bg_dim.h, SRL
 ----------------------*/
#include "title.h"
#include "bg_dim.h"
#include "app_state.h"
#include "display.h"
#include "scene/scene_map.h"   /* SCENE_N: the scenes the game-start warm walks */
#include "scene/title_art.inc" /* TITLE_ART_N: the shared title-folder picture count */
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
 | cd_enter_mood
 | Description: Enters the on-disc folder a background lives in and hands back the
 |   bare filename to open there. Every image on the disc -- "SUINE.TGA" and
 |   "HORROR/07.TGA" alike -- lives under /TGA (tools/make_tga.py lays it out that
 |   way), so this always climbs to root and steps into TGA first; a `path` that
 |   names a mood before the slash then takes one further step into that mood's
 |   subfolder, and a bare `path` (no slash) stays in /TGA, which is where
 |   SUINE.TGA is.
 |
 |   The working directory is process-wide, so the caller owes a cd_enter_root()
 |   once the open (or the failed attempt to make one) is done -- left standing in
 |   /TGA or /TGA/HORROR, a game catalog scan that runs GFS_LoadDir on /Z3 finds
 |   nothing (see game_catalog.cxx).
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: path -- "HORROR/07.TGA" or "SUINE.TGA"; leaf -- receives "07.TGA" or
 |   "SUINE.TGA"
 | Returns: true when the leaf can now be opened in the current directory
 ----------------------*/
static bool cd_enter_mood(const char *path, const char **leaf) {
    char dir[16];
    int i = 0;

    while (path[i] && path[i] != '/' && i < (int) sizeof(dir) - 1) {
        dir[i] = path[i];
        i++;
    }

    cd_enter_root();
    if (SRL::Cd::ChangeDir("TGA") < 0) { *leaf = path; return false; }

    if (path[i] != '/') { *leaf = path; return true; }
    dir[i] = '\0';
    *leaf = path + i + 1;
    return SRL::Cd::ChangeDir(dir) >= 0;
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
 | Description: The filename of the picture currently uploaded to NBG0, and the only
 |   way to record a new one. title_bg_show compares against it to decide whether a
 |   request is already on screen and can skip the decode and the upload entirely.
 |
 |   Every path that blits NBG0 has to record it here, the one-off logo path
 |   included -- a note about VRAM that VRAM does not agree with is invisible until
 |   a later request for that same name short-circuits onto the wrong picture.
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
 |   The other LWRAM residents are why the slot count is what it is, and none of them
 |   is permanent. SPLASH.PCM (~453 KB) is held across the splash and the title
 |   screen and freed before the mode menu. The local game's typeahead trie is
 |   several hundred KB and lives for the
 |   session, released by soft_reset_to_title. The save/restore scratch is ~47 KB
 |   while a save is written. The online trie is larger than any of them, but it is
 |   only built once the player actually dials (see ensure_online_typeahead), so it
 |   is not in the budget for an ordinary session at all -- which is where the slots
 |   below came from.
 |
 |   Only one reserve is needed, because the cache is now filled at a moment when
 |   nothing else is still owed. TGA_CACHE_FLOOR is therefore just the save scratch
 |   plus a margin, and the free-space check spends everything else on pictures.
 |
 |   Nothing pre-fills this at game start any more: a picture the cache does not
 |   hold is read at the bottom of a mood fade, where the volume is at zero and the
 |   screen has gone with it, so the miss costs frames nobody can perceive. The
 |   splash's own fill is capped separately -- the jingle is still resident there and
 |   the trie does not exist yet, so it takes a couple of slots at most and its real
 |   job is the title's own house.
 | Author: suinevere
 ----------------------*/
#define LWRAM_TOTAL        (1024u * 1024u)
#define TGA_CACHE_SLOTS    9
#define TGA_PLANE_MAX      (320u * 240u + 2048u)
#define TGA_PAL_BYTES      (256u * sizeof(SRL::Types::HighColor))
#define TGA_SLOT_BYTES     (TGA_PLANE_MAX + TGA_PAL_BYTES)
#define TGA_CACHE_FLOOR    (96u * 1024u)   /* save scratch + margin */

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
 |   Enters the picture's mood folder through cd_enter_mood before opening it, and
 |   calls cd_enter_root() on every exit that follows -- success, every validation
 |   failure, every allocation or read failure. The working directory is
 |   process-wide, and a return that skipped it would leave the CD standing in
 |   /TGA or /TGA/HORROR for whatever runs next (see cd_enter_mood).
 |
 |   Two modes, told apart by out->Cap on entry. Zero means "allocate": the plane
 |   is taken at exactly the size this picture needs and the caller owns it. Non-
 |   zero means out->Pixels and out->Colors are already-owned buffers of that
 |   capacity and this decodes into them, allocating nothing -- which is how a
 |   cache slot is refilled without ever returning its memory to the allocator.
 |   A picture too big for the capacity is refused rather than truncated, and the
 |   slot keeps whatever it held.
 | Author: suinevere
 | Dependencies: SRL, cd_enter_mood, cd_enter_root
 | Globals: N/A
 | Params: file -- the folder-relative path from display_image_file/
 |   display_scene_image or title_art_file, e.g. "ZORK1/07.TGA" or
 |   "TITLE/01.TGA", or a bare /TGA filename like "SUINE.TGA"; low -- true to
 |   allocate in Low Work RAM (ignored when reusing);
 |   limit -- most bytes the pixel plane may take; out -- filled on success, Cap
 |   read on entry and preserved
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

    const char *leaf = file;
    if (!cd_enter_mood(file, &leaf)) { cd_enter_root(); return false; }

    SRL::Cd::File f(leaf);
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
    if (reuse_pix != nullptr) {
        /* Refusing rather than truncating: a short plane would render as a band of
           the previous picture across the bottom of this one. */
        if (span > reuse_cap) { cd_enter_root(); return false; }
    } else {
        if (span + palsize > limit)                     { cd_enter_root(); return false; }
        if (tga_free_space(low) < span + palsize + 4096) { cd_enter_root(); return false; }
    }

    SRL::Types::HighColor *colors = reuse_col;
    if (colors == nullptr) colors = (SRL::Types::HighColor *) tga_alloc(palsize, low);
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

    uint8_t *pix = reuse_pix;
    if (pix == nullptr) pix = (uint8_t *) tga_alloc(span, low);
    // LoadBytes wants a long-aligned destination; refuse rather than corrupt.
    if (pix == nullptr || ((unsigned int) pix & 3) != 0) {
        if (reuse_pix == nullptr) { tga_free(pix, low); tga_free(colors, low); }
        cd_enter_root();
        return false;
    }
    if (f.LoadBytes((size_t) (pixoff / ss), (int32_t) span, pix) <= 0) {
        if (reuse_pix == nullptr) { tga_free(pix, low); tga_free(colors, low); }
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
    // A reused slot already paid for its buffers when they were taken and does not
    // pay again per picture, so its cost stays whatever the slot cost.
    if (reuse_pix == nullptr) out->Bytes = span + palsize;
    out->W      = (uint16_t) w;
    out->H      = (uint16_t) h;
    int k = 0;
    for (; file[k] && k < (int) sizeof(out->Name) - 1; k++) out->Name[k] = file[k];
    out->Name[k] = '\0';
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
 | title_art_file
 | Description: The disc path for the shared title picture at 1-based `index`
 |   ("TITLE/01.TGA"..), or NULL when index is out of 1..TITLE_ART_N -- which
 |   includes every index while TITLE_ART_N is 0, since no source images exist
 |   yet. TITLE is a flat, gapless folder addressed by literal filename rather
 |   than routed through display.c's scene machinery: the title screen has no
 |   room, no scene and no game to suit.
 | Author: suinevere
 | Dependencies: scene/title_art.inc (TITLE_ART_N)
 | Globals: N/A
 | Params: index -- 1..TITLE_ART_N
 | Returns: the disc path, or NULL
 ----------------------*/
const char *title_art_file(int index) {
    static char buf[16];
    if (index < 1 || index > TITLE_ART_N) return nullptr;
    buf[0] = 'T'; buf[1] = 'I'; buf[2] = 'T'; buf[3] = 'L'; buf[4] = 'E'; buf[5] = '/';
    buf[6] = (char) ('0' + index / 10);
    buf[7] = (char) ('0' + index % 10);
    buf[8] = '.'; buf[9] = 'T'; buf[10] = 'G'; buf[11] = 'A';
    buf[12] = '\0';
    return buf;
}

/*----------------------
 | title_art_random
 | Description: One of the shared title pictures, chosen by `seed` reduced
 |   modulo TITLE_ART_N -- the boot's own pick, separate from the preload
 |   walk below. NULL when TITLE_ART_N is 0, which the caller must treat as
 |   "no wallpaper" rather than read a nonexistent file. The #if guards the
 |   modulo itself rather than relying on a runtime check: TITLE_ART_N is
 |   compile-time 0 until real title art ships, and `seed % 0` is a
 |   -Wdiv-by-zero on every build even though the runtime check below made it
 |   statically unreachable.
 | Author: suinevere
 | Dependencies: title_art_file
 | Globals: N/A
 | Params: seed -- any value; reduced modulo TITLE_ART_N
 | Returns: the disc path, or NULL
 ----------------------*/
const char *title_art_random(unsigned int seed) {
#if TITLE_ART_N > 0
    const unsigned int n = (unsigned int) TITLE_ART_N;
    return title_art_file((int) (seed % n) + 1);
#else
    (void) seed;
    return nullptr;
#endif
}

/*----------------------
 | title_preload_art
 | Description: Fills cache slots with the shared TITLE/ pictures, stopping the
 |   moment the cache will not grow any further.
 |
 |   Stopping rather than continuing is the whole point. tga_cache_admit falls
 |   back to evicting once the cache is full, so walking every picture into a
 |   smaller cache would read pictures off the disc only to throw the earliest
 |   ones away before the splash even ends. Watching g_cache_count is what
 |   tells the two apart: unchanged means it evicted.
 |
 |   Runs at BOOT, before any game is selected -- splash.cxx and title_and_seed
 |   below both call it, with the jingle resident and before any game's
 |   typeahead trie exists, so the free space it can see is not the free space
 |   that will be left; filling the cache for actual gameplay is
 |   left to the rooms themselves, which read what they need when they need it.
 |   A no-op at TITLE_ART_N 0.
 | Author: suinevere
 | Dependencies: title_art_file, SRL
 | Globals: g_cache_count
 | Params: max_slots -- stop after taking this many fresh slots; 0 or less for as
 |   many as Low Work RAM allows
 | Returns: N/A
 ----------------------*/
void title_preload_art(int max_slots) {
    int taken = 0;
    for (int i = 1; i <= TITLE_ART_N; i++) {
        if (g_cache_count >= TGA_CACHE_SLOTS) break;
        if (tga_free_space(true) < TGA_SLOT_BYTES + TGA_CACHE_FLOOR) break;
        if (max_slots > 0 && taken >= max_slots) break;
        const char *file = title_art_file(i);
        if (file == nullptr) continue;
        if (tga_cache_find(file) != nullptr) continue;
        const int before = g_cache_count;
        if (tga_cache_admit(file) == nullptr) break;
        if (g_cache_count == before) break;
        taken++;
    }
    bitmap_read_end();
}

/*----------------------
 | title_bg_cache_release
 | Description: Hands every cache slot's Low Work RAM back and empties the cache.
 |
 |   For the soft-reset return, which is the one moment the zone has to be cleared
 |   rather than reused. A session ends with the cache full, ~580 KB, and the jingle
 |   splash_show is about to load wants ~453 KB of the same megabyte -- so a
 |   return that did not do this would find no room and drop onto a silent title.
 |   The pictures are cheap to lose: the title reads its own again on the next frame,
 |   and the next game warms the cache from scratch anyway.
 |
 |   Leaves the NBG0 upload alone. title_bg_show tracks what is in VRAM separately,
 |   so the picture on screen stays up and a re-show of that same file still
 |   short-circuits without a read.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cache, g_cache_count
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_bg_cache_release(void) {
    for (int i = 0; i < g_cache_count; i++) {
        tga_image_free(&g_cache[i]);
        g_cache[i].Name[0] = '\0';
        g_cache[i].LastUse = 0;
    }
    g_cache_count = 0;
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
    bool same = true;
    for (int i = 0; i < (int) sizeof(g_nbg0_loaded); i++) {
        if (g_nbg0_loaded[i] != file[i]) { same = false; break; }
        if (g_nbg0_loaded[i] == '\0') break;
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
        text_clear_line(20);
        text_clear_line(21);
        nbg0_note_loaded(file);
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
    bool decoded = tga_decode(file, false, 0xFFFFFFFFu, &oneoff);
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
    title_preload_art(0);
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
