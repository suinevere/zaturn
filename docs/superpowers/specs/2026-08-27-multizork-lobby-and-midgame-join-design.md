# Design: multizork lobby, named rooms, and mid-game joining

**Date:** 2026-08-27
**Status:** Approved, pending implementation plan
**Touches:** `saturn/multizorkd.c`, new `saturn/roomnames.h`,
`saturn/tests/test_multizork_join.py`, new `saturn/tests/test_multizork_lobby.py`
**Supersedes the entry-point behaviour of:** commit 6521e7f, which taught the
first prompt to accept a game code. That prompt goes away; see "Retiring the
hello-sailor code path".

## Goal

Replace multizorkd's three-item text menu with a lobby: a named, browsable list
of rooms you can join, plus a list of games you have played before and can
return to. Rooms get memorable word names instead of six random characters, can
be hidden from the lobby, and — in Part 2 — can be joined by a new player after
the game has already started.

## Current flow

A connection opens on `inpfn_hello_sailor`, which accepts either a blank line
(go make a name) or a code. A code is tried first as a live instance hash, then
as a player rejoin hash. After `inpfn_enter_name` the player gets
`inpfn_new_game_or_join`: `1` starts a game, `2` prompts for a code, `3` quits.

A new game is an `Instance` with a six-character `hash` from
`generate_unique_hash()`, which retries against the permanent `used_hashes`
table. That hash is the game code the host reads out to friends. Player rejoin
codes come from the same generator and the same table, which is why the first
prompt has to guess which kind of code it was handed.

At `go`, `start_instance()` flattens the connected players to the front of
`inst->players`, freezes `inst->num_players`, and builds each player's Z-machine
state. Joining after that point is refused outright.

## Part 1 — the lobby

### The first prompt is `username:`

A connection opens directly on the name prompt. `Connection.pending_join` and
the "that's a game code, not an access code" special case are both deleted.

The bot blocklist currently in `inpfn_hello_sailor` (`system`, `shell`,
`enable`, `root`, …) moves onto the username prompt, since that is where port
scanners now land. Losing it would un-fix a live abuse problem, so it moves
before anything else in that function is touched.

### Retiring the hello-sailor code path

Commit 6521e7f exists because a first-time arrival's only code is the game code,
and the first prompt refused it. Asking for a username first dissolves that
ambiguity by construction: you type your name, then a room name at the lobby.

`tests/test_multizork_join.py` asserts the old behaviour and must be rewritten
to assert the room name works at the *lobby* prompt. It keeps its second case —
the menu route — unchanged in spirit. The commit message says plainly that an
entry point was removed and why.

### Room names

`Instance.hash` widens from `char[8]` to `char[24]` and holds an
`adjective-noun` pair drawn from two wordlists in a new `saturn/roomnames.h`:
`brass-lantern`, `rusty-mailbox`, `white-house`. Uniqueness rides the existing
`generate_unique_hash()` retry against `used_hashes`, so a name is burned once
used and never comes back. 48 adjectives and 48 nouns give 2,304 plain pairs;
after 20 collisions the generator appends a hyphen and two digits
(`brass-lantern-47`), which raises the space to about 230,000 names and makes
the search terminate rather than spin once plain pairs run short.

Every word in both lists is at most 9 characters, which bounds the longest
composed name at 22 characters plus a NUL inside the 24-byte field. A longer
word would truncate a name silently, so the wordlists are declared as
fixed-width `char[][10]` arrays: an over-long entry fails to compile. The
invariant is enforced by the language, not by a comment asking future editors
to be careful.

Per-player rejoin codes stay six random characters. `Player.hash[8]` is
unchanged.

`Connection.pending_join[8]` is deleted along with the prompt that filled it.

Legacy six-character instance hashids in an existing database keep working.
They are simply odd-looking room names. The `strlen(hash) != 6` guard in
`find_live_instance_by_hash()` is removed, and the function is renamed
`find_live_instance_by_name()`.

Matching is case-insensitive via `strcasecmp()` (POSIX `<strings.h>`; the daemon
is already POSIX-only). Generated names are always lowercase, so a generated
name can never collide case-insensitively with a legacy mixed-case hashid.

### Privacy

`instances.private integer not null default 0`. Private rooms are absent from
the lobby's joinable list and reachable only by typing their name.

Privacy does **not** gate the "Your games" list. A game you played is yours to
see regardless.

`SQL_CREATE_TABLES` uses `create table if not exists`, so an existing database
will not gain the column. `db_init()` issues an `alter table instances add
column private integer not null default 0` whose "duplicate column name" error
is tolerated. Same idiom for every column this design adds.

### The lobby screen

Replaces `inpfn_new_game_or_join`:

```
Games waiting for players:
  1) brass-lantern    seanie, ashley
  2) white-house      bob

Your games:
  3) rusty-mailbox    with ashley, bob        (in progress)
  4) dented-lamp      with seanie             (left 3 days ago)

Type a number, or a room name if someone gave you one.
  n) start a new room     q) quit
```

