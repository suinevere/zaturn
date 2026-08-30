"""SH-2 pointer-table discovery for Sega Saturn binaries.

The Saturn CPUs (two Hitachi SH-2 cores) are **big-endian**, so 32-bit pointers
are stored most-significant-byte first. Game text is usually referenced through
a *pointer table*: a contiguous array of addresses, one per string. To translate
text without corrupting the game you must find these tables so the pointers can
be rewritten when string lengths change.

A pointer to a string at file offset ``S`` is typically stored as::

    stored_value = base_address + S

where ``base_address`` is the address the file (or text bank) is loaded to in
Saturn RAM. Common bases are 0x06000000 (High Work RAM) and 0x00200000-style
work areas, but many games use file-relative pointers (base = 0). This module
searches for both 16- and 32-bit pointers under a caller-supplied set of
candidate bases and can infer the most likely base/table automatically.
"""

from __future__ import annotations

from dataclasses import dataclass, asdict
from collections import Counter
import struct


def read_u32(data: bytes, off: int, big_endian: bool = True) -> int:
    return struct.unpack_from(">I" if big_endian else "<I", data, off)[0]


def read_u16(data: bytes, off: int, big_endian: bool = True) -> int:
    return struct.unpack_from(">H" if big_endian else "<H", data, off)[0]


def encode_u32(value: int, big_endian: bool = True) -> bytes:
    return struct.pack(">I" if big_endian else "<I", value & 0xFFFFFFFF)


def encode_u16(value: int, big_endian: bool = True) -> bytes:
    return struct.pack(">H" if big_endian else "<H", value & 0xFFFF)


@dataclass
class PointerHit:
    """A location in the binary that appears to point at ``target_offset``."""

    pointer_offset: int   # where the pointer itself is stored
    stored_value: int     # raw value found at that location
    target_offset: int    # file offset the pointer resolves to
    base: int             # base address assumed (stored_value - target_offset)
    width: int            # 2 or 4 bytes
    big_endian: bool

    def to_dict(self) -> dict:
        return asdict(self)


def find_pointers_to(
    data: bytes,
    target_offset: int,
    *,
    bases: tuple[int, ...] = (0x00000000, 0x06000000, 0x06004000, 0x00200000),
    widths: tuple[int, ...] = (4, 2),
    big_endian: bool = True,
    align: int = 1,
) -> list[PointerHit]:
    """Find every location whose stored value resolves to ``target_offset``.

    For each candidate ``base`` the expected stored value is
    ``base + target_offset``; the buffer is scanned for that exact value.
    """
    hits: list[PointerHit] = []
    for base in bases:
        expected = (base + target_offset) & 0xFFFFFFFF
        for width in widths:
            reader = read_u32 if width == 4 else read_u16
            mask = 0xFFFFFFFF if width == 4 else 0xFFFF
            want = expected & mask
            # A 16-bit pointer can only address 64 KiB; skip if target out of range.
            if width == 2 and (base + target_offset) > 0xFFFF:
                continue
            off = 0
            while off + width <= len(data):
                if reader(data, off, big_endian) == want:
                    hits.append(
                        PointerHit(
                            pointer_offset=off,
                            stored_value=want,
                            target_offset=target_offset,
                            base=base,
                            width=width,
                            big_endian=big_endian,
                        )
                    )
                off += align
    return hits


@dataclass
class PointerTable:
    """A detected contiguous run of pointers."""

    table_offset: int     # file offset where the table starts
    count: int            # number of entries
    stride: int           # bytes between entries (== width for tight tables)
    width: int            # pointer width in bytes
    base: int             # inferred base address
    big_endian: bool
    entries: list[int]    # resolved target file offsets, in table order

    def to_dict(self) -> dict:
        return asdict(self)


def detect_tables(
    data: bytes,
    string_offsets: list[int],
    *,
    bases: tuple[int, ...] = (0x00000000, 0x06000000, 0x06004000, 0x00200000),
    width: int = 4,
    big_endian: bool = True,
    min_entries: int = 3,
    align: int = 4,
) -> list[PointerTable]:
    """Infer pointer tables that index the known ``string_offsets``.

    Strategy: for each candidate base, slide through the binary looking for runs
    of consecutive slots that resolve to a plausible text offset. A slot is
    accepted when ``value - base`` is either a known ``string_offset`` *or* an
    offset whose bytes decode to a real Shift-JIS string. Including the decode
    check lets a run absorb strings the heuristic scanner mis-located (e.g. when
    a printable pointer byte glued onto the previous string), so a table that
    indexes N strings is detected with all N entries rather than N-1.

    Returns tables sorted by entry count (largest/most-confident first).
    """
    from .sjis import decode_string_at  # local import avoids a cycle

    targets = set(string_offsets)
    reader = read_u32 if width == 4 else read_u16
    mask = 0xFFFFFFFF if width == 4 else 0xFFFF
    n = len(data)
    results: list[PointerTable] = []

    def resolves(base: int, value: int) -> int | None:
        """Return the target offset if ``value`` is a plausible pointer."""
        t = (value - base) & mask
        if t in targets:
            return t
        if 0 <= t < n:
            hit = decode_string_at(data, t)
            if hit.has_kanji_kana and hit.length >= 2:
                return t
        return None

    for base in bases:
        if not targets:
            continue
        off = 0
        while off + width <= n:
            first = resolves(base, reader(data, off, big_endian))
            if first is not None:
                run_targets: list[int] = []
                cur = off
                while cur + width <= n:
                    t = resolves(base, reader(data, cur, big_endian))
                    if t is None:
                        break
                    run_targets.append(t)
                    cur += width
                if len(run_targets) >= min_entries:
                    results.append(
                        PointerTable(
                            table_offset=off,
                            count=len(run_targets),
                            stride=width,
                            width=width,
                            base=base,
                            big_endian=big_endian,
                            entries=run_targets,
                        )
                    )
                off = cur  # skip past the run we just consumed
            else:
                off += align

    results.sort(key=lambda t: t.count, reverse=True)
    return results
