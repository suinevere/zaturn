# Design: netbin typeahead — embedded Zork I vocabulary

**Date:** 2026-08-25
**Status:** Approved, pending implementation plan
**Extends:** `docs/superpowers/specs/2026-07-25-netbin-minimal-design.md`

## Goal

Restore autocomplete suggestions in `zaturn.netbin` by embedding `ZORK1.Z3`
whole and linking a Zork-I-only solution overlay, so the online terminal's line
editor runs against a real trie instead of an empty root.

Everything else in the 07-25 design stands: no Z-machine, no sound, no CD
access, no save/restore. This spec adds vocabulary data and nothing else.

## Why this is the only feature left to add

The netbin *is* `online_mode()`, and the CD build runs the same function. The
whole difference between them is three `#ifdef NETBIN` guards:

| Guard | Effect |
| --- | --- |
| `net/online.cxx:208-215` | `ensure_online_typeahead` returns after `create_trie_node` — **the trie is permanently empty** |
| `net/online.cxx:321` | `music_refresh()` skipped — no sound in this build, correct as-is |
| `net/online.cxx:390` | reboot prompt reads "dial page" not "title screen" — correct as-is |

Dialer, terminal, keyboard, scrollback, history, options, controls pages,
backup-RAM settings and the soft-reset chord are already present and behave
identically. Every other CD-build module is either CD-bound (`video/title.cxx`
alone has 50 CD call sites) or story-bound, so nothing else in the tree can be
re-linked into this target. Typeahead is the last recoverable feature.

## Size accounting

Measured with `sh2eb-elf-size` over the current objects; loadable is
`text+data`, since `.bss`, `HEAP`, `WORK_AREA` and `COMMAND_BUF` are all
`NOLOAD` in `sgl-netbin.linker`.

### Baseline

| Component | Bytes |
| --- | ---: |
| 21 project objects (the `NETBIN=1` source list) | 105,002 |
| SGL `SLPROG` | 24,496 |
| `PRELOADER` + `SLSTART` + `.tors` | 432 |
| raw sum | 129,930 |
| less COMDAT/inline dedup (~5%, see below) | |
| **image** | **~124 KB** |

The dedup factor is derived, not assumed: the CD build's project objects sum to
364,053 B and its `SLPROG` is the same 24,496, but `cd/data/0.bin` is exactly
368,723 B — the linker merges 20,258 B (5.2%) of duplicated inline and template
code across translation units. Per-object figures below are therefore upper
bounds.

### Added

| Item | Bytes |
| --- | ---: |
| `ZORK1.Z3` as a `.rodata` byte array | 84,876 |
| `input/typeahead_extract.o` | 5,073 |
| `input/typeahead_solution_zork1.o` (measured, cross-compiled) | 3,793 |
| **total** | **93,742** |

**Netbin after this change: ~215 KB.** Against the `NETBIN_MAX_BYTES` gate of
409,600 (`post.makefile:10`) that leaves ~194 KB of headroom.

This corrects the estimate recorded in the 07-25 spec and repeated in the
comment at `net/online.cxx:212`, which put the cost at "~69 KB … nearly
doubling the image". Measured, it is 91.5 KB and a 74% increase — larger in
absolute terms than that note claimed, but the note's arithmetic assumed
DEFLATE packing and did not account for the overlay at all. Both comments
should be updated to the numbers above.

### Rejected alternative: a story prefix

Zork I's dictionary ends at `0x4E37`, which is exactly `himem_addr`.
Everything `typeahead_extract` reads — header, abbreviations table, object
table, globals, grammar, dictionary — lives below high memory, so a
20,023-byte prefix (24% of the file) would do, cutting the added cost to
28,889 B and the image to ~152 KB.

Not chosen. That saving depends on every table the extractor touches sitting
below high memory, which is a property of this particular Zork I build rather
than a guarantee, and it creates a failure mode where a future extractor change
reads past the cut and returns quietly wrong suggestions instead of failing.
The whole file removes the assumption. Revisit only if the loader's real
ceiling turns out to be under ~256 KB.

## Runtime cost

Measured by running the real `typeahead_extract` and `apply_solution_overlay`
against `ZORK1.Z3` on the host under an instrumented allocator, then recosting
each allocation at SH-2 struct sizes (`TrieNode` 20 B, `DictionaryWord` 20 B,
`NextWordLink` 16 B — the host's 64-bit pointers otherwise inflate the total to
134 KB):

