# Zork I (Saturn) — full-playable-English scale-up SPEC

**Date:** 2026-07-03. **Goal:** finish the fan translation to a **fully playable English**
build — every command class (verbs, nouns, directions, prepositions) translates and all prose
renders in English, verified through a complete playthrough on emulator + hardware.

**Vehicle:** the proven **hybrid engflag build** — `game_patched/zork1_engflag_full/`, built by
`analysis/zork_translate_engflag.py`. English display via the engflag prose path + English input
via the EN→JP shim → the game's stock Japanese tokenizer/parser executes. The parser is never
touched; all command work is **data + regenerate** through `analysis/zork_shim.py` and
`analysis/zork_data/picker_en2jp.py`. This spec finishes that build; it does not open a new path.

Prior context: `docs/ZORK1_PICKER_VERB_FIX_HANDOFF.md` (leaflet RESOLVED),
`docs/ZORK1_TRANSLATION_SPEC.md`, `docs/ZORK1_STRINGS_RECON.md`,
`docs/ZORK1_PARSER_DICT_AUDIT.md`, and `memory/zork-command-box-display-parse.md`.

---

## Confirmed baseline (what already works — do NOT re-litigate)

- **Shim pipeline is correct.** English input (keyboard *and* the room-text word-picker) →
  SH-2 shim at file offset `0x4150c` (repointed TRANSCODE literal, was `0x0604608a`) rewrites
  the ASCII command into a Japanese reading (SJIS) → stock JP tokenizer/ZVOCTBL match → execute.
- **Two-stage shim** (`zork_shim.py`): Stage 1 = whole-command **PHRASE** table (keyboard SVO,
  e.g. `open mailbox`→`郵便箱を開ける`); Stage 2 = **WORD** table, split-on-space word-by-word
  (picker SOV). Particles carry an empty reading; nouns bake in their object particle `を`.
  Unknown word → tail-jump to the real TRANSCODE (safe fallback).
- **`open mailbox` → `take leaflet` → `read`/`examine leaflet` all execute; leaflet WELCOME TO
  ZORK text displays.** The earlier "regression" was a noun-mapping bug, now fixed:
  `leaflet`→`ぱんふれっと` (id `0x01f9`, the id the in-scope object binds).
- **English prose display** renders via the engflag path (`glyph = font[byte+0x1F]`, ASCII 8×16
  font from INIT2.SLD/SINIT2.SLD, bit-reversed to un-mirror): lowercase a–z + O–Z caps + period.
- EN→JP-reading data largely exists: `zork_en2reading.py` (canonical resolver, `DIRECTIONS`,
  `SYNONYMS`), `picker_en2jp.py` (NOUNS/VERBS/PARTICLES), `docs/ZORK1_PARSER_DICT_AUDIT.md`
  (331/333 English words already have an accepted ZVOCTBL reading).

---

## Milestone 1 — Navigation (PRIORITY, ships first)

**Symptom:** `GO WEST/NORTH/EAST/SOUTH` do not work.

**Root cause:** the shim has no direction concept. `north/south/east` are absent from the maps;
`west`→`西` is mis-typed as a **noun** (so it wrongly gets a baked `を`); `go`→`行く` is a verb.
Word-by-word, `go west` → `行く西を`, which the parser rejects. Zork's parser expects a **bare
direction reading** (ZVOCTBL flag `0x10`) with the movement verb **elided** — e.g. `go north`
must become just `きた`. The correct readings are already validated in `zork_en2reading.DIRECTIONS`.

**Direction readings (validated against ZVOCTBL):**
`west→にし`, `north→きた`, `east→ひがし`, `south→みなみ`, `up→うえ`, `down→した`,
`northeast→ほくとう`, `northwest→ほくせい`, `southeast→なんとう`, `southwest→なんせい`,
plus `in`/`out` (enter/exit movement — see fork below). Confirm each exact reading against
`zork_en2reading.DIRECTIONS` / the ZVOCTBL audit at build time; do not hand-transcribe.

