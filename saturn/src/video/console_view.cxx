/*----------------------
 | console_view.cxx
 | Description: Implements console-scrollback and on-screen-keyboard rendering,
 |   input-device hint tracking, and the blinking block text cursor (including
 |   its one-time DEL-slot glyph and the input-line drawing it shares between the
 |   real-keyboard and on-screen-keyboard layouts), plus typeahead_edit -- the
 |   one-frame input-editing pass with typeahead that the local prompt and the
 |   online terminal share.
 | Author: suinevere
 | Dependencies: console_view.h, app_state.h, command_view.h, input.h, dash_view.h,
 |   console.c, keyboard.c, typeahead.c, SRL
 ----------------------*/

#include <srl.hpp>
#include "text_map.h"
#include "console_view.h"
#include "app_state.h"
#include "command_view.h"
#include "command_rose.h"
#include "rose_draw.h"
#include "game_kb.h"
#include "input.h"
#include "dash_view.h"

// ---- rendering -------------------------------------------------------------

/*----------------------
 | SCREEN_ROWS
 | Description: The 30 text rows (0..29) the debug layer provides. The on-screen
 |   keyboard occupies the bottom rows when shown; when hidden (real keyboard in
 |   hand) those rows go back to the console for more text.
 | Author: suinevere
 ----------------------*/
static const int SCREEN_ROWS = 30;

/*----------------------
 | TOP_MARGIN
 | Description: One blank row kept at the top because TV overscan clips the first
 |   text row on real hardware. Console content starts on row 1; menus already
 |   draw from row 1+, so this only affects the console layout. Declared extern
 |   in console_view.h so dash_view.cxx's dash_hold can share this single copy.
 | Author: suinevere
 ----------------------*/
const int TOP_MARGIN = 1;

/*----------------------
 | g_kbd_visible
 | Description: Tracks whether the on-screen keyboard is showing (gamepad in hand)
 |   vs hidden (real keyboard). The caret-vs-suggestion arrow roles that used to
 |   live beside this now come from keyboard_get_insert().
 | Author: suinevere
 ----------------------*/
bool g_kbd_visible = true;

/*----------------------
 | console_height
 | Description: Subtracts TOP_MARGIN from SCREEN_ROWS for the available rows,
 |   then reserves one of three ways: with a real keyboard in hand (on-screen
 |   keyboard hidden) just the 1 input row; with a gamepad in hand, a game
 |   running, and the command panel selected, 1 input row + CV_STRIP_ROWS panel
 |   rows + 2 panel borders; every other gamepad-in-hand case (the on-screen
 |   keyboard explicitly selected, OR no game running at all -- the online
 |   terminal before/without a story, and the whole netbin build, which never
 |   assigns g_cmd_mode), 1 input row + KB_ROWS keyboard rows. The
 |   g_in_game gate matters because g_cmd_mode initializes to IFACE_PANEL and is
 |   assigned nowhere else: without it, the panel's layout would leak into
 |   every screen that draws through render_keyboard instead of the panel --
 |   title-screen online terminal, netbin -- silently shrinking the console and
 |   leaving rows unclaimed by any renderer.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_kbd_visible, g_cmd_mode, g_in_game
 | Params: N/A
 | Returns: the number of rows the console view may draw into
 ----------------------*/
/*----------------------
 | WIN_W0_* / WIN_NBG0_SUPPRESS
 | Description: The VDP2 window-0 WCTL byte that suppresses the image behind a
 |   box. SGL exposes no constants, so the encoding was read from the library:
 |   slScrWindowMode(scrn, mode) stores `mode` at 0x060ffd90 + scrn into SGL's
 |   WCTLA..WCTLD shadow (flushed at vblank), so `mode` is the raw per-screen WCTL
 |   byte. ENABLE = bit 1 (window 0 applies here), INSIDE/OUTSIDE = bit 0 (which
 |   side of the rect is the window). WIN_NBG0_SUPPRESS is the combined value; if
 |   the image ever hides everywhere except the box, swap INSIDE for OUTSIDE.
 | Author: suinevere
 ----------------------*/
#define WIN_W0_ENABLE       0x02
#define WIN_W0_INSIDE       0x00
#define WIN_W0_OUTSIDE      0x01
#define WIN_NBG0_SUPPRESS   (WIN_W0_ENABLE | WIN_W0_INSIDE)

/*----------------------
 | image_window_box
 | Description: See console_view.h. Converts text cells to pixels (cells are 8x8,
 |   the display is 320x224) and clamps to the screen.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: x0, y0 -- top-left corner in text cells; w, h -- size in cells
 | Returns: N/A
 ----------------------*/
void image_window_box(int x0, int y0, int w, int h) {
    int x1 = x0 * 8,             y1 = y0 * 8;
    int x2 = (x0 + w) * 8 - 1,   y2 = (y0 + h) * 8 - 1;
    if (x2 > 319) x2 = 319;
    if (y2 > 223) y2 = 223;
    if (x1 < 0)   x1 = 0;
    if (y1 < 0)   y1 = 0;
    slScrWindow0((uint16_t) x1, (uint16_t) y1, (uint16_t) x2, (uint16_t) y2);
}

