/*----------------------
 | party.h
 | Description: Who else is in the game and where they are standing. multizorkd
 |   seats up to four people in one instance and tells each client the whole
 |   roster out of band, so the map can show every player rather than only the
 |   one holding the pad. Pure state -- term.c fills it from the wire and
 |   map_view reads it. Implemented in party.c.
 |
 |   It is linked into both builds although only the netbin ever fills it. An
 |   empty roster is the ordinary state for a disc playing on its own, and a map
 |   that asks the same question in both builds needs no second code path for
 |   the single-player case: it finds nobody and falls back to naming the room
 |   the local player is in.
 |
 |   A seat's room is an object number in the story the client already has --
 |   the same numbering the local player's own room arrives in -- so a name for
 |   it costs nothing beyond room_model_object_name. What the roster cannot do
 |   is place a room the local map has never seen: on a difficulty that draws
 |   only what has been walked into, a player standing somewhere you have not
 |   been is named in the list and has no mark on the map, which is the honest
 |   answer rather than a guess at where they are.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef PARTY_H
#define PARTY_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | PARTY_SEATS / PARTY_NAME_MAX
 | Description: The seats one multizorkd instance holds, matching that server's
 |   MULTIZORK_SEATS_PER_GAME, and the room a username needs -- fifteen
 |   characters and a terminator, matching its Connection::username. Both are
 |   the server's numbers rather than ours; a frame naming a seat past the end
 |   is dropped by party_set rather than growing the table to fit it.
 | Author: suinevere
 ----------------------*/
#define PARTY_SEATS     4
#define PARTY_NAME_MAX  16

/*----------------------
 | party_reset
 | Description: Empties the roster and forgets which seat is ours. Called per
 |   dial, for the reason map_model_reset is: the server hands out a fresh
 |   instance as readily as it reconnects you to an old one, and a roster
 |   carried across dials would sometimes belong to another game.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room, g_name, g_self
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void party_reset(void);

/*----------------------
 | party_set
 | Description: Records one seat. An empty name or a zero room empties the seat,
 |   which is how a player leaving is reported -- the server sends the seat's new
 |   state rather than a departure, so a client that missed the join still ends
 |   up agreeing with it. A seat number outside the table is dropped.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room, g_name
 | Params: seat -- 0 to PARTY_SEATS-1; room -- object number, 0 for none; name
 |   -- null-terminated, truncated to PARTY_NAME_MAX-1
 | Returns: N/A
 ----------------------*/
void party_set(int seat, unsigned short room, const char *name);

/*----------------------
 | party_set_self / party_self
 | Description: Which seat the local player holds, as the server named it, and
 |   -1 before it has said. The map draws that seat's mark as the player's own
 |   and stands the figure beside it; every other seat is somebody else.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_self
 | Params: seat -- 0 to PARTY_SEATS-1, or -1 for none
 | Returns: self returns the seat, or -1
 ----------------------*/
void party_set_self(int seat);
int  party_self(void);

/*----------------------
 | party_seat
 | Description: One seat's occupant. The name is a pointer into the roster and
 |   stays valid until the next party_set or party_reset for that seat, which is
 |   longer than any caller holds it: the map prints it and moves on.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room, g_name
 | Params: seat -- 0 to PARTY_SEATS-1; room -- receives the object number, may
 |   be null; name -- receives the username, may be null
 | Returns: 1 when the seat is occupied, 0 when empty or out of range
 ----------------------*/
int party_seat(int seat, unsigned short *room, const char **name);

/*----------------------
 | party_count
 | Description: How many seats are occupied. Zero is the ordinary state offline,
 |   and is what tells the map to name the local player's room by itself instead
 |   of listing a roster.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_name
 | Params: N/A
 | Returns: 0 to PARTY_SEATS
 ----------------------*/
int party_count(void);

#ifdef __cplusplus
}
#endif
#endif /* PARTY_H */
