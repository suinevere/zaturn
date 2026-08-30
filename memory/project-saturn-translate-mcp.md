---
name: project-saturn-translate-mcp
description: The saturn-translate-mcp toolkit project — what it is, layout, status
metadata:
  type: project
---

Project "AI Sega Saturn Translation MCP Server". A Python FastMCP server + CLI for
translating/patching Sega Saturn games, built to orchestrate ghidra-mcp and
textra-ja-to-en-mcp.

Package `saturn_translate/`: `iso.py` (ISO9660, auto-detects 2048/2352),
`sjis.py` (Shift-JIS scan + pointer-anchored decode), `pointers.py` (big-endian
SH-2 pointer tables), `reinsert.py`, `build.py`, `project.py`, `triage.py`
(feasibility ranking), `langpatch.py` (language-selector flip), `vcdiff.py`
(multi-window xdelta encoder), `ips.py`, `ecc.py` (Mode-1 EDC/ECC), `sh2.py`
(SH-2 disassembler), `textra.py`, `ghidra.py`, `server.py` (MCP tools), `cli.py`.
CLI commands: `feasibility, list, extract, scan, tables, project, translate,
apply, pack, force-language --fix-ecc, disasm`. Tests in `tests/test_core.py`
(13 passing, run via a clean /tmp copy — see [[feedback-mnt-mirror-and-verification]]).

**First real result:** activated English on *Waialae no Kiseki - Extra 36 Holes*
via a 1-pointer `A.BIN` flip — see [[reference-tande-english-activation]]. Patches
+ patched disc live in `game_patched/`; original discs in `game_originals/`
(also Augusta 3, Jun Classic, Bug!, Steamgear Mash, Oh-chan no Oekaki Logic).
Distribution facts in [[reference-saturn-patch-distribution]]. Current deep effort:
Steamgear Mash translation — see [[reference-steamgear-mash]].
