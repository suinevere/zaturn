# Steamgear Mash (Japan) — Translation Reconnaissance

Disc: `Steamgear Mash (Japan)` — maker **T-103**, product **T-10301G**, 1995-08-24,
region **J** (Japan-only). MODE1/2352, 1 data track + 9 CD-audio tracks.
No English release, no `_E`/`_J` assets, no language selector — this is a
**from-scratch fan translation**, not an activation.

## Asset map (data track, 65 files)

| File(s) | Size | Entropy | What it is |
|---|---|---|---|
| `0.BIN` | 970 KB | mixed (code + high-entropy blobs) | **Main SH-2 executable**; starts with real SH-2 code. Contains compressed data sections. Font + script almost certainly live here (compressed). |
| `MASH*.BIN` | 50–490 KB | **2.16** (raw) | Player character "Mash" sprite art (4bpp), uncompressed. NOT the font. |
| `TITLEPIC.BIN` | 114 KB | **7.70** (compressed) | Title image. Magic header **`PIC\x1a`** (`50 49 43 1a`). Custom "PIC" compressed format. |
| `STAGE0CG`–`STAGE8CG.BIN` | 264 KB each | 4.5–5.3 | Per-stage CG (cutscene/event graphics), **RLE-compressed** (`80 00` control words). Likely where story art — and possibly baked-in story text — lives. |
| `WOOD_CG.BIN` | 56 KB | 3.55 | Misc CG, mostly raw. |
| `ONDATA.BIN` | 357 KB | 3.44 | Structured record table (`00 28 00 10 …`) — object/enemy placement or pointer data. |
| `FIELD_S#.BIN`, `BOSS_S#.BIN`, `ST#_ENE.BIN` | — | low/struct | Level / enemy data. |
| `CONFIG.DMP`, `SNDST#.DMP`, `SNDMAP.BIN`, `SDDRVS.TSK` | — | — | Sound driver + banks. |
| `CDDA1`–`9` | 27–41 MB | — | Streamed audio. |
| `OPDEMO/EN1MOV/EN2MOV/STFMOV/GOVMOV/S56MOV.CPK` | — | — | Cinepak movies (opening, endings `EN`, staff, game-over). Japanese video/voice. |

## Findings

1. **No plain text and no standalone font/script file.** Shift-JIS scans of `0.BIN`
   and `MASH.BIN` return only noise (binary mis-parsed as kana) — there is no
   readable script sitting in the clear.
2. **The text-bearing assets are compressed.** TITLEPIC is a `PIC`-magic compressed
   image; the stage CG panels are RLE (`80 00` codes). The dialogue font and script
   are not exposed; they are compressed, most likely inside `0.BIN`.
3. **Sprites are raw** (`MASH*.BIN`, entropy ~2), which is why those are the only
   things that render directly.

## The wall / what's needed next

To translate, the compression must be reversed first. Concretely, in order:

1. **Reverse the decompressor(s)** — the `PIC` image codec and the `80 00` CG RLE,
   and whatever packs the font/script in `0.BIN`. This is a **Ghidra** job: load
   `0.BIN`, find the routine that reads the `PIC\x1a` header / `80 00` stream, and
   reimplement it. (The project's `ghidra.py` client + ghidra-mcp are meant for
   exactly this, but ghidra-mcp must be running with `0.BIN` loaded.)
2. **Locate the font** (decompressed) and determine its glyph order / encoding.
3. **Locate + extract the script**, confirm it decodes with the font map.
4. **Translate**, draw an English font (custom glyphs), reinsert (re-compress or
   relocate), and **test in an emulator** (Yaba Sanshiro) extensively.
5. Possibly **edit CG panels** if any story text is baked into the graphics.

This is a multi-session reverse-engineering project. The immediate blocker is
step 1, which requires the Ghidra MCP connected — without it, static analysis can
map the formats (done here) but cannot recover the compressed text.

Render experiments and entropy maps used for this recon are in `../analysis/`.

## Reverse-engineering progress (SH-2, no Ghidra)

Since the sandbox can't run Ghidra (needs JDK 21; only 11, no network), a native
**SH-2 disassembler** was built into the toolkit: `saturn_translate/sh2.py`
(`disasm`, `disasm_range`; CLI `saturn-translate-cli disasm 0.BIN --offset … --base 0x06004000`).
Validated against `0.BIN`'s reset code (SR setup → SP load → BSS-clear loop) and a
unit test of canonical opcodes. Next is an SH-2 *interpreter* to execute a codec
routine and dump its output byte-exact (`sh2cpu.py`, not yet built).

**Confirmed addresses (base `0x06004000`):**
- Reset/entry at `0x06004000` (no header; code starts at byte 0).
- **Asset filename tables:** group table near file `0x45400` (header of RAM
  load-addresses + names `ONDATA/WOOD_CG/TITLEPIC/MASH2`); stage record table near
  `0x493d0` (`ST#_ENE`, `STAGE#CG`, `FIELD_S#`, `BOSS_S#`, 60-byte slots).
- **Asset-loader function at `0x06008e04`:** inits hardware regs, then a chain of
  `jsr` calls (targets in a literal pool at `0x06008e8c`) to load+process assets,
  with an error infinite-loop guard at `0x06008e3a`.
- The PIC magic (`50 49 43 1a`) is **not** compared in `0.BIN` — the codec doesn't
  branch on it, so find it structurally, not by magic.

**Next RE step:** the loader's CD-read path goes through wrappers `0x06007abc` /
`0x06007afc` (the latter is a 30×-retry read loop) into library functions in the
`0x060dd***` / `0x060c2***` range. The decompressor lives down that chain (the load
routine decompresses internally). Trace those library calls (disassemble
`0x060dd5b4`, `0x060dd8c0`, `0x060ddda0`) to find the bit-extraction/back-reference
loop, then run it via the SH-2 interpreter on `TITLEPIC.BIN` to recover the codec
byte-exact. After that: locate the font + script (likely behind the same codec),
build extract→translate→reinsert, and draw an English font.

This is a multi-session effort; the disassembler + asset map + loader map are the
foundation laid so far.

## Full Ghidra decompilation analysed (`game_originals/Steamgear Mash (Japan)/0.c`)

Exported the whole program: **1869 functions**, 58k lines of C. Confirmed the asset
pipeline end to end (base `0x06004000`):

