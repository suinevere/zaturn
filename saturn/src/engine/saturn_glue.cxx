/*----------------------
 | saturn_glue.cxx
 | Description: The bridge between the C Z-Machine core (mojozork) and the Saturn
 |   client. Implements the hooks the interpreter calls through its ZMachineState
 |   function pointers -- text output, the interactive read loop, story re-read,
 |   fatal halt, and save/restore of the game blob -- plus the typeahead trie the
 |   local prompt drives, the typeahead allocator the core links against, and the
 |   LWRAM scratch allocator the save/restore opcodes use so their buffers never
 |   compete with that trie for the C heap. The
 |   read loop is where a turn's input is gathered: it runs the mid-game menu
 |   shortcuts, the save/load function keys, and the shared typeahead editor, and
 |   intercepts the reboot/quit commands before they reach the interpreter.
 | Author: suinevere
 | Dependencies: saturn_glue.h, console.h + console_view.h (screen, on-screen
 |   keyboard, typeahead_edit), command_view.h (command-panel render/edit),
 |   room_model.h (the room snapshot the panel reads), keyboard.h
 |   (KeyboardState), saturn_keyboard.h (key events), input.h (g_pad, pad
 |   repeat/scroll, history, mode_toggle_fired), typeahead.h +
 |   typeahead_extract.h + typeahead_solution.h (the trie), menu.h + menu_pages.h
 |   (mid-game menus and save dialogs), save_ui.h (device/slot pickers),
 |   saturn_backup.h (backup reads/writes), soft_reset.h (reboot/quit handling),
 |   sound.h + music.h (per-turn audio service), app_state.h (option/session
 |   globals), SRL.
 ----------------------*/

#include <srl.hpp>
#include <stdarg.h>

/* Declared here rather than reached through <stdio.h>: SRL puts a dummy stdio.h
   on the include path ahead of newlib's (modules/dummy/stdio.h), and that one
   defines printf to a no-op and declares nothing else. newlib still provides the
   symbol -- online.cxx already links snprintf out of the same machinery -- so the
   prototype is all that is missing. */
extern "C" int vsnprintf(char *, size_t, const char *, va_list);

