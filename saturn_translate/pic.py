"""Decoder for the T-103 "PIC" image codec used by *Steamgear Mash* (Japan).

Reverse-engineered from the game's SH-2 code (``0.BIN`` functions ``FUN_06005cf8``
→ ``FUN_060059f0`` / ``FUN_06005ade`` plus the bit-readers ``FUN_060041dc`` /
``FUN_060041a0`` and the move-to-front colour dictionary). Verified byte-exact: the
disc's ``TITLEPIC.BIN`` decodes to the correct 320×244 "STEAMGEAR Mash" title image.
See ``docs/STEAMGEAR_MASH_RECON.md`` for the full provenance.

Format: header ``"PIC\\x1a"`` then a null, two skipped 16-bit fields, ``width`` and
``height`` (16-bit), then a single MSB-first big-endian bitstream consumed by:

* an exp-Golomb run-length reader (run = ``read_bits(k+1) + 2**(k+1) - 1`` where *k*
  is the number of leading 1-bits),
* a move-to-front colour dictionary (1 control bit: 0 ⇒ new 15-bit RGB555 literal
  recycling the LRU slot, 1 ⇒ 7-bit index promoted to the front),
* a chain-code contour fill (2-bit direction tokens walking x by ±1/±2 down rows).

Pixels are 15-bit Saturn RGB555 with bit 15 set as the "written" flag; value 0 means
untouched/background.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

MAGIC = b"PIC\x1a"
_OR = 0x8000          # bit15 "pixel written" flag the codec ORs into every pixel
_STRIDE = 0x200       # framebuffer row stride in 16-bit words (512), per the SH-2 code
_DICT = 128           # colour-dictionary entries (7-bit index)


@dataclass
class PicImage:
    width: int
    height: int
    pixels: list[int]          # row-major, width*height, raw 16-bit (incl. bit15)

    def rgb(self) -> list[tuple[int, int, int]]:
        """Pixels as 8-bit (r, g, b) tuples."""
        return [rgb555(p) for p in self.pixels]

    def to_bmp(self) -> bytes:
        return to_bmp(self)


def rgb555(value: int) -> tuple[int, int, int]:
    """Saturn RGB555 (R low, B high) → 8-bit (r, g, b)."""
    value &= 0x7FFF
    r = (value & 0x1F) * 255 // 31
    g = ((value >> 5) & 0x1F) * 255 // 31
    b = ((value >> 10) & 0x1F) * 255 // 31
    return r, g, b


class _BitReader:
    """MSB-first reader over big-endian 32-bit words (shared codec bit state)."""

    __slots__ = ("words", "pos", "buf", "cnt")

    def __init__(self, data: bytes, offset: int):
        if offset % 4:                      # the SH-2 stream pointer is word-aligned
            data = data[offset - (offset % 4):]
            offset %= 4
        body = data[offset:]
        if len(body) % 4:
            body += b"\x00" * (4 - len(body) % 4)
        self.words = list(struct.unpack(f">{len(body) // 4}I", body))
        self.pos = 0
        self.buf = 0
        self.cnt = 0

    def bit(self) -> int:
        if self.cnt == 0:
            self.buf = self.words[self.pos]
            self.pos += 1
            self.cnt = 32
        b = (self.buf >> 31) & 1
        self.buf = (self.buf << 1) & 0xFFFFFFFF
        self.cnt -= 1
        return b

    def bits(self, n: int) -> int:          # FUN_060041a0
        v = 0
        for _ in range(n):
            v = (v << 1) | self.bit()
        return v

    def gamma(self) -> int:                 # FUN_060041dc
        ones = nbits = 0
        while True:
            b = self.bit()
            nbits += 1
            if b == 0:
                break
            ones = (ones << 1) | 1
        v = 0
        for _ in range(nbits):
            v = (v << 1) | self.bit()
        return v + ((ones << 1) | 1)


class _Decoder:
    def __init__(self, data: bytes, offset: int):
        self.r = _BitReader(data, offset)
        # 128-node circular doubly-linked colour dictionary (FUN_06005cbc)
        self.val = [0] * _DICT
        self.prv = [i - 1 for i in range(_DICT)]
        self.nxt = [i + 1 for i in range(_DICT)]
        self.prv[0] = _DICT - 1
        self.nxt[_DICT - 1] = 0
        self.head = 0

    def _promote(self, idx: int) -> int:    # FUN_06005bdc (move-to-front)
        h = self.head
        if h != idx:
            ni, pi = self.nxt[idx], self.prv[idx]
            self.prv[ni] = pi
            self.nxt[pi] = ni
            hn = self.nxt[h]
            self.prv[hn] = idx
            self.nxt[idx] = hn
            self.nxt[h] = idx
            self.prv[idx] = h
            self.head = idx
        return self.val[idx]

    def _colour(self) -> int:               # FUN_06005c8a
        if self.r.bit() == 0:
            c = self.r.bits(15)
            self.head = self.nxt[self.head]  # FUN_06005b9e: overwrite LRU slot
            self.val[self.head] = c
            return c
        return self._promote(self.r.bits(7))

    def _contour(self, x, y, colour, fb, w, h):   # FUN_06005ade
        alive = True
        while True:
            d = self.r.bits(2)
            if d == 0:
                if self.r.bit() == 0:
                    return
                x += 2 if self.r.bit() else -2
            elif d == 1:
                x -= 1
            elif d == 3:
                x += 1
            if x >= w:
                break
            y += 1
            if y >= h:
                alive = False
            if alive:
                idx = y * _STRIDE + x
                if 0 <= idx < len(fb):
                    fb[idx] = colour | _OR

    def _body(self, w, h):                  # FUN_060059f0
        fb = [0] * (h * _STRIDE + _STRIDE)
        x, y, colour = -1, 0, 0
        while True:
            n = self.r.gamma()
            while True:
                n -= 1
                if n == 0:
                    break
                x += 1
                if x == w:
                    y += 1
                    if y == h:
                        return fb
                    x = 0
                px = fb[y * _STRIDE + x]
                if px != 0:
                    colour = px
                fb[y * _STRIDE + x] = colour | _OR
            x += 1
            if x == w:
                y += 1
                if y == h:
                    return fb
                x = 0
            colour = self._colour()
            fb[y * _STRIDE + x] = colour | _OR
            if self.r.bit() == 1:
                self._contour(x, y, colour, fb, w, h)

    def decode(self) -> PicImage:           # FUN_06005cf8
        r = self.r
        if not (r.bits(8) == 0x50 and r.bits(8) == 0x49 and r.bits(8) == 0x43):
            raise ValueError("not a PIC stream (magic mismatch)")
        while r.bits(8) != 0x1A:
            pass
        while r.bits(8) != 0x00:
            pass
        r.bits(16)
        r.bits(16)
        w = r.bits(16)
        h = r.bits(16)
        if not (0 < w <= 512 and 0 < h <= 512):
            raise ValueError(f"implausible PIC dimensions {w}x{h}")
        fb = self._body(w, h)
        pixels = [fb[y * _STRIDE + x] for y in range(h) for x in range(w)]
        return PicImage(w, h, pixels)


def decode_pic(data: bytes, offset: int = 0) -> PicImage:
    """Decode a PIC image whose ``"PIC\\x1a"`` magic starts at ``data[offset]``."""
    return _Decoder(data, offset).decode()


def find_pic(data: bytes) -> int | None:
    """Return the offset of the first ``"PIC\\x1a"`` magic, or ``None``."""
    i = data.find(MAGIC)
    return None if i < 0 else i


def to_bmp(img: PicImage) -> bytes:
    """Render a :class:`PicImage` to a 24-bit BMP (bottom-up)."""
    row_bytes = (img.width * 3 + 3) & ~3
    pad = b"\x00" * (row_bytes - img.width * 3)
    body = bytearray()
    for y in range(img.height - 1, -1, -1):
        for x in range(img.width):
            r, g, b = rgb555(img.pixels[y * img.width + x])
            body += bytes((b, g, r))
        body += pad
    header = b"BM" + struct.pack("<IHHI", 54 + len(body), 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, img.width, img.height,
                          1, 24, 0, len(body), 2835, 2835, 0, 0)
    return header + bytes(body)
