# Multizork Lobby and Four Seats Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace multizorkd's three-item text menu with a lobby of named rooms, and let a new player claim a seat in a game that has already started.

**Architecture:** Rooms gain generated `adjective-noun` names that replace the six-character game code as the join identifier, matched case-insensitively, with a privacy flag that hides a room from the lobby without making it unreachable. The connection's first prompt becomes `username:`, which makes the name the key for finding a returning player's games and removes the code-shape ambiguity the old first prompt had to guess at. Phase 2 then builds all four Zork 1 player seats at `go` rather than only the connected ones, because that is the only moment a dormant seat's Z-machine continuation can legally be constructed.

**Tech Stack:** C11, POSIX sockets, sqlite3, mojozork's Z-machine. Tests are host-compiled C (gcc) for pure logic and Python 3 TCP drivers for daemon behaviour.

**Spec:** `docs/superpowers/specs/2026-08-27-multizork-lobby-and-midgame-join-design.md`

## Global Constraints

- Author of record is **suinevere**. Commit messages are one sentence, no body, no bullets, no trailers, and never mention Claude, AI, or a session.
- Every function, constant, and file gets the project header-block comment form. Tests and generated files get a file header only. No comments inside function bodies.
- `saturn/multizorkd.c` and `saturn/roomnames.h` stay beside the vendored mojozork sources. They are not moved into `saturn/src/`.
- Room-name words are at most 9 characters, enforced by the fixed-width array declaration, never by a comment.
- `Instance.hash` becomes `char[24]` (`ROOMNAME_MAX`). Player access codes stay 6 characters in `Player.hash[8]`.
- All room-name comparison uses `strcasecmp`, already used at `multizorkd.c:1800`, so no new include is required.
- New database columns are added by tolerated `alter table`, never by editing `SQL_CREATE_TABLES`, so existing databases migrate in place.
- **The owner runs every daemon build and test.** multizorkd needs POSIX and sqlite3 and cannot be built on the Windows development box. Steps marked *(owner-run)* are handed over, not executed by the implementer.

**Build (owner):**

```bash
cmake -B build -DMOJOZORK_MULTIZORK=ON saturn && cmake --build build --target multizorkd
```

**Daemon tests (owner), with multizorkd listening:**

```bash
MULTIZORK_ADDR=127.0.0.1:2323 python3 saturn/tests/test_multizork_join.py
MULTIZORK_ADDR=127.0.0.1:2323 python3 saturn/tests/test_multizork_lobby.py
```

## File Structure

| File | Responsibility |
| --- | --- |
| `saturn/roomnames.h` (new) | The two wordlists and `roomname_compose()`. Pure, no sqlite, no sockets — the one piece testable on the dev box. |
| `saturn/multizorkd.c` (modify) | Everything else: schema migration, the lobby, the dispatcher, seat preallocation and claiming. |
| `saturn/tests/test_roomnames.c` (new) | Host-compiled unit test for name composition and the length invariant. |
| `saturn/tests/test_multizork_lobby.py` (new) | TCP driver for lobby, privacy, resume, and mid-game join. |
| `saturn/tests/test_multizork_join.py` (modify) | Rewritten for the moved entry point. |

---

## Phase 1 — The lobby

### Task 1: Room name wordlists and composer

**Files:**
- Create: `saturn/roomnames.h`
- Test: `saturn/tests/test_roomnames.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `ROOMNAME_MAX` (24), `ROOMNAME_NUM_ADJECTIVES`, `ROOMNAME_NUM_NOUNS`, and
  `static void roomname_compose(char *out, size_t outlen, size_t adj, size_t noun, int suffix)`.
  A `suffix` of 0 composes `adjective-noun`; 1 to 99 composes `adjective-noun-NN`, zero-padded.
  `adj` and `noun` are reduced modulo the list lengths, so callers may pass raw `random()` output.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_roomnames.c`:

```c
/*----------------------
 | test_roomnames.c
 | Description: Host test for room-name composition. Pins that a composed name
 |   always fits ROOMNAME_MAX so a long word added later cannot truncate a name
 |   silently, that the suffix form appears only when asked for, and that raw
 |   random() values are reduced rather than read off the end of the wordlists.
 | Author: suinevere
 | Dependencies: ../roomnames.h, assert.h, string.h, stdio.h
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/trn.exe saturn/tests/test_roomnames.c \
 |          && /tmp/trn.exe
 ----------------------*/
#include "../roomnames.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    char buf[ROOMNAME_MAX];

    roomname_compose(buf, sizeof (buf), 0, 0, 0);
    assert(strcmp(buf, "brass-lantern") == 0);

    roomname_compose(buf, sizeof (buf), 0, 0, 7);
    assert(strcmp(buf, "brass-lantern-07") == 0);

    roomname_compose(buf, sizeof (buf), 0, 0, 99);
    assert(strcmp(buf, "brass-lantern-99") == 0);

    for (size_t a = 0; a < ROOMNAME_NUM_ADJECTIVES; a++) {
        for (size_t n = 0; n < ROOMNAME_NUM_NOUNS; n++) {
            for (int s = 0; s <= 99; s++) {
                roomname_compose(buf, sizeof (buf), a, n, s);
                assert(strlen(buf) < ROOMNAME_MAX);
                assert(strchr(buf, '-') != NULL);
                for (const char *p = buf; *p; p++) {
                    assert(((*p >= 'a') && (*p <= 'z')) || (*p == '-') || ((*p >= '0') && (*p <= '9')));
                }
            }
        }
    }

    roomname_compose(buf, sizeof (buf), ROOMNAME_NUM_ADJECTIVES, ROOMNAME_NUM_NOUNS, 0);
    assert(strcmp(buf, "brass-lantern") == 0);

    printf("test_roomnames: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/trn.exe saturn/tests/test_roomnames.c && /tmp/trn.exe
```

Expected: FAIL — `roomnames.h: No such file or directory`.

- [ ] **Step 3: Write `saturn/roomnames.h`**

The wordlists are fixed-width `char[][10]` arrays on purpose: an entry longer than 9 characters is a compile error, so the length invariant is enforced by the language rather than by a comment asking future editors to be careful.

```c
/*----------------------
 | roomnames.h
 | Description: Wordlists and the composer that names a multizork room. Names are
 |   an adjective-noun pair so a player can read one aloud to a friend, with an
 |   optional two-digit suffix the generator falls back on once plain pairs start
 |   colliding.
 | Author: suinevere
 | Dependencies: stdio.h, stddef.h
 | Globals: roomname_adjectives, roomname_nouns
 | Params: N/A
 | Returns: N/A
 ----------------------*/
#ifndef ROOMNAMES_H
#define ROOMNAMES_H

#include <stdio.h>
#include <stddef.h>

/*----------------------
 | ROOMNAME_MAX
 | Description: Bytes a composed room name needs including its terminator: two
 |   nine-character words, a joining hyphen, a suffix hyphen and two digits.
 | Author: suinevere
 ----------------------*/
#define ROOMNAME_MAX 24

/*----------------------
 | ROOMNAME_WORD_MAX
 | Description: Bytes per wordlist entry. Nine characters and a terminator; a
 |   longer word fails to compile rather than truncating a name at runtime.
 | Author: suinevere
 ----------------------*/
#define ROOMNAME_WORD_MAX 10

/*----------------------
 | roomname_adjectives
 | Description: The first half of a room name.
 | Author: suinevere
 ----------------------*/
static const char roomname_adjectives[][ROOMNAME_WORD_MAX] = {
    "brass", "rusty", "dented", "hollow", "ancient", "crooked",
    "silver", "wooden", "iron", "gilded", "mossy", "frozen",
    "hidden", "narrow", "quiet", "sunken", "dusty", "bitter",
    "coiled", "drifting", "echoing", "faded", "gloomy", "granite",
    "humming", "jagged", "lonely", "molten", "murky", "oaken",
    "pale", "quartz", "ragged", "restless", "salted", "scarlet",
    "shallow", "slanted", "sodden", "tangled", "velvet", "windswept",
    "amber", "cracked", "hallowed", "leaden", "painted", "twisted"
};

/*----------------------
 | roomname_nouns
 | Description: The second half of a room name.
 | Author: suinevere
 ----------------------*/
static const char roomname_nouns[][ROOMNAME_WORD_MAX] = {
    "lantern", "mailbox", "coffin", "sword", "grating", "troll",
    "cellar", "attic", "chimney", "canyon", "river", "chasm",
    "lamp", "rope", "skeleton", "kitchen", "gallery", "painting",
    "basket", "boat", "rug", "window", "forest", "clearing",
    "maze", "vault", "dam", "reservoir", "temple", "altar",
    "bell", "candle", "book", "mirror", "tunnel", "bridge",
    "volcano", "balloon", "crystal", "sphere", "trident", "scarab",
    "jewel", "chalice", "bracelet", "pot", "timber", "barrow"
};

/*----------------------
 | ROOMNAME_NUM_ADJECTIVES
 | Description: Entries in roomname_adjectives.
 | Author: suinevere
 ----------------------*/
#define ROOMNAME_NUM_ADJECTIVES (sizeof (roomname_adjectives) / ROOMNAME_WORD_MAX)

/*----------------------
 | ROOMNAME_NUM_NOUNS
 | Description: Entries in roomname_nouns.
 | Author: suinevere
 ----------------------*/
#define ROOMNAME_NUM_NOUNS (sizeof (roomname_nouns) / ROOMNAME_WORD_MAX)

/*----------------------
 | roomname_compose
 | Description: Writes one room name, reducing the word indices so a caller can
 |   hand over raw random() output.
 | Author: suinevere
 | Dependencies: stdio.h
 | Globals: roomname_adjectives, roomname_nouns
 | Params: out -- destination, at least ROOMNAME_MAX bytes
 |   outlen -- bytes available at out
 |   adj -- adjective index, reduced modulo the list length
 |   noun -- noun index, reduced modulo the list length
 |   suffix -- 0 for a plain pair, 1 to 99 for a numbered one
 | Returns: N/A
 ----------------------*/
static void roomname_compose(char *out, const size_t outlen, const size_t adj, const size_t noun, const int suffix)
{
    const char *a = roomname_adjectives[adj % ROOMNAME_NUM_ADJECTIVES];
    const char *n = roomname_nouns[noun % ROOMNAME_NUM_NOUNS];
    if ((suffix > 0) && (suffix < 100)) {
        snprintf(out, outlen, "%s-%s-%02d", a, n, suffix);
    } else {
        snprintf(out, outlen, "%s-%s", a, n);
    }
}

#endif
```

- [ ] **Step 4: Run test to verify it passes**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/trn.exe saturn/tests/test_roomnames.c && /tmp/trn.exe
```

Expected: `test_roomnames: OK`

- [ ] **Step 5: Commit**

```bash
git add saturn/roomnames.h saturn/tests/test_roomnames.c
git commit -m "Name multizork rooms with an adjective-noun pair a player can read aloud to a friend, declaring both wordlists as fixed-width arrays so a word too long for the name buffer fails to compile instead of truncating a room's name at runtime."
```

---

### Task 2: Widen the instance name and generate room names

**Files:**
- Modify: `saturn/multizorkd.c` — includes (line 26), `Instance` (129-142), `Connection` (145-162), `generate_unique_hash` (855-873), `find_live_instance_by_hash` (1942-1968), `db_select_instance` (590)

**Interfaces:**
- Consumes: `roomname_compose`, `ROOMNAME_MAX` from Task 1.
- Produces: `static int generate_unique_room_name(char *name)` returning 1 on success, 0 on a database problem; `static Instance *find_live_instance_by_name(const char *name)`. `Instance.hash` is now `char[ROOMNAME_MAX]`.

- [ ] **Step 1: Include the wordlists**

In `saturn/multizorkd.c`, immediately after `#include "sqlite3.h"` (line 26), add:

```c
#include "roomnames.h"
```

- [ ] **Step 2: Widen `Instance.hash` and `Connection.pending_join`**

