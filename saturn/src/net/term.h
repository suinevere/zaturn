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

/*----------------------
 | ZATURN_TELOPT / TERM_OOB_START / TERM_OOB_END / TERM_OOB_MAX
 | Description: The private telnet option this client negotiates to ask
 |   multizorkd for out-of-band room ids, the bytes that frame one, and the
 |   longest payload the parser will buffer before giving up on a frame. Must
 |   match multizorkd.c's constants of the same names -- there is no shared
 |   header between the Saturn build and the server.
 | Author: suinevere
 ----------------------*/
#define ZATURN_TELOPT   178
#define TERM_OOB_START  0x01
#define TERM_OOB_END    0x02
#define TERM_OOB_MAX    8

/*----------------------
 | TERM_OOB_DRAIN_MAX
 | Description: How many bytes past a start marker the parser keeps waiting for
 |   the terminator before deciding this was never a frame. Without a bound a
 |   single corrupted byte that happened to be 0x01 would swallow the rest of the
 |   session in silence, which is far worse than printing a few stray characters.
 |   Generous against the seven-byte frame it is guarding.
 | Author: suinevere
 ----------------------*/
#define TERM_OOB_DRAIN_MAX 32

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
    int          oob_active;   /* inside a frame, between the two markers */
    int          oob_len;      /* bytes kept, or -1 once the frame ran too long */
    int          oob_seen;     /* bytes since the start marker, for the give-up bound */
    char         oob[TERM_OOB_MAX];
    unsigned int room_id;      /* last id the server sent */
    int          room_id_fresh;/* set on arrival, cleared by whoever acts on it */
} TermState;

/*----------------------
 | term_init / term_service / term_submit_line
 | Description: init clears the state; service drains up to max_bytes to the
 |   console (returning the count consumed); submit_line echoes and sends the
 |   keyboard's line with a newline, then resets it.
 | Author: suinevere
 ----------------------*/
void term_init(TermState *t);

/*----------------------
 | term_request_room_id
 | Description: Sends the one-time IAC WILL that asks the server to start
 |   reporting room ids. Costs three bytes and is safe against any server: one
 |   that does not know the option either ignores it or answers IAC WONT, which
 |   term_service already swallows. Call once, right after the link opens.
 | Author: suinevere
 | Dependencies: cui_transport.h
 | Globals: N/A
 | Params: tr -- the open transport
 | Returns: N/A
 ----------------------*/
void term_request_room_id(const cui_transport_t *tr);
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
