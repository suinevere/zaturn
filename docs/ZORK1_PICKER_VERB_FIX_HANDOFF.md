# Zork I (Saturn) English noun-picker — verb-vocab fix handoff

## ✅ RESOLVED 2026-07-02d — open/take/read all WORK; leaflet is readable (WELCOME TO ZORK)

Root cause was **none of the handoff's hypotheses** (not verb-vocab, not command-assembly,
not the shim). The pipeline was always correct. The real bug: the shim mapped English
`leaflet` → **`説明書` (ZVOCTBL id `0x0256`)**, a valid word that **no in-scope object carries**,
so the parser answered `ここには、見あたりません` ("you cannot see that here") — which was also
untranslated, so it rendered garbled and looked like a parser error.

Fix = two commits:
1. `feat: translate out-of-scope reply` (`ここには、見あたりません` → "you cannot see that here",
   `responses.py` @`0x0603c470`) — made the failure legible.
2. `fix: leaflet noun -> ぱんふれっと (0x01f9)` (`picker_en2jp.py`) — the id the leaflet object
   actually binds. (`せつめいしょ`=`0x025e` collides with mailbox `ゆうびんばこ`; `説明書`=`0x0256`
   is a valid word but unbound. Mailbox works via its kanji `郵便箱`=`0x025d`.)

User-confirmed: **`open mailbox` → `take leaflet` → `read leaflet`/`examine leaflet` all execute;
the leaflet WELCOME TO ZORK text displays** (screenshots 30–32, savestates hash `fd92e7b5…`
mc1=open / mc2=take / mc3=examine). Shim entry address is now `0x06024e4c` (moved from
`0x06024e44` when the word table grew).

### PARKED cosmetic follow-ups (diagnosed 2026-07-02d, not fixed — user parked)

