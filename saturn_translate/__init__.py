"""Saturn Translate MCP — Japanese→English translation toolkit for Sega Saturn games.

Public surface is the MCP server in :mod:`saturn_translate.server`. The modules
below implement the Saturn-specific primitives that neither ghidra-mcp nor
textra-ja-to-en-mcp provide:

* :mod:`saturn_translate.iso`      — read/list/extract files from Saturn CD images
* :mod:`saturn_translate.sjis`     — scan binaries for Shift-JIS Japanese text
* :mod:`saturn_translate.pointers` — locate SH-2 (big-endian) pointer tables
* :mod:`saturn_translate.reinsert` — write translated text back, pointer-safe
* :mod:`saturn_translate.project`  — JSON translation-table project format
* :mod:`saturn_translate.textra`   — thin client for the Textra JA→EN API
* :mod:`saturn_translate.ghidra`   — thin client for the GhidraMCP HTTP bridge
"""

__version__ = "0.1.0"