In `struct Instance`, change `char hash[8];` to:

```c
    char hash[ROOMNAME_MAX];
```

In `struct Connection`, **widen** `pending_join` — do not delete it here. Task 4 removes the prompt that fills it and the block that reads it, and the field goes with them; deleting the field now, while both use sites still exist, would not compile. Widening it also keeps the intervening commits behaviourally correct: at 8 bytes it would truncate a 22-character room name to 7 and silently break the "a friend gave me a code" path this task exists to serve.

```c
    char pending_join[ROOMNAME_MAX];  // room name typed at the hello-sailor prompt, joined once a name exists.
```

- [ ] **Step 3: Add the room-name generator**

Insert directly below `generate_unique_hash` (after line 873):

```c
/*----------------------
 | generate_unique_room_name
 | Description: Picks a room name no instance has ever used, falling back to a
 |   numbered form once plain pairs keep colliding so the search always ends.
 | Author: suinevere
 | Dependencies: roomnames.h, sqlite3.h
 | Globals: N/A
 | Params: name -- destination, at least ROOMNAME_MAX bytes
 | Returns: 1 when name holds an unused name, 0 on a database problem
 ----------------------*/
static int generate_unique_room_name(char *name)
{
    for (int attempt = 0; attempt < 200; attempt++) {
        const int suffix = (attempt < 20) ? 0 : ((int) ((((size_t) random()) % 99) + 1));
        int notunique = 0;
        roomname_compose(name, ROOMNAME_MAX, (size_t) random(), (size_t) random(), suffix);
        if (db_insert_used_hash(name, &notunique)) {
            return 1;
        } else if (!notunique) {
            return 0;
        }
    }
    loginfo("Ran out of room names after 200 tries!");
    return 0;
}
```

- [ ] **Step 4: Rename and loosen the live lookup**

Replace `find_live_instance_by_hash` (lines 1942-1968) in full. The six-character length guard goes away because names are no longer six characters, and the comparison becomes case-insensitive because a player types this by hand.

```c
/*----------------------
 | find_live_instance_by_name
 | Description: Finds the instance a room name points at, or NULL when no
 |   connected player is in a room by that name. Case is ignored because the
 |   name is meant to be read aloud and typed back.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: connections, num_connections
 | Params: name -- a room name as typed by a player
 | Returns: the Instance, or NULL when the name is empty or names no live room
 ----------------------*/
static Instance *find_live_instance_by_name(const char *name)
{
    if (*name == '\0') {
        return NULL;
    }

    // !!! FIXME: maintaining a list of instances means up to 4x less
    // !!! FIXME:  searches than looking through the connections to
    // !!! FIXME:  find it. A hashtable even more so.
    for (size_t i = 0; i < num_connections; i++) {
        Connection *c = connections[i];
        if (c->instance && (strcasecmp(c->instance->hash, name) == 0)) {
            return c->instance;
        }
    }

    return NULL;
}
```

- [ ] **Step 5: Fix the two callers that no longer compile**

`inpfn_enter_instance_code_to_join` (line 1982) and `inpfn_hello_sailor` (line 2238) both call the old name. Change both call sites to `find_live_instance_by_name`. Both functions are deleted in Tasks 4 and 6; renaming now keeps the tree compiling between commits.

- [ ] **Step 6: Verify the file still parses** *(owner-run)*

```bash
cmake -B build -DMOJOZORK_MULTIZORK=ON saturn && cmake --build build --target multizorkd
```

Expected: builds clean. `db_select_instance:590` already uses `sizeof (inst->hash)` in its `snprintf`, so the wider field needs no change there.

- [ ] **Step 7: Commit**

```bash
git add saturn/multizorkd.c
git commit -m "Give a multizork instance a room name wide enough to hold a word pair instead of six random characters, and match it without regard to case or length so a player can type back what a friend read to them."
```

---

### Task 3: Database columns and the your-games query

**Files:**
- Modify: `saturn/multizorkd.c` — SQL macros (253-310), statement globals (314-329), `Player` (96-127), `Instance` (129-142), `db_insert_player` (471-516), `db_select_instance` (604-651), `db_init` (745-826), `db_quit` (826-853)

**Interfaces:**
- Consumes: `ROOMNAME_MAX` from Task 1.
- Produces:
  - `Player.claimed` (int), `Instance.is_private` (int).
  - `typedef struct MyGameRow { char name[ROOMNAME_MAX]; sqlite3_int64 dbid; time_t savetime; } MyGameRow;`
  - `static int db_select_my_games(const char *username, MyGameRow *rows, int maxrows)` returning the row count written.

- [ ] **Step 1: Add the two struct fields**

In `struct Player`, directly below `char hash[8];`:

```c
    int claimed;             // a real person owns this seat; connection says whether they are here now.
```

In `struct Instance`, directly below `char hash[ROOMNAME_MAX];`:

```c
    int is_private;          // kept out of the lobby list; reachable only by name.
```

- [ ] **Step 2: Add the SQL macros**

After `SQL_RECAP_TRIM` (line 310), add. The `p.claimed <> 0` term matters: preallocated seats carry an empty username and would otherwise match a player whose name is also empty.

```c
#define SQL_MY_GAMES_SELECT \
    "select i.id as id, i.hashid as hashid, i.savetime as savetime from instances i" \
    " join players p on p.instance = i.id" \
    " where p.username = $username and p.claimed <> 0 and i.crashed = 0" \
    " order by i.savetime desc limit $limit;"

#define SQL_ADD_INSTANCE_PRIVATE \
    "alter table instances add column private integer not null default 0;"

#define SQL_ADD_PLAYER_CLAIMED \
    "alter table players add column claimed integer not null default 1;"
```

- [ ] **Step 3: Add the statement global**

After line 329 (`GStmtRecapTrim`):

```c
static sqlite3_stmt *GStmtMyGamesSelect = NULL;
```

- [ ] **Step 4: Migrate and prepare in `db_init`**

In `db_init`, directly after the `SQL_CREATE_TABLES` `sqlite3_exec` block, add the migration. A database created fresh already has the columns, so a failure here is expected and ignored — that is the whole idiom.

```c
    for (int i = 0; i < 2; i++) {
        const char *sql = i ? SQL_ADD_PLAYER_CLAIMED : SQL_ADD_INSTANCE_PRIVATE;
        char *addmsg = NULL;
        if (sqlite3_exec(GDatabase, sql, NULL, NULL, &addmsg) != SQLITE_OK) {
            loginfo("Column already present, continuing: %s", addmsg ? addmsg : "(no message)");
        }
        sqlite3_free(addmsg);
    }
```

Then, after the `SQL_RECAP_TRIM` prepare block at the end of `db_init`:

```c
    if (sqlite3_prepare_v2(GDatabase, SQL_MY_GAMES_SELECT, -1, &GStmtMyGamesSelect, NULL) != SQLITE_OK) {
        panic("Failed to create my-games select SQL statement! %s", sqlite3_errmsg(GDatabase));
    }
```

In `db_quit`, add alongside the other finalizers:

```c
    FINALIZE_DB_STMT(GStmtMyGamesSelect);
```

- [ ] **Step 5: Persist and load the two new columns**

In `SQL_INSTANCE_INSERT` (line 259), add `private` to the column list and `$private` to the values list. In `db_insert_instance` (437-453), add this bind alongside the others:

```c
             (SQLBINDINT(GStmtInstanceInsert, "private", inst->is_private) == SQLITE_OK) &&
```

In `db_select_instance`, after the `num_players` read at line 591:

```c
    inst->is_private = SQLCOLUMN(int, GStmtInstanceSelect, "private");
```

In `SQL_PLAYER_INSERT` (line 269), add `claimed` to the column list and `$claimed` to the values list. In `db_insert_player`, add:

```c
             (SQLBINDINT(GStmtPlayerInsert, "claimed", inst->players[playernum].claimed) == SQLITE_OK) &&
```

In `SQL_PLAYER_UPDATE` (line 280), add `claimed = $claimed,` to the SET list, and the matching bind in `db_update_player`:

```c
             (SQLBINDINT(GStmtPlayerUpdate, "claimed", inst->players[playernum].claimed) == SQLITE_OK) &&
```

In `db_select_instance`'s player loop, after the `username` read at line 619:

```c
        player->claimed = SQLCOLUMN(int, GStmtPlayersSelect, "claimed");
```

- [ ] **Step 6: Add the row type and query**

Insert directly above `db_trim_recap` (line 732):

```c
/*----------------------
 | MyGameRow
 | Description: One archived game a player has a seat in, as the lobby needs it.
 | Author: suinevere
 ----------------------*/
typedef struct MyGameRow
{
    char name[ROOMNAME_MAX];
    sqlite3_int64 dbid;
    time_t savetime;
} MyGameRow;

/*----------------------
 | db_select_my_games
 | Description: Finds the games a username holds a claimed seat in, newest save
 |   first. Liveness is not a database fact, so the caller filters out any row
 |   that is currently loaded.
 | Author: suinevere
 | Dependencies: sqlite3.h
 | Globals: GStmtMyGamesSelect
 | Params: username -- the name typed at the first prompt
 |   rows -- destination array
 |   maxrows -- entries available at rows
 | Returns: the number of rows written
 ----------------------*/
static int db_select_my_games(const char *username, MyGameRow *rows, const int maxrows)
{
    int total = 0;

    if ( (sqlite3_reset(GStmtMyGamesSelect) != SQLITE_OK) ||
         (SQLBINDTEXT(GStmtMyGamesSelect, "username", username) != SQLITE_OK) ||
         (SQLBINDINT(GStmtMyGamesSelect, "limit", maxrows) != SQLITE_OK) ) {
        db_log_error("select my games");
        return 0;
    }

    while ((total < maxrows) && (sqlite3_step(GStmtMyGamesSelect) == SQLITE_ROW)) {
        MyGameRow *row = &rows[total];
        snprintf(row->name, sizeof (row->name), "%s", SQLCOLUMN(text, GStmtMyGamesSelect, "hashid"));
        row->dbid = SQLCOLUMN(int64, GStmtMyGamesSelect, "id");
        row->savetime = (time_t) SQLCOLUMN(int64, GStmtMyGamesSelect, "savetime");
        total++;
    }

    sqlite3_reset(GStmtMyGamesSelect);
    return total;
}
```

- [ ] **Step 7: Set `claimed` where seats are filled today**

Phase 1 still fills seats only at `go`. In `start_instance`, inside the `for (size_t i = 0; i < num_players; i++)` loop (line 1559), directly after the `snprintf(player->username, ...)` at line 1575:

```c
        player->claimed = 1;
```

Task 9 replaces this loop wholesale; setting it here keeps every commit in between correct.

- [ ] **Step 7a: Leave old instances alone**

`db_select_instance` reads `num_players` from the instances row and refuses to load when the player row count disagrees (lines 648-651). An instance written before this work carries its original seat count and that many rows, so it rehydrates exactly as it does today and simply never offers a free seat once Phase 2 lands. There is no backfill and no migration of existing rows — only the two columns, which default correctly. Add nothing here; this step exists so the absence of a migration is a decision on the record rather than an oversight.

- [ ] **Step 8: Build and confirm the migration is idempotent** *(owner-run)*

Build, then start the daemon twice against an existing `multizork.sqlite3`. The first run logs `Column already present, continuing:` zero or two times depending on the database's age; the second run logs it twice. Neither run panics.

```bash
cmake --build build --target multizorkd && ./build/multizorkd --port 2323
```

- [ ] **Step 9: Commit**

```bash
git add saturn/multizorkd.c
git commit -m "Record whether a room is private and whether a seat has a real owner, adding both columns to a database that already exists rather than only to one created fresh, and find the games a returning player holds a seat in by the name they typed."
```

---

### Task 4: `username:` as the first prompt

**Files:**
- Modify: `saturn/multizorkd.c` — `inpfn_enter_name` (2073-2118), `inpfn_hello_sailor` (2208-2264), `accept_new_connection` (2461, 2479-2480)
- Modify: `saturn/tests/test_multizork_join.py`

