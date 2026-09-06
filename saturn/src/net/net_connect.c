/*----------------------
 | net_connect.c
 | Description: The dial-up connection front end for online play: powers on the
 |   NetLink modem, detects its 16550 UART, probes/initializes the modem, dials,
 |   and on success wraps the UART in a cui_transport the terminal reads/writes.
 |   Holds the single connection's state.
 | Author: suinevere
 | Dependencies: net_connect.h, saturn_uart16550.h (UART detect/SMPC enable),
 |   modem.h (probe/init/dial/hangup), transport_uart.h (the transport wrapper)
 ----------------------*/
#include "net_connect.h"
#include "saturn_uart16550.h"
#include "modem.h"
#include "transport_uart.h"

/*----------------------
 | g_uart / g_transport / g_open
 | Description: The one live connection: the detected UART, the transport built
 |   over it, and whether a connection is currently open.
 | Author: suinevere
 ----------------------*/
static saturn_uart16550_t g_uart;
static cui_transport_t    g_transport;
static int                g_open = 0;

/*----------------------
 | MODEM_DIAL_TIMEOUT
 | Description: ~35s at 28.6MHz. Trimmed for faster retry: a successful V.34
 |   training completes well under this, so a dead attempt gives up sooner and the
 |   caller can redial.
 | Author: suinevere
 ----------------------*/
#define MODEM_DIAL_TIMEOUT 105000000u

/*----------------------
 | DIAL_POLL_SLICE
 | Description: How long dial_polled waits on the wire before handing the frame
 |   back to the caller's poll. In the same made-up units MODEM_DIAL_TIMEOUT is
 |   counted in -- saturn_uart_getc_timeout's loop counter -- where that constant
 |   calls 105,000,000 about thirty-five seconds, so this is about a sixtieth of
 |   a second and the poll gets offered every frame.
 |
 |   Small on purpose. The cost of a short slice is one extra pass round a loop
 |   that is doing nothing anyway; the cost of a long one is a button press the
 |   player has to hold until the slice ends.
 | Author: suinevere
 ----------------------*/
#define DIAL_POLL_SLICE 50000u

/*----------------------
 | detect_uart
 | Description: Powers the modem on via SMPC, then probes the two known NetLink
 |   cart-port base addresses (verbatim from the coup examples) until one responds.
 | Author: suinevere
 | Dependencies: saturn_uart16550.h
 | Globals: g_uart
 | Params: N/A
 | Returns: 1 if a UART was detected, 0 otherwise
 ----------------------*/
static int detect_uart(void) {
    static const struct { uint32_t base; uint32_t stride; } addrs[] = {
        { 0x25895001, 4 },
        { 0x04895001, 4 },
    };
    int i;
    saturn_netlink_smpc_enable();
    for (i = 0; i < 2; i++) {
        g_uart.base = addrs[i].base;
        g_uart.stride = addrs[i].stride;
        if (saturn_uart_detect(&g_uart)) return 1;
    }
    return 0;
}

/*----------------------
 | dial_polled
 | Description: modem_dial's wait, reassembled here so the caller gets a frame
 |   between reads. modem.h is vendored from coup-saturn and its own dial spins
 |   inside saturn_uart_getc_timeout for the whole thirty-five seconds without
 |   ever coming up for air, which is why a cancel gesture could be held through
 |   an entire call and never be seen; this sends the same ATDT and parses the
 |   same replies through the same modem.h helpers, and only differs in waiting
 |   one slice at a time.
 |
 |   The deadline advances only on slices that expired, matching what
 |   modem_command_timeout does when it restarts its timeout for each character:
 |   a modem still talking is a modem still working.
 | Author: suinevere
 | Dependencies: saturn_uart16550.h, modem.h
 | Globals: N/A
 | Params: uart -- the detected UART; number -- what to dial; timeout -- the
 |   whole-dial budget in getc_timeout units; poll -- may be NULL; ctx -- passed
 |   to poll; cancelled -- receives 1 if poll asked to stop
 | Returns: the modem result, MODEM_TIMEOUT_ERR on a dial that never answered
 ----------------------*/