- **Open** `FUN_06007abc(dir, "NAME.BIN")` → handle.
- **Read** `FUN_060dd8c0(handle, nSectors, dest, size)` — a **raw** ISO9660/CD-block
  read (calls `FUN_060de19a/060dddfe/060dd9f0/060ddc5e`). e.g. TITLEPIC: `0x38`
  sectors × 2048 = `0x1c000` into dest `0x20280000` (cached LWRAM `0x00280000`).
  **No decompression here.**
- **Close** `FUN_060dd5b4(handle)`.
- Per-asset loaders: `FUN_06008e04` (ONDATA), `FUN_06008e62` (WOOD_CG),
  `FUN_06008ef4` (TITLEPIC) — all load into the **same** buffer `0x20280000`
  (`DAT_06008f58`) then set a loaded-flag and call sound/display setup
  (`FUN_0600b728`→`FUN_0600b640` = `CONFIG.DMP` sound init, **not** the codec).

So assets are loaded compressed into `0x00280000`; a **display/state routine
consumes that buffer and decompresses to VDP VRAM**. That consumer is invoked
through the state machine's function-pointer tables, so it has no direct caller in
the static C and wasn't pinned by: call-table tracing, LZSS-constant search
(no `0xFEE`/`0xFFF` Okumura ring), function fingerprinting (top hits were the
save-data routine `FUN_06008574` and unrolled render/math), or VDP-write search.

**TITLEPIC is genuinely compressed** (header `PIC\x1a`; not raw 4bpp and not raw
16bpp RGB555 — both render as noise), so a CPU decompressor definitely exists.

### Fastest ways to pin the codec from here
1. **Emulator watchpoint (recommended):** run to the title screen in an emulator
   with a debugger (Mednafen/Kronos), set a **read-watchpoint on `0x00280000`** (or a
   write-watchpoint on VDP2 VRAM `0x05E00000`); it traps inside the decompressor.
2. **Interactive Ghidra:** put the cursor on `DAT_06008f58` / the title state and use
   *References* to find the function that reads `0x20280000`; decompile it.
3. Continue tracing the state-machine function-pointer tables in `0.c`.

Tools ready for whichever path: `saturn_translate/sh2.py` (disassembler) and, once
the routine is located, a planned `sh2cpu.py` interpreter to run it byte-exact.

## Live emulator-watchpoint session (Mednafen) — findings

Ran the recommended watchpoint approach in Mednafen's SH-2 debugger. Confirmed facts
(ground truth, not inference):

1. **TITLEPIC is genuinely compressed.** Pulled the raw header straight from the disc
   (Track 01, `PIC\x1a` magic at file offset `0x5967370`):
   `50 49 43 1A 00 00 00 00 | 0F 01 40 00 F4 1D B4 90 …` — after the 8-byte header the
   data is immediately dense/high-entropy with no zero runs, no palette block, no cell
   structure. Rules out "raw VDP cells rendered as noise." A CPU decompressor exists.

2. **The title-image display path is DMA-driven, not a CPU blit.** Compressed PIC loads
   raw to `0x00280000`; the decompressed result reaches VDP VRAM via **DMA**, with a
   palette-fade engine running the fade. So CPU read/write-watchpoints on VRAM only ever
   trap the CPU's **DMA-completion busy-wait**, never the codec. Key routines:
   - `FUN_060da138` (`0x060da138`–`0x060da6fc`) = **palette-fade / colour-animation
     engine** (steps byte arrays toward targets, packs **RGB555** via `<<5 … <<5`).
   - `FUN_060db470` = 16-bit control-token decoder (compares vs `DAT_060db52c/530`, masks
     `DAT_060db534/516`, decodes count+mode).
   - `FUN_060db538` (`060db548⇄54c` tight loop) = **busy-wait on DMA completion** (seen
     spun 2213×). The DMA it waits on is **SCSP sound RAM** (SH-2 DMAC ch0
     `SAR=0x002BEAC4 → DAR=0x05A25888`), i.e. the **sound driver's** transfer — this is
     why the watchpoints kept landing here.

3. **LOCATED THE FULL PIC CODEC.** PC-breakpointed the bit-reader `0x060041dc` and it
   trapped with R6=`0x140` (=320, image width), working pointers in VDP2 VRAM
   (`0x25E4xxxx`/`0x25E7xxxx`), and `PR=0x06005A14` inside a 320-iteration pixel loop —
   confirming **graphics**, not sound. Resolving the literal pools nailed every routine.

   **Entry point: `FUN_06005cf8(stream, x, y)`** — the PIC decompressor. It checks the
   `50 49 43 1A` ("PIC\x1a") magic and reads width/height as 16-bit fields **8 bits at a
   time through the bit-reader** (`read8()=='P','I','C'`, skip to `0x1a`, skip to `0x00`,
   skip two 16-bit fields, `width=read16()`, `height=read16()`), then runs the decoder.
   **CORRECTION to the earlier note "PIC magic is not compared in 0.BIN":** it *is*
   compared — but bit-by-bit via the reader, so a byte-pattern search for the compare
   missed it.

   The codec is a **custom contour/RLE scheme with an adaptive move-to-front colour
   dictionary** — nothing like LZSS, which is why prior LZSS-constant / fingerprint
   searches all failed. Routine map:
   | Routine | Role |
   |---|---|
   | `FUN_06005cf8` | entry: parse `PIC\x1a` header + W/H, call decoder |
   | `FUN_060059f0` | main loop: RLE runs (`060041dc`) + paint pixels into VRAM grid |
   | `FUN_06005ade` | chain-code boundary fill: 2-bit dir tokens, walk x ±1/±2 per row |
   | `FUN_06005c8a`/`b9e`/`bdc`/`cbc` | move-to-front colour dictionary (12-byte list nodes) |
   | `FUN_060041dc` | bit-reader: one Elias-gamma/exp-Golomb value (run lengths) |
   | `FUN_060041a0(n)` | bit-reader: read `n` fixed bits (magic, dims, dict indices) |

   Bit-reader state: `0x06004204` (bitbuf), `06004208` (bitcount), `0600420c` (stream ptr,
   advances one 32-bit word per refill; MSB-first). Output framebuffer base `0x25E40000`
   (VDP2 VRAM), 16-bit RGB555 pixels OR'd with `0x8000` (bit15), **stride 0x200 shorts
   (512 px)** per row. Width var `*0x06083560`, height `*0x06083564`.

   **DONE — codec ported and VERIFIED.** Hand-ported the whole codec to Python at
   `analysis/decode_titlepic.py` (shared MSB-first big-endian bit-reader; exp-Golomb run
   reader; 128-node circular move-to-front RGB555 colour dict; chain-code contour fill;
   512-stride framebuffer). Decoding `TITLEPIC.BIN` yields **width=320 (0x140), height=244
   (0xF4)**, every pixel painted, and renders as the **correct "STEAMGEAR Mash" title
   image** (`analysis/titlepic.png`) — ground-truth confirmation, not self-consistency.
   Dict layout: nodes 12 bytes `{value,prev,next}`, base `0x06083568`, head idx var
   `0x06083B68`, init circular value=0/prev=i-1/next=i+1. `get_colour`: 1 bit → 0 = new
   15-bit RGB555 literal (recycle LRU = `head=next[head]`), 1 = 7-bit index (move-to-front).
   `TITLEPIC.BIN` `PIC\x1a` magic in Track 01 at file offset `0x5967370`.

   **Next: point the same decoder at the font/script.** Search `0.BIN` (and the stage CG)
   for more `PIC\x1a` blobs / the same exp-Golomb+MTF stream; the font and dialogue script
   are almost certainly this codec. Then build the inverse (PIC *encoder*) to reinsert an
   English font + translated script.

