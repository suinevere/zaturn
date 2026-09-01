/*----------------------
 | item_art.cxx
 | Description: See item_art.h.
 | Author: suinevere
 | Dependencies: SRL, oitem.h, item_art.h, title.h, scene/items.h, music.h
 | Globals: g_release, g_serial, g_have_game, g_archive, g_archive_len,
 |   g_pixels, g_clut, g_cur_picture, g_layer_up
 ----------------------*/
#include <srl.hpp>
#include "video/oitem.h"
#include "video/item_art.h"
#include "video/title.h"
#include "scene/items.h"
#include "sound/music.h"

/*----------------------
 | ITEM_DIR / ITEM_FILE
 | Description: Where OITEM.CZ lives on the disc: the same /BG directory the
 |   area archives share, one fixed name rather than an area-indexed stem.
 | Author: suinevere
 ----------------------*/
#define ITEM_DIR  "BG"
#define ITEM_FILE "OITEM.CZ"

/*----------------------
 | g_release / g_serial / g_have_game
 | Description: The running story's identity, and whether it carries an
 |   authored item-picture table at all. Held here so the overlay renderer
 |   does not have to.
 | Author: suinevere
 ----------------------*/
static unsigned int g_release = 0;
static char         g_serial[7] = { 0 };
static bool         g_have_game = false;

/*----------------------
 | g_archive / g_archive_len
 | Description: OITEM.CZ's bytes and their length, resident only between
 |   item_art_open and item_art_close.
 | Author: suinevere
 ----------------------*/
static unsigned char *g_archive = nullptr;
static unsigned long  g_archive_len = 0;

/*----------------------
 | g_pixels / g_clut
 | Description: The decode target and the palette the current picture
 |   produced. One target, reused: only one picture is ever on the pane.
 | Author: suinevere
 ----------------------*/
static unsigned char  *g_pixels = nullptr;
static unsigned short  g_clut[256];

/*----------------------
 | ART_HIDDEN / ART_BLACK
 | Description: The two states of a pane with no picture on it, held in
 |   g_cur_picture where a picture index would be. HIDDEN is the window
 |   transparent -- the overlay is not up and the layer belongs to nobody --
 |   and BLACK is the window present and empty, which is what a carried object
 |   with no bound picture gets. Negative so no picture index can collide with
 |   them.
 | Author: suinevere
 ----------------------*/
#define ART_HIDDEN (-1)
#define ART_BLACK  (-2)

/*----------------------
 | g_cur_picture
 | Description: The 0-based picture index last put on the pane by
 |   item_art_show, or ART_HIDDEN / ART_BLACK for the two empty states.
 |   Compared before the archive is even opened, so walking a cursor across a
 |   run of the same item costs one table lookup and no disc access.
 | Author: suinevere
 ----------------------*/
static int g_cur_picture = ART_HIDDEN;

/*----------------------
 | g_layer_up
 | Description: Whether NBG1's VRAM bank and bitmap registers have been
 |   claimed yet. Set once by layer_ensure and never cleared -- item_art_close
 |   blanks the pane's pixels but leaves the layer's VRAM claim standing,
 |   since nothing else on this build ever wants that bank back.
 | Author: suinevere
 ----------------------*/
static bool g_layer_up = false;

/*----------------------
 | ItemBitmap
 | Description: An IBitmap wrapping a borrowed pixel plane and an owned
 |   palette, used once by layer_ensure to claim NBG1's VRAM bank and set its
 |   bitmap registers. title.cxx's RawBitmap does the identical job for NBG0
 |   but is file-scope there, so this is its own copy rather than a shared
 |   one. GetInfo is const because SRL::Bitmap::IBitmap declares it that way;
 |   an override missing that qualifier does not override it and the class
 |   fails to compile as still-abstract.
 | Author: suinevere
 | Dependencies: SRL
 ----------------------*/
struct ItemBitmap final : SRL::Bitmap::IBitmap {
    uint8_t              *Pixels;
    SRL::Bitmap::Palette *Colors;
    uint16_t              W, H;
    ItemBitmap() : Pixels(nullptr), Colors(nullptr), W(0), H(0) {}
    ~ItemBitmap() override { if (Colors != nullptr) delete Colors; }
    uint8_t *GetData() override { return Pixels; }
    SRL::Bitmap::BitmapInfo GetInfo() const override {
        return SRL::Bitmap::BitmapInfo(W, H, Colors);
    }
};

