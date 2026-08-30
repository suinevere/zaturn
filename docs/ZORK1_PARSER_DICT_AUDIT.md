# Zork I (Saturn JP) — parser dictionary (EN→JP-reading) coverage audit

Goal: assess the English→Japanese-reading dictionary needed to feed the resident Z-machine parser
(typed/keyboard input or a menu translation shim). The parser is a black box in the resident module
(0x06000000–0x06003fff); we only need to hand it **Japanese readings it already accepts**, which live
in `ZVOCTBL.DAT`. See `memory/zork-command-box-display-parse.md`.

## ZVOCTBL structure
Fixed 23-byte records from offset `0x3da`: `[id u16-LE][POS u8][SJIS keyword ≤20B, NUL-pad]`.
POS flags: `0x80` noun, `0x20` adj, `0x02` verb, `0x40` verb-te(connective), `0x10` direction,
`0x01` misc (ALL/every + some verbs/dirs), plus sentinel/unused slots (`0xffff`, empty `0x00`).
Parser: `analysis/zork_zvoctbl_patch.py` `records()`. Data dump: `analysis/zork_zvoctbl_audit.csv`.

## Headline result — EN→JP coverage is effectively complete
Of **333 distinct English words** in our existing maps (`zork_data/dict_words.py` READING_EN-side +
`zork_data/verbs.py` VERBS), **331 already have ≥1 working ZVOCTBL reading** the parser accepts.
The 2 apparent misses (HIT, SHOUT) are false — their readings (e.g. 打つ=HIT) sit under flag `0x01`,
outside the 5 main classes the first pass scanned. So for single verb/noun/adjective/direction words,
the dictionary we need is essentially **already built**.

The JP→EN audit looked alarming (973 "gaps" of 1715 readings) but that over-counts: ZVOCTBL stores
many **redundant forms** of each word — kanji form, all-hiragana form, te-form conjugation, synonyms —
each as its own record/id. For *input* we only emit ONE accepted reading per English word, so those
variants don't need covering. (Verb synonym readings do NOT share an id, so id-propagation can't
collapse them — confirming they're distinct records, not a coverage problem.)

Per-class JP→EN (informational; "covered" = reading present in our maps, direct + id-propagated):
| class | covered/total | note |
|------|------|------|
| noun (0x80) | 413/487 | remaining = rare synonym readings |
| adj (0x20) | 171/224 | |
| verb (0x02) | 80/473 | the 393 "gaps" are hiragana/te variants of the 181 known verbs |
| verb-te (0x40) | 75/507 | connective forms; not needed for input |
| direction (0x10) | 3/24 | English abbrevs (N/S/E/W/U/D…) already IN the table |

## What remains for full typed/keyboard input (not blocking; well-scoped)
1. **English synonym layer** — players type get/grab/pick-up for TAKE, etc. Map each expected English
   synonym to the one canonical JP reading. Pure content; the JP targets already exist.
2. **Directions** — finalize EN→reading for N/S/E/W/NE/NW/SE/SW/UP/DOWN/IN/OUT (readings present:
   ひがし/にし/うえ/した/ほくとう…; English abbrevs already in the table).
3. **Prepositions / multi-word commands** — particles (に/で/から/の中…) are **absent from ZVOCTBL**;
   the Z-machine resolves "put X in Y" via the **syntax table SYNTBL.DAT**, not the dictionary. So
   prepositional commands need a separate SYNTBL analysis. Plain verb+noun ("take lamp", "open door")
   need none and work through the existing path.

## Conclusion
On-the-fly English→JP translation for the parser is highly feasible: the JP vocabulary is already
mapped. Next concrete step is to fold the maps into a single canonical EN→JP-reading table (one
reading per English word) + the synonym layer, then (later) the SYNTBL pass for prepositional grammar.

## Resolver built — `analysis/zork_en2reading.py`
Canonical EN→JP-reading resolver (the foundation for the menu shim AND keyboard input):
- `build_canon()` auto-derives 333 English→reading entries from READING_EN + VERBS ∩ ZVOCTBL (verbs
  use the canonical dict-form; nouns/adj the normalized kana, FORCE_ID-honored). Never drifts.
- `DIRECTIONS` (compass incl. NE/NW/SE/SW, ENTER/EXIT) and `SYNONYMS` (get/grab→TAKE, x→EXAMINE,
  kill→ATTACK, n/s/e/w abbrevs, …) — all targets validated against ZVOCTBL.
- `translate_command("open the mailbox")` → `開ける ゆうびんばこ`; `"go north"`→`きた` (movement verb
  elided before a direction); articles dropped. Verb+noun+direction commands resolve fully.
- Remaining unknowns are prepositions (WITH/IN/ON…) — they are NOT dictionary words; handled by
  SYNTBL.DAT syntax patterns (next analysis).
