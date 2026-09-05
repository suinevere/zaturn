/*----------------------
 | main_netbin.cxx
 | Description: Entry point for the NETBIN=1 build -- the PlanetWeb 4.0
 |   .netbin variant, which is a pure multizork telnet client. It re-initializes
 |   video (the browser hands over with VDP1/VDP2 in an unknown state), drops
 |   the modem's data session, then loops: dial page, connect, terminal, back to
 |   the dial page. There is no title screen, no game catalogue, no story file
 |   and no CD access anywhere in this build; see
 |   docs/superpowers/specs/2026-07-25-netbin-minimal-design.md.
 |
 |   This file also carries the netbin's reset implementation. engine/
 |   soft_reset.cxx is not linked here -- it is 8.0 KB, it calls into sound.c
 |   and net_connect.c, and its "return to title" has no meaning in a build with
 |   no title. The five symbols online.cxx and netbin_pages.cxx call are
 |   reimplemented against g_netbin_jmp, which lands back on the dial page.
 | Author: suinevere
 | Dependencies: online.h, netbin_pages.h, net_connect.h, console.h,
 |   console_view.h, options.h, display.h, menu.h, input.h, app_state.h,
 |   saturn_keyboard.h, dash_view.h, SRL
 ----------------------*/

#include <srl.hpp>
#include "text_map.h"
#include <setjmp.h>

#include "netbin_pages.h"
#include "online.h"
#include "menu.h"
#include "options.h"
#include "input.h"
#include "controller.h"
#include "console_view.h"
#include "app_state.h"
#include "saturn_keyboard.h"
#include "soft_reset.h"
#include "dash_view.h"

extern "C" {
#include "console.h"
#include "display.h"
#include "net/net_connect.h"
#include "synth.h"
#include "synth_target.h"
}

using namespace SRL::Types;

/*----------------------
 | g_netbin_jmp
 | Description: Reboot landing point, armed once in main() before the dial
 |   loop. check_soft_reset and the "reboot" command longjmp here, which is
 |   this build's whole reset story: there is no title screen to return to, so
 |   a reset means "hang up and go back to the dial page".
 | Author: suinevere
 ----------------------*/
static jmp_buf g_netbin_jmp;
/*----------------------
 | g_netbin_jmp_armed
 | Description: Whether g_netbin_jmp holds a landing site a reset may jump to.
 | Author: suinevere
 ----------------------*/
static bool    g_netbin_jmp_armed = false;

/*----------------------
 | typeahead_malloc / typeahead_free
 | Description: The allocator input/typeahead.c links against. It reaches these
 |   through TYPEAHEAD_MALLOC/FREE (typeahead.h), so the names never appear in
 |   any .c file and the netbin's link edge into the dropped
 |   engine/saturn_glue.cxx hid from every source-level check until the real
 |   link ran. Low Work RAM, matching engine/saturn_glue.cxx's choice for the CD
 |   build: the Zork I trie is 4,722 allocations totalling about 77 KB, which does
 |   not belong in the same High Work RAM heap the image and .bss already share.
 |   LWRAM is otherwise unclaimed in this build.
 | Author: suinevere
 | Dependencies: srl.hpp (SRL::Memory::LowWorkRam)
 | Globals: N/A
 | Params: size -- bytes to allocate / ptr -- allocation to release
 | Returns: the allocation, or NULL / N/A
 ----------------------*/
extern "C" void *typeahead_malloc(unsigned int size) {
    return SRL::Memory::LowWorkRam::Malloc((uint32_t) size);
}

extern "C" void typeahead_free(void *ptr) {
    if (ptr != nullptr) SRL::Memory::LowWorkRam::Free(ptr);
}

/*----------------------
 | line_is / is_reboot_command / is_quit_command
 | Description: Recognizes the typed commands that end a session. Matched
 |   case-insensitively against the whole line with surrounding spaces ignored.
 |   Not the same matching contract engine/soft_reset.cxx documents for the CD
 |   build (no "q" abbreviation, no trailing-punctuation over-matching, exact
 |   whitespace trim vs. any run) -- see soft_reset.h if that divergence ever
 |   needs closing.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: line -- the submitted input line
 | Returns: nonzero on a match
 ----------------------*/
static int line_is(const char *line, const char *word) {
    while (*line == ' ' || *line == '\t') line++;
    int i = 0;
    for (; word[i]; i++) {
        char c = line[i];
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if (c != word[i]) return 0;
    }
    while (line[i] == ' ' || line[i] == '\t') i++;
    return line[i] == '\0';
}

extern "C" int is_reboot_command(const char *line) { return line_is(line, "reboot"); }
extern "C" int is_quit_command(const char *line)   { return line_is(line, "quit"); }

/*----------------------
 | soft_reset_chord_held
 | Description: True while the A+B+C+Start reset chord is held on the pad.
 |   Uses IsHeld deliberately -- this is the one place a level test is correct,
 |   because the chord must be *held*, and all four buttons at once is not a
 |   pattern an absent peripheral's all-held report can be distinguished from
 |   anyway. Callers gate it behind an actual connection.
 | Author: suinevere
 | Dependencies: input.h (g_pad)
 | Globals: g_pad
 | Params: N/A
 | Returns: true while all four are held
 ----------------------*/
