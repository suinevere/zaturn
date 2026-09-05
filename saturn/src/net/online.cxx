/*----------------------
 | online.cxx
 | Description: Network play. Dials the NetLink modem to the multizork server,
 |   then runs a telnet terminal that reuses the local game's console, on-screen
 |   keyboard and typeahead. The typeahead trie is built from the disc's ZORK1.Z3
 |   once the carrier is up, never on the way to the dial, so its CD reads cannot
 |   silence the menu music behind a dial that never connects.
 | Author: suinevere
 | Dependencies: online.h, net/net_connect.h (dialing + transport), term.h
 |   (telnet terminal), console.h/console_view.h (screen + on-screen keyboard),
 |   keyboard.h (KeyboardState), saturn_keyboard.h (key events), input.h (g_pad,
 |   pad repeat/scroll, history), typeahead.h + typeahead_extract.h +
 |   typeahead_solution.h (the trie), menu.h (dialing boxes), soft_reset.h
 |   (reboot command + confirm), music.h (menu-track playback), app_state.h
 |   (g_difficulty/g_dialnum/g_scroll), game_catalog.h (Z3 scan), SRL.
 ----------------------*/

#include <srl.hpp>
#include "text_map.h"

#include "online.h"
#include "menu.h"
#include "console_view.h"
#include "input.h"
#include "controller.h"
#include "soft_reset.h"
#include "game_catalog.h"
#ifdef NETBIN
#include "command_view.h"
#include "map_view.h"
#include "netbin_pages.h"
#endif
extern "C" {
#include "room_model.h"
#include "map_model.h"
#include "map_atlas.h"
#include "map_marks.h"
}
extern "C" {
#include "console.h"
#include "keyboard.h"
#include "saturn_keyboard.h"
#include "term.h"
#include "party.h"
#include "net/net_connect.h"
#include "typeahead.h"
#include "typeahead_extract.h"
#include "typeahead_solution.h"
#include "netbin_story.h"
#include "music.h"
}
#include "app_state.h"

using namespace SRL::Types;

/*----------------------
 | snprintf
 | Description: Declared here because it links from newlib but the SRL dummy
 |   <stdio.h> omits it.
 | Author: suinevere
 | Dependencies: newlib
 | Globals: N/A
 | Params: as the C standard
 | Returns: bytes that would have been written
 ----------------------*/
extern "C" int snprintf(char *str, size_t size, const char *fmt, ...);

#ifdef NETBIN
/*----------------------
 | PauseSvc / pause_service
 | Description: The RX pump menu_sync runs on the netbin's behalf while the
 |   pause menu and anything under it holds the screen. Nothing about that menu
 |   pauses the game -- it is a telnet session, the server plays on, and
 |   transport_uart.c reads the 16550's FIFO with no software ring behind it, so
 |   a page that stops calling term_service loses output after a dozen or so
 |   bytes. Draining into the console rather than the wire is enough: the
 |   scrollback the player returns to is the whole point.
 | Author: suinevere
 | Dependencies: term.c
 | Globals: N/A
 | Params: ctx -- the PauseSvc holding the live TermState and transport
 | Returns: N/A
 ----------------------*/
struct PauseSvc { TermState *ts; const cui_transport_t *tr; };

static void pause_service(void *ctx) {
    PauseSvc *s = (PauseSvc *) ctx;
    term_service(s->ts, s->tr, ZATURN_RX_BUDGET);
}
#endif

/*----------------------
 | ONLINE_DIAL_ATTEMPTS
 | Description: Auto-redial count, because modem carrier training is flaky.
 | Author: suinevere
 ----------------------*/
#define ONLINE_DIAL_ATTEMPTS 3

/*----------------------
 | ONLINE_SETTLE_FRAMES
 | Description: Quiet frames that end a server response. The local game knows a
 |   turn's output is complete because it ran the turn; over the modem it only
 |   stops arriving, so the view waits for a gap before anchoring. ~0.2s, well
 |   past the ~16 bytes a 9600-baud line delivers per frame mid-response.
 | Author: suinevere
 ----------------------*/
#define ONLINE_SETTLE_FRAMES 12

/*----------------------
 | online_cancel_requested
 | Description: Reports the abort gesture -- Esc on the Saturn keyboard, or the
 |   L+R trigger chord on the gamepad (both triggers are unused for typing).
 | Author: suinevere
 | Dependencies: saturn_keyboard.h, input.h (g_pad)
 | Globals: g_pad
 | Params: N/A
 | Returns: true if the player asked to abort this frame
 ----------------------*/
