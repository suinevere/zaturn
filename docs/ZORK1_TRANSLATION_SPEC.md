# Zork I (Saturn, Japan) — English translation SPEC

**Game:** Zork I: The Great Underground Empire (Japan), Activision/SystemSoft, 1996.
Product **T-21502G**, area **J**, peripherals **`J`** (digital pad only), date 19960205.
A *multimedia/talking* edition: data track + 31 CDDA audio tracks + Cinepak FMV.

**Goal:** Japanese → English translation. The English text already exists (see Sources),
so this is **reinsertion + matching + reformatting**, not translation-from-scratch — the
same class of work as the Steamgear Mash text patch, but at much larger volume.

---

## 1. Disc / asset map (`cd/Zork I … (Japan)/`)

- **Track 01** — MODE1/2352 data track (56 MB). Holds the ISO9660 filesystem (66 files).
- **Tracks 02–32** — CDDA AUDIO (redbook). Likely music/ambience and possibly Japanese
  narration. **Out of scope** for the text translation (cf. Return to Zork = the audio job).
- `.cue` present; standard 2-track-style layout extended with 30 audio tracks.

### Files that matter (Track 01)
| File | Size | Role |
|------|------|------|
| **`/0ZORK.BIN`** | 640 KB | **Main program + engine + ALL game text.** Loads at `0x06004000`. |
| `/SJIS.CGD` | 264 KB | Shift-JIS glyph font. **Confirmed contains halfwidth Latin glyphs.** |
| `/ZVOCTBL.DAT` | 47 KB | Z vocabulary table (parser dictionary). |
| `/SYNTBL.DAT` | 7.7 KB | Syntax table (parser grammar). |
| `/VERBNOMI.DAT` | 10 KB | Verb/noun table. |
| `/GAME.DAT` | 5.8 KB | Small runtime data (save template / globals?). |
| `*.TPG` `*.CGL` `*.CGZ` `*.CGD` | — | Pre-rendered room-transition / scene graphics. |
| `*.SLD` | — | Slideshow sequences (intro/staff). |
| `*.CPK` (OPENING/TITLE/TYPE) | 33/1.5/3.5 MB | Cinepak FMV. |
| `SE*.BIN` | — | Per-area scene/sound resource blobs. |

Extracted to `work/zork1/`: `0ZORK.BIN`, `SJIS.CGD`, `GAME.DAT`, `ZVOCTBL.DAT`,
`SYNTBL.DAT`, `VERBNOMI.DAT`.

---

## 2. Engine & text format (reverse-engineered facts)

- **Z-machine-derived engine.** The Infocom parser tables (`ZVOCTBL`/`SYNTBL`/`VERBNOMI`)
  confirm direct Zork lineage, but it is **not a stock Z-machine** — it was reimplemented
  for Saturn with **Shift-JIS text** instead of packed 5-bit ZSCII.
- **Text lives in `0ZORK.BIN`.** ~11k candidate SJIS strings; the real prose is concentrated:
  - **Main block ≈ `0x88000`–`0x9C000`** (~80 KB, near-saturated Japanese).
  - Scattered strings across **`0xE000`–`0x58000`** (mixed with code/data).
- **Encoding = plain Shift-JIS** double-byte (kana + kanji + fullwidth digits, e.g. `５６９ページ`).
  Verified coherent prose, e.g. *"…して頭を使わないのですか？たぶん西の方でしょう。"*
