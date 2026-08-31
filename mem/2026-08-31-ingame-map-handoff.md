---
name: ingame-map-handoff
description: An in-game map built on branch input-dashboard over twelve reviewed tasks, laid out by walking the story's own exits at the original Saturn release's 32-pixel step; the reconnaissance it rests on was corrected first, and nothing here has ever been compiled for the target or seen on a screen.
metadata:
  type: project
---

Branch `input-dashboard`, 26 commits ahead of `origin/main` at `37ead4c`, **nothing pushed**.

The commits carry the what. This file carries what they cannot: the reconnaissance that had to be
corrected before any of it could be designed, the four defects that only the whole-branch review
could see, and the exact list of things no gate in this session could prove.

## The recon was wrong on the point everything rested on

`docs/ZORK1_MAP_RECON.md` had concluded the original's map used a per-room "canvas cursor" at
`0x060B4830`, splitting into `hi`/`lo` fields. That was the right instructions read against the
wrong variable. Corrected in `cf75dc4` after disassembling the actual code out of work RAM with
`sh2eb-elf-objdump`:

- `0x06072D38` is a **visited bitmap**. Three independent sites touch it and all three only test it
  against zero. Its low 15 bits are an incidental snapshot of a text-engine global referenced from
  ~110 constant-pool sites across `0x06040000`-`0x06042700`.
- The `hi`/`lo` split with the `0x1800`/`0x3000` scaling is real code at `0x06019BB4`, but it
  decodes the **label records' glyph entries** into an *address*, not a screen position.
- Map geometry is built on the **stack** (`r12 = r15`, stride 20) when the map opens and discarded
  when it closes. Two full HWRAM scans — map closed and map open — found no persistent coordinates.
- `0x0601A000` steps ±32 units per move by direction, offset from a constant −16, which is the same
  immediate the renderer hardcodes for the fixed figure's x.

So the original stores no room coordinates: it re-walks the graph from the player every time the
map opens, at 32 pixels per exit. Two ten-state walks proved the behavioural half — revisits within
a session restore a room's value exactly, but Behind House read `0x00B5` reached via South of House
and `0x00F5` via North of House, differing by exactly 2×32.

**The above-ground walk is gone.** Both walks saved into the same ten emulator slots hours apart and
the second overwrote the first. The underground ten are tracked at `saves/zork1-map-underground/`
with a README; the above-ground numbers survive only inside the recon document. If a future capture
needs two walks, copy the first set out of `mcs/` before starting the second.

## What was built, and the one place it diverges from the original

`map_model.c` keeps the original's rule and its fixed-centre figure but assigns each room a position
on **first entry and never moves it** — because recomputing on open is exactly what made the
original disagree with itself.

Be precise about what that buys, because the spec originally overclaimed it and `9fbefb6` retracted
the claim. A graph walk **cannot** put Behind House on the same cell regardless of approach; through
South of House it is south-east of the house, through North of House north-east. That is the same
non-Euclidean geometry the original tripped on. What this design guarantees, and the original did
not, is that a placed room never moves — including when you arrive again by another route. That is
the property `test_map_model.c`'s Behind House fixture pins.

## Four defects the twelve task reviews could not see

Every task passed its own review. The whole-branch review then found four Criticals, all traceable
to the plan rather than to any implementer:

- **The trail was invisible.** Marks were painted at palette index 9 over a ground already using
  5–8 — one step above its brightest. After the half-tint that is 1/31 per channel. Only the
  player's own room would have read. Now marks use 1, 12, 13 and 15, a worst-case separation of
  5–6 of 31. The old assertion could not have caught it: it compared tile **bytes**, and index 9
  differs byte-wise from index 7. The replacement measures **palette distance**, and was
  demonstrated to fail on the old indices.
- **The map never cleared the text layer**, so the Options menu's nine labels stayed lit on top of
  it — and unframed, because `dash_map_begin` blanks the box border.
- **Changing difficulty erased the map.** `map_model_reset()` landed inside `ensure_typeahead()`,
  whose guard includes `g_ta_diff == g_difficulty`. Now keyed on the story pointer alone.
- **A restored map had no links.** Serialisation carried positions but not exits, and
  `map_model_deserialize` calls `map_model_reset()`, which zeroes them. Now re-derived from the
  story after load by `map_model_rebind_exits`, which costs nothing in backup RAM.

Two earlier defects were caught mid-branch and are worth knowing about because both concern the same
NBG2 layer: adding `DASH_VARIANT_MAP` made `geom_of` index `g_geom` (sized `[DASH_BOX]`) out of
bounds, reachable through the layer's own expiry; and the first screen redrew every frame, which
would have cost ~0.2s per frame at full exploration and starved the looping-PCM hand-off.
`dash_map_hold` — mirroring the existing `dash_box_hold` — is what let `draw_once` genuinely run
once.

## What no gate in this session could prove

The author runs all builds. Every unit here was gated only by `gcc` host tests (for the SRL-free
halves) and `sh syntax-check.sh`, which is `-fsyntax-only`.

**Nothing in this branch has ever been compiled for the SH-2, linked, or run.** Specifically
unproven:

- The real target link. `map_model.c` now calls into `room_model.c`; the host test links it
  standalone only because `test_map_model.c` supplies its own stubs.
- One-frame timing, and whether `draw_once`'s single pass fits its budget on real hardware.
- **How the palette actually reads on a CRT.** The separation is arithmetically sound; whether tan
  marks on a tan ground look right is a judgement only the screen can make.
- Whether the map's geometry is legible at all — 10×7 rooms at four text cells each, with labels
  that will collide when rooms are adjacent.

## Known and deliberate

- **Diagonal links get a midpoint mark rather than their own glyph.** `dash_tiles.c` is on the
  netbin's size-gated source list, and no one in this session could measure what a new tile costs
  there. Documented in `draw_once`'s header.
- **The netbin has none of this.** The model would run there, but the spec's request to ship it was
  declined pending a measured clean rebuild — the dashboard design was reversed once already for
  guessing at exactly this. `test_netbin_sources.py` is unchanged.
- **`gather()` caps at 70 visible rooms** and silently drops beyond that, reachable only through
  collision-spiral stacking.
- **The first move after a restore infers no direction** and places its destination due south
  whatever way you went; a placed room never moves, so that one error is permanent. Documented in
  `map_model_deserialize`'s header rather than fixed.

Related: [[input-dashboard-handoff]] for the NBG2 layer this borrows,
[[netbin-menus-and-tile-chrome-handoff]] for `dash_box_hold`, whose shape `dash_map_hold` copies.
