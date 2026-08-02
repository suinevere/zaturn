# Dynamic Background Art — Design

**Date:** 2026-07-30
**Status:** Implemented 2026-07-30

## Goal

The mood category the engine already derives from on-screen text should drive the
**background picture** as well as the CD-DA track. The player selects this by
picking a new **"Dynamic"** entry at the top of the Display menu's Palette row,
which is also the new default appearance.

## Scope

In:

- A "Dynamic" palette at the top of the Palette row, and as the shipped default.
- A category → picture table, so the wallpaper follows the room.
- A category-change notification out of the music engine, so the picture and the
  track turn over on one event rather than two timers.
- A cross-fade bracketing that change **in game only** — picture and music dip to
  black/silence, swap, and come back.
- Debounce 3s → 1.5s, restarting on every room change.
- Rename the text-side `music_*` category symbols to `text_*`.

Explicitly out (decided 2026-07-30):

- **No On/Off slider.** Dynamic is a palette entry, not a separate toggle.
- **No room-title extraction and no per-room category table.** The keyword
  classifier stays as the category source. (A probe confirmed extraction is
  feasible — the dominant-parent-bucket heuristic yields 2,245 rooms across the
  31 shipped Z3 games, Zork I = 110 — but it is not wanted.)
- **`MUSIC_DYN_LOOPS` stays 3.** The three-passes-then-cycle rule is unchanged.

## Architecture

The music engine already owns a single piece of state that both features need:
`g_active_cat`, the category currently sounding. Rather than give the display its
own classifier and its own timer — two things that would drift apart — the engine
**publishes** that state and the display subscribes.

```
turn text ──> text_classify_room ──┐
                                   ├──> g_active_cat ──┬──> CD-DA track (internal)
room object ─> text_scan_event ────┘   (debounced)     └──> category_fn ──> wallpaper
                                            │
                                     fade_fn(level) ──> NBG0 brightness + CD-DA volume
```

`music.c` stays platform-independent and host-testable: it gains one callback
setter and calls it, and knows nothing about TGAs or VDP2.

## Components

### 1. `music.c` / `music.h` — category notification

New:

```c
void music_set_category_fn(void (*fn)(int cat));
```

Fired at every point `g_active_cat` changes:

| Site | When |
|---|---|
| `music_on_turn`, `g_active_track == 0` branch | first category of a session, plays immediately |
| `music_tick`, pending-commit branch | a debounced switch commits |
| `music_start_menu` | seeds `TC_NEUTRAL` when no room has been seen yet |
| `music_reset` | notifies `TC_NEUTRAL` so the picture returns to the default |

The callback is optional; with none installed the engine behaves exactly as
today, which is what the existing host tests assume.

### 2. Debounce: 1.5s, restarting on room change

- `MUSIC_DEBOUNCE_FRAMES` 180 → **90**.
- `music_on_turn` currently reloads `g_pending_frames` only when the pending
  *category* changes. Walking from one room to another that shares a category
  therefore leaves the old countdown running, so the switch can commit a fraction
  of a second after arrival. The rule wanted is "don't change until they stop in a
  room longer than 1.5 secs", so the countdown reloads on **any** room change
  while a switch is pending.

Note: 90 frames is 1.5s at 60Hz. On a 50Hz PAL machine the same constant is 1.8s.
Accepted — the frame counter is the engine's only clock and the difference is not
worth a timebase.

### 3. Transition fade (in game only)

A category change must not cut. Picture and music dip together, swap at the
bottom, and come back.

The swap issues a new `PlaySingle`, so the audio has to already be down when it
happens and come up after — the fade **brackets** the commit rather than running
alongside it. That means the engine, which owns the commit, has to know the
transition takes time. It gets one more optional callback:

```c
void music_set_fade_fn(void (*fn)(int level));   /* level 0..255 */
void music_set_fade_frames(int n);               /* 0 = instant, the default */
```

The pending-commit branch of `music_tick` grows two phases around the existing
one:

| Phase | Frames | Engine does | Client does |
|---|---|---|---|
| debounce | 90 | counts down | — |
| fade out | `n` | ramps `level` 255 → 0 | dims NBG0, drops CD-DA volume |
| commit | 1 | swaps picture + issues track | — |
| fade in | `n` | ramps `level` 0 → 255 | restores both |

One counter drives both media, so they cannot drift. `music_set_fade_frames(0)`
reproduces today's instant behaviour exactly, which is the default and what the
existing host tests assume.

**This must not be a blocking loop.** `title_bg_fade_in/out` are
`for i … Synchronize()` ramps — correct on the title screen, but running one from
the category callback would stall the interpreter for the whole fade at every
mood change. The phases above are ticked one step per `music_tick`, so the game
keeps running through the transition.

**The picture fades; the text does not.** `title_fade_engage()` deliberately
points NBG0 *and* NBG3 at colour offset channel A so the title screen dims as a
unit. In game that would blink the player's text out mid-sentence. So the
wallpaper fade drives **channel B on NBG0 only**, leaving NBG3 untouched and
leaving channel A entirely to the existing screen-wide page and title fades.

The audio side ramps `music_set_volume()`, which is documented never to restart
the track, scaled against the player's `g_music_level` so a fade never exceeds
the volume they chose.

**The floor is volume 1, not 0.** `music_set_volume(0)` calls `StopPause()`,
halting the drive outright, and unlike `music_set_level` it has no resurrect
path — a 0 would stop the disc and the ramp back up would raise a volume on a
stopped drive, losing the music entirely. Level 1 is quiet enough, and the swap
at the bottom re-issues the track anyway. A player who set Music to 0 keeps
silence: the fade never raises what they turned off.

Proposed length: **20 frames** each way (~⅓s), a `#define` next to the debounce
constant so it is one number to retune.

**Options is unaffected, and needs no suppression flag.** Cycling the Palette row
goes `display_cycle_row` → `display_apply` → `title_bg_show` directly; it never
passes through the engine's category commit, so it keeps swapping instantly as it
does today. The one thing to verify rather than assume: an engine commit firing
while an in-game menu is open. In-game menus hold the drive via `music_pause()`,
and `music_tick` returns early while paused, so no commit — and therefore no
fade — can occur behind a menu. That is a test, not an assertion.

### 4. Category → picture table

Keyed by **filename**, not slot number: slots index the disc's TGA scan order, so
a slot-keyed table would silently repoint if art were added, removed, or
reordered. This is the same reasoning the display save blob already uses.

It lives **inside `display.c`** rather than a new module. `display.c` is on the
NETBIN source list, which `saturn/tests/test_netbin_sources.py` pins to exactly
18 objects; a new file that `display.c` depended on would break the netbin link
and that test. The cost is an `#include "sound/music.h"` in `display.c` for the
`TC_*` names — which `options.cxx`, also on that list, already carries.

| Category | Picture |
|---|---|
| `TC_NEUTRAL` | `TYPEWRTR.TGA` |
| `TC_WILDERNESS` | `FOREST.TGA` |
| `TC_UNDERGROUND` | `BUNKER.TGA` |
| `TC_WATER` | `CLIFF.TGA` |
| `TC_NAUTICAL` | `CLIFF.TGA` |
| `TC_TOWN` | `HOUSE.TGA` |
| `TC_DUNGEON` | `ANCIENT.TGA` |
| `TC_DESERT` | `CLIFF.TGA` |
| `TC_MAGIC` | `CASTLE.TGA` |
| `TC_SCIFI` | `COMPUTER.TGA` |
| `TC_HORROR` | `BUNKER.TGA` |
| `TC_MYSTERY` | `HOUSE.TGA` |
| `TC_DANGER` | *(none — keep current)* |
| `TC_TRIUMPH` | *(none — keep current)* |

`TC_DANGER` and `TC_TRIUMPH` are turn-text events, not places. The music shifts
for them; the wallpaper holds on the room's own picture rather than flicking away
and back.

