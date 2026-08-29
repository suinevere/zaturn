/*----------------------
 | netbin_pages.h
 | Description: The netbin build's own screens -- the dialer (which is also its
 |   root screen), the gamepad and keyboard Controls pages reached from it, and
 |   the in-session pause menu with its Display and Gameplay pages. Lifted from
 |   menu_pages.cxx so the netbin links these rather than the whole Options page
 |   set; see
 |   docs/superpowers/specs/2026-07-25-netbin-minimal-design.md.
 | Author: suinevere
 | Dependencies: menu.h, input.h, console_view.h, options.h, app_state.h,
 |   keyboard.h, menu_layout.h, saturn_keyboard.h, soft_reset.h
 ----------------------*/
#ifndef NETBIN_PAGES_H
#define NETBIN_PAGES_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | netbin_dial_page
 | Description: The netbin's root screen: the server dial-number editor, driven
 |   by a real keyboard or by the on-screen grid under pad control, with Dial
 |   and Controls rows below the grid. Seeds its edit buffer from g_dialnum.
 |   Dial validates with valid_dialnum, commits into g_dialnum, calls
 |   options_save() and returns so the caller can connect; an invalid buffer
 |   keeps the page open with an inline error. Controls opens controls_dispatch
 |   in place and comes back here. There is no Cancel row and Start/Esc do
 |   nothing: this page is the root, so there is nowhere to back out to.
 | Author: suinevere
 | Dependencies: keyboard.c, saturn_keyboard.h, soft_reset.h, options.c
 |   (valid_dialnum, options_save), menu.c, console_view.c
 | Globals: g_dialnum
 | Params: N/A
 | Returns: N/A -- returns only once g_dialnum holds a committed valid number
 ----------------------*/
void netbin_dial_page(void);

/*----------------------
 | netbin_pause_menu
 | Description: The in-session pause menu, opened with Start from the telnet
 |   terminal: Resume, Display, Gameplay, Controls, Restart. Resume, B and Start
 |   all close it; there is nothing here to cancel, since each page commits or
 |   discards its own edits before returning. Display and Gameplay are the
 |   menu_pages.cxx pages minus what the netbin has no use for -- no Dynamic
 |   palette, no dimming. Restart runs the same confirm the soft-reset chord
 |   does and, if accepted, never returns.
 |
 |   The session stays live behind it. Nothing is paused: the server keeps
 |   playing and bytes keep arriving, so the caller must register a
 |   menu_set_service pump before opening this or the UART's FIFO overruns
 |   within a dozen bytes.
 | Author: suinevere
 | Dependencies: menu.h (menu_set_service), display.h, options.h, app_state.h,
 |   soft_reset.h
 | Globals: g_display, g_difficulty, g_verbosity
 | Params: N/A
 | Returns: N/A -- returns when the player resumes, or not at all on Restart
 ----------------------*/
void netbin_pause_menu(void);

#ifdef __cplusplus
}
#endif

#endif
