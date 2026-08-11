# Controller Command Interface — Design

**Date:** 2026-08-10
**Status:** Proposed
**Touches:** `src/input/`, `src/video/`, `src/engine/saturn_glue.cxx`

## Goal

A gamepad player composes commands one letter at a time. `render_keyboard`
draws a 13x4 grid, the D-pad walks it, and a face button types the character
under the picker. Typeahead softens this — `predict_candidates` offers a ghost
completion the player can accept — but the unit of input is still a letter, and
"open the mailbox" is thirteen picker moves before the ghost can help.

Shadowgate never asked the player to spell. It showed the commands available
and let them be chosen. Everything needed to do the same here already exists in
the story file and is already being decoded for other purposes:

- `typeahead_extract.c` reads the v3 dictionary's part-of-speech flags and the
  parser's grammar tables, so verbs, nouns, directions and prepositions are
  already classified, and verb->preposition->object-class transitions already
  derived.
- `mojozork.c:1321` already reads global `0x10` every turn to get the current
  room object, feeding `music_on_turn`. Global 0 is the current room by v3
  specification — it is how the status line works.
- `saturn_story_data()` returns the **live** story image, the same bytes the
  interpreter mutates, so the object tree read at a prompt is current state and
  not the CD copy.

What is missing is a model of the room. This design adds one, and a panel over
it that assembles commands into the existing input line.

## Scope

In:

- `room_model` — per-turn decode of the current room's exits, contents and the
  player's carried items from the live story image.
- `command_panel` — a pure state machine for the three-module panel: focus,
  slot, page, and the sentence assembled so far.
- `command_view` — the panel's rendering, and reverse-video text support in
  `text_map` to draw selection and focus.
- Dispatch in `saturn_readline` between the existing keyboard editor and the
  new panel, plus the toggle binding and its persisted preference.

Out:

- Any change to how a command reaches the interpreter. The panel fills
  `KeyboardState.input` and calls `keyboard_submit`, exactly as the on-screen
  keyboard does. Echo, history, `g_output_start`, the reboot/quit intercepts,
  the F-key save/restore shortcuts and the trailing-`\n` contract are all
  untouched, because there is still one exit.
- The netbin build. It embeds no story (`ensure_online_typeahead` returns
  immediately there), so there is no dictionary to filter verbs against and no
  object tree. Netbin keeps the keyboard.
- Executing story code to resolve an exit. Conditional and function exits could
  be resolved by calling the routine, but story routines have side effects —
  they print, they set flags — and running one to populate a menu would change
  the game underneath the player.

## What the story file already answers

Verified against `saturn/zork1.dat` and `saturn/zork1-infodump.txt`.

### Direction property numbers come from the dictionary

Every `FL_DIR` (0x10) dictionary entry carries its direction property number in
its data bytes. Decoded from the shipped Zork I image:

| word | flags | data | property |
|---|---|---|---|
| north | 0x13 | `1f 00` | 31 |
| east | 0x13 | `1e 00` | 30 |
| west | 0x32 | `a1 1d` | 29 |
| south | 0x13 | `1c 00` | 28 |
| ne | 0x13 | `1b 00` | 27 |
| up | 0x18 | `fc 17` | 23 |
| down | 0x18 | `fa 16` | 22 |
| in | 0x18 | `fb 15` | 21 |

The property byte is not at a fixed offset — a word that is also an adjective
or preposition carries that class's value first. The rule is: **of a `FL_DIR`
entry's data bytes, the direction property is the unique byte whose value lies
in 1..31**, the v3 property-number range. A word yielding zero candidates, or
more than one, fails the decode.

Every entry above yields exactly one: `north` discards the `00` pad, `west`
discards its adjective value `a1`, `up` discards its preposition value `fc`.

The recovered set must then form a contiguous descending run ending at 31
(ZILCH allocates direction properties first, from the top down). That check is
the decode's own sanity gate: if it fails, the story is not ZILCH-compiled and
the whole room model reports unavailable.

### Property data length is the exit type

| bytes | meaning | rendered |
|---|---|---|
| absent | no exit in that direction at all | blank |
| 1 | unconditional exit to that room object | uppercase |
| 2 | blocked, with a refusal message | blank |
| 3+ | conditional / door / function exit | lowercase |