When both sections are empty the screen says nobody is playing and offers only
`n` and `q`. That is the "default" case: create a room, or resume one of yours.
One screen covers both; there is no separate empty state.

**Joinable** means live, not private, and `seat_available_for(inst)` returns
true. In Part 1 that predicate is `!inst->started && has_vacant_seat(inst)`.
Part 2 changes only this function.

`seat_available_for()` answers "may a stranger sit down here" and nothing else.
Privacy is checked by the lobby before it calls the predicate, not inside it, so
that entering a private room by name uses the same seat rule as a listed one.
Returning to your own seat never consults it.

**Your games** is scoped by username — a join of `instances` to `players` on
`players.username = $username`. It has two kinds of row, in-progress ones first
and each kind newest `savetime` first:

1. Live started instances where your seat exists and its `connection` is NULL,
   shown `(in progress)`. This is the reconnect case `reconnect_player()`
   already handles; the lobby now surfaces it instead of requiring your code.
2. Instances in the database that are not currently live and not crashed, shown
   `(left N days ago)` from `savetime`. Liveness is decided in C against the
   connection list, since the database does not track it.

Capped at ten rows, newest `savetime` first.

### One entry point for names

`enter_room_by_name(conn, name)` is the single dispatcher. It resolves a name
case-insensitively against live instances, then against the database, and
handles joinable / started / full / private / not-found / yours-to-resume. Both
the lobby and any future prompt call it, so a private room's name works
anywhere without the lobby ever listing it.

The lobby prompt accepts a list number, a room name, or a six-character player
access code (still routed to `reconnect_player()`), in that order. Room names
are tried before player codes, preserving today's precedence.

### Creating a room

`n` creates the instance and asks one question — whether to list it in the
lobby — in a new `inpfn_new_room_privacy`, then falls into the existing
`inpfn_waiting_for_players`. The waiting-room text changes to read out the room
name instead of the hash.

## Part 2 — preallocated seats and mid-game joining

### Why preallocation is the whole trick

A player who joins mid-game needs a Z-machine continuation: a program counter,
stack, and base pointer parked on a READ instruction. `start_instance()` gets
each player's by resetting dynamic memory to the pristine story between players
and running a step. Mid-game that reset is impossible — it would wipe everyone's
progress.

Preallocation sidesteps this entirely. Run all four seats through the pristine
intro loop at `go`, while resetting memory is still legal, and the unclaimed
seats sit frozen on a valid READ indefinitely. A joiner inherits a continuation
built at the only moment it could legally be built.

### The space is already reserved

Three things that looked like obstacles are already sized for exactly four:

| Resource | Where | Capacity |
| --- | --- | --- |
| Object IDs | `ZORK1_EXTERN_MEM_OBJS_BASE` = 251, v3 caps at 255 | 251–254, four slots |
| Property tables | `get_virtualized_mem_ptr`, `0x10000 - (MULTIPLAYER_PROP_DATALEN * 5)` | four plus slack |
| Player records | `Player players[4]` in `Instance` | four |

Nothing has to grow. What gates mid-game joining today is `getObjectPtr()` and
`getObjectProperty()` refusing any slot `>= inst->num_players`, plus
`num_players` being frozen at `go`.

### Seats versus players

`inst->num_players` becomes the seat count and is always 4 for instances created
under this design. A new `Player.claimed` flag carries "a real person owns this
seat". `connection` continues to mean "that person is connected right now"; the
two are independent.

`start_instance()` builds all four seats: object data, property table, access
code, per-player globals, and an intro run each. Unclaimed seats get a
placeholder username for their ZSCII property table, rewritten on claim.

The username length cap of 15 characters gives at most 5 Z-words (10 bytes),
which the existing `assert((propsize + (numwords * 2) + 1) <=
sizeof(player->property_table_data))` already bounds. Rewriting a property table
on claim is confined to that player's own buffer.

### Hiding and revealing a seat

`start_instance()` currently *clears* INVISIBLE (attribute 0x07) and NDESCBIT
(0x0E) to make a player visible. Unclaimed seats **set** both instead, so they
stay out of room descriptions. Claiming clears them and the character
materialises. Both directions of the toggle already exist in the code.

### Claiming a seat

`seat_available_for()` in Part 2 becomes simply "some seat has `claimed == 0`",
dropping the `!inst->started` half. Started and unstarted games become joinable
by the same rule.

Claiming writes the username into the seat, rewrites its property table with the
ZSCII-encoded name, clears the two attribute bits, sets `claimed`, prints the
seat's access code, and moves the connection to `inpfn_ingame`. The access code
itself is generated at `go` along with everything else in the seat — claiming
reveals it rather than creating it, so a seat's identity never changes.

A stranger still cannot take a *claimed* seat whose owner is merely
disconnected. Only the owner returns to that, by username or access code.

### Audit list

Every loop over players needs a claimed-ness decision. These are the known
sites; the implementation plan enumerates them as individual tasks:

- `broadcast_to_instance` and `broadcast_to_room` already iterate all four
  slots. `write_to_connection` tolerates a NULL connection, but
  `db_insert_transcript(player->dbid, …)` with `dbid == 0` would log errors on
  every broadcast. `broadcast_to_room` is the worse case: dormant seats sit in
  West of House (object 180), so any broadcast to that room hits them.