#include "console_view.h"
#include "command_view.h"
#include "input.h"
#include "menu.h"
#include "menu_pages.h"
#include "room_model.h"
#include "save_ui.h"
#include "soft_reset.h"
extern "C" {
#include "saturn_glue.h"
#include "console.h"
#include "keyboard.h"
#include "saturn_keyboard.h"
#include "saturn_backup.h"
#include "sound.h"
#include "music.h"
#include "typeahead.h"
#include "typeahead_extract.h"
#include "typeahead_solution.h"
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

/*----------------------
 | typeahead_malloc / typeahead_free
 | Description: The allocator the C typeahead core links against, routed to Low
 |   Work RAM. The trie is the client's largest live allocation -- 89 KB for
 |   Mini-Zork I up to 318 KB for Wishbringer, measured over the shipped library --
 |   and the C heap is only the ~304 KB of High Work RAM left after the binary,
 |   which is also carrying the story image (47-130 KB). Eleven of the 32 stories
 |   could not fit their trie there at all: the build ran the heap dry part-way and
 |   the unchecked allocations wrote through NULL. LWRAM is a separate 1 MB zone,
 |   so the largest trie now lands with ~690 KB still free. LWRAM is the slower of
 |   the two work RAMs (16 bits wide, behind the SCU), which is why the story image
 |   and the interpreter's own state stay in HWRAM. The prompt does re-rank every
 |   frame, but the subtree walk only runs once the player has typed a letter and
 |   descends to that prefix first, so it touches a few hundred nodes rather than
 |   the whole trie. typeahead_free returns to the generic SRL free, which routes a
 |   pointer to whichever zone owns it.
 | Author: suinevere
 ----------------------*/
extern "C" void* typeahead_malloc(unsigned int size) {
    return SRL::Memory::LowWorkRam::Malloc(size);
}

extern "C" void typeahead_free(void* ptr) {
    SRL::Memory::Free(ptr);
}

/*----------------------
 | g_typeahead_root / g_ta_story / g_ta_diff
 | Description: The local prompt's typeahead trie and the (story, difficulty) it
 |   was last built for, so ensure_typeahead can tell when a rebuild is due.
 | Author: suinevere
 ----------------------*/
static TrieNode* g_typeahead_root = nullptr;
static const uint8_t* g_ta_story = nullptr;
static int g_ta_diff = -1;

/*----------------------
 | saturn_typeahead_release
 | Description: Frees the local prompt's trie and forgets the story it was built
 |   for, so the next game builds its own. For the soft reset, which is the moment
 |   this stops being a cache and starts being ~300 KB of Low Work RAM held for a
 |   game that has ended.
 |
 |   Not an optimisation. It is what the boot jingle could not fit beside: the
 |   online trie is legitimately kept across a return -- it is the same Zork I
 |   dictionary every time and rebuilding it costs a CD read -- but with BOTH tries
 |   resident the zone was ~653 KB spoken for and SPLASH.PCM's 453 KB Malloc simply
 |   returned null. A null there is silent by construction, which is why the title
 |   screen came back mute and stayed that way through the next loading screen.
 |
 |   g_ta_story goes with it, and would be worth clearing even if nothing else here
 |   were: the story image it points at is High Work RAM the reset is about to drop,
 |   so leaving it set leaves ensure_typeahead comparing against a dangling pointer.
 | Author: suinevere
 | Dependencies: typeahead.h
 | Globals: g_typeahead_root, g_ta_story, g_ta_diff
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void saturn_typeahead_release(void) {
    if (g_typeahead_root) { destroy_typeahead(g_typeahead_root); g_typeahead_root = nullptr; }
    g_ta_story = nullptr;
    g_ta_diff  = -1;
}

/*----------------------
 | ensure_typeahead
 | Description: Rebuilds g_typeahead_root from the currently loaded story whenever
 |   the story or the difficulty changes (freeing the old trie first), so
 |   switching games picks up the new vocabulary. Layers the story's own grammar,
 |   then the winning-path solution overlay (applied in BOTH Easy and Normal --
 |   Easy restricts suggestions to it, Normal uses its links as the "unless in
 |   solution" exception to the grammar filter), then the twelve stock
 |   abbreviations last so they land in whatever trie the story produced. Hard
 |   builds no trie at all, so it stays helpless -- the abbreviations exist iff a
 |   trie does. If even the root node cannot be allocated the trie stays null and
 |   the prompt behaves as it does on Hard; g_ta_story is still recorded, but the
 |   null root fails the cache test above, so the next prompt tries again.
 |   Binds the room model to the same story on every rebuild, unconditionally --
 |   deliberately outside the difficulty-gated trie-build branch, so Hard (which
 |   builds no trie) still gets a working room model; the panel has nothing to do
 |   with typeahead and must not go dark at the one difficulty a player picked to
 |   keep it useful.
 | Author: suinevere
 | Dependencies: saturn_glue.h (saturn_story_data), typeahead.h,
 |   typeahead_extract.h, typeahead_solution.h, room_model.h
 | Globals: g_typeahead_root, g_ta_story, g_ta_diff, g_difficulty
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void ensure_typeahead() {
    uint32_t len = 0;
    const uint8_t* story = saturn_story_data(&len);
    if (g_typeahead_root && story == g_ta_story && g_ta_diff == g_difficulty) return;
    if (g_typeahead_root) { destroy_typeahead(g_typeahead_root); g_typeahead_root = nullptr; }
    g_typeahead_root = create_trie_node();
    int have_solution = 0;
    if (g_typeahead_root != nullptr && story != nullptr && len > 0 && g_difficulty != DIFF_HARD) {
        build_typeahead_from_story(g_typeahead_root, story, len);
        have_solution = apply_solution_overlay(g_typeahead_root, story, len);
        typeahead_add_abbreviations(g_typeahead_root);
    }
    typeahead_set_easy(g_difficulty == DIFF_EASY, have_solution);
    if (story != nullptr && len > 0) room_model_bind(story, len);
    g_ta_story = story;
    g_ta_diff = g_difficulty;
}

/*----------------------
 | saturn_typeahead_build
 | Description: See saturn_glue.h.
 | Author: suinevere
 | Dependencies: typeahead.h
 | Globals: g_typeahead_root, g_ta_story, g_ta_diff
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void saturn_typeahead_build(void) {
    ensure_typeahead();
}

/*----------------------
 | saturn_writestr
 | Description: The interpreter's text-output hook: writes to the console and
 |   feeds the same text to the room-music classifier so Dynamic mix can react to
 |   what the room describes.
 | Author: suinevere
 | Dependencies: console.h, music.h
 | Globals: N/A
 | Params: str -- text to emit; slen -- its length
 | Returns: N/A
 ----------------------*/
extern "C" void saturn_writestr(const char *str, size_t slen) {
    console_write(str, (unsigned int) slen);
    music_note_output(str, (unsigned int) slen);
}

/*----------------------
 | typeahead_scan_screen
 | Description: Marks the words currently visible on the console as on-screen, so
 |   objects the game just described lead their suggestions. The visible text is
 |   split at g_output_start -- the line the last command's reply began on -- so
 |   the reply's nouns rank as fresh and the room description above them as
 |   ordinary scenery. Each half is gathered newest line first, so when the buffer
 |   fills it is the oldest text that is lost rather than the line that just
 |   arrived. Re-run after any trie rebuild (e.g. a mid-game difficulty change),
 |   since the marks live on the words.
 | Author: suinevere
 | Dependencies: console.h, console_view.h, typeahead.h
 | Globals: g_output_start
 | Params: root -- the trie whose words to mark
 | Returns: N/A
 ----------------------*/

/*----------------------
 | gather_lines
 | Description: Concatenates console lines [from, to) into `buf` in forward print
 |   order. When the whole range does not fit, it keeps the newest lines -- a
 |   backward pass finds the earliest line that still fits, then the text is
 |   emitted forward from there. Forward order matters because the screen marker
 |   ranks nouns by scan position (later = more recently printed); emitting newest
 |   first would invert that and offer the room's first-named object over its
 |   last, which read as "open house" when the last line said "a mailbox is here".
 | Author: suinevere
 | Dependencies: console.h
 | Globals: N/A
 | Params: from, to -- the half-open line range; buf/cap -- output
 | Returns: N/A
 ----------------------*/
static void gather_lines(int from, int to, char *buf, int cap) {
    int start = to, used = 0, sp = 0;
    for (int li = to - 1; li >= from; li--) {
        const char* ln = console_get_line(li);
        int len = 0; while (ln[len]) len++;
        if (used + len + 1 > cap - 1) break;
        used += len + 1;
        start = li;
    }
    for (int li = start; li < to && sp < cap - 1; li++) {
        const char* ln = console_get_line(li);
        for (int j = 0; ln[j] && sp < cap - 1; j++) buf[sp++] = ln[j];
        if (sp < cap - 1) buf[sp++] = ' ';
    }
    buf[sp] = '\0';
}

static void typeahead_scan_screen(TrieNode *root) {
    char older[768], recent[512];
    int total = console_line_count(), rows = console_height();
    int startln = (total > rows) ? (total - rows) : 0;
    /* g_output_start counts lines ever produced; console_get_line indexes only
       the retained ones, and the two part company as soon as the ring evicts.
       Convert through the turn's line count, the way console_view.cxx:286 does. */
    long added = console_total_lines() - g_output_start;
    if (added < 0) added = 0;
    if (added > total) added = total;
    int split = total - (int) added;
    if (split < startln) split = startln;
    gather_lines(startln, split, older, (int) sizeof older);
    gather_lines(split, total, recent, (int) sizeof recent);
    typeahead_set_screen_recent(root, older, recent);
}

/*----------------------
 | submit_command
 | Description: Puts `cmd` on the input line and marks it submitted, as if the
 |   player had typed it and pressed Enter -- so it echoes, enters history, and
 |   reaches the interpreter by the one path every other command uses. This is
 |   how the F-key shortcuts run the game's own save/restore.
 | Author: suinevere
 | Dependencies: keyboard.h
 | Globals: N/A
 | Params: k -- keyboard/input-line state to fill; cmd -- command text
 | Returns: N/A
 ----------------------*/
static void submit_command(KeyboardState &k, const char *cmd) {
    int n = 0;
    while (cmd[n] != '\0' && n < KB_INPUT_MAX - 1) { k.input[n] = cmd[n]; n++; }
    k.input[n] = '\0';
    k.input_len = n;
    k.cursor = n;
    k.submitted = 1;
}

/*----------------------
 | run_room_transition
 | Description: Runs an armed mood change to completion before the turn's text is
 |   drawn, so a new room's picture and track come up first and the description
 |   arrives onto them.
 |
 |   It works only because of where it sits. The interpreter prints a whole turn
 |   into the console buffer without advancing a frame, so at this point the new
 |   text exists but has never been rendered and the screen still holds the
 |   previous turn. Synchronizing here without calling render_console therefore
 |   fades the OLD screen out, swaps the picture at the bottom, and fades the new
 |   one in against a console that on_text_category cleared -- and the caller's
 |   first render_console after this is what finally puts the new text on it.
 |
 |   Costs the player the fade before they can type. That is the ask, and
 |   MUSIC_FADE_FRAMES in main.cxx is the number to cut if it reads sluggish --
 |   except on the opening room, which skips the ramp entirely (see below).
 | Author: suinevere
 | Dependencies: music.h, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void run_room_transition(void) {
    if (!music_transition_active()) return;

    // The opening room is a special case: main() holds the screen black until the
    // first prompt reveals it, so the forty fields this would otherwise spend are
    // spent fading black into black. Commit it outright instead -- the picture is
    // still swapped and the track still started, so what the reveal uncovers is
    // the same screen, just arrived at without the wait.
    if (g_intro_reveal) { music_transition_skip_fade(); return; }

    music_transition_flush();
    int guard = 0;
    while (music_transition_active() && guard < 600) {
        music_tick();
        SRL::Core::Synchronize();
        guard++;
    }
}

/*----------------------
 | saturn_readline
 | Description: The interpreter's line-input hook, and the local game loop. First
 |   disarms any unspent quick-save destination (F5 arms and submits in one
 |   breath, so at a prompt it is always spent or void; a story save opcode that
 |   bailed early would otherwise leave a stale slot to hijack the next save -- the
 |   restore side can't be swept the same way, since "Load Save Game" arms
 |   g_restore_* before the very readline that submits its "restore"). A queued
 |   one-shot autocommand (the "restore" that applies a pre-picked save) is
 |   returned immediately. Otherwise it rebuilds the typeahead, marks on-screen
 |   words, refreshes the room model from the current room (once per prompt,
 |   not per frame -- it walks the object tree), resets the command panel
 |   (cp_reset) every turn alongside k's own per-turn clear -- command_edit
 |   only resets it on its own completed submit, so a turn that ends any other
 |   way (a menu's Save Game row, a quick-save/restore key, toggling to the
 |   keyboard mid-build) would otherwise leave a half-built sentence to prefix
 |   the next turn's pick -- keeps the keyboard picker position across
 |   prompts, and positions the view at the TOP of the turn's output so a long
 |   response reads from its start.
 |   The frame loop runs the soft-reset chord, the F10/F11/F12 menu shortcuts
 |   (Sound only when there is audio to configure; F10's Options menu can
 |   itself report a Save Game/Load Game pick, submitted the same way as
 |   below, and holds the music paused mid-track while it is open), the
 |   F2/F5 save and F3/F6/F9 restore keys (which submit the game's own
 |   command so the blob hooks do the work), a toggle-button tap (gamepad
 |   only -- a real keyboard in hand keeps its own prompt untouched) that
 |   swaps g_cmd_mode between the command panel and the on-screen keyboard,
 |   and then whichever of the two editors g_cmd_mode selects -- command_edit
 |   or the shared typeahead editor -- each writing its result into the same
 |   KeyboardState, so both leave through the one submit path. Finally it
 |   services audio. On submit it strips the autocomplete-accept trailing
 |   space, echoes the command, and intercepts reboot/quit (a declined
 |   confirm is not passed to the game) before handing the line back with the
 |   fgets-style trailing '\n'. Four sites in this function hand control to a
 |   blocking UI of their own and call mode_toggle_reset() the instant it
 |   returns -- the toggle button could have been pressed and released
 |   entirely while that UI owned the screen, and without the reset the next
 |   frame's mode_toggle_fired would see a stale held state and swap
 |   interfaces on nothing the player did at the prompt: the F10/START/Esc
 |   Options menu (int om = options_menu()), the F11 keyboard-controls page,
 |   the F12 sound-options page (inside its has-audio guard), and the reboot/
 |   quit confirm_return_to_title() calls. The F2/F5/F3/F6/F9 save/restore
 |   keys are NOT such a site: they only call submit_command (which does not
 |   block) and exit the frame loop; their own device/slot picker runs later,
 |   inside saturn_save_blob/saturn_load_blob, entirely outside this
 |   function, where no reset placed here could reach it.
 | Author: suinevere
 | Dependencies: console.h, console_view.h, command_view.h, room_model.h,
 |   keyboard.h, saturn_keyboard.h, input.h, menu.h, menu_pages.h, soft_reset.h,
 |   sound.h, music.h, typeahead.h, SRL
 | Globals: g_save_device, g_save_slot, g_last_device, g_last_slot,
 |   g_restore_device, g_restore_slot, g_autocmd, g_kbd_visible, g_cmd_mode,
 |   g_scroll, g_output_start, g_pad, g_typeahead_root
 | Params: buf -- receives the entered line + '\n'; maxlen -- capacity of buf
 | Returns: N/A
 ----------------------*/
extern "C" void saturn_readline(char *buf, int maxlen) {
    if (maxlen < 2) { if (maxlen > 0) buf[0] = '\0'; return; }
    g_save_device = -1;
    g_save_slot   = -1;
    /* Ahead of g_autocmd, not instead of it: the Load Save Game path queues a
       "restore" there, and that one has to arrive at a prompt whose verbosity is
       already settled -- otherwise the restored game prints its first room in the
       mode the story shipped with rather than the player's. Each is a single-shot,
       so the restore simply lands on the next prompt. */
    if (g_verb_pending) {
        g_verb_pending = 0;
        const char *c = verbosity_command();
        int n = 0;
        while (c[n] && n < maxlen - 2) { buf[n] = c[n]; n++; }
        buf[n] = '\n'; buf[n + 1] = '\0';
        return;
    }
    if (g_autocmd != nullptr) {
        const char *c = g_autocmd; g_autocmd = nullptr;
        int n = 0;
        while (c[n] && n < maxlen - 2) { buf[n] = c[n]; n++; }
        buf[n] = '\n'; buf[n + 1] = '\0';
        return;
    }
    run_room_transition();
    ensure_typeahead();
    typeahead_scan_screen(g_typeahead_root);
    room_model_refresh();

    static KeyboardState k;
    static CommandPanel cpanel;
    static int kbd_inited = 0;
    if (!kbd_inited) { keyboard_reset(&k); kbd_inited = 1; }
    k.input_len = 0;
    k.input[0] = '\0';
    k.cursor = 0;
    k.submitted = 0;
    cp_reset(&cpanel);
    SRL::Core::Synchronize();
    int sug_index = 0;
    char sug_last[256] = "";
    console_scroll_to_output();
    /* The game hand-off ends here. Everything above has run for this turn --
       run_room_transition has settled the room's picture and track -- so this is
       the first moment the opening frame is complete and still unseen. Compose it,
       then let main()'s routine ramp it up out of the black the loading screen
       left. One shot: every later prompt finds this null. */
    if (g_intro_reveal) {
        void (*reveal)(void) = g_intro_reveal;
        g_intro_reveal = 0;
        render_console();
        reveal();
    }
    for (;;) {
      while (!k.submitted) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind != SATURN_KEY_NONE) g_kbd_visible = false;
        bool pad = (ke.kind == SATURN_KEY_NONE);
        if (pad && g_pad->AnyPressed()) g_kbd_visible = true;
        pad_repeat_update();
        chord_tick();

        if ((pad && g_pad->WasPressed(Button::START)) || ke.kind == SATURN_KEY_ESCAPE
            || ke.kind == SATURN_KEY_F10) {
            // Duck the music for as long as the menu is up rather than stopping it:
            // the game is still underneath, so the track should thin out, not cut.
            // The Sound page lifts it to full itself if the player goes that way --
            // it is judged by ear, so it is the one page that opens loud. Save/Load
            // are the exception: picking one here only closes the Options box --
            // the device/slot picker still has to run inside the interpreter's
            // save/restore hook, so resuming right here would restart the drive
            // before that picker is even open. saturn_save_blob/saturn_load_blob
            // call music_resume() themselves once THAT closes instead.
            int verb_was = g_verbosity;
            music_duck();
            int om = options_menu();
            mode_toggle_reset();
            if (om != OM_SAVE && om != OM_RESTORE) music_resume();
            ensure_typeahead();
            typeahead_scan_screen(g_typeahead_root);
            SRL::Core::Synchronize();
            // Room text is the story's own state, so a change only takes hold by
            // being typed at it. Save/Load own this turn, so it waits for the next
            // one through the same single-shot the Load path uses.
            const char *vcmd = (g_verbosity != verb_was) ? verbosity_command() : nullptr;
            if (om == OM_SAVE)    { if (vcmd) g_verb_pending = 1; submit_command(k, "save");    continue; }
            if (om == OM_RESTORE) { if (vcmd) g_verb_pending = 1; submit_command(k, "restore"); continue; }
            if (vcmd) { submit_command(k, vcmd); continue; }
            continue;
        }
        if (ke.kind == SATURN_KEY_F11) {
            keyboard_controls_page();
            mode_toggle_reset();
            menu_clear();
            SRL::Core::Synchronize();
            continue;
        }
        if (ke.kind == SATURN_KEY_F12) {
            if (music_cdda_has_audio() || sound_has_audio()) {
                sound_options_page();
                mode_toggle_reset();
                menu_clear();
                SRL::Core::Synchronize();
            }
            continue;
        }
        if (ke.kind == SATURN_KEY_F2 || ke.kind == SATURN_KEY_F5) {
            if (ke.kind == SATURN_KEY_F5 && g_last_slot >= 0) {
                g_save_device = g_last_device; g_save_slot = g_last_slot;
            }
            submit_command(k, "save");
            continue;
        }
        if (ke.kind == SATURN_KEY_F3 || ke.kind == SATURN_KEY_F6 || ke.kind == SATURN_KEY_F9) {
            if (ke.kind != SATURN_KEY_F3 && g_last_slot >= 0) {
                g_restore_device = g_last_device; g_restore_slot = g_last_slot;
            }
            submit_command(k, "restore");
            continue;
        }

        /* The two interfaces keep their own buffers -- the panel draws p.line,
           the keyboard k.input -- so the swap has to carry the half-built
           command across, or the player loses what they just picked or typed.
           cp_load_line re-derives the slot from the word count, exactly as it
           does for a recalled command. */
        if (g_kbd_visible && mode_toggle_fired()) {
            if (g_cmd_mode == IFACE_PANEL) {
                keyboard_load_line(&k, cpanel.line);
                g_cmd_mode = IFACE_KEYBOARD;
            } else {
                cp_load_line(&cpanel, k.input);
                g_cmd_mode = IFACE_PANEL;
            }
        }

        if (g_kbd_visible && g_cmd_mode == IFACE_PANEL) {
            CommandWords cw;
            command_edit(k, cpanel, *room_model_get(), g_typeahead_root, ke, cw);
            pad_scroll_update();
            render_console();
            render_command_panel(cpanel, *room_model_get(), cw);
        } else {
            DictionaryWord* selected; int cw_len;
            typeahead_edit(k, g_typeahead_root, sug_index, sug_last, ke, pad, selected, cw_len);
            pad_scroll_update();
            render_console();
            render_keyboard(k, selected, cw_len);
        }
        SRL::Core::Synchronize();
        sound_service();
        music_tick();
      }
      while (k.input_len > 0 && k.input[k.input_len - 1] == ' ') k.input[--k.input_len] = '\0';
      g_scroll = 0;
      history_push(k.input);
      console_write(k.input, (unsigned int) k.input_len);
      console_write("\n", 1);
      g_output_start = console_total_lines();
      render_console();
      if (is_reboot_command(k.input)) {
          confirm_return_to_title("reboot back to the title screen?");
          mode_toggle_reset();
          k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; k.submitted = 0;
          SRL::Core::Synchronize();
          continue;
      }
      if (is_quit_command(k.input)) {
          confirm_return_to_title("quit back to the title screen?");
          mode_toggle_reset();
          k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; k.submitted = 0;
          SRL::Core::Synchronize();
          continue;
      }
      break;
    }
    int n = k.input_len;
    if (n > maxlen - 2) n = maxlen - 2;
    for (int i = 0; i < n; i++) buf[i] = k.input[i];
    buf[n]     = '\n';
    buf[n + 1] = '\0';
}

