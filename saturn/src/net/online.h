/*----------------------
 | online.h
 | Description: The "Play Online" mode -- dialing the NetLink modem, running the
 |   multizork telnet terminal, and the Zork I typeahead the terminal shares with
 |   the local game.
 | Author: suinevere
 | Dependencies: net/net_connect.h, console_view.h, input.h, typeahead.h, menu.h
 ----------------------*/
#ifndef ONLINE_H
#define ONLINE_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | ensure_online_typeahead
 | Description: Builds the online terminal's typeahead trie from the local
 |   ZORK1.Z3 on the disc (multizork is Zork I, so it shares that dictionary,
 |   grammar and solution overlay). Idempotent, and rebuilt when the difficulty
 |   changes.
 |
 |   Called by online_mode once the carrier is up, and deliberately nowhere else.
 |   This trie is the largest single object in Low Work RAM -- larger than the boot
 |   jingle -- so building it at the splash would spend most of the zone on a feature
 |   most sessions never reach, at the expense of the art cache and the game's own
 |   trie.
 | Author: suinevere
 | Dependencies: typeahead.h, typeahead_extract.h, typeahead_solution.h,
 |   game_catalog.h, SRL
 | Globals: g_online_ta, g_online_diff, g_difficulty
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void ensure_online_typeahead(void);

/*----------------------
 | online_typeahead_release
 | Description: Frees the online trie so the next ensure_online_typeahead rebuilds
 |   it. Call on the soft reset: at ~625 KB it is the largest thing in Low Work RAM,
 |   and a session that dialled once should not carry it into the next game.
 | Author: suinevere
 ----------------------*/
void online_typeahead_release(void);

/*----------------------
 | online_mode
 | Description: Connects to the multizork server (auto-redialing a few times,
 |   since the NetLink<->DreamPi carrier handshake is probabilistic) and runs the
 |   telnet terminal until the link drops or the player quits, then returns to the
 |   mode menu.
 | Author: suinevere
 | Dependencies: net/net_connect.h, term.h, console.h, console_view.h, input.h,
 |   keyboard.h, saturn_keyboard.h, typeahead.h, menu.h, soft_reset.h, music.h,
 |   SRL
 | Globals: g_online_ta, g_dialnum, g_scroll, g_pad, g_kbd_visible
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void online_mode(void);

#ifdef __cplusplus
}
#endif

#endif