### Resolved pointer tables (from `0.BIN` literal pools)
- Codec readers: `PTR_FUN_06005abc=0x060041DC` (Elias-gamma), `…ac8/b58/cb8=0x060041A0`
  (read-N-bits). Output base `=0x25E40000`, OR-mask `=0x00008000`, W/H vars
  `0x06083560`/`0x06083564`.
- TITLEPIC loader `FUN_06008ef4` post-load call `PTR_FUN_06008f84 = 0x0600B728` =
  CONFIG.DMP sound init (confirms load path does **not** decompress; codec is invoked
  later from the title display state via `FUN_06005cf8`).
- `FUN_06046170` (SGL title setup, NOT the codec): `PTR_FUN_060461c4=0x0600533C`
  (SGL VDP2 scroll setup), `…c8=0x060D9AC8`, `…cc=0x06008E04` (ONDATA loader),
  copy routine `…d8=0x06004196`, buffer `=0x20280000`, VDP2 VRAM dest `=0x25E00000`.
- The `060da138`/`060db4xx`/`060db538` cluster that earlier watchpoints kept hitting is
  the **sound-DMA + palette-fade** path (SH-2 DMAC ch0 → SCSP `0x05A25888`), unrelated
  to the image codec.

### Text / dialogue-font hunt (static, no emulator)

Goal: locate the in-game **dialogue font + script** (player reports menus and first
in-game dialogue are Japanese). Findings:

- **The PIC codec is used by TITLEPIC only.** The codec's global state (`0x06083554/58/5C`,
  dict `0x06083568`) is referenced by *exactly* `FUN_060041a0`, `FUN_060041dc`,
  `FUN_06005cf8` and the PIC colour routines — nothing else. So the script/font is a
  **different** mechanism.
- **ASCII menu/HUD text** (`"TYPE 1"`, `"HOUR MIN SEC"`) is drawn by `FUN_06004E6C(x,y,
  attr,str)` → writes `char + attr<<16` into a VDP2 pattern-name grid (`DAT_06004edc=
  0x25E28000`, `DAT_0600501c=0x25E00000`); `FUN_06004ee4` composites cells into VDP1
  sprite commands via cell→VRAM LUTs at `0x0604D054` (x, step 8B) / `0x0604D094` (y, step
  0x100), implying ~16×16 glyph cells. The ASCII glyphs are almost certainly the **SGL
  built-in font** (library region) — no Japanese in it.
- **No Japanese font in any plainly-stored asset.** Ruled out by content census + 4bpp
  rendering (`analysis/gfxview.py`): `0.BIN` = code + a **458 KB zero/BSS gap
  (0x60000–0xD0000)** + library, no font; `WOOD_CG.BIN` = scenery; `MASH2.BIN` = sprites;
  `ONDATA/FIELD_S0/ST0_ENE` = record tables. The only strings in `0.BIN` are English
  system/filenames — no plaintext JP script.

**CORRECTED by player screenshots (title menu + first in-game dialogue):** the Japanese
text is a **kana FONT + SCRIPT**, NOT baked graphics. Menu shows crisp katakana
(`キロク1‑4`, `ニューゲームスタート`, `ボタンセット`, `サウンドモード`) beside the English
`HOUR MIN SEC`; in-game dialogue is crisp hiragana/katakana (`これでシュ！これがあれば…`).
So it is **not** the PIC codec, **not** the `80 00` RLE, and **not** plaintext SJIS
(a kana-density scan of every file found only noise — confirms a custom/non-SJIS encoding).

Text-system architecture (static): kana glyphs are **pre-rendered sprite tiles in VRAM**;
menu labels are stored as **lists of 16-bit glyph VRAM addresses**, not char codes — e.g.
the label table at `0x0604DD78` holds 12-byte entries like `00 80 | 0640 0644 0650 0654 |
00 00` (one VRAM address per glyph). ASCII labels (`TYPE 1`) instead go through
`FUN_06004E6C(x,y,attr,str)` (single-byte codes). The kana glyph **font bitmap** is loaded
to VDP1 VRAM at boot from a file not yet identified (ruled out: TITLEPIC, MASH/MASH2/MASH_W
= sprites, WOOD_CG = scenery, ONDATA/FIELD/ENE = tables, `0.BIN` = code+BSS+library).

**Status:** scoped but not cracked. The font location and the dialogue-script storage/
encoding are heavily pointer-dispatched + VRAM-address-indirect — the efficient crack is the
same as PIC: **one emulator data point** — when the dialogue box draws, watchpoint where the
script bytes are read and where the kana glyphs are sourced, then map the PC/PR to `0.c`.

(The `80 00` RLE STAGE#CG panels are cutscene *backgrounds*, confirmed compressed — raw
16bpp renders as noise; bit15 of each word discriminates control vs RGB555 literal, ~41%
control. Decoding them is a separate, lower-priority task from the dialogue text.)

