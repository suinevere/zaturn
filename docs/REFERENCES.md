# Reference material

Background and source material for Sega Saturn Japanese→English translation work.
The SegaXtreme resources below are the primary community knowledge base; the
released patches are useful as worked examples of how real Saturn text banks,
pointer tables and fonts are laid out.

## SegaXtreme

- Translations category (88+ Saturn patches):
  https://segaxtreme.net/resources/categories/translations.9/
- All resources (tools, docs, compilers):
  https://segaxtreme.net/resources/
- Saturn tooling worth knowing: Graphics Tools, Audio Tools, Video Tools and
  Other Tools categories under https://segaxtreme.net/resources/categories/sega-saturn.2/

### Worked examples (study these for layout conventions)

Mature, well-documented Saturn translation projects whose patches and threads
illustrate pointer-table and font handling in practice:

- Sakura Wars 2 — English Translation (TrekkiesUnite118) — large script game,
  good reference for scenario text banks and variable-width fonts.
- Wizardry VI & VII Complete — English Translation Patch (Remisse).
- Dungeons & Dragons: Tower of Doom — English Patch (Lirinica).
- Mobile Suit Gundam (English) (Shadowmask).
- Sword & Sorcery — English Patch (Rasputin3000) — built from the original
  Japanese script.
- Psychic Killer Taromaru (Shinrei Jusatsushi Taromaru) — English Patch.

(See the Translations category page for the current list and downloads.)

## Saturn technical notes relevant to this toolkit

- **CPU / endianness:** Two Hitachi SH-2 cores, **big-endian**. All multi-byte
  pointers and integers in game data are most-significant-byte first. This is
  why `saturn_translate.pointers` defaults to big-endian 32-bit reads.
- **Text encoding:** Shift-JIS (CP932) is near-universal for Japanese text.
  Some games use a *custom* font where byte values index a private glyph table;
  for those, supply a custom `encoder` to the reinsertion functions and map
  ASCII to the game's 1-byte glyph range.
- **Pointer bases:** A pointer usually stores `base + file_offset`. Common bases
  are `0x06000000` (High Work RAM), work-RAM areas around `0x00200000`, or `0`
  for file-relative tables. The toolkit probes a default set and infers the most
  likely base from the data.
- **Disc layout:** ISO9660 filesystem. Images are distributed either as plain
  2048-byte-sector `.iso` or as raw 2352-byte-sector Mode-1 `.bin` (with a
  cue sheet). The reader auto-detects both via the `SEGA SEGASATURN` IP header
  and the `CD001` identifier at sector 16.
- **Size budget:** Replacing a file in place only works while the new file fits
  in the sectors already allocated to it. Larger English text must be made to
  fit (abbreviation, VWF) or the bank relocated to free space; growing a file's
  extent in-image would overwrite the next file. `saturn_translate.build`
  enforces this and refuses unsafe writes.

## Upstream MCP servers orchestrated here

- **ghidra-mcp** — https://github.com/bethington/ghidra-mcp — Ghidra reverse
  engineering over MCP (string extraction, decompilation, xrefs, memory). Used
  for text that is referenced from SH-2 code rather than a flat pointer table.
- **textra-ja-to-en-mcp** — https://github.com/hokupod/textra-ja-to-en-mcp —
  JA→EN machine translation via the Textra API.
- **Textra (機械翻訳)** — https://mt-auto-minhon-mlt.ucri.jgn-x.jp/ — register for
  API credentials (key, secret, login name).
