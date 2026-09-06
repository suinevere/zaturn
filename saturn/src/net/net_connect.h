/*----------------------
 | net_connect.h
 | Description: The dial-up connection front end for online play: open a modem
 |   connection to a number, get the byte transport for the live link, and hang up.
 |   Implemented in net_connect.c.
 | Author: suinevere
 | Dependencies: cui_transport.h (the transport the caller reads/writes)
 ----------------------*/
#ifndef NET_CONNECT_H
#define NET_CONNECT_H
#include "cui_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | net_connect_result_t
 | Description: The outcome of net_connect_open: connected, no modem detected, or
 |   the dial failed (distinguished so the caller redials only when it might help).
 | Author: suinevere
 ----------------------*/
typedef enum { NET_OK = 0, NET_NO_MODEM, NET_DIAL_FAIL, NET_CANCELLED }
    net_connect_result_t;

/*----------------------
 | net_connect_poll_fn
 | Description: What net_connect_open_poll calls while it waits on the dial. The
 |   caller runs one frame of its own from here -- polling the pad, redrawing the
 |   box -- and answers whether the player has asked to give up.
 | Author: suinevere
 | Params: ctx -- the caller's opaque pointer, passed through untouched
 | Returns: nonzero to abandon the call
 ----------------------*/
typedef int (*net_connect_poll_fn)(void *ctx);

/*----------------------
 | net_connect_open / net_connect_transport / net_connect_close
 | Description: Open dials `dial_number` and reports the result; transport returns
 |   the live link's cui_transport (NULL if none is open); close hangs up (safe to
 |   call when nothing is open).
 | Author: suinevere
 ----------------------*/
net_connect_result_t net_connect_open(const char *dial_number);

/*----------------------
 | net_connect_open_poll
 | Description: net_connect_open with a way out. The dial is the one step here
 |   that can hold the machine for half a minute -- the modem is off dialling and
 |   training, and nothing arrives on the wire until it has an answer -- so this
 |   is the only step that gives `poll` the frames it needs to see a button. Probe
 |   and init are each a couple of seconds at worst and are still run blind.
 |
 |   A cancelled dial is aborted the way the AT command set says to abort one:
 |   any character sent to a modem that is dialling ends the call. Then it hangs
 |   up anyway, so the line is left idle whether or not the modem took the hint.
 | Author: suinevere
 | Dependencies: saturn_uart16550.h, modem.h, transport_uart.h
 | Globals: g_uart, g_transport, g_open
 | Params: dial_number -- the number to dial; poll -- called between reads while
 |   the dial is outstanding, may be NULL; ctx -- passed to poll
 | Returns: NET_OK, NET_NO_MODEM, NET_DIAL_FAIL, or NET_CANCELLED
 ----------------------*/
net_connect_result_t net_connect_open_poll(const char *dial_number,
                                           net_connect_poll_fn poll, void *ctx);
const cui_transport_t *net_connect_transport(void);
void net_connect_close(void);

/*----------------------
 | net_connect_reset
 | Description: Forces the modem back to command mode and hangs up, discarding
 |   any call already in progress. Exists for the netbin build, which is loaded
 |   *by* the PlanetWeb browser over this same modem and therefore inherits a
 |   live data session: in data mode the modem treats AT as payload, so
 |   modem_probe() would fail and net_connect_open() would report NET_NO_MODEM
 |   on a perfectly good modem. Safe and cheap when the line is already idle,
 |   and safe when no modem is present at all. The CD build does not call it.
 | Author: suinevere
 | Dependencies: saturn_uart16550.h, modem.h
 | Globals: g_uart, g_open
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void net_connect_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* NET_CONNECT_H */