/*----------------------
 | saturn_read_story_prefix
 | Description: Re-reads the first `want` bytes of the loaded story image from CD
 |   (there is no fopen on Saturn). The GFS size read can come back garbage on
 |   first access, so it retries the stat until the sector size and byte count
 |   match the expected whole-file length, then reads the prefix. Reading a prefix
 |   is safe and exact: SRL::Cd::File::Read stages whole sectors in its own work
 |   buffer and copies out exactly the byte count asked for. Save/restore ask for
 |   dynamic memory only -- roughly 12 KB of a 130 KB story -- which keeps both the
 |   scratch buffer and the seek time small.
 | Author: suinevere
 | Dependencies: app_state.h (g_story_filename), SRL
 | Globals: g_story_filename
 | Params: buf -- destination (>= want bytes); storylen -- expected whole-file
 |   length in bytes, used to validate the stat; want -- bytes to read from the
 |   start of the file, clamped to storylen
 | Returns: 1 on success, 0 on failure
 ----------------------*/
extern "C" int saturn_read_story_prefix(uint8_t *buf, uint32_t storylen, uint32_t want) {
    if (want > storylen) want = storylen;
    for (int attempt = 0; attempt < 300; attempt++) {
        SRL::Cd::File f(g_story_filename);
        if (f.Size.SectorSize == 2048 && (uint32_t) f.Size.Bytes == storylen) {
            if (f.Open()) {
                int32_t got = f.Read((int32_t) want, buf);
                f.Close();
                if (got == (int32_t) want) { return 1; }
            }
        }
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    return 0;
}

/*----------------------
 | saturn_read_story_file
 | Description: Re-reads the whole loaded story image from CD for opcode_restart,
 |   which adopts the buffer as the new story. The prefix reader does the work.
 | Author: suinevere
 | Dependencies: saturn_read_story_prefix
 | Globals: g_story_filename (via saturn_read_story_prefix)
 | Params: buf -- destination; len -- expected story length in bytes
 | Returns: 1 on success, 0 on failure
 ----------------------*/
extern "C" int saturn_read_story_file(uint8_t *buf, uint32_t len) {
    return saturn_read_story_prefix(buf, len, len);
}

/*----------------------
 | saturn_scratch_alloc / saturn_scratch_free
 | Description: The save/restore scratch allocator, routed to Low Work RAM rather
 |   than the C heap. The C heap is the ~304 KB of High Work RAM left after the
 |   binary, and it is already carrying the story image plus the typeahead trie
 |   (115-200 KB, measured across the shipped stories) by the time a player saves,
 |   so the opcodes' scratch mallocs came back NULL and the save/restore opcode
 |   branched false before ever reaching saturn_save_blob -- no picker, and the
 |   story printing its own "failed" line. LWRAM is a separate 1 MB zone with no
 |   other claimants in this client, so the scratch cannot collide with the trie
 |   however large the story's vocabulary is. TLSF backs both zones, so a free
 |   genuinely returns the space for the next save.
 | Author: suinevere
 | Dependencies: SRL (Memory::LowWorkRam)
 | Globals: N/A
 | Params: size -- bytes to allocate; ptr -- block to release (NULL ignored)
 | Returns: the block, or NULL if LWRAM cannot satisfy it
 ----------------------*/
extern "C" void *saturn_scratch_alloc(uint32_t size) {
    return SRL::Memory::LowWorkRam::Malloc((size_t) size);
}

extern "C" void saturn_scratch_free(void *ptr) {
    if (ptr != nullptr) SRL::Memory::LowWorkRam::Free(ptr);
}

/*----------------------
 | saturn_die
 | Description: The interpreter's fatal-halt hook, and main()'s too for a story
 |   that would not load. Prints the halt notice, then the caller's own reason under
 |   it, and spins forever redrawing the console so the last screen stays readable.
 |
 |   The reason is not decoration. This hook used to discard fmt entirely, which
 |   made every fatal cause print the same line: a story the CD never found and a
 |   Z-machine fault were indistinguishable on screen, and "Could not load %s from
 |   CD" -- passed from main() precisely so it would be seen -- never was.
 | Author: suinevere
 | Dependencies: console.h, console_view.h, SRL
 | Globals: N/A
 | Params: fmt -- printf-style reason from the caller; may be NULL
 | Returns: N/A (does not return)
 ----------------------*/
extern "C" void saturn_die(const char *fmt, ...) {
    console_write("\n*** interpreter halted ***\n", 28);
    if (fmt != nullptr) {
        char msg[128];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);
        if (n > 0) {
            if (n >= (int) sizeof(msg)) n = (int) sizeof(msg) - 1;
            console_write(msg, (unsigned int) n);
            console_write("\n", 1);
        }
    }
    while (1) { render_console(); SRL::Core::Synchronize(); }
}

