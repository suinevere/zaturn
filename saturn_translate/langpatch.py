"""Detect and flip the boot-language selector on multi-language Saturn discs.

Some Saturn titles ship every code overlay twice — a Japanese set (``GOLF.BIN``,
``LOAD.BIN``, …) and an English set with an ``E`` prefix (``EGOLF.BIN``,
``ELOAD.BIN``, …) — and a small boot dispatcher chooses between them via a table
of big-endian pointers to the overlay filenames. Region-locked Japanese releases
often default that table to the Japanese loader.

This module finds the top-level loader pointer pair (``LOAD.BIN`` vs
``ELOAD.BIN``, or a caller-given name pair) inside the boot file and produces the
single-pointer edit that makes the default slot point at the English loader, so
the whole English overlay chain loads. It returns an image-relative byte edit
suitable for :mod:`saturn_translate.vcdiff` / :mod:`saturn_translate.ips`.

The same idea generalizes to other ``NAME``/``ENAME`` loader pairs; pass them in.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

from .iso import SaturnImage, SECTOR_USER


@dataclass
class LanguageFlip:
    image_offset: int     # absolute byte offset in the disc image to patch
    old_bytes: bytes      # current pointer value (big-endian u32)
    new_bytes: bytes      # English pointer value (big-endian u32)
    boot_file: str        # file the pointer lives in
    from_name: str        # filename the default pointer currently targets
    to_name: str          # filename it will target after the flip
    load_base: int        # inferred load address of the boot file

    def describe(self) -> str:
        return (
            f"{self.boot_file}: default loader pointer @ image {self.image_offset:#x} "
            f"{self.old_bytes.hex()} -> {self.new_bytes.hex()} "
            f"({self.from_name} -> {self.to_name})"
        )


def _find_load_base(blob: bytes, str_off: int, ptr_positions: list[int]) -> int | None:
    """Given a string at ``str_off`` and candidate pointer slots, infer the file's
    HWRAM load base by finding a pointer whose value minus ``str_off`` is a sane
    Saturn RAM address."""
    for p in ptr_positions:
        val = struct.unpack_from(">I", blob, p)[0]
        base = val - str_off
        if 0x06000000 <= base <= 0x06100000:
            return base
    return None


def find_language_flip(
    image: SaturnImage,
    boot_file: str = "/A.BIN",
    jp_name: bytes = b"LOAD.BIN",
    en_name: bytes = b"ELOAD.BIN",
) -> LanguageFlip | None:
    """Locate the loader pointer pair in ``boot_file`` and build the flip edit.

    Returns ``None`` if the boot file or the pointer pair can't be found.
    """
    try:
        blob = image.extract(boot_file)
    except Exception:
        return None

    jp_str = blob.find(jp_name + b"\x00")
    en_str = blob.find(en_name + b"\x00")
    if jp_str < 0 or en_str < 0:
        return None

    # The real loader table stores the JP and EN filename pointers in *adjacent*
    # 4-byte slots: [ptr->JP][ptr->EN]. Requiring adjacency under a single shared
    # load base rejects coincidental lone values that merely happen to equal
    # base+offset for some unrelated base.
    # A.BIN also holds a *menu* pointer table whose entries are spaced the same
    # 0xC apart as the LOAD.BIN/ELOAD.BIN strings, so a base shifted by the
    # filename/menu gap mimics the loader pair from values alone. The true file
    # load base is page-aligned (Saturn loads overlays on aligned boundaries),
    # so we additionally require the inferred base to be aligned, which rejects
    # the menu table's off-by-0x48 pseudo-match.
    n = len(blob)
    jp_ptr_off = base = None
    for align in (0x1000, 0x100):           # try the strongest alignment first
        for off in range(0, n - 7, 4):
            v0 = struct.unpack_from(">I", blob, off)[0]
            v1 = struct.unpack_from(">I", blob, off + 4)[0]
            if not (0x06000000 <= v0 <= 0x06100000):
                continue
            b = v0 - jp_str
            if (b & (align - 1)) == 0 and 0x06000000 <= b <= 0x06100000 and v1 == b + en_str:
                jp_ptr_off, base = off, b
                break
        if jp_ptr_off is not None:
            break
    if jp_ptr_off is None:
        return None

    en_val = base + en_str
    # Map the JP pointer's in-file offset to an absolute image offset.
    entry = image.find(boot_file)
    # offset within the boot file -> image byte offset (boot file is contiguous on disc)
    sector = jp_ptr_off // SECTOR_USER
    in_sector = jp_ptr_off % SECTOR_USER
    image_off = (entry.lba + sector) * image.sector_size + image.data_offset + in_sector

    return LanguageFlip(
        image_offset=image_off,
        old_bytes=struct.pack(">I", base + jp_str),
        new_bytes=struct.pack(">I", en_val),
        boot_file=boot_file,
        from_name=jp_name.decode(),
        to_name=en_name.decode(),
        load_base=base,
    )
