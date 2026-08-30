"""Write modified files back into a Saturn disc image.

A full ISO9660 rebuild (relocating extents, rewriting path tables) is out of
scope; in practice Saturn translations replace a file's bytes *within the sectors
already allocated to it*. This module does exactly that: it writes new file
content into the original extent, padding the remainder of the last allocated
sector. It refuses writes that would overflow into the next file's sectors,
which is the failure mode that silently corrupts a disc.

For 2048-byte images the user data is written directly. For 2352-byte raw images
each sector's 2048-byte user-data window is patched in place (sync/header and
EDC/ECC are left untouched, which real hardware and emulators tolerate for data
tracks in the vast majority of cases; regenerate ECC with an external tool if a
title is strict about it).
"""

from __future__ import annotations

from dataclasses import dataclass

from .iso import SaturnImage, SECTOR_USER


@dataclass
class WriteResult:
    path: str
    original_size: int
    new_size: int
    allocated_bytes: int   # sectors * 2048 available to this file
    padded: bool


def replace_file(image_bytes: bytes, path: str, new_data: bytes) -> tuple[bytes, WriteResult]:
    """Return a new image with ``path`` replaced by ``new_data``.

    Raises ``ValueError`` if ``new_data`` does not fit in the file's allocated
    sectors. Caller is responsible for keeping translated files within budget
    (use the in-place reinsertion strategy, or trim/relocate text banks).
    """
    img = SaturnImage(image_bytes)
    entry = img.find(path)
    if entry is None:
        raise FileNotFoundError(path)
    if entry.is_dir:
        raise IsADirectoryError(path)

    sectors = (entry.size + SECTOR_USER - 1) // SECTOR_USER
    allocated = sectors * SECTOR_USER
    if len(new_data) > allocated:
        raise ValueError(
            f"{path}: new data is {len(new_data)} bytes but only {allocated} "
            f"bytes ({sectors} sectors) are allocated. Reduce text size or "
            f"relocate the bank; growing the extent would overwrite the next file."
        )

    out = bytearray(image_bytes)
    # Write sector by sector so 2352 raw layouts are patched in their user windows.
    payload = bytes(new_data) + b"\x00" * (allocated - len(new_data))
    for i in range(sectors):
        lba = entry.lba + i
        abs_off = lba * img.sector_size + img.data_offset
        chunk = payload[i * SECTOR_USER : (i + 1) * SECTOR_USER]
        out[abs_off : abs_off + len(chunk)] = chunk

    return bytes(out), WriteResult(
        path=path,
        original_size=entry.size,
        new_size=len(new_data),
        allocated_bytes=allocated,
        padded=len(new_data) < allocated,
    )
