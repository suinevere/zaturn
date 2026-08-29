/*----------------------
 | test_term_oob.c
 | Description: Host test for term.c's out-of-band frame parser. Feeds bytes
 |   through term_service via a mock transport and checks the two things that
 |   matter: the room id comes out, and not one byte of a frame ever reaches the
 |   console. The second is the one worth testing -- a parser that misses an id
 |   costs a stale rose, a parser that leaks costs visible garbage in the
 |   middle of the game's prose.
 |
 |   Splitting a frame across two term_service calls is not a contrived case: at
 |   9600 baud a seven-byte frame straddles a frame boundary most of the time,
 |   and ZATURN_RX_BUDGET caps each call regardless.
 | Author: suinevere
 | Build: gcc -std=c11 -Wall -o /tmp/tto.exe saturn/tests/test_term_oob.c \
 |          saturn/src/net/term.c -I saturn/src -I saturn/src/net \
 |          -I saturn/src/video -I saturn/src/input && /tmp/tto.exe
 ----------------------*/
#include "term.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* --- console stub: records everything term.c would have printed ----------- */
static char g_console[4096];
static unsigned int g_console_len;

void console_write(const char *text, unsigned int len) {
    unsigned int i;
    for (i = 0; i < len && g_console_len < sizeof(g_console) - 1; i++)
        g_console[g_console_len++] = text[i];
    g_console[g_console_len] = '\0';
}

/* --- keyboard stub: term_submit_line resets the line, and nothing here calls
       it, but the object still needs the symbol. --------------------------- */
void keyboard_reset(KeyboardState *k) { (void) k; }

/* --- mock transport: hands out a fixed byte string ------------------------ */
static const uint8_t *g_rx;
static int g_rx_len, g_rx_pos;
static uint8_t g_tx[64];
static int g_tx_len;

static bool mock_ready(void *ctx) { (void) ctx; return g_rx_pos < g_rx_len; }
static uint8_t mock_byte(void *ctx) { (void) ctx; return g_rx[g_rx_pos++]; }
static int mock_send(void *ctx, const uint8_t *d, int n) {
    int i;
    (void) ctx;
    for (i = 0; i < n && g_tx_len < (int) sizeof(g_tx); i++) g_tx[g_tx_len++] = d[i];
    return n;
}
static bool mock_conn(void *ctx) { (void) ctx; return true; }

static cui_transport_t mock(void) {
    cui_transport_t t;
    memset(&t, 0, sizeof t);
    t.rx_ready = mock_ready;
    t.rx_byte = mock_byte;
    t.send = mock_send;
    t.is_connected = mock_conn;
    t.ctx = NULL;
    return t;
}

static void feed(TermState *t, const cui_transport_t *tr,
                 const uint8_t *bytes, int len, int budget) {
    g_rx = bytes; g_rx_len = len; g_rx_pos = 0;
    while (g_rx_pos < g_rx_len) term_service(t, tr, budget);
}

static void reset(TermState *t) {
    term_init(t);
    g_console_len = 0;
    g_console[0] = '\0';
    g_tx_len = 0;
}

static int failures = 0;
static void check(const char *name, int ok, const char *detail) {
    if (ok) { printf("ok   %s\n", name); }
    else    { printf("FAIL %s\n  %s\n", name, detail); failures++; }
}

int main(void) {
    cui_transport_t tr = mock();
    TermState t;

    /* --- a whole frame between two runs of prose -------------------------- */
    {
        static const uint8_t s[] = {
            'W','e','s','t', 0x01,'R','0','0','B','4',0x02, 'h','o','m','e'
        };
        reset(&t);
        feed(&t, &tr, s, (int) sizeof s, ZATURN_RX_BUDGET);
        check("whole frame yields the id", t.room_id_fresh && t.room_id == 0xB4,
              "id did not arrive");
        check("whole frame leaves no trace", strcmp(g_console, "Westhome") == 0,
              g_console);
    }

    /* --- the same frame split across three calls -------------------------- */
    {
        static const uint8_t s[] = {
            'a', 0x01,'R','0','0', '5','1',0x02, 'b'
        };
        reset(&t);
        feed(&t, &tr, s, (int) sizeof s, 3);   /* forces a mid-frame boundary */
        check("split frame yields the id", t.room_id_fresh && t.room_id == 0x51,
              "id did not survive being split");
        check("split frame leaves no trace", strcmp(g_console, "ab") == 0, g_console);
    }

    /* --- an unterminated frame is abandoned, not replayed ----------------- */
    {
        static const uint8_t s[] = {
            'x', 0x01,'R','9','9','9','9','9','9','9','9','9','9', 'y'
        };
        reset(&t);
        feed(&t, &tr, s, (int) sizeof s, ZATURN_RX_BUDGET);
        check("runaway frame prints nothing it swallowed",
              strchr(g_console, '9') == NULL, g_console);
        check("runaway frame does not publish an id", !t.room_id_fresh,
              "a malformed frame set an id");
    }

    /* --- a malformed payload is dropped, not decoded ---------------------- */
    {
        static const uint8_t s[] = { 0x01,'R','0','0','G','4',0x02 };
        reset(&t);
        feed(&t, &tr, s, (int) sizeof s, ZATURN_RX_BUDGET);
        check("non-hex payload is refused", !t.room_id_fresh,
              "'G' was decoded as a hex digit");
    }

    /* --- an unknown frame type is dropped silently ------------------------ */
    {
        static const uint8_t s[] = { 'p', 0x01,'Z','1','2','3','4',0x02, 'q' };
        reset(&t);
        feed(&t, &tr, s, (int) sizeof s, ZATURN_RX_BUDGET);
        check("unknown frame type is ignored", !t.room_id_fresh, "decoded a 'Z' frame");
        check("unknown frame type still prints nothing",
              strcmp(g_console, "pq") == 0, g_console);
    }

    /* --- IAC still works, and does not eat a following frame -------------- */
    {
        static const uint8_t s[] = {
            0xFF, 253, 178, 0x01,'R','0','0','4','B',0x02, 'k'
        };
        reset(&t);
        feed(&t, &tr, s, (int) sizeof s, ZATURN_RX_BUDGET);
        check("IAC reply is swallowed and the frame after it survives",
              t.room_id_fresh && t.room_id == 0x4B && strcmp(g_console, "k") == 0,
              g_console);
    }

    /* --- a stray start marker gives up rather than eating the session ----- */
    {
        uint8_t s[80];
        int i, n = 0;
        s[n++] = 'A';
        s[n++] = TERM_OOB_START;              /* a corrupted byte, not a frame */
        for (i = 0; i < TERM_OOB_DRAIN_MAX + 4; i++) s[n++] = 'z';
        s[n++] = 'B';
        reset(&t);
        feed(&t, &tr, s, n, ZATURN_RX_BUDGET);
        check("a stray marker stops swallowing eventually",
              strchr(g_console, 'B') != NULL,
              "the terminal went silent for good after one stray byte");
        check("a stray marker does not publish an id", !t.room_id_fresh,
              "decoded an id out of noise");
    }

    /* --- the handshake is three bytes on the wire ------------------------- */
    {
        reset(&t);
        term_request_room_id(&tr);
        check("handshake is IAC WILL ZATURN_TELOPT",
              g_tx_len == 3 && g_tx[0] == 0xFF && g_tx[1] == 251 && g_tx[2] == ZATURN_TELOPT,
              "wrong bytes sent");
    }

    printf("\n");
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("test_term_oob: OK\n");
    return 0;
}
