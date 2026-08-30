---
name: reference-steamgear-mash
description: Steamgear Mash (Japan) reverse-engineering state — load pipeline, codec hunt
metadata:
  type: reference
---

**Steamgear Mash (Japan)** — Japan-only (maker T-103, product T-10301G, 1995),
MODE1/2352, 1 data track + 9 CD-audio. No English release, no language toggle:
a genuine from-scratch fan translation.

**Assets:** all text-bearing data is COMPRESSED. `TITLEPIC.BIN` = `PIC\x1a` magic
(entropy 7.70; confirmed compressed — renders as noise at both 4bpp and 16bpp
RGB555). `STAGE0..8CG.BIN` (264 KB each) = 4bpp CG with an `80 00` RLE-ish scheme.
Font + script are compressed inside `0.BIN`. `MASH*.BIN` = raw 4bpp sprites.
No `FONT/MSG/SCRIPT` file on the disc.

**Tooling:** native **SH-2 disassembler** built (`saturn_translate/sh2.py`, CLI
`disasm`), validated against `0.BIN`'s reset code. Full Ghidra decompilation exported
to `game_originals/Steamgear Mash (Japan)/0.c` (**1869 functions**, base `0x06004000`).

**Load pipeline (mapped):** open `FUN_06007abc(dir,"NAME.BIN")` → **raw** ISO/CD read
`FUN_060dd8c0(handle,nSectors,dest,size)` (TITLEPIC = 0x38 sectors → `0x1c000` into
`0x20280000` = cached LWRAM `0x00280000`) → close `FUN_060dd5b4`. Per-asset loaders
`FUN_06008e04/06008e62/06008ef4` load into the same buffer `0x20280000`
(`DAT_06008f58`), set a flag, and call sound/display setup (`FUN_0600b728`→
`FUN_0600b640` = CONFIG.DMP sound init — NOT the codec).

**Open problem:** the decompressor reads `0x00280000` and writes VDP VRAM, invoked
through the state machine's **function-pointer tables**, so it has no direct caller
in static C and was NOT found by: call-table tracing, Okumura-LZSS constant search
(no `0xFEE`/`0xFFF`), function fingerprinting (top hits were save-data `FUN_06008574`
+ unrolled render math), or VDP-write search.

**Next step (recommended): emulator watchpoint.** Run to the title screen in
Mednafen/Kronos, set a read-watchpoint on `0x00280000` (or write-watchpoint on VDP2
VRAM `0x05E00000`) → traps inside the decompressor. Pull that function from `0.c`,
port to Python (planned `sh2cpu.py` interpreter can execute it byte-exact), verify on
TITLEPIC, then locate font + script (same codec), translate, draw an English font,
reinsert. Full detail: `docs/STEAMGEAR_MASH_RECON.md`, `docs/STEAMGEAR_GHIDRA_RUNBOOK.md`.