static bool online_cancel_requested(void) {
    if (saturn_keyboard_poll().kind == SATURN_KEY_ESCAPE) return true;
    return g_pad->IsHeld(Button::L) && g_pad->IsHeld(Button::R);
}

/*----------------------
 | online_wait_any
 | Description: Blocks until any face button, Start, or a keyboard key is seen.
 |   Used to hold a terminal error screen until the player acknowledges it.
 | Author: suinevere
 | Dependencies: input.h (g_pad), saturn_keyboard.h, SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void online_wait_any(void) {
    for (;;) {
        if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
            g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) return;
        if (saturn_keyboard_poll().kind != SATURN_KEY_NONE) return;
        menu_sync();
    }
}

/*----------------------
 | online_settle_input
 | Description: Waits for input to release AND stay quiet before the terminal
 |   starts reading it, so nothing spurious is submitted on connect. Powering the
 |   NetLink modem over the SMPC reboots the controllers/keyboard (they re-init
 |   from EEPROM), and for a short window their peripheral reports can be stale or
 |   garbage that decodes into phantom keypresses. Requires a sustained
 |   fully-idle streak -- covering the raw held state AND the decoded key event
 |   the loop consumes -- after a minimum settle, draining the keyboard decoder
 |   every frame so no stale repeat leaks through. Caps at ~5s so it never hangs.
 | Author: suinevere
 | Dependencies: saturn_keyboard.h, input.h (g_pad), SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void online_settle_input(void) {
    const int MIN_FRAMES  = 45;
    const int IDLE_NEEDED = 10;
    const int MAX_FRAMES  = 300;
    int frames = 0, idle = 0;
    while ((frames < MIN_FRAMES || idle < IDLE_NEEDED) && frames < MAX_FRAMES) {
        SRL::Core::Synchronize();
        frames++;
        bool busy =
            saturn_keyboard_poll().kind != SATURN_KEY_NONE
            || saturn_keyboard_any_down() != 0
            || g_pad->IsHeld(Button::A) || g_pad->IsHeld(Button::B)
            || g_pad->IsHeld(Button::C) || g_pad->IsHeld(Button::X)
            || g_pad->IsHeld(Button::START) || g_pad->IsHeld(Button::Up)
            || g_pad->IsHeld(Button::Down) || g_pad->IsHeld(Button::Left)
            || g_pad->IsHeld(Button::Right);
        idle = busy ? 0 : (idle + 1);
    }
}

/*----------------------
 | g_online_ta / g_online_diff
 | Description: The online terminal's typeahead trie and the difficulty it was
 |   built for. Both survive the soft-reset longjmp, which is why a return to
 |   title makes the boot-time rebuild a no-op.
 | Author: suinevere
 ----------------------*/
static TrieNode* g_online_ta = nullptr;
static int g_online_diff = -1;

