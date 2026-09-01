/*----------------------
 | term.c
 | Description: A minimal telnet terminal over a cui_transport: drains received
 |   bytes to the console (defensively swallowing any telnet IAC command
 |   sequences, which the multizork server only sends in reply to our own) and
 |   sends a typed line to the server with its trailing newline.
 |   Platform-independent -- all I/O goes through the transport and the console.
 |
 |   Also strips multizorkd's out-of-band frames out of the stream and publishes
 |   what they carried. This file owns the byte stream and therefore the framing;
 |   it deliberately does not know what a room id means. It parks a number in
 |   TermState and whoever cares decides -- see online.cxx.
 |
 |   The roster is the one thing it publishes rather than parks, and only
 |   because there is nothing to decide: a seat's number, room and username are
 |   already the whole meaning, and routing them through TermState would need a
 |   fresh flag per seat and a reader that drained them before the next frame
 |   arrived. The room id keeps its flag because acting on it costs a model
 |   update that must happen once per move and not once per frame.
 | Author: suinevere
 | Dependencies: term.h, party.h, console.h (console_write), string.h
 ----------------------*/
#include "term.h"
#include "party.h"
#include "console.h"
#include <string.h>

/*----------------------
 | TELNET_IAC
 | Description: The Telnet "interpret as command" lead byte.
 | Author: suinevere
 ----------------------*/
#define TELNET_IAC 0xFF

/*----------------------
 | term_init
 | Description: Clears the terminal state (the IAC byte-skip counter).
 | Author: suinevere
 ----------------------*/
void term_init(TermState *t) {
    t->iac_skip = 0;
    t->armed = 0;
    t->saw_output = 0;
    t->quiet = 0;
    t->oob_active = 0;
    t->oob_len = 0;
    t->oob_seen = 0;
    t->oob[0] = 0;
    t->room_id = 0;
    t->room_id_fresh = 0;
}

/*----------------------
 | term_request_room_id
 | Description: Asks the server for out-of-band room ids. See term.h.
 | Author: suinevere
 | Dependencies: cui_transport.h
 | Globals: N/A
 | Params: tr -- the open transport
 | Returns: N/A
 ----------------------*/
void term_request_room_id(const cui_transport_t *tr) {
    static const uint8_t will[3] = { TELNET_IAC, 251 /* WILL */, ZATURN_TELOPT };
    cui_transport_send(tr, will, 3);
}

/*----------------------
 | hexval
 | Description: One uppercase hex digit's value, or -1 for anything else. Used
 |   to reject a malformed frame rather than silently decoding it as a room the
 |   server never named.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: c -- the character
 | Returns: 0..15, or -1
 ----------------------*/
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/*----------------------
 | oob_finish
 | Description: Decodes a completed out-of-band frame. Three forms exist and
 |   anything else is dropped without comment, so a future server that adds a
 |   frame type does not make an older client print garbage:
 |
 |     R HHHH             the local player's room object id
 |     S d                which seat of the instance the local player holds
 |     P d HHHH name      one seat's room and username, name possibly empty
 |
 |   A seat frame with an empty name is a seat nobody is in. The server reports
 |   the seat's new state rather than that somebody left, so a client that missed
 |   the join still ends up agreeing with it.
 |
 |   R is kept beside P rather than folded into it although the local player has
 |   a seat of their own: R is what the map model is driven from and it must
 |   arrive whether or not the server is one that knows about seats, which every
 |   build before this one was.
 | Author: suinevere
 | Dependencies: party.h
 | Globals: N/A
 | Params: t -- terminal state holding the captured payload
 | Returns: N/A
 ----------------------*/
static void oob_finish(TermState *t) {
    unsigned int v = 0;
    int i;

    if (t->oob_len == 5 && t->oob[0] == 'R') {
        for (i = 1; i <= 4; i++) {
            int d = hexval(t->oob[i]);
            if (d < 0) return;
            v = (v << 4) | (unsigned int) d;
        }
        t->room_id = v;
        t->room_id_fresh = 1;
        return;
    }

    if (t->oob_len == 2 && t->oob[0] == 'S') {
        if (t->oob[1] < '0' || t->oob[1] >= '0' + PARTY_SEATS) return;
        party_set_self(t->oob[1] - '0');
        return;
    }

    if (t->oob_len >= 6 && t->oob[0] == 'P') {
        char name[PARTY_NAME_MAX];
        int n = t->oob_len - 6;
        if (t->oob[1] < '0' || t->oob[1] >= '0' + PARTY_SEATS) return;
        for (i = 2; i <= 5; i++) {
            int d = hexval(t->oob[i]);
            if (d < 0) return;
            v = (v << 4) | (unsigned int) d;
        }
        if (n > PARTY_NAME_MAX - 1) n = PARTY_NAME_MAX - 1;
        for (i = 0; i < n; i++) name[i] = t->oob[6 + i];
        name[n] = '\0';
        party_set(t->oob[1] - '0', (unsigned short) v, name);
    }
}