extern "C" bool soft_reset_chord_held(void) {
    if (g_pad == nullptr) return false;
    return g_pad->IsHeld(Button::A) && g_pad->IsHeld(Button::B)
        && g_pad->IsHeld(Button::C) && g_pad->IsHeld(Button::START);
}

/*----------------------
 | confirm_return_to_title
 | Description: Asks the player to confirm a reboot and, on yes, hangs up and
 |   longjmps to the dial page. Never returns true -- it either returns false
 |   (declined) or does not return at all.
 | Author: suinevere
 | Dependencies: menu.c (menu_confirm), net_connect.c
 | Globals: g_netbin_jmp, g_netbin_jmp_armed
 | Params: question -- the confirmation prompt
 | Returns: false if the player declined
 ----------------------*/
extern "C" bool confirm_return_to_title(const char *question) {
    if (!menu_confirm("REBOOT", question)) return false;
    net_connect_close();
    if (g_netbin_jmp_armed) longjmp(g_netbin_jmp, 1);
    return false;
}

/*----------------------
 | SOFT_RESET_HOLD
 | Description: Frames the A+B+C+Start chord must be held before it fires. A
 |   debounce, not a feature: it rejects the garbage peripheral read on the
 |   very first frame (before the first vsync has polled real input), which
 |   otherwise reads as "all held" and resets instantly. This matters more
 |   here than in the CD build -- the netbin syncs once in netbin_video_init
 |   and then goes straight into the dial-page loop where check_soft_reset
 |   first runs, instead of running through several frames of splash/title
 |   first. ~0.5s is well below any real four-button hold. Mirrors
 |   engine/soft_reset.cxx's constant of the same name and value.
 | Author: suinevere
 ----------------------*/
static const int SOFT_RESET_HOLD = 30;

/*----------------------
 | LINE_SETTLE_FRAMES
 | Description: Frames held between dropping PlanetWeb's inherited data session
 |   and this build's first dial. net_connect_reset() only gets as far as ATH0
 |   answering OK; the hook is still releasing and the far end has not dropped
 |   its carrier yet, so an ATDT sent straight after it dials over the browser's
 |   own connection tone and trains against it. ~3s at 60Hz, long enough for
 |   both ends to fall quiet and short enough to read as a pause rather than a
 |   hang. Only the boot path waits: it sits above the reboot landing point, so
 |   a reboot -- whose hangup was deliberate and whose line is already idle --
 |   goes straight back to dialing.
 | Author: suinevere
 ----------------------*/
static const int LINE_SETTLE_FRAMES = 600;

/*----------------------
 | check_soft_reset
 | Description: Counts consecutive frames the chord is held (a file-static
 |   counter) and confirms/reboots once it reaches SOFT_RESET_HOLD. Called
 |   from every screen-holding loop in this build, with the same debounce
 |   engine/soft_reset.cxx's version applies before it soft-resets to the
 |   title.
 | Author: suinevere
 | Dependencies: menu.c, net_connect.c
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void check_soft_reset(void) {
    static int hold = 0;
    hold = soft_reset_chord_held() ? (hold + 1) : 0;
    if (hold >= SOFT_RESET_HOLD) {
        // Unlike engine/soft_reset.cxx's accept path (soft_reset_to_title(),
        // which never returns), confirm_return_to_title() here CAN return --
        // this build has no title to jump to, only "hang up and dial page".
        // Without clearing hold, a decline leaves it >= SOFT_RESET_HOLD, and
        // since B (the confirm's "no") is part of the chord, the dialog
        // reopens on the very next frame unless all four buttons are released
        // within a single frame.
        hold = 0;
        confirm_return_to_title("reboot back to the dial page?");
    }
}

/*----------------------
 | netbin_video_init
 | Description: Re-asserts the video mode after PlanetWeb hands over. The
 |   browser has been driving VDP1/VDP2 and leaves them in a state this build
 |   cannot predict, so nothing here may assume SRL's own startup values
 |   survived. Mirrors what SRL::Core::Initialize does for the CD build, then
 |   forces NBG0's window off and paints the configured back colour.
 | Author: suinevere
 | Dependencies: SRL, display.h, options.h
 | Globals: g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void netbin_video_init(void) {
    slScrWindowModeNbg0(0);
    SRL::VDP2::SetBackColor(HighColor(display_bg_rgb(g_display.bg)));
    text_set_color(display_text_rgb(g_display.text), display_bg_rgb(g_display.bg));
    for (int r = 0; r < console_screen_rows(); r++) text_clear_line(r);
    SRL::Core::Synchronize();
}

/*----------------------
 | main
 | Description: The netbin's whole life. Brings up SRL, loads saved settings,
 |   re-asserts video, clears the modem's inherited data session, then loops
 |   forever: dial page -> online_mode() -> dial page. online_mode() reports its
 |   own failures (no modem, no carrier) and returns, so a failed connect simply
 |   lands back on the dial page with the number still in it.
 | Author: suinevere
 | Dependencies: netbin_pages.h, online.h, net_connect.h, console.h, options.h,
 |   display.h, menu.h, input.h, SRL
 | Globals: g_display, g_pad, g_menu_page_fade, g_netbin_jmp, g_netbin_jmp_armed,
 |   g_menu_backing_depth, g_in_game
 | Params: N/A
 | Returns: 0 nominally, but it never actually returns
 ----------------------*/