/*----------------------
 | online_typeahead_release
 | Description: Frees the online trie so the next ensure_online_typeahead rebuilds
 |   it from the disc. For the soft reset, and it is not a saving -- it is what
 |   makes the return to title identical to a cold boot.
 |
 |   This trie is 625-700 KB, not the ~318 KB the budget comments around this
 |   codebase have long claimed (measured from the free-space readout on the title
 |   screen: LWRAM less what the art preload could still take). At that size it is
 |   the largest single object in the megabyte by a wide margin, and the order it is
 |   built in relative to SPLASH.PCM decides whether the jingle exists at all.
 |
 |   Cold boot gets that order right by accident: boot_music_load runs at the top of
 |   splash_show and takes its 453 KB out of an empty zone, and this trie is built
 |   afterwards, around it. A soft-reset return used to arrive with the trie already
 |   standing, leaving ~423 KB -- thirty-nine short -- so the Malloc returned null,
 |   and a null there is silent: no jingle, and no loading cue on the next game
 |   either, for the same reason. Releasing it here restores the cold-boot order
 |   exactly rather than trying to make a second order work.
 |
 |   Costs one ZORK1.Z3 read per return, which the splash logo is there to cover.
 | Author: suinevere
 | Dependencies: typeahead.h
 | Globals: g_online_ta, g_online_diff
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void online_typeahead_release(void) {
    if (g_online_ta) { destroy_typeahead(g_online_ta); g_online_ta = nullptr; }
    g_online_diff = -1;
}

/*----------------------
 | ensure_online_typeahead
 | Description: Rebuilds g_online_ta from ZORK1.Z3 whenever it is missing or the
 |   difficulty changed; frees the story bytes afterward since the trie is
 |   self-contained. Hard difficulty leaves an empty trie (typeahead off). The
 |   retry loop works around GFS_GetFileSize returning an uninitialized size on
 |   first access. It starts no music of its own: on the boot path the menu track
 |   has not started, and kicking it off mid-preload would only have the next
 |   retry's read silence it again -- callers re-assert playback once the reads
 |   are done. Under NETBIN the rebuild instead runs against the story embedded
 |   in the netbin's .rodata, since there is no CD to read one from.
 | Author: suinevere
 | Dependencies: game_catalog.h (scan_z3_folder, CD-build only), typeahead.h,
 |   typeahead_extract.h, typeahead_solution.h, netbin_story.h, SRL
 | Globals: g_online_ta, g_online_diff, g_difficulty
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void ensure_online_typeahead(void) {
    if (g_online_ta != nullptr && g_online_diff == g_difficulty) return;
    if (g_online_ta) { destroy_typeahead(g_online_ta); g_online_ta = nullptr; }
    g_online_ta = create_trie_node();
    g_online_diff = g_difficulty;
    if (g_difficulty == DIFF_HARD) return;
#ifdef NETBIN
    // The story is a .rodata blob, and both builders take a const pointer, so it
    // is read in place -- no allocation, and nothing to free afterward.
    build_typeahead_from_story(g_online_ta, netbin_story_data(), netbin_story_size());
    int have_solution = apply_solution_overlay(g_online_ta, netbin_story_data(),
                                               netbin_story_size());
    typeahead_add_abbreviations(g_online_ta);
    // Easy restricts context suggestions to the winning path, and it is a
    // no-op unless this is called: typeahead.c holds the mode in file statics
    // that start at zero, so a netbin that skipped this ranked as Normal
    // whatever Options said. The overlay above was applied either way, so the
    // links were there and unused. DIFF_HARD already returned above.
    typeahead_set_easy(g_difficulty == DIFF_EASY, have_solution);
    return;
#else
    char names[1][16];
    if (scan_z3_folder(names, 1) < 0) return;
    uint8_t* story = nullptr; uint32_t len = 0;
    for (int attempt = 0; attempt < 40 && story == nullptr; attempt++) {
        SRL::Cd::File f("ZORK1.Z3");
        int32_t bytes = f.Size.Bytes, ssz = f.Size.SectorSize;
        if (ssz == 2048 && bytes > 0 && bytes <= 0x40000) {
            uint8_t* buf = (uint8_t*) SRL::Memory::HighWorkRam::Malloc((uint32_t) bytes);
            if (buf != nullptr && f.Open()) {
                int32_t got = f.Read(bytes, buf); f.Close();
                if (got == bytes) { story = buf; len = (uint32_t) bytes; break; }
            }
            if (buf != nullptr) SRL::Memory::HighWorkRam::Free(buf);
        }
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    if (story != nullptr) {
        build_typeahead_from_story(g_online_ta, story, len);

        int have_solution = (g_difficulty != DIFF_HARD)
                          ? apply_solution_overlay(g_online_ta, story, len) : 0;

        typeahead_add_abbreviations(g_online_ta);

        typeahead_set_easy(g_difficulty == DIFF_EASY, have_solution);
        SRL::Memory::HighWorkRam::Free(story);
    }
#endif
}

#ifdef NETBIN
/*----------------------
 | netbin_room
 | Description: The room snapshot the command panel draws. Once multizorkd has
 |   named a room this is the real decode; before that it is a stand-in with all
 |   twelve directions open, because room_model reports every exit as NONE
 |   between being bound and being refreshed, and a rose showing a room with no
 |   way out is a worse lie than one offering directions the server will refuse.
 |
 |   Contents and inventory stay empty in both: room_model is in exits-only mode
 |   here, and this build has no business claiming either.
 | Author: suinevere
 | Dependencies: room_model.h
 | Globals: N/A
 | Params: N/A
 | Returns: the snapshot to draw and edit against
 ----------------------*/
static const RoomModel *netbin_room(void) {
    static RoomModel all_open;
    static bool built = false;
    if (room_model_has_room()) return room_model_get();
    if (!built) {
        for (int i = 0; i < RM_DIR_N; i++) {
            all_open.exits[i] = RM_EXIT_OPEN;
            all_open.dest[i]  = 0;
        }
        all_open.room = 0;
        all_open.nhere = 0;
        all_open.ncarried = 0;
        built = true;
    }
    return &all_open;
}
#endif