**Interfaces:**
- Consumes: `find_live_instance_by_name` from Task 2.
- Produces: `inpfn_enter_name` is the connection's opening `InputFn`. `inpfn_hello_sailor` no longer exists.

- [ ] **Step 1: Rewrite the daemon test for the moved entry point**

Commit 6521e7f taught the connect prompt to accept a game code, because that prompt came before the name and a new arrival had no other code. With the name asked first that ambiguity is gone, so the assertion moves rather than disappears. Replace the body of `host_a_game()` and `main()` in `saturn/tests/test_multizork_join.py`, keeping the module docstring's first paragraph and updating it to describe the lobby prompt:

```python
def host_a_game():
    """Return (connection, room name) for a host sitting in the waiting room."""
    h = connect()
    drain(h)
    send(h, "seanie")
    out = send(h, "n")
    m = re.search(r"room is called '([\w-]+)'", out)
    assert m, f"host never got a room name, saw: {out!r}"
    send(h, "y")
    return h, m.group(1)

def main():
    fails = 0

    host, room = host_a_game()
    guest = connect(); drain(guest)
    out = send(guest, "bob")
    fails += check("lobby lists the waiting room", room in out, out)
    out = send(guest, room)
    fails += check("room name at the lobby joins", "guest list" in out.lower(), out)
    out = drain(host)
    fails += check("host is told a guest joined", "has joined" in out, out)
    guest.close(); host.close()

    host, room = host_a_game()
    guest = connect(); drain(guest)
    send(guest, "bob")
    out = send(guest, room.upper())
    fails += check("room name is case-insensitive", "guest list" in out.lower(), out)

    out = send(host, "go")
    m = re.search(r"access code: '(\w+)'", out)
    fails += check("a started game hands the host an access code", bool(m), out)
    if m:
        host.close()
        again = connect(); drain(again)
        send(again, "seanie")
        out = send(again, m.group(1))
        fails += check("access code at the lobby still rejoins",
                       "We found you" in out, out)
        again.close()
    guest.close()

    if fails:
        print(f"test_multizork_join: {fails} FAILED", file=sys.stderr)
        sys.exit(1)
    print("test_multizork_join: OK")

main()
```

- [ ] **Step 2: Run the test to verify it fails** *(owner-run)*

```bash
MULTIZORK_ADDR=127.0.0.1:2323 python3 saturn/tests/test_multizork_join.py
```

Expected: FAIL — the daemon still opens on "Hello sailor!" and the assertion for a room name finds nothing.

- [ ] **Step 3: Move the bot blocklist onto the name prompt**

Port scanners send `system`, `shell` and friends as their first line, so the blocklist has to move with the prompt or a live abuse defence is silently lost.

**It must fire only on a connection's first line.** The old `inpfn_hello_sailor` ran exactly once per connection, so the check only ever saw raw first bytes. `inpfn_enter_name` is different: its two rejection paths `return` without changing `inputfn`, so it is re-entered on every retry. An unguarded check would block a real person who types `admin`, `root`, `sh` or `system` at a prompt that literally asks "What's your name?" — dropping them and blocking their address for `MULTIZORK_BLOCKED_TIMEOUT`, which is 24 hours. Gating to the first line restores the old semantics exactly and weakens nothing: a scanner sends its payload immediately, which is all the old code ever caught.

First add the flag to `struct Connection`, beside `blocked`:

```c
    int saw_first_line;      // the blocklist only ever inspected a connection's opening line.
```

Task 6 adds two more fields to `struct Connection` for the lobby snapshot; they are listed in that task.

Then insert this at the very top of `inpfn_enter_name`, before the empty-string check:

```c
    const int first_line = !conn->saw_first_line;
    conn->saw_first_line = 1;
    static const char *hacker_commands[] = { "system", "shell", "sh", "enable", "admin", "root", "Administrator", "runshellcmd", "linuxshell", "start-shell", "start start-shell", "start-shell bash" };
    for (int i = 0; first_line && (i < ARRAYSIZE(hacker_commands)); i++) {
        if (strcmp(str, hacker_commands[i]) == 0) {
            const char *addr = conn->address;
            loginfo("Socket %d (%s) is probably malicious, blocked and dropped.", conn->sock, addr);
            conn->blocked = 1;
            if ((strcmp(addr, "127.0.0.1") == 0) || (strcmp(addr, "::ffff:127.0.0.1") == 0) || (strcmp(addr, "::1") == 0)) {
                loginfo("(not actually blocking localhost.)");
            } else {
                db_insert_blocked(conn->address);
            }
            write_to_connection(conn, "Nice try.\n");
            drop_connection(conn);
            return;
        }
    }
```

- [ ] **Step 4: Delete `inpfn_hello_sailor` and the pending-join hand-off**

Delete the whole of `inpfn_hello_sailor` (lines 2208-2264). Then, in `inpfn_enter_name`, delete this block — nothing fills `pending_join` any more:

```c
    if (conn->pending_join[0]) {
        char code[sizeof (conn->pending_join)];
        snprintf(code, sizeof (code), "%s", conn->pending_join);
        conn->pending_join[0] = '\0';
        conn->inputfn = inpfn_enter_instance_code_to_join;
        conn->inputfn(conn, code);
        return;
    }
```

- [ ] **Step 5: Open the connection on the name prompt**

In `accept_new_connection`, change line 2461 to:

```c
    conn->inputfn = inpfn_enter_name;
```

and replace the "Hello sailor!" greeting (line 2480) with:

```c
        write_to_connection(conn, "Welcome to MULTIZORK.\n\nWhat's your name? Keep it simple or I'll simplify it for you.\n(sorry if your name isn't one word made up of english letters.\n This is American tech from 1980, after all.)\n\nusername: ");
```

`inpfn_enter_name` is defined at line 2073, above `accept_new_connection` at 2424, so no forward declaration is needed.

Finally, now that both use sites are gone, delete the field itself from `struct Connection`:

```c
    char pending_join[ROOMNAME_MAX];  // room name typed at the hello-sailor prompt, joined once a name exists.
```

- [ ] **Step 6: Reprint the prompt on a rejected name**

`inpfn_enter_name` returns early on an empty or unusable name without reprinting anything, which left the old flow showing a bare cursor. Append `"\n\nusername: "` to both of its rejection messages:

```c
        write_to_connection(conn, "You have to enter a name. Try again.\n\nusername: ");
```

```c
        write_to_connection(conn, "Sorry, I couldn't use any of that name. Try again.\n\nusername: ");
```

- [ ] **Step 7: Build and run the test** *(owner-run)*

```bash
cmake --build build --target multizorkd
MULTIZORK_ADDR=127.0.0.1:2323 python3 saturn/tests/test_multizork_join.py
```

Expected: still FAILS, now on the room-name assertion — the lobby does not exist until Task 6. The blocklist and the `username:` prompt are what this task delivers; confirm by hand that connecting shows `username:` and that sending `shell` as the first line drops the connection.

- [ ] **Step 8: Commit**

```bash
git add saturn/multizorkd.c saturn/tests/test_multizork_join.py
git commit -m "Ask a new connection for its name before anything else, which removes the guess the old first prompt had to make between a game code and an access code, and carry the port-scanner blocklist onto the prompt that now receives their first line."
```

---

### Task 5: The room-entry dispatcher

**Files:**
- Modify: `saturn/multizorkd.c` — insert above `inpfn_enter_instance_code_to_join` (1970)

**Interfaces:**
- Consumes: `find_live_instance_by_name` (Task 2), `db_select_my_games`, `Player.claimed`, `Instance.is_private` (Task 3), `reconnect_player` (2120), `db_select_instance`, `db_trim_recap`, `create_instance`, `free_instance`, `inpfn_player_waiting` (1918), `inpfn_ingame` (1785).
- Produces:
  - `static int seat_available_for(const Instance *inst)` — 1 when a stranger may sit down.
  - `static int enter_room_by_name(Connection *conn, const char *name)` — 1 when the connection was placed in a room and its `inputfn` moved, 0 when it was not (a message explaining why has already been written).

- [ ] **Step 1: Add the seat predicate**

Insert above `inpfn_enter_instance_code_to_join`. Before `go` no seat is `claimed` and occupancy is held by `connection`; after `go` `claimed` is what counts. One expression covers both, and Task 11 removes only the `started` line.

```c
/*----------------------
 | seat_available_for
 | Description: Answers whether a stranger may sit down in this room. Privacy is
 |   not consulted here, so entering a private room by name uses the same seat
 |   rule as picking a listed one out of the lobby.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: inst -- the room being considered
 | Returns: 1 when a seat is free, 0 otherwise
 ----------------------*/
static int seat_available_for(const Instance *inst)
{
    if (inst->crashed) {
        return 0;
    } else if (inst->started) {
        return 0;
    }

    for (size_t i = 0; i < ARRAYSIZE(inst->players); i++) {
        const Player *player = &inst->players[i];
        if (!player->claimed && (player->connection == NULL)) {
            return 1;
        }
    }
    return 0;
}
```

- [ ] **Step 2: Add the dispatcher**

Insert directly below `seat_available_for`. `find_own_seat` is a small helper the dispatcher needs twice.

```c
/*----------------------
 | find_own_seat
 | Description: Finds the seat in a room that belongs to a username and has
 |   nobody sitting in it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: inst -- the room to look in
 |   username -- the name to match
 | Returns: the Player, or NULL when no vacant seat carries that name
 ----------------------*/
static Player *find_own_seat(Instance *inst, const char *username)
{
    for (size_t i = 0; i < ARRAYSIZE(inst->players); i++) {
        Player *player = &inst->players[i];
        if (player->claimed && (player->connection == NULL) && (strcmp(player->username, username) == 0)) {
            return player;
        }
    }
    return NULL;
}

/*----------------------
 | enter_room_by_name
 | Description: Puts a connection into the room a name points at, whether that
 |   room is live, private, or archived. Every route into a game goes through
 |   here, so a name a friend read out works in the lobby and nowhere else has
 |   to know how rooms are found.
 | Author: suinevere
 | Dependencies: sqlite3.h
 | Globals: N/A
 | Params: conn -- the connection asking, with its username already set
 |   name -- the room name as typed
 | Returns: 1 when conn was placed and its inputfn moved, 0 when it was not
 ----------------------*/
static int enter_room_by_name(Connection *conn, const char *name)
{
    assert(conn->instance == NULL);

    Instance *inst = find_live_instance_by_name(name);

    if (inst != NULL) {
        Player *mine = find_own_seat(inst, conn->username);
        if (mine != NULL) {
            mine->connection = conn;
            conn->instance = inst;
            conn->inputfn = inpfn_ingame;
            write_to_connection(conn, "Welcome back to '");
            write_to_connection(conn, inst->hash);
            write_to_connection(conn, "'. Here's where you left off:\n\n");
            db_select_recap(mine, 5);
            return 1;
        }

        if (!seat_available_for(inst)) {
            write_to_connection(conn, inst->started ?
                "That game is already under way and has no free seats. Sorry!\n" :
                "That room appears to be full. Too popular!\n");
            return 0;
        }

        for (size_t i = 0; i < ARRAYSIZE(inst->players); i++) {
            if (!inst->players[i].claimed && (inst->players[i].connection == NULL)) {
                inst->players[i].connection = conn;
                conn->instance = inst;
                break;
            }
        }

        if (conn->instance == NULL) {
            write_to_connection(conn, "That room appears to be full. Too popular!
");
            return 0;
        }

        for (size_t i = 0; i < ARRAYSIZE(inst->players); i++) {
            Connection *c = inst->players[i].connection;
            if ((c != NULL) && (c != conn)) {
                write_to_connection(c, "\n*** ");
                write_to_connection(c, conn->username);
                write_to_connection(c, " has joined this game! ***\n>");
            }
        }

        conn->inputfn = inpfn_player_waiting;
        conn->inputfn(conn, "");
        return 1;
    }

    MyGameRow rows[16];
    const int total = db_select_my_games(conn->username, rows, (int) ARRAYSIZE(rows));
    for (int i = 0; i < total; i++) {
        if (strcasecmp(rows[i].name, name) != 0) {
            continue;
        }

        inst = create_instance();
        if (!inst) {
            write_to_connection(conn, "I know that room, but I seem to have run out of memory! Try again later.\n");
            return 0;
        } else if (!db_select_instance(inst, rows[i].dbid)) {
            write_to_connection(conn, "I know that room, but I had trouble starting it up! Try again later.\n");
            free_instance(inst);
            return 0;
        }

        if (inst->crashed) {
            write_to_connection(conn, "I know that room, but that game crashed before and can't be rejoined.
");
            free_instance(inst);
            return 0;
        }

        db_trim_recap(inst);
        loginfo("Rehydrated archived instance '%s'", inst->hash);
        inst->started = 1;

        Player *mine = find_own_seat(inst, conn->username);
        if (mine == NULL) {
            write_to_connection(conn, "I know that room, but I can't find your seat in it. Sorry!\n");
            free_instance(inst);
            return 0;
        }

        mine->connection = conn;
        conn->instance = inst;
        conn->inputfn = inpfn_ingame;
        write_to_connection(conn, "Picking '");
        write_to_connection(conn, inst->hash);
        write_to_connection(conn, "' back up. Here's where you left off:\n\n");
        db_select_recap(mine, 5);
        return 1;
    }

    write_to_connection(conn, "I can't find a room by that name.\n");
    return 0;
}
```

