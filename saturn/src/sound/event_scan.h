/*----------------------
 | event_scan.h
 | Description: The two moments that take the music away from the room's
 |   scene: losing and winning. Both are endings, not places, so they carry no
 |   background picture -- only a track that replaces the room's.
 |
 |   Neither is guessed from the turn's prose any more. Losing is the death
 |   routine's own banner, `**** You have died ****`, which appears 52 times
 |   across the thirty-two shipped stories and is printed by nothing else.
 |   Winning has no such banner in any of them, so it is not scanned for at
 |   all: music_on_win is called when the story ends itself, which for a game
 |   whose quit command is intercepted before it reaches the interpreter can
 |   only be the ending routine running to completion.
 |
 |   The keyword table this file used to carry -- treasure, gold, troll,
 |   flames -- fired on any turn that mentioned them, which made a chest of
 |   coins sound like the end of the game.
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
 | Description: The two event categories and their count. EV_NONE is not a
 |   category -- it is what event_scan returns when the turn was ordinary.
 |   The values are pool indices in music_data.c's CATEGORY_POOL, so their
 |   order is load-bearing.
 | Author: suinevere
 ----------------------*/
enum { EV_NONE = -1, EV_LOSE = 0, EV_WIN = 1 };
#define EVENT_N 2

/*----------------------
 | event_scan
 | Description: EV_LOSE when the turn's output carries the Z-machine death
 |   banner, EV_NONE otherwise. Never returns EV_WIN: winning is signalled by
 |   the interpreter ending, not by anything printed.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: text -- the turn's output text (NULL -> EV_NONE)
 | Returns: EV_LOSE or EV_NONE
 ----------------------*/
int event_scan(const char *text);

#ifdef __cplusplus
}
#endif
#endif /* EVENT_SCAN_H */
