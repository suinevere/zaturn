---
name: one-save-record-per-slot-handoff
description: A save slot is now one backup record holding the story blob and the map after it, instead of two files named X and XM; no new container was needed because the story blob is self-describing, old two-file saves still restore, and the boot-time space check now sizes one record instead of adding two block costs.
metadata:
  type: project
---

## What changed

A slot used to write two backup records: `<STORY><slot>` for the Z-machine save
and the same name with an `M` appended for the map. That cost a second directory
entry and a second header block on a device that charges both, and gave a
restore two ways to half-succeed. It is now one record: `[story blob][map blob]`.

**No new container, no memmove, no magic of its own.** The story blob already
describes its own length -- `opcode_save` writes a 21-byte header whose fields
include the delta length and the stack depth -- so the map simply follows it and
`save_blob_len` finds the seam. The interpreter still reads from byte 0 and
stops at its own stack, so it never sees the tail and `opcode_restore` did not
change.

## The three things that made it safe

- **`save_blob_len` is its own pure-C unit** (`saturn/src/engine/save_blob.c`),
  not a static in `saturn_glue.cxx`, because it is the piece that can be got
  wrong quietly: too long hands `map_model_deserialize` the middle of a
  Z-machine stack, too short loses the map. `saturn_glue.cxx` cannot be built on
  the host; this can, and `test/save_blob_test.c` builds blobs to the format by
  hand so the test states the layout independently of the code that reads it.
  Includes the wrap case -- `rle + spoff*2 == 2^32` would otherwise measure as a
  bare header with a map right behind it.
- **The load buffer is cleared before the read.** `saturn_bup_read` reports
  success without a length, so what sits past the story blob is whatever the
  scratch allocation happened to hold, and a stray `MAP_BLOB_MAGIC` in it would
  be decoded as a map. `maxlen` was previously ignored and documented as
  unused; it is now what bounds both the clear and the seam search.
- **The save appends in place.** `saturn_save_blob` takes `(data, len, cap)` and
  `mojozork.c` allocates `saturn_save_tail()` extra bytes (MAP_BLOB_MAX, 1540)
  for it. The alternative was a second LWRAM allocation the size of the blob
  itself just to add a kilobyte and a half, during a save that is already
  holding two.

## Old saves still work

A record with no tail falls back to reading the old `...M` companion, so saves
written before this still restore their map. Writing a slot deletes any
companion left there: the record it belonged to has just been overwritten, so
its map is stale, and a stale map is worse than none.

## Not built or run

Host `gcc` only -- `test/save_blob_test.c` clean under `-Wall -Wextra`, plus 98
Python tests -- and `-fsyntax-only` against the real SRL headers in DEBUG and
release for `saturn_glue.cxx`, `save_ui.cxx`, `save_blob.c` and
`mojozork_saturn.c`. Nothing on Mednafen or hardware. **The save and restore
round trip has never been exercised**, on any build: the format change is
argued from the blob layout and tested at the seam, not by writing a record and
reading it back.

## Two things to check on hardware

- **A save taken before this and restored after it** takes the legacy path. That
  path is the one with no test at all, since it needs a real backup device.
- **The space warning now asks for one record's worth.** `save_space_warn` used
  to add two block costs; it now sizes `SAVE_BLOB_MAX + MAP_BLOB_MAX` as a
  single record, which is a smaller number, so a device that used to warn may
  now stay quiet. That is the point, but it means the warning's threshold moved.

## Unrelated, found on the way

`tools/tests/test_gen_presentation.py::test_regeneration_is_byte_identical`
calls `g.main([])`, which **writes the real `saturn/src/scene/game_presentation.inc`**.
Running the Python suite therefore dirties a tracked, shipped file. It passes
while the checked-in table matches what the generator would emit today -- and it
stopped passing mid-session because `tools/assets/presentation/ADVENT.json`
appeared (one room assigned through the review app, untracked), which makes the
generator emit a second game row. Nothing here caused it and nothing here fixed
it; the `.inc` was restored with `git checkout` rather than committed. The
generated table is simply behind the review app's store, and the test has no
isolation from it.

Related: [[zork1-situational-cues-wired-handoff]]