/*----------------------
 | image_window_on
 | Description: See console_view.h. Cancels any window-off still owed to the next
 |   text flush before switching on, so a caller that arms the window every frame
 |   cannot be switched back off underneath itself by a menu that has just closed.
 | Author: suinevere
 | Dependencies: SRL, text_map.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void image_window_on(void) {
    text_on_flush(nullptr);
    slScrWindowModeNbg0(WIN_NBG0_SUPPRESS);
}

/*----------------------
 | image_window_off
 | Description: See console_view.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void image_window_off(void) {
    slScrWindowModeNbg0(0);
}

/*----------------------
 | SGL_TVMD_SHADOW / VDP2_TVMD_BDCLMD
 | Description: SGL's shadow copy of the VDP2 TVMD register, and the border
 |   colour mode bit inside it (0 = black border, 1 = back-screen colour).
 |
 |   SGL owns TVMD and rewrites it from this shadow, so the hardware register at
 |   0x25f80000 cannot be poked directly -- the write is undone. slTVOn/slTVOff
 |   disassemble to a read-modify-write of `@(192, gbr)`, which puts the shadow
 |   at GBR + 0xC0; SGL sets GBR to 0x060ffc00 (workarea.c's MasterStack), so
 |   TVMD's shadow is 0x060ffcc0 and the block from there mirrors the VDP2
 |   register offsets one for one. Five SGL functions confirm that layout, each
 |   landing exactly on its register: slScrCycleSet 0x060ffcd0 (CYCA0L 0x010),
 |   slScrScaleNbg0/1 0x060ffd38/0x060ffd48 (ZMXIN0 0x078 / ZMXIN1 0x088),
 |   slScrWindowMode 0x060ffd90 (WCTLA 0x0D0 -- the one menu.c already relies
 |   on), slColorCalcOn 0x060ffdac (CCCTL 0x0EC), slPriority 0x060ffdb8
 |   (PRINA 0x0F8).
 |
 |   SGL exposes no wrapper for this bit, which is why it is reached by address.
 |   Nothing else writes the shadow after boot -- slTVOn and slTVOff touch only
 |   bit 15 (DISP), and preserve the rest -- so clearing it once holds.
 | Author: suinevere
 ----------------------*/
#define SGL_TVMD_SHADOW    ((volatile uint16_t *) 0x060ffcc0)
#define VDP2_TVMD_BDCLMD   0x0100

/*----------------------
 | border_use_black
 | Description: See console_view.h. Clears BDCLMD in SGL's TVMD shadow, which
 |   SGL flushes to the hardware register at the next vblank.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void border_use_black(void) {
    *SGL_TVMD_SHADOW = (uint16_t) (*SGL_TVMD_SHADOW & ~VDP2_TVMD_BDCLMD);
}

int console_height(void) {
    int avail = SCREEN_ROWS - TOP_MARGIN;
    if (!g_kbd_visible) return avail - 1;
    /* In game both interfaces are the same bordered strip -- command panel or
       keyboard, each is the input line, two borders, and the seven-row rose. Off
       the title screen's online terminal keeps the smaller four-row grid. */
    if (g_in_game) return avail - (1 + CV_STRIP_ROWS + 2);
    return avail - (1 + KB_ROWS);
}

/*----------------------
 | console_screen_rows
 | Description: See console_view.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: SCREEN_ROWS
 ----------------------*/
int console_screen_rows(void) {
    return SCREEN_ROWS;
}

/*----------------------
 | hint
 | Description: Returns `pad` while the on-screen keyboard is showing (gamepad in
 |   hand) or `kbd` once it is hidden (real keyboard in hand), so the same call
 |   site's on-screen text always names the device the player is actually using.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_kbd_visible
 | Params: pad -- hint text for gamepad + on-screen keyboard; kbd -- hint text for
 |   a real keyboard
 | Returns: whichever of pad/kbd matches the last-used device
 ----------------------*/
const char *hint(const char *pad, const char *kbd) {
    return g_kbd_visible ? pad : kbd;
}

/*----------------------
 | note_input_device
 | Description: A real-keyboard key event clears g_kbd_visible (hides the
 |   on-screen keyboard); otherwise, any gamepad button edge this frame sets it
 |   back. Call once per input frame with that frame's key event.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_kbd_visible, g_pad
 | Params: ke -- this frame's keyboard event
 | Returns: N/A
 ----------------------*/
void note_input_device(const SaturnKeyEvent &ke) {
    if (ke.kind != SATURN_KEY_NONE) g_kbd_visible = false;
    else if (g_pad->AnyPressed())   g_kbd_visible = true;
}

// ---- scrollback ------------------------------------------------------------

/*----------------------
 | g_output_start
 | Description: The console_total_lines() mark taken before a turn's output (0 for
 |   the initial room), so console_scroll_to_output can land on the TOP of a long
 |   response instead of its bottom.
 | Author: suinevere
 ----------------------*/
