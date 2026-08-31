/*----------------------
 | dash_view.cxx
 | Description: Implements NBG2 bring-up for the input dashboard, the vblank
 |   copy of dash_map's shadow into the pattern name table, and dash_hold, which
 |   claims the panel on frames where a caller Synchronizes without a renderer.
 |   See dash_view.h.
 | Author: suinevere
 | Dependencies: dash_view.h, dash_map.h, dash_tiles.h, console_view.h,
 |   app_state.h, srl.hpp
 ----------------------*/
#include <srl.hpp>
#include "dash_view.h"
#include "dash_tiles.h"
#include "console_view.h"
#include "app_state.h"

/*----------------------
 | DASH_MAP_PITCH / DASH_PAL_NO
 | Description: The hardware map's row pitch in cells, and the CRAM palette
 |   number the dashboard writes into every pattern name. Palette 1 is entries
 |   16..31: 0..15 belong to the NBG3 font and 256+ to the wallpaper.
 | Author: suinevere
 ----------------------*/
#define DASH_MAP_PITCH 64
#define DASH_PAL_NO    1

/*----------------------
 | DASH_TINT_NUM / DASH_TINT_DEN
 | Description: How far a marble entry travels from neutral toward the
 |   background's hue -- half. Full tint turns a blue ground's marble pure blue
 |   and throws away the stone; none is what it did before. The dominant channel
 |   is untouched at any strength, so the ramp keeps its top end and only the
 |   other two channels come down, which is what reads as a tint rather than as
 |   a darkening.
 | Author: suinevere
 ----------------------*/
#define DASH_TINT_NUM 1
#define DASH_TINT_DEN 2

/*----------------------
 | g_ready / g_cell / g_map_vram / g_char_base
 | Description: Whether the layer came up, where its two allocations landed, and
 |   the character number its first tile sits at.
 | Author: suinevere
 ----------------------*/
static unsigned short g_tint_bg = 0;   // last background handed to dash_tint
static bool      g_ready = false;
static void     *g_cell = nullptr;
static uint16_t *g_map_vram = nullptr;
static uint16_t  g_char_base = 0;

