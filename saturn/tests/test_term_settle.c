/*----------------------
 | test_term_settle.c
 | Description: Covers the terminal's stream-settle tracking, which is what lets
 |   the online view anchor on the top of a long server response instead of
 |   following the newest byte. A response arrives over several frames, so the
 |   anchor may only fire once the stream has been quiet for a settle window --
 |   and exactly once per submitted command, so another player's later chatter
 |   cannot yank the page out from under a reader.
 | Author: suinevere
 | Dependencies: term.h, console.h, net/mock_transport.h
 ----------------------*/
#include "../src/net/term.h"
#include "../src/video/console.h"
#include "net/mock_transport.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define SETTLE 4

int main(void) {
    TermState ts;
    MockTransport m;
    cui_transport_t t;

    /* A response split across frames: settle must not fire while bytes flow. */
    console_init(); term_init(&ts);
    term_mark_output(&ts);
    mock_transport_init(&m, (const uint8_t*)"West of House\n", 14);
    t = mock_transport_iface(&m);
    term_service(&ts, &t, ZATURN_RX_BUDGET);
    assert(term_output_settled(&ts, SETTLE) == 0);   /* just received, not quiet yet */

    /* Quiet frames accumulate; fires exactly once, on reaching the window. */
    mock_transport_init(&m, (const uint8_t*)"", 0);
    t = mock_transport_iface(&m);
    int fired = 0;
    for (int f = 0; f < 20; f++) {
        term_service(&ts, &t, ZATURN_RX_BUDGET);
        if (term_output_settled(&ts, SETTLE)) fired++;
    }
    assert(fired == 1);

    /* Unsolicited later output must NOT re-fire: no mark, no anchor. */
    mock_transport_init(&m, (const uint8_t*)"Bob says hello\n", 15);
    t = mock_transport_iface(&m);
    term_service(&ts, &t, ZATURN_RX_BUDGET);
    mock_transport_init(&m, (const uint8_t*)"", 0);
    t = mock_transport_iface(&m);
    for (int f = 0; f < 20; f++) {
        term_service(&ts, &t, ZATURN_RX_BUDGET);
        assert(term_output_settled(&ts, SETTLE) == 0);
    }

    /* A fresh submit re-arms it. */
    term_mark_output(&ts);
    mock_transport_init(&m, (const uint8_t*)"You are in a maze.\n", 19);
    t = mock_transport_iface(&m);
    term_service(&ts, &t, ZATURN_RX_BUDGET);
    mock_transport_init(&m, (const uint8_t*)"", 0);
    t = mock_transport_iface(&m);
    fired = 0;
    for (int f = 0; f < 20; f++) {
        term_service(&ts, &t, ZATURN_RX_BUDGET);
        if (term_output_settled(&ts, SETTLE)) fired++;
    }
    assert(fired == 1);

    /* A mark with no output at all never fires -- nothing to anchor on. */
    term_mark_output(&ts);
    for (int f = 0; f < 20; f++) {
        term_service(&ts, &t, ZATURN_RX_BUDGET);
        assert(term_output_settled(&ts, SETTLE) == 0);
    }

    printf("test_term_settle: OK\n");
    return 0;
}