int main(void) {
    // 320x240, matching the CD build, so the console_view.cxx geometry the two
    // share (SCREEN_ROWS, TOP_MARGIN) means the same thing in both. The netbin
    // shows no wallpaper, so the taller mode costs it nothing and it gains the
    // same two rows the CD build did.
    SRL::Core::Initialize(HighColor::Colors::Black, SRL::TV::Resolutions::Normal320x240);
    border_use_black();
    text_map_init();
    dash_init();        // after text_map_init, as main.cxx:364 does it: VDP2 and
                        // the font are up, and a failure here only means the
                        // renderers keep printing their ASCII borders

    static MultiPad pads;
    g_pad = &pads;
    controller_init();

    display_defaults(&g_display);
    options_load();

    /* Bind and upload only; the music starts at the dial page below, which is
       this build's first real screen. Matching the CD build, where the title's
       jingle owns the title and the synth waits for the menu. */
    synth_target_init();

    netbin_video_init();
    console_init();

    // PlanetWeb downloaded this executable over the NetLink modem, so the line
    // is most likely still off-hook in a live data session. In data mode the
    // modem treats AT as payload and modem_probe() would fail on a perfectly
    // good modem, so escape and hang up before the first dial ever happens.
    net_connect_reset();

    for (int f = 0; f < LINE_SETTLE_FRAMES; f++) {
        menu_message("NETWORK", "Hanging up ...", "");
        menu_sync();
    }

    // Unlike the CD build, this one has no backdrop image for a fade to hide
    // behind, and online_mode() (src/net/online.cxx) has no fade-in to pair
    // with a fade-out -- menu_pages.cxx's page_fade_out/page_fade_in always
    // come in matched pairs, but netbin_dial_page's accept path fades out and
    // then online_mode() never fades back in. A nonzero g_menu_page_fade here
    // would engage colour offset A on NBG0/NBG3 and ramp to black on Dial,
    // then leave the whole online session -- dialing, "No carrier", the
    // telnet terminal -- drawing through that offset with nothing left to
    // clear it. netbin_pages.cxx's page_fade_out/page_fade_in are already
    // guarded (`if (frames > 0) menu_fade_*(frames)`), so 0 here makes both
    // literal no-ops: colour offset A is never touched, not engaged-then-
    // left-black.
    g_menu_page_fade = 0;

    setjmp(g_netbin_jmp);
    g_netbin_jmp_armed = true;
    // The reboot longjmp above lands here skipping the destructor of whatever
    // MenuBacking scope was active on the ancestor stack (netbin_pages.cxx and
    // online.cxx both open one around their check_soft_reset() call), which
    // would otherwise leave g_menu_backing_depth corrupted and the VDP2
    // image-suppressing window stuck on. Mirrors main.cxx's own post-setjmp
    // reset of the same global.
    g_menu_backing_depth = 0;
    // And the chrome that guard was backing, which resetting the count does not
    // take down: NBG2 still holds the box the longjmp jumped out of, and a latch
    // owed by a guard that died first holds it there, since dash_frame_end will
    // not expire a latched layer and nothing below ever claims NBG2 to paint over
    // it. The dial page and the terminal would wear the pause menu's box for the
    // rest of the session. Mirrors main.cxx, which answers for the same thing on
    // its own way back to the title.
    dash_clear();
    // The map sets this while it is showing its own ground and clears it on the
    // way out -- a way out Restart skips, since map_view polls for it. Inert here
    // while g_menu_page_fade stays 0 and no fade reads it, and cleared anyway so
    // that stops being the only thing keeping it harmless.
    menu_back_override(0);
    // Same reasoning for the menu service: online_mode registers its RX pump
    // around the pause menu, and Restart longjmps out from inside it, leaving
    // menu_sync holding a pointer into a frame that no longer exists.
    menu_set_service(nullptr, nullptr);
    g_in_game = false;

    /* Straight onto the wire the first time: with a saved default number there is
       nothing to type, so auto-dial it instead of parking the player on the dial
       page. online_mode() does the actual dialing from g_dialnum. After it returns
       -- hang-up, no carrier, session end -- the dial page opens so the number can
       be changed or redialed. */
    /* Started here rather than at boot, and after the setjmp so a Restart takes
       the loop from the top again. The netbin has no disc, so the fallback rule
       always resolves in the synth's favour -- asked through the same function
       the CD build asks rather than assumed, so the two cannot drift. */
    if (synth_should_play(0)) {
        synth_set_level(g_synth_level);
        synth_start();
    }

    bool auto_dial = valid_dialnum(g_dialnum);
    for (;;) {
        menu_clear();
        if (!auto_dial) netbin_dial_page();
        auto_dial = false;
        g_in_game = true;
        online_mode();
        g_in_game = false;
    }
    return 0;
}
