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
 |   dash_view.h (holding the input strip's panel over frames no renderer draws),
 |   text_map.h (text_clear_line, for blanking the strip's rows on a fatal halt),
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
#include "dash_view.h"
#include "text_map.h"
#include "input.h"
#include "menu.h"
#include "menu_pages.h"
#include "room_model.h"
#include "map_model.h"
#include "save_blob.h"
#include "map_atlas.h"
#include "map_marks.h"
#include "save_ui.h"
#include "soft_reset.h"
#include "title.h"   /* title_bg_fade_engaged */
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
#include "video/item_art.h"

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
 | g_map_story
 | Description: The story the map model currently holds rooms for. Kept apart
 |   from g_ta_story because the trie is rebuilt whenever the difficulty
 |   changes and whenever a node allocation failed, and neither of those is a
 |   new world -- keying the map's reset on the trie's cache test threw the
 |   whole map away when the player changed difficulty from the same Options
 |   menu that offers the map, and once per prompt under memory pressure.
 | Author: suinevere
 ----------------------*/
static const uint8_t* g_map_story = nullptr;

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
 |   g_map_story is cleared for exactly that reason and no other.
 | Author: suinevere
 | Dependencies: typeahead.h
 | Globals: g_typeahead_root, g_ta_story, g_ta_diff, g_map_story
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void saturn_typeahead_release(void) {
    if (g_typeahead_root) { destroy_typeahead(g_typeahead_root); g_typeahead_root = nullptr; }
    g_ta_story = nullptr;
    g_ta_diff  = -1;
    g_map_story = nullptr;
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
 |   Forgets the map on the same terms: only when the story pointer itself
 |   changes, which is the one event that makes stored rooms belong to another
 |   world. Deliberately not on the cache test above, which also fires on a
 |   difficulty change and on every prompt after a failed node allocation.
 | Author: suinevere
 | Dependencies: saturn_glue.h (saturn_story_data), typeahead.h,
 |   typeahead_extract.h, typeahead_solution.h, room_model.h, map_model.h,
 |   map_atlas.h, map_marks.h
 | Globals: g_typeahead_root, g_ta_story, g_ta_diff, g_difficulty, g_map_story
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
    if (story != g_map_story) {
        map_atlas_bind(story, len);
        map_marks_bind(story, len);
        map_model_reset();
        g_map_story = story;
    }
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
 | strip_trailing_prompt
 | Description: The length `str` should be written at once its own trailing
 |   "> " prompt is peeled off: trailing spaces, then a single trailing '>',
 |   then any spaces that preceded it. A chunk with no '>' at its (space-
 |   trimmed) end is returned unchanged -- the strip only fires on the shape an
 |   input request actually prints.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: str -- text to measure; slen -- its length
 | Returns: the length to write, which may be 0
 ----------------------*/
static size_t strip_trailing_prompt(const char *str, size_t slen) {
    size_t n = slen;
    while (n > 0 && str[n - 1] == ' ') n--;
    if (n == 0 || str[n - 1] != '>') return slen;
    n--;
    while (n > 0 && str[n - 1] == ' ') n--;
    return n;
}

/*----------------------
 | saturn_writestr
 | Description: The interpreter's text-output hook: writes to the console,
 |   with the story's own trailing "> " prompt stripped first since the input
 |   line prints its own, and feeds the ORIGINAL, unstripped text to the
 |   room-music classifier so Dynamic mix still sees what the room describes.
 | Author: suinevere
 | Dependencies: console.h, music.h
 | Globals: N/A
 | Params: str -- text to emit; slen -- its length
 | Returns: N/A
 ----------------------*/
extern "C" void saturn_writestr(const char *str, size_t slen) {
    size_t n = strip_trailing_prompt(str, slen);
    if (n > 0) console_write(str, (unsigned int) n);
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
/*----------------------
 | menu_ramp_down / menu_ramp_up / menu_ramp_cut
 | Description: The gameplay half of an in-game menu's fade. ramp_down takes the
 |   whole screen to black over the same fields the title-screen menus use, so
 |   that the page opening next -- which fades itself in from black, exactly as
 |   it does under the mode menu -- is preceded by a ramp rather than by a pop.
 |   ramp_up is the other end, and is called from the bottom of the frame loop
 |   rather than from beside ramp_down: the reveal has to land on the first frame
 |   that has a whole gameplay screen composed for it, or the ramp is spent
 |   lighting a half-drawn one (see `reveal_owed`). It advances exactly one frame
 |   when there is no ramp to run, so the loop's frame count is the same either
 |   way.
 |
 |   ramp_cut is the exit for the paths that must not stay black: every menu
 |   choice that submits a command ends the turn, and the interpreter -- and, for
 |   Save and Load, its own device and slot pickers -- draws at normal
 |   brightness, so a screen left held black would simply hide it. It releases
 |   the hold with no ramp, on a screen cleared first so the menu's own text is
 |   not what flashes back.
 |
 |   All three are inert when the fade length is 0, which is what the netbin sets
 |   and what a build that wants the old instant menus would set.
 | Author: suinevere
 | Dependencies: menu.h (menu_fade_out/menu_fade_in/menu_fade_clear/menu_clear,
 |   g_menu_page_fade), SRL
 | Globals: g_menu_page_fade
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void menu_ramp_down(void) {
    if (g_menu_page_fade > 0) menu_fade_out(g_menu_page_fade);
}

static void menu_ramp_up(void) {
    if (g_menu_page_fade > 0) menu_fade_in(g_menu_page_fade);
    else                      SRL::Core::Synchronize();
}

static void menu_ramp_cut(void) {
    if (g_menu_page_fade <= 0) return;
    menu_clear();
    menu_fade_clear();
}

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
 | Description: Starts an armed picture change at the prompt rather than waiting
 |   out its settle -- and then leaves, which is the whole of it.
 |
 |   It used to run the transition to completion here: ninety frames of ramp with
 |   the game frozen, so that the old screen faded out, the picture swapped at the
 |   bottom, and the turn's text was drawn onto the new one. That ordering was the
 |   only thing the wait bought, and it cost the player a second and a half at
 |   every room boundary that changed a picture.
 |
 |   The fade never needed it, because the fade does not touch the text.
 |   title_bg_dyn_fade drives colour offset channel B, which carries the picture
 |   and the backdrop; the console and the marble chrome are on channel A and do
 |   not move -- the split exists precisely so a transition cannot blink the words
 |   out mid-sentence. So the turn's text is drawn at once and read at full
 |   brightness while the picture dims out behind it, swaps, and comes back up,
 |   and the read loop's own per-frame music_tick is what advances the ramp. The
 |   player types through all of it.
 |
 |   Walking on before a ramp finishes does not strand it on the wrong room:
 |   on_art_commit shows g_art_room, which on_text_room rewrites on every room
 |   change, so a ramp begun two rooms ago still puts up the room the player is
 |   standing in now.
 |
 |   Only the picture's half is flushed. A track change is left on its own
 |   counter deliberately -- committing one at every prompt would restart the
 |   music in every room of a corridor, which is what the settle exists to
 |   prevent, and nothing on screen is waiting for it either way.
 |
 |   The opening room commits outright instead: main holds the screen black until
 |   the first prompt reveals it, so a ramp there would be spent fading black into
 |   black, and what the reveal uncovers should already be the new room.
 | Author: suinevere
 | Dependencies: music.h
 | Globals: g_intro_reveal
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void run_room_transition(void) {
    if (!music_transition_active()) return;
    if (g_intro_reveal) { music_transition_skip_fade(); return; }
    if (music_transition_art()) music_transition_flush();
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
 |   words, enters the room model into the map (the model itself is refreshed by
 |   mojozork.c before it calls music_on_turn, which reads it -- once per prompt,
 |   not per frame, because it walks the object tree), resets the command panel
 |   (cp_reset) every turn alongside k's own per-turn clear -- command_edit
 |   only resets it on its own completed submit, so a turn that ends any other
 |   way (a menu's Save Game row, a quick-save/restore key, toggling to the
 |   keyboard mid-build) would otherwise leave a half-built sentence to prefix
 |   the next turn's pick, and cp_reset moves nothing the player can see, so this
 |   keeps the panel's own selection across prompts as it already kept the
 |   keyboard picker's, and positions the view at the TOP of the turn's output so
 |   a long response reads from its start.
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
 |   The same three blocking sites, and the keyboard-interface branch of the
 |   editor split, each take the item picture down first. render_command_panel
 |   is the only thing that ever puts one up and the only thing that takes it
 |   down, so any path that stops running it leaves NBG1 latched -- and NBG1
 |   sits above the marble and the wallpaper, so a treasure would sit on top
 |   of the Options menu, the map reached from it, the controls and sound
 |   pages, and the software keyboard. item_art_hide early-outs on an already
 |   blank pane, so the calls cost nothing on the frames that do not need
 |   them. The archive itself is left resident: it is bound to the overlay's
 |   own lifetime by render_command_panel, and the player is still inside that
 |   overlay while a menu is up over it.
 | Author: suinevere
 | Dependencies: console.h, console_view.h, command_view.h, room_model.h,
 |   keyboard.h, saturn_keyboard.h, input.h, menu.h, menu_pages.h, soft_reset.h,
 |   sound.h, music.h, typeahead.h, dash_view.h (dash_hold), video/item_art.h,
 |   SRL
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
    /* Not refreshed here: mojozork.c refreshes the model immediately before it
       calls music_on_turn, which reads it, and a second refresh in the same
       prompt would feed the player-object inference a duplicate sample. */
    map_model_enter(room_model_get());

    static KeyboardState k;
    static CommandPanel cpanel;
    static int kbd_inited = 0;
    if (!kbd_inited) { keyboard_reset(&k); kbd_inited = 1; }
    k.input_len = 0;
    k.input[0] = '\0';
    k.cursor = 0;
    k.submitted = 0;
    cp_reset(&cpanel);
    console_scroll_to_output();
    // A prompt entered straight out of a menu -- Save and Load are the two that
    // leave the frame loop to get here -- still has that menu's box on screen:
    // its letters are in the text shadow, the image window is still aimed at its
    // rectangle, and NBG2 still holds its marble, because ~MenuBacking owes all
    // three to the next frame that changes the text. Nothing below draws until
    // the loop's first pass, so the debounce frame under it used to repaint the
    // strip over that marble and leave the box's letters on bare backdrop
    // colour -- the hollow box dash_hold_latch exists to prevent, arrived at by
    // a second claimant rather than by expiry, which is the one way the latch
    // cannot stop. Clearing the box's text here is what fires the owed teardown,
    // so the marble, the window and the letters end on one frame together.
    // Gated on the latch so an ordinary turn, which has no chrome to take down,
    // keeps its own composed screen through the debounce instead of blinking
    // its input strip empty once per command.
    if (dash_hold_latched()) {
        menu_clear();
        render_console();
    }
    dash_hold();
    SRL::Core::Synchronize();
    int sug_index = 0;
    char sug_last[256] = "";
    /* Set by every branch that opened a menu and left the screen held black.
       Spent at the bottom of the frame loop rather than where it is set, because
       that is the first point a whole gameplay frame -- console, strip and all --
       is composed for the ramp to reveal; revealing at the call site would spend
       the ramp on a half-drawn screen and snap the rest in at the end of it. */
    /* Seeded from the debt a save or restore hook left behind: its pickers ran
       inside the interpreter's turn and ended on black, and this is the first
       frame on the far side of that turn with a whole gameplay screen composed
       for the ramp to reveal. */
    bool reveal_owed = (g_screen_owed != 0);
    g_screen_owed = 0;
    /* The game hand-off's ramp, taken off g_intro_reveal here and spent at the
       bottom of the loop for the same reason reveal_owed is: the opening frame is
       not complete until the loop has drawn the input strip under the text, and
       running it here lit the words alone and then popped the panel in on the
       first frame after the ramp. One shot: every later prompt finds this null. */
    void (*intro_reveal)(void) = g_intro_reveal;
    g_intro_reveal = 0;
    for (;;) {
      while (!k.submitted) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind != SATURN_KEY_NONE) g_kbd_visible = false;
        bool pad = (ke.kind == SATURN_KEY_NONE);
        if (pad && g_pad->AnyPressed()) g_kbd_visible = true;
        pad_repeat_update();
        chord_tick();

        /* g_menu_reopen is the pause menu coming back after the save or restore
           it sent, which the interpreter ran in a turn of its own -- so the two
           entries differ only in what the screen is already doing: a keypress
           arrives on a lit gameplay frame and has to ramp it down, a re-open
           arrives on the black the hook left and must not, since menu_fade_out
           starts at full brightness and would flash the room back on first. */
        bool menu_back = (g_menu_reopen != 0);
        if (menu_back || (pad && g_pad->WasPressed(Button::START))
            || ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_F10) {
            g_menu_reopen = 0;
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
            item_art_hide();
            music_duck();
            if (!menu_back) menu_ramp_down();
            else            reveal_owed = false;   // the debt is the menu's now
            int om = options_menu();
            mode_toggle_reset();
            if (om != OM_SAVE && om != OM_RESTORE) music_resume();
            ensure_typeahead();
            typeahead_scan_screen(g_typeahead_root);
            // Room text is the story's own state, so a change only takes hold by
            // being typed at it. Save/Load own this turn, so it waits for the next
            // one through the same single-shot the Load path uses.
            const char *vcmd = (g_verbosity != verb_was) ? verbosity_command() : nullptr;
            // Every exit below that submits a command ends the turn and hands the
            // screen to the interpreter, which draws at normal brightness. None
            // of them comes back through the frame loop, so none of them can be
            // revealed there and each has to release the black itself. Only the
            // exits that return to this same prompt are ramped back up.
            //
            // Save and Load are the two that release it somewhere else. Their
            // device and slot pickers fade IN from black -- choose_device arms
            // g_menu_intro_fade, which is live in game because main leaves
            // g_menu_page_fade set -- so releasing here handed them a screen
            // already lit, and their ramp then drove the picture and the backdrop
            // alone while the box and its text sat at full brightness on the
            // black. The hooks own the release at the far end of their own
            // pickers now, exactly as they already own music_resume.
            const bool hook_owns_screen = (om == OM_SAVE || om == OM_RESTORE);
            if (!hook_owns_screen) {
                if (vcmd != nullptr) menu_ramp_cut();
                else                 reveal_owed = true;
            }
            /* Picked from the pause menu, so the pause menu is where the player
               came from and where they go back to: the hook's own prompt reads
               this and opens it again. The command panel's Save/Load rows and the
               function keys submit the same two commands without setting it, and
               land in the room as they always did. */
            if (om == OM_SAVE || om == OM_RESTORE) g_menu_reopen = 1;
            if (om == OM_SAVE)    { if (vcmd) g_verb_pending = 1; submit_command(k, "save");    continue; }
            if (om == OM_RESTORE) { if (vcmd) g_verb_pending = 1; submit_command(k, "restore"); continue; }
            if (vcmd) { submit_command(k, vcmd); continue; }
            continue;
        }
        if (ke.kind == SATURN_KEY_F11) {
            item_art_hide();
            menu_ramp_down();
            keyboard_controls_page();
            mode_toggle_reset();
            menu_clear();
            reveal_owed = true;
            continue;
        }
        if (ke.kind == SATURN_KEY_F12) {
            if (music_cdda_has_audio() || sound_has_audio()) {
                item_art_hide();
                menu_ramp_down();
                sound_options_page();
                mode_toggle_reset();
                menu_clear();
                reveal_owed = true;
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
            item_art_hide();
            typeahead_edit(k, g_typeahead_root, sug_index, sug_last, ke, pad, selected, cw_len);
            pad_scroll_update();
            render_console();
            render_keyboard(k, selected, cw_len);
        }
        // The frame is composed but not yet pushed, which is exactly what the
        // ramp wants: its own first Synchronize is what carries it to VRAM,
        // under the black the menu left behind. Every other frame just syncs.
        if (reveal_owed)       { reveal_owed = false; menu_ramp_up(); }
        else if (intro_reveal) { void (*r)(void) = intro_reveal; intro_reveal = 0; r(); }
        else                   SRL::Core::Synchronize();
        sound_service();
        music_tick();
      }
      /* A first prompt the loop never reached the bottom of -- a key genuinely
         held as the game comes up can take one of the branches above straight
         out of it -- has no composed frame to ramp, and every one of those exits
         releases the hold itself. Drop the ramp rather than run it onto a screen
         somebody else has already lit. */
      intro_reveal = 0;
      while (k.input_len > 0 && k.input[k.input_len - 1] == ' ') k.input[--k.input_len] = '\0';
      g_scroll = 0;
      history_push(k.input);
      console_write("> ", 2);
      console_write(k.input, (unsigned int) k.input_len);
      console_write("\n", 1);
      g_output_start = console_total_lines();
      render_console();
      // No ramp around these two, unlike the F10/F11/F12 menus: a confirm box
      // draws at normal brightness and fades itself neither way, so a screen
      // taken to black ahead of one would simply hide the question. The
      // Synchronize that used to sit at the end of each is gone instead -- it
      // was a frame nobody drew and nobody held, which is where the marble
      // expired from under the box's still-lit text; the `continue` reaches the
      // loop's own render one frame sooner without it.
      if (is_reboot_command(k.input)) {
          confirm_return_to_title("reboot back to the title screen?");
          mode_toggle_reset();
          k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; k.submitted = 0;
          continue;
      }
      if (is_quit_command(k.input)) {
          confirm_return_to_title("quit back to the title screen?");
          mode_toggle_reset();
          k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; k.submitted = 0;
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
 |   scratch buffer and the seek time small. The retry delay calls dash_hold
 |   before each Synchronize, since opcode_save reaches this before any menu
 |   paints and would otherwise blank the still-displayed strip out from under
 |   itself; dash_hold is a no-op on opcode_restore's already-cleared screen.
 | Author: suinevere
 | Dependencies: app_state.h (g_story_filename), dash_view.h (dash_hold), SRL
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
        for (int i = 0; i < 8; i++) { dash_hold(); SRL::Core::Synchronize(); }
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
 |
 |   render_console only redraws the console's own rows, which stop short of the
 |   gamepad strip's ten rows when it is up, so its last turn's rose/word/command
 |   lists would otherwise sit there frameless forever. Blanked once before the
 |   loop instead of held: the session is over, so nothing should look like a
 |   live control surface under the halt message. Harmless when the strip was
 |   never up (main()'s CD-load-failure path already cleared the screen).
 | Author: suinevere
 | Dependencies: console.h, console_view.h, text_map.h, SRL
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
    for (int r = TOP_MARGIN + console_height(); r < console_screen_rows(); r++) text_clear_line(r);
    while (1) { render_console(); SRL::Core::Synchronize(); }
}

/*----------------------
 | saturn_save_tail
 | Description: See saturn_glue.h. How much room past the story blob a caller
 |   should leave for what this file appends to it.
 | Author: suinevere
 | Dependencies: map_model.h (MAP_BLOB_MAX)
 | Globals: N/A
 | Params: N/A
 | Returns: the byte count
 ----------------------*/
extern "C" uint32_t saturn_save_tail(void) { return (uint32_t) MAP_BLOB_MAX; }

/*----------------------
 | legacy_map_name
 | Description: The companion record's name under the old two-file scheme: the
 |   slot's own name with an 'M' appended. Kept so saves written before the two
 |   were merged still restore their map, and so a save over one of them can
 |   retire the orphan rather than leave a stale map paired with a fresh save.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: name -- the slot's record name; out -- receives the companion name
 | Returns: N/A
 ----------------------*/
static void legacy_map_name(const char *name, char *out) {
    int i = 0;
    while (name[i] && i < 10) { out[i] = name[i]; i++; }
    out[i++] = 'M';
    out[i] = 0;
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
 |
 |   The map goes beside the save in a companion record named for the slot plus
 |   an 'M'. When that companion cannot be written the old one is deleted: the
 |   player is told "Saved." on the strength of the game blob alone, and a stale
 |   map left paired with a fresh save is one a later restore has no way to
 |   recognise and loads as authoritative.
 | Author: suinevere
 | Dependencies: save_ui.h (choose_device/pick_slot_and_name/make_slot_name),
 |   saturn_backup.h, map_model.h, menu.h, music.h (music_resume), SRL
 | Globals: g_save_device, g_save_slot, g_last_device, g_last_slot
 | Params: data -- the blob to write, appended to in place; len -- its length;
 |   cap -- how much of data is writable, so the map can go after the blob
 | Returns: 1 on success, 0 on cancel or failure
 ----------------------*/
extern "C" int saturn_save_blob(uint8_t *data, uint32_t len, uint32_t cap) {
    int device, slot;
    int interactive = 0;
    char comment[12];
    char name[12];
    if (g_save_slot >= 0) {
        device = g_save_device; slot = g_save_slot;
        g_save_device = -1; g_save_slot = -1;
        make_slot_name(name, slot);
        if (!saturn_bup_info(device, name, comment))
            snprintf(comment, sizeof(comment), "Save %d", slot + 1);
    } else {
        // The pause menu hands this a screen it has already ramped down; the
        // command panel's own Save row and a typed "save" hand it a lit one. The
        // picker below fades in from black either way -- menu_select's one-shot
        // forces the level whether or not anyone is holding it -- so a lit screen
        // jumped to black on the picker's first frame instead of falling to it.
        if (!title_bg_fade_engaged()) menu_ramp_down();
        interactive = 1;
        device = choose_device("SAVE - device?");
        // choose_device is the one picker here that does NOT fade itself out --
        // choose_dest wraps it and owns that for the restore side -- so the cancel
        // has to, or the debt below would ramp up a screen already lit and flash
        // it black first.
        if (device < 0) {
            if (g_menu_page_fade) menu_fade_out(g_menu_page_fade);
            g_screen_owed = 1; music_resume(); return 0;
        }

        // Between the two pickers, as choose_dest does between its own: each
        // fades itself up from the black the one before it left.
        if (g_menu_page_fade) menu_fade_out(g_menu_page_fade);
        g_menu_intro_fade = g_menu_page_fade;
        if (!pick_slot_and_name(device, &slot, comment, 8)) { g_screen_owed = 1; music_resume(); return 0; }
        if (comment[0] == 0) snprintf(comment, sizeof(comment), "Save %d", slot + 1);

        make_slot_name(name, slot);
        char existing[12];
        if (saturn_bup_info(device, name, existing)) {
            char q[40];
            snprintf(q, sizeof(q), "Overwrite \"%s\"?", existing);
            // Up out of the picker's black on the same one-shot the pickers
            // themselves use, and back down to black for the report below.
            g_menu_intro_fade = g_menu_page_fade;
            int keep = menu_confirm(q, "Are you sure?") ? 1 : 0;
            menu_ramp_down();
            if (!keep) { g_screen_owed = 1; music_resume(); return 0; }
        }
    }

    // The map goes into the same record, straight after the story blob, in the
    // slack the caller left for it. A map that will not serialise, or will not
    // fit, simply is not written -- the save itself is what matters, and a
    // restore that finds no map resets rather than presenting a stale one.
    uint32_t total = len;
    if (data != nullptr && cap > len) {
        unsigned int mlen = map_model_serialize(data + len, (unsigned int) (cap - len));
        total = len + (uint32_t) mlen;
    }
    int ok = saturn_bup_write(device, name, comment, data, total);
    if (ok) {
        // Any companion left by the old two-file scheme is an orphan now, and an
        // orphan is worse than nothing: the record it belonged to has just been
        // overwritten, so its map is stale, and the loader below would rather
        // reset the map than hand back a map of somewhere else.
        char mname[12];
        legacy_map_name(name, mname);
        saturn_bup_delete(device, mname);
    }
    if (ok) { g_last_device = device; g_last_slot = slot; }
    {
        // menu_message composes without pushing, which is exactly what a ramp
        // wants: the picker path reveals the composed box on the fade's own first
        // frame and takes it back down after, and the quick-save path -- which
        // never left the screen dark -- draws it over the room as it always did.
        MenuBacking backing;
        menu_message("SAVE", ok ? "Saved." : "Save FAILED (no space?).",
                     "(press any key/button)");
        if (interactive && g_menu_page_fade) menu_fade_in(g_menu_page_fade);
        menu_wait();
    }
    if (interactive) { menu_ramp_down(); g_screen_owed = 1; }
    music_resume();
    return ok;
}

/*----------------------
 | saturn_load_blob
 | Description: The interpreter's restore hook. A pre-picked slot (armed by "Load
 |   Save Game") is consumed once; otherwise it runs choose_dest to resolve a
 |   device and slot. Reads the blob, records the device/slot as the quick-key
 |   target on success, and reports an empty slot to the player.
 |
 |   Then the map, which now travels in the same record: save_blob_len finds
 |   where the story blob ends and the map begins after it. A save written before
 |   the two were merged has no tail, and falls back to reading the old companion
 |   record -- whose first two bytes are zeroed before the read, because
 |   saturn_bup_read reports success without a length and a shorter record than
 |   asked for would leave the header map_model_serialize_len reads as stack
 |   garbage. Either way a map that loads is handed to map_model_rebind_exits,
 |   since it stores positions only and without the story's exits back in place
 |   the restored map draws marks with no trail between them. A map that is
 |   missing or refused resets the model rather than presenting a stale one.
 | Author: suinevere
 | Dependencies: save_ui.h (choose_dest/make_slot_name), saturn_backup.h,
 |   map_model.h, menu.h, music.h (music_resume)
 | Globals: g_restore_device, g_restore_slot, g_last_device, g_last_slot
 | Params: buf -- destination for the blob; maxlen -- its capacity, which is
 |   cleared before the read and is what bounds the search for the map
 | Returns: 1 on success, 0 on cancel or failure
 ----------------------*/
extern "C" int saturn_load_blob(uint8_t *buf, uint32_t maxlen) {
    int device, slot;
    int interactive = 0;
    if (g_restore_slot >= 0) {
        device = g_restore_device; slot = g_restore_slot;
        g_restore_device = -1; g_restore_slot = -1;
    } else {
        // As in saturn_save_blob: the picker fades in from black, so a caller
        // that handed over a lit screen owes it a ramp down first.
        if (!title_bg_fade_engaged()) menu_ramp_down();
        int picked = choose_dest("RESTORE - device?", "RESTORE - slot?", &device, &slot);
        // choose_dest ends held black on every one of its exits, and the black is
        // kept rather than released: the room that comes back is not the room on
        // screen, so the reveal belongs to the prompt that draws the new one --
        // or to the pause menu, when the pick came from there and is going back.
        // It used to cut straight back to the outgoing room here, which is the
        // pop this exists to stop.
        interactive = 1;
        g_screen_owed = 1;
        if (!picked) { music_resume(); return 0; }
    }
    char name[12];
    make_slot_name(name, slot);
    // Cleared before the read so a record with no map leaves zeroes where a map
    // header would be. saturn_bup_read reports success without a length, so what
    // sits past the story blob is otherwise whatever the scratch allocation held,
    // and a stray MAP_BLOB_MAGIC in it would be decoded as a map.
    if (buf != nullptr && maxlen) memset(buf, 0, maxlen);
    int ok = saturn_bup_read(device, name, buf);
    if (ok) {
        uint32_t slen = save_blob_len(buf, maxlen);
        int have_map = 0;
        if (slen && slen < maxlen && buf[slen] == MAP_BLOB_MAGIC)
            have_map = map_model_deserialize(buf + slen,
                                             map_model_serialize_len(buf + slen));
        if (!have_map) {
            // Written before the two were merged: the map is its own record.
            char mname[12];
            unsigned char mblob[MAP_BLOB_MAX];
            legacy_map_name(name, mname);
            mblob[0] = 0; mblob[1] = 0;
            if (saturn_bup_read(device, mname, mblob))
                have_map = map_model_deserialize(mblob, map_model_serialize_len(mblob));
        }
        if (have_map) map_model_rebind_exits();
        else          map_model_reset();
    }
    if (ok) { g_last_device = device; g_last_slot = slot; }
    if (!ok) {
        // Nothing is coming back to reveal, so the failure gets a ramp of its
        // own out of the picker's black and the black is put back after it for
        // whatever the debt above hands the screen to.
        MenuBacking backing;
        menu_message("RESTORE", "No save in that slot.", "(press any key/button)");
        if (interactive && g_menu_page_fade) menu_fade_in(g_menu_page_fade);
        menu_wait();
        if (interactive) menu_ramp_down();
    }
    music_resume();
    return ok;
}