long g_output_start = 0;

/*----------------------
 | g_more_below
 | Description: Set by render_console: true when off-screen text remains below the
 |   view (the "more v" marker is showing). render_keyboard reads it to repaint the
 |   marker in real-keyboard mode, where the input line is drawn over the console's
 |   last row and would otherwise wipe it.
 | Author: suinevere
 ----------------------*/
static bool g_more_below = false;

/*----------------------
 | render_console
 | Description: Clamps g_scroll to [0, maxstart+1] (the +1 allows one blank line
 |   past the top as a scroll-limit affordance), then prints `rows` console lines
 |   starting from the computed `start` index. Prints a "^" at column 39 of the
 |   top row when older text is scrolled off above, and sets g_more_below (and
 |   prints "more v" at column 34 -- chosen so the 6-wide marker's trailing 'v'
 |   lands inside the 40-cell text layer instead of clipping at column 35) when
 |   newer text remains below the window.
 | Author: suinevere
 | Dependencies: console.c, text_map.h
 | Globals: g_scroll, g_more_below
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void render_console(void) {
    int rows = console_height();
    int total = console_line_count();
    int maxstart = (total > rows) ? (total - rows) : 0;
    if (g_scroll < 0)            g_scroll = 0;
    if (g_scroll > maxstart + 1) g_scroll = maxstart + 1;
    int top_blank = (g_scroll == maxstart + 1) ? 1 : 0;
    int start = maxstart - (g_scroll - top_blank);
    for (int r = 0; r < rows; r++) {
        text_clear_line(TOP_MARGIN + r);
        int li = start + r - top_blank;
        if (li >= 0 && li < total)
            text_print(0, TOP_MARGIN + r, "%s", console_get_line(li));
    }
    if (start > 0 && !top_blank) text_print(39, TOP_MARGIN, "^");
    g_more_below = (start + rows < total);
    if (g_more_below)            text_print(34, TOP_MARGIN + rows - 1, "more v");
}

/*----------------------
 | console_scroll_to_output
 | Description: Computes how many lines the turn just emitted from the delta
 |   against g_output_start (using the monotonic total-lines counter so this
 |   stays correct even after old lines evict from the 128-line ring). If that
 |   exceeds the visible rows, sets g_scroll so the turn's first row lands at the
 |   top of the window (clamped to the oldest surviving line if the turn is
 |   longer than the ring); otherwise sets g_scroll to 0 (the live bottom).
 | Author: suinevere
 | Dependencies: console.c
 | Globals: g_output_start, g_scroll
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void console_scroll_to_output(void) {
    int total = console_line_count(), rows = console_height();
    int maxstart = (total > rows) ? (total - rows) : 0;
    long added = console_total_lines() - g_output_start;
    if (added > rows) {
        int top = total - (int) added;
        if (top < 0) top = 0;
        g_scroll = maxstart - top;
        if (g_scroll < 0) g_scroll = 0;
    } else {
        g_scroll = 0;
    }
}

// ---- blinking block cursor -------------------------------------------------

/*----------------------
 | CURSOR_BLOCK_STR
 | Description: The one-character string printed as the text cursor. The SGL ASCII
 |   font has no solid-block glyph, so one is carved into the otherwise-unused DEL
 |   (0x7F) slot (see install_block_glyph). ASCII::Print addresses font 0's char
 |   data at VDP2_VRAM_B1 + 0x18000 + (char+640)*0x20; for 0x7F that is +0x1DFE0,
 |   the last tile LoadFontSG populated. The fill uses color index 15, a different
 |   CRAM entry than the glyphs (index 1), so text_set_color writes both to keep
 |   the block the same color as the text.
 | Author: suinevere
 ----------------------*/
static const char CURSOR_BLOCK_STR[2] = { (char) 0x7f, '\0' };

/*----------------------
 | install_block_glyph
 | Description: Fills the DEL (0x7F) font tile at VDP2_VRAM_B1 + 0x18000 +
 |   (0x7f + 640)*0x20 with 0xFF (every 4bpp pixel set to color index 15), so
 |   printing CURSOR_BLOCK_STR renders a solid block instead of a character.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void install_block_glyph(void) {
    volatile uint8_t* tile =
        (volatile uint8_t*)(VDP2_VRAM_B1 + 0x18000 + (0x7f + 640) * 0x20);
    for (int i = 0; i < 32; i++) tile[i] = 0xFF;
}

/*----------------------
 | draw_input_line
 | Description: Prints "> {input}{ghost}" at (0,row). The ghost (the
 |   typeahead completion's remaining characters, case-matched to whatever the
 |   player was typing) is only appended when the caret sits at the end of the
 |   line -- a mid-line caret means the player is editing, so the completion is
 |   suppressed. The blinking block cursor overprints whichever cell it
 |   currently sits on: the character under the caret when mid-line, else the
 |   ghost's next character or a space; when the block is "off" that cell's
 |   real character prints instead, so it appears to blink. Only called from
 |   render_keyboard, so it stays file-local.
 | Author: suinevere
 | Dependencies: keyboard.c, SRL
 | Globals: N/A
 | Params: row -- console row to draw on; k -- current keyboard/input-line
 |   state; prediction -- the selected typeahead completion, or null;
 |   current_word_len -- length of the word being completed; block_on --
 |   whether the cursor block is in its "on" blink phase
 | Returns: N/A
 ----------------------*/