/*----------------------
 | term_mark_output
 | Description: Arms the settle tracker for the response to a command just sent.
 | Author: suinevere
 | Dependencies: term.h
 | Globals: N/A
 | Params: t -- terminal state
 | Returns: N/A
 ----------------------*/
void term_mark_output(TermState *t) {
    t->armed = 1;
    t->saw_output = 0;
    t->quiet = 0;
}

/*----------------------
 | term_output_settled
 | Description: Reports the frame on which the armed response is judged complete
 |   and disarms, so one command anchors the view once and later unsolicited
 |   traffic leaves it alone.
 | Author: suinevere
 | Dependencies: term.h
 | Globals: N/A
 | Params: t -- terminal state; settle_frames -- quiet frames that end a response
 | Returns: 1 on the settling frame, 0 otherwise
 ----------------------*/
int term_output_settled(TermState *t, int settle_frames) {
    if (!t->armed || !t->saw_output) return 0;
    if (t->quiet < settle_frames) return 0;
    t->armed = 0;
    return 1;
}

/*----------------------
 | term_service
 | Description: Pulls up to `max_bytes` received bytes and writes them to the
 |   console, which handles \r, \n, and wrapping. A telnet IAC byte marks the next
 |   two bytes as a command to swallow (defensive; the server does not send them).
 |   Budgeted per call so a flood cannot starve the rest of the frame loop.
 | Author: suinevere
 | Dependencies: console.h, the transport rx callbacks
 | Globals: N/A
 | Params: t -- terminal state; tr -- transport to read; max_bytes -- read budget
 | Returns: the number of bytes consumed this call
 ----------------------*/
int term_service(TermState *t, const cui_transport_t *tr, int max_bytes) {
    int consumed = 0;
    while (consumed < max_bytes && cui_transport_rx_ready(tr)) {
        uint8_t c = cui_transport_rx_byte(tr);
        consumed++;
        if (t->iac_skip > 0) {
            t->iac_skip--;
            continue;
        }
        if (c == TELNET_IAC) {
            t->iac_skip = 2;
            continue;
        }
        /* Out-of-band frame. Nothing between the markers reaches the console --
           an unterminated one is abandoned at the buffer cap and its bytes are
           dropped rather than replayed, because replaying them would put the
           very control characters this framing exists to hide onto the screen. */
        if (t->oob_active) {
            if (c == TERM_OOB_END) {
                if (t->oob_len >= 0) oob_finish(t);
                t->oob_active = 0;
            } else if (++t->oob_seen > TERM_OOB_DRAIN_MAX) {
                /* No terminator in sight, so this was never a frame -- give up
                   and let the bytes after it print. A stray 0x01 costs a few
                   swallowed characters instead of the rest of the session. */
                t->oob_active = 0;
            } else if (t->oob_len >= 0) {
                /* Too long to be the frame we understand. Keep discarding to the
                   terminator rather than resuming mid-payload, which would put
                   the frame's own tail on screen. */
                if (t->oob_len >= TERM_OOB_MAX) t->oob_len = -1;
                else t->oob[t->oob_len++] = (char) c;
            }
            continue;
        }
        if (c == TERM_OOB_START) {
            t->oob_active = 1;
            t->oob_len = 0;
            t->oob_seen = 0;
            continue;
        }
        {
            char ch = (char)c;
            console_write(&ch, 1);
        }
    }
    if (consumed > 0) { t->saw_output = 1; t->quiet = 0; }
    else              { t->quiet++; }
    return consumed;
}

/*----------------------
 | term_submit_line
 | Description: Sends the keyboard's current line to the server, echoing it onto
 |   the prompt line first (there is no server echo) and appending a newline both
 |   on screen and on the wire, then resets the keyboard for the next line.
 | Author: suinevere
 | Dependencies: console.h, keyboard (keyboard_reset), the transport send callback
 | Globals: N/A
 | Params: tr -- transport to send on; k -- keyboard state holding the line
 | Returns: N/A
 ----------------------*/
void term_submit_line(const cui_transport_t *tr, KeyboardState *k) {
    int len = k->input_len;
    if (len > 0)
        console_write(k->input, (unsigned int)len);
    console_write("\n", 1);
    if (len > 0)
        cui_transport_send(tr, (const uint8_t*)k->input, len);
    cui_transport_send(tr, (const uint8_t*)"\n", 1);
    keyboard_reset(k);
}