Object 81, "North of House", reads:

```
[31] 4b        north -> obj 75     open
[30] 4f        east  -> obj 79     open
[29] b4        west  -> obj 180    open
[28] 8e a7     south               blocked (the boarded windows)
[25] 4f        se    -> obj 79     open
[24] b4        sw    -> obj 180    open
```

That is Zork's map exactly, including the SE/SW shortcuts to Behind House and
West of House.

Door exits are partly resolvable without running code — the door object's open
attribute is a static read — but a door that a puzzle opens later is still a
door, so they stay lowercase rather than being promoted.

### Six characters is the parser's own resolution

A v3 dictionary entry holds 4 text bytes, which is 6 Z-characters. `mailbo` and
`mailbox` are the same word to the parser. The panel's 6-character word columns
therefore lose no distinction the game can make.

## Architecture

```
saturn_readline
   |-- room_model_refresh()      once per prompt, after run_room_transition
   \-- per frame, dispatch on mode:
         typeahead_edit()  -> existing keyboard path
         command_edit()    -> new panel path
                  |
                  v
         command_panel   (pure C state machine)
              reads room_model    (pure C decode)
              reads trie           (ranking only; NULL on Hard)
              writes KeyboardState.input  <- the one existing exit
```

### `src/engine/room_model.c/h`

Pure C. No SRL, no console, no trie.

`room_model_bind(story, len)` runs once per story load: locates the object
table, property defaults, globals and dictionary; builds the direction-word to
property-number map; runs the contiguous-run sanity check. Reports whether the
model is available.

`room_model_refresh()` runs once per prompt: reads global 0, walks the room
object's property table for exits, walks the child chain for contents,
resolves carried items.

`room_model_has_word(text)` answers whether the story's dictionary accepts a
word, by binary search over the sorted dictionary. This exists because **Hard
difficulty builds no trie at all** — `ensure_typeahead` returns early — so the
panel cannot depend on the trie for correctness, only for ranking.

Identifying the player object has no specification behind it. The model uses
the intersection heuristic: candidates start as the current room's children,
and on each room change the set is intersected with the new room's children.
Only the player follows the player, so this converges within two moves. Until
it converges, carried items simply do not appear and everything else works.

### `src/input/command_panel.c/h`

Pure C state machine. Owns which module has focus, which slot is being filled,
which page the word list is on, cursor positions, and the sentence so far. It
knows nothing about drawing or polling. Candidate ordering is injected — the
panel takes whatever order it is handed.

### `src/video/command_view.cxx/h`

Draws the panel, mirroring `render_keyboard`'s role.

### `command_edit()` in `console_view.cxx`

Beside `typeahead_edit`. Reads the pad, drives the panel, and on a completed
sentence writes it into `k.input` and calls `keyboard_submit`.

## Layout

One bordered strip, three modules separated by single vertical rules, exactly
40 columns: travel 13, words 15, commands 8. The input line sits above it.

```
...last line of room text

> open _
+-------------+---------------+--------+
|NW ^  N  ^ NE| look   take   | invent |
|   \ IN  /   | open   read   | look   |
|W --  +  -- E| drop   close  | save   |
|   / OUT \   | push   pull   | load   |
|SW v  S  v SE| move   v more | quit   |
|             |               |        |
+---L/R box---+-A=pick B=bck--+-Z=kbd--+
```

Every module is five rows of content over one blank row. Nothing is centred —
content starts at the top row and the trailing blank is shared, which is what
keeps the strip six rows deep rather than seven.

No module headers. The input line above carries what is being assembled, which
is what tells the player whether the word list is verbs or nouns.

The panel occupies ten rows: a blank row separating it from the game text, the
input line, and the strip's six content rows between two borders. The blank row
is structural, not padding — without it the last line of room text butts
against the prompt. `console_height` therefore returns 17 with the panel up,
against 21 with the keyboard and 26 with neither.

### Travel

The rose draws the room rather than listing twelve buttons. A direction renders
uppercase when decoded open, lowercase when conditional or undecodable, and
blank when there is no exit or the exit only prints a refusal — and a blank
direction takes its spoke with it. North of House decodes to:

```
+-------------+
|      N      |
|      |      |
|W --  +  -- E|
|   /     \   |
|SW         SE|
|             |
+---L/R box---+
```

The six non-compass directions ride the rose rather than taking rows of their
own:

| direction | when available | when not |
|---|---|---|
| up | carets flanking N, two spaces clear — `^  N  ^` | bare `N` |
| down | v's flanking S, two spaces clear — `v  S  v` | bare `S` |
| in | `IN` replaces the north spoke on row 2 | spoke `\|` or blank |
| out | `OUT` replaces the south spoke on row 4 | spoke `\|` or blank |

The flanking carets and v's draw from the inverted bank, so they read as active
markers rather than as letters. Nothing else in the travel module is ever
inverted — the D-pad is literal here, so there is no selection to indicate and
the highlight is free for this.

The two-space clearance puts the carets and v's at inner columns 3 and 9,
directly above and below the diagonal spokes, so the rose reads as one figure
rather than a letter with decorations. `IN`'s N sits beneath the compass N and
`OUT` centres on the same column; when a spoke's own direction is unavailable
and its word is too, the cell is blank like any other absent direction.

Blank never means unpressable. The D-pad is literal while travel holds focus,
every direction remains submittable, and a wrong guess costs one turn.

### Words

One module, two 6-character columns over five rows, filled row-major by
descending likelihood across all ten cells. `v more` claims the tenth cell only
when a further candidate exists and is absent otherwise.

The module switches word class as the sentence fills: verbs, then nouns, then
a preposition **only when the story's own grammar says that verb takes one**,
then the second noun. `take lamp` is two picks; `put coffin in boat` opens the
slots it needs.

Verb candidates lead with a curated core, in this order:

```
look   take   open   read   drop   close   push   pull
move   attack climb  enter  throw  turn    eat    drink
```

Each is dropped unless `room_model_has_word` (or, with no room model, the trie)
confirms the story accepts it, so a game without `attack` never offers it. The
story's remaining verbs follow behind, trie-ranked, on later pages. `look` also
sits in the commands module; that is a deliberate shortcut for the most-used
command, not a duplicate to remove.

Noun candidates come from the object tree first — the room's
children, the contents of open containers, carried items — with on-screen
vocabulary filling in prose-only scenery and Infocom's shared global objects,
which rooms reference by property rather than own.

### Commands

Five fixed entries, one per content row. Each routes to a
mechanism that already exists: `invent` opens the inventory overlay, `look`,
`save`, `load` and `quit` submit through `submit_command` exactly as the F-keys
do, inheriting the device/slot pickers and `confirm_return_to_title`.

Options is deliberately not here — START already opens it from the prompt.

### Inventory overlay

`invent` opens a smaller box drawn across all three modules, listing the
carried set from the object tree.

Picking an item fills the current slot **only when that slot takes a noun** —
then the overlay is the noun source for things held rather than things present.
With a verb slot active the overlay is a viewer only, and Accept closes it with
the sentence unchanged. Cancel always closes with the slot unchanged.

## Controls

| input | effect |
|---|---|
| L / R | cycle focus across the three modules |
| D-pad in travel | literal compass; a press submits that move immediately |
| D-pad elsewhere | walk the grid |
| Accept (mapped face button) | pick the highlighted entry |
| Back (mapped face button) | unwind one slot; from an empty sentence, return focus to travel |
| Z tap | swap to the on-screen keyboard and back |

Z is chosen because Y and Z do nothing on their own today — they are shift
modifiers, live only while held with a direction or shoulder — so a tap costs
no existing binding. It gets a row on Options > Controller > Configure and is
reassignable there like every other action.

## Rendering: reverse video

Selection and focus are carried by inverted glyphs. This cannot be done with a
palette bank: NBG3 is 4bpp and VDP2 treats *pixel value* 0 as transparent
whatever bank the cell selects, which is why `menu.cxx` needs a VDP2 window
just to get an opaque menu interior. Glyph tiles are ink at pixel value 1 on a
value-0 background, so no bank fills that background.

