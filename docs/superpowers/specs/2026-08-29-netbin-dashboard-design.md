# Design: the marble dashboard in the netbin

**Date:** 2026-08-29
**Status:** Implemented, hardware verification outstanding
**Amends:** `2026-08-28-input-dashboard-design.md`, which excluded the netbin
from the dashboard on a cost argument. The cost has now been measured and the
argument does not survive it.

## What changed

The netbin links `video/dash_map.c`, `video/dash_tiles.c` and
`video/dash_view.cxx`, and `main_netbin.cxx` calls `dash_init()`. The gamepad
strip is drawn as the marble panel on NBG2 in both builds instead of as printed
ASCII borders in one of them.

## Why the original exclusion was reversed

The dashboard design said:

> Rather than add the dashboard files to that list — which
> `saturn/tests/test_netbin_sources.py` pins at exactly 27 objects, and which
> would put bytes into a size-gated build for a feature it does not use —
> `dash_view.h` defines all three entry points as `#ifdef NETBIN` no-op inlines.

Two of those three clauses were wrong by the time they were written, and the
third was never tested:

- **"a feature it does not use."** The netbin draws the same gamepad strip. It
  has `console_view.cxx` and `command_view.cxx` in its source list, and both
  call `dash_set`. The strip is arguably *more* of the netbin's screen than the
  CD build's, since that build has a wallpaper competing for attention and this
  one has a flat backdrop.
- **"pins at exactly 27 objects."** It pinned 31 by then, and pins 34 now. The
  makefile's own header comment carried the same stale count and has been
  corrected alongside it (34 netbin objects against the CD's 54).
- **"would put bytes into a size-gated build."** True, but nobody had said how
  many. It is **5,328**.

## Measured

Clean rebuilds (`compile-netbin.bat clean` first).

| step | bytes | delta |
|---|---:|---:|
| before | 176,592 | |
| dashboard linked | 181,920 | **+5,328** |

Per-object, in the netbin configuration:

| object | text | data | bss |
|---|---:|---:|---:|
| `dash_map.o` | 952 | 8 | 1,292 |
| `dash_tiles.o` | 1,792 | 0 | 0 |
| `dash_view.o` | 2,380 | 66 | 1,441 |
| **total** | **5,124** | **74** | **2,733** |

The remaining ~130 bytes of image delta are alignment plus the `dash_init()`
call site. The `.bss` is work RAM and does not travel down the wire.

**The link pulled in nothing else.** This was the real risk — `dash_init` uses
SRL's VDP2 VRAM allocator, its CRAM palette bank and `Tilemap::TilemapInfo`, and
a build that had none of those would have paid far more than the objects
themselves. It has all of them already: `text_map.cxx` brings up NBG3 through
the same paths. The link came back with no undefined references and no growth
beyond the three objects.

Beware the arithmetic trap that made an earlier estimate read 5,644: the
`dash_*.o` files left on disk after `compile.bat` are **debug-config** objects
(`-DDEBUG`), not netbin ones. Size the netbin build's own objects.

## VRAM, which is free here in a way it is not in the CD build

Unchanged from the dashboard design: ~17 KB in bank B0 (about 7 KB of character
patterns for 55 tiles, 8 KB of pattern name table), and CRAM palette 1 at
entries 16–31.

`dash_init` names `VramBank::B0` explicitly because SRL's `AutoAllocateMap`
would try A0 first and then fall to B1, where SRL's own NBG3 font lives
untracked. In the CD build A0 is unavailable anyway — the wallpaper's 512×256
8bpp bitmap owns the whole bank. **In the netbin A0 is free**, so that
justification does not apply there.

B0 is still named in both, and the comment now says why: B1 is the wrong answer
in either build, and one allocation site is worth more than a bank nobody is
competing for. Same for `slPriorityNbg0(1)`, which in the netbin orders a layer
carrying nothing — harmless, and cheaper than a second code path for a register
nobody reads.

What *does* differ visibly: `DT_BLANK` is transparent, so in the CD build the
cells outside the panel show the wallpaper and in the netbin they show the flat
backdrop colour from `display_bg_rgb`. That is the intended look in both.

`image_window_box` / `image_window_on` are unaffected. They drive
`slScrWindowModeNbg0`, which touches NBG0 only — the layer the netbin does not
use — so the window neither helps nor interferes there, exactly as the
dashboard design predicted for the opaque-panel case.

## The fallback did not go away

`dash_init()` still returns false when either VRAM allocation fails, `dash_ready()`
still reports 0, and every renderer still prints `CV_BORDER`, `KB_STRIP_BORDER`,
its `|` dividers and the keyboard module's `-----` rule. What was removed is the
`#ifdef NETBIN` block in `dash_view.h` that made those the *only* path in this
build, and the matching `#ifndef NETBIN` around `dash_view.cxx`'s body.

So the failure path is now identical in both builds and is reachable in both,
rather than being a compile-time certainty in one of them.

## Not done

- **Nothing has drawn a tile.** This links, gates and measures. No marble has
  appeared on a netbin screen, and it cannot appear on this machine: reaching
  the strip needs the PlanetWeb loader, a NetLink modem and a live multizork
  daemon.
- **Ordering against the PlanetWeb hand-over.** `dash_init` sits directly after
  `text_map_init()`, mirroring `main.cxx:364`, which puts it after
  `SRL::Core::Initialize` in both builds — so VDP2 has been re-initialised by
  SRL before it runs, and the browser's leftover register state is not what it
  is reading. It does, however, run *before* `netbin_video_init()`, which is
  this build's own re-assertion pass. That pass touches NBG0's window, the back
  colour and the text colour, none of which is NBG2, so it should be harmless.
  "Should be" is the operative phrase: if the panel comes up missing or corrupt
  on hardware, moving `dash_init` below `netbin_video_init()` is the first thing
  to try.
- **Size.** 181,920 is ~44% of `post.makefile`'s 400 KB gate, which is
  documented as softer than it looks. +5.3 KB is roughly +1.5 s of download at
  28.8k.
