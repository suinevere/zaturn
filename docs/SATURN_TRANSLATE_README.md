# Saturn Translate MCP

An MCP (Model Context Protocol) server for translating **Sega Saturn** games from
Japanese to English. It provides the Saturn-specific tooling that general-purpose
servers lack — reading the disc image, finding Shift-JIS text, mapping the SH-2
pointer tables that reference it, writing English back without corrupting those
pointers, and patching the file back into the disc — and orchestrates two upstream
servers for the parts they already do well:

- **[ghidra-mcp](https://github.com/bethington/ghidra-mcp)** — SH-2 binary reverse
  engineering, for text referenced from code rather than a flat table.
- **[textra-ja-to-en-mcp](https://github.com/hokupod/textra-ja-to-en-mcp)** —
  Japanese→English machine translation via the Textra API.

> Use this only with games you legally own, on your own disc images. This is a
> translation/ROM-hacking toolkit, not a distribution tool.

## How it fits together

```
                ┌─────────────────────────────┐
   MCP client   │      saturn-translate        │   Saturn-specific tools:
 (Claude, etc.) │  (this server, Python/MCP)   │   ISO • Shift-JIS • pointers
        ▲       └───────┬──────────────┬────────┘   • reinsert • repack
        │               │              │
        │        HTTP   │              │  subprocess / shared Textra API
        │               ▼              ▼
        │       ┌──────────────┐  ┌──────────────────────┐
        └──────▶│  ghidra-mcp  │  │ textra-ja-to-en-mcp   │
                │  (:8089)     │  │  (Textra JA→EN)       │
                └──────────────┘  └──────────────────────┘
```

`saturn-translate` owns the workflow and the Saturn file formats; it calls
ghidra-mcp over HTTP when it needs code analysis, and Textra (directly, or via the
textra MCP server) for the actual translations.

## The translation workflow

1. **`iso_list_files`** — list files inside the Saturn disc image (`.iso` or
   Mode-1 `.bin`) and find which one holds the text.
2. **`iso_extract_file`** — pull that file out to the work directory.
3. **`create_translation_project`** — scan the file for Shift-JIS strings, detect
   the big-endian pointer table(s) that index them, and write a JSON project
   (offsets, original Japanese, and the pointer slots for each string).
4. **`translate_project`** — fill in English via Textra (or edit the JSON by hand,
   or translate through the textra MCP server).
5. **`apply_translation`** — write the English back, either:
   - `in_place` — overwrite each string within its original byte length (pointers
     don't move; safest, but English must fit), or
   - `repoint` — append translations to a new region and rewrite their pointer
     slots (removes the length limit).
6. **`write_file_to_image`** — patch the translated file back into a copy of the
   disc image, refusing any write that would overflow into the next file.

For text the scanner can't reach (computed addresses, compressed banks), use the
`ghidra_*` tools to drive ghidra-mcp: list Ghidra's extracted strings, follow
cross-references to the code that loads a string, and decompile it.

## Tools

| Tool | Purpose |
|------|---------|
| `iso_list_files` | List files in a Saturn disc image (auto-detects 2048/2352 sectors) |
| `iso_extract_file` | Extract one file from the image to disk |
| `scan_japanese_text` | Find Shift-JIS Japanese text runs in a file |
| `find_pointer_tables` | Detect big-endian SH-2 pointer tables indexing the text |
| `find_pointers_to` | Find every pointer slot that targets a given offset |
| `create_translation_project` | Build a JSON translation project (text + pointers) |
| `translate_project` | Fill English via the Textra API |
| `project_stats` | Report translation progress |
| `apply_translation` | Reinsert English (`in_place` or `repoint`) |
| `write_file_to_image` | Patch a translated file back into the disc image |
| `ghidra_check_connection` | Verify the ghidra-mcp HTTP bridge is up |
| `ghidra_list_strings` | List strings Ghidra extracted from the loaded program |
| `ghidra_xrefs_to` | Cross-references to an address (find code using a string) |

## Setup

Requires Python 3.10+. [`uv`](https://docs.astral.sh/uv/) is recommended.

```bash
# from the project directory
uv venv
uv pip install -e .
# (optional) tests
uv pip install -e '.[dev]'
```

Copy `.env.example` to `.env` and fill in your Textra credentials and (optionally)
the ghidra-mcp URL/token. Textra credentials come from
https://mt-auto-minhon-mlt.ucri.jgn-x.jp/.

### Run the server

```bash
saturn-translate-mcp           # stdio transport (for MCP clients)
# or
python -m saturn_translate.server
```

### CLI (no MCP client needed)

The same pipeline is available as a command-line tool, `saturn-translate-cli`,
for scripting and triage. It only needs the core dependencies (no fastmcp).

```bash
# Triage a disc: rank its files by how easy they are to translate
saturn-translate-cli feasibility GAME.iso

# Inspect and pull out a text file
saturn-translate-cli list GAME.iso
saturn-translate-cli extract GAME.iso /DATA/MENU.BIN -o work/MENU.BIN
saturn-translate-cli scan   work/MENU.BIN
saturn-translate-cli tables work/MENU.BIN

# Build a project, translate, reinsert, repack
saturn-translate-cli project   work/MENU.BIN -o work/MENU.json --image GAME.iso
saturn-translate-cli translate work/MENU.json            # Textra; or edit JSON by hand
saturn-translate-cli apply     work/MENU.json --strategy in_place -o work/MENU.en
saturn-translate-cli pack      GAME.iso /DATA/MENU.BIN work/MENU.en GAME_EN.iso
```

`feasibility` is the place to start with an unfamiliar disc. It scores every
file on text volume, pointer-table cleanliness, and how much English will fit in
place, then ranks them easiest-first so you can pick a low-hanging-fruit target:

```
difficulty score  strings jp_chars  table  fits  file
------------------------------------------------------------------------------
easy         100        8       39   100%  100%  /MENU.BIN
moderate      47        8      768      -  100%  /TILES.DAT

Start with: /MENU.BIN  (easy, 8 strings, 100% likely fit in place)
```

### Wire up all three servers

See `mcp.example.json` for a client config (Claude Desktop / Cursor) that launches
`saturn-translate`, `textra-translator`, and the `ghidra` bridge together. For the
Ghidra side: start Ghidra, load the Saturn binary, run **Tools > GhidraMCP > Start
MCP Server** (HTTP on `:8089`), then this server's `ghidra_*` tools can reach it.

## Project / file formats

A **translation project** is a single human-editable JSON file:

```json
{
  "source_file": "work/MSG.BIN",
  "image_path": "game.iso",
  "base_address": 100663296,
  "pointer_width": 4,
  "big_endian": true,
  "strings": [
    {"id": 0, "offset": 16, "length": 10, "original": "こんにちは",
     "translation": "Hello", "pointer_offsets": [0], "note": ""}
  ]
}
```

You can translate by hand simply by editing the `translation` fields, then run
`apply_translation`.

## Notes & limitations

- Saturn is **big-endian** (SH-2); pointers are stored MSB-first. The toolkit
  assumes this by default.
- Custom-font games (where bytes index a private glyph table instead of Shift-JIS)
  need a custom encoder; the reinsertion functions accept one.
- `write_file_to_image` does an in-extent patch, not a full ISO9660 rebuild. Keep
  translated files within their allocated sectors (use `in_place`, abbreviate, or
  relocate a bank) — the tool refuses writes that would corrupt the next file.
- For 2352-byte raw images, user data is patched in place; EDC/ECC is left as-is.
  Regenerate it with an external tool if a specific title is strict about it.

## Tests

```bash
pytest            # or: uv run pytest
```

Covers Shift-JIS scanning, pointer-anchored decoding, pointer-table detection,
both reinsertion strategies, ISO read/extract, and project round-tripping.

## References

See [`docs/REFERENCES.md`](docs/REFERENCES.md) for SegaXtreme resources, worked
example patches, and Saturn technical notes.
