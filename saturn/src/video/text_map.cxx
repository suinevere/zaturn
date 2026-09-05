/*----------------------
 | text_map.cxx
 | Description: Implements the work-RAM text shadow and its vblank flush. See
 |   text_map.h for why the program does not draw into the tilemap directly.
 | Author: suinevere
 | Dependencies: text_map.h, srl.hpp
 ----------------------*/

#include <srl.hpp>
#include "text_map.h"

/*----------------------
 | TEXT_VRAM_MAP
 | Description: The VDP2 tilemap SRL::ASCII prints into, which this shadow feeds.
 |   Hard-coded because SRL keeps the same address in a private member; it has to
 |   agree with ASCII::tileMap in srl_ascii.hpp.
 | Author: suinevere
 ----------------------*/
#define TEXT_VRAM_MAP ((volatile uint32_t *)(VDP2_VRAM_B1 + 0x1E000))

/*----------------------
 | TEXT_FONT_BANK / TEXT_COLOR_BANK
 | Description: The pattern-name encoding SRL::ASCII uses: a cell holds the
 |   character code plus the font's tile offset, or'd with the palette bank. The
 |   values are the ones VDP2::Initialize leaves set (SetFont(0) -> 128 * (5 - 0),
 |   SetPalette(0) -> 0 << 12) and nothing in this program changes them; the
 |   Options page notes at options.cxx:42 why the palette is never re-pointed.
 | Author: suinevere
 ----------------------*/
#define TEXT_FONT_BANK  640
#define TEXT_COLOR_BANK 0

/*----------------------
 | TEXT_DIM_BANK
 | Description: TEXT_DIM_PAL in the pattern name's palette field (bits 15..12),
 |   the one bit-or that moves a cell onto the dim ink.
 | Author: suinevere
 ----------------------*/
#define TEXT_DIM_BANK (TEXT_DIM_PAL << 12)

/*----------------------
 | TEXT_BLANK
 | Description: The pattern-name word for a space, which is what a cleared cell
 |   holds.
 | Author: suinevere
 ----------------------*/
#define TEXT_BLANK ((uint16_t)((uint16_t)' ' + TEXT_FONT_BANK) | TEXT_COLOR_BANK)

/*----------------------
 | g_shadow
 | Description: The composed frame, one pattern-name word per cell, in the same
 |   layout as the hardware map so a flush is a straight copy. Aligned to 4 so the
 |   flush can move it a long at a time.
 | Author: suinevere
 ----------------------*/
static uint16_t g_shadow[TEXT_ROWS][TEXT_COLS] __attribute__((aligned(4)));

/*----------------------
 | text_long
 | Description: The word pair the flush moves at a time. may_alias because the
 |   shadow it reads is an array of uint16_t, and without it the compiler is
 |   entitled to assume the two types never refer to the same storage and to
 |   hoist the copy above the stores that filled it.
 | Author: suinevere
 ----------------------*/
typedef uint32_t __attribute__((may_alias)) text_long;

/*----------------------
 | g_dirty_top / g_dirty_bottom
 | Description: The inclusive row range changed since the last flush, held as a
 |   span rather than a per-row flag because the rows are contiguous in both the
 |   shadow and VRAM and so copy as one block. Empty is top > bottom.
 | Author: suinevere
 ----------------------*/
static int g_dirty_top    = TEXT_ROWS;
static int g_dirty_bottom = -1;

/*----------------------
 | g_registered
 | Description: Whether text_map_init has already subscribed the flush, so a
 |   second call does not stack another callback on OnAfterSync.
 | Author: suinevere
 ----------------------*/
static bool g_registered = false;

/*----------------------
 | mark_dirty
 | Description: Widens the dirty span to include one row.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_dirty_top, g_dirty_bottom
 | Params: y -- cell row
 | Returns: N/A
 ----------------------*/
static inline void mark_dirty(int y)
{
    if (y < g_dirty_top)    g_dirty_top = y;
    if (y > g_dirty_bottom) g_dirty_bottom = y;
}

/*----------------------
 | TEXT_FONT_TILES / font_tile
 | Description: Where font 0's tiles live, and the address of one character
 |   code's tile within it. install_block_glyph (console_view.cxx) writes the
 |   same region for the block cursor, and install_backslash_glyph below patches
 |   one character of it.
 | Author: suinevere
 ----------------------*/
#define TEXT_FONT_TILES (VDP2_VRAM_B1 + 0x18000)

static volatile unsigned char *font_tile(int code)
{
    return (volatile unsigned char *) (TEXT_FONT_TILES + (code + TEXT_FONT_BANK) * 0x20);
}