**Design (data-driven; no new SH-2 assembly):**
1. Add a **DIRECTIONS map** to `picker_en2jp.py`: full compass + `up/down` + short forms
   (`n/s/e/w/ne/nw/se/sw/u/d`) → bare kana reading, **no `を` particle**.
2. Make **movement verbs** (`go/walk/run/head/proceed`) emit **empty** in the shim word-path
   (same mechanism as particles), so `go west`, `west`, `w`, and picker order `west go` all
   collapse to `にし`.
3. Add whole-command **PHRASE** entries (`go west`→`にし`, `go north`→`きた`, …, and the bare
   forms) so the keyboard SVO path matches directly.
4. **Remove `west`→`西` from NOUNS** (it is a direction, not an object).

The existing two-stage matcher already supports empty-emitting tokens, so this is purely data +
`zork_shim.py` regenerate. Extend the `zork_shim.py` self-test with every direction case
(`go west`, `west`, `w`, `go north`, … → expected kana, note=None).

**Fork resolved (user-approved):** elide **only bare `go`/`walk`/`run` + a compass direction**.
Keep `enter`/`exit`/`go in`/`go out` as their existing verbs (`入る`/`出る`) — do not fold them
into the direction-elision rule.

**Verify:** on emulator, each direction actually **changes the room** (West-of-House → North/
South-of-House → Forest, etc.), not just prints a message. Commit after nav is confirmed.

---

## Milestone 2 — Prose character-spacing (PRIORITY, ships second)

**Symptom (user-confirmed):** in **story prose only**, every character has an empty space after
it. The **command-input line is fine** (tightly spaced).

**Root cause:** the engflag **prose** renderer (`glyph = font[byte+0x1F]`, ASCII 8×16 font)
inherits a **fullwidth 16px per-character advance** from the original Japanese layout, but the
Latin glyph art is only 8px wide → an 8px gap after every letter. The command line looks correct
because it uses the **menu** draw path (`FUN_0x0600f520` / `FUN_0x0600eda8`), which already
advances at the halfwidth 8px stride. The two paths differ only in horizontal advance.

**Design:** reduce the prose path's per-character horizontal advance from fullwidth (16px) to
halfwidth (8px, i.e. the glyph width). Because the prose draw routine is **not yet pinned in
static disassembly** (it is the separate +0x1F path, distinct from the located `eda8`/`ef00`/
`f520` menu drawers), this milestone has one genuine RE step:

1. **Locate** the advance: Mednafen PC-breakpoint on the prose glyph draw while "west of house"
   renders (or read-watchpoint on the prose pool string, then step to the draw); identify the
   instruction that advances the X cursor and its stride (immediate or register).
2. **Patch** that stride 16→8 (or to glyph width) in `0ZORK.BIN` via the build script.
3. **Verify** prose reads tightly ("WEST OF HOUSE", not "W E S T…") and the command line is
   unaffected.

**Regression safety:** this path is **flag=1 (engflag) English-only**, so JP fullwidth rendering
cannot regress. If the advance turns out to be shared with a JP path, gate the patch behind the
engflag flag or the ASCII-glyph range instead. Commit after confirmed.

---

## Milestone 3 — Full command coverage

Fold every verb / noun / adjective / synonym into the shim tables:
- Source from `zork_en2reading.py` (`build_canon()`, 333 entries, ZVOCTBL-validated) +
  `picker_en2jp.py`. Add a **synonym layer** (get/grab/pick-up→take, x→examine, kill→attack,
  look/l, examine/x, etc.) mapping each expected English input to the one canonical JP reading.
- **Budget watch:** monitor combined PHRASE+WORD table size against the free-pool allocation in
  `zork_translate_engflag.py` (`POOL_SIZE`); common verbs stay early in the WORD table so
  gap-size truncation never drops them (existing `COMMON_VERBS` mechanism).
- Extend the `zork_shim.py` self-test with representative commands per class.

## Milestone 4 — Prepositional grammar (SYNTBL)

