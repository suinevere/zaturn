/*----------------------
 | term.h
 | Description: A minimal telnet terminal over a cui_transport: drain received
 |   bytes to the console and send a typed line with its newline. Implemented in
 |   term.c.
 | Author: suinevere
 | Dependencies: cui_transport.h (the link), keyboard.h (the input line)
 ----------------------*/
#ifndef TERM_H
#define TERM_H
#include "net/cui_transport.h"
#include "keyboard.h"

/*----------------------
 | ZATURN_RX_BUDGET
 | Description: Max bytes term_service drains per call, so a flood cannot starve
 |   the rest of the frame loop.
 | Author: suinevere
 ----------------------*/
#define ZATURN_RX_BUDGET 512

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | TermState
 | Description: Terminal state -- the count of bytes still to swallow after a
 |   telnet IAC command, plus the stream-settle tracking that tells the view when
 |   a server response has finished arriving. armed is set by term_mark_output and
 |   cleared once term_output_settled reports, so one submitted command anchors the
 |   view exactly once; saw_output guards against anchoring on a response that
 |   never came; quiet counts consecutive frames with no received bytes.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int iac_skip;
    int armed;
    int saw_output;
    int quiet;
} TermState;

/*----------------------
 | term_init / term_service / term_submit_line
 | Description: init clears the state; service drains up to max_bytes to the
 |   console (returning the count consumed); submit_line echoes and sends the
 |   keyboard's line with a newline, then resets it.
 | Author: suinevere
 ----------------------*/
void term_init(TermState *t);
int  term_service(TermState *t, const cui_transport_t *tr, int max_bytes);
void term_submit_line(const cui_transport_t *tr, KeyboardState *k);

/*----------------------
 | term_mark_output
 | Description: Arms the settle tracker for the response to a command just sent.
 |   Until this is called again, term_output_settled reports at most once, which is
 |   what keeps unsolicited traffic from other players from moving the page.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: t -- terminal state
 | Returns: N/A
 ----------------------*/
void term_mark_output(TermState *t);

/*----------------------
 | term_output_settled
 | Description: Reports the single frame on which the armed response is judged
 |   complete: output was seen and the stream has then been quiet for
 |   settle_frames consecutive calls. Disarms itself, so it never reports twice
 |   for one command. Call once per frame, after term_service.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: t -- terminal state; settle_frames -- quiet frames that end a response
 | Returns: 1 on the settling frame, 0 otherwise
 ----------------------*/
int term_output_settled(TermState *t, int settle_frames);

#ifdef __cplusplus
}
#endif
#endif /* TERM_H */
