"""Saturn Translate MCP server.

Exposes Saturn-specific translation tooling over the Model Context Protocol and
orchestrates the two upstream servers:

* **ghidra-mcp** (bethington/ghidra-mcp) — SH-2 binary reverse engineering, for
  text that is referenced from code rather than a flat pointer table.
* **textra-ja-to-en-mcp** (hokupod/textra-ja-to-en-mcp) — JA→EN machine
  translation via the Textra API (also callable directly here).

Typical workflow:
    1. iso_list_files / iso_extract_file   — pull a text file off the disc image
    2. create_translation_project          — scan Shift-JIS strings + find pointers
    3. translate_project                   — fill English via Textra (or edit by hand)
    4. apply_translation                   — write English back (in_place or repoint)
    5. write_file_to_image                 — patch the file back into the disc image

Binaries stay on disk and are referenced by path; tools return JSON summaries so
large game data never floods the conversation.
"""

from __future__ import annotations

import os
from pathlib import Path

from fastmcp import FastMCP

from . import sjis, pointers, reinsert, build
from .iso import SaturnImage
from .project import TranslationProject, StringEntry
from .textra import translate_many, TextraError
from .ghidra import GhidraClient, GhidraError

mcp = FastMCP("saturn-translate")

WORKDIR = Path(os.environ.get("SATURN_WORKDIR", "./work")).expanduser()


def _workdir() -> Path:
    WORKDIR.mkdir(parents=True, exist_ok=True)
    return WORKDIR


# ─────────────────────────────────────────────────────────────────────────
# Disc image tools
# ─────────────────────────────────────────────────────────────────────────
@mcp.tool()
def iso_list_files(image_path: str, recursive: bool = True) -> dict:
    """List files inside a Sega Saturn disc image (.iso / Mode-1 .bin).

    Returns each file's image path, starting sector (LBA), and byte size so you
    can pick which file holds the game text.
    """
    img = SaturnImage.from_file(image_path)
    files = img.list_files(recursive=recursive)
    return {
        "image_path": image_path,
        "sector_size": img.sector_size,
        "file_count": len(files),
        "files": [f.to_dict() for f in files],
    }


@mcp.tool()
def iso_extract_file(image_path: str, file_path: str, out_path: str = "") -> dict:
    """Extract one file from a Saturn disc image to disk.

    ``file_path`` is the in-image path from iso_list_files (e.g. "/DATA/MSG.BIN").
    If ``out_path`` is empty the file is written into the work directory.
    """
    img = SaturnImage.from_file(image_path)
    data = img.extract(file_path)
    if not out_path:
        name = file_path.strip("/").replace("/", "_")
        out_path = str(_workdir() / name)
    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "wb") as fh:
        fh.write(data)
    return {"file_path": file_path, "out_path": out_path, "size": len(data)}


# ─────────────────────────────────────────────────────────────────────────
# Text + pointer analysis
# ─────────────────────────────────────────────────────────────────────────
@mcp.tool()
def scan_japanese_text(
    file_path: str,
    min_chars: int = 2,
    require_japanese: bool = True,
    limit: int = 500,
) -> dict:
    """Scan a binary file for Shift-JIS Japanese text runs.

    Returns offset, byte length and decoded text for each run. Use this to see
    what text a file contains before building a translation project.
    """
    with open(file_path, "rb") as fh:
        data = fh.read()
    hits = sjis.scan(data, min_chars=min_chars, require_japanese=require_japanese)
    return {
        "file_path": file_path,
        "total_found": len(hits),
        "showing": min(limit, len(hits)),
        "strings": [h.to_dict() for h in hits[:limit]],
    }


@mcp.tool()
def find_pointer_tables(
    file_path: str,
    bases: list[int] | None = None,
    width: int = 4,
    big_endian: bool = True,
    min_entries: int = 3,
) -> dict:
    """Detect SH-2 pointer tables that index the file's Shift-JIS strings.

    Scans the file for the strings first, then looks for contiguous runs of
    big-endian pointers (one per string) under each candidate base address.
    Saturn is big-endian; common bases include 0x06000000 (HWRAM) and 0 (file-
    relative). Returns the most-confident (largest) tables first.
    """
    with open(file_path, "rb") as fh:
        data = fh.read()
    hits = sjis.scan(data)
    offsets = [h.offset for h in hits]
    base_tuple = tuple(bases) if bases else (0x00000000, 0x06000000, 0x06004000, 0x00200000)
    tables = pointers.detect_tables(
        data, offsets, bases=base_tuple, width=width, big_endian=big_endian, min_entries=min_entries
    )
    return {
        "file_path": file_path,
        "string_count": len(offsets),
        "table_count": len(tables),
        "tables": [t.to_dict() for t in tables[:20]],
    }