A `NULL` entry means "keep the current picture", which is also the fallback when a
named file is not registered on this disc — so a disc missing art degrades to a
static wallpaper rather than to a CD read per room.

### 5. Display model — the Dynamic palette

`display.c` gains the dynamic category as state, so palette indexing stays a pure
function of the display model and cycling onto Dynamic shows the right picture
immediately:

```c
#define DISP_PAL_DYNAMIC 0
void display_set_dynamic_category(int cat);   /* engine -> display */
int  display_dynamic_slot(void);              /* last resolved slot */
```

`display_set_dynamic_category` stores the *resolved slot*, not the raw category,
and leaves it alone when the category maps to no picture. It is seeded with
`TC_NEUTRAL`'s picture. So `display_dynamic_slot()` returns
`DISP_IMAGE_NONE` only on a disc with no art — never as a transient "keep
current". That matters because cycling **onto** Dynamic during a `TC_DANGER`
moment would otherwise have no current picture to keep, and would land on no
wallpaper at all.

Palette index layout — **uniform, no conditional arithmetic**:

| Index | Meaning |
|---|---|
| `0` | Dynamic |
| `1 .. DISP_PRESET_N` | colour preset `PRESETS[index - 1]` |
| `DISP_PRESET_N + 1 ..` | image slot `index - DISP_PRESET_N - 1` |

`display_palette_count()` always returns `1 + DISP_PRESET_N + g_image_count`.
Index 0 always exists; when the disc carries no TGAs, `display_cycle_palette`
**skips** it rather than the count changing shape. This keeps every
`display_preset_*` accessor a single unconditional expression.

Consequential edits:

- `display_preset_bg(0)` / `display_preset_text(0)` return black / white, matching
  the image presets — a picture is showing, so the backdrop reads as deliberate
  letterboxing and menu text stays legible.
- `display_preset_image(0)` returns `display_dynamic_slot()`, so `display_is_image()`
  is true while Dynamic is active. This is what keeps
  `loading_screen.cxx`'s wallpaper hide/restore working with no change.
