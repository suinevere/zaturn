---
name: reference-tande-english-activation
description: How T&E Soft Saturn golf games store language assets, and how to force English
metadata:
  type: reference
---

**T&E Soft Saturn golf engine** (maker T-114). Common root files: `A.BIN` (boot
dispatcher, loads at HWRAM `0x06054000`), `EXEC.BIN`, `GOLF.BIN`, `SETUP.BIN`,
`TUTORIAL.BIN`, `GUIDE.BIN`, `DEMO.BIN`, plus `*.DAT`. Discs are MODE1/2352, SH-2
**big-endian**, text uses a **custom font** (private-use glyphs), so plain
Shift-JIS scanning of the overlays returns garbage.

**Two language mechanisms seen:**
1. **Full dual build (Waialae no Kiseki, T-11402G):** every overlay exists twice —
   JP (`GOLF.BIN`…) and English (`EGOLF.BIN`…), chosen by a 3-stage loader
   `A.BIN → LOAD.BIN/ELOAD.BIN → EXEC.BIN/EEXEC.BIN → overlays`. `A.BIN` holds a
   "Please Select Language" menu and a big-endian pointer pair
   `[ptr→LOAD.BIN][ptr→ELOAD.BIN]`. **Forcing English = flip the default pointer to
   ELOAD.BIN.** For Waialae this is 4 bytes at image offset `0x1C008`
   (`06054748→06054754`); confirmed working on emulator + Saroo.
2. **Asset-pair toggle (Augusta 3 = T-11401G/1995, Jun Classic = T-11403G/1997):**
   NO English code build, NO `LOAD.BIN/ELOAD.BIN`, NO language menu. But media
   files come in `_E`/`_J` pairs (Augusta: `GOLF/1M_E.CPK` video; Jun Classic:
   `GOLF/CADDIE/0M_E.APC` audio + `_E.GDT` graphics). A runtime flag picks the
   suffix. Only Waialae had a Western "True Golf Classics" counterpart → only it
   got a full English build. The others would need the full translation pipeline
   (custom-font RE + text extract/translate/reinsert) for UI text.

   **Augusta 3 verdict (investigated):** the letter = `'E' + system_language`
   (BIOS language 0=Eng..5=JP); `GOLF.BIN` builds `%dM_%c.CPK`, `DEMO.BIN` indexes
   a letter table. BUT the ONLY `_E` assets are **4 Cinepak videos**
   (`GOLF/1M_E.CPK`..`4M_E.CPK`, ~1.4 MB) — no English text/graphics/audio. So
   "forcing English" yields only English movie clips, not an English game. Set the
   emulator BIOS language to English to see them (no patch). Not worth a patch.
   Jun Classic still unverified — it has `_E.APC` (audio) + `_E.GDT` (graphics)
   pairs, possibly more substantial than Augusta.

**Detection:** `force-language` tool in [[project-saturn-translate-mcp]] finds the
LOAD/ELOAD pointer pair by requiring the two pointers be adjacent AND the inferred
load base be page-aligned (rejects the menu pointer table, which is also 0xC-spaced
and mimics the loader pair). Confirm uniqueness: the JP loader pointer value should
occur exactly once in the image.

**Distribution / hardware notes:** see [[reference-saturn-patch-distribution]].