/*----------------------
 | online_mode
 | Description: Dials first and builds the typeahead only once the carrier is up,
 |   so a dial that fails or is cancelled costs the menu track nothing; the track
 |   is re-asserted after that build, which is the one CD read on this path.
 |   Dials inside a MenuBacking-scoped image-suppressing window that covers the
 |   whole redial sequence and is dropped before the terminal takes the screen.
 |   The terminal loop services RX into the console, rescans on-screen words for
 |   the typeahead only when output grows, honors the global reboot command and
 |   the soft-reset chord, and disconnects on Esc or a deliberate ~0.75s L+R hold
 |   (L and R alone page the scrollback, so a brief chord must not drop the link).
 |
 |   Long responses anchor on their first line behind a "more v" marker rather than
 |   running on to the newest byte, matching the local game. The local prompt calls
 |   console_scroll_to_output directly because it ran the turn and knows the output
 |   is complete; here a response only stops arriving, so the anchor waits for
 |   ONLINE_SETTLE_FRAMES of silence. It fires once per command sent, so a message
 |   from another player never moves the page out from under a reader.
 | Author: suinevere
 | Dependencies: net/net_connect.h, term.h, console.h, console_view.h, input.h,
 |   keyboard.h, saturn_keyboard.h, typeahead.h, menu.h, soft_reset.h, music.h,
 |   SRL
 | Globals: g_online_ta, g_dialnum, g_scroll, g_output_start, g_pad,
 |   g_kbd_visible
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void online_mode(void) {
    const char *number = g_dialnum;

    {
    MenuBacking backing;
    net_connect_result_t rc = NET_DIAL_FAIL;
    for (int attempt = 1; attempt <= ONLINE_DIAL_ATTEMPTS; attempt++) {
        {
            char dial[40];
            snprintf(dial, sizeof(dial), "Dialing %s ... (attempt %d/%d)",
                     number, attempt, ONLINE_DIAL_ATTEMPTS);
            menu_message("ONLINE", dial,
                         hint("L+R = cancel", "Esc = cancel"));
            // menu_sync, not a bare Synchronize: menu_message draws the box
            // once and this is the frame it is shown on, so the frame has to
            // keep claiming NBG2 or the border blinks out under the text.
            menu_sync();
        }

        rc = net_connect_open(number);
        if (rc == NET_OK) break;
        if (rc == NET_NO_MODEM) break;

        if (attempt < ONLINE_DIAL_ATTEMPTS) {
            menu_message("ONLINE", "No carrier. Retrying...",
                         hint("L+R = cancel", "Esc = cancel"));
            bool cancelled = false;
            for (int f = 0; f < 180; f++) {
                if (online_cancel_requested()) { cancelled = true; break; }
                menu_message("ONLINE", "No carrier. Retrying...",
                             hint("L+R = cancel", "Esc = cancel"));
                menu_sync();
            }
            if (cancelled) { net_connect_close(); return; }
        }
    }

    if (rc != NET_OK) {
        menu_message("ONLINE",
            rc == NET_NO_MODEM ? "NetLink modem not found." : "Connection failed.",
            "(press any button)");
        online_wait_any();
        return;
    }
    }

    // Built here and not at the splash or ahead of the dial, because it is the
    // largest single thing in Low Work RAM -- more than the boot jingle -- and its
    // CD reads silence the menu track. Held back until the carrier is up, a player
    // who never dials never pays for either.
    ensure_online_typeahead();
#ifndef NETBIN
    // Re-assert through the engine, so the track the read just killed comes back as
    // one the cycle rule is counting rather than an endless loop.
    music_refresh();
#endif

    menu_clear();

    const cui_transport_t *tr = net_connect_transport();
    TermState ts; term_init(&ts);
    KeyboardState k; keyboard_reset(&k);
    console_init();
    online_settle_input();
    keyboard_reset(&k);

    g_scroll = 0;
    g_output_start = console_total_lines();
    term_mark_output(&ts);

    const int LR_DISCONNECT_HOLD = 45;
    int lr_hold = 0;
    int sug_index = 0;
    char sug_last[256] = "";
    int last_scan_lines = -1;
#ifdef NETBIN
    CommandPanel cpanel; cp_init(&cpanel);
    g_cmd_mode = g_cmd_iface;
    mode_toggle_reset();
    /* The rose can show this game's real exits, but only the exits: the story
       is the one in .rodata and the game is on the server, so its room contents
       are whatever Zork shipped with and its inventory is a guess. Ask for the
       ids, bind the decoder to the embedded image, and tell it to stop at the
       doorways. Until the first id lands, room_model_has_room() is false and
       both roses keep offering all twelve directions. */
    term_request_room_id(tr);
    room_model_bind(netbin_story_data(), netbin_story_size());
    room_model_set_exits_only(1);
    /* The same embedded image the rose decodes is release 88 serial 840726, so
       the authored Zork I table binds off it and the map can place rooms where
       Infocom drew them rather than where a graph walk guesses.

       Reset per dial, deliberately. The model's state is file scope and would
       otherwise outlive this function, carrying the last session's rooms into
       the next one -- and multizorkd hands out a fresh instance as readily as it
       reconnects you to your old one (see reconnect_player), so a map that
       persisted across dials would sometimes be the right game's and sometimes
       be another's, with nothing here able to tell which. A map that starts
       empty every session is always honest about what it is showing. */
    map_atlas_bind(netbin_story_data(), netbin_story_size());
    map_marks_bind(netbin_story_data(), netbin_story_size());
    map_model_reset();
    /* And the roster with it, for the same reason: the seats belong to one
       instance, and the next dial may be a different one. */
    party_reset();
