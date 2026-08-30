# Zork I (Saturn JP) — string-table recon (`0ZORK.BIN`)

Foundational RE for the translation (tier-independent). `0ZORK.BIN` loads at `0x06004000`,
so address = `0x06004000 + file_offset`. Main Japanese prose block ≈ file `0x88000`–`0x9c000`.

## String pointer tables (found 2026-06-20)

Big-endian 32-bit absolute pointers (into the prose region). Located by scanning for runs of
pointers targeting `0x0608c000`–`0x0609e000`:

| Table addr (file)        | Entries | Targets | Content |
|--------------------------|---------|---------|---------|
| `0x0608f074` (f`0x8b074`) | 98      | room descriptions; entry[0] → `0x0608ef94` = kana/symbol/**abbreviation set** |
| `0x0608f2f4` (f`0x8b2f4`) | 33      | (TBD) |
| `0x06099a68` (f`0x95a68`) | 234     | short **object/room names** ("みかげ石"=granite, "青い鳥", "トロル"=troll) |
| `0x0609dbe8` (f`0x99be8`) | 777     | longer **messages / room descriptions** |

(The earlier "1001-entry @0x0609f238" run was a false positive — points into itself/garbage.)
Tables are embedded near the end of the prose block; targets overlap (shared suffixes), typical
of an abbreviated dictionary.

## String format

- **Shift-JIS**, **`0x00`-terminated**.
- **Abbreviation / substitution codes** (Z-machine-style "print a common word/object"):
  - `0x0e XX` — expand abbreviation/word #XX (e.g. `\x0e#` ≈ "木/trees", `\x0e"`, `\x0e\x02`).
  - `0x1e XX` — second substitution bank.
  - These compress the text; the dictionary is the kana/word set around `0x0608ef94`.
- **`0x1c`** — newline / sentence separator (appears right after `。`/`？`).
- Other control bytes seen: `0x02 0x03 0x05 0x13 0x18` (formatting / more substitution banks — TBD).

## What this means for translation

Reinsertion must: (1) enumerate every JP string via these 4+ tables; (2) **decode the
abbreviation system** (`0x0e`/`0x1e` + index → expansion) so each string's full meaning is known
and so English can be re-encoded (with or without abbreviations); (3) repoint after length
changes (English ≠ JP length) — a relocated string pool + rewritten pointer tables (composable,
per release-bundle-conventions). English source is on hand (ZIL + Masterpieces `.DAT`), so this
is matching + re-encoding, not translating.

## Next steps
1. Enumerate all tables fully (count total unique strings; find any tables beyond these 4).
2. **Crack the abbreviation dictionary** (`0x0e`/`0x1e` banks) — decode a known room (e.g. "West
   of House") end-to-end and cross-check against the English original to confirm the codec.
3. Build `zork_strings.py`: (table, index, offset, decoded-bytes) for every string.
4. Then: scope decision (Tier 1 display-only vs Tier 2 full playable) + fullwidth/halfwidth fit.

## ABBREVIATION CODEC CRACKED + VERIFIED (2026-06-20)

- **`0x0e XX` / `0x1e XX` → expand to word #XX from the 234-entry table @`0x06099a68`** (the
  word/object dictionary). Confirmed: `<0e:21>`=白い家(white house), `<0e:4a>`=ドア(door),
  `<0e:1c>`=板(board). Expansion is recursive (dictionary entries may contain codes).
- **Verified end-to-end:** 98-table[13] (`0x0608f074` table) decodes to
  *"[You]は、白い家の北側に / こちら側にはドアがなく、壁の窓にはすべて板が打ちつけられて /
  北には狭い小道が木々をぬって伸びて…"* = a perfect match to Zork's **"North of House"**
  ("You are facing the north side of a white house. There is no door here, and all the windows
  are boarded up. To the north a narrow path winds through the trees.").
- Control bytes: **`0x0f`** = "You"/player; **`0x01`** = clause separator; **`0x0c`** = paragraph
  break; **`0x1c`** = newline. **Strings terminate on a low control byte (≈`0x03`), not `0x00`**
  (the room block is one big run; the pointer table indexes each entry's start).

Decoder prototype lives inline (recursive abbrev expansion via the 234-table). The 234-table is
both the in-game vocabulary AND the text-compression dictionary — translating those 234 words to
English keeps the compression and shrinks the reinsertion job.

## Next steps
1. Identify the exact inter-string terminator byte; build `zork_strings.py` to enumerate every
   string (table, index, start, decoded text, end).
2. Decode all rooms/messages; auto-align to the English original (ZIL/Masterpieces) by game order.
3. Scope decision (Tier 1 vs Tier 2) + fullwidth/halfwidth fit; then re-encode English (reuse the
   234-dict in English) + repoint.

## ENUMERATOR BUILT (2026-06-20)
`analysis/zork_strings.py` → `analysis/zork_strings_dump.txt`. Reads all 4 tables, decodes each
entry with recursive abbrev expansion; string ends = next-higher pointer in the combined sorted
target set (prose is contiguous; pointer table delimits). **1142 entries:** rooms98=98,
tbl33=33, dict234=234, msg777=777. Decoding is for alignment; reinsertion will relocate+repoint
so exact terminators don't matter. North-of-House etc. decode clean & bounded.

## Reinsertion strategy (decided)
**Relocate + repoint**, not in-place edit: write re-encoded English strings into a fresh pool
(free space in 0ZORK.BIN, TBD), rewrite each pointer-table entry to point at its new string.
Old string boundaries irrelevant. The 234-dict words get English equivalents too (keeps the
abbreviation compression and shrinks the English footprint).

## Next steps
1. **Align JP entries → English** (ZIL source `zork1/` or Masterpieces `.DAT`) by table-index /
   game order; spot-check with decoded JP (room names, key nouns).
2. Scope decision: **Tier 1** (display text only; parser stays JP) vs **Tier 2** (also convert
   ZVOCTBL/SYNTBL/VERBNOMI + kana input → English = fully playable).
3. Fullwidth vs halfwidth fit; find a free pool in 0ZORK.BIN for the English strings.
4. Re-encode English (reuse 234-dict in English) + repoint + rebuild track + EDC/ECC.

## TIER-1 PIPELINE PROVEN IN-GAME (2026-06-20)
Test disc (`analysis/zork_make_disc.py` → `game_patched/zork1_tier1_test/`) repointed 3 dict
words to English in free padding (`0x060a0214`). **In-game result: WHITE/HOUSE/DOOR/BOARD all
render** in the West-of-House description — **relocate + repoint + ASCII render all work.**
- Single English words render clean.
- **ASCII space `0x20` is mangled** by the full-width renderer → use **full-width space
  `0x81 0x40`** between English words. (Fixed + CONFIRMED in-game: "WHITE HOUSE" renders clean.)
- Renderer terminates strings on `0x00`; English strings are NUL-terminated in the pool.
- Disc note: Mednafen rejects slashed cue paths → builder hardlinks audio tracks into the
  output folder + bare-filename cue.

**=> Tier-1 is a green light.** Remaining = scale up: extract English (ZIL/Masterpieces),
align to all ~900 entries + 234 dict words, re-encode (full-width spaces; reuse 234-dict),
write to a real pool (96 KB free run @`0x0605e984`), repoint, rebuild.

## SCALE-UP: alignment is content-based (2026-06-20)
- Renderer letter conversion is **formula-based** (table @`0x8b024` is punctuation only). Uppercase
  A–Z confirmed in-game; **lowercase a–z does NOT render** (user-tested) → write all English in **UPPERCASE** (period-appropriate for Zork).
- **Saturn 234-dict ≠ ZIL object order.** It's a custom *compression dictionary*: object names
  (`白い家`/`ドア`/`板` = white house/door/board) MIXED with frequent phrases (`ありがとうございます。`,
  `ください。`, `何も起こり…`). So NO index-alignment to the ZIL/source.
- English source on hand: ZIL `zork1/1dungeon.zil` (object DESC/LDESC), `1actions.zil` (messages),
  + Masterpieces `.DAT`. Matching is **by content/meaning** (the Saturn JP is a translation of
  English Zork, so each string has an English counterpart). Anchors: room titles (short DESC like
  `家の西`=West of House) and identifiable nouns.

### MORE TABLES EXIST (2026-06-20)
West-of-House body (`…の西の野原に立って…正面に見える…塞がれて…`, seen in-game) is in **NONE**
of the 4 mapped tables (rooms98/tbl33/dict234/msg777) — searching all of them fully-expanded for
`野原`/`正面`/`塞が` returns nothing. So additional string pointer tables (or a different
storage/region) hold some rooms incl. the opening West-of-House. **Next: re-scan for all pointer
tables** (the first scan only kept runs ≥16 into `0x0608c000`–`0x0609e000`; widen range + lower
threshold + check other regions). The translation framework (`zork_translate.py`) is table-agnostic
— just add the table to `TBL` and entries to the map once found. rooms98[13]/[15] (North/South of
House) ARE translated and should render English (confirm by walking north/south from start).

### POOL SOLVED — ISO-extend works in-game (2026-06-20) 🎉
`analysis/zork_make_disc_ext.py` appends an EXTENDED copy of 0ZORK.BIN (original + 128 KB pool)
at the end of track 1 (lba 23878) and repoints its root-dir entry (LBA+size, LE&BE) — only
~377 sectors written + dir patch + re-ECC of dir sectors. **CONFIRMED in-game: boots and renders
English** → the BIOS loads the first file by its directory entry, so the relocated/extended copy
(with the pool) is what runs. The original lba-22 copy is orphaned (harmless).
**=> The pool problem is solved. The full Zork translation pipeline is complete & validated:
encoder (UPPERCASE, full-width spaces) → map → relocate+repoint → 128 KB pool → disc+ECC → boots.
Remaining work is purely FILLING THE MAP** (match ~900 msgs + 234 dict words to the English source
in `zork_translate.py`; pool is expandable via POOL_SIZE). Use `zork_make_disc_ext.py` as the
canonical builder. (Audio LBAs shift since track 1 grew — fine for text; revisit if CD-audio
playback matters.)

### ISO-REBUILD FEASIBILITY CONFIRMED (2026-06-20) — viable
- **Loader uses directory size:** Zork IP `1st read addr=0x06004000, 1st read size=0` → BIOS loads
  the whole first file (0ZORK.BIN) by its dir size. Grow the dir size → more RAM mapped at
  end-of-image (proven-safe pool class).
- **ISO is FLAT:** 66 files, 0 subdirs; root dir @lba 20 (size 4096). Only root dir + path table.
- **End slack:** last file /OVER.CGL ends @lba 23728; track = 23878 sectors → ~150 free sectors.
  Shift files by N≤150 sectors without resizing the track. Need ~40 (80 KB).
- **Rebuild steps:** (1) insert N sectors after 0ZORK (lba 334) = English pool; (2) shift all
  files lba≥335 by +N (into end slack); (3) root-dir: 0ZORK size += N*2048, each shifted file
  LBA += N (LE+BE fields), fix path table; (4) rewrite moved sectors' header MSF + EDC/ECC.
- Then full translation packs into the new end-of-image pool. **= the next implementation task.**

### POOL: only end-of-image is safe; full translation needs an ISO rebuild (2026-06-20)
Tested three pool locations:
- `0x9c214` end-of-image padding (~736 B) — **BOOTS + renders** (nothing references it). SAFE.
- `0x5a984` 96 KB mid-image zero run — **BLACK SCREEN** (not loaded / used at runtime).
- `0x88000` JP prose region — **NO BOOT** (prose holds the dict-word strings that abbreviations
  `0x0e/0x1e` still reference; corrupting them hangs the recursive expander).
**=> The only safe write space is end-of-image padding.** For the full ~80 KB of English, the fix
is to **extend 0ZORK.BIN** so the loader maps more end-of-image RAM. No ISO slack (ACTLOGO.CGZ at
lba 335, right after 0ZORK's lba 22–334), so this needs a **full track rebuild**: shift all files
after 0ZORK by +N sectors, update their directory + path-table LBAs, grow 0ZORK's size by N*2048,
re-ECC the shifted sectors. Then English packs into the new end-of-image pool (proven-safe class).
Also must confirm the loader sizes the load from the directory entry (vs hardcoded) — if hardcoded,
patch the load count too. **This is the next engineering task; the translation framework is done.**

### POOL CONSTRAINT (2026-06-20)
- **Safe pool = end-of-image padding `0x9c214`–`0x9c4f4` (~736 B only)** — proven (dict + 4 rooms boot/render).
- **The 96 KB mid-image zero run `0x5a984` is NOT free → BLACK SCREEN** (used at runtime or excluded
  from load). Do **not** use mid-image zero runs.
- **=> The full translation needs a bigger safe pool.** Design task: (a) check ISO for slack sectors
  after 0ZORK.BIN (lba 22–334) and **extend 0ZORK.BIN's directory size** to load them as fresh
  end-of-image RAM; or (b) overlay a genuinely-unused loaded region. Builder now packs-what-fits
  + warns (`SKIP`) so the map can hold everything while the small pool uses a subset.

### Scale-up plan (incremental, recommended)
1. Translate the **opening area first** (West/North/South of House, Forest, mailbox/leaflet,
   key objects) end-to-end: decode JP → match English source → re-encode (full-width spaces,
   reuse/extend dict) → write to the 96 KB pool @`0x0605e984` → repoint rooms98/dict/msg777
   entries → rebuild → playable English demo of the opening.
2. Expand outward area-by-area; QA each on emulator.
3. Decide Tier-1 (display only) vs Tier-2 (parser) once the demo proves the full
   room+message re-encode (not just dict words).

This is the **labor phase** (the spec's predicted cost = volume, ~900 msgs + 234 dict). The
engineering is solved; remaining is matching + re-encoding, doable incrementally with a
shippable demo at each step.


### MENU RENDERING (2026-06-20): full-width, NOT ASCII
The Action menu (mode labels @0x20fb0 + the sequential verb list @0x21fb4) renders raw bytes
through the font as 2-byte SJIS — it does NOT use the room/dialogue ASCII->full-width converter.
So menu English must be **full-width SJIS** (A=0x8260, space=0x8140), and it must fit the JP
**character** count (2 bytes/char), in-place. 2-char JP verbs can't fit (need relocation, deferred).
Done in-place: 43 verbs + 4 mode labels (ACT/LETTER/VERBS/MEMO). The 4 persistent tab BUTTONS are
still graphics (separate tile repaint).

## Status: TIER-1 PROVEN. Alignment=content-based. Scale-up = incremental (opening area first).
