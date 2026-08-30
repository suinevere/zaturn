---
name: reference-saturn-game-survey
description: Per-game verdicts for the Saturn discs surveyed (game_originals/)
metadata:
  type: reference
---

Translation feasibility verdicts for discs in `game_originals/`:

- **Waialae no Kiseki** — full English build on disc; 1-byte loader flip. DONE.
  See [[reference-tande-english-activation]].
- **Augusta 3** — only 4 English Cinepak videos behind a BIOS-language toggle; no
  English UI. Not worth patching.
- **Jun Classic** — has `_E.APC` (caddie audio) + `_E.GDT` (graphics) `_E`/`_J`
  pairs; not yet fully characterized.
- **Bug! (JP, "Bug Jump shite…")** — already English. Western game; Sega's JP release
  kept the English UI (JP `0.BIN` has identical English menu strings to US). 98/125
  files byte-identical to the US disc; the 26 differing files are revised sprite
  graphics, NOT a Japanese-text layer. No injection needed/feasible.
- **Steamgear Mash (Japan)** — Japan-only (T-103), real from-scratch translation.
  See [[reference-steamgear-mash]].
- **Oh-chan no Oekaki Logic** — not yet examined.