/*----------------------
 | saturn_save_blob
 | Description: The interpreter's save hook. A pre-armed quick-save (F5) goes
 |   straight to the last slot with no device/slot/overwrite prompt -- the whole
 |   point of it -- keeping whatever name the slot already carries. Otherwise it
 |   picks a device, runs the slot picker + in-place name editor, defaults an
 |   empty name to "Save N", and confirms before overwriting an existing save.
 |   Records the committed device/slot as the quick-key target, then reports the
 |   result over an opaque backing so it reads over an image background.
 | Author: suinevere
 | Dependencies: save_ui.h (choose_device/pick_slot_and_name/make_slot_name),
 |   saturn_backup.h, menu.h, music.h (music_resume), SRL
 | Globals: g_save_device, g_save_slot, g_last_device, g_last_slot
 | Params: data -- blob to write; len -- its length
 | Returns: 1 on success, 0 on cancel or failure
 ----------------------*/
extern "C" int saturn_save_blob(const uint8_t *data, uint32_t len) {
    int device, slot;
    char comment[12];
    char name[12];
    if (g_save_slot >= 0) {
        device = g_save_device; slot = g_save_slot;
        g_save_device = -1; g_save_slot = -1;
        make_slot_name(name, slot);
        if (!saturn_bup_info(device, name, comment))
            snprintf(comment, sizeof(comment), "Save %d", slot + 1);
    } else {
        device = choose_device("SAVE - device?");
        if (device < 0) { music_resume(); return 0; }

        if (!pick_slot_and_name(device, &slot, comment, 8)) { music_resume(); return 0; }
        if (comment[0] == 0) snprintf(comment, sizeof(comment), "Save %d", slot + 1);

        make_slot_name(name, slot);
        char existing[12];
        if (saturn_bup_info(device, name, existing)) {
            char q[40];
            snprintf(q, sizeof(q), "Overwrite \"%s\"?", existing);
            if (!menu_confirm(q, "Are you sure?")) { music_resume(); return 0; }
        }
    }

    int ok = saturn_bup_write(device, name, comment, data, len);
    if (ok) { g_last_device = device; g_last_slot = slot; }
    {
        MenuBacking backing;
        menu_message("SAVE", ok ? "Saved." : "Save FAILED (no space?).",
                     "(press any key/button)");
        menu_wait();
    }
    music_resume();
    return ok;
}