- [ ] **Step 3: Forward-declare what the dispatcher calls ahead of itself**

`db_select_recap` is defined at line 658 and `inpfn_ingame` is already forward-declared at line 1519, so both resolve. `create_instance` (1263), `db_select_instance` (574), `db_trim_recap` (732), `free_instance` (1715) and `inpfn_player_waiting` (1918) all precede line 1970. No new declarations are needed; confirm by building.

- [ ] **Step 4: Build** *(owner-run)*

```bash
cmake --build build --target multizorkd
```

Expected: builds clean. `enter_room_by_name` is not called yet, so expect an unused-function warning until Task 6.

- [ ] **Step 5: Commit**

```bash
git add saturn/multizorkd.c
git commit -m "Resolve a room name in one place whether the room is live, private or archived, so a name a friend read out is entered the same way the lobby enters a room it listed and nothing else has to know how rooms are found."
```

---

### Task 6: The lobby, and creating a room

**Files:**
- Modify: `saturn/multizorkd.c` — replace `inpfn_new_game_or_join` (2025-2071), delete `inpfn_enter_instance_code_to_join` (1970-2023), tail of `inpfn_enter_name` (2113-2117)

**Interfaces:**
- Consumes: `enter_room_by_name`, `seat_available_for` (Task 5), `db_select_my_games`, `MyGameRow` (Task 3), `generate_unique_room_name` (Task 2), `reconnect_player` (2120), `create_instance`, `db_failed_at_instance_start`, `inpfn_waiting_for_players`.
- Produces:
  - `typedef struct LobbyRow { char name[ROOMNAME_MAX]; Instance *live; int mine; int in_progress; int free_seats; int days_ago; } LobbyRow;`
  - `static size_t build_lobby_rows(Connection *conn, LobbyRow *rows, size_t maxrows)`
  - `static void show_lobby(Connection *conn)`, `static void inpfn_lobby(Connection *conn, const char *str)`
  - `static void start_new_room(Connection *conn)`, `static void inpfn_new_room_privacy(Connection *conn, const char *str)`

Room creation lives in this task rather than its own, because `inpfn_lobby` calls `start_new_room` — splitting them would leave a commit that does not link.

- [ ] **Step 1: Delete the code-entry prompt**

Delete `inpfn_enter_instance_code_to_join` in full (lines 1970-2023). Everything it did now lives in `enter_room_by_name`.

- [ ] **Step 2: Forward-declare `reconnect_player`**

`inpfn_lobby` lands at roughly line 2025 and `reconnect_player` is defined at 2120, so the call does not resolve without this. Add it beside the existing `inpfn_ingame` forward declaration at line 1519:

```c
static Player *reconnect_player(Connection *conn, const char *access_code);
```

- [ ] **Step 3: Add the row builder**

One builder feeds both the printed list and the number the player types back, so a number can never mean a different row than the one they read. Insert where `inpfn_new_game_or_join` began (line 2025):

First add the row cap and the per-connection snapshot. A player reads a numbered list, then types a number some seconds later; in between, the poll loop services other connections, any of which can create a room, take the last seat in one, or free one up. Rebuilding the list at resolve time would let `3` mean a different room than the one printed. Snapshotting the names when the list is printed makes the guarantee real, and removes the second `build_lobby_rows` call entirely.

Add near the other `#define`s at the top of the file:

```c
/*----------------------
 | LOBBY_MAX_ROWS
 | Description: Lines the lobby will list, and the size of the snapshot a
 |   connection keeps so a typed number resolves to the row that was printed.
 | Author: suinevere
 ----------------------*/
#define LOBBY_MAX_ROWS 24
```

and to `struct Connection`, below `saw_first_line`:

```c
    char lobby_rows[LOBBY_MAX_ROWS][ROOMNAME_MAX];  // room names as last printed to this connection.
    size_t num_lobby_rows;
```

Then the row type and builder:

```c
/*----------------------
 | LobbyRow
 | Description: One line of the lobby, whether it names a live room or a game
 |   sitting in the database.
 | Author: suinevere
 ----------------------*/
typedef struct LobbyRow
{
    char name[ROOMNAME_MAX];
    Instance *live;
    int mine;
    int in_progress;
    int free_seats;
    int days_ago;
} LobbyRow;

/*----------------------
 | build_lobby_rows
 | Description: Collects the rooms a player may walk into and the games they
 |   already hold a seat in, in the order the lobby prints them. Both the print
 |   and the number-to-row lookup call this, so a typed number always means the
 |   row the player just read.
 | Author: suinevere
 | Dependencies: sqlite3.h
 | Globals: connections, num_connections, GNow
 | Params: conn -- the connection asking, with its username set
 |   rows -- destination array
 |   maxrows -- entries available at rows
 | Returns: the number of rows written
 ----------------------*/
static size_t build_lobby_rows(Connection *conn, LobbyRow *rows, const size_t maxrows)
{
    size_t total = 0;

    for (size_t i = 0; (i < num_connections) && (total < maxrows); i++) {
        Instance *inst = connections[i]->instance;
        if ((inst == NULL) || inst->is_private || !seat_available_for(inst)) {
            continue;
        }

        int already = 0;
        for (size_t j = 0; j < total; j++) {
            already = already || (rows[j].live == inst);
        }
        if (already) {
            continue;
        }

        LobbyRow *row = &rows[total];
        memset(row, '\0', sizeof (*row));
        snprintf(row->name, sizeof (row->name), "%s", inst->hash);
        row->live = inst;
        row->in_progress = inst->started;
        for (size_t j = 0; j < ARRAYSIZE(inst->players); j++) {
            const Player *p = &inst->players[j];
            row->free_seats += (!p->claimed && (p->connection == NULL)) ? 1 : 0;
        }
        total++;
    }

    MyGameRow mine[16];
    const int nmine = db_select_my_games(conn->username, mine, (int) ARRAYSIZE(mine));
    for (int i = 0; (i < nmine) && (total < maxrows); i++) {
        if (find_live_instance_by_name(mine[i].name) != NULL) {
            continue;
        }

        LobbyRow *row = &rows[total];
        memset(row, '\0', sizeof (*row));
        snprintf(row->name, sizeof (row->name), "%s", mine[i].name);
        row->mine = 1;
        row->days_ago = (int) ((((sqlite3_int64) GNow) - ((sqlite3_int64) mine[i].savetime)) / (60 * 60 * 24));
        total++;
    }

    return total;
}
```

- [ ] **Step 4: Add the printer**

Insert directly below `build_lobby_rows`:

```c
/*----------------------
 | show_lobby
 | Description: Prints the lobby. Privacy keeps a room off the joinable list for
 |   strangers; a room the caller holds a seat in still appears under Your games,
 |   private or not.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: conn -- the connection to print to, with its username set
 | Returns: N/A
 ----------------------*/
static void show_lobby(Connection *conn)
{
    LobbyRow rows[LOBBY_MAX_ROWS];
    const size_t total = build_lobby_rows(conn, rows, ARRAYSIZE(rows));
    int printed_joinable = 0;
    int printed_mine = 0;
    char line[160];

    for (size_t i = 0; i < total; i++) {
        if (rows[i].mine) {
            continue;
        } else if (!printed_joinable) {
            write_to_connection(conn, "\nGames waiting for players:\n");
            printed_joinable = 1;
        }

        snprintf(line, sizeof (line), "  %d) %-22s", (int) (i + 1), rows[i].name);
        write_to_connection(conn, line);
        const char *sep = "";
        for (size_t j = 0; j < ARRAYSIZE(rows[i].live->players); j++) {
            Connection *c = rows[i].live->players[j].connection;
            if (c != NULL) {
                write_to_connection(conn, sep);
                write_to_connection(conn, c->username);
                sep = ", ";
            }
        }
        write_to_connection(conn, "\n");
    }

    for (size_t i = 0; i < total; i++) {
        if (!rows[i].mine) {
            continue;
        } else if (!printed_mine) {
            write_to_connection(conn, "\nYour games:\n");
            printed_mine = 1;
        }

        snprintf(line, sizeof (line), "  %d) %-22s (left %d day%s ago)\n",
                 (int) (i + 1), rows[i].name, rows[i].days_ago, (rows[i].days_ago == 1) ? "" : "s");
        write_to_connection(conn, line);
    }

    if (!printed_joinable && !printed_mine) {
        write_to_connection(conn, "\nNobody's playing right now. You could be the first.\n");
    }

    conn->num_lobby_rows = total;
    for (size_t i = 0; i < total; i++) {
        snprintf(conn->lobby_rows[i], sizeof (conn->lobby_rows[i]), "%s", rows[i].name);
    }

    write_to_connection(conn, "\nType a number, or a room name if someone gave you one.\n");
    write_to_connection(conn, "  n) start a new room     q) quit\n\n> ");
}
```

- [ ] **Step 5: Add room creation**

Insert directly below `show_lobby`:

```c
/*----------------------
 | inpfn_new_room_privacy
 | Description: Asks whether a freshly made room should appear in the lobby, then
 |   drops the host into the waiting room either way.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: conn -- the host, already holding a seat in the new room
 |   str -- their answer
 | Returns: N/A
 ----------------------*/
static void inpfn_new_room_privacy(Connection *conn, const char *str)
{
    if ((strcasecmp(str, "y") == 0) || (strcasecmp(str, "yes") == 0)) {
        conn->instance->is_private = 0;
        write_to_connection(conn, "Listed. Strangers can wander in.\n\n");
    } else if ((strcasecmp(str, "n") == 0) || (strcasecmp(str, "no") == 0)) {
        conn->instance->is_private = 1;
        write_to_connection(conn, "Kept quiet. Only people you give the name to can get in.\n\n");
    } else {
        write_to_connection(conn, "Please answer 'y' or 'n'.\n\n> ");
        return;
    }

    conn->inputfn = inpfn_waiting_for_players;
    write_to_connection(conn, "We'll wait for people now.\n");
    write_to_connection(conn, "Type 'go' to begin when enough have arrived.\n");
    write_to_connection(conn, "There's still room for three more people.\n");
    write_to_connection(conn, "Type 'quit' to drop this room and anyone connected.\n");
    write_to_connection(conn, "\n\nWhile we're waiting, let me say I built this for my patrons. If you like\n");
    write_to_connection(conn, "this sort of thing, please send a dollar to https://patreon.com/icculus !\n\n> ");
}

/*----------------------
 | start_new_room
 | Description: Makes a room, names it, seats the host, and asks whether it
 |   should be listed.
 | Author: suinevere
 | Dependencies: roomnames.h
 | Globals: N/A
 | Params: conn -- the connection asking for a new room
 | Returns: N/A
 ----------------------*/
static void start_new_room(Connection *conn)
{
    assert(!conn->instance);
    conn->instance = create_instance();
    if (!conn->instance) {
        write_to_connection(conn, "Uhoh, we appear to be out of memory. Try again later?\n");
        drop_connection(conn);
        return;
    }

    if (!generate_unique_room_name(conn->instance->hash)) {
        write_to_connection(conn, "Uhoh, we appear to be having a database problem. Try again later?\n");
        Instance *inst = conn->instance;
        conn->instance = NULL;
        db_failed_at_instance_start(inst);
        drop_connection(conn);
        return;
    }

    loginfo("Created new instance '%s'", conn->instance->hash);
    conn->instance->players[0].connection = conn;

    write_to_connection(conn, "\nYour room is called '");
    write_to_connection(conn, conn->instance->hash);
    write_to_connection(conn, "'.\nTell your friends to telnet here and type that. Capitals don't matter.\n\n");
    write_to_connection(conn, "Should I list it in the lobby, so strangers can wander in? (y/n)\n\n> ");
    conn->inputfn = inpfn_new_room_privacy;
}
```

