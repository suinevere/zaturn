/*----------------------
 | options.cxx
 | Description: Load/save of the persisted MOJOOPTS blob and the runtime apply
 |   of display settings to VDP2. Owns no menu UI -- the option pages call in
 |   here.
 | Author: suinevere
 | Dependencies: app_state.h, input.h, display.h, saturn_backup.h,
 |   title.h, SRL
 ----------------------*/

#include <srl.hpp>

#include "options.h"
#include "app_state.h"
#include "input.h"
#include "display.h"
#include "text_map.h"   // TEXT_DIM_CRAM, the entry the dim ink lives in
#include "dash_view.h"  // dash_tint, the marble's sixteen CRAM entries
#ifndef NETBIN
#include "title.h"
#include "room_art.h"
#endif

extern "C" {
#include "saturn_backup.h"
}

/*----------------------
 | text_dim_rgb
 | Description: `ink` mixed five-eighths of the way from `bg`, per RGB555
 |   channel -- the colour an unselected menu row is drawn in, so the selected
 |   one stands out by being the only text at the player's chosen brightness.
 |   Mixed TOWARD THE BACKGROUND rather than toward black, because a preset can
 |   put dark text on a light ground (ZX Spectrum, Mac Classic, TI-99/4A) and
 |   darkening black text leaves it black -- the selected row would then look
 |   exactly like the rest. Reducing contrast against whatever is behind the
 |   text always reads as "less prominent", whichever way round the preset is.
 |   Inside a menu the background really is this flat colour even under a
 |   wallpaper, since MenuBacking's window suppresses the picture over the box.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: ink -- the text colour; bg -- the colour behind it
 | Returns: the mixed RGB555 word
 ----------------------*/
static unsigned short text_dim_rgb(unsigned short ink, unsigned short bg) {
    unsigned r = (((ink        & 31) * 5) + ((bg        & 31) * 3)) >> 3;
    unsigned g = ((((ink >>  5) & 31) * 5) + (((bg >>  5) & 31) * 3)) >> 3;
    unsigned b = ((((ink >> 10) & 31) * 5) + (((bg >> 10) & 31) * 3)) >> 3;
    return (unsigned short) (0x8000 | (b << 10) | (g << 5) | r);
}

/*----------------------
 | text_set_color
 | Description: Writes the Saturn RGB555 word `rgb555` into the VDP2 CRAM
 |   entries that color the SGL debug font, the reverse-video letter, and the
 |   block cursor, via the raw VDP2_COLRAM address (a bare integer, hence the
 |   cast; it reaches this file through <srl.hpp>). The font lives in ASCII
 |   palette 0, not palette 1: colorBank's declarator initializes it to
 |   1 << 12, but Core::Initialize -> VDP2::Initialize calls
 |   ASCII::SetPalette(0) before any of our code runs and nothing here calls
 |   SetPalette again, and NBG3 is COL_TYPE_16 (4bpp), so palette 0 is CRAM
 |   entries 0-15 (bytes 0-31). Three entries matter and entry 1 and entry 15
 |   are not adjacent: entry 1 is the glyph foreground (VDP2::Initialize seeds
 |   it via SetPrintPaletteColor(0, White), which writes 1 + (index << 8); its
 |   other six calls, index 1..6, land on entries 257, 513, ... which a 4bpp
 |   cell cannot reach, so index 0 is the only one that colors anything);
 |   entry 2 is the letter punched out of a reverse-video cell's solid ink
 |   block (glyph_invert.h) and is painted the BACKGROUND colour. It used to be
 |   forced black, on the reasoning that black stays legible against every
 |   background this page can set -- but the letter does not sit on the
 |   background, it sits inside a block of the ink, so what it has to contrast
 |   with is the ink. Six presets set the text colour to black (ZX Spectrum,
 |   TRS-80 CoCo, TI-99/4A, Mac Classic, Monochrome P3), and on those the block
 |   and the punched letter were both black, so every highlighted word in the
 |   command panel, the compass rose and the on-screen keyboards came out a
 |   solid black box. The background colour instead gives the punched letter
 |   exactly the contrast against the ink that the player already chose for
 |   text against ground, which is correct for every preset by construction; and entry 15 is the cursor (install_block_glyph() fills its tile
 |   with 0xFF, and 4bpp pixel value 15 selects entry 15). SRL::ASCII::SetColor
 |   cannot be used for the glyphs: it indexes from (colorBank >> 6), which is
 |   0 here, so SetColor(c, i) writes
 |   entry i -- that reaches the cursor at i=15 but never the glyphs, which is
 |   why changing Text previously appeared to do nothing. A print-time colour
 |   call is likewise no use: text_print bakes the palette bank into the
 |   pattern name it writes. The CRAM address is in the SH-2's uncached
 |   mirror, so no flush is needed, and the only DMA into CRAM
 |   (CRAM::Palette::Load) targets bank 1 at entries 256+ and never overlaps.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: rgb555 -- Saturn RGB555 color word; bg555 -- the background the text
 |   sits on, which the dim ink is mixed toward
 | Returns: N/A
 ----------------------*/
