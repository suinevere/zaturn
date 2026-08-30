"""Minimal ISO9660 reader for Sega Saturn disc images.

Saturn discs use a standard ISO9660 filesystem on top of CD sectors. Images come
in two common shapes:

* **2048-byte sectors** ("user data only", i.e. a plain .iso) — the usual case.
* **2352-byte raw sectors** (Mode 1, as in many .bin dumps) — each sector has a
  16-byte sync/header before the 2048 bytes of user data, then 288 bytes of
  EDC/ECC.

This reader auto-detects the layout (by locating the ``SEGA SEGASATURN`` IP
header and the ``CD001`` ISO9660 identifier) and exposes the directory tree plus
raw file extraction. It is read-only; rebuilding is handled in :mod:`build`.
"""

from __future__ import annotations

from dataclasses import dataclass, asdict
import struct

SECTOR_USER = 2048
RAW_2352 = 2352
RAW_HEADER = 16  # sync(12) + header(4) in a Mode 1 raw sector


@dataclass
class DirEntry:
    name: str
    lba: int          # starting logical block (sector) of the file's extent
    size: int         # size in bytes
    is_dir: bool
    path: str         # full path within the image, e.g. "/DATA/SCENARIO.BIN"

    def to_dict(self) -> dict:
        return asdict(self)


class SaturnImage:
    """Read-only view over a Saturn disc image."""

    def __init__(self, raw: bytes):
        self._raw = raw
        self.sector_size, self.data_offset = self._detect_layout(raw)

    # ── layout detection ───────────────────────────────────────────────
    @staticmethod
    def _detect_layout(raw: bytes) -> tuple[int, int]:
        # 2048-byte image: IP header sits at byte 0.
        if raw[0:16] == b"SEGA SEGASATURN ":
            return SECTOR_USER, 0
        # 2352 raw: user data begins 16 bytes into each sector.
        if raw[RAW_HEADER : RAW_HEADER + 16] == b"SEGA SEGASATURN ":
            return RAW_2352, RAW_HEADER
        # Fall back to probing for CD001 at sector 16 for both layouts.
        if raw[16 * SECTOR_USER + 1 : 16 * SECTOR_USER + 6] == b"CD001":
            return SECTOR_USER, 0
        if raw[16 * RAW_2352 + RAW_HEADER + 1 : 16 * RAW_2352 + RAW_HEADER + 6] == b"CD001":
            return RAW_2352, RAW_HEADER
        # Default to plain 2048.
        return SECTOR_USER, 0

    @classmethod
    def from_file(cls, path: str) -> "SaturnImage":
        with open(path, "rb") as fh:
            return cls(fh.read())

    # ── sector access ──────────────────────────────────────────────────
    def read_sector(self, lba: int) -> bytes:
        """Return the 2048 bytes of user data for logical block ``lba``."""
        start = lba * self.sector_size + self.data_offset
        return self._raw[start : start + SECTOR_USER]

    def read_extent(self, lba: int, size: int) -> bytes:
        """Read ``size`` bytes starting at sector ``lba`` (across sectors)."""
        out = bytearray()
        sector = lba
        remaining = size
        while remaining > 0:
            chunk = self.read_sector(sector)
            take = min(len(chunk), remaining)
            out += chunk[:take]
            remaining -= take
            sector += 1
        return bytes(out)

    # ── ISO9660 parsing ────────────────────────────────────────────────
    def _pvd(self) -> bytes:
        # Primary Volume Descriptor lives at sector 16.
        return self.read_sector(16)

    def root_entry(self) -> DirEntry:
        pvd = self._pvd()
        # Root directory record is 34 bytes at offset 156 in the PVD.
        rec = pvd[156 : 156 + 34]
        lba = struct.unpack_from("<I", rec, 2)[0]      # LBA (little-endian copy)
        size = struct.unpack_from("<I", rec, 10)[0]    # extent size
        return DirEntry(name="", lba=lba, size=size, is_dir=True, path="/")

    def _read_dir(self, entry: DirEntry) -> list[DirEntry]:
        raw = self.read_extent(entry.lba, entry.size)
        entries: list[DirEntry] = []
        pos = 0
        while pos < len(raw):
            rec_len = raw[pos]
            if rec_len == 0:
                # padding to next sector boundary
                next_boundary = ((pos // SECTOR_USER) + 1) * SECTOR_USER
                if next_boundary >= len(raw):
                    break
                pos = next_boundary
                continue
            rec = raw[pos : pos + rec_len]
            lba = struct.unpack_from("<I", rec, 2)[0]
            size = struct.unpack_from("<I", rec, 10)[0]
            flags = rec[25]
            name_len = rec[32]
            name_bytes = rec[33 : 33 + name_len]
            pos += rec_len

            # 0x00 = "." , 0x01 = ".." special records
            if name_len == 1 and name_bytes in (b"\x00", b"\x01"):
                continue
            name = name_bytes.decode("ascii", errors="replace")
            # strip ISO9660 version suffix ";1"
            if ";" in name:
                name = name.split(";", 1)[0]
            is_dir = bool(flags & 0x02)
            child_path = (entry.path.rstrip("/") + "/" + name) if entry.path != "/" else "/" + name
            entries.append(
                DirEntry(name=name, lba=lba, size=size, is_dir=is_dir, path=child_path)
            )
        return entries

    def list_files(self, recursive: bool = True) -> list[DirEntry]:
        """List all files (and directories) in the image."""
        out: list[DirEntry] = []
        stack = [self.root_entry()]
        while stack:
            cur = stack.pop()
            for child in self._read_dir(cur):
                out.append(child)
                if child.is_dir and recursive:
                    stack.append(child)
        out.sort(key=lambda e: e.path)
        return out

    def find(self, path: str) -> DirEntry | None:
        """Find a single entry by its full image path (case-insensitive)."""
        want = path.upper().rstrip("/")
        if want == "":
            return self.root_entry()
        for e in self.list_files():
            if e.path.upper() == want:
                return e
        return None

    def extract(self, path: str) -> bytes:
        """Return the raw bytes of the file at ``path``."""
        entry = self.find(path)
        if entry is None:
            raise FileNotFoundError(path)
        if entry.is_dir:
            raise IsADirectoryError(path)
        return self.read_extent(entry.lba, entry.size)

    def file_byte_offset(self, path: str) -> int:
        """Absolute byte offset of a file's data in the underlying image.

        Useful for writing changes straight back into a 2048-byte image without
        rebuilding it (works only when the new data is the same length).
        """
        entry = self.find(path)
        if entry is None:
            raise FileNotFoundError(path)
        return entry.lba * self.sector_size + self.data_offset
