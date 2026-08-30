/*----------------------
 | console_view.h
 | Description: Text-console and on-screen-keyboard rendering: how many console
 |   rows are available given whether the on-screen keyboard is showing, which
 |   input-method-appropriate hint text to print, tracking which input device (real
 |   keyboard vs gamepad) the player last used, painting the scrollback view and
 |   positioning it on new output, and drawing the on-screen keyboard grid with its
 |   blinking block text cursor. Also runs the shared per-frame input-editing pass
 |   (typeahead_edit) that both the local prompt and the online terminal drive, so
 |   the two behave identically; callers still poll the raw key events and feed
 |   them in.
 | Author: suinevere
 | Dependencies: console.h, keyboard.h, saturn_keyboard.h, typeahead.h
 ----------------------*/

#ifndef CONSOLE_VIEW_H
#define CONSOLE_VIEW_H

#include "console.h"
#include "keyboard.h"
#include "saturn_keyboard.h"
#include "typeahead.h"

/*----------------------
 | g_kbd_visible
 | Description: Whether the on-screen keyboard is drawn, flipped by whichever
 |   input device was last used.
 | Author: suinevere
 ----------------------*/
extern bool g_kbd_visible;

/*----------------------
 | g_output_start
 | Description: The console_total_lines() mark taken just before a turn runs, so
 |   console_scroll_to_output can size that turn even after lines evict.
 | Author: suinevere
 ----------------------*/
extern long g_output_start;

/*----------------------
 | TOP_MARGIN
 | Description: One blank row kept at the top because TV overscan clips the
 |   first text row on real hardware. Console content starts on row 1; menus
 |   already draw from row 1+, so this is the base every renderer adds
 |   console_height() to. Exported so dash_view.cxx's dash_hold can compute the
 |   same base row the renderers use without a second copy of the constant.
 | Author: suinevere
 ----------------------*/
extern const int TOP_MARGIN;

/*----------------------
 | image_window_box / image_window_on / image_window_off
 | Description: VDP2 window 0, which suppresses the NBG0 wallpaper inside a
 |   rectangle so the back-plane colour shows there while NBG3 text still draws
 |   over it. NBG3 treats palette entry 0 as transparent, so without this a box
 |   drawn over a picture shows the picture through its interior. box() aims the
 |   window at a character-cell rectangle, on() switches the suppression in and
 |   off() takes it back out; they are separate because a menu aims the window
 |   every frame it redraws but switches it on once, on the outermost page.
 |
 |   Two callers: menu.c's MenuBacking, which blacks a menu box, and the in-game
 |   interface strip, which blacks the command panel or the on-screen keyboard.
 |   Only one rectangle exists in hardware, so the two must never want it at the
 |   same time -- they do not, because menus are modal and the game's render loop
 |   does not run while one is up.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: box: x0, y0 -- top-left in text cells; w, h -- size in cells
 | Returns: N/A
 ----------------------*/
void image_window_box(int x0, int y0, int w, int h);
void image_window_on(void);
void image_window_off(void);

/*----------------------
 | border_use_black
 | Description: Makes the display border draw black instead of the back-screen
 |   colour. The border is the raster outside the active display -- the columns
 |   either side of the 320 the layers cover, and the lines below the 224 they
 |   are tall -- and by default VDP2 fills it with the back screen, which is the
 |   same register the player's background colour and every menu box's backing
 |   read from. Call once, after Core::Initialize.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void border_use_black(void);

/*----------------------
 | console_height
 | Description: How many console text rows are currently available for
 |   scrollback, given whether the on-screen keyboard is showing (it reserves its
 |   own rows plus a hint row when visible).
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_kbd_visible
 | Params: N/A
 | Returns: the number of rows the console view may draw into
 ----------------------*/
int console_height(void);

/*----------------------
 | console_screen_rows
 | Description: The screen's total text row count -- physical geometry, not the
 |   rows currently free for console content. Unlike console_height(), it does
 |   not move when the on-screen keyboard shows or hides, which is what a
 |   full-screen wipe (a title/menu/splash reset, saturn_die's halt clear) needs
 |   instead: console_height()'s bound is wrong there, since with the keyboard
 |   hidden it reports fewer rows than the screen actually has, leaving rows
 |   above the console unclearable through it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: the screen's total row count (SCREEN_ROWS)
 ----------------------*/
int console_screen_rows(void);

