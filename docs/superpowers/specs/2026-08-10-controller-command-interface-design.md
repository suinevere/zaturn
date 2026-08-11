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
> open _
+-------------+---------------+--------+
|             |               |        |
|NW ^  N  ^ NE| look   take   | invent |
|   \ IN  /   | open   read   | look   |
|W --  +  -- E| drop   close  | save   |
|   / OUT \   | push   pull   | load   |
|SW v  S  v SE| move   v more | quit   |
|             |               |        |
+---L/R box---+-A=pick B=bck--+-Z=kbd--+
```

The strip is seven rows: a blank under the top border, five rows of content,
and a blank above the bottom border. Both blanks span all three modules, so
every module's content occupies the same five rows and nothing needs centring
logic.

No module headers. The input line above carries what is being assembled, which
is what tells the player whether the word list is verbs or nouns.

The panel occupies ten rows: the input line, then the strip's two borders and
seven rows between them. `console_height` therefore returns 17 with the panel
up, against 21 with the keyboard and 26 with neither.

### Travel

The rose draws the room rather than listing twelve buttons. A direction renders
uppercase when decoded open, lowercase when conditional or undecodable, and
blank when there is no exit or the exit only prints a refusal — and a blank
direction takes its spoke with it. North of House decodes to:

```
+-------------+
|             |
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

Noun candidates are decided by the object tree and worded by the vocabulary. The
tree says what is *present* — the room's children, the contents of open
containers, carried items — and those objects' words are matched from the
trie's on-screen vocabulary, or from the dictionary directly on Hard where no
trie exists. `room_model` deliberately does not decode object short names:
the only Z-string decoder in the tree (`typeahead_extract.c`'s `decode_at`)
reads a story pointer that the trie builder sets, and Hard never runs that
builder. On-screen vocabulary also fills in prose-only scenery and Infocom's
shared global objects, which rooms reference by property rather than own.

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

Selection and focus are carried by inverted glyphs.

Neither the hardware nor the palette can do this per cell. A pattern name
carries palette bits and H/V flip, but no invert bit. And colour-0 transparency
is a per-layer setting (`slScrTransparent`), not per-cell — turning it off to
make pixel value 0 paint would make *every* console cell opaque and hide the
room art behind the text. `menu.cxx` needing a VDP2 window just to get an
opaque menu interior is the same constraint seen from the other side.

So inversion has to be a tile write, and the only real question is where those
tiles live. They live in tiles the program already owns.

`TEXT_FONT_BANK` is 640, so font 0 spans tiles 640..767 — the full 128-code
ASCII range — and `install_block_glyph` already writes into the top of it
(`0x7f + 640`) to make the block cursor. Character codes **0x00..0x1F are
control codes this program never prints**, which leaves 32 tiles already
allocated to font 0 and guaranteed inert.

Inverted glyphs are generated on demand into those slots: read the wanted
character's tile, map pixel value `0 -> 1` and `1 -> 2`, write it to a scratch
slot, and point the cell at that slot's character code. The result is a solid
block of ink colour with the letter punched out in CRAM entry 2.

This needs:

- **No new VRAM.** The 32 control-code tiles are already font 0's.
- A `char -> slot` cache with a generation stamp. A selection that persists
  across frames writes nothing; a changed one writes a few 32-byte tiles. The
  writes go in the existing `OnAfterSync` callback, immediately before the
  shadow flush, so they land in vblank beside the map copy.
- CRAM entry 2 set to a fixed dark colour, written alongside entries 1 and 15
  in `text_set_color` so a palette change keeps them in step. Leaving the
  letter pixels transparent instead would show the room picture through the
  letter, which is unreadable over art.
- `text_print_hl` in `text_map`, which resolves each character to its scratch
  slot and bakes that code into the pattern name.

Demand fits the 32 slots with room to spare: a selected word cell is at most 6
distinct characters, a border hint about 10, and the compass markers 2 — about
18 worst case. Overflow degrades to drawing that cell uninverted rather than
failing.

Applied to: the selected entry in the focused module, the focused module's
bottom-border hint, and the compass's `^`/`v` up-down markers.

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
| Easy | walkthrough's next verb and noun float to the first cells | as decoded |
| Medium | ranked by grammar, room contents and on-screen boost | as decoded |
| Hard | curated core verbs first, then the story's own vocabulary enumerated from the dictionary | flat lowercase, no state |

Hard's exit shading is suppressed deliberately, because decoded exits are
precisely the guidance Hard exists to withhold.

Hard is not "the panel, degraded". It builds no trie, so it has no ranking
signal at all — but a panel whose first cells are alphabetical accidents is
worse to use than one led by the verbs any player reaches for, and that
ordering reveals nothing about *this* room. So the curated core leads at every
difficulty; only the boosts below it change.

Nouns on Hard come from a dictionary enumerator rather than the trie. The
trie is the vocabulary source everywhere else, and its absence would otherwise
leave the noun column permanently blank — which reads as "this room is empty"
rather than "this mode lists no nouns", the worst of both. `room_model` already
walks the dictionary for `room_model_has_word`; enumeration is the same walk
without the early return.

The Easy rose carries no walkthrough marker. Easy still floats the solution
overlay's verbs and nouns to the first cells, which needs no new export; a
compass marker would have required a new accessor out of the typeahead and a
fourth state on an already dense 13-column figure.

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
- **Inverted tiles** — assert the pixel transform (`0 -> 1`, `1 -> 2`) on a
  known tile pattern.
- **Slot cache** — a repeated character reuses its slot without a rewrite; a
  changed selection releases slots it no longer needs; a nineteenth distinct
  character still allocates and a thirty-third degrades to uninverted. The VRAM
  write itself is not host-testable and is verified on hardware.

## Risks

- **The scratch slots assume nothing prints a control code.** `text_print_str`
  writes the raw byte plus the font bank with no filtering
  (`text_map.cxx:147`), so a string carrying 0x01..0x1F would land on a slot
  and draw a stale inverted glyph. Nothing passes one today, but story output
  reaches the console through `saturn_writestr` and is only as clean as the
  game's ZSCII. Confirm, and if it is not guaranteed, filter in
  `text_print_str` rather than trusting callers.
- **Player-object identification is heuristic** and converges only after a room
  change. Carried items are absent until then; nothing else depends on it.
- **Four console rows** are lost to the panel. If 17 proves too tight in play,
  the strip can drop to five content rows and hand two back: every module's
  content already fits in five, so the only loss is the two blanks and the
  strip gets visually tighter.