/*----------------------
 | flush_hook
 | Description: The OnAfterSync subscriber. Closes the frame first, which takes
 |   the panel down when no renderer claimed it, then writes each dirty row's 40
 |   painted columns as 2-word pattern names and empties the span. Runs in vblank
 |   for the reason text_map.h's file header gives: VDP2 re-reads a cell's
 |   pattern name on every scanline of that cell's row, so a store landing
 |   mid-row shows one tile above the beam and another below it.
 | Author: suinevere
 | Dependencies: dash_map.h
 | Globals: g_map_vram, g_char_base
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void flush_hook(void)
{
    dash_frame_end();

    int top = dash_dirty_top();
    int bottom = dash_dirty_bottom();
    if (bottom < top || g_map_vram == nullptr) return;

    for (int y = top; y <= bottom; y++) {
        volatile uint16_t *dst = g_map_vram + (y * DASH_MAP_PITCH * 2);
        for (int x = 0; x < DASH_COLS; x++) {
            dst[x * 2]     = (uint16_t) DASH_PAL_NO;
            dst[x * 2 + 1] = (uint16_t) (g_char_base + dash_cell(x, y));
        }
    }
    dash_dirty_clear();
}

/*----------------------
 | write_palette
 | Description: Writes CRAM entries 16..31 from dash_tiles.c's ramp, each scaled
 |   DASH_TINT_NUM/DASH_TINT_DEN of the way toward g_tint_bg's hue. `peak` is the
 |   background's brightest channel, so scaling by (peak + channel) / 2*peak
 |   leaves the dominant channel exactly where it was and halves one that the
 |   background has none of. A background with no colour at all (peak 0, i.e.
 |   black) divides by nothing and is left alone. Straight into the SH-2's
 |   uncached CRAM mirror, as text_set_color does it, so no flush is needed and
 |   nothing has to wait for a DMA.
 | Author: suinevere
 | Dependencies: dash_tiles.h (dash_palette), SRL (VDP2_COLRAM)
 | Globals: g_tint_bg
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void write_palette(void)
{
    volatile unsigned short *cram = (volatile unsigned short *) VDP2_COLRAM;
    unsigned br = g_tint_bg & 31;
    unsigned bg = (g_tint_bg >> 5) & 31;
    unsigned bb = (g_tint_bg >> 10) & 31;
    unsigned peak = br > bg ? br : bg;
    if (bb > peak) peak = bb;

    for (int i = 0; i < 16; i++) {
        unsigned short src = dash_palette[i];
        if (src == 0) { cram[DASH_PAL_NO * 16 + i] = 0; continue; }   // transparent
        unsigned r = src & 31, g = (src >> 5) & 31, b = (src >> 10) & 31;
        if (peak != 0) {
            const unsigned d = DASH_TINT_DEN, n = DASH_TINT_NUM;
            r = (r * ((d - n) * peak + n * br)) / (d * peak);
            g = (g * ((d - n) * peak + n * bg)) / (d * peak);
            b = (b * ((d - n) * peak + n * bb)) / (d * peak);
        }
        cram[DASH_PAL_NO * 16 + i] =
            (unsigned short) (0x8000 | (b << 10) | (g << 5) | r);
    }
}

void dash_tint(unsigned short bg555)
{
    g_tint_bg = bg555;
    write_palette();
}

unsigned short dash_tint_current(void)
{
    return g_tint_bg;
}

bool dash_init(void)
{
    if (g_ready) return true;

    const int32_t cellBytes = (int32_t) (DT_N * 32);
    const int32_t mapBytes  = (int32_t) (DASH_MAP_PITCH * DASH_MAP_PITCH * 4);

    g_cell = SRL::VDP2::VRAM::Allocate((uint32_t) cellBytes, 32,
                                       SRL::VDP2::VramBank::B0, 1);
    if (g_cell == nullptr) return false;

    void *map = SRL::VDP2::VRAM::Allocate((uint32_t) mapBytes,
                                          (uint32_t) mapBytes,
                                          SRL::VDP2::VramBank::B0, 1);
    if (map == nullptr) return false;
    g_map_vram = (uint16_t *) map;

    SRL::VDP2::NBG2::SetCellAddress(g_cell, cellBytes);
    SRL::VDP2::NBG2::SetMapAddress(map, mapBytes);

    volatile uint8_t *cell = (volatile uint8_t *) g_cell;
    for (int t = 0; t < DT_N; t++)
        for (int b = 0; b < 32; b++) cell[t * 32 + b] = dash_tile_data[t][b];

    g_char_base = (uint16_t) ((((uint32_t) g_cell) - VDP2_VRAM_A0) >> 5);

    SRL::VDP2::NBG2::TilePalette =
        SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted16, DASH_PAL_NO);
    SRL::CRAM::SetBankUsedState(DASH_PAL_NO,
                                SRL::CRAM::TextureColorMode::Paletted16, true);
    SRL::VDP2::NBG2::TilePalette.Load(
        (SRL::Types::HighColor *) dash_palette, 16);
    // Over the Load, so the tint survives however this races display_apply:
    // the CD build brings the layer up before the colours are set and the
    // netbin sets them first, and neither should decide what the marble is.
    write_palette();

    SRL::Tilemap::TilemapInfo info(SRL::CRAM::TextureColorMode::Paletted16,
                                   PNB_2WORD, CHAR_SIZE_1x1, PL_SIZE_1x1,
                                   DASH_MAP_PITCH, DASH_MAP_PITCH, cellBytes);
    SRL::VDP2::NBG2::Init(info);

    dash_reset();
    for (int y = 0; y < DASH_ROWS; y++) {
        volatile uint16_t *dst = g_map_vram + (y * DASH_MAP_PITCH * 2);
        for (int x = 0; x < DASH_COLS; x++) {
            dst[x * 2]     = (uint16_t) DASH_PAL_NO;
            dst[x * 2 + 1] = (uint16_t) (g_char_base + DT_BLANK);
        }
    }

    // NBG2 above NBG0, and NBG3's own priority is left alone so the text stays
    // above both. NBG0 carries the wallpaper in the CD build and nothing at all
    // in the netbin, where this line orders an empty layer -- harmless, and
    // cheaper than a second path for the sake of a register nobody reads.
    slPriorityNbg0(1);
    slPriorityNbg2(2);

    SRL::Math::Types::Vector2D origin(0, 0);
    SRL::VDP2::NBG2::SetPosition(origin);
    SRL::VDP2::NBG2::ScrollEnable();

    SRL::Core::OnAfterSync += &flush_hook;
    g_ready = true;
    return true;
}

void dash_set(int variant, int base_row)
{
    if (!g_ready) return;
    dash_build(variant, base_row);
}

int dash_ready(void) { return g_ready ? 1 : 0; }

void dash_hold(void)
{
    if (!g_in_game || !g_kbd_visible) return;
    int border_top = TOP_MARGIN + console_height() + 1;
    if (g_cmd_mode == IFACE_PANEL) dash_set(DASH_PANEL, border_top);
    else                           dash_set(DASH_GAMEKB, border_top);
}

