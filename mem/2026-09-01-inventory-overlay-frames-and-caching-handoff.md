---
name: inventory-overlay-frames-and-caching-handoff
description: The inventory overlay's chrome moved from printed ASCII onto the tile dashboard's own stone, the item picture got a frame of its own, OITEM.CZ moved to a load-time read held for the session, and the "Inventory does nothing until you move a room" bug was traced to the player-object heuristic; five commits on the branch, none of it seen on screen.
metadata:
  type: project
---

Continues [[inventory-item-pictures-handoff]], which is where the feature came from and
which is now PARTLY STALE on the overlay's geometry and on the archive's lifetime --
both changed here. Its account of the picture binding, the TVALUE measurement and the
`bg.bat` staging bug still stands.

Same branch, `inventory-item-pictures`, still **not merged and not pushed**. Five
commits on top of the nineteen that handoff lists:

`7e48eec` → `a90f661` → `b6ccdba` → `8026e63` → `b4a0ce2`

Read the commit messages for the what. This file carries only what they do not say.

## The owner's five reports, and what each turned out to be

The session started from five hardware observations. Four were what they looked like.
One was not.

**"Choosing inventory in the controller dashboard doesn't show the inventory menu until
moving one room."** This was the interesting one, and the qualifier the owner attached
to it -- *"only if an image doesn't exist"* -- is still unexplained. The defect found is
image-independent: `room_model.c` identified the player object *only* by intersecting
two consecutive rooms' child sets, so `room_model_player()` was 0 until the first room
change, `ncarried` was 0 with it, and `cv_cmd_accept` fell through to submitting the
typed `inventory` command instead of raising the overlay. That accounts for "until
moving one room" exactly. It does not account for "only if an image doesn't exist", and
nothing in the render path does either -- the two branches differ only in geometry and
both draw every frame. **Ask the owner to re-check that qualifier on hardware.** If it
survives the fix, it is a second bug and this handoff has not found it.

The other four were straightforward and the commits describe them.

## What the fix rests on, and why it is not a guess

The new player heuristic (`player_from_pickup` in `room_model.c`) says: of the objects
standing in this room, the one whose own child count went up since the last prompt is
the player. The argument that it cannot be fooled early is that the *first* inventory
transfer of any game is necessarily a take -- nothing can be put down before something
has been picked up -- and a take moves the thing under the player. A put would name the
container instead, but by then the question is already answered.

That was not reasoned about in the abstract. It was checked against the real
interpreter: `saturn/mojozork.c` builds standalone on the host with plain `gcc -O1 -w`,
and a copy in the scratchpad with a dump inserted at the top of `opcode_read` prints
the room's children and each child's child count at every prompt. Feeding it
`open mailbox / take leaflet / inventory / drop leaflet / take leaflet / north` gives:

```
room=180  children: 4(0) 181(0) 160(1)      <- first prompt
room=180  children: 4(0) 181(0) 160(1)      <- after "open mailbox", nothing moved
room=180  children: 4(1) 181(0) 160(0)      <- after "take leaflet": 4 grew, 160 shrank
room=180  children: 161(0) 4(0) 181(0) ...  <- after "drop": the ROOM grew, not a child
```

Object 4 is the player (`cretin`). Exactly one grower, on the turn it matters, with no
room change. That transcript is the whole evidential basis for the heuristic and is
cheap to reproduce -- do it before touching this code again.

**A heuristic that was tried and rejected**, so nobody spends the afternoon on it
again: scanning the global-variable table for a global naming one of the room's
children. Every ZIL game keeps the player in a global (`WINNER`), so this looks
obviously right. The same probe shows it is not: at the first prompt in West of House,
`G111`, `G128` and `G171` all name object 4 -- but `G064` names object 160, the mailbox.
Two candidates, every prompt, so a refuse-when-ambiguous rule never latches and a
take-the-majority rule has a margin of one. On the *cold* image it looks even better
than it is, because the player is parentless until `GO` runs, so a static probe of
`zork1.dat` reports zero candidates and tells you nothing. The globals are a dead end;
the object tree is not.

## The tile work, and the one thing it pins

The second frame around the picture is 20 new tiles appended to the generated set
(`DT_PIC_*` in `dash_map.h`, emitted by `tools/gen_dash_tiles.py`, `N` 83 → 103).
Appended rather than inserted because every index before them is a literal in
`dash_tiles.c` -- the same reason the dead `DT_BOX_*` set is still in there.