#endif
    /* The command module's menu and swap rows, held across one frame. Both are
       set where the panel's action is spent, which is below the two blocks that
       act on them, so each is read on the frame after the row was picked. */
    bool panel_menu = false;
    bool panel_swap = false;
    for (;;) {
        term_service(&ts, tr, ZATURN_RX_BUDGET);
        if (term_output_settled(&ts, ONLINE_SETTLE_FRAMES)) console_scroll_to_output();

        int lc = console_line_count();
        if (lc != last_scan_lines) {
            last_scan_lines = lc;
            char scr[1024]; int sp = 0;
            int rows = console_height();
            int startln = (lc > rows) ? (lc - rows) : 0;
            for (int li = startln; li < lc && sp < (int) sizeof(scr) - 1; li++) {
                const char* ln = console_get_line(li);
                for (int j = 0; ln[j] && sp < (int) sizeof(scr) - 1; j++) scr[sp++] = ln[j];
                if (sp < (int) sizeof(scr) - 1) scr[sp++] = ' ';
            }
            scr[sp] = '\0';
            typeahead_set_screen(g_online_ta, scr);
        }

        check_soft_reset();
#ifdef NETBIN
        if (ts.room_id_fresh) {
            room_model_refresh_room((unsigned short) ts.room_id);
            /* The map is fed from the server's id rather than from the screen,
               which is the whole reason it can exist in this build: no
               interpreter runs here, but multizorkd names the room out of band
               and the embedded story knows what that object connects to. */
            if (room_model_has_room()) map_model_enter(room_model_get());
            ts.room_id_fresh = 0;
        }
#endif
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind != SATURN_KEY_NONE) g_kbd_visible = false;
        bool pad = (ke.kind == SATURN_KEY_NONE);
        if (pad && g_pad->AnyPressed()) g_kbd_visible = true;
        pad_repeat_update();
        chord_tick();
        controller_tick();
        controller_feed_key(ke);

        bool lr = g_pad->IsHeld(Button::L) && g_pad->IsHeld(Button::R);
        lr_hold = lr ? (lr_hold + 1) : 0;
        if (ke.kind == SATURN_KEY_ESCAPE || lr_hold >= LR_DISCONNECT_HOLD) {
            console_write("\n*** disconnected ***\n", 22);
            render_console();
            SRL::Core::Synchronize();
            break;
        }