void text_set_color(unsigned short rgb555, unsigned short bg555) {
    volatile unsigned short *cram = (volatile unsigned short *) VDP2_COLRAM;
    cram[1]  = rgb555;   // glyph foreground
    cram[2]  = bg555;    // reverse-video letter, punched out of the ink block
    cram[15] = rgb555;   // install_block_glyph()'s cursor tile
    cram[TEXT_DIM_CRAM] = text_dim_rgb(rgb555, bg555);   // unselected menu rows
    dash_tint(bg555);   // and the marble chrome, which follows the same ground
}

/*----------------------
 | display_apply
 | Description: Recolors via text_set_color (writes both the glyph CRAM entry
 |   and install_block_glyph's cursor entry in one call; recolouring at print
 |   time is not an option, since text_print bakes the palette bank into the
 |   pattern name it writes). Sets the back plane BEFORE any
 |   image load, because it is what shows through the transparent menu frames
 |   and is on screen during the 1-2s CD read. Re-applies the held wallpaper
 |   dim (title_bg_dim_set) unconditionally, same as the color and back-plane
 |   writes above it -- this is what makes a saved dim reach VDP2 on boot and
 |   what restores the pre-edit dim when the Display Options page cancels,
 |   since g_display is already back to its snapshot by the time this runs.
 |   On image-load failure it drops
 |   to a color preset -- and if the failed palette WAS Dynamic (the only one
 |   left that carries a picture), rewrites it to preset 12 (IBM PC/MDA) so the
 |   broken picture is not re-selected.
 |
 |   The image branch reads the disc whenever the wanted picture is not cache-
 |   resident, which since the art stopped being preloaded is most of them. Every
 |   caller that can be reached with CD-DA playing has to cover that: the mode
 |   menu pauses the track around options_menu() (main.cxx) and the in-game
 |   Options menu already did (saturn_glue.cxx).
 |     When no image resolves and the palette is Dynamic on an authored game,
 |   the "no image" branch asks room_art to redraw instead of hiding NBG0:
 |   room_art owns that layer on this path and title_bg_loaded_file's area stem
 |   can never resolve through display_image_slot, so hiding here would blank the
 |   room picture every time this page re-applies (Display Options cancel, any
 |   cycler press). Redrawing rather than only leaving it alone is what puts the
 |   picture up the moment the Palette row lands on Dynamic.
 | Author: suinevere
 | Dependencies: display.h, title.h, room_art.h, SRL
 | Globals: g_display
 | Params: N/A
 | Returns: true if applied; false if a load failed and the fallback was
 |   installed
 |   Under NETBIN the picture branch is compiled out entirely -- that build
 |   links no title.cxx and reads no disc, so there is no room art to show and
 |   the solid back-colour path is the only reachable one.
 ----------------------*/
bool display_apply(void) {
    text_set_color(display_text_rgb(g_display.text), display_bg_rgb(g_display.bg));
    SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
#ifndef NETBIN
    title_bg_dim_set(display_dim_offset(g_display.dim));
    if (g_display.palette == DISP_PAL_DYNAMIC && room_art_available()) {
        // The only picture route there is. Redraw rather than merely leave it
        // alone -- this is the path the Palette row takes when it lands on
        // Dynamic, and the room subscriber fires only on a room change, so
        // otherwise the picture would not appear until the player walked
        // somewhere. Free when it is already up: room_art_show short-circuits
        // to a bare ScrollEnable.
        room_art_reshow();
    } else {
        title_bg_hide();
    }
#endif
    return true;
}