static void draw_input_line(int row, const KeyboardState &k,
                            DictionaryWord* prediction, int current_word_len,
                            bool block_on) {
    const char* suffix = "";
    char sbuf[64];
    if (prediction && k.cursor == k.input_len && k.input_len < KB_INPUT_MAX - 1) {
        const char* g = prediction->text + current_word_len;
        bool up = current_word_len > 0 && k.input_len > 0 &&
                  k.input[k.input_len - 1] >= 'A' && k.input[k.input_len - 1] <= 'Z';
        int i = 0;
        for (; g[i] && i < (int) sizeof(sbuf) - 1; i++)
            sbuf[i] = (up && g[i] >= 'a' && g[i] <= 'z') ? (char) (g[i] - 'a' + 'A') : g[i];
        sbuf[i] = '\0';
        suffix = sbuf;
    }
    text_print(0, row, "> %s%s", k.input, suffix);

    int cursor_col = 2 + k.cursor;
    char under = (k.cursor < k.input_len) ? k.input[k.cursor]
                                          : (suffix[0] ? suffix[0] : ' ');
    if (block_on) text_print(cursor_col, row, "%s", CURSOR_BLOCK_STR);
    else          text_print(cursor_col, row, "%c", under);
}

/*----------------------
 | CURSOR_BLINK_FRAMES
 | Description: Half-period of the cursor blink in frames (~0.33s at 60fps, ~1.5Hz).
 | Author: suinevere
 ----------------------*/
#define CURSOR_BLINK_FRAMES 20

/*----------------------
 | typeahead_edit
 | Description: Gamepad editing runs first (Caps toggle on the L+R combo; history
 |   recall and text-caret moves on their configurable chords; plain D-pad moves
 |   the on-screen picker; the mapped face buttons type and backspace). Then a
 |   refresh lambda re-derives the current word (text after the last space), the
 |   previous word (looked up in the trie so grammar can filter), and the
 |   candidate list, resetting the cycle index whenever the current word changes.
 |   accept commits the ghost suffix, matching the case the player is typing --
 |   uppercasing the completion when the last typed char is uppercase (see
 |   draw_input_line). Typeahead is live only when the caret sits at the end of
 |   the line. Insert mode (keyboard_get_insert, latched by the physical Insert
 |   key) selects whether plain or Ctrl arrows move the caret vs cycle
 |   suggestions. Accept (mapped face button / Tab) commits the ghost with no
 |   trailing space, or -- with no ghost -- submits, unless the line already ends
 |   in a space so a just-typed separator does not fire the command; X commits the
 |   ghost plus a space, or types a space to open the next word.
 |   Remaining key events type/erase/submit/recall or fall through to scroll
 |   handling; ScrollLock selects whether plain or Ctrl Up/Down recalls history,
 |   the other pair scrolling one line.
 | Author: suinevere
 | Dependencies: keyboard.c, input.cxx, typeahead.c
 | Globals: g_pad
 | Params: k -- keyboard/input-line state, edited in place; root -- typeahead
 |   trie; sug_index -- suggestion-cycle index (in/out); sug_last -- word the
 |   cycle index belongs to (in/out); ke -- decoded key event, consumed as
 |   handled; pad -- gamepad is the active device; selected_out -- chosen
 |   suggestion or null; cw_len_out -- current word length
 | Returns: N/A
 ----------------------*/
/*----------------------
 | g_kb_on_rose / g_kb_rose_dir
 | Description: The in-game keyboard's focus -- whether the picker sits on the
 |   compass rose left of the keys, and the RM_* direction it holds when it does.
 |   File-scope because the keyboard view is a singleton whose focus outlives one
 |   frame.
 | Author: suinevere
 ----------------------*/
static bool g_kb_on_rose = false;
static int  g_kb_rose_dir = -1;

/*----------------------
 | kb_rose_row / kb_grid_row
 | Description: Map a keyboard grid row (0..GKB_ROWS-1) to the strip row it shares
 |   with the rose and back. The symbols and number rows sit above the divider on
 |   strip row 2; the three letter rows and the space bar sit below it, so a grid
 |   row from the qwerty row down draws one strip row lower than its index.
 | Author: suinevere
 | Params: grid_row / rose_row -- the row in the other geometry
 | Returns: the mapped row
 ----------------------*/
static int kb_rose_row(int grid_row) { return grid_row < 2 ? grid_row : grid_row + 1; }
static int kb_grid_row(int rose_row) {
    if (rose_row < 2)  return rose_row;
    if (rose_row == 2) return 2;
    return rose_row - 1;
}