- `display_preset_name(0)` returns `"Dynamic"` (7 chars, well inside the 16-char
  ceiling the Display page's value column imposes).
- `display_defaults()` selects `DISP_PAL_DYNAMIC`, or palette `1` when the disc has
  no images.
- `display_apply()`'s image-load fallback currently rewrites the palette to `12`
  (IBM PC/MDA); that becomes `13` under the new indexing.

### 6. Persistence

Display blob sentinel `3` → **`4`**, with a new `DISP_BLOB_DYNAMIC` marker in
`out[1]` and an empty name field.

Decode:

- Sentinel `4`: indices are stored in the new space, read straight through.
- Sentinels `1`/`2`/`3`: colour-preset indices are in the old space, so **+1** on
  read. Image presets are already resolved by name and need no shift.

`DISP_BLOB_BYTES` is unchanged, so `options_save`'s 62-byte budget is unaffected.

### 7. Saturn wiring

`main.cxx` installs the handler at startup:

```c
music_set_category_fn(on_text_category);
```

```
on_text_category(cat):
    display_set_dynamic_category(cat)           /* no-op for DANGER/TRIUMPH */
    if g_display.palette != DISP_PAL_DYNAMIC: return
    slot = display_dynamic_slot()
    if slot == DISP_IMAGE_NONE: return          /* disc carries no art */
    g_display.image = slot
    title_bg_show(display_image_file(slot))
```

For `TC_DANGER` / `TC_TRIUMPH` the stored slot is unchanged, so this re-requests
the picture already showing and `title_bg_show` short-circuits — the wallpaper
holds without a special case in the handler.

No CD read: all eight shipped pictures (~580 KB) fit `TGA_CACHE_BUDGET` (608 KB)
and are decoded into Low Work RAM during the title's silent window, so
`title_bg_show` serves them from RAM. It also short-circuits when the requested
file is already up, so categories that share a picture cost nothing.

## Rename

Text-side symbols only. `music_category_pool` and `music_category_track` keep
their names — they map a text category onto *CD tracks*, which is genuinely
music-side.

| Old | New |
|---|---|
| `MUSIC_NUM_CATEGORIES` | `TEXT_NUM_CATEGORIES` |
| `MC_*` | `TC_*` |
| `music_classify_room` | `text_classify_room` |
| `music_scan_event` | `text_scan_event` |
| `music_keywords` | `text_keywords` |
| `music_events` | `text_events` |
| `music_game_room_category` | `text_game_room_category` |

Blast radius, confirmed by grep: `saturn/src/sound/music.{c,h}`,
`saturn/src/sound/music_data.c`, `test/music_mix_test.c`, `test/music_test.c`.
The `category` field in `game_catalog.h` is a story-file genre and is **not**
part of this rename.

## Error handling

| Condition | Behaviour |
|---|---|
| Category has no picture (`DANGER`/`TRIUMPH`) | stored slot unchanged; wallpaper holds |
| Named file not registered on this disc | stored slot unchanged; wallpaper holds |
| Disc carries no TGAs at all | Dynamic skipped in cycling; default falls to palette 1 |
| `title_bg_show` fails | existing `display_apply` fallback to a colour preset |
| No category callback installed | engine behaves exactly as today |

## Testing

Host tests (pure C, `gcc -I saturn/src/sound`):

- The category callback fires once per commit, with the committed category, and
  not at all when the category is unchanged.
- The debounce countdown reloads on a room change that keeps the same category.
- A switch commits at 90 frames, not 180.
- With `music_set_fade_frames(0)` the commit is unchanged and `fade_fn` never
  fires — the instant path stays byte-for-byte today's behaviour.
- With `n > 0` the phases run debounce → fade out → commit → fade in, the commit
  lands exactly once and only at the bottom, and `fade_fn` sees a monotonic
  255→0 then 0→255 ramp hitting both endpoints.
- A room change during the fade-out does not restart the debounce or abort a
  commit already in flight.
- No commit or fade occurs while the engine is paused.
- `TC_DANGER` / `TC_TRIUMPH` map to no picture; the other twelve map to a name.
- Existing `music_test.c` / `music_mix_test.c` pass after the rename.

Repo guard test (Python, alongside `test_lwram_splash_budget.py`):

- Every filename in the category → picture table exists in `saturn/cd/data/TGA/`.

On-device checklist:

- Walking between rooms of different moods changes picture and track together,
  through a fade rather than a cut.
- The game text stays at full brightness throughout the fade.
- Cycling the Palette row in Display Options still swaps instantly, with no fade.
- Moving room-to-room faster than 1.5s changes neither until you stop.
- Combat/treasure text shifts the track but not the picture.
- Dynamic is the default on cleared backup RAM, and survives a reboot.
- An existing saved palette still selects the same appearance after the sentinel
  bump.
- The loading screen still hides and restores the wallpaper with no flash.

## Open risks

- **Blob compatibility** is the one place a mistake is invisible until a player
  reboots. The +1 shift on sentinels 1–3 needs a direct test, not just inspection.
- **Mid-frame `title_bg_show` during gameplay** is exercised today only from the
  Options menu. It writes VRAM and CRAM; per-room swaps put that on the game
  screen instead. Worth watching for tearing on device — the fade helps here,
  since the swap now lands while the picture is black.
- **Colour offset channel B** is assumed available and unused. Nothing in the
  tree writes it today (`title_fade_set` only touches A), but if SRL does not
  expose `SetColorOffsetB`, the fallback is channel A engaged on NBG0 alone —
  which works, and only conflicts with the screen-wide page fades, which cannot
  run during gameplay anyway. Confirm at first build.
- **Fade length interacts with the debounce.** 20 frames each way adds ~⅔s on top
  of the 1.5s settle, so a mood change lands a little over two seconds after you
  stop moving. If that reads as sluggish, the fade constant is the one to cut.
