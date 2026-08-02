/*----------------------
 | netbin_pages.h
 | Description: The netbin build's three screens -- the dialer (which is also
 |   its root screen) and, reached from it, the gamepad and keyboard Controls
 |   pages. Lifted from menu_pages.cxx so the netbin links these three rather
 |   than the whole Options page set; see
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

#ifdef __cplusplus
}
#endif

#endif