**Emulator follow-up (menu redraw).** Write-watchpoint on the text layer `0x25E28000`
fired in the grid-*clear* routine `FUN_060051e8` (R1/R2=`25E28000`, fill 0x40×0x40), called
with `PR=0x0600CE2E` from an **unlabelled** menu-draw orchestrator at `~0x0600CE1C` (in the
gap after `FUN_0600c9a8`, reached only via fn-pointers), which then composes glyphs via the
text compositor `FUN_06004ee4`/`06004e94` (trace `06004ec4/ebe ×63`) using menu data near
`0x0604DE0C`. So the text path is confirmed (clear `25E28000` → compose glyphs → VDP1
sprite cmds) but the draw routine + font load are unlabelled/indirect. **Practical next
attempts (fresh session):** (a) to get the **font** — on reset, write-watchpoint VDP1 VRAM
glyph area (`25c03200`-ish, derived from the label table's `0x0640…<<3` addresses) to catch
the boot-time font copy and read its source; (b) to get the **script** — in-game, after the
grid-clear fires, continue (`R`) until a *different* PC writes `25E28000` (the glyph
compositor) and read its source pointer = decoded text buffer; walk `PR` up to the dialogue
routine. The unlabelled routines can also be read via `saturn_translate/sh2.py` disasm of
`0.BIN` at those addresses (no Ghidra needed).

**Static disasm of the title-menu orchestrator (`FUN @0x0600CDC0`, via `analysis/
disasm_orch.py`).** It: clears two VDP2 text layers (`FUN_060051e8`/`06004e94` at VRAM
offsets `0x28000`,`0x3C000`); draws the **menu background by calling `FUN_06008EF4`
(TITLEPIC load) then `FUN_06005CF8` (the PIC decoder we already own) on `0x20280000`** — so
the menu bg is just TITLEPIC; then calls menu-content/cursor helpers (`FUN_0600E60C` save-
slot check; `FUN_0600E6E0` draws digits as **glyph_addr = nibble*4 + base** where base =
`DAT_0600e772`). This pins the **font VRAM layout** (glyphs contiguous, index*4 stride) but
NOT the font-bitmap source. **Open linchpin:** where the kana/ASCII font bitmap is loaded
into VDP1 VRAM at boot — not in any file as plain 4bpp/1bpp (rendered → noise/code), so it's
compressed and/or built by indirect boot code. Cracking the dialogue requires this font
bitmap (needed to map glyph-addr → kana, hence to decode the custom-encoded script). Best
remaining moves: a **boot-time VDP1-VRAM write-watchpoint** to catch the font copy + read its
source, or a dedicated static trace of the boot/init sequence.

**Savestate VRAM dump (works — reusable).** Mednafen `.mc0` savestates are **gzip**; decompress →
**MDFNSS** format, fields are `[namelen:1][name][size:4 LE][data]`. Pulled VDP1 VRAM, VDP2 VRAM
(both `\x04VRAM`, 0x80000 each), CRAM (`\x04CRAM`, 0x1000), and VDP2 `RawRegs` (0x200) to
`analysis/vram_0.bin` / `vram_1.bin` / `cram.bin`. Scripts: `analysis/tileview.py` (4bpp tile grid),
`raw16.py` (RGB555), `render8pal.py` (8bpp+CRAM), `disasm_orch.py` (sh2 disasm w/ literal resolve).
From a **title-menu savestate**: VDP1 VRAM = command table + empty (kana are **not** VDP1 sprites
here). VDP2 regs `TVMD=0x0080 BGON=0x0700 CHCTLA=0x0132` ⇒ **NBG0 = 512×256 RGB555 bitmap** = the
title image. `MPABN0=0x3C3D`/`MPCDN0=0x3E3F` (NBG0 planes 0x3C–0x3F ×0x2000), `MPABN2=0x0A0B`/
`MPCDN2=0x0C0D` (NBG2 planes 0x0A–0x0D → VRAM `0x14000`+), `MPOFN=0x2000`. ⇒ **the kana text is an
NBG2 cell layer; pattern-name planes at VRAM `0x14000`+, glyph cells = the character data those
patterns reference.** Clean finish (next session): read NBG2 `PNCN2`/`CHCTLB` cell format + pattern
names at `0x14000`, follow to the character-data base, render the kana font cells → map glyph→kana →
decode the custom script. Savestate: `mednafen-1.32.1-win64/mcs/Steamgear Mash (Japan).710edf3c….mc0`.

### KANA FONT LOCATED + RENDERED (breakthrough)

The kana/kanji **font bitmap lives in VDP2 VRAM at `0x14000`** as **16×16 glyphs = 2×2
8×8 cells, 4bpp**, laid out so each glyph's 4 cells are 4 consecutive `0x20`-byte cells
(row-major: TL, TR, BL, BR). Rendering that region as 16×16 glyphs yields a **readable
kana/kanji font sheet** (`analysis/fs16b.png`) — ground-truth confirmed, the glyphs are
crisp katakana/hiragana/kanji. Tool: **`analysis/fontsheet.py`**
(`fontsheet.py <vram_off_hex> <ntiles> <palbank> <out.png> [cells_per_glyph_side] [scale]`),
e.g. `python analysis/fontsheet.py 14000 1024 0 out.png 2 3`.

**VDP2 registers (read from the savestate `RawRegs` field, big-endian, byte-offset by
VDP2 register address):** the doc above was missing two — now resolved:
- **`CHCTLB = 0x0000`** ⇒ NBG2 = 8×8 cells (`N2CHSZ=0`), **16-colour/4bpp** (`N2CHCN=0`).
- **`PNCN2 = 0x0000`** ⇒ **2-word pattern names** (bit15=0). `PLSZ=0x0000`.
  So `analysis/nbg_assemble.py`'s format assumptions (8×8/4bpp/2-word) were right; its
  garble was reading `0x14000` (which is the **font cell data**, not the pattern plane)
  as pattern names. The character number in a pattern is an absolute cell index → VRAM
  byte addr = `char * 0x20`.
- Savestate field offsets (reusable): two `\x04VRAM` fields (VDP1 then VDP2, 0x80000 ea),
  one `\x04CRAM` (0x1000), one `\x07RawRegs` (0x200) sitting just before the VDP2 VRAM.

**Savestate slot map** (`mednafen-1.32.1-win64/mcs/…710edf3c….mcN`): discriminate by
NBG0 — a 512×256 RGB555 **bitmap** (`CHCTLA` N0BMEN bit) is present only on the title/menu.
- **slot 0 / slot 1 (`.mc0`/`.mc1`)** = **main-menu** scene (`CHCTLA=0x0132`, NBG0 bitmap on)
  = `Screenshot 2026-06-07 063003.png`.
- **slot 2 (`.mc2`)** = **in-game dialogue box** (`CHCTLA=0x0101`, cell-based gameplay, no
  title bitmap) = `Screenshot 2026-06-09 155143.png` (`ミーナちゃん！いま、たすけにいくっシュ！！`).
- The font at `0x14000` is resident (33% fill) in **all three** states → same font decodes
  both menu (slot 1) and dialogue (slot 2).

**Next:** (1) catalogue the font sheet → build a **glyph-index → kana** table (glyph index =
`(char - 0x14000/0x20) / 4`); (2) read the NBG2 pattern-name **plane** (find its VRAM base —
*not* `0x14000`; that's the cells) for the menu labels and the dialogue box, map each
pattern's char# back through the glyph table to recover the on-screen strings; (3) locate
where the **script** is stored (the menu label tables near `0x0604DD78`/`0x0604DE0C` hold
glyph/VRAM addresses; the dialogue script's storage/encoding still to be pinned — slot 2 +
a write-watchpoint on the dialogue text layer is the fast path). Then translate → draw an
English 16×16 font into the `0x14000` cells → reinsert.

### TEXT PIPELINE FULLY MAPPED (2026-06-09 pm, VDP1-write watchpoint)

A write-watchpoint on **VDP1 VRAM `0x25C00000`** during a save/continue screen (showing
`キロク1`, `タイトルにもどる`, `HOUR MIN SEC`) cracked the on-screen text path. **Text is
VDP1 sprites**, and the pipeline is a normal text-drawing API:

- **`FUN_06004e6c(x, y, color, char *str)`** = the **text-draw API**: walks a
  **null-terminated byte string**, writing `*str + color<<16` per char into a text buffer
  (`DAT_06004edc + y*0x100 + x*4`). ⇒ **the script is null-terminated strings in a custom
  single-byte charset** (one byte = one glyph code). Standard, very translatable.
- **`FUN_06004ee4(buf)`** = turns that buffer into **VDP1 sprite commands**: per char it
  looks up the glyph's VDP1-VRAM address via tables `PTR_DAT_06004fb0/fb4` (indexed by the
  char code) and writes 4 sub-cell command words (the 16×16 = 4×8×8). This is the
  **char-code → glyph-VRAM-address** map = the charset table.
- **`FUN_06004840`** = the higher-level text routine (caught at PC `0x060049xx`, PR
  `0x06004638`): builds VDP1 command tables at `0x25C00000+` and copies glyph bitmaps into
  VDP1 VRAM (e.g. `FUN_06004186` copying HWRAM `0x06076754` → VDP1 VRAM `0x25C10400`). Glyph
  source data sits in HWRAM (`0x06076754`, `0x06080294/ac8/bc8`).
- **Charset is NOT plain ASCII** at glyph-index level (font glyph slots 32–95 are kana, see
  `analysis/ascii_range` render); `HOUR/MIN/SEC` proves Latin glyphs exist, reached via the
  `…06004fb4` lookup. Pin the exact byte→glyph map by either dumping `DAT_06004fb4` or
  catching `FUN_06004e6c`'s `str` arg live.

**Route-B font tooling BUILT + verified (`saturn_translate/sgfont.py`):** the glyph codec
`decode_glyph`/`encode_glyph` round-trips **byte-exact on all 142 populated font glyphs**
(test `test_sgfont_*`), and `english_glyph(ch)` renders ASCII as game-format 16×16 glyphs
(stroke = nibble 3) — proven legible in `analysis/english_demo.png` (START / OPTIONS /
STEAMGEAR MASH). So we can mint English glyphs in the exact font format on demand.

**Remaining for a playable patch:** (1) pin the byte→glyph **charset** (`DAT_06004fb4` dump
or live `str` capture); (2) locate the **script strings** (byte arrays, referenced via
fn-pointer dispatch — find via the charset + data scan, or a read-watchpoint on a known
string); (3) translate → emit English byte strings + inject English glyphs (sgfont) at the
needed codes → reinsert. Codec/re-encode is NOT required for route B (overwrite glyphs +
strings in place / via hook).

### SCRIPT = PLAIN ASCII, EDITABLE IN 0.BIN (2026-06-09 pm — the payoff)

A PC breakpoint at **`FUN_06004e6c`** (`0x06004e6c`, the text-draw API) caught 3 calls with
`R4=x, R5=y, R6=color, R7=str` — `R7` = `0x06049660/78/90`. Reading those from WorkRAMH and
**byte-swapping 16-bit words** (Mednafen stores work-RAM as host-LE uint16; real Saturn
memory is normal ASCII) gives **plain ASCII strings**. The engine **renders ASCII English
natively** — the font already has Latin glyphs (e.g. it shows `HOUR MIN SEC`, `TYPE 1`).

**The UI string table is in `0.BIN` at file offset `0x4565F`** (plain ASCII, un-swapped;
also Track01 `0x5C46F`). `0.BIN` loads to `0x06004000` ⇒ **`0.BIN` offset = HWRAM addr −
`0x06004000`** (`0x45678` → `0x06049678` = the captured R7). Strings there:
`"# TAKARA Co.,Ltd. 1995"`, `"PROGRAMMED BY TAMSOFT"`, `"HIT ANY KEY TO START"`,
`"HOUR   MIN   SEC"`, `"NEW"`, `"GAME"`, `"TYPE 1/2/3"`. Asset filename table just above
(`ONDATA.BIN`, `TITLEPIC.BIN`, `*.CPK`, …). `0.BIN` has **19,429 ASCII runs ≥3 chars** total.

⇒ **Translation pathway (no codec work):** English UI text is directly editable ASCII in
`0.BIN`; Japanese (kana-coded) strings are replaced with ASCII English the same way. Reinsert
via the cleanest path = ship the edited `0.BIN` as a **Sega Saturn Patcher `.ssp`** (rebuilds
EDC/ECC), exactly like the Waialae `A.BIN` flow. Glyph minting for any missing Latin/styled
letters is handled by `saturn_translate/sgfont.py`. **This removes the last blocker — the
game is fully translatable by editing ASCII strings in `0.BIN`.**

**Next:** dump 0.BIN's string table region into a translation worklist (English strings to
rewrite + locate the kana strings), edit, repack to `.ssp`, test on emulator/hardware.