The only un-started RE pass. Two-object / prepositional commands ("put X in Y", "attack troll
with sword", "unlock grating with key") resolve via **SYNTBL.DAT** (syntax patterns), **not** the
dictionary — particles (`に`/`で`/`から`/`の中`…) are absent from ZVOCTBL. Analyze `SYNTBL.DAT`
structure, then teach the shim the correct particle insertion and two-object ordering so the JP
string it emits matches a syntax pattern the parser accepts. Largest remaining unknown; scope its
own sub-investigation. Plain verb+noun and verb+direction commands need none of this and already
work.

## Milestone 5 — Full prose coverage

Fill `analysis/zork_data/*.py` (dict234 / rooms98 / msg777 / loose, ~1142 pointer-table entries)
with English, matched from the on-disc English source (Infocom ZIL `zork1/` + Masterpieces
`ZORK1.DAT`) by content/meaning (the Saturn 234-dict is a compression dictionary, **not** ZIL
order — align by room titles + identifiable nouns). Untranslated entries currently keep JP
pointers and **garble** on the +0x1F path, so this is correctness, not just polish. Encode per the
engflag prose charset (lowercase = char−0x1F, space, newline; period painted). Relocate + repoint
into the extended-image pool (already solved: `zork_make_disc_ext.py` / the engflag builder).

## Milestone 6 — Full-playthrough QA

Play through on Mednafen (or Yaba Sanshiro), then real hardware (Fenrir / Saroo / Terraonion
MODE). Recompute EDC/ECC with `ecc.py` (MODE requires it). Fix coverage gaps found in play.

## Milestone 7 — Distribution

Ship `.ssp` (changed `0ZORK.BIN` only) as the primary format, plus xdelta + IPS + readme. Credit
**Suinevere Pendragon**. Raw image must match the original for xdelta/IPS.

---

## Parked polish (non-blocking; fold into M5/M6)

- **`take`-ack "taken." never draws.** The ack (`取り`+ctrl → `取りました。`, source
  `0x06099143`) is transcoded into a buffer but not drawn to screen — a verb-engine/display-path
  issue rendered from LWRAM, **not** a string fix (documented dead-end in the handoff). Take
  itself works (leaflet enters inventory). Revisit only by redirecting that render; possibly the
  picker path overwrites the response area (untested: whether a keyboard `take` shows the ack).
- **lowercase `z` renders as `m`** ("world of mork"). Source is correct; the engflag font's `z`
  glyph is mis-drawn. Fix by repainting the `z` slot in `patch_sld_font`
  (`zork_translate_engflag.py`), like the existing `.`-glyph repaint.

---

## Verification norm (per CLAUDE.md)

Ground-truth every step against actual game behavior (room changes; text reads correctly) on
emulator/hardware — not encoder self-consistency (a self-verifying xdelta only proves round-trip).
Commit after each verified step for regression safety. Confirm the shim self-test passes before
each build.

## Key addresses / files (reference)

- Build: `analysis/zork_translate_engflag.py` (`build_patched_0zork`), BASE `0x06004000`.
- Shim: `analysis/zork_shim.py`; TRANSCODE `0x0604608a`; repoint literal file offset `0x4150c`.
- Command data: `analysis/zork_data/picker_en2jp.py` (NOUNS/VERBS/PARTICLES — add DIRECTIONS),
  `analysis/zork_en2reading.py` (`DIRECTIONS`, `SYNONYMS`, `build_canon`).
- Prose data: `analysis/zork_data/{dict_words,messages,rooms,...}.py`.
- Disc builder: `analysis/zork_make_disc_ext.py`. ECC: `saturn_translate/ecc.py`.
- Font patch: `patch_sld_font` in `zork_translate_engflag.py` (INIT2.SLD/SINIT2.SLD, 8×16).
- Savestate RE helper: `analysis/zork_savestate.py` (`load_ram(path,"WorkRAMH")`). SH-2 disasm:
  `saturn_translate/sh2.py`.

## Sequencing

M1 (nav) → M2 (spacing) as independent shippable commits, then M3 → M4 → M5 → M6 → M7.
(User-approved order; nav before spacing despite spacing being the larger RE unknown.)