static modem_result_t dial_polled(const saturn_uart16550_t *uart,
                                  const char *number, uint32_t timeout,
                                  net_connect_poll_fn poll, void *ctx,
                                  int *cancelled) {
    char line[MODEM_LINE_MAX];
    int      idx   = 0;
    uint32_t spent = 0;

    *cancelled = 0;
    saturn_uart_puts(uart, "ATDT");
    saturn_uart_puts(uart, number);
    saturn_uart_puts(uart, "\r");

    while (spent < timeout) {
        int c = saturn_uart_getc_timeout(uart, DIAL_POLL_SLICE);
        if (c < 0) {
            spent += DIAL_POLL_SLICE;
            if (poll != 0 && poll(ctx)) { *cancelled = 1; return MODEM_NO_CARRIER; }
            continue;
        }
        /* Leading CR/LF are the separators before the reply, not the reply, so
           a terminator with nothing behind it is dropped -- modem_read_line's
           own rule, kept because the replies being parsed are the same ones. */
        if (c == '\r' || c == '\n') {
            if (idx > 0) {
                modem_result_t r;
                line[idx] = '\0';
                idx = 0;
                r = modem_parse_response(line);
                if (r != MODEM_UNKNOWN) return r;
            }
        } else if (idx < MODEM_LINE_MAX - 1) {
            line[idx++] = (char) c;
        }
    }
    return MODEM_TIMEOUT_ERR;
}

/*----------------------
 | net_connect_open / net_connect_open_poll
 | Description: Runs the full connect sequence -- detect UART, probe and init the
 |   modem, dial -- and on a carrier connect builds the transport and marks the
 |   link open. Distinguishes a missing/unresponsive modem from a failed dial so
 |   the caller can redial only when redialing might help, and a dial the player
 |   called off from either, so the caller can say nothing about it. The poll-less
 |   form is the same call with nothing to interrupt it, so the two can never
 |   drift.
 | Author: suinevere
 | Dependencies: saturn_uart16550.h, modem.h, transport_uart.h
 | Globals: g_uart, g_transport, g_open
 | Params: dial_number -- the phone number to dial; poll/ctx -- see net_connect.h
 | Returns: NET_OK, NET_NO_MODEM, NET_DIAL_FAIL, or NET_CANCELLED
 ----------------------*/
net_connect_result_t net_connect_open(const char *dial_number) {
    return net_connect_open_poll(dial_number, 0, 0);
}

net_connect_result_t net_connect_open_poll(const char *dial_number,
                                           net_connect_poll_fn poll, void *ctx) {
    modem_result_t rc;
    int cancelled = 0;

    g_open = 0;
    if (!detect_uart())                    return NET_NO_MODEM;
    if (modem_probe(&g_uart) != MODEM_OK)  return NET_NO_MODEM;
    if (modem_init(&g_uart) != MODEM_OK)   return NET_NO_MODEM;

    rc = dial_polled(&g_uart, dial_number, MODEM_DIAL_TIMEOUT, poll, ctx, &cancelled);
    if (cancelled) {
        /* Any character ends a call the modem is still placing; the hangup
           after it is for the case where it had already finished placing it. */
        saturn_uart_puts(&g_uart, "\r");
        modem_hangup(&g_uart);
        return NET_CANCELLED;
    }
    if (rc != MODEM_CONNECT) return NET_DIAL_FAIL;

    g_transport = transport_uart_make(&g_uart);
    g_open = 1;
    return NET_OK;
}

/*----------------------
 | net_connect_transport
 | Description: Returns the active transport, or NULL when no connection is open.
 | Author: suinevere
 ----------------------*/
const cui_transport_t *net_connect_transport(void) {
    return g_open ? &g_transport : 0;
}

/*----------------------
 | net_connect_close
 | Description: Hangs up the modem and marks the link closed. Idempotent -- safe
 |   to call when nothing is open (the soft reset calls it unconditionally).
 | Author: suinevere
 | Dependencies: modem.h
 | Globals: g_uart, g_open
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void net_connect_close(void) {
    if (g_open) { modem_hangup(&g_uart); g_open = 0; }
}

/*----------------------
 | net_connect_reset
 | Description: Detects the UART, sends the guard-timed +++ escape to leave
 |   data mode, then hangs up. No-op when no UART answers. See net_connect.h
 |   for why the netbin needs this at boot.
 | Author: suinevere
 | Dependencies: saturn_uart16550.h, modem.h
 | Globals: g_uart, g_open
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void net_connect_reset(void) {
    g_open = 0;
    if (!detect_uart()) return;
    saturn_uart_init(&g_uart, MODEM_BAUD_9600);
    modem_escape_to_command(&g_uart);
    modem_hangup(&g_uart);
}
