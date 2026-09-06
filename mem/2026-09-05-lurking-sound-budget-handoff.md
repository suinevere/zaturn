---
name: lurking-sound-budget-handoff
description: STALE on the route and every number in it, superseded by [[lwram-sound-and-image-trim-handoff]]; the diagnosis and what BOOTSND.MAP will not yield still stand. Lurking Horror's fourteen sound effects fit nowhere -- 0 of 14 now, 2 of 14 before this session -- and the fix is not to free memory but to allocate from a different pool; the owner picked the sound-RAM route, which is blocked on an SCSP slot question a probe was built to answer and nobody has run yet.
metadata:
  type: project
---

> **Stale.** The owner took the Low Work RAM route instead, both budget floors
> are clear, and the probe this note tells you to run was printing no slot
> number at all when it was written. Read
> [[lwram-sound-and-image-trim-handoff]] first; come back here for the
> measurement of Lurking's claimants and for why BOOTSND.MAP cannot be decoded.

Branch `synth-music`, **one commit ahead of `origin/main`** (`ed301e3`, the
probe). The two commits below it are pushed. Working tree carries one unstaged
line the owner wrote before this session (`saturn/run_with_mednafen.bat`,
`MEDNAFEN_ALLOWMULTI=1`) and an untracked `out.wav`; neither is mine.

Continues [[synth-song-catalogue-handoff]], which is **stale on its headline
numbers only** -- they were measured before the rebase onto the controller work
that had landed on `origin/main`. Everything that note says about the tune
catalogue, the mapping, the disc file and the Sound page still stands. The
current numbers are below.

Nothing here has run on hardware or in an emulator. The probe is the whole point
and it has not been listened to.

## Where the numbers are now

The rebase put my two commits on top of the controller module and Controls page
work. Both together break two headroom floors that neither breaks alone:

| | measured | floor | under by |
|---|---|---|---|
| CD heap behind `LURKING.Z3` (129,704) | 6,200 | 16,384 | 10,184 |
| netbin spare under 307,200 | 30,048 | 32,768 | 2,720 |

Nothing is over a hardware limit and nothing fails to load. `LURKING.Z3` fits;
the netbin fits. `test_hwram_budget.py::test_the_largest_story_leaves_room_to_run_in`
and `test_netbin_budget.py::test_the_image_leaves_room_to_grow` are the two
failures, plus the pre-existing art-archive one in `test_lwram_budget.py`.

**Backing my work out would not clear the floor.** Measured: this session cost
the heap 4,480 bytes (152,416 -> 147,936); the rebase cost 12,032 more
(147,936 -> 135,904). Computed from those two, the controller work alone sits
near 140,384, already ~5,700 under. That last figure is arithmetic -- I did not
build `origin/main` on its own to confirm it.

## The real finding: Lurking Horror's sound effects

`LURKING.BLB` is the only sound blorb on the disc, and Lurking Horror is also
the largest story -- so the game that needs runtime heap is the one with least.
Fourteen mono 8-bit samples, measured off the AIFF chunks:

* **7,736 to 59,998 bytes**, 0.44 to 5.17 seconds, 9,676 to 31,250 Hz.
* `load_slice` (`saturn/src/sound/sound.cxx`) mallocs each one **whole** from the
  HWRAM heap, uncapped, and caches up to `NCACHE` 8 of them.

How many ever fitted:

| heap | free behind LURKING | samples that fit |
|---|---|---|
| before this session, 152,416 | 22,712 | 2 of 14 |
| after the catalogue, 147,936 | 18,232 | 1 of 14 |
| after the rebase, 135,904 | 6,200 | **0 of 14** |

So this is **a pre-existing hole widened to complete**, not a regression: twelve
of fourteen were already unreachable, because a 16 KB floor was never going to
hold a 60 KB sample. Eight cached at once was never possible at any heap size.

## The correction that matters

The shortage is **HWRAM**. Everything releasable is **LWRAM**. Freeing memory
does not fix this; allocating from a different pool does.

What Lurking specifically claims, against the worst case the budget test holds:

| claimant | test's worst case | Lurking's actual | free for Lurking |
|---|---|---|---|
| item pane | 50,056 | **0** | **50,056** |
| typeahead trie | 325,632 | ~241,336 | **~84,295** |
| song bank slot | 8,704 | 8,704 | **8,704** on CD music |
| save scratch | 65,536 | 65,536 | -- |
| map parchment | 82,194 | 82,194 | 82,194, but see below |
| area archive + decode + 4K | 499,160 | 8 areas, unsized | -- |

* **Item pane, already free.** `scene/game_items.inc` has `ITEM_GAME_N 1` --
  Zork I is the only story with an item-picture table, and `item_art_open`
  refuses before touching the disc for every other one. `test_lwram_budget.py`
  counts it unconditionally, which is why its worst case reads tighter than any
  real Lurking session.
