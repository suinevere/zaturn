# CLAUDE.md — Sega Saturn translation project memory

Persistent context for this repo, consolidated for Claude Code / CLI use. (Mirrors
the notes under `memory/`.)

## User

Sega Saturn fan-translation / ROM-hacking hobbyist. Focus so far: **T&E Soft golf
games** (maker code T-114). Tests patches on real hardware via multiple ODEs —
**Fenrir, Saroo, Terraonion MODE** — using **Pseudo Saturn Kai** (on an Action
Replay / comms-link cart), and in the **Yaba Sanshiro** emulator. Distributes
patches with **DeltaPatcher** (xdelta GUI) and **Sega Saturn Patcher**
(knight0fdragon, `.ssp`). Comfortable with hex/bytes, hardware, burning/flashing.
Prefers concise, direct answers.

## Project: saturn-translate-mcp

Python FastMCP server + CLI to translate/patch Saturn games, built to orchestrate
`ghidra-mcp` and `textra-ja-to-en-mcp`. Package `saturn_translate/`:
- `iso.py` — ISO9660 reader, auto-detects 2048/2352-byte sectors.
- `sjis.py` — Shift-JIS scan + pointer-anchored decode.
- `pointers.py` — big-endian SH-2 pointer-table detection.
- `reinsert.py`, `build.py`, `project.py`, `triage.py` (feasibility ranking).
- `langpatch.py` — boot-language-selector flip detector.
- `vcdiff.py` — multi-window xdelta/VCDIFF encoder (single window over a 500 MB
  disc is rejected by xdelta3's `XD3_HARDMAXWINSIZE`, so it splits into 8 MB windows).
- `ips.py` — IPS writer. `ecc.py` — Mode-1 EDC/ECC recompute.
- `sh2.py` — Hitachi SH-2 disassembler (for RE without Ghidra).
- `pic.py` — decoder for Steamgear Mash's "PIC" image codec (exp-Golomb RLE + move-to-
  front RGB555 dict + chain-code contour fill). Verified byte-exact on `TITLEPIC.BIN`.
- `textra.py`, `ghidra.py` — upstream-server clients. `server.py` — MCP tools. `cli.py`.
CLI commands: `feasibility, list, extract, scan, tables, project, translate, apply,
pack, force-language [--fix-ecc], disasm`. Tests in `tests/test_core.py` (13 passing).

### Working norms (important)
- **The bash/`mnt` mirror lags and truncates large/recent Write+Edits** (authoritative
  Windows copy is fine). To run/test code, copy `saturn_translate` to `/tmp/run2` and
  run with `PYTHONPATH=/tmp/run2 PYTHONPYCACHEPREFIX=/tmp/pycX`; if a module shows BAD
  on `ast.parse`, rewrite that one file into /tmp via heredoc. Verify authoritative
  files with the editor's Read, not bash. (This is a Cowork-sandbox quirk; under the
  CLI on the real filesystem it shouldn't apply.)
- **Verify patches against ground truth, not just self-consistency.** A self-verifying
  xdelta only proves the encoder round-trips, not that the edit is semantically right.
  Two real bugs were caught only by comparing auto-detector output to a hand-verified
  offset (a coincidental pointer match; a base-alignment ambiguity with a menu pointer
  table). For Saturn language patches: confirm the JP loader pointer value is unique in
  the image and the inferred load base is page-aligned.

## Reference: T&E Soft golf English activation

T&E golf engine (maker T-114). Root files: `A.BIN` (boot dispatcher, loads to HWRAM
`0x06054000`), `EXEC.BIN`, `GOLF.BIN`, `SETUP.BIN`, `TUTORIAL.BIN`, `GUIDE.BIN`,
`DEMO.BIN`, `*.DAT`. MODE1/2352, SH-2 **big-endian**, on-screen text uses a **custom
font** (private-use glyphs) so plain Shift-JIS scanning of overlays = garbage.

Two language mechanisms:
1. **Full dual build (Waialae no Kiseki, T-11402G):** every overlay exists twice — JP
   (`GOLF.BIN`…) and English (`EGOLF.BIN`…), via `A.BIN → LOAD.BIN/ELOAD.BIN →
   EXEC.BIN/EEXEC.BIN → overlays`. `A.BIN` holds a "Please Select Language" menu and a
   big-endian pointer pair `[ptr→LOAD.BIN][ptr→ELOAD.BIN]`. **Force English = flip the
   default pointer to ELOAD.BIN** — for Waialae, 4 bytes at image offset `0x1C008`
   (`06054748→06054754`). Confirmed working on emulator + Saroo. Detector:
   `force-language` requires the two pointers adjacent AND the load base page-aligned
   (rejects the menu pointer table, which is also 0xC-spaced and mimics the loader pair).
2. **Asset-pair toggle (Augusta 3 T-11401G; Jun Classic T-11403G):** no English code
   build, no LOAD/ELOAD, no menu — just `_E`/`_J` media pairs picked by a runtime flag
   (filename letter = `'E' + system_language`, 0=Eng..5=JP). Only Waialae had a Western
   "True Golf Classics" counterpart → only it got a full English build.

