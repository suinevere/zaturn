"""Minimal IPS patch writer.

IPS is the simplest ROM-patch format and is widely supported. It addresses bytes
with a 24-bit offset, so it only works for files up to 16 MiB *or* for edits that
fall within the first 16 MiB of a larger file. For a Saturn disc image that is
fine as long as the edited bytes live in the low 16 MiB (boot/exec area), which
is where language-select and loader tables sit.
"""

from __future__ import annotations

from dataclasses import dataclass

MAX_OFFSET = 0xFFFFFF  # 24-bit


@dataclass
class Record:
    offset: int
    data: bytes


def encode(records: list[Record]) -> bytes:
    """Return an IPS patch for the given (offset, data) records."""
    out = bytearray(b"PATCH")
    for r in sorted(records, key=lambda x: x.offset):
        if r.offset > MAX_OFFSET:
            raise ValueError(
                f"offset {r.offset:#x} exceeds IPS 16 MiB range; use xdelta instead"
            )
        if r.offset == 0x454F46:  # 'EOF' as an offset is illegal in IPS
            raise ValueError("offset collides with IPS EOF marker; use xdelta")
        n = len(r.data)
        out += bytes([(r.offset >> 16) & 0xFF, (r.offset >> 8) & 0xFF, r.offset & 0xFF])
        out += bytes([(n >> 8) & 0xFF, n & 0xFF])
        out += r.data
    out += b"EOF"
    return bytes(out)
