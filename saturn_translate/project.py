"""Translation-table project format.

A translation project is a single JSON file that captures everything needed to
go from an extracted Saturn game file to a translated one: the source file, each
discovered string (offset, original Japanese, length), its translation, and the
pointer slots that reference it. It is the unit of work passed between the
extract → translate → reinsert MCP tools, and it is human-editable so a
translator can refine machine output by hand.
"""

from __future__ import annotations

from dataclasses import dataclass, field, asdict
import json


@dataclass
class StringEntry:
    id: int
    offset: int
    length: int                       # original byte length (excl. terminator)
    original: str                     # Japanese source text
    translation: str = ""             # English (filled by translate step)
    pointer_offsets: list[int] = field(default_factory=list)
    note: str = ""

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class TranslationProject:
    source_file: str                  # path of the extracted file these refer to
    image_path: str = ""              # original disc image, if any
    base_address: int = 0             # pointer base for repointing
    pointer_width: int = 4
    big_endian: bool = True
    strings: list[StringEntry] = field(default_factory=list)

    # ── serialization ──────────────────────────────────────────────────
    def to_dict(self) -> dict:
        d = asdict(self)
        return d

    def to_json(self, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), ensure_ascii=False, indent=indent)

    @classmethod
    def from_dict(cls, d: dict) -> "TranslationProject":
        strings = [StringEntry(**s) for s in d.get("strings", [])]
        return cls(
            source_file=d["source_file"],
            image_path=d.get("image_path", ""),
            base_address=d.get("base_address", 0),
            pointer_width=d.get("pointer_width", 4),
            big_endian=d.get("big_endian", True),
            strings=strings,
        )

    @classmethod
    def from_json(cls, text: str) -> "TranslationProject":
        return cls.from_dict(json.loads(text))

    def save(self, path: str) -> None:
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(self.to_json())

    @classmethod
    def load(cls, path: str) -> "TranslationProject":
        with open(path, "r", encoding="utf-8") as fh:
            return cls.from_json(fh.read())

    # ── stats ──────────────────────────────────────────────────────────
    def stats(self) -> dict:
        total = len(self.strings)
        translated = sum(1 for s in self.strings if s.translation.strip())
        with_ptrs = sum(1 for s in self.strings if s.pointer_offsets)
        return {
            "total_strings": total,
            "translated": translated,
            "untranslated": total - translated,
            "with_pointers": with_ptrs,
            "percent_complete": round(100 * translated / total, 1) if total else 0.0,
        }
