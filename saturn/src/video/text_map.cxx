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
 | flush_hook
 | Description: The OnAfterSync subscriber, separate from text_flush so what is
 |   registered is a C++ function pointer of the event's own type.
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

extern "C" void text_flush(void)
{
    if (g_dirty_top > g_dirty_bottom) return;

    const text_long *src = (const text_long *)&g_shadow[g_dirty_top][0];
    volatile uint32_t *dst = TEXT_VRAM_MAP + (g_dirty_top * TEXT_COLS >> 1);
    int longs = ((g_dirty_bottom - g_dirty_top + 1) * TEXT_COLS) >> 1;

    for (int i = 0; i < longs; i++) *dst++ = *src++;

    g_dirty_top    = TEXT_ROWS;
    g_dirty_bottom = -1;
}

extern "C" void text_map_init(void)
{
    if (g_registered) return;

    for (int y = 0; y < TEXT_ROWS; y++)
        for (int x = 0; x < TEXT_COLS; x++) g_shadow[y][x] = TEXT_BLANK;

    g_dirty_top    = 0;
    g_dirty_bottom = TEXT_ROWS - 1;
    text_flush();

    SRL::Core::OnAfterSync += &flush_hook;
    g_registered = true;
}