#ifdef NETBIN
        /* Start opens the pause menu. It is free here: Esc and a held L+R are
           disconnect, L/R alone cycles suggestions, and the interface toggle is
           a Y/Z-class tap (g_toggle_btn), so nothing else in this loop claims
           it. The menu runs its own poll loop, so register the RX pump for as
           long as it owns the screen and clear it before touching the wire
           again -- Restart never comes back, and main()'s landing clears it for
           that path. */
        if (panel_menu || g_pad->WasPressed(Button::START)) {
            panel_menu = false;
            int verb_was = g_verbosity;
            PauseSvc svc = { &ts, tr };
            menu_set_service(pause_service, &svc);
            netbin_pause_menu();
            menu_set_service(nullptr, nullptr);
            /* The toggle button can be pressed and released entirely while the
               menu owns the screen -- see mode_toggle_reset in input.h. */
            mode_toggle_reset();
            menu_clear();
            SRL::Core::Synchronize();
            /* Room text is the parser's own state and the parser is on the
               server, so a change only takes hold by being typed at it -- the
               same handling saturn_glue.cxx gives the CD build's Options menu.
               The half-built line is put back afterwards, since
               term_submit_line resets the keyboard it sends from. */
            if (g_verbosity != verb_was) {
                char pending[KB_INPUT_MAX];
                int i = 0;
                for (; k.input[i] != '\0' && i < KB_INPUT_MAX - 1; i++) pending[i] = k.input[i];
                pending[i] = '\0';
                keyboard_load_line(&k, verbosity_command());
                term_submit_line(tr, &k);
                g_output_start = console_total_lines();
                term_mark_output(&ts);
                keyboard_load_line(&k, pending);
            }
            continue;
        }

        /* The two interfaces keep their own buffers -- the panel draws
           cpanel.line, the keyboard k.input -- so the swap carries the
           half-built command across, exactly as saturn_glue.cxx does it for the
           local game. */
        if (g_kbd_visible && (mode_combo_fired() || panel_swap)) {
            panel_swap = false;
            if (g_cmd_mode == IFACE_PANEL) {
                keyboard_load_line(&k, cpanel.line);
                g_cmd_mode = IFACE_KEYBOARD;
            } else {
                cp_load_line(&cpanel, k.input);
                g_cmd_mode = IFACE_PANEL;
            }
        }
        bool panel = (g_kbd_visible && g_cmd_mode == IFACE_PANEL);
        CommandWords cw;
        DictionaryWord* selected = nullptr; int cw_len = 0;
        if (panel) command_edit(k, cpanel, *netbin_room(), g_online_ta, ke, cw);
        else       typeahead_edit(k, g_online_ta, sug_index, sug_last, ke, pad, selected, cw_len);
        /* The panel's Map row. Spent here for the same reason the pause menu is
           run from this loop and not from inside the editor: the map owns the
           screen while it is up, and this is the only place that can give it and
           take it back. */
        if (cpanel.action == CP_ACT_MAP) {
            cpanel.action = CP_ACT_NONE;
            map_view_show();
            mode_toggle_reset();
            menu_clear();
            SRL::Core::Synchronize();
            continue;
        }
        /* The menu and swap rows only raise a flag: both are acted on at the top
           of the loop, where the pad's own Start and L+R are already handled, so
           a row and a button press cannot take two different paths to the same
           place. */
        if (cpanel.action == CP_ACT_MENU) { cpanel.action = CP_ACT_NONE; panel_menu = true; continue; }
        if (cpanel.action == CP_ACT_SWAP) { cpanel.action = CP_ACT_NONE; panel_swap = true; continue; }
#else
        DictionaryWord* selected; int cw_len;
        typeahead_edit(k, g_online_ta, sug_index, sug_last, ke, pad, selected, cw_len);
#endif
        pad_scroll_update();

        bool did_submit = k.submitted;
        if (k.submitted) {
            g_scroll = 0;
            history_push(k.input);
            if (is_reboot_command(k.input)) {
#ifdef NETBIN
                // No title screen in this build -- match the chord prompt in
                // main_netbin.cxx's check_soft_reset.
                confirm_return_to_title("reboot back to the dial page?");
#else
                confirm_return_to_title("reboot back to the title screen?");
#endif
                keyboard_clear_line(&k);
#ifdef NETBIN
                // The confirm ran its own poll loop, so the toggle button could
                // have been pressed and released entirely while it owned the
                // screen -- see mode_toggle_reset in input.h.
                mode_toggle_reset();
#endif
                online_settle_input();
            } else {
                term_submit_line(tr, &k);
                g_output_start = console_total_lines();
                term_mark_output(&ts);
#ifdef NETBIN
                cp_reset(&cpanel);
#endif
            }
        }

        if (!cui_transport_is_connected(tr)) {
            console_write("\n*** connection lost ***\n", 25);
            render_console();
            SRL::Core::Synchronize();
            break;
        }

        render_console();
        console_pointer_scroll();
#ifdef NETBIN
        if (panel) render_command_panel(cpanel, *netbin_room(), cw);
        else       render_keyboard(k, did_submit ? nullptr : selected, did_submit ? 0 : cw_len);
#else
        render_keyboard(k, did_submit ? nullptr : selected, did_submit ? 0 : cw_len);
#endif
        text_print(0, console_screen_rows() - 1, "%s", hint("L/R=cycle  hold L+R=disconnect", "Esc=disconnect"));
        menu_sync();
    }
    net_connect_close();
}
