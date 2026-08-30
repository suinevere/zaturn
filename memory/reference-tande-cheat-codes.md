---
name: reference-tande-cheat-codes
description: Button-combo cheat/secret codes in the T&E Saturn golf games, found by SH-2 static RE
metadata:
  type: reference
---

Button-combo (controller) codes in the T&E golf discs, located by static SH-2
analysis of the overlays. Saturn standard-pad held-button mask (active-high bits):
`Right=0x8000 Left=0x4000 Down=0x2000 Up=0x1000 Start=0x0800 A=0x0400 C=0x0200
B=0x0100 R=0x0080 X=0x0040 Y=0x0020 Z=0x0010 L=0x0008`.

Tooling: `analysis/cheat_scan.py <file.BIN> <load_base_hex>` — finds a read of the
live controller word followed (tight window + conditional branch) by `cmp/eq`/`tst`
against a valid pad-button constant, and decodes the button names. High-precision
version requires the input-deref + mask-load to be adjacent to the compare; this
removed false positives (e.g. GOLF.BIN `cmp/eq r13,r14` near literal `0xa3c8` is a
loop-counter compare, not a button check). Recurring library combos to IGNORE
(ordinary demo-skip / "press any action button" / menu nav, present in many
overlays): `0x00f0`=R+X+Y+Z, `0x00e0`=R+X+Y, `0xc000`=Left+Right, `0x0f70`=any
action button (tst), `0x0300`=C+B, `0xff00`=any d-pad/face (tst).

**Waialae no Kiseki (T-11402G):** the ONLY genuine secret button code in the whole
game is the **startup combo Down+C+X+L** (`cmp/eq` vs `0x2248` at `A.BIN`
`0x060540ec`; live pad word = `0x06069aec`). Hold it while the game boots. On match
it `bsr`s the menu routine `0x06054170`, whose returned selection reaches the load
dispatcher `0x06054568`; on no-match `r4=0` → default load. Verified by disassembly;
no other startup or in-game combo exists (every other overlay hit was a library
skip/pause routine).

**The hidden menu = the western "True Golf Classics" 6-language menu, but only JP+EN
work.** A.BIN file 0x700+ holds 6 labels: `1. Japanese / 2. English / 3. Dutch /
4. France / 5. Spanish / 6. Italian` (string-ptr table at file 0x6a8). BUT (a) the
disc only ships two loader overlays — `LOAD.BIN`(JP) and `ELOAD.BIN`(EN); the
loader-name pointer pair is at A.BIN file 0x6f8/0x6fc; (b) the load dispatcher
`0x06054568` builds only a 2-entry pointer array and indexes by selection*4, and
(c) the menu confirm logic hard-rejects selection ≥ 2 (`mov #2,r3; cmp/ge r3,r13;
bra back` at `0x060542ec`). So Dutch/French/Spanish/Italian are drawn but
**unselectable** and have no data. There is nothing to "patch in" for other
languages — the engine here only supports Japanese and English.

**Patches (in `game_patched/`):**
- Force English (existing): A.BIN file `0x6f8` `06054748`→`06054754` (LOAD→ELOAD
  default ptr), image off `0x1C008`. See [[reference-tande-english-activation]].
- **"Always ask for language"** (always show the menu, no cheat needed): NOP the
  cheat's skip branch — A.BIN file `0xee` `8b04`(bf)→`0009`(nop), image off
  `0x1B9FE`. Both branch paths restore PR in their bra delay slots, so stack-safe.
  Built by `analysis/make_langmenu_patch.py` → `waialae-langmenu.{xdelta,ips}` +
  `ssp_source_langmenu/A.BIN`. Do NOT combine with the English-force flip (that
  would make the menu's "Japanese" entry load English). Optional tweaks: default
  cursor to English = `mov #0,r10`→`mov #1,r10` (`0xea00`→`0xea01`) at file `0x7a`;
  trim menu to 2 entries needs locating the draw-count (the confirm cap is already 2).
  STILL NEEDS a boot test on emulator/hardware.

**Augusta 3 (T-11401G):** `A.BIN` has **zero** button-combo checks — no startup
language/cheat gate of any kind. Only the recurring `0x00f0` demo-skip in
TUTORIAL.BIN. Consistent with: no English code build, only BIOS-language Cinepak
videos ([[reference-saturn-game-survey]]).

**Jun Classic C.C. & Rope Club (T-11403G):** `A.BIN` has no combo `cmp/eq` (only an
`0xff00` any-button mask test) — **no startup language/cheat gate**. `SETUP.BIN`
has repeated `cmp/eq` vs `0x0090`=**R+Z** and `0x0098`=**R+Z+L** — candidate hidden
options-menu shortcuts (UNVERIFIED; test on hardware). Not a language menu (Jun has
no English build). No dual build / no ELOAD/EEXEC/EGOLF.

Both sequels: nothing to "force English" the Waialae way.
