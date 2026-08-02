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
typedef enum { NET_OK = 0, NET_NO_MODEM, NET_DIAL_FAIL } net_connect_result_t;

/*----------------------
 | net_connect_open / net_connect_transport / net_connect_close
 | Description: Open dials `dial_number` and reports the result; transport returns
 |   the live link's cui_transport (NULL if none is open); close hangs up (safe to
 |   call when nothing is open).
 | Author: suinevere
 ----------------------*/
net_connect_result_t net_connect_open(const char *dial_number);
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