## Reference: patch distribution

- **EDC/ECC:** patching a byte in MODE1/2352 invalidates the sector's EDC/ECC.
  Emulators + Fenrir/Saroo tolerate it; **Terraonion MODE freezes at the SEGA logo**.
  Fix: recompute via `ecc.py` (validated by reproducing untouched sectors' own ECC).
- **`.ssp` (Sega Saturn Patcher) is the cleanest distribution:** renamed ZIP of only
  the **changed files**; it rebuilds the disc + regenerates all EDC/ECC itself. Ship
  just the changed file (Waialae = `A.BIN`, 228 KB → `game_patched/ssp_source/A.BIN`).
  Huge `.ssp` = you included the whole disc/track.
- **xdelta/IPS** also provided (DeltaPatcher / IPS tools); raw image must match original.
- **Pseudo Saturn Kai cheats:** custom AR code entry is **debug-build only**; cheats need
  the CWX loader (JHL disables them); codes are region-specific.

## Reference: game survey (game_originals/)

- **Waialae no Kiseki** — full English build; 1-byte loader flip. DONE (`game_patched/`).
- **Augusta 3** — only 4 English Cinepak videos behind BIOS-language toggle; no English
  UI. Not worth patching.
- **Jun Classic** — `_E.APC` (caddie audio) + `_E.GDT` (graphics) pairs; not fully
  characterized.
- **Bug! (JP "Bug Jump shite…")** — already English (Western game; JP kept English UI;
  98/125 files byte-identical to US disc; the 26 differing files are revised sprites,
  not a JP-text layer). No injection needed/feasible.
- **Steamgear Mash (Japan)** — see below; real from-scratch translation, in progress.
  **PIC image codec SOLVED + ported (`saturn_translate/pic.py`)**; text/font codec next.
- **Oh-chan no Oekaki Logic** — not examined.

## Steamgear Mash (Japan) — PIC codec SOLVED; text/font codec next

Japan-only (T-103, T-10301G, 1995). All text-bearing assets compressed: `TITLEPIC.BIN`
= `PIC\x1a` (confirmed compressed — not raw 4bpp/16bpp), `STAGE#CG.BIN` = `80 00` RLE
4bpp, font/script compressed in `0.BIN`. Sprites (`MASH*.BIN`) raw.

Full Ghidra decompilation analysed at `game_originals/Steamgear Mash (Japan)/0.c`
(1869 functions). Load pipeline (base `0x06004000`): open `FUN_06007abc(dir,name)` →
**raw** ISO read `FUN_060dd8c0(handle,nSectors,dest,size)` (TITLEPIC = 0x38 sectors →
`0x1c000` into `0x20280000`) → close `FUN_060dd5b4`. Per-asset loaders `FUN_06008e04/
06008e62/06008ef4` load into the same buffer `0x20280000` then set a flag + call
sound/display setup (`FUN_0600b728`→`FUN_0600b640` = CONFIG.DMP sound init, NOT codec).

**PIC codec SOLVED** (the earlier blocker — it's invoked via the state-machine's
function-pointer tables, so static tracing/LZSS/fingerprint searches all missed it).
Cracked it with a **Mednafen emulator watchpoint**: write-watchpoint on VDP2 VRAM kept
trapping the sound-DMA busy-wait, but a PC breakpoint on the bit-reader caught it inside
a 320-wide pixel loop with `PR` giving a real static call chain. Codec = entry
`FUN_06005cf8` → main `FUN_060059f0` + contour fill `FUN_06005ade`, bit-readers
`FUN_060041dc` (exp-Golomb run length) / `FUN_060041a0` (read-N-bits), and a 128-node
move-to-front RGB555 colour dictionary. Ported to `saturn_translate/pic.py`; decodes
`TITLEPIC.BIN` → correct 320×244 title image (`analysis/titlepic.png`), byte-exact.
**Key correction to old notes:** the `PIC\x1a` magic *is* checked — bit-by-bit via the
reader, which is why a byte-pattern search concluded it wasn't.

**Next (text/font):** `0.BIN` has NO `PIC\x1a` blobs and the disc has only one (TITLEPIC),
so font/script use a *different* scheme (`STAGE#CG.BIN` = `80 00` 4bpp RLE; dialogue
font/script compressed in `0.BIN` by an as-yet-unidentified codec). Decode a `STAGE#CG`
RLE and locate the dialogue font next; an emulator watchpoint when dialogue first draws
is the fast path again. Full detail: `docs/STEAMGEAR_MASH_RECON.md`,
`docs/STEAMGEAR_GHIDRA_RUNBOOK.md`.

## Docs in repo
- `docs/TANDE_GOLF_ENGLISH_ACTIVATION.md` — the T&E technique playbook.
- `docs/STEAMGEAR_MASH_RECON.md` — Steamgear asset map + RE state.
- `docs/STEAMGEAR_GHIDRA_RUNBOOK.md` — how to drive Ghidra for the codec.
