/*----------------------
 | menu_pages.h
 | Description: The Options menu and its sub-pages -- Network dial number,
 |   Controls (live remap editor + controller/keyboard controls views),
 |   Sound, Display, and Gameplay (Difficulty) -- plus the read-only Credits
 |   page, which is reached from the title mode-select menu, not from
 |   Options. Owns the option-menu UI only; persistence and runtime apply of
 |   the settings these pages edit live in options.h. Four entries are
 |   called from outside this module: the main loop's Options/F10 and
 |   Sound/F12 hotkeys, the in-game F11 Controls key, and the title
 |   mode-select menu's Credits row. Every other page is reachable only from
 |   options_menu and stays file-local to menu_pages.cxx.
 | Author: suinevere
 | Dependencies: menu.h, input.h, options.h, console_view.h, app_state.h,
 |   soft_reset.h, keyboard.h, menu_layout.h, display.h, sound.h, music.h, SRL
 ----------------------*/

#ifndef MENU_PAGES_H
#define MENU_PAGES_H

/*----------------------
 | OM_NONE / OM_SAVE / OM_RESTORE
 | Description: options_menu()'s result. OM_SAVE/OM_RESTORE mean the player
 |   picked Save Game/Load Game (shown only while g_in_game is set); the menu
 |   itself cannot serialize or restore game state, so the caller must submit
 |   the matching "save"/"restore" command through the normal input path
 |   (saturn_glue.cxx's saturn_readline does this via submit_command, the
 |   same helper the F2/F3 quick keys use). OM_NONE covers every other exit
 |   (Done, B/Esc, or Return to Title, which never returns since it calls
 |   soft_reset_to_title()).
 | Author: suinevere
 ----------------------*/
enum { OM_NONE = 0, OM_SAVE, OM_RESTORE };

/*----------------------
 | options_menu
 | Description: Opens the Options menu: Save Game and Load Game (shown only
 |   while a game is in progress), Gameplay, Display, Sound (shown only when
 |   there is audio to configure), Controls, Network, Return to Title (shown
 |   only while a game is in progress), and Done. Blocks until the player
 |   picks one. Save Game/Load Game close the menu immediately and report
 |   which was picked via the return value (see OM_SAVE/OM_RESTORE above)
 |   without performing the save/restore themselves. Return to Title
 |   confirms via menu_confirm and, on yes, soft-resets to the title screen
 |   (never returns). Every other exit returns OM_NONE. Credits is not on
 |   this menu -- it lives on the title mode-select menu (main.cxx).
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_in_game, g_difficulty
 | Params: N/A
 | Returns: OM_NONE, OM_SAVE, or OM_RESTORE
 ----------------------*/
int options_menu(void);

/*----------------------
 | keyboard_controls_page
 | Description: Physical-keyboard settings page: Insert mode (also flips
 |   whether plain or Ctrl arrows move the caret vs cycle suggestions),
 |   CapsLock, and NumLock. OK commits and saves; Cancel (B/Esc/Start)
 |   restores the snapshot taken on entry. Reached from the Options menu's
 |   Controls row (via controls_dispatch) when a real keyboard is the active
 |   device, and directly from the in-game F11 key.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: true if it exited because the player switched to a gamepad
 |   mid-page (controls_dispatch should open controls_page instead); false
 |   on a normal Ok/Cancel exit
 ----------------------*/
bool keyboard_controls_page(void);

/*----------------------
 | sound_options_page
 | Description: Sound Options page. Which rows appear depends on what the
 |   disc/game actually provide: Audio Mix / Track / Music need CD-DA;
 |   PCM level needs the loaded game's .BLB; OK/Cancel always show.
 |   Previews audio live while open; OK
 |   commits and saves, Cancel restores the snapshot including live audio.
 |   Reached from the Options menu's Sound row and directly from the in-game
 |   F12 key.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_mix_mode, g_sel_track, g_music_level, g_pcm_level
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sound_options_page(void);

/*----------------------
 | credits_page
 | Description: Read-only, paginated Credits page. Left/Right step between
 |   pages (clamped, not wrapping); any of B/A/C/Start/Enter/Esc/Backspace
 |   closes it, since there is nothing here to confirm or cancel. Reached
 |   from the title mode-select menu's Credits row (main.cxx) -- not from
 |   the Options menu.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void credits_page(void);

#endif /* MENU_PAGES_H */
