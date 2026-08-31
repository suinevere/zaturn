# Zork I authentic backgrounds and audio — design

Zork I is the one game in the catalogue whose original console release has been
reverse-engineered down to the byte. `docs/ZORK1_ROOM_BACKGROUNDS.md` and
`docs/ZORK1_AUDIO_MAP.md` give, for all 110 rooms, the exact background frame and
the exact CD-DA track the Sega Saturn release showed and played. This design
spends that on the one game that has it: in Zork I every room shows its own
original picture and plays its own original track, and no other game changes at
all.

Sub-project A+B of the decomposition recorded in
[the handoff](#relationship-to-other-work). Item pictures (C), sound effects (D),
the seven unattributed tracks (E) and the ending art (F) are out of scope here
and get their own cycles.

## What makes this cheap

Three facts, all of them measured rather than assumed, are what make an
authentic-presentation feature small instead of enormous:

**The soundtrack is already on the disc at the right track numbers.**
`saturn/cd/music/tracklist` is the Japanese Zork I soundtrack, tracks 2–32.
`ZORK1_AUDIO_MAP.md`'s room column holds raw disc track numbers in that same
2–32 range. The audio half of this feature is a table, not an asset job — no
new audio ships, and no disc space is spent.

**The room sets are the same size on all three sides.** `ZORK1.Z3` (release 88,
serial `840726`) decodes to 110 rooms; the Saturn presentation record covers
rooms 0–109; and `1dungeon.zil` declares exactly 110 `<ROOM>`s. 97 of the 110
join on title alone.

**The screen can carry the frames unaltered.** The frames are 320x240 8bpp with
a 256-entry RGB555 CLUT each. SRL supports `Normal320x240`, and
`SRL::VDP2::NBG0::TransparentDisable()` (wrapping SGL's `slScrTransparent`) lets
NBG0 treat palette index 0 as a real colour. So the pixels reach the screen as
the bytes the original disc holds: no crop, no scale, no requantisation, and
none of `make_tga.py`'s index-0 shift.

## The data

The original keeps picture, music and sound-effect bank in **one** 16-byte
per-room record at `0x75060` in `0ZORK.BIN`. This design mirrors that rather
than inventing a seam the source data does not have: one generated table,
object-indexed, keyed by release and serial exactly as `game_rooms.inc` already
keys `GAME_ROOM_MAP`.

New generated file `saturn/src/scene/game_presentation.inc`:

```
GAME_PRES_ZORK1[256]   /* three bytes per Z-machine object number */
    image     1-based index into IMAGE_FRAME; 0 = no authored picture
    track     raw disc track 2..32; 0 = silence
    se_bank   0..10; nothing reads this until sub-project D

IMAGE_FRAME[75]        /* where a frame lives inside its archive */
    area      0..10, indexing the archive name table
    offset    byte offset of the record inside the .CGL
    length    byte length of that record
```

`image` is 1-based so that 0 means "unauthored", the same convention
`game_rooms.inc` already uses for scenes ("scene index plus one"). The area is
derivable from the image, so no room column names it.

`IMAGE_FRAME` exists because a `.CGL` is a chain of records with **no
compressed-length header** — a record's end is only discovered by decompressing
it. So reaching frame 14 of `BCEL` by walking means decompressing the thirteen
before it, which is why the original's own room record carries offset *and*
length instead. The offsets are generated for the same reason. 75 entries is
roughly 600 bytes.

Keyed by `(release, serial)` so the table can only ever bind to the story it was
generated from. A different Zork I release renumbers objects, and a table bound
to the wrong one would put the Troll Room's picture in the Attic.

### Generating it

New `tools/gen_presentation.py` joins three sources:

1. `analysis/zork_bg/room_backgrounds.csv` — Saturn room index → frame, archive,
   offset, length, CD track, SE bank.
2. `tools/assets/rooms/ZORK1.json` — Z-machine object number → room title, from
   `gen_room_inventory.py`.
3. `cd/Zork I - The Great Underground Empire (Japan)/zork1/1dungeon.zil` — the
   110 `<ROOM>` declarations, giving canonical identity (`MAZE-1`…`MAZE-15`,
   `DEAD-END-1`…`DEAD-END-5`, `RIVER-1`…`RIVER-5`) and full exits. This source
   compiles to Zork I release 119, serial `880429`, not the release 88, serial
   `840726` the disc actually boots — its `<CONSTANT SERIAL 0>` is a build-time
   placeholder, discoverable only by compiling it. It is therefore a sound
   witness for room count and map geography, both stable across Zork I
   releases and both re-verified here (110 rooms on all three sides, the two
   contested exits unchanged), but it is not authoritative for anything
   release-specific, such as attribute numbers or object numbering.

97 rooms join on title. The other 13, across 12 distinct titles, are the same room
renamed between the Japanese release and the story file, resolved by a checked-in
alias table rather than fuzzy matching:

| Story file | Saturn |
|---|---|
| SMELLY ROOM | FOUL ROOM |
| ROCKY LEDGE | LEDGE |
| TWISTING PASSAGE | CURVED PASSAGE |
| STRANGE PASSAGE | NARROW PASSAGE |
| CAVE (x2) | SHAFT (x2) |
| THE TROLL ROOM | TROLL ROOM |
| DAM BASE | BASE OF DAM |
| EGYPTIAN ROOM | EGYPT ROOM |
| STONE BARROW | BARROW ENTRANCE |
| MAINTENANCE ROOM | CONTROL ROOM |
| DAM | FLOOD CONTROL DAM |
| LAND OF THE DEAD | LAND OF THE LIVING DEAD |

The alias table is committed data and reviewed by a human once. It is not
inferred, because two of these pairs (`CAVE`/`SHAFT`, `STRANGE
PASSAGE`/`NARROW PASSAGE`) share no words and no edit distance would find them.
Those same two are the only rows in the table that are not self-evident, and
both must be confirmed against the ZIL's exits before the table is committed
rather than taken from this document.

Duplicate titles resolve through ZIL identity plus area. Ordering inside a
duplicate group only changes which picture a room gets in three places — the
maze (`BMAZ_00` for four rooms, `BMAZ_01` for eleven), the river (`BRIV_01` for
four, `BRIV_02` for one) and the dead ends (`BMAZ_02` for four maze ones,
`BMIN_05` for the mine one). Everywhere else a duplicate group shares a single
image and the ordering is unobservable.

**The generator refuses rather than guesses.** An unresolved room, a duplicate
assignment, or a story whose release and serial are not 88 / `840726` is a build
error. A zero written for a room that should have had a picture would show up
only as a background that silently fails to change, which is the hardest class
of bug to notice.

Zork I's row in `GAME_ROOM_MAP` becomes dead weight, and the 35-group
scene-blessing task that `mem/2026-08-22-scene-tagged-art-handoff.md` queued for
Zork I is discharged — the authentic table answers the same question exactly, by
measurement instead of judgement.

## The loader

Two modules, split the way `dash_map`/`dash_view` already splits, so the hard
part is host-testable with plain gcc:

**`saturn/src/video/cgl.c` / `cgl.h`** — pure logic. The Okumura LZSS variant
(4 KiB ring initialised to 0, write pointer at n−18, 4-byte little-endian
decompressed size) and the record layout (`[256-entry RGB555 LE CLUT = 512
bytes][LZSS stream → 320x240 8bpp]`). No SRL, no disc, no VDP2. Ported from
`analysis/zork_cgl.py`.

**`saturn/src/video/room_art.cxx` / `room_art.h`** — the SRL half: the resident
archive, the disc read, the decode target, the upload.

### Entering a room

1. Look up `GAME_PRES_ZORK1[obj]`. `image == 0` holds whatever is showing.
2. `IMAGE_FRAME[image-1].area`. If that archive is not the resident one, free
   the buffer and read `B<AREA>.CGL`. **This is the only disc read on the path.**
3. Copy the record's first 512 bytes into `Colors[256]`; decompress the rest of
   the record into the decode target.
4. Upload.

Step 4 is the existing `tga_blit_nbg0` unchanged: it takes `{Pixels,
Colors[256], W, H}` and hands it to `SRL::VDP2::NBG0::LoadBitmap`, and its own
header records that it "touches no CD, so it is safe to call with music
playing". A decoded CGL frame is exactly that shape, so the fade, the dim, the
image window and the Dynamic pin all keep working with no change.

### Memory

| | |
|---|---:|
| Largest archive (`BCEL.CGL`, 15 frames) | 408.5 KB |
| Decode target (320x240 8bpp) | 76.8 KB |
| Palette | 0.5 KB |
| **Peak** | **~486 KB** |
| LWRAM, less the 96 KB save floor | 928 KB |

It fits because `title_bg_cache_release()` — which already exists — drops the
nine-slot TGA cache when a game carrying a presentation table starts. **The two
art paths never hold memory at the same time.** All eleven archives together are
2.0 MB, so keeping more than one resident is not on the table; the same 75
frames as loose TGAs would be 5.6 MB on disc and would thrash a nine-slot cache
every time the player crawled through `BCEL`'s fifteen rooms.

### Disc

The eleven `.CGL` archives ship verbatim, 2.0 MB, alongside the existing
`TGA/` tree. Against that, the 12 MB of mood-era TGAs under `saturn/cd/data/TGA/`
have been unreadable since `GAME_SCENE` went all-zero and go. Net change is
about −10 MB.

### Timing and failure

The archive read completes **before** the new track is requested, so the drive
is never asked to seek for data and stream audio in the same moment. An area
change is already a track change, so the one disc read lands where the music was
going to break anyway.

Archive missing, read failure, or a stream that would overrun its target: hold
the current picture, and say nothing on screen. Art is decoration; the game stays
playable, and a failed load must never be able to blank the screen.

## The screen

- `SRL::Core::Initialize(..., Normal320x240)` in `main.cxx`.
- `TGA_PLANE_MAX` in `title.cxx` to `320*240`, which takes a wallpaper cache slot
  from 74.2 KB to 79.4 KB; nine slots is 715 KB, still inside LWRAM.
- `TransparentDisable()` on NBG0.
- `SCREEN_ROWS` at `console_view.cxx:34` from 28 to 30, and the layout pass that
  follows from it.

The comment at `main.cxx:352` explains why the client narrowed to 224 in the
first place — "every layer this client paints is 224 lines tall … the surplus 16
lines were painted by nothing and showed the back-plane colour as a band". That
reasoning is circular with respect to this work, and it was written when no
picture wanted the extra lines. Now one does, and the moment is cheap:
`tools/assets/png/` holds no source art at all, so **no existing picture has to
be re-cut**.

`TOP_MARGIN` reserves row 0 because overscan clips the first text row on real
hardware, so content rows become 1–29 and the console gains two rows. The
bottom row is now the exposed edge. If it clips on a real set, a bottom margin
is one constant and costs one of the two rows back — that is a judgement to make
once it is on a TV, not before.

`dash_map`'s variants are already anchored relative to the input row, so the
dashboard follows the console down without arithmetic. Everything positioned by
`TOP_MARGIN + console_height()` follows too. The layout pass is a review of what
does *not* derive from those.

PAL keeps the same class of problem it already has, smaller: SRL's PAL default
is 320x256, so 240 leaves 16 unpainted lines rather than 32.

`make_tga.py` keeps writing 320x224 for the shared TGA path — those pictures sit
with a margin — or moves to 240 whenever art is next sourced for the other
thirty games, which is free today.

## Music

`MIX_DYNAMIC` gains one branch: when the running game carries a presentation
table, the room's track comes from that table rather than from a scene pool.
`MIX_OVERRIDE`, `MIX_SEQUENTIAL` and `MIX_RANDOM` are untouched, and every game
without a table behaves exactly as it does today. No new mode appears on the
Sound page and the player's existing mental model does not change — Dynamic
simply becomes exact for the one game that can be exact.

Four behaviours, each of which is wrong by default if not stated:

**Same track means do nothing.** Most room changes stay inside an area and keep
the same track. When the new room's track equals the playing track the music is
not restarted, not faded and not re-issued. This single rule is the difference
between an authentic score and a score that stutters on every step.

**Track 0 stops the music.** Ten rooms are authored silent — Barrow Entrance,
Grating Room, Atlantis Room, Base of Dam, the five Frigid River rooms and Slide
Room. That silence is composition, not a gap to fill from a pool.

**Looping is forever.** `MUSIC_DYN_LOOPS` — the three-passes-then-move-on rule —
does not apply here. The original issues start track *n* index 1, end track *n*
index 99, mode `0x0F`; `music_play_fn(track, loop=1)` already means the same
thing.

**Stings return to the room.** `event_scan`'s danger and triumph still fire. On
the pool path the engine simply moves on afterwards; here it must come back. A
sting plays one-shot and then the room's own track resumes.

## Dark rooms

An unlit room draws black rather than its picture. Showing a fully rendered cave
that the player is being told they cannot see is the one place where authentic
art fights the game's own fiction.

**How the engine learns a room is unlit is an implementation check, not a
settled fact.** The likely path is the room object's `ONBIT` attribute read
through the same object-table access `room_model` already uses, verified against
a walkthrough that goes below ground without the lamp. If it cannot be
determined reliably, the picture shows and this detail waits for its own cycle.
A darkness signal that is wrong in the false-positive direction blanks the
screen during normal play, which is far worse than the fidelity it buys.

**The investigation ran, and the signal is not reachable.** Two reasons,
either sufficient on its own. First, the ZIL release mismatch above: no
symbolic `ONBIT` number taken from that source can be trusted against the
binary the disc actually boots. Second, and stronger because it does not
depend on which release compiled: Zork's own `LIT?` routine scope-scans a
room's contents and its open containers, while `room_model.c` walks immediate
children only, so a carried lit lamp — the ordinary case underground — is
exactly what a room-attribute-only check would miss. Dark rooms therefore
keep showing their picture, the fallback already named above. What would
unblock this is giving `room_model` a scope-aware traversal.

## Fallbacks

In order:

1. No presentation table for the running game → the existing scene path,
   unchanged. This is thirty of the thirty-one games.
2. Table present, image index 0 → hold the current picture.
3. Table present, archive will not read or the stream would overrun → hold the
   current picture.

Nothing on this path can leave the player without a playable game, and nothing
prints to the screen when it fails.

## The netbin

The netbin has no disc. `room_art` compiles out there through the existing
`netbin_nocd.c` pattern, and must not drag GFS or the CD filesystem into that
link — the same undefined-symbol trap that cost 83 KB before it was found. The
size gate catches it, but late.

## Tests

- **`cgl.c` on the host decodes all 75 frames** and matches the reference output
  already in `analysis/zork_bg/png/`. This makes the SH-2 port provably correct
  before it runs on hardware once.
- **The generator resolves all 110 rooms**: no zeros, no duplicate assignments,
  and a refusal when the story's release and serial are not 88 / `840726`.
- **Generated files regenerate byte-identically**, the pattern the scene tables
  already use. `.gitattributes` pins `saturn/src/scene/**` to `eol=lf`; the new
  `.inc` inherits that.
- **Music**: the room's track is chosen from the table; an equal-track
  transition does not re-issue; track 0 stops; a sting returns to the room's
  track afterwards.
- **`syntax-check.sh` clean** for both the CD and netbin configurations, and the
  netbin size gate unmoved.

## Relationship to other work

Sub-projects deliberately left out, in the order they should follow:

- **E — the seven unattributed tracks.** Tracks 7, 13–17 and 32 are reached only
  through the byte-pointer path at `0x06048adc`/`0x06048bbe`, which static
  tracing cannot resolve. A runtime capture using the breakpoint procedure in
  `docs/ZORK1_MAP_RECON.md` would settle them. If one turns out to be a room
  theme, `game_presentation.inc` is regenerated — which is why this is cheap to
  do early and annoying to do late.
- **C — item pictures.** The 19 `OITEM.CZ` pictures (64x80, 8bpp, own CLUT each)
  in a fixed pane beside the inventory list, on NBG1 in VDP2 bank A1 — which the
  input-dashboard design explicitly left free "for whatever wants a bitmap next".
  Needs the 19 identified by eye and bound to object numbers; they are Zork's
  treasure set plus a couple of tools, and their names are not decoded.
- **D — sound effects.** The eleven SE banks field `+2` selects. Unstarted: no
  file located, no sample format, and — the real unknown — no trigger model. A
  spike before it is a project.
- **F — ending and transition art.** `HUS_BAR.TPG` and `BBAR_01`, the one frame
  no room references.

Supersedes the Zork I half of
[`mem/2026-08-22-scene-tagged-art-handoff.md`](../../../mem/2026-08-22-scene-tagged-art-handoff.md):
that document's "bless Zork I" and "source its art" owner tasks are discharged
for this game by measurement. Both still stand for the other thirty.
