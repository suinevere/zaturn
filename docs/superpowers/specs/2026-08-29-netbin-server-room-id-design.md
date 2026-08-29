# Design: the server tells the netbin which room it is in

**Date:** 2026-08-29
**Status:** Implemented 2026-08-29. Hardware verification outstanding.
**Extends:** `docs/superpowers/specs/2026-07-25-netbin-minimal-design.md`
**Reverses, in part:** `docs/superpowers/specs/2026-08-25-netbin-direction-rose-design.md`
— that design made the rose static because the client could not know the room.
This one gives it a way to know, and keeps the static rose as the fallback for
every case where it still cannot.

## Goal

Have `multizorkd` send the connected player's room object number out of band,
once per turn, so the netbin's compass rose draws the room's real exits instead
of offering all twelve unconditionally.

Exits only. Objects in the room and the player's inventory stay the server's
business — see "What a room ID does not buy".

## Why this is cheap

Four things are already in place, and three of them are already load-bearing
elsewhere.

**We own both ends.** `saturn/multizorkd.c` is in this repo and
`docker/Dockerfile:10-12` builds the shipped image from it. There is no third
party to negotiate a protocol with.

**The server already has the number.** `gvar_location` is a per-player field
(`multizorkd.c:129`), swapped into Z-machine global 0 before a player's turn runs
and back out after (`:1563`, `:1657`), and persisted per seat in SQLite
(`:222`). It is the room object id. Nothing needs computing.

**The client already has the decoder, and it is production code.**
`room_model_refresh_room(unsigned short room)` (`room_model.c:747`) takes a room
id explicitly and reads only the bound story image — no interpreter, no live
object tree. It is what drives the CD build's in-game rose today.

**The client already has a byte-stream escape hatch.** `term_service`
(`term.c:79-100`) is a twenty-line loop that already recognises one escape
(telnet IAC) and suppresses it from the console.

### The premise is measured, not assumed