@mcp.tool()
def find_pointers_to(
    file_path: str,
    target_offset: int,
    bases: list[int] | None = None,
    big_endian: bool = True,
) -> dict:
    """Find every pointer in the file that resolves to ``target_offset``.

    Use this to locate the pointer slot(s) for a specific string so it can be
    repointed when the translation is longer than the original.
    """
    with open(file_path, "rb") as fh:
        data = fh.read()
    base_tuple = tuple(bases) if bases else (0x00000000, 0x06000000, 0x06004000, 0x00200000)
    hits = pointers.find_pointers_to(data, target_offset, bases=base_tuple, big_endian=big_endian)
    return {
        "file_path": file_path,
        "target_offset": target_offset,
        "hit_count": len(hits),
        "pointers": [h.to_dict() for h in hits],
    }


# ─────────────────────────────────────────────────────────────────────────
# Translation projects
# ─────────────────────────────────────────────────────────────────────────
@mcp.tool()
def create_translation_project(
    file_path: str,
    project_path: str = "",
    image_path: str = "",
    detect_pointers: bool = True,
    base_address: int = 0,
    min_chars: int = 2,
) -> dict:
    """Build a translation-table project from an extracted game file.

    Scans Shift-JIS strings and (optionally) links each to the pointer slots
    that reference it, then writes a JSON project ready for translate_project.
    """
    with open(file_path, "rb") as fh:
        data = fh.read()
    scan_hits = sjis.scan(data, min_chars=min_chars)

    proj = TranslationProject(
        source_file=file_path,
        image_path=image_path,
        base_address=base_address,
        big_endian=True,
    )

    inferred_base = base_address
    table_offsets: list[int] = []
    if detect_pointers:
        scan_offsets = [h.offset for h in scan_hits]
        tables = pointers.detect_tables(data, scan_offsets, min_entries=3)
        if tables:
            inferred_base = tables[0].base
            proj.base_address = inferred_base
            # Pointer-table offsets are exact; scan offsets are heuristic.
            table_offsets = sorted(set(off for t in tables for off in t.entries))

    # Prefer authoritative pointer-table offsets when available, else the scan.
    if table_offsets:
        hits = [sjis.decode_string_at(data, off) for off in table_offsets]
    else:
        hits = scan_hits

    for i, h in enumerate(hits):
        ptr_offsets: list[int] = []
        if detect_pointers:
            ph = pointers.find_pointers_to(data, h.offset, bases=(inferred_base,))
            ptr_offsets = [p.pointer_offset for p in ph]
        proj.strings.append(
            StringEntry(
                id=i,
                offset=h.offset,
                length=h.length,
                original=h.text,
                pointer_offsets=ptr_offsets,
            )
        )

    if not project_path:
        stem = Path(file_path).stem
        project_path = str(_workdir() / f"{stem}.translation.json")
    proj.save(project_path)
    return {
        "project_path": project_path,
        "base_address": proj.base_address,
        **proj.stats(),
    }


@mcp.tool()
def translate_project(
    project_path: str,
    only_untranslated: bool = True,
    limit: int = 0,
) -> dict:
    """Fill in English translations for a project using the Textra JA→EN API.

    Requires TEXTRA_API_KEY / TEXTRA_API_SECRET / TEXTRA_USER_NAME. If you'd
    rather translate via the textra-ja-to-en-mcp server or by hand, skip this
    and edit the project JSON directly. ``limit`` caps how many are translated
    in one call (0 = all).
    """
    proj = TranslationProject.load(project_path)
    targets = [
        s for s in proj.strings
        if (not only_untranslated) or not s.translation.strip()
    ]
    if limit > 0:
        targets = targets[:limit]
    if not targets:
        return {"project_path": project_path, "translated_now": 0, **proj.stats()}

    try:
        results = translate_many([s.original for s in targets])
    except TextraError as exc:
        return {"error": str(exc), "project_path": project_path, **proj.stats()}

    for s, en in zip(targets, results):
        s.translation = en
    proj.save(project_path)
    return {"project_path": project_path, "translated_now": len(targets), **proj.stats()}


@mcp.tool()
def project_stats(project_path: str) -> dict:
    """Report translation progress for a project (counts + percent complete)."""
    proj = TranslationProject.load(project_path)
    return {"project_path": project_path, "base_address": proj.base_address, **proj.stats()}