- [ ] **Step 6: Add the lobby prompt**

Insert directly below `start_new_room`:

```c
/*----------------------
 | inpfn_lobby
 | Description: Takes a list number, a room name, a player access code, or one of
 |   the two letter commands, and hands anything room-shaped to the dispatcher.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: conn -- the connection at the lobby
 |   str -- what they typed
 | Returns: N/A
 ----------------------*/
static void inpfn_lobby(Connection *conn, const char *str)
{
    if (strcasecmp(str, "q") == 0) {
        write_to_connection(conn, "\n\nOkay, bye for now!\n\n");
        drop_connection(conn);
        return;
    } else if (strcasecmp(str, "n") == 0) {
        start_new_room(conn);
        return;
    }

    char picked[ROOMNAME_MAX];
    if (*str && (strspn(str, "0123456789") == strlen(str))) {
        const int choice = atoi(str);
        if ((choice >= 1) && (((size_t) choice) <= conn->num_lobby_rows)) {
            snprintf(picked, sizeof (picked), "%s", conn->lobby_rows[choice - 1]);
            str = picked;
        }
    }

    if (enter_room_by_name(conn, str)) {
        return;
    }

    if (strlen(str) == 6) {
        Player *player = reconnect_player(conn, str);
        if (player) {
            write_to_connection(conn, "We found you! Here's where you left off:\n\n");
            db_select_recap(player, 5);
            if (player->game_over) {
                drop_connection(conn);
            }
            return;
        }
    }

    show_lobby(conn);
}
```

- [ ] **Step 7: Point the name prompt at the lobby**

At the tail of `inpfn_enter_name`, replace the four-line menu (lines 2113-2117) with:

```c
    conn->inputfn = inpfn_lobby;
    show_lobby(conn);
```

- [ ] **Step 8: Build and run the join test** *(owner-run)*

```bash
cmake --build build --target multizorkd
MULTIZORK_ADDR=127.0.0.1:2323 python3 saturn/tests/test_multizork_join.py
```

Expected: `test_multizork_join: OK`. All four assertions pass — the lobby lists a waiting room, a room name joins it, the name is case-insensitive, and an access code still rejoins.

- [ ] **Step 9: Commit**

```bash
git add saturn/multizorkd.c
git commit -m "Replace the three-item menu with a lobby that shows the rooms a player can walk into and the games they already have a seat in, naming a new room out loud and asking its host once whether strangers may find it, and building the printed list and the number lookup from one function so a typed number always means the row that was read."
```

---

### Task 7: Games of yours that are still running

**Files:**
- Modify: `saturn/multizorkd.c` — `build_lobby_rows` and `show_lobby` (Task 6), `inpfn_waiting_for_players` (1871-1916)

**Interfaces:**
- Consumes: `build_lobby_rows`, `LobbyRow` (Task 6), `find_own_seat` (Task 5).
- Produces: no new symbols.

Task 6 lists only archived games under "Your games", so a player who drops out of a game still running sees nothing and has to remember the room name. `enter_room_by_name` already rejoins them; this makes the lobby offer it.

- [ ] **Step 1: Collect live games where your seat is empty**

In `build_lobby_rows`, insert this loop between the joinable-rooms loop and the `db_select_my_games` block:

```c
    for (size_t i = 0; (i < num_connections) && (total < maxrows); i++) {
        Instance *inst = connections[i]->instance;
        if ((inst == NULL) || (find_own_seat(inst, conn->username) == NULL)) {
            continue;
        }

        int already = 0;
        for (size_t j = 0; j < total; j++) {
            already = already || (rows[j].live == inst);
        }
        if (already) {
            continue;
        }

        LobbyRow *row = &rows[total];
        memset(row, '\0', sizeof (*row));
        snprintf(row->name, sizeof (row->name), "%s", inst->hash);
        row->live = inst;
        row->mine = 1;
        row->in_progress = 1;
        total++;
    }
```

A room where you hold a seat is yours whether or not it is private, and whether or not a stranger could also join it — the `already` test keeps it out of the joinable list if it appeared there first.

- [ ] **Step 2: Print in-progress rows differently**

In `show_lobby`'s "Your games" loop, replace the single `snprintf` with a fork on `in_progress`, since a running game has no "left N days ago" to report:

```c
        if (rows[i].in_progress) {
            snprintf(line, sizeof (line), "  %d) %-22s (in progress)\n", (int) (i + 1), rows[i].name);
        } else {
            snprintf(line, sizeof (line), "  %d) %-22s (left %d day%s ago)\n",
                     (int) (i + 1), rows[i].name, rows[i].days_ago, (rows[i].days_ago == 1) ? "" : "s");
        }
        write_to_connection(conn, line);
```

- [ ] **Step 3: Correct `show_lobby`'s header block**

This task falsifies a claim that header makes. Before it, a private room genuinely never appeared in the lobby. Now a private room where the caller holds a vacant seat is surfaced by the new pass and printed under "Your games" — which is the intended behaviour, since a seat is yours regardless of privacy. Reword the `Description` field from:

```
 | Description: Prints the lobby. A private room never appears here, which is the
 |   only thing privacy does; its name still opens it.
```

to:

```
 | Description: Prints the lobby. Privacy keeps a room off the joinable list for
 |   strangers; a room the caller holds a seat in still appears under Your games,
 |   private or not.
```

- [ ] **Step 4: Read out the room name in the waiting room**

`inpfn_waiting_for_players` never says which room it is, which is awkward now that rooms have names worth repeating. Replace the line `write_to_connection(conn, "Still waiting for people to join.\n");` with:

```c
        write_to_connection(conn, "Still waiting for people to join '");
        write_to_connection(conn, inst->hash);
        write_to_connection(conn, "'.\n");
```

- [ ] **Step 5: Verify by hand** *(owner-run)*

```bash
cmake --build build --target multizorkd
```

Start a two-player game, disconnect one player without quitting, reconnect with the same username, and confirm the room appears under "Your games" marked `(in progress)` and that picking its number drops you back into the game. Task 8 covers the archived half of the same list automatically.

- [ ] **Step 6: Commit**

```bash
git add saturn/multizorkd.c
git commit -m "Offer a player back the game they are still in, not just the ones that have been archived, so dropping a connection mid-game no longer means remembering the room name to get back to a seat nobody else can take."
```

---

### Task 8: Lobby, privacy and resume tests

**Files:**
- Create: `saturn/tests/test_multizork_lobby.py`

**Interfaces:**
- Consumes: the whole of Phase 1.
- Produces: nothing consumed by later tasks; Task 12 appends to this file.

- [ ] **Step 1: Write the test**

```python
#!/usr/bin/env python3
"""Drive multizorkd's lobby over TCP: listing, privacy, case, and resume.

Checks the promises the lobby makes that no unit test can reach, because they
are all about what one connection can see of another's room. A private room is
the interesting case: absent from the list, but still enterable by anyone told
its name, which is the whole point of hiding it.

Drives a running daemon rather than spawning one: multizorkd is POSIX-only and
wants sqlite3, so the caller supplies the address. Skips when nothing answers.

  MULTIZORK_ADDR=127.0.0.1:2323 python3 tests/test_multizork_lobby.py
"""
import os, re, socket, sys, time

ADDR = os.environ.get("MULTIZORK_ADDR", "127.0.0.1:2323")
HOST, _, PORT = ADDR.partition(":")
PORT = int(PORT or 2323)

SETTLE = 0.4

def connect():
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.settimeout(SETTLE)
    return s

def drain(sock):
    out = b""
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk: break
            out += chunk
    except socket.timeout:
        pass
    return out.decode("latin-1")

def send(sock, line):
    sock.sendall((line + "\r\n").encode("latin-1"))
    time.sleep(SETTLE)
    return drain(sock)

def check(label, condition, saw):
    if condition:
        return 0
    print(f"FAIL: {label}\n  saw: {saw!r}", file=sys.stderr)
    return 1

def make_room(username, listed):
    """Return (connection, room name) for a host in the waiting room."""
    c = connect()
    drain(c)
    send(c, username)
    out = send(c, "n")
    m = re.search(r"room is called '([\w-]+)'", out)
    if not m:
        print(f"FAIL: host never got a room name\n  saw: {out!r}", file=sys.stderr)
        sys.exit(1)
    send(c, "y" if listed else "n")
    return c, m.group(1)

def main():
    try:
        probe = connect()
    except OSError:
        print("test_multizork_lobby: SKIP (nothing listening on %s)" % ADDR)
        return
    fails = 0
    greeting = drain(probe)
    fails += check("first prompt asks for a username", "username:" in greeting, greeting)
    probe.close()

    # Whatever else is going on, the lobby always offers the two standing choices.
    # That is the "default" a player lands on when nothing is listed at all.
    solo = connect(); drain(solo)
    out = send(solo, "wanderer")
    fails += check("lobby offers a new room", "start a new room" in out, out)
    fails += check("lobby offers to quit", "quit" in out, out)
    solo.close()

    # A generated room name is two real words joined by a hyphen.
    host, room = make_room("seanie", listed=True)
    fails += check("room name is an adjective-noun pair",
                   re.fullmatch(r"[a-z]+-[a-z]+(-\d\d)?", room) is not None, room)

    # A listed room shows up for someone else, named, with its occupants.
    guest = connect(); drain(guest)
    out = send(guest, "ashley")
    fails += check("listed room appears in the lobby", room in out, out)
    fails += check("lobby names who is waiting", "seanie" in out, out)
    guest.close()

    # A private room does not appear, but its name still opens it.
    quiet_host, quiet = make_room("mira", listed=False)
    guest = connect(); drain(guest)
    out = send(guest, "bob")
    fails += check("the lobby answered at all", "start a new room" in out, out)
    fails += check("private room is absent from the lobby", quiet not in out, out)
    out = send(guest, quiet)
    fails += check("private room opens to anyone told its name",
                   "guest list" in out.lower(), out)
    guest.close(); quiet_host.close()

    # Case is ignored, because the name is meant to be read aloud.
    guest = connect(); drain(guest)
    send(guest, "bob")
    out = send(guest, room.upper())
    fails += check("room name is case-insensitive", "guest list" in out.lower(), out)
    guest.close()

    # Bring a second player in so the game outlives the host walking away.
    stayer = connect(); drain(stayer)
    send(stayer, "ashley")
    send(stayer, room)
    out = send(host, "go")
    fails += check("game starts", "THE GAME IS STARTING" in out, out)

    # The host drops. The game is still running, so it is offered back as such.
    host.close()
    time.sleep(1.0)
    back = connect(); drain(back)
    out = send(back, "seanie")
    fails += check("a game still running is offered back", room in out, out)
    fails += check("and marked as still running", "(in progress)" in out, out)
    out = send(back, room)
    fails += check("rejoining reaches the game prompt", ">" in out, out)
    back.close()
    time.sleep(1.0)

    # Now everybody leaves, so the same game is offered back from the archive.
    stayer.close()
    time.sleep(1.0)
    back = connect(); drain(back)
    out = send(back, "seanie")
    fails += check("an archived game is offered back", room in out, out)
    fails += check("and marked as left behind", "ago)" in out, out)
    out = send(back, room)
    fails += check("resuming reaches the game prompt", ">" in out, out)
    back.close()

    # Somebody else does not see it.
    other = connect(); drain(other)
    out = send(other, "nobody")
    fails += check("the lobby answered a different name at all", "start a new room" in out, out)
    fails += check("another name is not offered that game", room not in out, out)
    other.close()

    if fails:
        print(f"test_multizork_lobby: {fails} FAILED", file=sys.stderr)
        sys.exit(1)
    print("test_multizork_lobby: OK")

main()
```

