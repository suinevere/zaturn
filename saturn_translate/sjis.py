"""Shift-JIS text scanning for Sega Saturn binaries.

Saturn games almost universally store Japanese text as Shift-JIS (CP932) byte
sequences embedded directly in game files (SLAVE/MASTER SH-2 binaries, scenario
data files, etc.). This module finds runs of plausible Japanese text and decodes
them, returning byte offsets so the strings can later be re-pointed and replaced.

The scanner is deliberately conservative: it looks for runs of double-byte
Shift-JIS characters (optionally mixed with single-byte ASCII / half-width kana)
that meet a minimum length, which strongly suppresses false positives from code
and graphics data.
"""

from __future__ import annotations

from dataclasses import dataclass, asdict


# Shift-JIS lead-byte ranges (first byte of a 2-byte sequence)
def _is_sjis_lead(b: int) -> bool:
    return (0x81 <= b <= 0x9F) or (0xE0 <= b <= 0xFC)


# Valid trail byte (second byte of a 2-byte sequence)
def _is_sjis_trail(b: int) -> bool:
    return (0x40 <= b <= 0x7E) or (0x80 <= b <= 0xFC)


# Half-width katakana (single byte)
def _is_halfwidth_kana(b: int) -> bool:
    return 0xA1 <= b <= 0xDF


# Printable ASCII allowed inside a string run (incl. space)
def _is_ascii_text(b: int) -> bool:
    return 0x20 <= b <= 0x7E


@dataclass
class StringHit:
    """A run of decoded Shift-JIS text found in a binary."""

    offset: int          # byte offset of the first byte of the run
    length: int          # length of the run in bytes (not counting terminator)
    text: str            # decoded text
    has_kanji_kana: bool # True if the run contains real Japanese (not just ASCII)
    terminator: int | None = None  # terminator byte if one followed (e.g. 0x00)

    def to_dict(self) -> dict:
        return asdict(self)


def _decode(data: bytes) -> str:
    # 'replace' guards against the rare invalid pair slipping through validation.
    return data.decode("cp932", errors="replace")


def scan(
    data: bytes,
    *,
    min_chars: int = 2,
    require_japanese: bool = True,
    terminators: tuple[int, ...] = (0x00,),
    start: int = 0,
    end: int | None = None,
) -> list[StringHit]:
    """Scan ``data`` for Shift-JIS text runs.

    Parameters
    ----------
    min_chars:
        Minimum number of *characters* (not bytes) a run must contain to be
        reported. Filters out stray single glyphs.
    require_japanese:
        When True (default) only runs that contain at least one full-width
        kanji/kana or half-width kana are reported, so pure-ASCII strings are
        skipped. Set False to also capture ASCII-only strings.
    terminators:
        Byte values that legitimately end a string (recorded on the hit). A run
        also ends naturally at any byte that is not valid text.
    start, end:
        Restrict the scan to ``data[start:end]``. Offsets in results are still
        absolute (relative to the whole ``data`` buffer).
    """
    if end is None:
        end = len(data)

    hits: list[StringHit] = []
    i = start
    while i < end:
        run_start = i
        chars = 0
        has_jp = False

        j = i
        while j < end:
            b = data[j]
            if _is_sjis_lead(b) and j + 1 < end and _is_sjis_trail(data[j + 1]):
                chars += 1
                has_jp = True
                j += 2
            elif _is_halfwidth_kana(b):
                chars += 1
                has_jp = True
                j += 1
            elif _is_ascii_text(b):
                chars += 1
                j += 1
            else:
                break

        run_len = j - run_start
        if run_len > 0 and chars >= min_chars and (has_jp or not require_japanese):
            term = None
            if j < end and data[j] in terminators:
                term = data[j]
            hits.append(
                StringHit(
                    offset=run_start,
                    length=run_len,
                    text=_decode(data[run_start:j]),
                    has_kanji_kana=has_jp,
                    terminator=term,
                )
            )
            # advance past the run (and terminator if present)
            i = j + (1 if term is not None else 0)
        else:
            # not a valid run; step forward one byte and retry
            i = run_start + 1

    return hits


def decode_string_at(
    data: bytes,
    offset: int,
    *,
    terminators: tuple[int, ...] = (0x00,),
    max_len: int = 4096,
) -> StringHit:
    """Decode a single Shift-JIS string starting exactly at ``offset``.

    Walks forward until a terminator or an invalid text byte. Use this when the
    start offset is known authoritatively (e.g. from a pointer table) -- it is
    exact, unlike :func:`scan`, which infers boundaries heuristically and can be
    thrown off by adjacent binary data.
    """
    j = offset
    has_jp = False
    end = min(len(data), offset + max_len)
    while j < end:
        b = data[j]
        if b in terminators:
            break
        if _is_sjis_lead(b) and j + 1 < end and _is_sjis_trail(data[j + 1]):
            has_jp = True
            j += 2
        elif _is_halfwidth_kana(b):
            has_jp = True
            j += 1
        elif _is_ascii_text(b):
            j += 1
        else:
            break
    term = data[j] if j < len(data) and data[j] in terminators else None
    return StringHit(
        offset=offset,
        length=j - offset,
        text=_decode(data[offset:j]),
        has_kanji_kana=has_jp,
        terminator=term,
    )


def scan_file(path: str, **kwargs) -> list[StringHit]:
    """Convenience wrapper: read ``path`` and scan it."""
    with open(path, "rb") as fh:
        return scan(fh.read(), **kwargs)
