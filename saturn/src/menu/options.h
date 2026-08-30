/*----------------------
 | options.h
 | Description: Persistence and runtime application of the player's game
 |   options -- difficulty, online dial number, audio levels/mix/track,
 |   controller mapping, and display colors/background -- plus the display
 |   cycling used by the Display Options page. This header is plain C++ (not
 |   extern "C"-guarded): display_cycle_row's parameter is a C++ enum type, so
 |   it is included by .cxx translation units only.
 | Author: suinevere
 | Dependencies: app_state.h, input.h, display.h, saturn_backup.h, music.h
 ----------------------*/

#ifndef OPTIONS_H
#define OPTIONS_H

/*----------------------
 | DisplayCycleRow
 | Description: Which Display Options row a cycle applies to, named separately
 |   from the page's own row enum because display_cycle_row lives here.
 | Author: suinevere
 ----------------------*/
enum DisplayCycleRow { DCR_PALETTE, DCR_BG, DCR_TEXT, DCR_DIM };

/*----------------------
 | options_load
 | Description: Restores persisted game options from backup RAM (the
 |   MOJOOPTS blob) into the app_state/input globals: difficulty, online dial
 |   number, music/pcm audio levels, controller face-button and shift-chord
 |   mapping, sound mix mode and selected track, and display state. Any field
 |   absent from a missing, older, or truncated blob is left at its compiled
 |   default, so older saves remain readable. An image reference resolves
 |   against the compiled-in category tables (display_decode -> image_slot_of),
 |   not a runtime scan, so this carries no ordering requirement against disc
 |   access -- just call it before anything reads these globals.
 | Author: suinevere
 | Dependencies: saturn_backup.h, display.h, input.h, music.h
 | Globals: g_difficulty, g_dialnum, g_music_level, g_pcm_level, g_face_btn,
 |   g_chord_slot, g_verbosity, g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void options_load(void);

/*----------------------
 | options_save
 | Description: Persists the current game options (difficulty, online dial
 |   number, music/pcm audio levels, controller mapping, sound mix mode and
 |   selected track, display state) to backup RAM as the MOJOOPTS blob, in
 |   the layout options_load reads back.
 | Author: suinevere
 | Dependencies: saturn_backup.h, display.h, input.h
 | Globals: g_difficulty, g_dialnum, g_music_level, g_pcm_level, g_face_btn,
 |   g_chord_slot, g_verbosity, g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void options_save(void);

/*----------------------
 | display_apply
 | Description: Pushes the current display settings (g_display) to VDP2 so
 |   body text, menus, the on-screen keyboard, and the cursor all take the new
 |   colors, loading the selected background image when one is chosen.
 |   Returns false when the chosen background could not be shown, so a caller
 |   cycling presets can step over a bad one instead of settling on the
 |   fallback it installs.
 | Author: suinevere
 | Dependencies: display.h, title.h (title_bg_show/title_bg_hide, forward-
 |   declared here until the title module is extracted), SRL
 | Globals: g_display
 | Params: N/A
 | Returns: true if the requested display was applied; false if it fell back
 ----------------------*/
bool display_apply(void);

/*----------------------
 | display_cycle_row
 | Description: Cycles one row of the Display Options page (palette,
 |   background, text, or dim) in direction `dir` and pushes the result to
 |   VDP2, stepping past any palette entry that fails to apply (an image that
 |   will not load) so repeated cycling cannot get stuck re-selecting the same
 |   broken picture.
 | Author: suinevere
 | Dependencies: display.h
 | Globals: g_display
 | Params: which -- DCR_PALETTE, DCR_BG, DCR_TEXT, or DCR_DIM; dir -- -1 or +1
 | Returns: N/A
 ----------------------*/
void display_cycle_row(DisplayCycleRow which, int dir);

/*----------------------
 | valid_dialnum
 | Description: Whether `s` is an acceptable online dial number: non-empty,
 |   digits only, and no longer than DIALNUM_MAX (g_dialnum has no room past
 |   that).
 | Author: suinevere
 | Dependencies: app_state.h
 | Globals: N/A
 | Params: s -- candidate dial-number string
 | Returns: true if `s` passes validation
 ----------------------*/
bool valid_dialnum(const char *s);

/*----------------------
 | text_set_color
 | Description: Recolors the SGL font glyphs, the block cursor and the dim ink
 |   unselected menu rows are drawn in, by writing the VDP2 CRAM entries they
 |   are read from. Takes the background as well as the text colour because the
 |   dim ink is a mix of the two -- see text_dim_rgb in options.cxx for why it
 |   is not simply a darker text colour. Declared here (rather than kept
 |   file-local to options.cxx) because the title-screen setup in main.cxx also
 |   needs to set the initial text color before display_apply runs.
 | Author: suinevere
 | Dependencies: SRL, text_map.h (TEXT_DIM_CRAM)
 | Globals: N/A
 | Params: rgb555 -- Saturn RGB555 text color (see DISP_RGB555 in display.h);
 |   bg555 -- the background color behind it
 | Returns: N/A
 ----------------------*/
void text_set_color(unsigned short rgb555, unsigned short bg555);

#endif /* OPTIONS_H */