* **Trie headroom.** The 318 KB reserve is Wishbringer's vocabulary. Measured off
  the Z-machine dictionary headers: **LURKING 773 words, WISHBRNG 1,043** (0.74x).
  First-order only -- a trie shares prefixes -- but the direction is not in doubt.
* **Song bank slot.** Mine, from the catalogue work. Earns its place only when the
  synth is the music source; on a CD-DA disc with CD music selected it is dead
  weight and could be freed and re-claimed on a source switch.
* **Map parchment is a trade, not free.** Held all session so opening the map
  never seeks, because a seek stops CD-DA. Releasing it costs one seek per map
  open.

~143,000 bytes for Lurking with no behaviour change, against 59,998 for its
largest sample.

**Lurking's area archives could not be sized**: its 21 pictures span 8 areas and
none of those has a `.CGL` staged, which is the same pre-existing failure
`test_every_frame_lies_inside_its_archive` reports. When art-v2 finishes
generating them the archive claim returns and the ~143,000 shrinks.

## Two routes, and which the owner picked

The owner was given three and chose **stream the load into sound RAM**: read each
sample from CD in ~2 KB chunks straight into the SCSP's own memory and play it
from slots of our own, the way the synth already plays its waveforms. Nothing
large in main RAM, no disc access during playback, CD-DA untouched.

It is blocked, and the block is real:

* **Sound RAM is fine.** `BOOTSND.MAP`'s allocations top out near `0x48600` under
  the most generous reading; the synth's waveform area is at `0x70000`. ~158 KB
  unclaimed between them.
* **SCSP slots are the blocker.** The synth holds 28-31; effects need four more.
  The driver's slot allocation lives inside `SDDRVS.TSK` and `BOOTSND.MAP`, and
  **that file's format is not decodable from this repository** -- two candidate
  readings both produce overlapping regions, so both are wrong. `PCM.channel` in
  SGL's `sl_def.h` is a driver channel index, not a slot.
* **The synth's own claim on 28-31 rests on the same unchecked comment.** Nothing
  has ever verified it either.

The other route, which the owner did not pick but which now looks cheap given the
Lurking measurements above: **move `load_slice` from `HighWorkRam::Malloc` to
`LowWorkRam::Malloc`** with a headroom check modelled on `item_art_open`'s, and
size the cache from actual free space instead of a fixed 8. Contained to
`sound.cxx`, no new SCSP slots, no probe, works today, and Lurking has ~143 KB
there. It does not give every game every effect the way sound RAM would.

## What to run first

    tools\assets\scspfx.bat

Builds `tools/scspfx` -- a disc that runs **with the SGL driver on**, writes a
tone into sound RAM at `0x60000`, and keys one slot with it every two seconds
while naming the slot on screen. All thirty-two, about a minute. Listen and read
together.

* **24-27 sound** -> the effects have their slots; write the engine.
* **almost nothing sounds** -> the driver owns more of the chip than the synth's
  own placement assumed, which is a bigger problem than the sound effects and
  better learned from a one-minute probe than from a silent game.

This is the project's own method. Eleven SCSP faults were found by recording the
chip and none by reading the code; building a new SCSP consumer on a slot guess
is exactly the pattern that cost eleven rounds.

## Open, for the owner

1. **Run the probe.** Everything below waits on it.
2. **The two budget floors.** Not clearable by reverting my work. Either the disc
   build's image gets trimmed or the floors get re-argued. `test_hwram_budget.py`
   names the link map as where to look.
3. **Which sound route**, once the probe answers. The LWRAM route is available
   whatever it says.
4. **Nothing in the catalogue work has been heard on the chip or seen on a
   screen** -- the twelve tunes, the disc catalogue's reads, the Sound page's
   Music From and Test Track rows. See [[synth-song-catalogue-handoff]]'s own
   open list, which still stands.

## Suggested skills

* **`superpowers:verification-before-completion`** -- the governing lesson here.
  Two of this session's three hard findings (0 of 14 samples fitting; the slot
  map being undecodable) only appeared because a claim was checked instead of
  asserted. Do not report the effect engine working until a recording says so.
* **`superpowers:brainstorming`** -- before committing to a route after the probe.
  The three options were genuinely different in cost and one was wrong for a
  reason nobody would have guessed (streaming from CD stops CD-DA, which is why
  the cache exists).
* **`superpowers:systematic-debugging`** -- if the probe comes back silent on most
  slots. That would mean the synth's existing placement is wrong too, and the
  symptom would be indistinguishable from "the probe is broken".
* **`claude-mem:mem-search`** -- earlier sessions carry the eleven SCSP faults and
  the KYONB/slot-scan finding that the register writes in `scsp.c` rest on.