/*----------------------
 | display_cycle_row
 | Description: For DCR_BG/DCR_TEXT/DCR_DIM, steps that field and applies -- a
 |   plain change that cannot fail to load, unlike a palette entry. For
 |   DCR_PALETTE, repeatedly steps the palette in `dir` and applies, restoring
 |   the pre-step state and retrying whenever the candidate fails to load, for
 |   up to display_palette_count() tries; only the Palette row can hit that
 |   failure path, since it is the one carrying picture presets. The
 |   restore-and-retry matters because display_apply() installs a color-preset
 |   fallback on failure, which rewrites the very index being cycled --
 |   without restoring it first, the next press would resume from the
 |   fallback and land on the same bad image, making every image past it
 |   unreachable. If every candidate fails (a disc whose images are all
 |   unreadable), the loop gives up and lets the fallback from the last
 |   display_apply() call stand.
 | Author: suinevere
 | Dependencies: display.h
 | Globals: g_display
 | Params: which -- DCR_PALETTE, DCR_BG, DCR_TEXT, or DCR_DIM; dir -- -1 or +1
 | Returns: N/A
 ----------------------*/
void display_cycle_row(DisplayCycleRow which, int dir) {
    if (which != DCR_PALETTE) {
        if (which == DCR_BG) display_cycle_bg(&g_display, dir);
        else if (which == DCR_TEXT) display_cycle_text(&g_display, dir);
        else if (which == DCR_DIM) display_cycle_dim(&g_display, dir);
        display_apply();     // these rows cannot fail to load, unlike DCR_PALETTE;
                              // also re-applies the dim -- see display_apply's comment
        return;
    }
    int tries = display_palette_count();
    while (tries-- > 0) {
        display_cycle_palette(&g_display, dir);
        DisplayState want = g_display;
        if (display_apply()) return;   // showing what was asked for
        g_display = want;              // keep our place and step past the bad entry
    }
    display_apply();
}