/*----------------------
 | art_dir_restore
 | Description: Puts the CD back where it was before this module stepped into
 |   /BG, the same two-step room_art.cxx uses and for the same reason:
 |   cd_restore_z3 alone is a no-op before the catalogue scan has captured
 |   /Z3, so climbing to root first makes the restore correct on every path
 |   that can reach item_art_open.
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
 | layer_ensure
 | Description: Claims NBG1's VRAM bank and bitmap registers the first time a
 |   picture is shown. Loads a blank 64x80 image and a placeholder palette
 |   through LoadBitmap once -- SRL sizes the container off Width/Height
 |   thresholds rather than the exact request, so even this small a picture
 |   lands the allocator on the same 512x256 8bpp, one-bank container every
 |   later picture writes into directly. Doing it this way once is far cheaper
 |   than routing every picture through LoadBitmap's allocate-and-blank path.
 |   Idempotent: a second call that already succeeded is a no-op that returns
 |   true without touching SRL again.
 |
 |   LoadBitmap has two silent early returns of its own -- an allocation
 |   failure and a CRAM palette-bank exhaustion -- neither of which throws or
 |   sets an error this module can read back directly, so g_layer_up is set
 |   only after both halves check out: NBG1::CellAddress against the same
 |   less-than-VDP2_VRAM_A0 test SRL's own allocator uses internally (catching
 |   both a null CellAddress and its unclaimed sentinel value, VDP2_VRAM_A0 -
 |   1, which is what the allocation failure leaves behind), and
 |   NBG1::TilePalette.GetData() being non-null (the palette-bank exhaustion
 |   leaves it at its untouched nullptr, since the failing return fires before
 |   the assignment that would give it a real CRAM address). Any caller that
 |   skips these checks and writes through CellAddress or TilePalette anyway
 |   lands one byte below VRAM bank A0, into NBG0's wallpaper, or DMAs through
 |   a null CRAM handle.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_layer_up
 | Params: N/A
 | Returns: true once NBG1 holds a usable VRAM claim, false if the bring-up
 |   did not take and no VRAM/CRAM write is safe yet
 ----------------------*/
static bool layer_ensure(void) {
    static uint8_t blank[OITEM_PIC_BYTES];
    SRL::Types::HighColor *colors;
    ItemBitmap bmp;
    int i;

    if (g_layer_up) return true;

    colors = new SRL::Types::HighColor[256];
    if (colors == nullptr) return false;
    for (i = 0; i < 256; i++) colors[i] = SRL::Types::HighColor((unsigned short) 0);

    bmp.Pixels = blank;
    bmp.W      = (uint16_t) OITEM_WIDTH;
    bmp.H      = (uint16_t) OITEM_HEIGHT;
    bmp.Colors = new SRL::Bitmap::Palette(colors, 256);
    if (bmp.Colors == nullptr) { delete[] colors; return false; }

    SRL::VDP2::NBG1::LoadBitmap(&bmp);

    if ((uint32_t) SRL::VDP2::NBG1::CellAddress < VDP2_VRAM_A0) return false;
    if (SRL::VDP2::NBG1::TilePalette.GetData() == nullptr) return false;

    slPriorityNbg1(3);
    SRL::VDP2::NBG1::ScrollEnable();
    g_layer_up = true;
    return true;
}

/*----------------------
 | ART_BLACK_INDEX
 | Description: The palette entry item_art_blank paints the window with. Any
 |   non-zero index would do -- index 0 is the one VDP2 reads as transparent --
 |   and this one is only ever on the layer with the all-black palette under
 |   it, never alongside a picture, so it can never collide with a colour a
 |   picture wanted for itself.
 | Author: suinevere
 ----------------------*/
#define ART_BLACK_INDEX 1

/*----------------------
 | put_palette
 | Description: Reloads NBG1's palette from a freshly decoded CLUT, converting
 |   each Saturn RGB555 word to HighColor the way title_bg_show_raw does for
 |   NBG0.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: clut -- 256 Saturn CRAM words
 | Returns: N/A
 ----------------------*/
static void put_palette(const unsigned short *clut) {
    SRL::Types::HighColor colors[256];
    int i;
    for (i = 0; i < 256; i++) colors[i] = SRL::Types::HighColor(clut[i]);
    SRL::VDP2::NBG1::TilePalette.Load(colors, 256);
}