- [ ] **Step 2: Run it** *(owner-run)*

```bash
MULTIZORK_ADDR=127.0.0.1:2323 python3 saturn/tests/test_multizork_lobby.py
```

Expected: `test_multizork_lobby: OK`. If "your own game is offered back to you" fails, the likely cause is that `free_instance` had not finished archiving when the reconnect ran — raise the `time.sleep(1.0)` before treating it as a bug.

- [ ] **Step 3: Commit**

```bash
git add saturn/tests/test_multizork_lobby.py
git commit -m "Cover what only a second connection can see of the lobby: that a listed room is named with its occupants, that a private one is missing from the list yet opens to anyone told its name, and that a game comes back to the player who was in it and to nobody else."
```

**Phase 1 is complete and shippable here.** Started games are still closed to strangers.

---

## Phase 2 — Preallocated seats

### Task 9: One seat accessor, and the ghost watch

**Files:**
- Modify: `saturn/multizorkd.c` — `Instance` (129-142), `getObjectPtr` (1043-1067), `getObjectProperty` (1081-1123), `opcode_get_prop_addr_multizork` (1132-1155), `opcode_print_obj_multizork` (1156-1189)

**Interfaces:**
- Consumes: `Player.claimed` (Task 3).
- Produces: `static Player *get_seat(Instance *inst, int seatnum, uint16 objid)`, and `Instance.ghost_watch` / `Instance.ghost_logged`.

There are **four** places that bounds-check a seat index against `num_players`, not two. All four become calls to one accessor, which is also the only place the ghost watch has to live.

- [ ] **Step 1: Add the two instance fields**

In `struct Instance`, below `int is_private;`:

```c
    int ghost_watch;         // switched on once start_instance is done building seats.
    uint8 ghost_logged;      // one bit per seat, so a wandering thief logs once and not every turn.
```

- [ ] **Step 2: Add the accessor**

Insert directly above `remap_objectid` (line 1020):

```c
/*----------------------
 | get_seat
 | Description: Resolves a multiplayer object index to its seat, and notes the
 |   first time the game reaches a seat nobody is sitting in. That can happen:
 |   an unclaimed seat is on West of House's child list from the first turn, so
 |   the thief on his rounds can find one. It is allowed, since it only makes the
 |   game slightly easier, but it should not happen unobserved.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: GState
 | Params: inst -- the instance owning the seat
 |   seatnum -- the index, already relative to ZORK1_EXTERN_MEM_OBJS_BASE
 |   objid -- the object id asked for, for the log line
 | Returns: the seat; does not return when seatnum is out of range
 ----------------------*/
static Player *get_seat(Instance *inst, const int seatnum, const uint16 objid)
{
    if ((seatnum < 0) || (seatnum >= inst->num_players)) {
        GState->die("Invalid multiplayer object id referenced");
    }

    Player *player = &inst->players[seatnum];
    if (inst->ghost_watch && !player->claimed && !(inst->ghost_logged & (1 << seatnum))) {
        inst->ghost_logged |= (uint8) (1 << seatnum);
        loginfo("Instance '%s': game logic touched unclaimed seat %d (object %u)",
                inst->hash, seatnum, (unsigned int) objid);
    }
    return player;
}
```

- [ ] **Step 3: Route all four sites through it**

In `getObjectPtr` (1054-1060), replace the body of the external-object branch with:

```c
        Instance *inst = (Instance *) GState;  // this works because zmachine_state is the first field in Instance.
        ptr = get_seat(inst, (int) (objid - external_mem_objects_base), objid)->object_table_data;
```

In `getObjectProperty` (1087-1093):

```c
            Instance *inst = (Instance *) GState;  // this works because zmachine_state is the first field in Instance.
            ptr = get_seat(inst, (int) (objid - external_mem_objects_base), objid)->property_table_data;
```

In `opcode_get_prop_addr_multizork` (1145-1151), replace the bounds check and both uses of `requested_player`:

```c
        const int requested_player = (int) (objid - external_mem_objects_base);
        const Player *seat = get_seat(inst, requested_player, objid);
        result = fake_prop_base_addr + (MULTIPLAYER_PROP_DATALEN * requested_player);  // we give each player FAKE bytes at the end of the address space.
        result += (uint16) ((size_t) (ptr - seat->property_table_data));
```

In `opcode_print_obj_multizork` (1165-1170):

```c
            ptr = get_seat(inst, (int) (objid - external_mem_objects_base), objid)->property_table_data + 1;
```

`opcode_print_obj_multizork` matters here: it does not go through `getObjectPtr` for external objects, so a probe placed only in `getObjectPtr` would miss the game printing a dormant seat's name.

- [ ] **Step 4: Build** *(owner-run)*

```bash
cmake --build build --target multizorkd
```

Expected: builds clean, behaviour unchanged — `ghost_watch` is still zero everywhere, so nothing logs.

- [ ] **Step 5: Commit**

```bash
git add saturn/multizorkd.c
git commit -m "Resolve a multiplayer object to its seat in one accessor rather than repeating the same bounds check at four call sites, and have that accessor note once per seat when the game reaches one nobody is sitting in."
```

---

### Task 10: Build all four seats at go

**Files:**
- Modify: `saturn/multizorkd.c` — `start_instance` (1521-1697)

**Interfaces:**
- Consumes: `Player.claimed` (Task 3), `Instance.ghost_watch` (Task 9).
- Produces: after `start_instance`, `inst->num_players` is always `ARRAYSIZE(inst->players)` and every seat has a valid Z-machine continuation; unclaimed seats carry `claimed == 0`, an empty `username`, and both hiding attributes set.

- [ ] **Step 1: Seat everyone, then fill the rest**

Replace the flattening block at the top of `start_instance` (lines 1523-1535) with:

```c
    size_t num_players = 0;
    Player players[ARRAYSIZE(inst->players)];
    const uint32 entrypoint = inst->players[0].next_logical_pc;
    memset(players, '\0', sizeof (players));
    for (size_t i = 0; i < ARRAYSIZE(inst->players); i++) {
        Connection *conn = inst->players[i].connection;
        if (conn) {
            memcpy(&players[num_players], &inst->players[i], sizeof (Player));
            players[num_players].claimed = 1;
            num_players++;
        }
    }
    const size_t num_claimed = num_players;
    for (size_t i = num_players; i < ARRAYSIZE(players); i++) {
        players[i].next_logical_pc = entrypoint;
        num_players++;
    }
    memcpy(inst->players, players, sizeof (inst->players));
    inst->num_players = (int) num_players;
```

`num_players` is now always 4 and `num_claimed` is how many are real people. Every loop below that should touch only real people uses `num_claimed`.

`entrypoint` is read before the compaction loop rather than per-seat afterwards: `create_instance` sets every seat to the same game entry point and nothing has stepped yet, but compaction overwrites the tail of the array, so reading `inst->players[i]` for an empty seat after the loop would sometimes read a seat that was just moved.

- [ ] **Step 2: Extract the property-table builder**

Task 12 has to rebuild a seat's name property when someone claims it, and the only correct way to do that is the way `start_instance` already does it. Extract it once rather than writing a second, subtly different copy. Insert directly above `start_instance` (line 1521):

```c
/*----------------------
 | write_player_name_property
 | Description: Builds a seat's property table: its ZSCII-encoded short name
 |   followed by a fresh copy of the pristine player object's properties. Called
 |   once per seat when a game starts, and again when a late arrival claims one,
 |   so a seat's name can change without the properties behind it drifting.
 | Author: suinevere
 | Dependencies: mojozork.c
 | Globals: GState
 | Params: player -- the seat whose table is being written
 |   name -- lowercase ASCII short name, at most 15 characters
 | Returns: N/A
 ----------------------*/
static void write_player_name_property(Player *player, const char *name)
{
    const uint8 *playerptr = GState->story + GState->header.objtab_addr;
    playerptr += 31 * sizeof (uint16);  // skip properties defaults table
    playerptr += 9 * (ZORK1_PLAYER_OBJID-1);  // find object in object table  // ZORK 1 SPECIFIC MAGIC

    const uint8 *propptr = playerptr + 7;  // skip to properties address field.
    const uint16 propaddr = READUI16(propptr);
    propptr = GState->story + propaddr;
    propptr += (*propptr * 2) + 1;  // skip object name to start of properties.

    uint16 propsize = 0;
    while (propptr[propsize]) {
        propsize += (((propptr[propsize] >> 5) & 0x7) + 1) + 1;
    }

    memset(player->property_table_data, '\0', sizeof (player->property_table_data));

    uint8 *propdst = player->property_table_data;
    propdst++;  // text-length (number of 2-byte words). Skip for now.

    // Encode the player's name to ZSCII. We cheat and only let you have
    // lowercase letters for now (!!! FIXME: but a better ZSCII encoder here would open options)
    uint8 numwords = 0;
    const char *str = name;
    while (*str) {
        const uint16 zch1 = (uint8) ((*(str++) - 'a') + 6);
        const uint16 zch2 = *str ? ((uint8) ((*(str++) - 'a') + 6)) : 5;  // 5 is a padding character at end of string.
        const uint16 zch3 = *str ? ((uint8) ((*(str++) - 'a') + 6)) : 5;  // 5 is a padding character at end of string.
        const uint16 termbit = ((*str == '\0') || (zch2 == 5) || (zch3 == 5)) ? (1 << 15) : 0;
        const uint16 zword = (zch1 << 10) | (zch2 << 5) | zch3 | termbit;
        WRITEUI16(propdst, zword);
        numwords++;
    }
    *player->property_table_data = numwords;

    assert((propsize + (numwords * 2) + 1) <= sizeof (player->property_table_data));
    memcpy(propdst, propptr, propsize);
}
```

The `memset` is what makes this safe to call a second time. `start_instance` relies on the table arriving zeroed, so the property block needs no explicit terminator; without the memset a rewrite would leave the previous name's tail behind it.

- [ ] **Step 2a: Call it from `start_instance`**

Delete these now-redundant blocks from `start_instance`: the `playerptr`/`propptr`/`propsize` derivation above the loop (lines 1540-1554, including its `assert`) and, inside the loop, everything from `// Copy the original player object for each player.` through the `memcpy(propdst, propptr, propsize);` at line 1600 — *except* the `memcpy(player->object_table_data, playerptr, ...)` at line 1578, which the helper does not do.

The loop's name and property setup becomes:

```c
        if (conn) {
            snprintf(player->username, sizeof (player->username), "%s", conn->username);
        } else {
            player->username[0] = '\0';
        }

        memcpy(player->object_table_data, GState->story + GState->header.objtab_addr +
               (31 * sizeof (uint16)) + (9 * (ZORK1_PLAYER_OBJID-1)), sizeof (player->object_table_data));
        write_player_name_property(player, player->claimed ? player->username : "guest");
```

`GState` is already set to `&inst->zmachine_state` at line 1538, above the loop, so the helper resolves the story correctly.

Delete the duplicate `snprintf(player->username, ...)` at line 1606; it repeats what this block already did.