/*----------------------
 | options_load
 | Description: Restores persisted game options from the backup-RAM MOJOOPTS
 |   blob into the app_state/input globals, defaulting any field the blob
 |   lacks. Layout, in order: difficulty (1 byte, accepted only if <=
 |   DIFF_HARD); the dial number as a NUL-terminated string (copied up to
 |   DIALNUM_MAX chars, but the scan still advances to the STORED string's own
 |   NUL rather than the copy's, because a blob written before the 11-digit
 |   cap can hold a longer number and every field below is located relative to
 |   that terminator); two audio-level bytes [music][pcm] each 0..7 -- a
 |   legacy blob instead stored one sound flag here (1 = off, else on), so
 |   when the pair doesn't parse as two in-range levels it is read as that
 |   flag and mapped to pcm 0 (off) or 4, music forced to 7; a controller-
 |   mapping block, format sentinel 3 followed by FA_N face-button bytes then
 |   CA_N chord-slot bytes, each byte accepted only if within range, applied
 |   only when the sentinel matches. Sentinel 2 is the same block one face byte
 |   shorter, from before Space became remappable, and is still read -- the width
 |   found is what every block behind it is measured from (an absent block keeps
 |   the compiled default mapping); a sound block, sentinel 1 followed by [mix][track]; a
 |   gameplay block -- sentinel 5 followed by one VERB_* verbosity byte (the
 |   original form), or sentinel 7 followed by the verbosity byte plus a packed
 |   byte (bit 0 = g_cmd_iface, bit 1 = g_toggle_btn), the form that also carries
 |   the command-panel preference and its toggle-button binding. Either sentinel
 |   is accepted so a blob written by an older build still restores its
 |   verbosity rather than being silently reset; when the block is absent
 |   entirely every gameplay field keeps its compiled default; and finally a
 |   display block handed to display_decode() with whatever bytes remain in the
 |   64-byte buffer rather than a fixed width, so the older 4-byte form still
 |   parses even when a long stored dial number leaves too little room for the
 |   name-bearing current form. buf is zero-filled up front, so bytes past
 |   whatever was actually written read as an absent block. display_decode()
 |   resolves an image reference against the compiled-in category tables rather
 |   than a runtime scan, so this carries no ordering requirement against disc
 |   access.
 | Author: suinevere
 | Dependencies: saturn_backup.h, display.h, input.h
 | Globals: g_difficulty, g_dialnum, g_music_level, g_pcm_level, g_face_btn,
 |   g_chord_slot, g_verbosity, g_cmd_iface,
 |   g_toggle_btn, g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void options_load(void) {
    uint8_t buf[64];
    for (int z = 0; z < (int) sizeof(buf); z++) buf[z] = 0;
    if (!saturn_bup_read(SATURN_BUP_CONSOLE, "MOJOOPTS", buf)) return;
    if (buf[0] <= DIFF_HARD) g_difficulty = (int) buf[0];
    int i = 1;   // tracks the offset of the dial number's terminating NUL
    if (buf[1]) {
        int j;
        for (j = 0; buf[1 + j] && j < (int) sizeof(g_dialnum) - 1; j++) g_dialnum[j] = (char) buf[1 + j];
        g_dialnum[j] = '\0';
        while (buf[1 + j] && 1 + j < (int) sizeof(buf) - 1) j++;
        i = 1 + j;
    }
    if (i + 1 < (int) sizeof(buf)) {
        uint8_t a = buf[i + 1], b = (i + 2 < (int) sizeof(buf)) ? buf[i + 2] : 0xFF;
        if (a <= 7 && b <= 7) { g_music_level = a; g_pcm_level = b; }
        else { g_pcm_level = (a == 1) ? 0 : 4; g_music_level = 7; }   // legacy sound flag
    }
    /* The mapping block grew a face action -- Space joined Accept, Backspace and
       Type-letter when it stopped being a fixed X -- so sentinel 2's block is one
       byte shorter than sentinel 3's, and everything after it (sound, gameplay,
       display) sits at a different offset. Both widths are read, and the width
       actually found is what the rest of the layout is measured from; getting
       that wrong would not lose the mapping, it would silently misparse every
       block behind it. A sentinel-2 blob carries no Space byte, so Space keeps
       its compiled default of X -- which is where it always fired. */
    const int FA_N_V1 = 3;
    int m = i + 3;
    int fa_stored = (buf[m] == 3) ? FA_N : FA_N_V1;
    int btn_max   = (buf[m] == 3) ? FA_BTN_N : FA_N_V1;
    if (m + 1 + fa_stored + CA_N <= (int) sizeof(buf) && (buf[m] == 2 || buf[m] == 3)) {
        for (int a = 0; a < fa_stored; a++) { int v = buf[m + 1 + a];             if (v < btn_max) g_face_btn[a]   = v; }
        for (int a = 0; a < CA_N; a++)      { int v = buf[m + 1 + fa_stored + a]; if (v < SL_N)    g_chord_slot[a] = v; }
    }
    /* The sound block's sentinel and its two bytes -- the mix mode and the
       selected track -- are read past rather than read. Both settings are gone;
       the bytes stay reserved because every block behind them is positional, and
       reclaiming two bytes would silently misparse every blob already written. */
    int s = m + 1 + fa_stored + CA_N;
    /* The gameplay block sits between the sound block and the display one because
       the display block is the variable-width tail. Sentinel 5 (v1, verbosity
       only) and sentinel 7 (v2, verbosity plus a packed command-interface byte)
       rather than 3 or 6: this byte is where a blob written before the block
       existed has its display sentinel, and those run 1..4, 6 and 8, so 5 and
       7 -- neither ever a display sentinel, and both off-limits to the next one
       for that reason -- are the values that cannot be mistaken for one. A v1
       blob (sentinel 5) is still accepted so an older
       build's save is not silently reset; it just leaves g_cmd_iface/g_toggle_btn
       at their compiled defaults, same as it always left them unset. When the
       block is absent entirely the display block starts here instead and every
       gameplay field keeps its compiled default -- which is how an old save
       comes back verbose rather than silently brief. */
    int gp = s + 3, dsp = gp;
    if (gp + 1 < (int) sizeof(buf) && buf[gp] == 5) {
        if (buf[gp + 1] <= VERB_VERBOSE) g_verbosity = buf[gp + 1];
        dsp = gp + 2;
    } else if (gp + 2 < (int) sizeof(buf) && buf[gp] == 7) {
        if (buf[gp + 1] <= VERB_VERBOSE) g_verbosity = buf[gp + 1];
        g_cmd_iface  = buf[gp + 2] & 1;
        g_toggle_btn = (buf[gp + 2] >> 1) & 1;
        dsp = gp + 3;
    }
    if (dsp + 4 <= (int) sizeof(buf)) {
        display_decode(buf + dsp, (int) sizeof(buf) - dsp, &g_display);
    }
}

