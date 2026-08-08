---
name: classify-from-captured-turn-text
description: A wrong room mood is a question about the bytes in g_turn_text, not about the classifier — capture the real turn with host mojozork before reasoning about anything.
metadata:
  type: project
---

Zork III opened on a lake photograph in a dungeon, every run. I spent a long
stretch eliminating the classifier, the per-game fallback, the authored room map,
the category-to-folder mapping, the menu's slot pin and the `GameGenre` field
order — all sound work, all correct, and all of it reasoning about text I had
never actually looked at.

The bug was that the classifier never saw the room. Zork III prints a ~600-byte
dream sequence before its banner, turn one runs to 1050 bytes, and
`music_note_output` kept the first 511 and dropped the rest — so the buffer held
the dream alone, and `lake` (Biome) outranked the room's own `eerie`/`shadow`
(Feature). Fixed in `500a250`, which keeps the newest bytes instead; the
companion heading-trim is `673783c`. The ledger entry under
`.superpowers/sdd/2026-08-08-art-review-persistence/progress.md` has the full
chain and why neither fix works without the other.

## The check that should come first

Two minutes, no Saturn hardware, no guessing:

```sh
gcc -O2 -o /tmp/moj saturn/mojozork.c
printf 'quit\ny\n' | /tmp/moj tools/assets/Z3/ZORK3.Z3 > turn1.txt
```

Everything before the first `>` is exactly what lands in `g_turn_text`. Compare
its length against `MUSIC_TEXT_MAX`. Then feed those bytes through the real
engine — link `music.c`, `music_data.c`, `room_class.c`, `room_class_data.c`,
install a `music_set_category_fn` callback, push the text through
`music_note_output` in small chunks the way the interpreter prints, and call
`music_on_turn`. That reproduces the shipped path end to end on the host, and
building it against `<commit>~1` proves the test is sensitive rather than merely
green.

`test/room_class_test.c`'s header carries the classifier-only build line for when
the buffer is not in question.

## Why this keeps biting

The corpus cannot catch it. `tools/gen_room_corpus.py` issues `look` as its own
command specifically to get a clean room chunk, so every row in `rooms.inc` is
text the shipped code may never see in that form. The 1024-room snapshot stayed
green through both bugs. A green corpus says the classifier is consistent; it
says nothing about what reaches the classifier.

Neither bug is Zork III-specific. Any long intro, cutscene or verbose death
message can push the room out of a 512-byte window, and `g_room_cache` then
memoizes the wrong verdict for the whole session — so it presents as "this room
is always wrong" rather than as a transient.

Related: [[verify-before-claiming-root-cause]], [[user-runs-all-builds]],
[[room-art-pipeline-handoff]]