They are the existing four-pixel bead with its ramp measured from the opposite edge, so
it closes against the picture instead of around the module. Rendering a tile as hex
digits is the fast way to check that; `DT_TOP0` reads `7/3/2/D` down from the top of the
cell and `DT_PIC_TOP0` reads the same four up from the bottom.

**The thing to know:** their marble phase is pinned to the ring's actual screen rows and
columns (17/28 and 29/38) the same way `EDGE_RP` and `DIV_CP` pin the outer frame's.
Move the overlay and they must be regenerated. Worth noting that the existing constants
are *already* stale by this standard -- `EDGE_RP = 3` and its comment claim the panel's
top row is screen row 19, but with `MENU_SCREEN_ROWS = 30` it is 21. Nobody has ever
noticed the resulting phase discontinuity, which is a useful calibration on how much
this matters: the bead overwrites the outer four pixels either way, and the four left
are seeded noise.

## Caching OITEM.CZ

The read moved to `item_art_set_game`, called from `main.cxx:623` -- after
`menu_fade_out_hold()` and before `music_start()`, which is the one window in the
session where a 40 KB blocking read costs nothing. Nothing in `render_command_panel`
opens or frees any more; `item_art_close()` at the two title-screen boundaries in
`main.cxx` still frees it.

The Low Work RAM budget did not move and did not need to. `ITEM_ART_RESERVE` in
`saturn/tests/test_lwram_budget.py` was always checked against the whole in-game
pairing -- trie + area archive + save scratch -- because the window the archive used to
be resident in sat inside all three. Only the comments changed. Do not let a reviewer
talk you into "but now it is resident longer, so the budget must grow": the peak is the
same set of claimants.

`music_pause`/`music_resume` are still wrapped around `item_art_open`'s disc work even
though the load-time call no longer needs them (nothing is playing yet). They cover the
lazy retry `item_art_show` falls back to if the load-time read was refused, which is the
only path that can still hit the drive mid-session.

## What is unverified

Nothing in these five commits has been built to an ISO or seen on hardware. Host tests
and both SH-2 syntax-check configurations are the whole verification. In particular:

- The second frame's appearance is inferred from hex dumps of the tile data. It has
  never been rendered.
- The black no-image fill is `0x8000` (opaque black) in every palette entry with the
  window filled at index 1. The reasoning is that VDP2 treats palette index 0 as
  transparent for a scroll screen and ignores the CRAM word's top bit, so any non-zero
  index over an all-black palette is opaque black. Not confirmed on hardware.
- The list gained rows in both shapes (7 instead of 5 plain, 12 instead of 10 tall)
  as a side effect of the box's frame becoming the strip's frame. Paging by those
  counts is untested against a real inventory of more than that many items.

## Pre-existing, not caused here

`saturn/tests/test_ci_boot_music.py` fails, and failed before this session: it asserts
`release.yml` invokes `tools/assets/pvms.bat`, and `SPLASH.PCM` is gitignored. It is a
bare script whose `main()` runs at import, so it takes the whole pytest run down with an
INTERNALERROR rather than a failure -- run pytest with
`--ignore=saturn/tests/test_ci_boot_music.py` until somebody fixes it.

## Suggested skills

- **`superpowers:systematic-debugging`** and **`diagnosing-bugs`** if the owner's
  "only if an image doesn't exist" qualifier survives hardware re-testing. The
  temptation will be to reason about the render path from the code; the mojozork probe
  above is the pattern that actually settled the last one.
- **`superpowers:verification-before-completion`** before telling the owner any of this
  works. None of it has been seen.
- **`code-review`** against `main` before merging the branch -- 24 commits now, and the
  last whole-branch review (recorded in [[inventory-item-pictures-handoff]]) predates
  all five of these.
- **`run`** to get the disc built and the overlay on screen, which is the single most
  valuable next action.

## Also worth knowing

The Bash tool mangles backslashes inside quoted heredocs on this setup -- a Python
`'\\0'` reached the interpreter as a NUL byte and a string replace silently failed to
match while other blocks in the same script matched fine. Write scripts to the
scratchpad with the Write tool and run them by path, and `assert old in s` before every
write. Recorded in the session memory as `bash-tool-python-stdin-hangs`.