/*----------------------
 | console_strip_shift
 | Description: How far up, in pixels, the NBG0 wallpaper sits while the
 |   gamepad's input strip is on screen. The strip's marble covers its two
 |   borders and CV_STRIP_ROWS of content -- nine rows, 72 lines -- of a
 |   240-line picture, and image_window_box suppresses NBG0 under all of it, so
 |   what is left to see is the 168 lines above. Half the covered height
 |   re-centres the picture in that window: the top 36 lines go off the screen,
 |   the bottom 36 stay behind the strip, and neither end is lopsidedly cropped.
 |
 |   Geometry only: it does not ask whether the strip is up. That question
 |   belongs to whoever also armed the NBG0 window the offset relies on -- see
 |   dash_view.cxx's flush_hook, which pairs this with dash_input_up().
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: the offset in pixels
 ----------------------*/
int console_strip_shift(void);

/*----------------------
 | hint
 | Description: Picks the input-hint string matching the last-used device, so
 |   on-screen text always names the device the player actually has in hand
 |   instead of listing both.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_kbd_visible
 | Params: pad -- hint text for gamepad + on-screen keyboard; kbd -- hint text for
 |   a real keyboard
 | Returns: whichever of pad/kbd matches the last-used device
 ----------------------*/
const char *hint(const char *pad, const char *kbd);

/*----------------------
 | note_input_device
 | Description: Updates which input device is considered active from this
 |   frame's key event: a real key hides the on-screen keyboard, a gamepad press
 |   shows it again. Call once per input frame with that frame's event.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_kbd_visible, g_pad
 | Params: ke -- this frame's keyboard event
 | Returns: N/A
 ----------------------*/
void note_input_device(const SaturnKeyEvent &ke);

/*----------------------
 | render_console
 | Description: Draws the current scrollback window into the console's text
 |   rows, clamping the scroll position and showing "^"/"more v" edge markers
 |   when off-screen text remains above/below.
 | Author: suinevere
 | Dependencies: console.c, SRL
 | Globals: g_scroll, g_more_below
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void render_console(void);

/*----------------------
 | console_scroll_to_output
 | Description: Positions the scrollback on the turn's newly-landed output: if
 |   the turn produced more lines than fit on screen, lands on its top row so the
 |   player reads from the start and pages down via "more v"; otherwise snaps to
 |   the live bottom.
 | Author: suinevere
 | Dependencies: console.c
 | Globals: g_output_start, g_scroll
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void console_scroll_to_output(void);

/*----------------------
 | install_block_glyph
 | Description: Carves a solid-block glyph into the unused DEL (0x7F) font slot
 |   so it can be printed as the blinking input-line text cursor.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void install_block_glyph(void);

/*----------------------
 | render_keyboard
 | Description: Draws the input line with its blinking block cursor, and --
 |   when the on-screen keyboard is showing -- the keyboard grid, its cursor
 |   marker, the CapsLock indicator, and the face-button legend below it.
 | Author: suinevere
 | Dependencies: keyboard.c, input.cxx, SRL
 | Globals: g_kbd_visible, g_more_below
 | Params: k -- current keyboard/input-line state; prediction -- the selected
 |   typeahead completion, or null; current_word_len -- length of the word being
 |   completed
 | Returns: N/A
 ----------------------*/
void render_keyboard(const KeyboardState &k, DictionaryWord* prediction, int current_word_len);

/*----------------------
 | typeahead_edit
 | Description: Runs one frame of on-screen input editing with typeahead, so the
 |   local game prompt and the online terminal behave identically. Handles both
 |   the gamepad (with auto-repeat) and a real keyboard: moves the picker,
 |   types/deletes, moves the text caret, cycles suggestions, accepts a
 |   completion (with or without a trailing space), and recalls history --
 |   ScrollLock choosing whether plain or Ctrl Up/Down recalls, with the other
 |   pair scrolling the console one line. May set
 |   k.submitted. sug_index/sug_last carry the suggestion-cycle position across
 |   frames. Reports back the selected suggestion and the length of the word being
 |   completed so the caller can render the ghost. The caller must poll ke/pad and
 |   tick the input-repeat helpers before calling.
 | Author: suinevere
 | Dependencies: keyboard.c, input.cxx, typeahead.c
 | Globals: g_pad
 | Params: k -- keyboard/input-line state, edited in place; root -- the typeahead
 |   trie; sug_index -- suggestion-cycle index (in/out); sug_last -- the word the
 |   cycle index belongs to (in/out); ke -- this frame's decoded key event,
 |   consumed as it is handled; pad -- true when the gamepad is the active device;
 |   selected_out -- receives the chosen suggestion or null; cw_len_out -- receives
 |   the current word length
 | Returns: N/A
 ----------------------*/
void typeahead_edit(KeyboardState &k, TrieNode *root,
                    int &sug_index, char *sug_last,
                    SaturnKeyEvent &ke, bool pad,
                    DictionaryWord *&selected_out, int &cw_len_out);

#endif /* CONSOLE_VIEW_H */