/*----------------------
 | flush_hook
 | Description: The OnAfterSync subscriber, separate from text_flush so what is
 |   registered is a C++ function pointer of the event's own type. It used to
 |   build a reverse-video tile per highlighted character first, because a
 |   highlighted cell's pattern name must not reach VRAM ahead of the tile data
 |   it names. Selection is a second ink now, not a second tile -- palette
 |   TEXT_DIM_PAL for what is not selected, entry 1 for what is -- so a
 |   selection costs nothing but the pattern name and there is nothing left to
 |   drain ahead of the map copy.
 | Author: suinevere
 | Dependencies: text_flush
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void flush_hook(void)
{
    text_flush();
}

/*----------------------
 | g_on_flush
 | Description: The one-shot callback text_on_flush registered, or NULL.
 | Author: suinevere
 ----------------------*/
static void (*g_on_flush)(void) = nullptr;

/*----------------------
 | predraw_hook
 | Description: The OnBeforeSync subscriber, which fires the one-shot callback
 |   when the coming vblank has rows to carry. Before the sync and not after it:
 |   SRL invokes OnAfterSync once slSynch has already committed SGL's register
 |   shadows for this field, so a hardware write made there lands a frame later
 |   than the rows it belongs with, and the caller's whole reason for deferring
 |   is to have the two change together.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_on_flush, g_dirty_top, g_dirty_bottom
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void predraw_hook(void)
{
    if (g_dirty_top > g_dirty_bottom || g_on_flush == nullptr) return;

    void (*fn)(void) = g_on_flush;
    g_on_flush = nullptr;
    fn();
}

extern "C" void text_print_str(int x, int y, const char *s)
{
    if (y < 0 || y >= TEXT_ROWS || x >= TEXT_COLS || s == nullptr) return;
    if (x < 0) x = 0;

    uint16_t *row = g_shadow[y];

    for (int c = x; c < TEXT_COLS && *s != '\0'; c++)
    {
        uint16_t word = (uint16_t)((uint16_t)(uint8_t)(*s++) + TEXT_FONT_BANK) | TEXT_COLOR_BANK;

        if (row[c] != word)
        {
            row[c] = word;
            mark_dirty(y);
        }
    }
}

extern "C" void text_print_dim(int x, int y, const char *s)
{
    if (y < 0 || y >= TEXT_ROWS || x >= TEXT_COLS || s == nullptr) return;
    if (x < 0) x = 0;

    uint16_t *row = g_shadow[y];

    for (int c = x; c < TEXT_COLS && *s != '\0'; c++)
    {
        uint16_t word = (uint16_t)((uint16_t)(uint8_t)(*s++) + TEXT_FONT_BANK) | TEXT_DIM_BANK;

        if (row[c] != word)
        {
            row[c] = word;
            mark_dirty(y);
        }
    }
}

extern "C" void text_print_ink(int x, int y, const char *s, int slot)
{
    if (y < 0 || y >= TEXT_ROWS || x >= TEXT_COLS || s == nullptr) return;
    if (x < 0) x = 0;

    uint16_t bank = (slot >= 0 && slot < TEXT_PARTY_PALS)
                    ? (uint16_t)((TEXT_PARTY_PAL0 + slot) << 12)
                    : (uint16_t) TEXT_COLOR_BANK;
    uint16_t *row = g_shadow[y];

    for (int c = x; c < TEXT_COLS && *s != '\0'; c++)
    {
        uint16_t word = (uint16_t)((uint16_t)(uint8_t)(*s++) + TEXT_FONT_BANK) | bank;

        if (row[c] != word)
        {
            row[c] = word;
            mark_dirty(y);
        }
    }
}

extern "C" void text_set_party_ink(int slot, unsigned short rgb555)
{
    if (slot < 0 || slot >= TEXT_PARTY_PALS) return;
    volatile unsigned short *cram = (volatile unsigned short *) VDP2_COLRAM;
    cram[TEXT_PARTY_CRAM(slot)] = (unsigned short) (0x8000 | rgb555);
}

extern "C" void text_clear_line(int y)
{
    if (y < 0 || y >= TEXT_ROWS) return;

    uint16_t *row = g_shadow[y];

    for (int c = 0; c < TEXT_CLEAR_COLS; c++)
    {
        if (row[c] != TEXT_BLANK)
        {
            row[c] = TEXT_BLANK;
            mark_dirty(y);
        }
    }
}

/*----------------------
 | g_cur_x / g_cur_y / g_cur_word
 | Description: The one overlay cell the pointer cursor occupies, and the pattern
 |   word to paint there. It is deliberately not part of the shadow: the shadow is
 |   what the program composed, and the cursor is painted over the top of it at
 |   flush time so the character underneath comes back by itself the moment the
 |   cursor moves off. g_cur_x < 0 means no cursor.
 | Author: suinevere
 ----------------------*/
static int      g_cur_x = -1;
static int      g_cur_y = -1;
static uint16_t g_cur_word = 0;