It can be done with inverted tiles, and this program already writes font tiles
at runtime — `install_block_glyph` fills the DEL tile with `0xFF` to make the
block cursor. Generalising it: at init, read the 95 printable tiles, map pixel
value `0 -> 1` and `1 -> 2`, and write the result into a second font bank.
A cell printed against that bank is a solid block of ink colour with the letter
punched out in CRAM entry 2.

This needs:

- ~3 KB of VDP2 VRAM for the inverted bank.
- CRAM entry 2 set to the backdrop colour, written alongside entries 1 and 15
  in `text_set_color` so a palette change keeps the two in step.
- `text_print_hl` in `text_map`, which is `text_print` with the alternate bank
  offset baked into the pattern name.

Applied to: the selected entry in the focused module, and the focused module's
bottom-border hint, so which module holds the D-pad is never ambiguous.

## Degradation

One fallback path covers every case where the room model is unavailable:

- an Inform-compiled v3 story, which has no direction properties at all;
- online play, where `online.cxx` builds its trie from `ZORK1.Z3` and then
  frees the story bytes, and the game state lives on the server regardless;
- any decode failing the contiguous-run sanity check.

In all three the panel still runs: verbs filter against the trie instead of the
dictionary, nouns come from on-screen vocabulary, the rose renders every
direction lowercase and fully pressable, and `invent` submits the `inventory`
command rather than opening the overlay.

## Difficulty

The panel is available at every difficulty; what it reveals follows the
setting.

| setting | words | rose |
|---|---|---|
| Easy | walkthrough's next verb and noun float to the first cells | walkthrough's next move marked |
| Medium | ranked by grammar, room contents and on-screen boost | as decoded |
| Hard | flat alphabetical | flat lowercase, no state |

Hard builds no trie, so its flat ordering is what the panel produces naturally
rather than a special case — but the exit shading is suppressed deliberately,
because decoded exits are precisely the guidance Hard exists to withhold.

## Options and persistence

- A gamepad gets the panel by default; a real keyboard is unaffected and keeps
  the prompt it has today.
- Options > Gameplay gains a row selecting the default interface, persisted in
  MOJOOPTS beside `g_difficulty` and `g_verbosity`.
- Options > Controller > Configure gains the toggle-binding row.

## Testing

`room_model` and `command_panel` are plain C with no SRL, so both take host
tests beside `test_typeahead_oom.c` and `test_room_genre.c`:

- **Direction map** — bind against the real `zork1.dat` and assert
  north=31, east=30, west=29, south=28, ne=27, up=23, down=22, in=21.
- **Sanity gate** — a synthetic dictionary whose direction properties are not a
  contiguous run down from 31 must report the model unavailable.
- **Exit decode** — object 81 must report N/E/W/SE/SW open and S blocked.
- **Exit types** — one case per property length: absent, 1, 2, and 3+.
- **Word filter** — `room_model_has_word` accepts a known Zork verb and rejects
  a word the story does not define.
- **Panel assembly** — drive the state machine and assert the string it writes:
  a two-slot command, a command whose grammar opens a preposition slot, Back
  unwinding a slot, and paging past the tenth candidate.
- **Fill order** — nine candidates fill all cells with no `v more`; ten or more
  put `v more` in the last cell.
- **Inverted tiles** — assert the pixel transform on a known tile pattern. The
  VRAM write itself is not host-testable and is verified on hardware.

## Risks

- **The second font bank's VRAM may not be free.** SRL's font banks sit 128
  tiles apart (`SetFont(n)` -> `128 * (5 - n)`), so bank 1 at offset 512 looks
  unclaimed, but this is inferred from the encoding rather than confirmed
  against what else lives in `VDP2_VRAM_B1`. Confirm before the plan commits.
  Fallback: an inverted set covering only `a-z`, `0-9` and space.
- **Player-object identification is heuristic** and converges only after a room
  change. Carried items are absent until then; nothing else depends on it.
- **Four console rows** are lost to the panel. If 17 proves too tight in play,
  the strip can drop to five content rows and hand one back: every module's
  content already fits in five, so the only loss is the shared trailing blank
  and the strip gets visually tighter.
