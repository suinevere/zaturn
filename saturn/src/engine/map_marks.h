/*----------------------
 | map_marks.h
 | Description: Passage marks -- Infocom's fifth legend symbol, the narrow
 |   passageway a baggage limit closes to a player carrying too much, plus the
 |   arrowhead-derived retraction of a one-way exit the drawing shows closed
 |   coming back -- scanned off Infocom's own maps for stories whose geography
 |   somebody has drawn. Pure data and lookup -- map_model.c decides when to
 |   consult it and owns whatever it draws with the answer. Implemented in
 |   map_marks.c.
 |
 |   The lookup key is (room, dir), not room alone: a baggage limit belongs to
 |   one exit out of a room, not the room itself, and a room can carry a mark
 |   on one exit while its other exits are ordinary.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef MAP_MARKS_H
#define MAP_MARKS_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | MARK_BAGGAGE
 | Description: Set when the exit is a narrow passageway closed to a player
 |   over the baggage limit -- Infocom's fifth legend symbol.
 | Author: suinevere
 ----------------------*/
#define MARK_BAGGAGE 0x01

/*----------------------
 | MARK_RETRACT
 | Description: Set when the drawing's arrowhead shows this exit does not run
 |   back the other way, so a line drawn for it must stop short of a return
 |   stroke rather than complete one.
 | Author: suinevere
 ----------------------*/
#define MARK_RETRACT 0x02

/*----------------------
 | map_marks_bind
 | Description: Selects the scanned table matching a story image, by the
 |   release number and serial in its Z-machine header, and forgets any table
 |   bound before. The same identification map_atlas_bind uses and for the
 |   same reason: object numbers are assigned by the compiler, so a table is
 |   only ever valid for the exact build it was derived from.
 |
 |   Binding no table is a normal outcome, not a failure. It is what every
 |   story nobody has drawn gets, and it leaves every exit unmarked exactly as
 |   it behaves without this module.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_marks, g_n
 | Params: story -- the story image, may be null; len -- its length
 | Returns: the number of marks bound, 0 when none matched
 ----------------------*/
int map_marks_bind(const unsigned char *story, unsigned int len);

/*----------------------
 | map_marks_for
 | Description: The mark for one exit, keyed by the room it leaves and the
 |   direction it leaves in. Scans the bound table linearly rather than
 |   bisecting: a shipped table holds a handful of rows, not the tens the
 |   atlas carries, and this is consulted once per exit at record time rather
 |   than once per drawn cell, so the saving a bisection buys is not there to
 |   buy.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_marks, g_n
 | Params: room -- object number; dir -- the exit direction; dest -- receives
 |   the destination the drawing supplies when the story's own exit hid it, 0
 |   when the story already carries one; flags -- receives MARK_BAGGAGE and
 |   MARK_RETRACT
 | Returns: 1 when the exit carries a mark, 0 otherwise
 ----------------------*/
int map_marks_for(unsigned short room, int dir, unsigned char *dest,
                   unsigned char *flags);

#ifdef __cplusplus
}
#endif
#endif /* MAP_MARKS_H */