extern "C" void text_cursor_set(int x, int y, char ch)
{
    if (x < 0 || y < 0 || x >= TEXT_COLS || y >= TEXT_ROWS) { text_cursor_off(); return; }
    uint16_t word = (uint16_t)((uint16_t)(uint8_t) ch + TEXT_FONT_BANK) | TEXT_COLOR_BANK;
    if (g_cur_x == x && g_cur_y == y && g_cur_word == word) return;
    if (g_cur_x >= 0) mark_dirty(g_cur_y);
    g_cur_x = x;
    g_cur_y = y;
    g_cur_word = word;
    mark_dirty(y);
}

extern "C" void text_cursor_off(void)
{
    if (g_cur_x < 0) return;
    mark_dirty(g_cur_y);
    g_cur_x = -1;
    g_cur_y = -1;
}

extern "C" void text_flush(void)
{
    if (g_dirty_top > g_dirty_bottom) return;

    const text_long *src = (const text_long *)&g_shadow[g_dirty_top][0];
    volatile uint32_t *dst = TEXT_VRAM_MAP + (g_dirty_top * TEXT_COLS >> 1);
    int longs = ((g_dirty_bottom - g_dirty_top + 1) * TEXT_COLS) >> 1;

    for (int i = 0; i < longs; i++) *dst++ = *src++;

    /* After the block copy, never inside it: the copy has just laid the composed
       frame down over wherever the cursor was, which is exactly how the character
       under it is restored, and painting the cursor first would only erase it. */
    if (g_cur_x >= 0 && g_cur_y >= g_dirty_top && g_cur_y <= g_dirty_bottom)
    {
        volatile uint16_t *cell = (volatile uint16_t *) TEXT_VRAM_MAP;
        cell[g_cur_y * TEXT_COLS + g_cur_x] = g_cur_word;
    }

    g_dirty_top    = TEXT_ROWS;
    g_dirty_bottom = -1;
}

extern "C" void text_on_flush(void (*fn)(void))
{
    g_on_flush = fn;
}

/*----------------------
 | install_backslash_glyph
 | Description: Replaces the tile at 0x5C with a horizontal mirror of the one at
 |   0x2F, so a backslash draws as a backslash. The font VDP2::Initialize uploads
 |   is the SGL one, whose 0x5C is the yen sign it inherits from the Japanese
 |   character set -- printing '\' rendered a currency symbol, which the compass
 |   rose's northwest and southeast spokes made impossible to miss. Mirroring the
 |   font's own forward slash rather than drawing a bitmap here is what keeps the
 |   two strokes the same weight, height and position in the cell.
 |
 |   4bpp, two pixels per byte, so a mirrored row is the four bytes reversed with
 |   the nibbles swapped inside each. Nothing in this program wants a yen sign,
 |   and the tile is patched once at init rather than per draw.
 | Author: suinevere
 | Dependencies: SRL (font already uploaded by Core::Initialize)
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void install_backslash_glyph(void)
{
    volatile unsigned char *fwd = font_tile('/');
    volatile unsigned char *back = font_tile('\\');
    for (int r = 0; r < 8; r++)
    {
        unsigned char src[4];
        for (int i = 0; i < 4; i++) src[i] = fwd[r * 4 + i];
        for (int i = 0; i < 4; i++)
        {
            unsigned char b = src[3 - i];
            back[r * 4 + i] = (unsigned char) ((b << 4) | (b >> 4));
        }
    }
}

extern "C" void text_map_init(void)
{
    if (g_registered) return;

    // Claim the dim ink's palette before anything else can be handed it. The
    // entry itself is written by text_set_color, which is the only code that
    // knows what colour the player picked; this only reserves the bank.
    SRL::CRAM::SetBankUsedState(TEXT_DIM_PAL,
                                SRL::CRAM::TextureColorMode::Paletted16, true);
    // And the four seat inks behind it, for the same reason and on the same
    // terms: claimed here, written by whichever screen knows whose colour is
    // whose (map_view.cxx is the only one so far).
    for (int i = 0; i < TEXT_PARTY_PALS; i++)
        SRL::CRAM::SetBankUsedState((uint16_t)(TEXT_PARTY_PAL0 + i),
                                    SRL::CRAM::TextureColorMode::Paletted16, true);

    install_backslash_glyph();

    for (int y = 0; y < TEXT_ROWS; y++)
        for (int x = 0; x < TEXT_COLS; x++) g_shadow[y][x] = TEXT_BLANK;

    g_dirty_top    = 0;
    g_dirty_bottom = TEXT_ROWS - 1;
    text_flush();

    SRL::Core::OnBeforeSync += &predraw_hook;
    SRL::Core::OnAfterSync += &flush_hook;
    g_registered = true;
}
