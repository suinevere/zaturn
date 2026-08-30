---
name: reference-saturn-patch-distribution
description: EDC/ECC, xdelta/IPS, and Sega Saturn Patcher (.ssp) facts for distributing Saturn patches
metadata:
  type: reference
---

**EDC/ECC:** patching a byte in a MODE1/2352 image invalidates that sector's
EDC/ECC. Emulators and lenient ODEs (Fenrir, Saroo) ignore it; **Terraonion MODE
freezes at the SEGA logo** because of it. Fix = recompute EDC/ECC for the changed
sector (canonical ecmtools/ECMA-130 algorithm, implemented in
`saturn_translate/ecc.py`; validated by reproducing untouched sectors' own ECC
byte-for-byte). Only the changed sector(s) need fixing.

**Patch formats:**
- **xdelta/VCDIFF** (`saturn_translate/vcdiff.py`, multi-window — single window over
  a 500 MB disc is rejected by xdelta3's `XD3_HARDMAXWINSIZE`): applied with
  DeltaPatcher; source must be the exact original image.
- **IPS** (`saturn_translate/ips.py`): 24-bit offsets, only for edits in the low
  16 MiB (fine for boot/loader-area patches).
- **`.ssp` (Sega Saturn Patcher, knight0fdragon):** a renamed ZIP of **only the
  changed files** + metadata (`version.txt`). It diffs at the **file** level and
  **rebuilds the disc + regenerates all EDC/ECC itself** — so the ECC problem
  disappears and the patch is the cleanest distribution route. Keep it small by
  including ONLY the changed file (e.g. Waialae = just `A.BIN`, 228 KB). Build
  against a vanilla ISO. Huge `.ssp` = you included the whole disc/track.

**Pseudo Saturn Kai cheat codes:** custom Action Replay code entry is **debug build
only**; the standard/lite firmware runs built-in codes only. Cheats need the CWX
loader (JHL loader disables them). Codes are region-specific.