### CORRECTION (2026-06-09 pm): text is **VDP1 sprites**, not a VDP2 cell layer

The "kana = NBG2 cell layer" premise above is **wrong** — disproven by exhaustively
checking both savestates (slot1 menu, slot2 dialogue). Findings:

- **VDP2 register read was byte-swapped.** `RawRegs` stores regs **little-endian**; the
  earlier big-endian read mis-set bits. Correct LE values: `TVMD=0x8000` (DISP on),
  `BGON=0x0007` (N0/N1/N2 on), slot1 `CHCTLA=0x3201` (**N0BMEN=0** — NBG0 is *not* a bitmap;
  the old "512×256 title bitmap" discriminator was an endianness artifact). N0/N1 char size
  `=16×16` on the text scenes (`N0CHSZ`/`N1CHSZ`=1), N2`=8×8`.
- **Plane bases (LE, Yabause multiplier):** page mult = `0x1000` (16×16/2-word), `0x4000`
  (8×8/2-word). slot1: N0=0x3C000, N2=0x28000. slot2: N0=0x3C000, N1 map=0x38000–0x3BFFF.
  **Rendered every enabled plane** (`analysis/render_plane.py`, 16×16 char→4-cell decode,
  `--gray` mode): they are **backgrounds + the dialogue-box fill tiles**, NOT kana.
