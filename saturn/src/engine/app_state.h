/*----------------------
 | app_state.h
 | Description: The cross-cutting runtime globals shared across the interpreter's
 |   modules -- persisted game options (difficulty, audio levels/mix, display
 |   palette, online dial number, the command-panel/keyboard interface
 |   preference and its toggle-button binding), save/restore session state
 |   (pre-picked and last-used slots, the queued auto-command), the soft-reset
 |   jump target, the story file in play, and the console scroll position.
 |   Housing g_scroll here (rather than in the input or console_view module)
 |   avoids a mutual input<->console_view header cycle: input writes it,
 |   console_view reads it, and this header is neutral C-safe ground both can
 |   include. Declarations only -- definitions live in app_state.cxx.
 | Author: suinevere
 | Dependencies: display.h
 ----------------------*/

#ifndef APP_STATE_H
#define APP_STATE_H

#include <setjmp.h>
#include <stdbool.h>
#include "display.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Difficulty (Options menu). Easy = full typeahead + winning-path hints; Medium =
   typeahead, grammar weights only; Hard = typeahead off. */
enum { DIFF_EASY = 0, DIFF_MEDIUM = 1, DIFF_HARD = 2 };

/* Room-description verbosity (Options -> Gameplay), in the parser's own three
   modes and named after the commands that set them. Ordered quietest first so
   the row's Left/Right reads as less/more text. The Z-machine's own default is
   Brief; this client ships Verbose, because a console player cannot skim back up
   a scrollback the way a terminal player can. */
enum { VERB_SUPERBRIEF = 0, VERB_BRIEF = 1, VERB_VERBOSE = 2 };

/* Online dial number (editable in Options -> Network; persisted). 11 digits is
   the longest we accept (NANP country code plus number). */
#define DIALNUM_MAX 11

// Difficulty selected in Options (DIFF_EASY/MEDIUM/HARD); gates typeahead hints
// and grammar weighting.
extern int g_difficulty;

// Room-description verbosity (VERB_*); persisted in MOJOOPTS. Applied by handing
// the parser the matching command, at game start and again whenever this changes.
extern int g_verbosity;

// Set by main() when a game starts, so the first prompt hands g_verbosity to the
// parser before anything else reaches it; cleared as it is consumed.
extern int g_verb_pending;

/* Which input interface a gamepad gets (IFACE_KEYBOARD / IFACE_PANEL). */
enum { IFACE_KEYBOARD = 0, IFACE_PANEL = 1 };

// Interface a gamepad starts a game in; persisted in MOJOOPTS and set on the
// Options > Gameplay page. Defaults to IFACE_PANEL.
extern int g_cmd_iface;

// Interface in use right now, seeded from g_cmd_iface when a game starts and
// flipped by the toggle button. Not persisted -- a tap is for this session.
extern int g_cmd_mode;

// Reserved. Used to pick which shift button carried the interface toggle (0 = Z,
// 1 = Y) before that moved onto the fixed L+R combo (mode_combo_fired). Nothing
// reads it now; it stays declared and persisted so a MOJOOPTS blob written either
// side of the change still loads on the other.
extern int g_toggle_btn;

// Set by main() when a game starts: the routine that reveals the game's opening
// frame. The screen is held black from the loading screen right through to it,
// and the first prompt calls it once -- at the only moment the opening room has
// been composed but not yet shown -- then clears it. Null when already spent.
extern void (*g_intro_reveal)(void);

/*----------------------
 | verbosity_command
 | Description: The parser command for the current g_verbosity -- the only way to
 |   set this, since the mode lives in the story's own state rather than anywhere
 |   this client can write.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_verbosity
 | Params: N/A
 | Returns: "superbrief", "brief" or "verbose"
 ----------------------*/
const char *verbosity_command(void);

/*----------------------
 | MUSIC_LEVEL_DEFAULT / PCM_LEVEL_DEFAULT
 | Description: The shipped audio levels. Named rather than left as the literals
 |   app_state.cxx seeds the globals with because the Sound page's master switch
 |   has to put them back: turning sound off drops both levels to 0, and turning
 |   it on again has nothing to restore them from -- the levels the player had
 |   before are the ones it just overwrote -- so it restores these instead.
 | Author: suinevere
 ----------------------*/
#define MUSIC_LEVEL_DEFAULT 7
#define PCM_LEVEL_DEFAULT   4

// CD-DA music volume level, 0..7 (0 = off); persisted in MOJOOPTS.
extern int g_music_level;

// PCM sound-effect volume level, 0..7 (0 = off); persisted in MOJOOPTS.
extern int g_pcm_level;

// Current display colors/background/image, applied to VDP2 by display_apply
// and persisted in MOJOOPTS.
extern DisplayState g_display;

// Online dial number text, editable in Options > Network; persisted in MOJOOPTS.
extern char g_dialnum[DIALNUM_MAX + 1];

// Save device pre-picked from "Load Save Game", applied by the first in-game
// "restore" (queued via g_autocmd) instead of the choose_dest prompt.
extern int g_restore_device;

// Save slot pre-picked from "Load Save Game", paired with g_restore_device.
extern int g_restore_slot;

// Command auto-submitted on the next readline (used to queue "restore" after a
// pre-picked load).
extern const char *g_autocmd;

// Device of the last save/restore that actually committed this session; -1
// until one commits.
extern int g_last_device;

// Slot of the last save/restore that actually committed this session; used by
// the F5/F6/F9 quick keys to skip the pickers.
extern int g_last_slot;

// Save destination pre-picked by quick-save, the save-side mirror of
// g_restore_device/g_restore_slot. One-shot.
extern int g_save_device;

// Save slot pre-picked by quick-save, paired with g_save_device. One-shot.
extern int g_save_slot;

// Set when Save Game or Load Game is picked from the in-game pause menu, so the
// prompt after the save or restore opens that menu again instead of dropping the
// player into the room. One-shot. The command panel's own Save/Load rows and the
// F2/F3/F5/F6/F9 quick keys leave it clear, and go straight back to the game.
extern int g_menu_reopen;

// Set by the save/restore hooks when they leave the screen ramped down to black,
// so the next prompt ramps the composed gameplay frame back up rather than
// cutting to it -- the same debt reveal_owed carries inside one readline, across
// the turn the interpreter takes to run the save. One-shot.
extern int g_screen_owed;

// Soft-reset jump target armed by main() just before the title screen; the
// input loops longjmp here on the reset chord or the typed "reboot" command.
extern jmp_buf g_title_jmp;

// True once g_title_jmp has been armed by setjmp and is safe to longjmp to.
extern bool g_title_jmp_armed;

/*----------------------
 | g_returned_to_title
 | Description: True when this pass through the title arrived by longjmp rather
 |   than by booting -- see app_state.cxx. Read by title_and_seed to decide
 |   whether the menu plays the opening's fixed track or draws one.
 | Author: suinevere
 ----------------------*/
extern bool g_returned_to_title;

// Story file currently loaded from CD (set by main after game selection);
// re-read by saturn_read_story_file for save/restart.
extern const char *g_story_filename;

// True once a game is loaded and running (set by main after mojo_boot
// succeeds); false before then and after a soft-reset back to the title.
// Gates the Options menu's Save Game / Load Game rows.
extern bool g_in_game;

// Console scroll offset from the live bottom, in lines (0 = latest text).
// Written by the input module (scroll_handle_key, pad_scroll_update) and read
// by console_view's render_console.
extern int g_scroll;

#ifdef __cplusplus
}
#endif

#endif /* APP_STATE_H */