/*----------------------
 | KB_EXITS_ALL / kb_exits
 | Description: The exit states the keyboard's rose draws. With an interpreter in
 |   hand that is the current room's own exits, flattened to conditional on Hard so
 |   the rose gives no more away than the panel's. With the game on a remote server
 |   there is no object tree to read and rooms share names with different exits, so
 |   every direction is offered and the server refuses the ones that do not exist.
 | Author: suinevere
 | Dependencies: room_model.h (RM_* constants; no link edge under NETBIN)
 | Globals: g_difficulty
 | Params: flat -- RM_DIR_N scratch the flattened copy is built in
 | Returns: the exit states to draw
 ----------------------*/
#ifdef NETBIN
static const unsigned char KB_EXITS_ALL[RM_DIR_N] = {
    RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN,
    RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN,
    RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN
};
static const unsigned char *kb_exits(unsigned char *flat) {
    (void) flat;
    /* Once multizorkd has named the room, the story in .rodata knows its real
       exits; until then it does not, and a model that has been bound but never
       refreshed reports every direction as NONE -- a rose with no way out at
       all, which is a worse lie than offering all twelve. */
    if (room_model_has_room()) return room_model_get()->exits;
    return KB_EXITS_ALL;
}
#else
static const unsigned char *kb_exits(unsigned char *flat) {
    const RoomModel *m = room_model_get();
    if (g_difficulty != DIFF_HARD) return m->exits;
    for (int i = 0; i < RM_DIR_N; i++)
        flat[i] = (m->exits[i] == RM_EXIT_OPEN) ? RM_EXIT_MAYBE : m->exits[i];
    return flat;
}
#endif

/*----------------------
 | game_kb_dpad
 | Description: One frame of D-pad for the in-game keyboard. On the keys it walks
 |   the grid; a left press off the leftmost column hands focus to the rose at the
 |   height it left from. On the rose it aims the press the way the panel's travel
 |   module does -- a fired edge plus a held one makes a diagonal -- and a press
 |   that carries off the right edge returns to the keys at the same height.
 | Author: suinevere
 | Dependencies: game_kb.h, command_rose.h, input.h
 | Globals: g_kb_on_rose, g_kb_rose_dir, g_pad
 | Params: k -- keyboard state whose picker is moved
 | Returns: N/A
 ----------------------*/
static void game_kb_dpad(KeyboardState &k) {
    unsigned char flat[RM_DIR_N];
    const unsigned char *exits = kb_exits(flat);
    bool fu = pad_fired(Button::Up),   fd = pad_fired(Button::Down);
    bool fl = pad_fired(Button::Left), fr = pad_fired(Button::Right);

    if (g_kb_on_rose) {
        if (!fr && !fl && !fd && !fu) return;
        int dx = fr ? 1 : fl ? -1 : g_pad->IsHeld(Button::Right) ? 1
                                  : g_pad->IsHeld(Button::Left)  ? -1 : 0;
        int dy = fd ? 1 : fu ? -1 : g_pad->IsHeld(Button::Down)  ? 1
                                  : g_pad->IsHeld(Button::Up)    ? -1 : 0;
        if (g_kb_rose_dir < 0 || cr_dir_row(g_kb_rose_dir) < 0) {
            g_kb_rose_dir = cr_enter(exits, 3, 1);
            if (g_kb_rose_dir < 0) g_kb_on_rose = false;
            return;
        }
        int dir = g_kb_rose_dir;
        if (cr_move(exits, dir, dx, dy, &dir) > 0) {
            g_kb_on_rose = false;
            k.cursor_row = kb_grid_row(cr_dir_row(dir));
            k.cursor_col = 0;
        }
        g_kb_rose_dir = dir;
        return;
    }

    if (fu) game_kb_move(&k.cursor_row, &k.cursor_col, 0, -1);
    if (fd) game_kb_move(&k.cursor_row, &k.cursor_col, 0, +1);
    if (fr) game_kb_move(&k.cursor_row, &k.cursor_col, +1, 0);
    if (fl && game_kb_move(&k.cursor_row, &k.cursor_col, -1, 0)) {
        int d = cr_enter(exits, kb_rose_row(k.cursor_row), 1);
        if (d >= 0) { g_kb_on_rose = true; g_kb_rose_dir = d; }
    }
}

/*----------------------
 | game_kb_travel
 | Description: Sends the rose's selected direction as a command the way the panel
 |   does -- the direction's word fills the line and is submitted -- when it names
 |   an exit the room actually offers.
 | Author: suinevere
 | Dependencies: command_rose.h, keyboard.h
 | Globals: g_kb_rose_dir
 | Params: k -- keyboard state the direction word is written into
 | Returns: N/A
 ----------------------*/
static void game_kb_travel(KeyboardState &k) {
    unsigned char flat[RM_DIR_N];
    const unsigned char *exits = kb_exits(flat);
    int d = g_kb_rose_dir;
    if (d < 0 || d >= RM_DIR_N) return;
    if (exits[d] != RM_EXIT_OPEN && exits[d] != RM_EXIT_MAYBE) return;
    keyboard_load_line(&k, cr_dir_word(d));
    keyboard_submit(&k);
}