| | Count | SH-2 bytes |
| --- | ---: | ---: |
| trie nodes | 1,943 | 38,860 |
| dictionary words | 686 | 13,720 |
| transition links | 1,407 | 22,512 |
| word text | 686 | 4,115 |
| **total** | **4,722 allocations** | **79,207 B (77.4 KB)** |

The solution overlay matches: release 88 / serial `840726`.

**No transient story buffer is needed.** `build_typeahead_from_story` and
`apply_solution_overlay` both take `const unsigned char*`, so the netbin passes
a pointer straight into the `.rodata` blob. The CD path's malloc-read-free
cycle (`online.cxx:216-240`) has no analogue here, and peak RAM is the trie
alone.

That 77.4 KB is well inside the measured budget — the netbin's HWRAM window is
720,896 B (load base `0x06010000` to `work_area_start 0x060C0000`), currently
holding a ~124 KB image and 60,752 B of project `.bss`, leaving ~524 KB of
heap. LWRAM is entirely unclaimed in this build.

## Architecture

### New files

**`tools/gen_blob.py`** — converts a binary file into a C byte-array source,
the same pattern the generated `typeahead_solution.c` and `music_data.c`
already use. Must be written fresh: the `netbin-build` branch that carried the
original (`0eeee04`) no longer exists in this repository — the branch is absent
from `git branch -a`, from the reflog and from `git fsck --lost-found`, and
commits `0eeee04`, `df85e24`, `371f1cc`, `a00537d` and `0690a7f` are all
unreachable. Nothing from it can be cherry-picked; the 07-25 spec's
"cherry-picked from `0690a7f`" notes are stale.

**`saturn/src/input/netbin_story.{c,h}`** — `netbin_story.c` is generated and
never hand-edited. Exposes:

```c
const unsigned char *netbin_story_data(void);
unsigned int         netbin_story_size(void);
```

Both return `NULL` / `0` when `NETBIN` is not defined, so the file compiles to
an empty object in the CD build's `find`-globbed source list. It lives under
`input/` because its only consumer is the typeahead layer; this build has no
engine concern for a story to belong to.

**`saturn/src/input/typeahead_solution_zork1.c`** — generated by the existing
`tools/typeahead/gen_solution.py` with a single `--game` pair. It defines the
same `apply_solution_overlay` symbol as `typeahead_solution.c`; the two are
never linked together, because the netbin source list is explicit and the CD
build's glob is filtered. This is the same mechanism that already keeps
`main_netbin.cxx` and `main.cxx` apart.

### Modified files

**`saturn/src/net/online.cxx`** — replace the `#ifdef NETBIN` early return in
`ensure_online_typeahead` (`:208-215`) with the embedded-blob path: call
`build_typeahead_from_story`, `apply_solution_overlay` and
`typeahead_add_abbreviations` against `netbin_story_data()`, with no allocation
and no free. The `DIFF_HARD` early return above it stays, so hard difficulty
still means no suggestions. The `scan_z3_folder` / `SRL::Cd::File` branch stays
`#ifndef NETBIN` — this build still never touches the drive.

**`saturn/src/main_netbin.cxx`** — `typeahead_malloc` / `typeahead_free`
(`:78-85`) currently route to `SRL::Memory::HighWorkRam`, and the header block
above them documents the reason as "a full story trie is 89-318 KB, and this
build never has one". That premise no longer holds. Both must move to
`SRL::Memory::LowWorkRam`, matching what `engine/saturn_glue.cxx:90-94` does
for the CD build, and the comment must be rewritten to say why. Leaving them on
HWRAM would put 77 KB plus per-block overhead into the same heap the image and
`.bss` share.

**`saturn/Makefile`** — three sources added to the `NETBIN=1` block:
`src/input/typeahead_extract.c`, `src/input/typeahead_solution_zork1.c`,
`src/input/netbin_story.c`. The last two are netbin-only and must also be added
to `NETBIN_ONLY_SOURCES` so the CD build's `find` glob excludes them; otherwise
a plain `make all` links two `apply_solution_overlay` definitions.
`typeahead_extract.c` is *not* netbin-only — the CD build already compiles it.

**`saturn/tests/test_netbin_sources.py`** — `EXPECTED` goes from 21 to 24
entries and `NETBIN_ONLY` from 2 to 4. This gate is the reason the source list
is explicit; it must move with the list.

**`tools/typeahead/gen_all.ps1`** — add a second `gen_solution.py` invocation
emitting the Zork-I-only file, so the two overlays cannot drift. Its header
comment explains why there is one combined file; that reasoning still holds for
the CD build and needs a sentence about why the netbin gets its own.

