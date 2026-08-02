/*----------------------
 | loading_screen.h
 | Description: The post-selection loading screen: shown once the player has
 |   picked a game, and held for the whole of the real CD read that follows.
 |   Pauses the title-menu CD-DA track, fades in a retro-OS boot screen together
 |   with LOADCD.PCM, and types out the boot sequence (loading_text.h) with the
 |   game's title substituted in. The screen wears the player's own Display
 |   Options background and text colours; only the wallpaper is set aside, since
 |   the backdrop sits behind it and the boot text would otherwise type over the
 |   picture.
 |   Typing is skippable with any button or key (same check menu_wait() uses):
 |   the remaining text fills in at once.
 |
 |   It comes in three parts rather than one call because the thing it is
 |   covering -- the story-file read, and the sound-blorb read after it -- takes
 |   as long as it takes, and only main() knows when that is done. begin() puts
 |   the screen up and leaves it up, lit, with the cue playing; tick() is called
 |   through the load to keep the cue going round; end() fades picture and sound
 |   away together and restores the player's display colours. The alternative,
 |   fading out before the load and reading behind a black screen, meant the
 |   player watched several seconds of nothing at the exact moment the machine
 |   looked most like it had died.
 |
 |   end() leaves the screen held black on purpose: main() calls menu_fade_clear
 |   to cut to the game's first frame, and that is what releases the hold.
 | Author: suinevere
 | Dependencies: loading_text.h, loading_music.h, menu.h, options.h,
 |   app_state.h, input.h, saturn_keyboard.h, music.h, display.h, title.h, SRL
 ----------------------*/
#ifndef LOADING_SCREEN_H
#define LOADING_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | loading_screen_begin
 | Description: Pauses the menu track, blacks the screen out, takes the player's
 |   background and text colours and sets their wallpaper aside, loads LOADCD.PCM,
 |   and fades the boot block in while typing it. Returns with the screen at
 |   full brightness, the text on it, and the cue playing -- the caller does its
 |   loading under that and must finish with loading_screen_end.
 | Author: suinevere
 | Dependencies: loading_text.h, loading_music.h, menu.h, options.h,
 |   app_state.h, input.h, saturn_keyboard.h, music.h, title.h, SRL
 | Globals: g_display, g_pad
 | Params: name -- the story's 8.3 disc filename ("ZORK1.Z3"), not its display
 |   title: the block quotes a LOAD line and a LOAD line names a file. Must not
 |   be NULL
 | Returns: N/A
 ----------------------*/
void loading_screen_begin(const char *name);

/*----------------------
 | loading_screen_tick
 | Description: Prints the cue's loop state to the held screen in DEBUG builds,
 |   and does nothing at all in a release one. Call from the caller's own frame
 |   loop between begin and end -- it does not Core::Synchronize, so it does not
 |   impose a frame rate on a caller that is mostly blocked on the CD anyway.
 |
 |   Nothing needs calling to keep the screen or its cue alive. This used to be
 |   what serviced LOADCD.PCM, and that was the bug: a cue serviced by callers is
 |   only serviced where somebody remembered a call, and the load is made of
 |   multi-second blocking operations with nowhere inside them to put one. The
 |   loop is issued from the V-blank interrupt now (see loading_music.h), and
 |   what is left here is the readout that showed it.
 | Author: suinevere
 | Dependencies: loading_music.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void loading_screen_tick(void);

/*----------------------
 | loading_screen_end
 | Description: Fades the boot block and the cue out together, frees the cue's
 |   Low Work RAM, and restores the player's text colour, backdrop and
 |   wallpaper. Leaves the screen held black for main()'s menu_fade_clear (see
 |   the header comment). Must be called once for every loading_screen_begin.
 | Author: suinevere
 | Dependencies: loading_music.h, menu.h, options.h, app_state.h, SRL
 | Globals: g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void loading_screen_end(void);

#ifdef __cplusplus
}
#endif
#endif /* LOADING_SCREEN_H */