- **No VDP2 plane references the `0x14000` font** (scanned all planes for char#→`0x14000`
  cells, in both 8×8-direct and 16×16/×4 encodings: none).
- **No copies of the font anywhere** in slot2: searched VDP1 *and* VDP2 VRAM for each
  `0x14000` glyph in **both** VDP2 cell-format *and* VDP1 16×16 raster-format (16 rows×8 B):
  **zero** matches. VDP1 command lists are transient/idle in the savestates.

⇒ Consistent model: **all on-screen text is drawn as VDP1 sprites, with glyph bitmaps
SCU-DMA'd from the `0x14000` VDP2 master font per draw** — nothing persists in a static
savestate, which is why no tilemap and no copies exist. The `0x14000` block is the single
**master font source** (`main stroke = nibble 3`; outline = nibble 4; ~142 kana glyphs
0–143, then kanji to ~430). Reinsertion plan therefore = **(a) edit the `0x14000` master
font** (draw English glyphs) + **(b) find the script** (string/glyph-index data in loaded
game files / work RAM, e.g. `0.BIN`), *not* a VRAM tilemap edit. New tools this session:
`analysis/render_plane.py`, `analysis/font_gray.py` (binary/stroke-only kana sheets:
`font_gray.py <slot> <start> <count> <out.png> [scale] [perrow]`).

**Fast next step (per runbook):** live Mednafen watchpoint at dialogue draw to catch the
VDP1 text routine + its source-glyph pointer (the script). Static analysis has bottomed out.

### FONT CODEC FOUND via watchpoint (2026-06-09 pm) — it's the SOLVED exp-Golomb codec

Live Mednafen **write-watchpoint on VDP2 VRAM `0x25E14000`** (font region) at boot, refined
to a deep offset `0x25E17200` (to skip a sprite-coord setup table `FUN_06011818` writes at the
very start of `0x14000`). Result — the font pipeline:

1. The font is **exp-Golomb-decompressed into LWRAM** (staged at `~0x00297614`). The decoder
   uses the shared bit-readers **`FUN_060041a0`** (read-N-bits, MSB-first, 32-bit word stream:
   accumulator `DAT_060041b0`, bitcount `…b4`, stream ptr `…b8`) and **`FUN_060041dc`**
   (exp-Golomb: count leading 1s = `uVar5`, read that many more bits = `uVar6`, return
   `uVar6 + (uVar5<<1|1)`). These are the **same `FUN_060041dc`/`FUN_060041a0` the PIC codec
   uses** → the font is the **already-solved codec family** (`saturn_translate/pic.py`), NOT a
   new scheme. (The orchestrator that drives them + sets the stream pointer lives in the
   unnamed region `~0x06004210–0x060046e0`, between `FUN_060041dc` and `FUN_060046e0`.)
2. The decompressed LWRAM font is then **plain-copied LWRAM→VRAM** by **`FUN_06004186`**
   (`mov.l @r5+,r1; mov.l r1,@r4` loop; caught at PC `0x06004192`, R4=`25E17200` dest,
   R5=`00297614` src, R6=count, PR=`060042EA`). `FUN_06004196` is the same as a 4-byte copy.

**Watchpoint registers (font copy break):** PC `06004192`, PR `060042EA`, R4 `25E17200`
(dest VRAM), R5 `00297614` (src LWRAM), R6 = word count. CHCTLA `3201`, CHCTLB `0000`.

**Patch implications:** two viable routes now that the codec is known —
- **(A) Offline file patch:** locate the compressed font blob (the bitstream `FUN_060041a0`
  reads — trace the orchestrator at `~0x06004210` or watch its stream-source reads), decode
  with `pic.py`'s reader, draw English glyphs, **re-encode** with a matching exp-Golomb
  encoder, patch the blob in its file. Needs an encoder (decoder exists).
- **(B) Code-hook (no re-encode):** hook right after `FUN_06004186` (LWRAM→VRAM copy) or
  overwrite the LWRAM staging post-decompress, blitting English 16×16 glyph bitmaps from an
  appended table over the kana. Sidesteps the codec entirely; only needs free space + a hook.
Route (B) is the lower-risk path to a first playable build; route (A) is cleaner long-term.

### Decompressor + script table located (2026-06-09 pm, watchpoints A/B)