- `step_instance`, `save_instance`, `db_update_player`
- the guest-list printers in `inpfn_waiting_for_players` and
  `inpfn_player_waiting`
- `reconnect_player`
- `drop_connection`'s `players_still_connected` count is already correct: it
  counts non-NULL connections, so a game still archives when the last human
  leaves.

### Legacy instances

`db_select_instance()` reads `num_players` from the instances row and refuses to
load if the player row count disagrees. Instances written before this change
carry their original seat count and correspondingly fewer rows, so they
rehydrate exactly as they do today and simply never offer a free seat. No
migration, no backfill.

## Database changes

| Table | Column | Purpose |
| --- | --- | --- |
| `instances` | `private integer not null default 0` | hide from the lobby |
| `players` | `claimed integer not null default 1` | seat has a real owner |

`claimed` defaults to 1 so legacy rows read as owned, which is what they are.
Both are added by tolerated `alter table` at `db_init()`.

One new prepared statement selects a username's games:

```sql
select i.id, i.hashid, i.savetime, i.crashed
  from instances i join players p on p.instance = i.id
 where p.username = $username and i.crashed = 0
 order by i.savetime desc limit 20;
```

Liveness filtering and the ten-row cap happen in C.

## Testing

`saturn/tests/test_multizork_lobby.py`, following the TCP-driving style of
`test_multizork_join.py` — connect to a daemon named by `MULTIZORK_ADDR`, skip
when nothing answers.

Part 1:

1. First prompt asks for a username.
2. Empty lobby offers create and quit and says nobody is playing.
3. A public room created by one connection appears in a second connection's
   lobby, listed by name with the host's username.
4. A private room does not appear in that list but is enterable by typing its
   name.
5. Name matching is case-insensitive: `BRASS-LANTERN` finds `brass-lantern`.
6. A player who disconnects from a started game sees it under "Your games" as
   `(in progress)` and rejoins by picking it.
7. A different username does not see that game.

Part 2:

8. A game started by one player is still listed as joinable, and a second
   connection joins it after `go` and reaches the `>` prompt.
9. The joining player's name is announced to the players already in the game.
10. Once all four seats are claimed, the room leaves the joinable list and a
    fifth connection typing its name is refused with a full-game message.
11. A seat whose owner is merely disconnected is not offered to a stranger:
    a started game with one claimed-but-absent seat and no unclaimed seats is
    not joinable.

`test_multizork_join.py` is rewritten for the moved entry point.

## Non-goals

- Authenticating usernames. Anyone typing `seanie` sees seanie's games and can
  sit in seanie's seat. Access codes still exist and still work, but they stop
  being the only way back in. This is a deliberate trade for a hobby telnet
  server and is called out here so it is not mistaken for an oversight.
- Taking over another player's claimed character. Resume finds your own seat by
  username; there is no character picker.
- Games larger than four players. The object-ID ceiling is 255 and Zork 1 uses
  250.
- Moving `multizorkd.c` into the `src/` tree. It sits with the vendored mojozork
  sources and stays there.

## Ghosts in the object tree: observed, not prevented

A dormant seat is a child of West of House from turn 0. INVISIBLE and NDESCBIT
keep it out of room descriptions, but Zork's thief wanders and steals from
rooms, and `take all` walks child lists.

This is **accepted, not a blocker**. A thief who can rob a dormant seat only
makes the game slightly easier. But it must not happen unobserved, so the
implementation instruments it.

`getObjectPtr()` is the single funnel for every object access in the
interpreter, and it already computes `requested_player` for any id at or above
`ZORK1_EXTERN_MEM_OBJS_BASE`. When that seat is unclaimed, it logs once per seat
per instance:

```
loginfo("Instance '%s': game logic touched unclaimed seat %d (object %u)",
        inst->hash, requested_player, objid);
```

A once-per-seat latch keeps a wandering thief from flooding the log. Two things
make this probe trustworthy:

- `remap_objectid()` only maps the player object id to `base + current_player`,
  and the interpreter is never stepped for a dormant seat, so `current_player`
  is always a claimed seat during play. Any hit on a dormant seat therefore came
  from the game walking the object tree on its own — exactly the event of
  interest.
- The hiding code in `start_instance()` reaches player objects through
  `opcode_set_attr`, which goes through `getObjectPtr()` too. A per-instance
  `ghost_watch` flag is therefore switched on only after `start_instance()`
  finishes, so setup does not trip its own probe. It cannot be keyed off
  `inst->started`, which is set *before* the intro-run loop.

## Settled: a joiner wakes in West of House

They arrive where preallocation parked them, possibly alone while the party is
in the Troll Room, and walk to catch up. It reads as "you just arrived", needs
no object-tree surgery, and is the behaviour this design commits to. Teleporting
a joiner to another player's room is not implemented.

## Build and run verification is the owner's

multizorkd needs POSIX and sqlite3 and cannot be built or exercised on the
Windows development box. The tests need a daemon the owner starts.
