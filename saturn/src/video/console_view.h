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