## Build configuration

Unchanged from the 07-25 design: `NETBIN=1` selects the object set,
`LDFILE=./sgl-netbin.linker` on the make command line,
`SRL_USE_SGL_SOUND_DRIVER = 0`, `objcopy -O binary` in `post.makefile` with the
size gate.

`post.makefile`'s plain `objcopy` (no `-R` flags, unlike SRL's own rule at
`shared.mk:265`) is correct for this target and must stay that way:
`sgl-netbin.linker` marks `WORK_AREA`, `COMMAND_BUF`, `HEAP` and `.bss`
`NOLOAD`, so they contribute nothing to the flat image. Verified by flattening
the current CD ELF both ways — 1,013,760 B plain versus 368,723 B with SRL's
`-R` set.

Note for anyone reading the 07-25 spec: its claim that `sgl-netbin.linker`
"differs in exactly one literal" from `sgl.linker` is wrong. The scripts differ
substantially — `OUTPUT_FORMAT(coff-sh)` instead of `elf32-sh`, four sections
marked `NOLOAD`, `_end` placed at the end of `.bss` instead of at
`SYSTEM_END 0x060FFC00`, no `/DISCARD/` block, and `.eh_frame` kept rather than
discarded.

## Testing

- **Host**: extend the typeahead host tests with a case that builds a trie from
  the embedded blob and asserts the overlay matched (release 88, serial
  `840726`) and that a known prefix predicts a known completion. The
  measurement harness used for this spec is the template.
- **Host**: `tools/gen_blob.py` round-trip — bytes in, identical bytes out of
  the generated array.
- **Host**: `tests/test_netbin_sources.py` updated and passing.
- **Hardware**: dial, connect, type a prefix at the multizork prompt, confirm
  the suggestion row appears and cycles. This is the only check that exercises
  the LWRAM allocation.
- **Regression**: the CD build must be unchanged apart from the new
  empty-in-CD `netbin_story.o`. `cd/data/0.bin` is currently 368,723 B.

## Risks

- **The served story may not be the embedded one.** `multizorkd.c` takes
  `story_filename` as a runtime argument, so the operator chooses what the
  server runs — presumably a multiplayer-modified Zork I. The overlay is keyed
  on release + serial and will silently no-op on a mismatch, leaving the
  grammar layer working but the walkthrough boosts inert. Confirm the server's
  header bytes at `0x02` and `0x12` before spending the 3,793 B. If they differ,
  the embedded story should be the server's build, not the disc's.
- **Allocator overhead is unmeasured.** 77.4 KB of payload arrives as 4,722
  separate allocations. SRL's per-block header size was not established here; at
  8 B/block that is another 37 KB, at 16 B another 74 KB. LWRAM has room either
  way, but the number should be measured on hardware rather than assumed.
- **LWRAM availability in this build is unproven.** `Memory::Initialize`
  (`srl_memory.hpp:837-846`) initializes all three zones including LWRAM, and
  the netbin calls the same `SRL::Core::Initialize` the CD build does — but the
  comment at `main_netbin.cxx:68` describes LWRAM as "a zone the netbin's boot
  path never sets up", and the netbin's linker script places `_end` differently
  from the stock one. Verify an LWRAM allocation succeeds before relying on it;
  this is the first thing in this build to ask for the zone.
- **The loader's real download ceiling is still not established.** The gate is
  set at the documented 400 KB while `post.makefile:10` records that the true
  limit is lower and unknown. At ~215 KB this design has ~194 KB of margin,
  which is likely but not provably enough. If the real number lands under
  ~256 KB, fall back to DEFLATE: `ZORK1.Z3` compresses to 60,889 B at level 9,
  saving 23,987 B gross, less the cost of an inflater — which, like
  `gen_blob.py`, would have to be written from scratch rather than recovered.

## Non-goals

- Any story prefix, subsetting or dictionary-only extraction.
- Compression, unless the ceiling risk above forces it.
- Overlays for any game other than Zork I.
- The direction rose and keyboard strip. Specced separately in
  `2026-08-25-netbin-direction-rose-design.md`, which draws all twelve
  directions statically and needs no story; the two designs are independent and
  may land in either order. Note that the embedded blob here must **not** be
  used to drive `room_model` — it is a never-executed image, so global 0 holds
  the starting room forever and the rose would show one room's exits for the
  whole session.
- Local single-player play, save/restore, sound, or CD access of any kind.
- Changing the CD build's behavior.