/*----------------------
 | put_black_palette
 | Description: Loads a palette that is opaque black in every entry, so the
 |   empty window is black whatever index is under it. Only ever loaded when no
 |   picture is on the layer to want the palette for itself.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void put_black_palette(void) {
    SRL::Types::HighColor colors[256];
    int i;
    for (i = 0; i < 256; i++) colors[i] = SRL::Types::HighColor((unsigned short) 0x8000u);
    SRL::VDP2::NBG1::TilePalette.Load(colors, 256);
}

/*----------------------
 | fill_window
 | Description: Writes one byte across the picture's own 64x80 window in NBG1's
 |   container, one row at a time with a stride of 512, the container's real
 |   width rather than the picture's. 0 is transparent and takes the window
 |   away; ART_BLACK_INDEX under the black palette is the empty frame. The
 |   window is exactly what put_pixels writes, so the two are interchangeable
 |   and nothing outside it is ever touched -- the rest of the container stays
 |   at index 0, which VDP2 reads as transparent.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: value -- the palette index to fill with
 | Returns: N/A
 ----------------------*/
static void fill_window(unsigned char value) {
    volatile uint8_t *base = (volatile uint8_t *) SRL::VDP2::NBG1::CellAddress;
    int x, y;
    base += (ITEM_ART_Y * 512) + ITEM_ART_X;
    for (y = 0; y < OITEM_HEIGHT; y++) {
        for (x = 0; x < OITEM_WIDTH; x++) base[x] = value;
        base += 512;
    }
}

/*----------------------
 | put_pixels
 | Description: Writes one decoded picture straight into NBG1's container at
 |   the picture's own offset, one 64-byte row at a time with a stride of 512.
 |   It covers the whole window, so nothing has to be cleared ahead of it
 |   whatever the window held before.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: px -- OITEM_WIDTH*OITEM_HEIGHT 8bpp bytes
 | Returns: N/A
 ----------------------*/
static void put_pixels(const unsigned char *px) {
    volatile uint8_t *base = (volatile uint8_t *) SRL::VDP2::NBG1::CellAddress;
    int x, y;
    base += (ITEM_ART_Y * 512) + ITEM_ART_X;
    for (y = 0; y < OITEM_HEIGHT; y++) {
        for (x = 0; x < OITEM_WIDTH; x++) base[x] = px[y * OITEM_WIDTH + x];
        base += 512;
    }
}

/*----------------------
 | item_art_set_game
 | Description: See item_art.h. Closes first, so a game change frees the
 |   outgoing story's archive before the incoming one asks for room, and opens
 |   after, so the read is the last thing it does rather than something the
 |   first inventory of the session pays for. Both are no-ops for a story with
 |   no pictures.
 | Author: suinevere
 | Dependencies: scene/items.h, item_art_open, item_art_close
 | Globals: g_release, g_serial, g_have_game
 | Params: release, serial -- the story identity
 | Returns: N/A
 ----------------------*/
void item_art_set_game(unsigned int release, const char *serial) {
    int i;
    item_art_close();
    g_release = release;
    for (i = 0; i < 6; i++) g_serial[i] = serial ? serial[i] : '\0';
    g_serial[6] = '\0';
    g_have_game = (serial != nullptr) && (items_available(release, g_serial) != 0);
    item_art_open();
}

/*----------------------
 | item_art_available
 | Description: See item_art.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_have_game
 | Params: N/A
 | Returns: 1 when the story has a table
 ----------------------*/
int item_art_available(void) { return g_have_game ? 1 : 0; }

/*----------------------
 | item_art_open
 | Description: See item_art.h. Refuses before touching the disc when the
 |   running story has no item-picture table -- OITEM.CZ is one fixed-path
 |   file staged for every game, so the read would otherwise succeed for a
 |   story that can never decode a picture from it, costing a blocking CD
 |   seek and 40 KB of Low Work RAM every time the overlay opens. Past that
 |   gate, mirrors room_art.cxx's load_area: enter root, change into /BG,
 |   check Exists(), check the Low Work RAM headroom against the archive plus
 |   one decode target plus 4 KB, allocate, check the result is long-aligned
 |   (LoadBytes requires it), read, and restore the directory on every exit
 |   path including the successful one.
 |   The whole disc detour runs with the music held by music_pause, which stops
 |   the drive on a known frame and picks the same track up there afterwards. A
 |   data seek silences CD-DA whether or not anybody asked it to, and a track
 |   that was never held reads to the engine as one that ended, so the next tick
 |   starts it again from the top -- opening the inventory restarted the music
 |   instead of skipping a moment of it. A hold already in force (an open menu
 |   ducking the track) is left exactly as found rather than lifted here.
 | Author: suinevere
 | Dependencies: SRL, title.h, music.h
 | Globals: g_have_game, g_archive, g_archive_len
 | Params: N/A
 | Returns: 1 when the archive is resident, 0 on any refusal
 ----------------------*/