Binding `room_model` to the *trimmed* 20,023-byte blob the netbin embeds
(`netbin_story.c`, per `2026-08-29`'s trim) and sweeping every object id:

```
room_model_bind(trimmed) = 1
room_model_available()   = 1
objects with any exit: 112   total OPEN exits: 274

obj 15   n:OPEN(24) e:OPEN(45) down:OPEN(72)
obj 25   e:OPEN(26) w:OPEN(76) s:blocked(0) nw:OPEN(74) down:OPEN(26)
obj 37   s:OPEN(38) ne:OPEN(50) sw:OPEN(41) up:OPEN(41) down:blocked(0)
```

Destinations resolve and blocked exits are distinguished from absent ones. The
trim kept the object table, the property tables and the globals; everything this
design reads is below the cut.

Driving the same decode with room ids captured from a live daemon confirms the
geography end to end. The server reported 180, then 81, then 75, then 81 as a
player walked north, north, south:

```
server id 180  n:OPEN(81) e:blocked(0) w:OPEN(78) s:OPEN(80) ne:OPEN(81) ...
server id  81  n:OPEN(75) e:OPEN(79)   w:OPEN(180) s:blocked(0) ...
server id  75  n:OPEN(143) e:OPEN(77)  w:OPEN(78)  s:OPEN(81) up:OPEN(88)
```

180 is West of House: north leads to 81, and east is *blocked* — the boarded
front door. 81's north leads to 75 and its west returns to 180. 75 is the Forest
Path, whose `up` is the climbable tree. Every destination the static image
predicts matches the transition the live server actually made, and every id is
inside 1..255, which is the check that catches a byte-swap.

## What a room ID does and does not buy

| | With the room ID |
| --- | --- |
| Compass rose exits | **Real**, replacing all-twelve-open |
| Objects in the room (`nhere`) | Must be **suppressed** — see below |
| Inventory (`ncarried`) | Must be **suppressed** — see below |
| Typeahead ranking | Unchanged; `typeahead_set_screen` already scans visible text each turn |

This asymmetry is the design, not a shortfall. Exits are compile-time properties
on the room object and are stable; room contents and inventory change every turn
and are not in a static image. A client that decoded `nhere` from the initial
image would list the lamp in the Living Room forever. The rose design's rule
holds: **make no claim you cannot keep.**

The netbin's current behaviour when it has no object knowledge is already
correct and must survive this change — `cv_cmd_accept` submits `inventory` as a
real command and lets the server answer, rather than opening a local browser.

### `refresh_room` must be told to stop at the exits

**Correction to this design's first draft, found by running it.** An earlier
draft claimed `refresh_room` leaves `nhere` and `ncarried` empty. It does not.
Past the exit loop (`room_model.c:781-792`) it goes on to:

1. `g_model.nhere = collect_children(room, ...)` — the room's contents *as the
   story shipped*. Decoding room 180 gives `nhere=2`: the mailbox is really
   there, and it stays there in this model forever, including after somebody
   takes it.
2. Infer the player object, by looking for the single object common to the
   previous room's children and this room's. If that guess lands,
   `ncarried = collect_children(g_player, ...)` and the panel starts reporting a
   **fabricated inventory**.

Both are correct for the CD build, which refreshes against a live story. Both
are exactly the confidently-wrong claim this design exists to avoid. Worse, the
second is a *guess*, and a guess is the one thing the rose design ruled out.

So `room_model` gains an explicit mode, set once by the netbin after binding:

```c
void room_model_set_exits_only(int on);
```

When on, `refresh_room` returns as soon as the property walk is done, leaving
`nhere` and `ncarried` at 0 and never running the player inference. The CD build
never sets it and is untouched.

This is a small API addition rather than a netbin-side workaround because
`room_model_get()` hands back a `const RoomModel *` into a file static — there
is no honest way to blank those fields from outside, and a cast would be a lie
about ownership.

## Protocol

### Handshake — the client opts in, the server stays quiet otherwise

`multizorkd` listens on port 23 and real people play it with real telnet
clients. Unsolicited control bytes would land in their scrollback. So the
channel is off until a client asks for it.

The client sends, once, immediately on connect:

```
IAC WILL <ZATURN_OPT>      (0xFF 0xFB <opt>)
```

The server sets `conn->wants_room_id = 1` and replies `IAC DO <ZATURN_OPT>`.
The client's existing IAC handling skips exactly two bytes after `0xFF`, which
is correct for this three-byte reply, so no client change is needed to swallow
it.

`ZATURN_OPT` is **178**, picked out of the unassigned option space and away from
both the IANA-registered low numbers and the de-facto MUD options (69/70
MSDP/MSSP, 85/86 MCCP, 90/91 MSP/MXP, 93 ZMP, 200/201 ATCP/GMCP) that a real
client might actually send. It is defined twice — `multizorkd.c` and `term.h` —
because there is no header the Saturn build and the server both include.

### Do not use subnegotiation

`IAC SB … IAC SE` is the protocol-correct way to carry a payload and it is the
wrong choice here, because **both ends currently mishandle it**:

- Server (`multizorkd.c:3120-3133`): anything `>= 250` after IAC skips exactly
  one more byte. A subnegotiation payload would fall straight through into
  `conn->inputbuf` and be parsed as the player's typed command.
- Client (`term.c:88-91`): `iac_skip = 2` unconditionally. Correct for the
  three-byte WILL/WONT/DO/DONT commands, wrong for a variable-length SB, and
  wrong for `IAC IAC` (an escaped literal 0xFF, where it eats a real byte).

Neither is a live bug — the server only ever sends three-byte `IAC WONT` today —
but both would become one the moment SB appeared on the wire. Fixing them is
worth doing on its own merits; this design does not depend on it.

### Frame — server to client

Seven bytes, printable-safe, no byte can collide with IAC:

```
0x01 'R' h h h h 0x02
```

`hhhh` is the room object id as four uppercase hex digits. Four rather than two
so a v5 story with more than 255 objects does not need a new frame.

0x01/0x02 (SOH/STX) are chosen because the Z-machine never prints them and the
console would render them as garbage — so if one ever leaks to a client that did
not ask, it is visible rather than silent.

### When it is emitted

At the turn boundary, from `opcode_read_multizork` (`multizorkd.c:1407`), which
is where the Z-machine has finished printing this player's turn and is about to
read their next command. Emit before the prompt.

**Read the live value, not the cached one.** At that point `globals[0]` holds
the current room; `player->gvar_location` still holds the pre-step value until
swap-out at `:1657`. Taking the wrong one sends last turn's room, which is the
kind of off-by-one-turn bug that looks like flaky decoding.

Only while in-game (`conn->inputfn == inpfn_ingame`) and only when
`wants_room_id` is set. Nothing is emitted in the lobby, where there is no room.

Suppress the frame when the id has not changed since the last one sent, so
non-movement turns cost nothing.

## Architecture

### Server — `saturn/multizorkd.c`

1. `Connection` gains `int wants_room_id` and `uint16 last_room_id_sent`.
2. The IAC receive loop gains one case: `WILL <ZATURN_OPT>` sets the flag and
   replies `DO`.
3. A `write_room_id(Player *player, uint16 room)` helper formats the frame and
   calls `write_to_connection`, skipping when the connection is null, the flag
   is clear, or `room == last_room_id_sent`.
4. `opcode_read_multizork` calls it with `globals[0]`.

No change to the Z-machine, the database schema, or the lobby.

### Client — `saturn/src/net/term.c`

`TermState` gains a small frame parser: on `0x01` (and only when the netbin
asked for the channel) enter capture, buffer up to a fixed maximum, and on
`0x02` publish `t->room_id` and set `t->room_id_fresh`. Bytes inside a frame
never reach `console_write`. An unterminated frame is abandoned at the buffer
cap and its bytes are dropped, not replayed — a corrupt frame must not become
visible garbage.

The parser lives in `term.c` because that file owns the byte stream. It must not
know what a room is: it publishes a number, and `online.cxx` decides what that
means. The handshake send belongs here too, beside `term_submit_line`.

### Client — `saturn/src/net/online.cxx`

Under `#ifdef NETBIN`, in the terminal loop:

- Once, before the loop: `room_model_bind(netbin_story_data(), netbin_story_size())`.
  If it returns 0, nothing else in this design engages and the rose stays
  all-open.
- Each frame: if `t.room_id_fresh`, call `room_model_refresh_room(t.room_id)`
  and clear the flag.

### Client — `room_model.c` gains the exits-only mode

`room_model_set_exits_only(int)` sets a file static; `refresh_room` returns
straight after the property walk when it is on. `online.cxx` calls it once,
immediately after `room_model_bind`.

### Client — the two all-open fallbacks come back on when we know nothing

`netbin_room_model.c` is deleted; the netbin links the real `room_model.c`
instead. But "all twelve open" must remain the behaviour until a room id has
actually arrived, because a rose drawn from `room_model`'s zeroed initial state
would show *no* exits at all — a confidently wrong rose, the exact failure the
rose design forbids.

So both consumers gain the same guard, on a new `room_model_has_room()`
predicate (true once `refresh_room` has been called with a nonzero id):

- `console_view.cxx:457-465` — the `#ifdef NETBIN` `kb_exits` returns
  `KB_EXITS_ALL` when `!room_model_has_room()`, and the room's real exits
  otherwise. The existing `KB_EXITS_ALL` table stays exactly as it is.
- `command_view.cxx` — the panel's travel module reads `m.exits`, which is
  already correct in both cases.

### Client — the inventory overlay must not open on an empty model

`cv_cmd_accept` (`command_view.cxx:1132`) currently reads:

```c
if (p.cursor == 0 && room_model_available()) { cp_overlay_open(&p); return; }
```

Once the netbin binds a story, `room_model_available()` becomes 1 and this opens
an empty inventory box instead of submitting `inventory` to the server — a
regression against the behaviour shipped in `3ad1f6b`.

**Recommended fix:** gate on having something to browse rather than on having a
model. `cv_cmd_accept` takes the `RoomModel` (it does not today; thread it from
`command_edit`, which already has it) and the condition becomes
`room_model_available() && m.ncarried > 0`.

This also changes the CD build: an empty-handed player who picks Inventory now
submits the command and gets the game's own "You are empty-handed" instead of an
empty box. That is an improvement, but it *is* a CD behaviour change and should
be called out in review rather than slipped in.

**Alternative if that is unwanted:** split the predicate — `room_model_available()`
keeps meaning "exits are real", and a new `room_model_objects_live()` gates every
`nhere`/`ncarried` path. More honest, more surface area, and it makes the two
kinds of knowledge explicit in shared code. Prefer this if the CD change is
contentious.

## Size accounting

Measured from the current netbin map unless marked.

| Item | Image | `.bss` |
| --- | ---: | ---: |
| `engine/room_model.o` (linked) | +4,176 | +260 |
| `engine/netbin_room_model.o` (deleted) | −360 | 0 |
| `net/term.o` growth — frame parser + handshake | (in the total) | |
| `net/online.cxx` wiring, `command_view`/`console_view` guards | (in the total) | |
| **measured total** | **+4,432** | |

Netbin went from 159,472 B to **163,904 B**, against the ~4,230 B this table
estimated before the work. Server-side changes cost the client nothing.

`ZATURN_OPT` was settled at **178**.

## Bandwidth

9600 baud is about 960 bytes per second, or 16 bytes per frame at 60 Hz. Seven
bytes once per *turn*, suppressed when the room has not changed, is beneath
noticing against room descriptions already running to hundreds of bytes.

This matters because the load-time work in `a077383` established that perceived
latency on this link is a real constraint. A room ID is not where it will go
wrong; a full object list would have been.

## Testing

- **Host**: a `term.c` frame-parser test — a frame arriving whole; a frame split
  across two `term_service` calls; an unterminated frame abandoned at the cap
  without leaking bytes to the console; a literal `0x01` in game text passed
  through untouched when the channel was never requested.
- **Host**: extend the `room_model` coverage with the sweep this design was
  verified by — bind the trimmed `netbin_story.c` bytes, assert `bind` returns
  1, and pin a known object id's full exit table. That pins the trim, the
  decode and the premise together, and it fails loudly if a future trim cuts
  the object table.
- **Host**: `tests/test_netbin_sources.py` — `EXPECTED` loses
  `src/engine/netbin_room_model.c` and gains `src/engine/room_model.c`, which
  also leaves `NETBIN_ONLY_SOURCES`.
- **Server**: a test that no frame is emitted without the handshake, none in the
  lobby, and none when the room is unchanged.
- **Interop**: connect with a stock `telnet` client, play a few turns, confirm
  the transcript is byte-identical to today's. This is the check that protects
  every non-Saturn player and it must be run before deploy, not after.
- **Hardware**: dial in, walk between rooms, watch the rose change and match the
  prose. Then walk into a room reached through a door or a conditional exit and
  confirm it reads as `maybe` rather than promising passage.

## Risks

- **The exits come from the initial image, and the live world has diverged.**
  This is the design's one real risk. Direction properties are compile-time in
  ZILCH and conditional exits encode the *condition*, not the resolved answer —
  `room_model` already reports anything longer than the one-byte unconditional
  form as `RM_EXIT_MAYBE`, so the hedging exists. But Zork's dynamic memory is
  world state, `multizorkd` persists it per instance, and nothing here proves
  the game never writes a direction property.
  **Verify before shipping:** pull an `instances.dynamic_memory` blob out of
  `multizork.sqlite3` from a well-played game and diff it against `ZORK1.Z3`'s
  dynamic region, restricted to the property-table byte ranges of the 112
  objects the sweep found. Any difference in a direction property means tier 1
  is unsound and the server must send the twelve exit states instead of an id.
- **The decode is proven against live stories, not static ones.**
  `room_model.c` drives the CD build's rose today, so the decode itself is not
  new. What is new is feeding it an image that no interpreter is mutating. The
  sweep above shows it produces plausible geography; it does not prove the
  direction-to-property mapping is right for every room. Pin a real Zork I room
  against known geography in the host test rather than trusting shape alone.
- **`refresh_room` does more than exits, and an earlier draft of this document
  said it did not.** Caught by running the decode rather than reading it. The
  exits-only mode above is the fix, and the host test must assert
  `nhere == ncarried == 0` after a refresh in that mode — otherwise the next
  person to touch `room_model` can quietly re-enable a fabricated inventory.
- **A public server gains a new code path.** The handshake gate is the only
  thing standing between stock telnet users and control bytes in their
  scrollback. Its test is listed under Interop and is not optional.
- **Two ends must be deployed together-ish.** An old client against a new server
  never sends the handshake and sees nothing, and a new client against an old
  server sends `IAC WILL` and gets `IAC WONT`, which it already skips. Both
  directions degrade to today's behaviour, so the ordering does not matter —
  but that property is worth preserving deliberately rather than by luck.

## Non-goals

- Sending room contents or inventory. If the static-image risk above forces the
  server to send derived facts, that is a different design and should be written
  as one.
- Anything for the CD build's online mode, whose `room_model` holds a stale
  *local* room while a remote game runs. That is a real bug and out of scope
  here; it wants the same treatment and should get its own entry.
- Fixing the IAC subnegotiation handling on either end. Documented above because
  this design has to route around it, not because it changes it.
- A difficulty or interface toggle in the netbin's own UI.
