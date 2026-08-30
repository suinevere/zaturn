"""Feasibility triage: rank disc files by how easy they are to translate.

The hard part of a Saturn translation is rarely the translating — it's whether
the text is reachable and whether English fits. This module scores each file in
a disc image on three axes and produces a ranked report so you can pick a
*low-hanging-fruit* target instead of guessing.

Signals used:

* **Text volume** — number of Shift-JIS strings and total Japanese characters.
  Less text = less work, and fits an automated first pass better.
* **Pointer cleanliness** — does a flat big-endian pointer table index the
  strings? A clean table means repointing is mechanical; no table means the
  text is referenced from code (needs Ghidra) or is compressed.
* **ASCII fit** — a rough estimate of how many strings' English translation will
  fit inside the original byte budget, i.e. how much of the file can use the
  safe ``in_place`` strategy with no repointing at all.

The fit estimate is heuristic (English length ≈ 1.6 bytes per Japanese
character on average). Because Japanese stores 2 bytes per kana/kanji, English
at < 2.0 bytes/char usually fits the original budget; menu/UI text comfortably
does, dense prose sometimes doesn't. Treat the percentages as a relative
ranking signal, not a guarantee.
"""

from __future__ import annotations

from dataclasses import dataclass, asdict

from . import sjis, pointers

# Empirical average: a Japanese character expands to ~1.6 English bytes. Japanese
# stores 2 bytes/char, so anything under 2.0 generally fits the original slot.
# Menu/UI words sit well under this; long prose can exceed it. Triage estimate.
ENGLISH_BYTES_PER_JP_CHAR = 1.6


@dataclass
class FileScore:
    path: str
    size: int
    string_count: int
    jp_char_total: int
    has_pointer_table: bool
    best_table_coverage: float   # fraction of strings covered by the best table
    est_fit_ratio: float         # fraction of strings whose English likely fits
    difficulty: str              # "easy" | "moderate" | "hard"
    score: float                 # higher = easier (for ranking)

    def to_dict(self) -> dict:
        return asdict(self)


def _jp_char_count(text: str) -> int:
    # count non-ASCII (i.e. Japanese) characters
    return sum(1 for ch in text if ord(ch) > 0x7F)


def analyze_data(path: str, data: bytes, *, min_chars: int = 2) -> FileScore:
    """Score a single file's bytes for translation feasibility."""
    hits = sjis.scan(data, min_chars=min_chars)
    string_count = len(hits)

    if string_count == 0:
        return FileScore(
            path=path, size=len(data), string_count=0, jp_char_total=0,
            has_pointer_table=False, best_table_coverage=0.0,
            est_fit_ratio=0.0, difficulty="hard", score=0.0,
        )

    jp_total = sum(_jp_char_count(h.text) for h in hits)

    # pointer-table coverage
    offsets = [h.offset for h in hits]
    tables = pointers.detect_tables(data, offsets, min_entries=3)
    has_table = bool(tables)
    coverage = 0.0
    if has_table:
        covered = len({off for t in tables for off in t.entries})
        coverage = min(1.0, covered / string_count)

    # ASCII-fit estimate: English bytes vs original byte budget per string
    fit = 0
    for h in hits:
        jp = _jp_char_count(h.text)
        est_en_bytes = jp * ENGLISH_BYTES_PER_JP_CHAR + (h.length - jp * 2)
        if est_en_bytes <= h.length:
            fit += 1
    fit_ratio = fit / string_count

    # difficulty + ranking score
    # Easy: clean table OR most text fits in place, and not a huge script.
    score = 0.0
    score += coverage * 40
    score += fit_ratio * 30
    score += max(0.0, 1.0 - jp_total / 5000) * 20  # small scripts score higher
    score += (10 if has_table else 0)

    if score >= 65:
        difficulty = "easy"
    elif score >= 40:
        difficulty = "moderate"
    else:
        difficulty = "hard"

    return FileScore(
        path=path, size=len(data), string_count=string_count,
        jp_char_total=jp_total, has_pointer_table=has_table,
        best_table_coverage=round(coverage, 3),
        est_fit_ratio=round(fit_ratio, 3),
        difficulty=difficulty, score=round(score, 1),
    )


def analyze_image(image_bytes: bytes, *, min_string_count: int = 1, max_files: int = 0) -> list[FileScore]:
    """Score every file in a disc image and return them ranked easiest-first.

    Files with fewer than ``min_string_count`` Japanese strings are skipped
    (no text to translate). ``max_files`` caps how many files are analysed
    (0 = all); large images can have many tiny files.
    """
    from .iso import SaturnImage

    img = SaturnImage(image_bytes)
    scores: list[FileScore] = []
    files = [f for f in img.list_files() if not f.is_dir]
    if max_files:
        files = files[:max_files]
    for entry in files:
        try:
            data = img.extract(entry.path)
        except Exception:
            continue
        fs = analyze_data(entry.path, data)
        if fs.string_count >= min_string_count:
            scores.append(fs)
    scores.sort(key=lambda s: s.score, reverse=True)
    return scores