int item_art_open(void) {
    if (!g_have_game) return 0;
    if (g_archive != nullptr) return 1;

    int held = music_is_paused();
    if (!held) music_pause();

    int ok = 0;
    cd_enter_root();
    if (SRL::Cd::ChangeDir(ITEM_DIR) >= 0) {
        SRL::Cd::File f(ITEM_FILE);
        if (f.Exists()) {
            const uint32_t bytes = (uint32_t) f.Size.Bytes;
            if (bytes != 0 && SRL::Memory::LowWorkRam::GetFreeSpace()
                              >= bytes + OITEM_PIC_BYTES + 4096) {
                g_archive = (unsigned char *) SRL::Memory::LowWorkRam::Malloc(bytes);
                if (g_archive != nullptr && ((unsigned int) g_archive & 3) == 0
                    && f.LoadBytes(0, (int32_t) bytes, g_archive) > 0) {
                    g_archive_len = bytes;
                    ok = 1;
                } else if (g_archive != nullptr) {
                    SRL::Memory::LowWorkRam::Free(g_archive);
                    g_archive = nullptr;
                }
            }
        }
    }
    art_dir_restore();
    if (!held) music_resume();
    return ok;
}

/*----------------------
 | item_art_close
 | Description: See item_art.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_archive, g_archive_len, g_pixels
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void item_art_close(void) {
    item_art_hide();
    if (g_archive != nullptr) SRL::Memory::LowWorkRam::Free(g_archive);
    if (g_pixels != nullptr)  SRL::Memory::LowWorkRam::Free(g_pixels);
    g_archive = nullptr;
    g_pixels = nullptr;
    g_archive_len = 0;
}

/*----------------------
 | item_art_show
 | Description: See item_art.h. Also refuses, holding whatever the pane
 |   already shows, when layer_ensure cannot confirm NBG1 actually claimed a
 |   VRAM bank -- SRL's LoadBitmap can fail its allocation or its palette-bank
 |   lookup without telling this module directly, and writing through
 |   NBG1::CellAddress on that failure would land one byte below VRAM bank A0,
 |   into NBG0's wallpaper, rather than merely leaving the pane blank.
 | Author: suinevere
 | Dependencies: SRL, oitem.h, scene/items.h
 | Globals: g_have_game, g_release, g_serial, g_archive, g_archive_len,
 |   g_pixels, g_clut, g_cur_picture
 | Params: obj -- the carried object's number
 | Returns: 1 when a picture is on the pane, 0 when it was blanked or held
 ----------------------*/
int item_art_show(unsigned int obj) {
    int pic;

    if (!g_have_game) return 0;

    pic = items_picture_of(g_release, g_serial, obj);
    if (pic < 0) { item_art_blank(); return 0; }
    if (pic == g_cur_picture) return 1;

    if (g_archive == nullptr && !item_art_open()) return 0;

    if (g_pixels == nullptr) {
        g_pixels = (unsigned char *) SRL::Memory::LowWorkRam::Malloc(OITEM_PIC_BYTES);
        if (g_pixels == nullptr) return 0;
    }

    if (!oitem_decode(g_archive, g_archive_len, pic, g_pixels, g_clut)) return 0;

    if (!layer_ensure()) return 0;

    put_palette(g_clut);
    put_pixels(g_pixels);
    g_cur_picture = pic;
    return 1;
}

/*----------------------
 | item_art_blank
 | Description: See item_art.h. Brings the layer up if it is not up yet, since
 |   an empty frame is something to draw rather than nothing: the tall overlay
 |   has already given the module over and its marble showing through the frame
 |   would read as a picture that failed rather than an item that has none. A
 |   bring-up that will not take leaves the window alone, exactly as
 |   item_art_show does.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_cur_picture, g_layer_up
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void item_art_blank(void) {
    if (g_cur_picture == ART_BLACK) return;
    if (!layer_ensure()) return;
    put_black_palette();
    fill_window(ART_BLACK_INDEX);
    g_cur_picture = ART_BLACK;
}

/*----------------------
 | item_art_hide
 | Description: See item_art.h. Early-outs when g_cur_picture is already
 |   ART_HIDDEN: that value means the window is already transparent, whether
 |   because nothing has ever been shown (layer_ensure's bring-up bitmap is all
 |   zero) or because a previous hide already cleared it, so the 5 KB VDP2 write
 |   below would be redundant. Guarded here rather than at each call site
 |   so every caller, present and future, gets the same one-thing-on-this-layer
 |   contract at no repeated cost -- render_command_panel calls this every frame
 |   once the overlay has ever been shown.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cur_picture, g_layer_up
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void item_art_hide(void) {
    if (g_cur_picture == ART_HIDDEN) return;
    g_cur_picture = ART_HIDDEN;
    if (g_layer_up) fill_window(0);
}
