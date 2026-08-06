---
name: merge-order-constraint-fallback-mood
description: room-categorization-tiers must not be merged alone — it ships a genre-wipe bug whose fix lives only on per-game-fallback-mood.
metadata:
  type: project
---

Two stacked branches, and the order they merge in matters.

`room-categorization-tiers` (`9f8b80a..62a54b7`) added the authored per-game
genre table. Commit `1b47a90` on that branch introduced a defect: `main.cxx`
calls `music_set_game` at :578 and `music_reset` at :580, and `music_reset`
called `room_class_reset()`, which cleared `g_genre` and `g_genre_lock`. Every
game load therefore wiped its own authored genre and fell back to runtime
inference. Starcross still reached sci-fi, but by inference over three rooms
rather than from its table row.

`per-game-fallback-mood` (`de60457..3d42946`, stacked on the above) hit the same
root cause with `g_fallback_cat` and fixed both: `music_reset` now re-derives
everything owned by game identity instead of clearing it, since `g_release` and
`g_serial` deliberately survive reset.

**Merging `room-categorization-tiers` alone ships the genre wipe.** Merge
`per-game-fallback-mood` (it contains the parent tip) or merge both together.

The class of bug is worth remembering beyond these two branches: state derived
from game identity, cleared by a reset that runs *after* the deriving call, with
no re-derivation path. A whole-branch review enumerated all 30 statics in
`music.c` and confirmed only those two had the shape — but any new
`music_set_game`-derived value would inherit it. Tests miss it unless they
reproduce the real `set_game` → `reset` order; the original suite called them in
the reverse order and could not have caught either bug.

Also still open on `room-categorization-tiers`: commits `8ca1ddc`, `1b47a90` and
`62a54b7` never received an independent review — dispatch was blocked at the
time. `1b47a90` is where the genre wipe came from.

Related: [[user-runs-all-builds]]