**P1 — `take` ack "taken." never displays. (deep-dived 2026-07-03 — NOT string-fixable.)**
The real ack source IS a static `0ZORK.BIN` string after all: savestate slot5 (PC breakpoint on
TRANSCODE `0x0604608a` during take) gave **R5=`0x06099143`** = `取り` + ctrl `0x02`(→`ました。`),
ptr `0x602f774`, 5B span to `0x1c`. But it renders via a **DIFFERENT path** than the prose/`not
here` messages: R7 caller is in **LWRAM** (`0x002ee6f2`, the verb engine) and TRANSCODE runs in
**mode 1 = ASCII→full-width** (r6=1). Proof it's not string-fixable: translating `0x06099143`
in place to engflag "took" was mis-encoded by that path into full-width `ＵＰＰＬ` at the render
dest `0x060af16c`, and **it still never reached the screen** (snapshot 33: take response is
empty; every capture shows take's ack blank regardless of content). So the ack is transcoded
into a buffer but **not drawn** — a display-path/verb-engine issue, not a translation one. That
in-place edit was reverted (commit `977e312`). Take itself WORKS (leaflet enters inventory;
examine/read show its text). Possible next lead if revisited: the ack may be suppressed
specifically on the **picker** path (object-list redraw overwrites the response area) — worth
testing whether a KEYBOARD `take leaflet` shows it. Fixing = force/redirect that render.

**P2 — lowercase `z` renders as `m`.** Screenshot 32 shows "world of **m**ork". Source is correct
(`messages.py` msg 479 = "WELCOME TO THE WORLD OF " + token `0xe9`=`ZORK`, `dict_words.py`). The
engflag font's lowercase **`z` glyph is drawn as `m`** — a font-glyph bug (rare letter, missed).
Fix lives in the SLD font patch path (`patch_sld_font` in `zork_translate_engflag.py`), likely
small: remap/repaint the `z` glyph slot like the `.`-glyph fix already there.

**Non-blocking. The picker/leaflet goal is fully achieved; these are polish only.**

---

**Status at handoff (2026-07-02):** picker recognizes/resolves/displays/executes English
nouns in engflag mode. Confirmed working earlier: `open mailbox`, `leaflet in look`,
`leaflet in take` ("taken."). **New regression report:** `leaflet in take` now fails too,
and `leaflet in read` / `leaflet in examine` fail with a garbled error (not WELCOME TO ZORK).
This doc scopes the *right* fix for a fresh session.

---

## 1. What is proven correct (do NOT re-litigate)

- **The two-stage shim's EN→JP mapping is correct.** `analysis/zork_shim.py` self-test and
  the SH-2 simulator both show, for the *exact placed tables*:
  - `leaflet in take` → `説明書を取る`  (note=None = success path, not fallback)
  - `leaflet in look` → `説明書を見る`
  - `leaflet in examine` → `説明書を調べる`
  - `leaflet in read` → `説明書を読む`
  So the shim, particle-baking (`を` onto nouns), and word-by-word transcode are **not** the bug.
- Recognition dict, resolver order (JP table `0x0607c9bc`, mailbox=index 11), box-write
  full-width→ASCII hook — all verified in prior sessions.

## 2. New diagnostic evidence (`leaflet in take`, this session)

Savestates (running image hash `9e9254634e8c24103dad6febf3c8d5d8`):
- `mcs/Zork1 (engflag full).9e92…d5d8.mc1` = slot 1 (before), `.mc2` = slot 2 (after).
- **mc1 and mc2 are byte-identical in the relevant regions → the command never executed.**

Region dump (both slots):
| Addr | Meaning | Contents |
|------|---------|----------|
| `0x060ae080` | picker command box | `4c 45 41 46 4c 45 54 20` = `"LEAFLET "` (noun only, UPPERCASE ASCII, trailing space, rest 0) |
| `0x060ae000` | (paired command box) | all zero |
| `0x060b2dc8` | shim transcode dest | `0x51` then zeros — **garbage, not `説明書…`** |

Reproduce:
```
cd analysis
PYTHONIOENCODING=utf-8 python /tmp/diag_take.py   # see the snippet at bottom
```

**Interpretation.** The shim dest holds `0x51` (a single stray byte), NOT the correct
`説明書を取る` that the simulator produces. That means the shim was **not** invoked on the
string `"leaflet in take"`. The picker deposited only the noun `"LEAFLET"` into a box; the
full multi-word command string never reached the shim's source register (r5) at call time.
So the on-hardware failure is upstream of the shim's EN→JP logic.

---

## UPDATE 2026-07-02b — static re-investigation OVERTURNS §2's hypothesis (A)

New evidence from the SAME savestates (`9e925463…`) + the deterministic rebuild:

1. **Parse buffer `0x060a5a9c` holds the FULL command.** mc2 (the `take` attempt)
   `0x060a5a9c` = `"LEAFLET in take "` (mc1 = the prior `"MAILBOX in open "`). The
   multi-word command **does** reach the parser buffer — **command-assembly is NOT the
   bug. Hypothesis (A) as written in §2 is disproven.**
2. **The `0x51` at `0x060b2dc8` is stale, not the live shim dest** — it is byte-identical
   in mc1 and mc2. §2 misread it as evidence the shim ran on garbage.
3. **Case is handled.** The shim match loop `OR #0x20` case-folds each source byte, so
   uppercase `"LEAFLET"` folds to `"leaflet"` before table lookup.
4. **Shim + placed 188-word table are CORRECT.** Feeding the *exact placed tables pulled
   from the hardware savestate RAM* (routine `0x06024e44`, phrase `0x06024460`, word
   `0x06024488`) through `zork_shim.simulate` gives, note=None (success):
   `LEAFLET in take`→`説明書を取る`, `…look`→`見る`, `…read`→`読む`, `…examine`→`調べる`.
   All needed words (leaflet/take/look/read/examine/open/mailbox) are present in the 188.
   So **truncation is NOT the bug**, and the shim, *if invoked*, yields correct JP.
5. **KEY LEAD — three TRANSCODE callsites, only one repointed.** The original image has
   **3** literals holding TRANSCODE `0x0604608a`: `@0x0604550c`, `@0x060463f8`,
   `@0x06048a48`. The build repoints **only `@0x0604550c`** → shim `0x06024e44` (see
   `zork_translate_engflag.py` `SHIM_LIT_FOFF=0x4150c`). `@0x060463f8` and `@0x06048a48`
   still call the *original* TRANSCODE. (The partial `0ZORK.c` decompile is only a ~28-fn
   excerpt ending at rel `0x4e0c`; all 3 callsites are far beyond it, so no static
   caller resolution.)

**Refined root cause — exactly two possibilities remain:**
- **(A′) The picker-command execute path runs an UN-repointed TRANSCODE callsite**
  (`@0x060463f8` or `@0x06048a48`), bypassing the shim → English `"LEAFLET in take"` hits
  the original TRANSCODE → garbage JP → parser fails. Keyboard `open mailbox` works
  because keyboard input funnels through the repointed `@0x0604550c`. **← leading.**
  Fix would be to repoint the picker's callsite too (place a 2nd shim copy or share one).
- **(B) The shim IS invoked** (picker shares `@0x0604550c`) and the parser rejects the
  JP verb (verb-vocab). Does not cleanly explain the `take` regression.

**Decisive experiment (live, one run):** PC breakpoints at BOTH shim entry `6024e44` and
original TRANSCODE `604608a`; run `leaflet in take` via the picker; see which fires + read
r5/PR. See the Mednafen procedure handed to the user this session. If `6024e44` never
fires but `604608a` does (PR near `463f8`/`48a48`, r5=`"LEAFLET in take"`) → (A′) confirmed.

## UPDATE 2026-07-02c — LIVE BREAKPOINTS: PIPELINE WORKS. Root cause = leaflet OUT OF SCOPE.

User ran the dual-breakpoint test. Both fired; savestates saved (`9e925463…` mc2=shim entry,
mc3=TRANSCODE). Registers parsed straight from the Mednafen savestate (SFORMAT
`[1-byte namelen][name][u32-LE size][data]`, master CPU section `SH2-M`; `R`=0x40 array,
`PC`, then `CtrlRegs`):

- **mc2 @ shim entry `0x06024e44`** (PC=`06024e48`): **R5(src)=`0x060a5a9c` → `"LEAFLET in take "`**,
  R4(dest)=`0x060b2dc8`, R1=`0x06024e44`. So the picker command **does** run the repointed
  shim callsite (`@0x0604550c`). NOT an un-repointed-bypass — kills lead (A′).
- **mc3 @ TRANSCODE `0x0604608a`** (PC=`0604608e`): this is a **separate, later** call —
  R5=`0x0603c470` → **`"ここには、見あたりません"`** ("it isn't here"), R7=`0x060489ec`
  (the `@0x06048a48` response-render callsite). And **`@0x060b2dc8` now holds `説明書を取る`** —
  i.e. between mc2 and mc3 **the shim ran and wrote the correct JP**; it did NOT fall back.

**Airtight conclusion:** assembly → shim → transcode → parser ALL WORK. `LEAFLET in take`
became `説明書を取る`; the parser accepted verb `取る` AND noun `説明書` (the game's own ZVOCTBL
maps `せつめいしょ/説明書`→LEAFLET, confirmed) and replied **"the leaflet isn't here."** The
leaflet was simply **out of scope** at command time (classic Zork: `take leaflet` before the
mailbox is opened/leaflet revealed ⇒ "You can't see any leaflet here").

**Why it LOOKED like a garbled error:** `ここには、見あたりません` (@`0x0603c470`) is NOT in
`responses.py` (25 entries), so the engflag +0x1F renderer draws it as garbage. take/read/
examine all produce the *same* out-of-scope reply → all three "fail with garbled error."

**So there is NO parser/shim/verb bug.** The real open questions are game-flow:
1. Does the picker `open mailbox` (`郵便箱を開ける`) actually put the leaflet **in scope**
   (reveal it), or does it only print a message? Test the canonical West-of-House sequence:
   `open mailbox` → (should reveal leaflet) → `take leaflet` → `read leaflet` → WELCOME TO ZORK.
   Watch whether `open mailbox`'s reply mentions revealing the leaflet.
2. Translate the scope/interaction messages so testing is legible — at minimum
   `ここには、見あたりません` (@`0x0603c470`) and the mailbox-reveal line — add to `responses.py`.

Registers/strings reproducible from the two savestates via `zork_savestate._raw` + the
SFORMAT walk above (see this session's transcript for the exact parser).

## 3. [SUPERSEDED by UPDATE c] Two candidate root causes — the fresh session must disambiguate FIRST

**Do not code a fix until you know which of these it is.** They imply different fixes.

### (A) Command-assembly / transcode-source regression  ← most consistent with the evidence
The parser buffer that the shim (TRANSCODE hook @ `0x0604608a`) reads is not being loaded
with the assembled `"<noun> <particle> <verb>"` string. Shim dest = `0x51` garbage and box
holds only `"LEAFLET"` point here. This would also explain why `take` **regressed** (it used
to work) — a later build changed how/where the command string is assembled before TRANSCODE.

Decisive test: **Mednafen PC breakpoint on the shim entry** (the repointed
`SHIM_LIT_FOFF=0x4150c` target / shim routine address). At the breakpoint, read **r5** — that
is the exact source string the shim receives. 
- If r5 = `"leaflet in take"` → NOT this bug; go to (B).
- If r5 = `"leaflet"` or garbage → confirmed (A): the picker→transcode-source plumbing drops
  the verb. Fix is in the picker command-assembly path, not the verb list.

### (B) Parser verb-vocabulary mismatch  ← the fix "offered last session"
If r5 IS the full correct command and the shim output IS valid JP, but the game still rejects
`読む`(read)/`調べる`(examine) while accepting `見る`(look)/`取る`(take), then those JP verbs
are simply **not in this port's parser verb vocabulary (ZVOCTBL)**. The shim's `VERBS` were
sourced from a general JP dictionary, broader than the game's actual accepted verbs.

Fix (see §4). Note: (B) alone does NOT explain the `take` regression or the `0x51` garbage,
so (A) is the leading hypothesis — but (B) may still apply on top for read/examine.

## 4. The verb-list fix (once (A) is ruled out or fixed)

Goal: every verb the picker offers must be one the game's parser actually accepts, and
English `read`/`examine` must map to an **accepted** JP verb.

1. **Enumerate the game's real verb vocab.** Cross-reference `docs/ZORK1_PARSER_DICT_AUDIT.md`
   (EN→JP-reading coverage, 331/333) and the resolver's JP table `0x0607c9bc`. Extract the
   set of JP verbs the parser will accept (ZVOCTBL). This is the ground truth.
2. **Prune `COMMON_VERBS` / `VERBS` in `analysis/zork_shim.py` + `analysis/zork_data/picker_en2jp.py`**
   to only verbs in that set. Drop or remap any (e.g. `読む`, `調べる`) not present.
3. **Remap read/examine** → the accepted JP verb the game uses to reveal item text
   (candidate: `見る`/look, since look is accepted). Confirm on hardware that the remapped
   verb reveals the leaflet's WELCOME TO ZORK text.
4. Re-run `python analysis/zork_shim.py` self-test; rebuild; verify on emulator.

**Answered by user (2026-07-02): NO verb has ever shown WELCOME TO ZORK** — not look, read,
or examine. So the earlier "working" `leaflet in look` was rendering the room/container
listing, **never the leaflet's text**. Consequences for the plan:
- Reading the leaflet is genuinely unsolved; no accepted verb has revealed item text yet.
- The leaflet almost certainly must be **taken into inventory first** (Zork requires holding/
  opening it), and `take` is currently broken (see §3A) — so fixing execution (A) is the
  prerequisite before "read" can even be tested.
- Identify the game's true "read printed matter" verb from ZVOCTBL; do NOT assume `見る`(look)
  is it — look demonstrably shows the listing, not the text.

## 5. Key addresses / files (reference)

- Build: `analysis/zork_translate_engflag.py` (`build_patched_0zork`), BASE `0x06004000`.
- Shim: `analysis/zork_shim.py`; `TRANSCODE_ADDR = 0x0604608a`; repoint `SHIM_LIT_FOFF=0x4150c`.
- Verb/noun data: `analysis/zork_data/picker_en2jp.py` (NOUNS/VERBS/PARTICLES),
  `analysis/zork_data/picker_resolve_order.py` (RESOLVE_ORDER).
- Responses (in-place translations): `analysis/zork_data/responses.py` (25 entries).
- Picker builder: append primitive `FUN_0x0601ae92`, boxes `0x060ae000`/`0x060ae080`,
  stride `0x80`, count @ `0x060b4834`; resolver JP table `0x0607c9bc`.
- Grid (16-bit, byteswapped): `0x060b4b18`, stride `0x78`. Savestate helper:
  `analysis/zork_savestate.py` → `load_ram(path,"WorkRAMH")` returns `(base, ram)`.
- Disc (unchanged): `game_patched/zork1_engflag_full/Zork1 (engflag full).cue`.

## 6. Diagnostic snippet used (`/tmp/diag_take.py`)

```python
import sys, os; sys.path.insert(0, os.getcwd())
from zork_savestate import load_ram
MCS=r'...\mednafen-1.32.1-win64\mcs'
def load(p): return load_ram(p,"WorkRAMH")[1]
def rd(ram,a,n): o=a-0x06000000; return bytes(ram[o:o+n])
# dump 0x060ae000/0x060ae080 (boxes) and 0x060b2dc8 (shim dest) for mc1 vs mc2
# run with: PYTHONIOENCODING=utf-8 python diag_take.py
```

## 7. First actions for the fresh session

1. Set the Mednafen breakpoint on the shim entry, run `leaflet in take`, read **r5**. Decide (A) vs (B).
2. If (A): trace picker command-assembly (`FUN_0x0601ae92` append path) — why only the noun
   reaches the transcode source. This is the regression that broke `take`.
3. If (B): do §4 verb-vocab prune/remap.
4. Commit after each verified step (regression safety, per user norm).