/*----------------------
 | options_save
 | Description: Serializes the current option globals into the same MOJOOPTS
 |   layout options_load reads: difficulty byte; NUL-terminated dial number;
 |   music and pcm level bytes; controller-mapping sentinel byte (3) followed
 |   by the face-button and chord-slot bytes; sound-block sentinel byte (1)
 |   followed by two reserved bytes that used to carry the mix mode and the
 |   selected track; gameplay-block sentinel byte (7)
 |   followed by the verbosity byte and a packed byte (bit 0 = g_cmd_iface, bit
 |   1 = g_toggle_btn); then the display block from display_encode(), appended
 |   only if it fits the remaining space in the 62-byte payload. Writes the
 |   assembled buffer to backup RAM under the "MOJOOPTS" filename.
 | Author: suinevere
 | Dependencies: saturn_backup.h, display.h, input.h
 | Globals: g_difficulty, g_dialnum, g_music_level, g_pcm_level, g_face_btn,
 |   g_chord_slot, g_verbosity, g_cmd_iface,
 |   g_toggle_btn, g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void options_save(void) {
    uint8_t buf[64]; int n = 0;
    buf[n++] = (uint8_t) g_difficulty;
    for (int i = 0; g_dialnum[i] && n < 62; i++) buf[n++] = (uint8_t) g_dialnum[i];
    buf[n++] = 0;
    buf[n++] = (uint8_t) g_music_level;           // audio levels: [music][pcm], 0..7
    buf[n++] = (uint8_t) g_pcm_level;
    buf[n++] = 3;                                 // controller-mapping format sentinel: + Space
    for (int a = 0; a < FA_N && n < 62; a++) buf[n++] = (uint8_t) g_face_btn[a];
    for (int a = 0; a < CA_N && n < 62; a++) buf[n++] = (uint8_t) g_chord_slot[a];
    buf[n++] = 1;                                 // sound-block sentinel
    buf[n++] = 0;                                 // reserved (was the mix mode)
    buf[n++] = 0;                                 // reserved (was the selected track)
    buf[n++] = 7;                                 // gameplay-block sentinel, v2
    buf[n++] = (uint8_t) g_verbosity;             // VERB_*
    buf[n++] = (uint8_t) ((g_cmd_iface & 1) | ((g_toggle_btn & 1) << 1));
    if (n + DISP_BLOB_BYTES <= 62) n += display_encode(&g_display, buf + n);
    saturn_bup_write(SATURN_BUP_CONSOLE, "MOJOOPTS", "options", buf, (uint32_t) n);
}

/*----------------------
 | valid_dialnum
 | Description: Scans `s` and rejects it unless every character is a digit,
 |   the string is non-empty, and its length is at most DIALNUM_MAX (the
 |   fixed storage in g_dialnum has no room past that).
 | Author: suinevere
 | Dependencies: app_state.h
 | Globals: N/A
 | Params: s -- candidate dial-number string
 | Returns: true if `s` passes validation
 ----------------------*/
bool valid_dialnum(const char *s) {
    if (!s[0]) return false;
    int i = 0;
    for (; s[i]; i++) if (s[i] < '0' || s[i] > '9') return false;
    return i <= DIALNUM_MAX;   // g_dialnum has no room past this
}