An unclaimed seat is named `guest` in its property table but carries an empty `username`, which is what keeps `db_select_my_games`'s `p.claimed <> 0` term from ever matching it.

- [ ] **Step 3: Guard the per-connection output**

Still in that loop, the block from `write_to_connection(conn, "\n\n");` through `conn->inputfn = inpfn_ingame;` (lines 1607-1615) writes to `conn`, which is NULL for an empty seat. Wrap it:

```c
        if (conn) {
            write_to_connection(conn, "\n\n");
            write_to_connection(conn, "*** THE GAME IS STARTING ***\n");
            write_to_connection(conn, "You can leave at any time by typing 'quit'.\n");
            write_to_connection(conn, "You can speak to others in the same room with '!some text' or the whole game with '!!some text'.\n");
            write_to_connection(conn, "If you get disconnected or leave, you can rejoin at any time\n");
            write_to_connection(conn, " with this access code: '");
            write_to_connection(conn, player->hash);
            write_to_connection(conn, "'\n\n(Have fun!)\n\n\n");
            conn->inputfn = inpfn_ingame;
        }
```

The `generate_unique_hash(player->hash)` above it stays outside the guard: every seat gets its access code at `go`, so claiming later reveals a code rather than creating one and a seat's identity never changes.

- [ ] **Step 4: Hide the empty seats**

In the intro-run loop, the inner `for (int j = 0; j < num_players; j++)` block (lines 1645-1659) clears INVISIBLE and NDESCBIT to make a player visible. Replace the two `opcode_clear_attr()` calls with a branch:

```c
            GState->operands[0] = external_mem_objects_base + j;
            GState->operands[1] = 0x07;  // INVISIBLE bit
            if (inst->players[j].claimed) { opcode_clear_attr(); } else { opcode_set_attr(); }
            GState->operands[0] = external_mem_objects_base + j;
            GState->operands[1] = 0x0E;  // NDESCBIT bit
            if (inst->players[j].claimed) { opcode_clear_attr(); } else { opcode_set_attr(); }
```

- [ ] **Step 5: Step only the real players, but write all four rows**

Change the **intro-run loop only** (line 1632) to run over real people:

```c
    for (int i = 0; i < (int) num_claimed; i++) {
```

**Do not widen this loop to all four seats.** `step_instance` returns early for a seat with no connection (`if (player->connection == NULL) { return 1; }`), so extra iterations would step nothing — but the loop body would still run its `memcpy(story, GOriginalStory, staticmem_addr)` reset first. The loop would therefore *end* with dynamic memory pristine, discarding the last real player's intro run along with Zork's CLOCK and queue tables: no thief, no lantern burn-down, no candle timers. Transcripts and tests would still look correct, because each player's intro text was already written to their connection before the reset. Silent and gameplay-breaking.

Unclaimed seats get their continuation in Step 5a instead.

**Leave the database loop at line 1679 at `inst->num_players`** — all four rows must be written. `db_insert_instance` binds `inst->num_players` (now always 4) into the instances row, and `db_select_instance` refuses to load an instance whose player row count disagrees with it (lines 648-651). Writing only `num_claimed` rows would make every game saved under this phase impossible to rehydrate — a silent data-loss bug none of this plan's tests would catch, because they never restart the daemon.

Inside that loop, only the transcript insert is per-connection. Replace its body with:

```c
        for (int i = 0; i < inst->num_players; i++) {
            Player *player = &inst->players[i];
            if (dbokay) {
                player->dbid = db_insert_player(inst, i);
                dbokay = dbokay && (player->dbid != 0);
                if (player->connection) {
                    dbokay = dbokay && db_insert_transcript(player->dbid, TT_GAME_OUTPUT, player->connection->outputbuf + outputbuf_used_at_start[i]);
                }
            }
        }
```

`outputbuf_used_at_start` is only filled for indices below `num_claimed`, but it is read only inside the `player->connection` guard, and an unclaimed seat has none.

The inner `for (int j = 0; j < num_players; j++)` at line 1645 stays at 4, because all four objects have to be threaded into the child list. Line 1649's `(j < (num_players-1))` sibling chain is correct as-is with `num_players` at 4.

- [ ] **Step 5a: Give the unclaimed seats a parked continuation**

An unclaimed seat is never stepped, so its `next_logical_pc` is still the game entry point that `create_instance` set. That is the one state it must not be left in: when Task 12 claims the seat and calls `step_instance`, the interpreter would run Zork's startup routine against **live, mid-game** dynamic memory, re-initialising globals and world state under the players already in the room.

Copy a claimed seat's continuation instead. Every player's intro ran the same code from the same pristine memory and stopped at the same READ — the only difference between their runs was which object the PLAYER global pointed at, which lives in memory, not on the stack — so seat 0's parked state is valid for any seat. Insert this immediately after the intro-run loop and before the database loop:

```c
    for (size_t i = num_claimed; i < ARRAYSIZE(inst->players); i++) {
        Player *seat = &inst->players[i];
        const Player *src = &inst->players[0];
        seat->next_logical_pc = src->next_logical_pc;
        seat->next_logical_sp = src->next_logical_sp;
        seat->next_logical_bp = src->next_logical_bp;
        seat->next_inputbuf = src->next_inputbuf;
        seat->next_inputbuflen = src->next_inputbuflen;
        seat->next_operands[0] = src->next_operands[0];
        seat->next_operands[1] = src->next_operands[1];
        memcpy(seat->stack, src->stack, sizeof (seat->stack));
        snprintf(seat->againbuf, sizeof (seat->againbuf), "%s", src->againbuf);
        seat->gvar_location = src->gvar_location;
        seat->gvar_coffin_held = src->gvar_coffin_held;
        seat->gvar_dead = src->gvar_dead;
        seat->gvar_deaths = src->gvar_deaths;
        seat->gvar_lit = src->gvar_lit;
        seat->gvar_alwayslit = src->gvar_alwayslit;
        seat->gvar_verbose = src->gvar_verbose;
        seat->gvar_superbrief = src->gvar_superbrief;
        seat->gvar_lucky = src->gvar_lucky;
        seat->gvar_loadallowed = src->gvar_loadallowed;
    }
```

**The `gvar_*` block is not optional.** `step_instance` swaps those ten fields *into* the live globals array before resuming any continuation and saves them back out after the turn. The per-seat setup loop captures them for all four seats from **pristine** globals, before any intro has run; only claimed seats then get post-intro values written back by their own intro step. Copying the stack without them would leave an unclaimed seat resuming a post-intro program counter against a pre-intro world — `gvar_location` alone would put the newcomer somewhere its PC does not expect to be. That is exactly the compiler-invisible, only-shows-up-in-play failure this step exists to prevent.

`next_inputbuf` is a pointer into the instance's own story buffer, which every seat shares, so copying it is correct. `num_claimed` is at least 1 — a game cannot reach `go` without its host connected.

This leaves dynamic memory exactly as the last real player's intro left it, which is what preserves the CLOCK and queue tables, while still giving every unclaimed seat a valid READ to wake up on.

- [ ] **Step 5b: Leave a failed instance immediately instead of breaking**

Inside the intro-run loop, the failure branch currently reads:

```c
        if (!step_instance(inst, i, NULL)) {
            break;  // instance failed, don't access it further.
        }
```

`step_instance` calls `free_instance(inst)` on its fatal-error path before returning 0, so `inst` is already freed here. `break` then falls straight into the Step 5a copy loop and the database loop, both of which walk `inst->players` — a use-after-free. The comment already says "don't access it further"; the control flow does not honour it. Change it to:

```c
        if (!step_instance(inst, i, NULL)) {
            return;  // instance failed and freed itself, don't access it further.
        }
```

This predates the plan, but Step 5a adds another loop inside the exposed window, so it is fixed here rather than left to grow.

- [ ] **Step 6: Arm the ghost watch last**

At the very end of `start_instance`. **`db_failed_at_instance_start` frees the instance** — it calls `free_instance(inst)`, which calls `free(inst)` — so the arming line must not sit after that call unguarded, or it writes through a dangling pointer. Return from the failure branch instead, matching the idiom already used at the earlier `!dbokay` site:

```c
    if (!dbokay) {
        db_failed_at_instance_start(inst);
        return;
    }

    inst->ghost_watch = 1;
```

It cannot key off `inst->started`, which is set at line 1623 *before* the intro loop runs — the hiding code in Step 4 reaches player objects through `opcode_set_attr` and would otherwise trip its own probe on every instance.

- [ ] **Step 7: Build and confirm a two-player game is unchanged** *(owner-run)*

```bash
cmake --build build --target multizorkd
MULTIZORK_ADDR=127.0.0.1:2323 python3 saturn/tests/test_multizork_join.py
MULTIZORK_ADDR=127.0.0.1:2323 python3 saturn/tests/test_multizork_lobby.py
```

Expected: both `OK`. Then play a two-player game by hand for a few turns and confirm no ghost appears in a room description, and watch the log for `touched unclaimed seat`. A hit there is information, not a failure.

- [ ] **Step 8: Commit**

```bash
git add saturn/multizorkd.c
git commit -m "Build all four Zork 1 player seats when a game starts rather than only the occupied ones, because resetting dynamic memory between players is legal only at that moment and it is the one chance to give a seat nobody has taken yet a program counter and stack it can be woken from later."
```

---

### Task 11: Teach every player loop about empty seats

**Files:**
- Modify: `saturn/multizorkd.c` — `db_trim_recap` (732-743), `broadcast_to_instance` (956-967), `broadcast_to_room` (969-982), `save_instance` (1699-1713), `step_instance` (1476), `inpfn_waiting_for_players` (1893-1901), `inpfn_player_waiting` (1928-1935), `reconnect_player` (2135, 2189)

**Interfaces:**
- Consumes: `Player.claimed` (Task 3).
- Produces: no new symbols. Every loop over seats either skips unclaimed ones or is documented as correct across all four.

With `num_players` pinned at 4, every one of these loops now visits two seats that are not people. Each needs a decision, and getting one wrong is where the bugs in this phase will be.

- [ ] **Step 1: Skip unclaimed seats in the two broadcasts**

Both already iterate all four slots. `write_to_connection` tolerates a NULL connection, but `db_insert_transcript(player->dbid, ...)` with `dbid == 0` logs a database error on every broadcast. `broadcast_to_room` is the worse case: unclaimed seats sit in West of House, so any broadcast to object 180 hits them. In `broadcast_to_instance`, after `Player *player = &inst->players[i];`:

```c
            if (!player->claimed) { continue; }
```

In `broadcast_to_room`, the same line in the same position, before the `gvar_location` test.

- [ ] **Step 2: Skip unclaimed seats in the database loops**

In `db_trim_recap`, change the loop body's first statement to guard on the seat:

```c
    for (int i = 0; i < inst->num_players; i++) {
        if (!inst->players[i].claimed) { continue; }
```

In `save_instance`, the `db_update_player(inst, i)` loop must still write unclaimed seats — their frozen continuation is exactly what a later joiner inherits, and dropping them would break `db_select_instance`'s row-count check. **Leave this loop alone**, and add nothing to it.

- [ ] **Step 3: Leave the endgame touchbit loop alone**

`step_instance`'s loop at line 1476 clears the West of House touchbit for every seat so anyone who wanders back is told about the stone barrow. An unclaimed seat inheriting that is correct — a player who claims it afterwards should hear about the barrow too. **No change.** Note it in the commit so a later reader does not "fix" it.

- [ ] **Step 4: Skip unclaimed seats in the two guest lists**

`inpfn_waiting_for_players` and `inpfn_player_waiting` both print seats with a non-NULL connection, and an unclaimed seat has none, so both are already correct. **No change**, but confirm by reading each loop.

- [ ] **Step 5: Skip unclaimed seats in `reconnect_player`**

Both loops match on `player->hash`, and unclaimed seats have a real access code from Task 10 — so an access code for a seat nobody has claimed would silently seat you as a nameless player. Guard both. At line 2135:

```c
            for (int i = 0; i < inst->num_players; i++) {
                Player *player = &inst->players[i];
                if (!player->claimed) { continue; }
```

At line 2189:

```c
    for (int i = 0; i < inst->num_players; i++) {
        Player *player = &inst->players[i];
        if (!player->claimed) { continue; }
```

- [ ] **Step 5a: Filter unclaimed seats out of the access-code lookup itself**

Guarding the two loops in `reconnect_player` fixes the symptom and opens a worse path. `SQL_FIND_INSTANCE_BY_PLAYER_HASH` is `select instance from players where hashid=$hashid limit 1;` with no `claimed` condition, and `start_instance` mints an access code for **every** seat — the `generate_unique_hash(player->hash)` call sits outside the `if (conn)` guards. So an unclaimed seat's code is a real, resolvable code.

With only the loop guards, entering one of those codes now: skips the match in the live loop, falls through to `db_find_instance_by_player_hash`, which happily returns the instance row **even when that instance is live in memory**; builds a *second* `Instance` from the same row; skips the match again in the second loop; and lands on `assert(!"This shouldn't happen")` followed by `free_instance(inst)`. That `free_instance` calls `save_instance`, writing the stale just-rehydrated snapshot back over a live game's row. The project sets no `NDEBUG`, so the assert also aborts the daemon for everyone connected.

Fix it at the source instead. Change the macro to:

```c
#define SQL_FIND_INSTANCE_BY_PLAYER_HASH \
    "select instance from players where hashid=$hashid and claimed <> 0 limit 1;"
```

An unclaimed seat's code then resolves to nothing and `reconnect_player` reports "I can't find a game with that access code" the way it does for any bogus code.

Keep both loop guards from Step 5 — they still matter for the live-instance path, which walks in-memory instances rather than the database.

- [ ] **Step 6: Build and rerun both daemon tests** *(owner-run)*

```bash
cmake --build build --target multizorkd
MULTIZORK_ADDR=127.0.0.1:2323 python3 saturn/tests/test_multizork_join.py
MULTIZORK_ADDR=127.0.0.1:2323 python3 saturn/tests/test_multizork_lobby.py
```

Expected: both `OK`, with no `db_log_error` lines in the daemon log during a game with fewer than four players.

- [ ] **Step 7: Commit**

```bash
git add saturn/multizorkd.c
git commit -m "Walk past the seats nobody is sitting in when broadcasting, trimming recaps and matching access codes, while still saving them, since a seat's frozen state is exactly what a later arrival wakes up into and the endgame touchbit should reach whoever claims one next."
```

---

### Task 12: Claiming a seat mid-game

**Files:**
- Modify: `saturn/multizorkd.c` — `seat_available_for` (Task 5), `enter_room_by_name` (Task 5), `show_lobby` (Task 6)
- Modify: `saturn/tests/test_multizork_lobby.py`

**Interfaces:**
- Consumes: everything in Tasks 9 through 11.
- Produces: `static int claim_seat(Connection *conn, Instance *inst)`, returning the index of the seat it claimed or -1 when none was free.

- [ ] **Step 1: Write the failing tests**

Append to `main()` in `saturn/tests/test_multizork_lobby.py`, immediately before the `if fails:` block:

```python
    # A game already under way still has seats, and a stranger can take one.
    host, room = make_room("ivy", listed=True)
    out = send(host, "go")
    fails += check("solo game starts", "THE GAME IS STARTING" in out, out)

    late = connect(); drain(late)
    out = send(late, "bob")
    fails += check("a started room is still listed", room in out, out)
    out = send(late, room)
    fails += check("late arrival reaches the game prompt", ">" in out, out)
    fails += check("late arrival starts at the beginning", "West of House" in out, out)
    fails += check("late arrival gets an access code",
                   re.search(r"access code: '(\w+)'", out) is not None, out)

    out = drain(host)
    fails += check("the table is told someone sat down", "bob has joined" in out, out)

    # Fill it, and it drops off the list and refuses a fifth.
    extras = []
    for name in ("cass", "dev"):
        e = connect(); drain(e)
        send(e, name)
        send(e, room)
        extras.append(e)

    fifth = connect(); drain(fifth)
    out = send(fifth, "nael")
    fails += check("a full room leaves the lobby", room not in out, out)
    out = send(fifth, room)
    fails += check("a full room refuses a fifth player", "no free seats" in out, out)
    fifth.close()

    # A seat whose owner merely walked away is still theirs. Drop one of the
    # four and the room must stay off the list and keep refusing strangers --
    # this is the case a careless seat_available_for() would quietly break.
    extras[0].close()
    time.sleep(1.0)
    stranger = connect(); drain(stranger)
    out = send(stranger, "opportunist")
    fails += check("an absent player's seat does not reopen the room", room not in out, out)
    out = send(stranger, room)
    fails += check("an absent player's seat is not handed to a stranger",
                   "no free seats" in out, out)
    stranger.close()

    # The owner of that seat still gets it back.
    owner = connect(); drain(owner)
    out = send(owner, "cass")
    fails += check("the absent player is offered their game", room in out, out)
    owner.close()

    for e in extras[1:]: e.close()
    late.close(); host.close()
```

- [ ] **Step 2: Run to verify it fails** *(owner-run)*

```bash
MULTIZORK_ADDR=127.0.0.1:2323 python3 saturn/tests/test_multizork_lobby.py
```

Expected: FAIL at "a started room is still listed" — `seat_available_for` still refuses every started game.

- [ ] **Step 3: Open started games**

In `seat_available_for`, delete these two lines. A game in progress is now judged by its seats like any other room:

```c
    } else if (inst->started) {
        return 0;
```

leaving:

```c
    if (inst->crashed) {
        return 0;
    }
```

- [ ] **Step 4: Add the claim**

Insert directly above `enter_room_by_name`:

```c
/*----------------------
 | claim_seat
 | Description: Wakes an unclaimed seat for a new arrival: gives it their name,
 |   rewrites its property table so the other players see them, and clears the
 |   two attributes that were hiding it. The seat's Z-machine state is untouched,
 |   because it has been parked on a READ instruction since the game started.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: GState
 | Params: conn -- the arriving connection, with its username set
 |   inst -- the room they are joining
 | Returns: 1 when a seat was claimed, 0 when none was free
 ----------------------*/
static int claim_seat(Connection *conn, Instance *inst)
{
    int seatnum = -1;
    for (size_t i = 0; i < ARRAYSIZE(inst->players); i++) {
        if (!inst->players[i].claimed && (inst->players[i].connection == NULL)) {
            seatnum = (int) i;
            break;
        }
    }

    if (seatnum < 0) {
        return -1;
    }

    Player *player = &inst->players[seatnum];
    player->claimed = 1;
    player->connection = conn;
    conn->instance = inst;
    snprintf(player->username, sizeof (player->username), "%s", conn->username);

    GState = &inst->zmachine_state;
    write_player_name_property(player, player->username);

    const int previous_player = inst->current_player;
    inst->current_player = seatnum;
    GState->operands[0] = ZORK1_EXTERN_MEM_OBJS_BASE + seatnum;
    GState->operands[1] = 0x07;  // INVISIBLE bit
    opcode_clear_attr();
    GState->operands[0] = ZORK1_EXTERN_MEM_OBJS_BASE + seatnum;
    GState->operands[1] = 0x0E;  // NDESCBIT bit
    opcode_clear_attr();
    inst->current_player = previous_player;
    GState = NULL;

    conn->inputfn = inpfn_ingame;
    return seatnum;
}
```

The name property is rebuilt through Task 10's `write_player_name_property` rather than patched in place. A username can be shorter or longer than the `guest` placeholder it replaces, so sliding the trailing properties by hand would read or write past the end of the 32-byte table depending on which way the length went. Rebuilding sidesteps the arithmetic entirely, and the helper's `memset` clears the placeholder's tail.

- [ ] **Step 5: Call it from the dispatcher**

In `enter_room_by_name`, inside the live-instance branch, replace everything from the seat-taking `for` loop through `return 1;` — that is, the loop that assigns `inst->players[i].connection = conn`, the announcement loop, and the `inpfn_player_waiting` hand-off — with this single block:

```c
        int seatnum = -1;
        if (inst->started) {
            seatnum = claim_seat(conn, inst);
            if (seatnum < 0) {
                write_to_connection(conn, "That game is already under way and has no free seats. Sorry!\n");
                return 0;
            }
            write_to_connection(conn, "\nYou slip into '");
            write_to_connection(conn, inst->hash);
            write_to_connection(conn, "', already in progress. The others are somewhere\nahead of you; you'll have to catch up.\n\n");
            write_to_connection(conn, "Rejoin any time with this access code: '");
            write_to_connection(conn, inst->players[seatnum].hash);
            write_to_connection(conn, "'\n\n");
        } else {
            for (size_t i = 0; i < ARRAYSIZE(inst->players); i++) {
                if (!inst->players[i].claimed && (inst->players[i].connection == NULL)) {
                    inst->players[i].connection = conn;
                    conn->instance = inst;
                    break;
                }
            }

            if (conn->instance == NULL) {
                write_to_connection(conn, "That room appears to be full. Too popular!
");
                return 0;
            }
        }

        for (size_t i = 0; i < ARRAYSIZE(inst->players); i++) {
            Connection *c = inst->players[i].connection;
            if ((c != NULL) && (c != conn)) {
                write_to_connection(c, "\n*** ");
                write_to_connection(c, conn->username);
                write_to_connection(c, " has joined this game! ***\n>");
            }
        }

        if (inst->started) {
            step_instance(inst, seatnum, NULL);
        } else {
            conn->inputfn = inpfn_player_waiting;
            conn->inputfn(conn, "");
        }
        return 1;
```

`step_instance` runs last, after the announcement, so the arriving player's room description is the final thing on their screen rather than being followed by other players' chatter. It is what prints that description at all — the seat has been parked on a READ instruction since `go`, and stepping it with no input walks it forward to the next one.

- [ ] **Step 6: Show free seats on a started room**

In `show_lobby`, the joinable list now includes games under way, so say how many seats are open. `build_lobby_rows` already counted them into `row->free_seats`; use that rather than recounting. Replace the `snprintf(line, ...)` at the top of the joinable loop with:

```c
        snprintf(line, sizeof (line), "  %d) %-22s%s%d seat%s free, ",
                 (int) (i + 1), rows[i].name,
                 rows[i].in_progress ? "(in progress) " : "",
                 rows[i].free_seats, (rows[i].free_seats == 1) ? "" : "s");
        write_to_connection(conn, line);
```

and change the section heading from `"\nGames waiting for players:\n"` to `"\nGames you can join:\n"`, since not all of them are waiting any more.

- [ ] **Step 7: Run the tests** *(owner-run)*

```bash
cmake --build build --target multizorkd
MULTIZORK_ADDR=127.0.0.1:2323 python3 saturn/tests/test_multizork_lobby.py
MULTIZORK_ADDR=127.0.0.1:2323 python3 saturn/tests/test_multizork_join.py
```

Expected: both `OK`.

- [ ] **Step 8: Play it** *(owner-run)*

Start a two-player game, walk both players east into the Forest, then connect a third and join by name. Confirm: the newcomer is alone in West of House; the pair are told bob joined; the newcomer walking east finds them; `look` in West of House does not mention a fourth person; and the daemon log is checked for `touched unclaimed seat` lines. Play long enough for the thief to wander — a hit is information to report, not a failure to fix.

- [ ] **Step 9: Commit**

```bash
git add saturn/multizorkd.c saturn/tests/test_multizork_lobby.py
git commit -m "Let a new arrival take a free seat in a game already under way, waking the seat where it was parked at West of House by rewriting its name into the property table and clearing the two attributes that had been hiding it, without moving a single pointer in the object tree."
```

---

## Done

Phase 1 alone gives a lobby, named rooms, privacy, and resume-by-username, and leaves started games closed. Phase 2 opens them. The seam between the two is `seat_available_for()`, so Phase 2 can be reverted by restoring its `started` test without touching anything else.
