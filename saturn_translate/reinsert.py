"""Write translated English text back into a Saturn binary.

Two reinsertion strategies are supported:

* **in_place** — overwrite each original Shift-JIS string with the (ASCII)
  translation, truncated or space/null padded to the *original byte length*.
  Pointers never move, so no pointer table is required. Safe but length-limited.

* **repoint** — append all translated strings to a fresh region at the end of
  the file and rewrite a pointer table so each entry targets the new location.
  Removes the length limit, at the cost of needing a correctly-identified table.

English is encoded as ASCII/latin-1 by default. Many Saturn games use a custom
font where ASCII maps directly to the 1-byte half of the table; for games that
need a custom mapping, pass an ``encoder`` callable.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from .pointers import encode_u32, encode_u16


Encoder = Callable[[str], bytes]


def _default_encoder(s: str) -> bytes:
    return s.encode("latin-1", errors="replace")


@dataclass
class Edit:
    """One string to reinsert."""

    offset: int            # original file offset of the Japanese string
    original_length: int   # original byte length (excludes terminator)
    translation: str       # English replacement
    pointer_offsets: list[int] | None = None  # pointer slots that target this string


@dataclass
class InPlaceResult:
    written: int           # number of strings written
    overflowed: list[int]  # offsets whose translation was truncated to fit


def reinsert_in_place(
    data: bytearray,
    edits: list[Edit],
    *,
    encoder: Encoder = _default_encoder,
    pad_byte: int = 0x20,        # space
    terminator: int | None = 0x00,
) -> InPlaceResult:
    """Overwrite each string in place, padded/truncated to its original length.

    ``data`` is mutated in place. The terminator (if the original had room for
    one is preserved implicitly because we never write past original_length).
    """
    overflowed: list[int] = []
    written = 0
    for e in edits:
        payload = encoder(e.translation)
        capacity = e.original_length
        if len(payload) > capacity:
            payload = payload[:capacity]
            overflowed.append(e.offset)
        # write payload
        data[e.offset : e.offset + len(payload)] = payload
        # pad the remainder of the original slot
        pad_start = e.offset + len(payload)
        pad_end = e.offset + capacity
        for k in range(pad_start, pad_end):
            data[k] = pad_byte
        # restore a terminator at the very end of the slot if requested
        if terminator is not None and pad_end > e.offset:
            data[pad_end - 1] = terminator
        written += 1
    return InPlaceResult(written=written, overflowed=overflowed)


@dataclass
class RepointResult:
    written: int
    new_region_offset: int     # where appended strings begin
    appended_bytes: int
    repointed: int             # number of pointer slots rewritten


def reinsert_repoint(
    data: bytearray,
    edits: list[Edit],
    *,
    base: int,
    width: int = 4,
    big_endian: bool = True,
    encoder: Encoder = _default_encoder,
    terminator: int | None = 0x00,
    align: int = 1,
) -> RepointResult:
    """Append translated strings and rewrite their pointer slots.

    Each edit must carry ``pointer_offsets`` (the slots that reference it). New
    strings are written to a fresh region appended to ``data``; every referenced
    pointer slot is rewritten to ``base + new_offset``.
    """
    region_start = len(data)
    if align > 1 and region_start % align:
        data.extend(b"\x00" * (align - (region_start % align)))
        region_start = len(data)

    encode_ptr = encode_u32 if width == 4 else encode_u16
    repointed = 0
    written = 0

    for e in edits:
        new_off = len(data)
        payload = bytearray(encoder(e.translation))
        if terminator is not None:
            payload.append(terminator)
        data.extend(payload)
        if align > 1 and len(data) % align:
            data.extend(b"\x00" * (align - (len(data) % align)))

        stored = (base + new_off) & (0xFFFFFFFF if width == 4 else 0xFFFF)
        for ptr_off in (e.pointer_offsets or []):
            enc = encode_ptr(stored, big_endian)
            data[ptr_off : ptr_off + width] = enc
            repointed += 1
        written += 1

    return RepointResult(
        written=written,
        new_region_offset=region_start,
        appended_bytes=len(data) - region_start,
        repointed=repointed,
    )
