/*----------------------
 | event_scan.h
 | Description: The turn-by-turn danger/triumph scan: the one thing the old
 |   room-mood classifier read from printed text that survives its deletion.
 |   Danger and triumph are moments, not places -- they carry no background
 |   picture, only an event track that overrides the room's mix for the turn
 |   they fire on.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef EVENT_SCAN_H
#define EVENT_SCAN_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | EV_* / EVENT_N
 | Description: The event categories a turn's text can scan to, and their
 |   count. EV_NONE is not a category -- it is what event_scan returns when no
 |   keyword fired.
 | Author: suinevere
 ----------------------*/
enum { EV_NONE = -1, EV_DANGER = 0, EV_TRIUMPH = 1 };
#define EVENT_N 2

/*----------------------
 | event_scan
 | Description: Looks for an event keyword (combat, death, treasure, etc.) in
 |   the turn's text, returning the first match's category so an event track
 |   can override the room's base mix for that turn.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: text -- the turn's output text (NULL -> EV_NONE)
 | Returns: EV_DANGER, EV_TRIUMPH, or EV_NONE
 ----------------------*/
int event_scan(const char *text);

#ifdef __cplusplus
}
#endif
#endif /* EVENT_SCAN_H */
