/*----------------------
 | test_party_frames.c
 | Description: Drives term.c's out-of-band parser with the exact bytes
 |   multizorkd emits and checks that the roster ends up saying what the server
 |   said, that no frame byte reaches the console, and that the frames a client
 |   does not understand cost it nothing.
 |
 |   The frames here are written out as literal bytes rather than built by
 |   including the server, which cannot be compiled on every machine this test
 |   runs on. They are the contract: multizorkd.c's write_party_to and
 |   write_room_id must produce these, and term.c must read them.
 | Author: suinevere
 ----------------------*/
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "term.h"
#include "party.h"

static char  g_console[4096];
static int   g_console_len;
static const char *g_wire;
static int   g_wire_len;
static int   g_wire_at;

void console_write(const char *str, unsigned int len) {
    unsigned int i;
    for (i = 0; i < len && g_console_len < (int) sizeof g_console - 1; i++)
        g_console[g_console_len++] = str[i];
    g_console[g_console_len] = '\0';
}

void keyboard_reset(KeyboardState *k) { (void) k; }

static bool fake_ready(void *ctx) { (void) ctx; return g_wire_at < g_wire_len; }

static uint8_t fake_byte(void *ctx) {
    (void) ctx;
    return (uint8_t) g_wire[g_wire_at++];
}

static int fake_send(void *ctx, const uint8_t *d, int n) {
    (void) ctx; (void) d;
    return n;
}

static void feed(TermState *t, const char *bytes, int len) {
    cui_transport_t tr;
    memset(&tr, 0, sizeof tr);
    tr.rx_ready = fake_ready;
    tr.rx_byte  = fake_byte;
    tr.send     = fake_send;
    g_wire = bytes;
    g_wire_len = len;
    g_wire_at = 0;
    while (g_wire_at < g_wire_len)
        term_service(t, &tr, ZATURN_RX_BUDGET);
}

int main(void) {
    TermState t;
    unsigned short room = 0;
    const char *name = 0;

    term_init(&t);
    party_reset();
    g_console_len = 0;
    g_console[0] = '\0';

    assert(party_count() == 0);
    assert(party_self() == -1);

    /* One turn's worth of traffic: prose, this client's own room, its seat, and
       a roster of two. The prose is what has to survive; everything between the
       markers is what must not appear. */
    {
        static const char wire[] =
            "West of House\n"
            "\x01" "R00B4" "\x02"
            "\x01" "S1" "\x02"
            "\x01" "P000B4suinevere" "\x02"
            "\x01" "P100C8guest" "\x02"
            ">";
        feed(&t, wire, (int) sizeof wire - 1);
    }

    assert(strcmp(g_console, "West of House\n>") == 0);
    assert(t.room_id_fresh && t.room_id == 0x00B4u);
    assert(party_self() == 1);
    assert(party_count() == 2);

    assert(party_seat(0, &room, &name));
    assert(room == 0x00B4u);
    assert(strcmp(name, "suinevere") == 0);

    assert(party_seat(1, &room, &name));
    assert(room == 0x00C8u);
    assert(strcmp(name, "guest") == 0);

    assert(!party_seat(2, &room, &name));
    assert(!party_seat(3, &room, &name));
    assert(!party_seat(-1, &room, &name));
    assert(!party_seat(PARTY_SEATS, &room, &name));

    /* A seat somebody left is reported as itself emptied, not as a departure,
       so a client that missed the join still ends up agreeing with the server. */
    {
        static const char wire[] = "\x01" "P00000" "\x02";
        feed(&t, wire, (int) sizeof wire - 1);
    }
    assert(!party_seat(0, &room, &name));
    assert(party_count() == 1);

    /* A move is one frame naming the seat's new room. */
    {
        static const char wire[] = "\x01" "P10190guest" "\x02";
        feed(&t, wire, (int) sizeof wire - 1);
    }
    assert(party_seat(1, &room, &name));
    assert(room == 0x0190u);
    assert(strcmp(name, "guest") == 0);

    /* Malformed frames are dropped rather than half-applied: a bad hex digit, a
       seat past the table, and a truncated payload each leave the roster where
       it was. The name is checked too -- a parser that wrote the seat before
       validating the room would pass a room-only check. */
    {
        static const char wire[] =
            "\x01" "P1Z190ghost" "\x02"
            "\x01" "P90190ghost" "\x02"
            "\x01" "P101" "\x02"
            "\x01" "S9" "\x02";
        g_console_len = 0;
        g_console[0] = '\0';
        feed(&t, wire, (int) sizeof wire - 1);
    }
    assert(g_console_len == 0);
    assert(party_seat(1, &room, &name));
    assert(room == 0x0190u);
    assert(strcmp(name, "guest") == 0);
    assert(party_self() == 1);
    assert(party_count() == 1);

    /* A username at the full width the server allows arrives whole. */
    {
        static const char wire[] = "\x01" "P20064abcdefghijklmno" "\x02";
        feed(&t, wire, (int) sizeof wire - 1);
    }
    assert(party_seat(2, &room, &name));
    assert(strcmp(name, "abcdefghijklmno") == 0);
    assert((int) strlen(name) == PARTY_NAME_MAX - 1);

    /* A frame type nobody here knows is swallowed whole, which is what lets the
       server add one without an older client printing it. */
    {
        static const char wire[] = "ok" "\x01" "X" "whatever" "\x02" "\n";
        g_console_len = 0;
        g_console[0] = '\0';
        feed(&t, wire, (int) sizeof wire - 1);
    }
    assert(strcmp(g_console, "ok\n") == 0);

    /* And a reset empties it, which is what a fresh dial gets: the seats belong
       to one instance and the next dial may be a different one. */
    party_reset();
    assert(party_count() == 0);
    assert(party_self() == -1);

    printf("test_party_frames: ok\n");
    return 0;
}
