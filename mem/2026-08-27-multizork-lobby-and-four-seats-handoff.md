---
name: multizork-lobby-and-four-seats-handoff
description: Branch multizork-lobby adds a named-room lobby to multizorkd and lets a latecomer claim a preallocated Zork seat mid-game; twelve tasks complete and reviewed, never built or run.
metadata:
  type: project
---

Branch `multizork-lobby` at `6dedb15`, forked from `origin/main` at `1be2b44`. Twelve tasks,
each implemented and reviewed by a separate agent, then one whole-branch review and one fix wave.
Spec: `docs/superpowers/specs/2026-08-27-multizork-lobby-and-midgame-join-design.md`.
Plan: `docs/superpowers/plans/2026-08-27-multizork-lobby-and-four-seats.md`.
Execution ledger with all twenty rulings: `.superpowers/sdd/2026-08-27-multizork-lobby-and-four-seats/progress.md`.

**Nothing on this branch has ever been compiled for real or executed.** multizorkd needs POSIX
and sqlite3; the dev box is Windows. A stub-header compile gate stood in for a build — it proves
the file parses, type-checks and leaves nothing dangling, and says nothing about linking or
runtime. The owner's build is the first genuine test.

## What it does

Phase 1 (Tasks 1-8) replaces the six-character game code with an `adjective-noun` room name and
the three-item menu with a lobby. The first prompt is now `username:`, which removes the guess
the old prompt had to make between a game code and an access code, and makes the username the key
for finding a returning player's games. Rooms can be private: hidden from the lobby, still
enterable by anyone told the name. `enter_room_by_name` is the single funnel every route into a
game passes through.

Phase 2 (Tasks 9-12) makes every game build all four Zork 1 player seats at `go` rather than only
the occupied ones, so a latecomer can claim an empty one mid-game. The object-id space always had
room for exactly four (`ZORK1_EXTERN_MEM_OBJS_BASE` is 251, v3 caps at 255); only `num_players`
was gating it. Unclaimed seats are hidden with INVISIBLE and NDESCBIT and parked on a continuation
copied from a seated player, because `step_instance` refuses a seat with no connection and the
intro's pristine-memory reset is legal only at `go`.

`seat_available_for()` is the seam: restoring its `started` test reverts Phase 2 alone.

## Open risks the owner must settle by playing

1. **Continuation equivalence.** An unclaimed seat's parked stack and ten `gvar_*` fields are
   copied from the last claimed seat. If Zork's startup cached the PLAYER global into a stack
   local before READ, a latecomer acts as that seat for one turn. Symptom: a newcomer's first
   command behaves as another player. Not resolvable by reading C.
2. **Ghosts in the object tree.** Dormant seats sit on West of House's child list from turn 0.
   Accepted by design, but instrumented: `get_seat` logs once per seat per instance when game
   logic touches an unclaimed one. Watch for `touched unclaimed seat` in the log. Lines at turn 1
   rather than after the thief starts wandering mean the hiding attributes did not take.
3. **`get_virtualized_mem_ptr` behaviour change.** It had a pre-existing out-of-bounds read for
   seats 1-3 (added the whole `base_offset` to a 32-byte table instead of the remainder). Fixed,
   and now routed through `get_seat`, which calls `GState->die` on an out-of-range index where the
   old code read past the buffer silently. Strictly safer, but a game that previously appeared to
   work could now abort loudly.

## Deferred, non-blocking

`start_instance` and several touched functions lack header blocks; `// INVISIBLE bit` style
comments inside function bodies at four sites (two predating this work) contradict the no-comments
rule and want a plan-wide decision; `write_player_name_property` recomputes an address
`start_instance` also computes inline. Full list in the ledger.

Related: [[netbin-rose-and-typeahead]] shares no code with this work.