/*----------------------
 | saturn_load_blob
 | Description: The interpreter's restore hook. A pre-picked slot (armed by "Load
 |   Save Game") is consumed once; otherwise it runs choose_dest to resolve a
 |   device and slot. Reads the blob, records the device/slot as the quick-key
 |   target on success, and reports an empty slot to the player.
 | Author: suinevere
 | Dependencies: save_ui.h (choose_dest/make_slot_name), saturn_backup.h, menu.h,
 |   music.h (music_resume)
 | Globals: g_restore_device, g_restore_slot, g_last_device, g_last_slot
 | Params: buf -- destination for the blob; maxlen -- capacity (unused; the
 |   backup layer knows the record size)
 | Returns: 1 on success, 0 on cancel or failure
 ----------------------*/
extern "C" int saturn_load_blob(uint8_t *buf, uint32_t maxlen) {
    (void) maxlen;
    int device, slot;
    if (g_restore_slot >= 0) {
        device = g_restore_device; slot = g_restore_slot;
        g_restore_device = -1; g_restore_slot = -1;
    } else if (!choose_dest("RESTORE - device?", "RESTORE - slot?", &device, &slot)) {
        music_resume();
        return 0;
    }
    char name[12];
    make_slot_name(name, slot);
    int ok = saturn_bup_read(device, name, buf);
    if (ok) { g_last_device = device; g_last_slot = slot; }
    if (!ok) {
        MenuBacking backing;
        menu_message("RESTORE", "No save in that slot.", "(press any key/button)");
        menu_wait();
    }
    music_resume();
    return ok;
}