**Font decompressor (watchpoint on LWRAM staging `0x0029A000`, past the BIOS RAM-clear):**
PC `0x060E4864` inside **`FUN_060e4704`** — a **generic multi-mode graphics transfer/codec
engine** (builds a descriptor from stride/dim params; `FUN_060e488c` = its strided copy;
`FUN_060e492a` = the per-call dispatcher that picks a codec handler via fn-pointer tables
`PTR_PTR_060e49a0`/`…4a40` on a mode field at `+0xa4`). The **exp-Golomb readers
`FUN_060041dc/a0` are one of its modes.** Registers at break: R12 `0x06084704` = source
descriptor/blob (**`0.BIN` file offset ~`0x80704`**, load base `0x06004000`), R13
`0x25818000` (VDP2 VRAM `0x18000`, font region), R14 `0x2029A000` (LWRAM dest, uncached),
R9 `0xBB8`/R6 `0x200` counts, PR `0x060E49F4` (caller `FUN_060e492a`). ⇒ font is
decompressed by this engine into LWRAM `~0x297614`, then plain-copied to VRAM by
`FUN_06004186`. **Implication:** the codec is embedded in a complex pluggable engine, so
**route (B) code-hook is the practical patch path** (hook `FUN_06004186` / overwrite the
LWRAM staging post-decompress); route (A) would need to re-implement this engine's encoder.

**Menu script/label table (WorkRAML/H located: `WorkRAML`=LWRAM `0x00200000`,
`WorkRAMH`=HWRAM `0x06000000`, each 1 MB):** at **WorkRAMH `0x4DD60`** (= `0x0604DD60`,
matching the recon-flagged `~0x0604DD78`) — a table of menu strings, each
`8000 <glyph-refs…> 0000` (start/end markers), e.g. label 1 = `8000 9803 4804 7804 1404 0000`
(~7 labels, 4 glyph-refs each; refs repeat across labels = shared kana). The 16-bit
ref→glyph encoding is decoded by the menu-draw routine — **watchpoint B** (read-bp on
`0x0604DD62`, move cursor to trigger) will catch that routine and crack the encoding =
**the script**. This is the remaining piece for meaningful English (vs. raw glyph-swap).

### Mednafen debugger key reference (for next session)
`ALT+D` open debugger · `ALT+1` CPU view · `SHIFT+R` read bp · `SHIFT+W` write bp ·
`Space` toggle PC bp on selected disasm address · `S` step · `R` run. Breakpoints take
single addrs or ranges (`280000-29ffff`); clear by emptying the editor field.

## BUTTON-config (controller) screen — graphics, not text (2026-06-11)

The option→BUTTON (controller config) screen shows a gamepad diagram + a vertical
list of kanji action labels (ショット/ジャンプ/ウエポン/武器選択1/武器選択2/視点変更/加速),
button letters A-Z/L/R + "TYPE 1", "RESET" (already English), and a blue bottom bar
"スタートボタン＝決定". The "BUTTON"/"RESET" English render proves the text patch is loaded;
the kanji action labels are **baked graphics, not text strings** (no Shift-JIS on disc, no
glyph-string copy, kanji 武器/選択/視点 absent from the text font, 決定 glyph-pair appears
only in the save-prompt legend).

**Asset located:** the screen loads **ONDATA.BIN** (LBA 39914, 357,136 B) via
`FUN_06008e04` (raw-read, the option/config sprite-cell data) plus **WOOD_CG.BIN**
(LBA 40089, 55,776 B) via `FUN_06008e62` (`0x1c` sectors raw = the wood-texture menu
background). cf. `FUN_06008ef4`=TITLEPIC.

**ONDATA.BIN format = 4bpp, 8×8 tiles, leading RGB555 palette (NOT compressed):**
- First ~8 halfwords = a header/coordinate (sprite-cell) table: `0028 0010 0028 0410
  002a 0410 002a 1310 …` (looks like (size/addr) records).
- Then RGB555 palette(s): a grey ramp `8220 ffff f7bd ef7b … 8000` (high bit = colour-used),
  followed by more 16-colour banks.
- Then 4bpp indexed pixel data (nibble indices: `1111 aaaa 9999 bbbb …`).
- A linear width-N render is scrambled (Saturn 8×8 tiling); **de-tiled** at 8×8 the glyph
  sheet is legible — katakana/kanji glyphs visible (`analysis/tileview.py ONDATA.BIN 0x30
  0x18000 8 8 32 out.png`). So the labels are tile-assembled from this sheet.

**To translate (route): repaint, not re-script.** Steps: (1) emulator watchpoint when the
BUTTON screen draws → identify which ONDATA tiles + the header-table cells place each of
the 6 labels (same fast-path that cracked the PIC codec); (2) draw English 8×8 4bpp glyphs
in the same palette; (3) overwrite those tiles in ONDATA.BIN; (4) reinject ONDATA (separate
file → clean) + fix ECC, same as 0.BIN. **Fit constraint:** each kanji label is only a few
tiles wide (武器選択=4 tiles); English must abbreviate to fit the fixed sprite positions
(SHOT / JUMP / WEAPON / WPN1 / WPN2 / VIEW / DASH). Extracted copies: `analysis/ONDATA.BIN`,
`analysis/WOOD_CG.BIN` (git-ignored, re-extract from disc).

## CORRECTION (2026-06-12): BUTTON-config labels ARE TEXT in 0.BIN, not graphics

Savestate forensics (slot-0 save on the BUTTON screen, `mcs/...2758e8c9....mc0`)
overturned the "graphics" conclusion. The ONDATA font is DMA'd into **VDP2 VRAM base 0**
as an 8×8 4bpp tileset where **char-number == glyph index** (ASCII region: tile = ASCII,
so 'A'=65…; verified 'S'@VRAM 0xA60, '0'@0x600). The controller-config action labels are
drawn by the **same wide-text routine** as the main menu (`FUN_06005020`, code = glyph*4,
**big-endian** in 0.BIN), from the same null-terminated string table that holds
" NEW GAME"/" BUTTON"/" SOUND". So they are fully patchable as text — no tile repaint.

**Label table in 0.BIN (BE, value = glyph*4, 0x00 terminator, ~5-char slots):**
- 0x49D38 (≤7) — 7 glyphs `[227,211,286,247,244,231,246]` (unread)
- 0x49D4A (≤4, after icon glyphs) — `[229,278,243,246]`  (likely ショット / SHOT)
- 0x49D60 (≤5) — `[230,274,286,261]` = **ジャンプ → JUMP** (confirmed)
- 0x49D6C (≤5) — `[211,214,267,286]` = **ウエポン → WEAPON** (confirmed; slot fits 5 → "WEPON")
- 0x49D78 (≤5) — `[400,401,404,405]` kanji (shares suffix 404,405 with D84)
- 0x49D84 (≤5) — `[402,403,404,405]` kanji (shares prefix 402,403 with D90)
- 0x49D90 (≤5) — `[402,403,406,407]` kanji
- 0x49D9C (≤5) — `[755,756,408,409]` kanji
- 0x49DA8 (≤5) — `RESET` (already ASCII)
- 0x49DB4 — `[…]=決定` bottom bar (スタートボタン＝決定)