- **Inline substitution tokens:** single-byte ASCII letters embedded in the stream (e.g. `J`,
  `d`) are **variable/object-name placeholders** the engine expands at print time (Z-machine
  "print object" mechanic). Example: `Jは燃やせ…Jは傷つけられ…` (`J` = an object's name).
- **Control codes:** `0x03`, `0x04`, and the pair `0x1c 0e` act as string separators /
  terminators / formatting markers between sentences.
- **Pointer/index structure:** a regular table precedes the main text block (~`0x88000`);
  the engine indexes strings via pointers (TBD — must be mapped, see §5).

---

## 3. English sources on hand

1. **ZIL source** — `cd/…/zork1/` (released Infocom `historicalsource`):
   `1actions.zil`, `1dungeon.zil`, `gverbs.zil`, etc. Canonical, human-readable, includes
   the exact object-name substitution grammar. Best for *meaning + token mapping*.
2. **Compiled English story** — `InfocomMasterpieces.img` (12.5 MB, custom FS; file table
   ~`0x19400` references `ZORK`/`.DAT`). The packed-ZSCII `ZORK1.DAT` can be decoded to a
   flat, in-order list of every printable string with standard Z-machine tools
   (`infodump`/`txd`/ZILF). Best for *bulk string extraction in engine order*.

Having both lets us cross-check: extract English strings from the `.DAT`, align to the ZIL
source for token/grammar fidelity, then match each to its Saturn JP string.

---

## 4. Scope decision (MUST choose before building)

The single biggest scoping fork:

- **Tier 1 — Display text only.** Translate the ~thousands of SJIS output strings to English.
  The parser still expects **Japanese input** (`ZVOCTBL`/`SYNTBL` untouched), so the player
  reads English but must still type Japanese commands. Lower effort; **not truly playable**
  for an English speaker. Useful as a milestone / proof of pipeline.
- **Tier 2 — Full playable English.** Tier 1 **plus** converting the parser: rewrite
  `ZVOCTBL` (vocabulary), `SYNTBL` (syntax), `VERBNOMI` (verb/noun) to accept English words,
  and rework the **Japanese text-input UI** (kana entry) into an English/ASCII entry method.
  Much larger; this is what makes it a real English release.

Recommendation: build the **Tier-1 pipeline first** (proves extraction→match→reinsert→render),
then decide whether to fund Tier-2 parser work. Most of the Tier-1 tooling is reusable.

---

## 5. Open RE questions (resolve during build)

1. **String table / pointer format** in `0ZORK.BIN`: how strings are indexed (the table at
   ~`0x88000`), so we can repoint after length changes. *Fast path: Mednafen read-watchpoint
   on the table when a room description prints; trace the indexing routine.*
2. **ASCII-render gate — RESOLVED (passes), 2026-06-19.** The text-stream processor at
   **`0x06046000`** (file 0x42000 region) accepts **single-byte ASCII and converts it to
   fullwidth Shift-JIS on the fly**, then renders it. Proof:
   - Hardcoded map e.g. ASCII `0x3f` (`?`) → emits `0x81 0x48` (`？`) at `0x060461e8`.
   - Table-driven map at **`0x0608f024`** (file 0x8b024): index 0 = ASCII `0x21` (`!`) →
     `0x8149` (`！`); then `"`→`”`, `#`→`＃`, `$`→`＄`, `%`→`％`, `&`→`＆`… (the standard
     ASCII→fullwidth run). Sibling tables at `0x0608f05e`/`0x0608f06a` cover the
     `[ \ ] ^ _ ` / `{ | } ~` ranges; A–Z/a–z handled by formula.
   - Same routine dispatches the stored-string control codes (`0x1c 0e`, `0x03`, `0x04`)
     and space (`0x20`) → it is the main text path.
   - **Conclusion:** English inserted as ASCII WILL display. **Caveat:** it renders
     **fullwidth** (each char = full-width cell), which **halves per-line capacity** — a real
     fit problem for verbose English. **Mitigation to investigate:** `SJIS.CGD` also contains
     **halfwidth Latin glyphs**, and Shift-JIS halfwidth (single-byte `0xa1–0xdf` / direct
     ASCII) is a plausible alternate path; finding/enabling a halfwidth render mode would
     roughly double the fit budget. Not a feasibility blocker either way.
3. **Token table:** the mapping of substitution letters (`J`, `d`, …) → object/global indices,
   so translated strings keep correct substitutions.
4. **Control-code semantics:** exact meaning of `0x03`, `0x04`, `0x1c 0e` (newline / end /
   style) to preserve formatting.
5. **Whether the CDDA tracks carry Japanese voice narration** (would make Tier-2 feel
   incomplete without English audio — but audio is explicitly a separate project).

---

## 6. Build plan (Tier 1)

1. **Map the string table** (RE question 1) → `zork_strings.py`: enumerate (index, offset,
   bytes) for every JP string.
2. **Extract English** from `ZORK1.DAT` (and/or ZIL) into an ordered string list.
3. **Align JP↔EN** strings (by game order + heuristic anchors; the engine's string order
   should track the source). Produce an editable bilingual table (JSON/CSV).
4. ~~Confirm ASCII rendering~~ **DONE** (gate passes, §5.2). Build step instead:
   **decide fullwidth vs. halfwidth render path** (fit budget) via a one-string test.
5. **Reinsert** English: write a **relocated string pool** in free space + **repoint** the
   pointer table (composable/disjoint, per `release-bundle-conventions`), reusing
   `reinsert.py`/`pointers.py`. Preserve tokens + control codes.
6. **Rebuild** Track 01 with patched `0ZORK.BIN`; **recompute EDC/ECC** (`ecc.py`) for
   Terraonion MODE.
7. **Test** on Yaba Sanshiro / Mednafen, then hardware (Fenrir/Saroo/MODE).
8. **Distribute**: `.ssp` (changed `0ZORK.BIN`) + xdelta + IPS + readme; credit
   **Suinevere Pendragon**.

## 7. Effort & risk

- **Reuses:** Steamgear text-RE skill, `reinsert.py`/`pointers.py`/`sjis.py`/`ecc.py`,
  relocated-pool/composable patch method, `.ssp` distribution.
- **Cost drivers:** large text volume (~80 KB+ main block); JP↔EN alignment QA; the
  single-byte-ASCII rendering gate (RE #2); Tier-2 parser rewrite if pursued.
- **Free:** the translation itself (English source on disc) and the font glyphs (Latin
  present). No new translation, no codec cracking, no new font art.
- **Difficulty:** medium — methodology is known; labor + the ASCII-render gate are the risks.

## Status: SPEC COMPLETE. ASCII-render gate RESOLVED (passes, §5.2). Pending: scope
decision (Tier 1 vs 2) and fullwidth-vs-halfwidth fit strategy.