# ─────────────────────────────────────────────────────────────────────────
# Reinsertion
# ─────────────────────────────────────────────────────────────────────────
@mcp.tool()
def apply_translation(
    project_path: str,
    out_file_path: str = "",
    strategy: str = "in_place",
) -> dict:
    """Write a project's English translations back into its source file.

    strategy="in_place": overwrite each string within its original byte length
    (padded with spaces). No pointers move — safest, but English must fit.

    strategy="repoint": append translations to a new region and rewrite each
    string's pointer slots to the new location. Removes the length limit but
    requires pointer_offsets to have been detected. The patched file grows;
    keep it within the file's allocated disc sectors before write_file_to_image.

    Writes the patched file to ``out_file_path`` (defaults to "<source>.en").
    """
    proj = TranslationProject.load(project_path)
    with open(proj.source_file, "rb") as fh:
        data = bytearray(fh.read())

    edits = [
        reinsert.Edit(
            offset=s.offset,
            original_length=s.length,
            translation=s.translation,
            pointer_offsets=s.pointer_offsets,
        )
        for s in proj.strings
        if s.translation.strip()
    ]

    if strategy == "in_place":
        result = reinsert.reinsert_in_place(data, edits)
        summary = {
            "strategy": "in_place",
            "written": result.written,
            "overflowed_offsets": result.overflowed,
            "overflow_count": len(result.overflowed),
        }
    elif strategy == "repoint":
        missing = [e.offset for e in edits if not e.pointer_offsets]
        result = reinsert.reinsert_repoint(
            data, edits, base=proj.base_address, width=proj.pointer_width, big_endian=proj.big_endian
        )
        summary = {
            "strategy": "repoint",
            "written": result.written,
            "new_region_offset": result.new_region_offset,
            "appended_bytes": result.appended_bytes,
            "repointed_slots": result.repointed,
            "strings_without_pointers": missing,
        }
    else:
        return {"error": f"unknown strategy '{strategy}' (use 'in_place' or 'repoint')"}

    if not out_file_path:
        out_file_path = proj.source_file + ".en"
    with open(out_file_path, "wb") as fh:
        fh.write(data)
    summary["out_file_path"] = out_file_path
    summary["new_size"] = len(data)
    return summary


@mcp.tool()
def write_file_to_image(
    image_path: str,
    in_image_path: str,
    patched_file_path: str,
    out_image_path: str,
) -> dict:
    """Patch a translated file back into a copy of the disc image.

    Writes the bytes of ``patched_file_path`` into the sectors allocated to
    ``in_image_path`` and saves a new image at ``out_image_path``. Fails if the
    patched file is larger than its allocated sectors (which would corrupt the
    next file) — keep translations within budget or relocate the text bank.
    """
    with open(image_path, "rb") as fh:
        image_bytes = fh.read()
    with open(patched_file_path, "rb") as fh:
        new_data = fh.read()
    out_bytes, result = build.replace_file(image_bytes, in_image_path, new_data)
    Path(out_image_path).parent.mkdir(parents=True, exist_ok=True)
    with open(out_image_path, "wb") as fh:
        fh.write(out_bytes)
    return {
        "out_image_path": out_image_path,
        "in_image_path": in_image_path,
        "original_size": result.original_size,
        "new_size": result.new_size,
        "allocated_bytes": result.allocated_bytes,
        "padded": result.padded,
    }


# ─────────────────────────────────────────────────────────────────────────
# ghidra-mcp orchestration (for code-referenced text)
# ─────────────────────────────────────────────────────────────────────────
@mcp.tool()
def ghidra_check_connection() -> dict:
    """Verify the GhidraMCP HTTP bridge is reachable (set GHIDRA_MCP_URL)."""
    try:
        return {"ok": True, "response": GhidraClient().check_connection()}
    except Exception as exc:  # noqa: BLE001
        return {"ok": False, "error": str(exc)}


@mcp.tool()
def ghidra_list_strings(limit: int = 200, offset: int = 0) -> dict:
    """List strings Ghidra extracted from the program loaded in GhidraMCP."""
    try:
        return {"ok": True, "response": GhidraClient().list_strings(limit=limit, offset=offset)}
    except Exception as exc:  # noqa: BLE001
        return {"ok": False, "error": str(exc)}


@mcp.tool()
def ghidra_xrefs_to(address: str) -> dict:
    """Cross-references to an address in Ghidra — find code that uses a string."""
    try:
        return {"ok": True, "response": GhidraClient().get_xrefs_to(address)}
    except Exception as exc:  # noqa: BLE001
        return {"ok": False, "error": str(exc)}


def main() -> None:
    mcp.run()


if __name__ == "__main__":
    main()