Glyph factoring of the 4 kanji labels: P1=400,401 / P2=402,403 / S1=404,405(=選択?) /
S2=406,407(=変更?) → D78=P1+S1, D84=P2+S1, D90=P2+S2, D9C=755,756,408,409. So they are
"○○選択 / △△選択 / △△変更 / ????" — NOT 武器選択1・2 as earlier guessed (those would share
prefix). Exact kanji need a human read (8×8 kanji are sub-legible to the renderer).

**To translate:** overwrite each slot with ≤5-char English (glyph*4 BE, ASCII tiles already
in VRAM), via the menu pipeline in `analysis/build_menu_patch.py` (MENU dict), then reinject
0.BIN + ECC as usual. Render of all labels: `analysis/button_words.png`,
`analysis/glyph_grid.png`. Mednafen VDP2 VRAM in savestate is **byte-swapped** (16-bit);
HWRAM blob too (read LE there, but 0.BIN on disc is BE).

## BUTTON-config screen LAYOUT — render code, columns, and how to move them (2026-06-12)

The controller-config screen is drawn by a render function at **0.BIN file ~0xA0E0–0xA4E4
(HWRAM 0x0600E0E0–0x0600E4E4)**. It composites **two VDP2 layers** with different tile
widths, which is why earlier pixel intuition kept failing:
- **Wide-text layer** (`FUN_06005020`, 5 args `x,y,color,?,str`; map stride 0x80 = 32
  tiles/row, ~16px tiles). Draws the **action labels** at column **x=9** and the per-row
  **button letters** (A/B/C/X/Y/Z…) at column **x=16**.
- **ASCII layer** (`FUN_06004e6c(x,y,color,str)`; map base `DAT_06004edc`, stride 0x100 = 64
  tiles/row, ~8px tiles). Draws the **row numbers `1/2/3`** at column **x=28** and the
  **`TYPE 1/2/3`** selector at column **x=30**.

On-screen left→right: `label(x9 wide)` · `number(x28 ascii)` · `button-letter(x16 wide)` ·
(`TYPE` below). The button letter is the rightmost; the row number sits between label and
button and is what collides with an over-long label (the `1/2/3`-on-`L` bug).

**Each column's x is a HARDCODED immediate** `mov #N,r4` (SH-2 bytes `E4 NN`) sitting in the
delay slot of the draw `jsr`. So a column moves with one-byte edits — no relocation, no
table. Draw sites (0.BIN file offsets):
- button letters (x=16, `E4 10`): `0xA0EE,0xA212,0xA244,0xA29C,0xA2CE,0xA30E,0xA364,0xA3A4,0xA3D6`
- row numbers `1/2/3` (x=28, `E4 1C`): `0xA2DE,0xA31E,0xA374` — str = `"1"/"2"/"3"` at
  `0x060496E0/E4/E8`, drawn via the ASCII routine.
- `TYPE 1/2/3` (x=30, `E4 1E`): `0xA40C,0xA448,0xA45A` — str = `"TYPE n"` at `0x060496EC/F4/FC`.
- (also seen: an x=28 highlight test `cmp/eq #N` selecting r9 vs r10 = the selected-row color.)

Verified the button-letter str arg is a per-row assignment lookup `r8 + rowval*4` (rowval =
`[r12+offset]`), confirming x=16 == the assignable button column. **To widen the label field,
push the right cluster out together** (keep order: numbers/TYPE move in ascii units = half a
wide tile). `build_menu_patch.py` does this via `BTN_COL_MOVES` — current: buttons x=16→19,
numbers x=28→34, TYPE x=30→36. Each edit asserts the expected `E4 NN` before patching.

**Button → action mapping (from in-game testing, user-supplied):** A=ショット shoot (hold=
charge), B=ジャンプ jump, C=ウエポン *special* weapon (blue energy bar), X/Z=武器選択 change
special weapon (two rows, numbered 1/2), Y=select bomb, L=移動選択 use skill, R=移動決定
swap/use special skill. So the kanji **移動○○ rows are the SKILL buttons, not "move"** — the
literal kanji ("movement select/decide") do not match the in-game function. Final English
labels (relocated into resident gaps, see `BTN_RELOC`): WEAPON / **CHG WPN** (X/Z) / **SKILL**
(L) / **SWAP SKL** (R) / D-PAD / RESET, plus in-place SHOT / JUMP / "START = OK".

## Text-location map + how to search for kanji (2026-06-12)

Steamgear has **two on-screen text systems**, with two different scan methods:

| Text | 0.BIN location | Encoding | How to find it |
|------|----------------|----------|----------------|
| Menu / button-config labels | `0x45xxx`, `0x49C90`–`0x49DB4` | wide text, code = glyph*4 BE, **`0x0000`-terminated** | `analysis/find_kanji.py` |
| Stage hints + **Mina cutscene** (story) | **`0x5AF40`–`0x5BBB2`** | dialogue font (glyph = ASCII+438), fixed-width lines, **`0xFFFE`/`0xFFFF`** line delimiters | dialogue worklist builder (`scan_dialogue` / `apply_worklist_file`) |

**`analysis/find_kanji.py`** scans any disc or extracted file for wide-text runs that contain
non-ASCII glyphs (kana/kanji = glyph outside `0x20..0x7E` and the dialogue range `470..563`),
filtering out record-table/tilemap false positives (FIELD_S*.BIN, ST*_ENE etc. satisfy
`code%4==0` but are structured data, rejected by a separator-cadence + low-entropy test).
Result: **all genuine wide-text is in 0.BIN's label block** — no residual story text hides in
the other files (the .CPK movies and STAGE*CG carry any other text as baked graphics).

**The Mina cutscene is the story driver** (Mash chases the kidnapped Mina through the bosses
into space): worklist lines 76–101, file offsets `0x5B87E`–`0x5BBB2`. It is NOT `0x0000`-
terminated, so `find_kanji.py` skips it — use the dialogue path. Dialogue status: **86/102**
lines translated (the untranslated remainder are blank/system/save-prompt lines).