void typeahead_edit(KeyboardState &k, TrieNode *root,
                    int &sug_index, char *sug_last,
                    SaturnKeyEvent &ke, bool pad,
                    DictionaryWord *&selected_out, int &cw_len_out) {
    if (pad) {
        if (caps_combo_fired()) keyboard_set_caps(!keyboard_get_caps());
        if (chord_fired(CA_RECALL, -1)) history_recall(&k, 1);
        if (chord_fired(CA_RECALL, +1)) history_recall(&k, 0);
        if (chord_fired(CA_CURSOR, -1)) keyboard_caret_left(&k);
        if (chord_fired(CA_CURSOR, +1)) keyboard_caret_right(&k);
        // X joined Z and Y as a chord shift when Recall moved onto X+Up/Dn, so
        // the grid cursor has to stand still for it too -- chord_shift_held is
        // the one place that knows which buttons those are.
        if (!chord_shift_held()) {
            if (g_in_game) game_kb_dpad(k);
            else
            {
                if (pad_fired(Button::Up))    keyboard_move(&k, 0, -1);
                if (pad_fired(Button::Down))  keyboard_move(&k, 0,  1);
                if (pad_fired(Button::Left))  keyboard_move(&k, -1, 0);
                if (pad_fired(Button::Right)) keyboard_move(&k,  1, 0);
            }
        }
        if (g_in_game) {
            if (g_kb_on_rose) {
                if (pad_fired(face_button(FA_TYPE))) game_kb_travel(k);
                if (pad_fired(face_button(FA_BACK))) g_kb_on_rose = false;
            } else {
                if (pad_fired(face_button(FA_TYPE)))
                    keyboard_type_char(&k, game_kb_char_at(k.cursor_row, k.cursor_col,
                                                           keyboard_get_caps()));
                if (pad_fired(face_button(FA_BACK))) keyboard_backspace(&k);
            }
        } else
        {
            if (pad_fired(face_button(FA_TYPE))) keyboard_type(&k);
            if (pad_fired(face_button(FA_BACK))) keyboard_backspace(&k);
        }
    }

    char current_word[256]; int cw_len; DictionaryWord *prev_word;
    DictionaryWord *cands[24]; int ncand; DictionaryWord *selected;
    auto refresh = [&]() {
        int ws = 0;
        for (int i = k.input_len - 1; i >= 0; i--) if (k.input[i] == ' ') { ws = i + 1; break; }
        cw_len = k.input_len - ws;
        if (cw_len > 255) cw_len = 255;
        for (int i = 0; i < cw_len; i++) current_word[i] = k.input[ws + i];
        current_word[cw_len] = '\0';
        prev_word = nullptr;
        if (ws > 1) {
            int ps = 0;
            for (int i = ws - 2; i >= 0; i--) if (k.input[i] == ' ') { ps = i + 1; break; }
            char pw[256]; int pl = (ws - 1) - ps; if (pl > 255) pl = 255;
            for (int i = 0; i < pl; i++) pw[i] = k.input[ps + i];
            pw[pl] = '\0';
            prev_word = find_exact_word(root, pw);
        }
        ncand = predict_candidates(root, prev_word, current_word, cands, 24, ws == 0);
        bool same = true;
        for (int i = 0; i <= cw_len; i++) if (current_word[i] != sug_last[i]) { same = false; break; }
        if (!same) { sug_index = 0; for (int i = 0; i <= cw_len; i++) sug_last[i] = current_word[i]; }
        if (ncand == 0) sug_index = 0; else if (sug_index >= ncand) sug_index %= ncand;
        selected = ncand > 0 ? cands[sug_index] : nullptr;
    };
    refresh();

    auto ghost_len = [&]() -> int {
        if (!selected) return 0;
        int n = 0; while (selected->text[n]) n++;
        return n > cw_len ? n - cw_len : 0;
    };
    auto accept = [&](bool add_space) {
        bool up = cw_len > 0 && k.input_len > 0 &&
                  k.input[k.input_len - 1] >= 'A' && k.input[k.input_len - 1] <= 'Z';
        if (ghost_len() > 0)
            for (int i = cw_len; selected->text[i] && k.input_len < KB_INPUT_MAX - 1; i++) {
                char c = selected->text[i];
                if (up && c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
                keyboard_type_char(&k, c);
            }
        if (add_space && k.input_len < KB_INPUT_MAX - 1) keyboard_type_char(&k, ' ');
        sug_index = 0;
    };

    bool at_end = (k.cursor == k.input_len);

    bool ins = keyboard_get_insert();
    bool caret_l = ins ? (ke.kind == SATURN_KEY_LEFT)  : (ke.kind == SATURN_KEY_CTRL_LEFT);
    bool caret_r = ins ? (ke.kind == SATURN_KEY_RIGHT) : (ke.kind == SATURN_KEY_CTRL_RIGHT);
    if (caret_l) keyboard_caret_left(&k);
    if (caret_r) keyboard_caret_right(&k);

    bool kb_prev = ins ? (ke.kind == SATURN_KEY_CTRL_LEFT)  : (ke.kind == SATURN_KEY_LEFT);
    bool kb_next = ins ? (ke.kind == SATURN_KEY_CTRL_RIGHT) : (ke.kind == SATURN_KEY_RIGHT);
    bool cyc_prev = (pad && chord_fired(CA_AUTO, -1)) || kb_prev;
    bool cyc_next = (pad && chord_fired(CA_AUTO, +1)) || kb_next;
    if (at_end && ncand > 0 && cyc_prev) sug_index = (sug_index - 1 + ncand) % ncand;
    if (at_end && ncand > 0 && cyc_next) sug_index = (sug_index + 1) % ncand;
    selected = (at_end && ncand > 0) ? cands[sug_index] : nullptr;
    if (ke.kind == SATURN_KEY_LEFT || ke.kind == SATURN_KEY_RIGHT ||
        ke.kind == SATURN_KEY_CTRL_LEFT || ke.kind == SATURN_KEY_CTRL_RIGHT) ke.kind = SATURN_KEY_NONE;

    bool a_press   = pad && g_pad->WasPressed(face_button(FA_ACCEPT));
    bool sp_press  = pad && pad_fired(face_button(FA_SPACE));
    bool has_ghost = selected && ghost_len() > 0;
    if (a_press) {
        if (has_ghost) accept(false);
        else if (k.input_len == 0 || k.input[k.input_len - 1] != ' ') keyboard_submit(&k);
    }
    if (ke.kind == SATURN_KEY_TAB) {
        if (has_ghost) accept(false);
        else if (at_end && k.input_len > 0 && k.input[k.input_len - 1] != ' ')
            keyboard_type_char(&k, ' ');
        ke.kind = SATURN_KEY_NONE;
    }
    if (sp_press) {
        if (has_ghost) accept(true);
        else           keyboard_type_char(&k, ' ');
    }

    bool scrl = keyboard_get_scrolllock();
    bool hist_up   = scrl ? (ke.kind == SATURN_KEY_CTRL_UP)   : (ke.kind == SATURN_KEY_UP);
    bool hist_down = scrl ? (ke.kind == SATURN_KEY_CTRL_DOWN) : (ke.kind == SATURN_KEY_DOWN);

    if      (ke.kind == SATURN_KEY_CHAR)      keyboard_type_char(&k, ke.ch);
    else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
    else if (ke.kind == SATURN_KEY_DELETE)    keyboard_delete_forward(&k);
    else if (ke.kind == SATURN_KEY_ENTER)     keyboard_submit(&k);
    else if (ke.kind == SATURN_KEY_CLEAR)     { k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; }
    else if (hist_up)                         history_recall(&k, 1);
    else if (hist_down)                       history_recall(&k, 0);
    else                                      scroll_handle_key(ke);

    refresh();
    if (k.cursor != k.input_len) selected = nullptr;
    selected_out = selected;
    cw_len_out = cw_len;
}

/*----------------------
 | KB_STRIP_BORDER
 | Description: The in-game keyboard strip's top and bottom border, 39 columns:
 |   the rose module (1..13), the keyboard module (15..37), and the three dividers
 |   at 0, 14 and 38 -- the same left column as the command panel so the two
 |   interfaces line up when the player toggles between them.
 | Author: suinevere
 ----------------------*/
static const char *KB_STRIP_BORDER = "+-------------+-----------------------+";

/*----------------------
 | render_game_keyboard
 | Description: Draws the in-game keyboard as a bordered strip matching the command
 |   panel: the input line above it, then the seven-row compass rose on the left and
 |   the key block on the right, split by a divider on the middle row between the
 |   number and letter rows and closed by the space bar. The focused picker cell is
 |   reverse-video for a key or, when focus has crossed left, the rose's selected
 |   direction; the space bar instead blinks filled/blank on the caret phase, like
 |   the input-line cursor. CapsLock shows on the input row.
 | Author: suinevere
 | Dependencies: command_rose.h, rose_draw.h (cv_draw_rose_row), game_kb.h,
 |   room_model.h, text_map.h
 | Globals: g_kb_on_rose, g_kb_rose_dir, g_difficulty
 | Params: k -- keyboard/input-line state; prediction -- selected completion or
 |   null; current_word_len -- length of the word being completed; block_on -- the
 |   caret blink phase; base -- the input row (first row of the strip block)
 | Returns: N/A
 ----------------------*/
static void render_game_keyboard(const KeyboardState &k, DictionaryWord *prediction,
                                 int current_word_len, bool block_on, int base) {
    int input_row = base;
    int border_top = input_row + 1;
    int content0 = border_top + 1;
    int border_bottom = content0 + CR_ROWS;

    int dash = dash_ready();
    dash_set(DASH_GAMEKB, border_top);

    image_window_box(0, border_top, 40, border_bottom - border_top + 1);
    image_window_on();

    text_clear_line(input_row);
    draw_input_line(input_row, k, prediction, current_word_len, block_on);
    if (keyboard_get_caps()) text_print(35, input_row, "CAPS");

    text_clear_line(border_top);
    text_clear_line(border_bottom);
    if (!dash) {
        text_print(0, border_top, KB_STRIP_BORDER);
        text_print(0, border_bottom, KB_STRIP_BORDER);
    }

    unsigned char flat[RM_DIR_N];
    const unsigned char *exits = kb_exits(flat);
    int sel = g_kb_on_rose ? g_kb_rose_dir : -1;
    int caps = keyboard_get_caps();

    for (int r = 0; r < CR_ROWS; r++) {
        int y = content0 + r;
        text_clear_line(y);
        if (!dash) text_print(0, y, "|");
        cv_draw_rose_row(r, exits, y, sel);
        if (!dash) { text_print(14, y, "|"); text_print(38, y, "|"); }
        if (r == 2) {
            if (!dash) text_print(15, y, "-----------------------");
            continue;
        }
        int gr = kb_grid_row(r);
        if (gr == GKB_SPACE_ROW) {
            bool selected = !g_kb_on_rose && k.cursor_row == GKB_SPACE_ROW;
            /* Steady block when unselected; when the picker is on it, blink the
               bar between filled and blank on the caret phase, the way the input
               line's block cursor does, rather than sit reverse-video. */
            if (!selected || block_on) {
                char bar[15];
                for (int i = 0; i < 14; i++) bar[i] = (char) 0x7f;
                bar[14] = '\0';
                text_print(20, y, bar);
            }
        } else {
            char keys[GKB_COLS * 2];
            int p = 0;
            for (int c = 0; c < GKB_COLS; c++) {
                keys[p++] = game_kb_char_at(gr, c, caps);
                if (c < GKB_COLS - 1) keys[p++] = ' ';
            }
            keys[p] = '\0';
            text_print(17, y, keys);
            if (!g_kb_on_rose && k.cursor_row == gr) {
                char one[2] = { game_kb_char_at(gr, k.cursor_col, caps), '\0' };
                text_print_hl(17 + k.cursor_col * 2, y, one);
            }
        }
    }
}

/*----------------------
 | render_keyboard
 | Description: Installs the solid-block cursor glyph on first use (deferred
 |   here, not at boot, so VDP2/the font are guaranteed up by the first render),
 |   then advances the blink phase and dispatches. When the on-screen keyboard is
 |   hidden (a real keyboard is in hand), the console's last row is already the ">"
 |   prompt, so the input line is drawn over it (clearing that row and the one
 |   below first) and any "more v" marker is repainted. In game the bordered strip
 |   -- rose plus key block -- is drawn by render_game_keyboard. Off the title
 |   screen's online terminal there is no room, so the original KB_ROWS grid is
 |   drawn instead, its picker cell in reverse video.
 | Author: suinevere
 | Dependencies: keyboard.c, input.cxx, SRL
 | Globals: g_kbd_visible, g_more_below
 | Params: k -- current keyboard/input-line state; prediction -- the selected
 |   typeahead completion, or null; current_word_len -- length of the word being
 |   completed
 | Returns: N/A
 ----------------------*/
void render_keyboard(const KeyboardState &k, DictionaryWord* prediction, int current_word_len) {
    static bool glyph_ready = false;
    if (!glyph_ready) { install_block_glyph(); glyph_ready = true; }

    static uint32_t blink = 0;
    bool block_on = ((blink++ / CURSOR_BLINK_FRAMES) & 1) != 0;

    int base = TOP_MARGIN + console_height();

    if (!g_kbd_visible) {
        /* A real keyboard is in hand: no on-screen interface to back, so drop the
           window and draw the input line over the console's ">" prompt row. */
        if (g_in_game) image_window_off();
        int row = base - 1;
        text_clear_line(base);
        text_clear_line(row);
        draw_input_line(row, k, prediction, current_word_len, block_on);
        if (g_more_below) text_print(34, row, "more v");
        return;
    }

    if (g_in_game) { render_game_keyboard(k, prediction, current_word_len, block_on, base); return; }

    /* Off the title screen's online terminal (no room, no rose): the original
       four-row grid, its picker cell in reverse video. */
    int row = base;
    text_clear_line(row);
    draw_input_line(row, k, prediction, current_word_len, block_on);
    for (int r = 0; r < KB_ROWS; r++) {
        char rowbuf[KB_COLS * 2 + 1];
        int p = 0;
        for (int c = 0; c < KB_COLS; c++) {
            rowbuf[p++] = ' ';
            rowbuf[p++] = keyboard_char_at(r, c);
        }
        rowbuf[p] = '\0';
        text_clear_line(row + 1 + r);
        text_print(2, row + 1 + r, "%s", rowbuf);
        if (r == k.cursor_row) {
            char sel[2] = { keyboard_char_at(r, k.cursor_col), '\0' };
            text_print_hl(2 + k.cursor_col * 2 + 1, row + 1 + r, sel);
        }
    }
    if (keyboard_get_caps()) text_print(30, row + 1, "CAPS");
}
