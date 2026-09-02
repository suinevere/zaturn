/*----------------------
 | soft_reset.cxx
 | Description: The in-process soft reset -- the Sega-mandated A+B+C+Start chord
 |   and the typed "reboot"/"quit" commands that return the player to the title
 |   screen via longjmp, without an SMPC/hardware reset. Owns the reboot/quit
 |   command recognizers, the chord poller, the modal confirm, and the longjmp
 |   itself. main.cxx arms the setjmp target (g_title_jmp, in app_state) just
 |   before the title screen.
 | Author: suinevere
 | Dependencies: soft_reset.h, app_state.h (g_title_jmp), net/net_connect.h
 |   (drops the modem link), sound.h (stops audio), input.h (g_pad), menu.h +
 |   menu_layout.h (the confirm box), console_view.h (hint/device),
 |   saturn_keyboard.h (key events), SRL/SGL.
 ----------------------*/

#include <srl.hpp>
#include "text_map.h"

#include "soft_reset.h"
#include "menu.h"
#include "console_view.h"
#include "input.h"
extern "C" {
#include "menu_layout.h"
#include "saturn_keyboard.h"
#include "sound.h"
#include "music.h"
#include "saturn_glue.h"
#include "net/net_connect.h"
}
#include "online.h"
#include "game_catalog.h"
#include "app_state.h"

using namespace SRL::Types;

/*----------------------
 | is_reboot_command
 | Description: Case-insensitive exact match against "reboot", one char at a time
 |   so no <string.h> is pulled in.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: line -- the raw input line to test
 | Returns: non-zero when the line is exactly the reboot command
 ----------------------*/
int is_reboot_command(const char *line) {
    static const char cmd[] = "reboot";
    int i;
    for (i = 0; cmd[i]; i++) {
        char c = line[i];
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if (c != cmd[i]) return 0;
    }
    return line[i] == '\0';
}

/*----------------------
 | is_alnum_ch
 | Description: True for ASCII letters and digits -- the word-character test the
 |   quit recognizer breaks tokens on.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: c -- byte to classify
 | Returns: non-zero for [A-Za-z0-9]
 ----------------------*/
