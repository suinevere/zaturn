# Steamgear Mash — Ghidra Runbook (you run, I build)

I can't run Ghidra from the sandbox (it needs JDK 21; only JDK 11 is available here
and there's no network to add one). You have Ghidra 12.0.1 in the project, so the
fastest path is: **you run these steps on your machine and paste the output back to
me**, and I turn the decompiled routines into Python decompressors + extract/reinsert
tooling in `saturn_translate/`.

Goal of this pass: recover the **asset codec(s)** so we can read the font and script.
Two concrete targets — the `PIC` image decompressor and whatever packs the font/script
in `0.BIN`.

## 1. Import `0.BIN`

1. First extract it (so Ghidra sees just the file, not the whole disc). Either use the
   toolkit — `saturn-translate-cli extract "<Steamgear Track 1>.bin" /0.BIN -o 0.BIN`
   — or copy it out with any ISO tool.
2. Ghidra → **File ▸ Import File** → `0.BIN`.
3. **Language:** click the `…` and pick **SuperH / SH-2 / 32 / big** (filter on
   "SuperH"; choose the SH-2 big-endian variant).
4. **Options ▸ Base Address:** `0x06004000` (the disc's IP load address; this makes
   absolute pointer cross-references resolve correctly).
5. Import, open in CodeBrowser, **Analysis ▸ Auto Analyze** with defaults.

> Note: `0.BIN` may have a small header before code — its first bytes
> `00 02 cb f0` look like a length/entry field, with SH-2 code starting a few bytes
> in. If auto-analysis misses the entry, disassemble at `0x06004008` and let it flow.

## 2. Find the `PIC` image decompressor

`TITLEPIC.BIN` starts with magic `50 49 43 1a` (`PIC\x1a`). The loader compares that
magic, so the decompressor is near a constant `0x50494300`/`0x504943` or the literal
bytes `P I C`.

1. **Search ▸ For Scalars** (or Memory) for `0x504943` and `0x50494331`/`0x1a`.
2. For each hit, follow the cross-reference (`Ctrl+Shift+F` on the address) into the
   function that reads it.
3. That function (and the loop it calls) is the decompressor. **Decompile it**
   (`Ctrl+E` / the Decompiler window) and copy the C for it **and any helper it calls**.

Paste me that C. I'll port it to `saturn_translate/pic.py` and we'll decode
`TITLEPIC.BIN` to verify byte-exact.

## 3. Find the font + script

The dialogue font and script live in `0.BIN`'s high-entropy regions (compressed).
To find them, work backward from text rendering:

1. **Search ▸ For Strings** (min length 4). Note any readable ASCII (debug names,
   file names like `STAGE%dCG`, `%s`, format strings) — paste the list.
2. Find the **VDP1/VDP2** character/sprite draw calls — writes to `0x25C00000`
   (VDP1 VRAM) or `0x25E00000` (VDP2 VRAM), or DMA to those. The routine that fills a
   glyph cell from a source buffer is the **text blitter**; its source pointer is the
   font.
3. Decompile the function that builds a string on screen (takes a buffer of byte codes,
   loops, draws cells). Paste it — that tells us the **byte→glyph encoding** (SJIS vs a
   custom table) and where the script bytes come from.

## 4. (Optional) headless alternative

If you'd rather not click, from a shell with JDK 21 on PATH:

```
ghidra_12.0.1_PUBLIC/support/analyzeHeadless <projDir> sm -import 0.BIN \
  -processor "SuperH:BE:32:SH-2" -loader BinaryLoader \
  -loader-baseAddr 0x06004000 -postScript DecompileToC.java
```

(Ghidra ships example scripts; `Search Program Text` + the Decompiler export work too.)
Paste me the decompiled C of the routines from steps 2–3.

## What I do with it

For each codec you paste, I implement and **validate it byte-exact** against the disc
files (decompress → recompress or known-size check), then build the script
extract/translate/reinsert flow and an English font. Same verify-against-ground-truth
discipline we used for the patches.

## Status / what's already known

See `STEAMGEAR_MASH_RECON.md` for the full asset map. Key facts for this pass:
- `0.BIN` — SH-2 executable, base `0x06004000`, compressed data blobs (font/script).
- `TITLEPIC.BIN` — `PIC\x1a` compressed image (entropy 7.70).
- `STAGE#CG.BIN` — 4bpp graphics in a tiled/planar layout (nibble-run pixel data, not
  high-entropy compression) — likely a tile-editor job (Crystal Tile 2), not a codec.
- `MASH*.BIN` — raw 4bpp sprites.

## RUNBOOK: map the BUTTON-config kanji labels (2026-06-11)

Goal: translate the controller-config screen's kanji action labels (ショット/ジャンプ/
ウエポン/武器選択1・2/視点変更/加速). They are baked tile graphics drawn from **ONDATA.BIN**,
which carries an 8×8 4bpp font that is **ASCII-indexed** (tile = `0x410 + ord(C)*32`,
fg nibble 0xF; A-Z/0-9 verified) PLUS a kana/kanji region. So the labels are almost
certainly drawn by a **tile-index list / tilemap** (one entry per label = a run of glyph
tile indices), NOT pre-composed label bitmaps. **Preferred translate route = re-point that
list to the Latin tiles that already exist in ONDATA** (clean, localized — same idea as the
menu re-script), with `analysis/ondata_label.py` available as the pixel-copy fallback.

Two things to find: (i) where each label's glyph-index run lives, (ii) the index→tile encoding.

**Static first (cheap):** ONDATA loads raw via `FUN_06008e04` (cf. `0.c` ~L9584). Find its
dest buffer and any code that walks a per-label table. Look for a structure like the menu
table (`8000 <refs…> 0000`, recon doc) but for 7 controller actions. The header records at
ONDATA start (`0028 0010 0028 0410 002a 0410 002a 1310 …`) are candidate sprite-cell / index
records — decode them against the known glyph-sheet tile indices.

**Watchpoint (fast path, mirrors the PIC/menu cracks):**
1. Boot patched disc in Mednafen; main menu → BUTTON (controller config) screen.
2. `ALT+D` debugger. The labels redraw when the cursor moves between action rows, so an
   access there re-triggers reliably.
3. Locate ONDATA in RAM: search WRAM for the kana label bytes (e.g. the シ/ョ/ッ/ト glyph
   tile-index values from the sheet) or for the ASCII font signature; that gives the loaded
   ONDATA base. Set a **read-bp (`SHIFT+R`) on the label-index run** (the table, not the
   pixels) and nudge the cursor → catches the label-draw routine. `PR` = static caller.
4. From that routine: read the per-label index list (offset in the ONDATA buffer → minus
   base = ONDATA file offset) and the index→tile mapping. Record, per label: the file offset
   of its index run, its length (= available tile width), and how indices map to tiles.

**Then translate:** rewrite each label's index run to spell the English using the ASCII
tile indices (ord(C) under the same encoding), space-padded to the run's width. Reinject
ONDATA via `analysis/patch_image.py` (separate file → clean) + ECC, same as 0.BIN.

**Fit (width = original kana tile count):** ショット=4→`SHOT`, ジャンプ=4→`JUMP`,
ウエポン=4→`WPN ` (or `WEPN`), 武器選択1=5→`WPN 1`/`SEL 1`, 武器選択2=5→`WPN 2`/`SEL 2`,
視点変更=4→`VIEW`, 加速=2→`GO`/`SP` (only 2 tiles — `DASH` won't fit unless the slot has
trailing pad; confirm width at step 4). Mednafen debugger keys: see RECON doc reference.