static int is_alnum_ch(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/*----------------------
 | is_quit_command
 | Description: Matches "q" or "quit" as the first word of `line`, a safety
 |   boundary that intercepts the story's QUIT before the interpreter's own quit
 |   opcode (which crashes on Saturn) can run. It errs toward over-matching: a
 |   Z-machine tokenizer breaks on its declared separators (Zork I: , . ") with or
 |   without a space, so "quit." reads as QUIT to the game -- splitting on spaces
 |   alone would let it through. Any non-alphanumeric char therefore ends the
 |   word and the rest is ignored ("quit now" is caught too); the worst case for
 |   over-matching is a confirm prompt the player declines.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: line -- the raw input line to test
 | Returns: non-zero when the first word is q/quit
 ----------------------*/
int is_quit_command(const char *line) {
    while (*line != '\0' && !is_alnum_ch(*line)) line++;
    char w[6];
    int n = 0;
    for (; is_alnum_ch(line[n]) && n < 5; n++) {
        char c = line[n];
        w[n] = (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
    }
    w[n] = '\0';
    return w[0] == 'q' && (w[1] == '\0' ||
           (w[1] == 'u' && w[2] == 'i' && w[3] == 't' && w[4] == '\0'));
}

/*----------------------
 | soft_reset_to_title
 | Description: Ramps the screen down, drops any live modem link, stops all audio,
 |   lifts any music hold, then longjmps back to the title screen armed in main --
 |   an in-process restart, so the console never re-reads the CD. If the jump
 |   target is not armed yet (impossible once main is running), falls back to the
 |   SMPC reset-button NMI and spins. Never returns.
 |
 |   The ramp is the one thing every way back to the title shares -- the typed q
 |   and quit, the Options page's Title Screen row, the reset chord, and a story
 |   that ran to its end -- so it belongs here rather than at four call sites. It
 |   runs at g_menu_page_fade, the length every other menu transition uses, and is
 |   inert at 0 (the netbin, and any build that wants the old instant cut). The
 |   far side is already a ramp: main's re-entry blacks the screen and the splash
 |   logo fades itself up out of it, so what this adds is the missing half.
 |
 |   Nothing is drawn between the ramp and the jump, so the ramp's own hold is
 |   what the black is made of; main's re-entry re-arms it with title_bg_fade_arm
 |   before anything is composed on top.
 | Author: suinevere
 | Dependencies: menu.h (menu_fade_out, g_menu_page_fade), net/net_connect.h,
 |   sound.h, music.h, SGL (slNMIRequest)
 | Globals: g_title_jmp, g_title_jmp_armed, g_menu_page_fade
 | Params: N/A
 | Returns: N/A (does not return)
 ----------------------*/
void soft_reset_to_title(void) {
    if (g_menu_page_fade > 0) menu_fade_out(g_menu_page_fade);
    net_connect_close();
    sound_stop_all();
    // Any music hold belongs to the menu that took it, and that menu is gone. It
    // survives the longjmp otherwise: the title comes back with the CD-DA still
    // at the ducked level, and the next loading screen inherits a latch that
    // makes its own hold look redundant.
    music_resume();
    // Both typeahead tries: between them they hold most of Low Work RAM, and they
    // belong to a session that has ended. The next game builds its own at game
    // start; the online one is rebuilt only if the player dials again.
    saturn_typeahead_release();
    online_typeahead_release();
    // And the story image, after the typeahead that indexes it: it is the one
    // big HWRAM claimant, and the ~194 KB C heap cannot hold two of them.
    // initStory would free it, but not until the next mojo_boot, by which point
    // main has already allocated the incoming one -- so releasing it here is
    // what makes a game switch possible at all, and what leaves the title
    // screen's wallpaper decode the 82 KB it refuses to work without.
    mojo_release();
    // And the catalogue, so the next load re-captures /Z3 from the root rather
    // than trusting a record from before the game that just ended.
    game_catalog_invalidate();
    if (g_title_jmp_armed) longjmp(g_title_jmp, 1);
    slNMIRequest();
    while (1) {}
}

/*----------------------
 | SOFT_RESET_HOLD
 | Description: Frames the A+B+C+Start chord must be held before it fires. A
 |   debounce, not a feature: it rejects the garbage peripheral read on the very
 |   first frame (before the first vsync has polled real input), which otherwise
 |   reads as "all held" and resets instantly. ~0.5s is well below any real
 |   four-button hold.
 | Author: suinevere
 ----------------------*/
static const int SOFT_RESET_HOLD = 30;

/*----------------------
 | soft_reset_chord_held
 | Description: True when A, B, C and Start are all physically held this frame.
 | Author: suinevere
 | Dependencies: input.h (g_pad)
 | Globals: g_pad
 | Params: N/A
 | Returns: true while the reset chord is held
 ----------------------*/
bool soft_reset_chord_held(void) {
    return g_pad->IsHeld(Button::A) && g_pad->IsHeld(Button::B) &&
           g_pad->IsHeld(Button::C) && g_pad->IsHeld(Button::START);
}

/*----------------------
 | check_soft_reset
 | Description: Counts consecutive frames the chord is held (a file-static
 |   counter) and soft-resets to the title once it reaches SOFT_RESET_HOLD. Call
 |   once per frame from every input loop; never returns once the threshold is
 |   crossed.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void check_soft_reset(void) {
    static int hold = 0;
    hold = soft_reset_chord_held() ? (hold + 1) : 0;
    if (hold >= SOFT_RESET_HOLD) soft_reset_to_title();
}

/*----------------------
 | confirm_return_to_title
 | Description: Clears the screen to the wallpaper, draws a modal Y/N box on it,
 |   and loops until the player answers. The game text and the input strip both go
 |   down for as long as the question is up: this is the one confirm that offers to
 |   end the session, and leaving the turn's words and a live-looking control
 |   surface behind it read as though the game were still waiting to be typed at.
 |   The box width is budgeted unconditionally for the widest row, the pad hint
 |   "(A) (C) (Start) = yes" (21 cols), so it does not resize when the player
 |   switches between pad and keyboard mid-prompt. Yes soft-resets to the title
 |   (never returns); No returns false so the caller resumes the game.
 |
 |   menu_sync's own dash_hold_any is the whole of the NBG2 bookkeeping here. This
 |   loop used to call dash_hold as well, to keep the gamepad strip up behind the
 |   box -- but NBG2 carries one variant at a time, and dash_hold paints the strip,
 |   so it was overwriting the marble menu_frame had just drawn on every single
 |   frame. The box was never marbled, which is the exact failure dash_hold_any's
 |   own box warns about; holding whatever is painted, and painting nothing, is
 |   what leaves the marble where menu_frame put it.
 | Author: suinevere
 | Dependencies: menu.h, menu_layout.h, console_view.h, saturn_keyboard.h,
 |   input.h, SRL
 | Globals: g_pad, g_kbd_visible
 | Params: question -- the yes/no question shown in the box
 | Returns: false when the player declines (Yes does not return)
 ----------------------*/
bool confirm_return_to_title(const char *question) {
    MenuBacking backing;
    int qlen = 0;
    while (question[qlen]) qlen++;

    int content_w = qlen;
    if (content_w < 21) content_w = 21;
    int x0, y0, w, h;
    menu_box_fit("RETURN TO TITLE", content_w, 5, &x0, &y0, &w, &h);

    // Before the debounce frame, not after it: the text shadow reaches VRAM on
    // OnAfterSync, so clearing after this Synchronize would show the turn's words
    // for the one frame the box is not drawn on yet.
    menu_clear();
    SRL::Core::Synchronize();
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        bool yes = (ke.kind == SATURN_KEY_CHAR && (ke.ch == 'y' || ke.ch == 'Y' || ke.ch == '1'))
                 || ke.kind == SATURN_KEY_ENTER
                 || g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::START)
                 || g_pad->WasPressed(Button::C);
        bool no  = (ke.kind == SATURN_KEY_CHAR && (ke.ch == 'n' || ke.ch == 'N' || ke.ch == '2'))
                 || ke.kind == SATURN_KEY_ESCAPE || g_pad->WasPressed(Button::B);
        if (yes) { soft_reset_to_title(); }
        if (no) { return false; }

        menu_clear();
        menu_frame(x0, y0, w, h, "RETURN TO TITLE");
        int cx = x0 + 2, cy = y0 + 3;
        text_print(cx, cy, "%s", question);
        if (!g_kbd_visible) text_print(cx, cy + 2, "1) Yes    2) No");
        text_print(cx, cy + 3, "%s", hint("(A) (C) (Start) = yes", "Y / Enter = yes"));
        text_print(cx, cy + 4, "%s", hint("(B) = no", "N / Esc = no"));
        menu_sync();
    }
}
