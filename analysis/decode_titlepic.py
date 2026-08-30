#!/usr/bin/env python3
"""Standalone port of Steamgear Mash's 'PIC' image codec, reverse-engineered from
0.c (FUN_06005cf8 / 060059f0 / 06005ade / 060041dc / 060041a0 + the move-to-front
colour dictionary).  Decodes TITLEPIC.BIN and writes a 24-bit BMP for eyeballing.

Codec summary (base 0x06004000, SH-2 big-endian):
  FUN_06005cf8  entry: check 'PIC\\x1a' magic (8 bits at a time), skip to 0x1a then
                0x00, skip two 16-bit fields, width=read16(), height=read16(); decode.
  FUN_060059f0  main loop: run-length via exp-Golomb reader (060041dc); paint pixels
                into a 512-stride framebuffer; pick up already-painted pixels.
  FUN_06005ade  chain-code boundary fill: 2-bit direction tokens walk x by +-1/+-2.
  FUN_06005c8a  colour: 1 bit -> new 15-bit RGB555 literal (insert) or 7-bit dict idx
                (move-to-front).  Dict = 128-node circular doubly-linked list.
  Shared bit-reader state: bitbuf @0x0608355C, bitcount @0x06083554, strm @0x06083558.
  Output base 0x25E40000 (VDP2 VRAM), pixels = colour | 0x8000, stride 0x200 shorts.
"""
import struct, sys, os

TRACK = r"C:\Users\saggl\IdeaProjects\AI Sega Saturn Translation MCP Server\game_originals\Steamgear Mash (Japan)\Steamgear Mash (Japan) (Track 01).bin"
MAGIC_TRACK_OFF = 0x5967370          # 'PIC\x1a' located in the raw track
OUT = os.path.join(os.path.dirname(__file__), "titlepic.bmp")

SECTOR, UHDR, USIZE = 2352, 16, 2048  # MODE1/2352
ORMASK = 0x8000
STRIDE = 0x200                         # 512 shorts per row


def extract_stream(track_path, magic_off, n_sectors=80):
    """De-frame the MODE1/2352 track into the contiguous user-data byte stream that
    was present in RAM, starting exactly at the PIC magic."""
    with open(track_path, "rb") as f:
        data = f.read()
    sec = magic_off // SECTOR
    within = magic_off % SECTOR
    wd = within - UHDR                 # offset of magic inside the sector's user data
    assert 0 <= wd < USIZE, f"magic not in user-data region (within={within})"
    out = bytearray()
    for i in range(sec, min(sec + n_sectors, len(data) // SECTOR)):
        base = i * SECTOR + UHDR
        out += data[base:base + USIZE]
    stream = bytes(out[wd:])
    # pad to a multiple of 4 (reader consumes 32-bit big-endian words)
    if len(stream) % 4:
        stream += b"\x00" * (4 - len(stream) % 4)
    return stream


class Codec:
    def __init__(self, stream):
        self.words = list(struct.unpack(f">{len(stream)//4}I", stream))
        self.pos = 0
        self.buf = 0
        self.cnt = 0
        # colour dictionary: 128-node circular doubly-linked list
        self.val = [0] * 128
        self.prev = [(i - 1) for i in range(128)]
        self.next = [(i + 1) for i in range(128)]
        self.prev[0] = 127
        self.next[127] = 0
        self.head = 0

    # --- bit reader (FUN_060041a0 / shared state) -----------------------------
    def _bit(self):
        if self.cnt == 0:
            self.buf = self.words[self.pos]
            self.pos += 1
            self.cnt = 32
        b = (self.buf >> 31) & 1
        self.buf = (self.buf << 1) & 0xFFFFFFFF
        self.cnt -= 1
        return b

    def read_bits(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | self._bit()
        return v

    # --- exp-Golomb run length (FUN_060041dc) ---------------------------------
    def read_gamma(self):
        ones = 0
        nbits = 0
        while True:
            b = self._bit()
            nbits += 1
            if b == 0:
                break
            ones = (ones << 1) | 1          # 2^k - 1 after k ones
        v2 = 0
        for _ in range(nbits):
            v2 = (v2 << 1) | self._bit()
        return v2 + ((ones << 1) | 1)       # + (2^(k+1) - 1)

    # --- colour dictionary (FUN_06005c8a / b9e / bdc) -------------------------
    def _promote(self, idx):                # FUN_06005bdc
        h = self.head
        if h != idx:
            ni, pi = self.next[idx], self.prev[idx]
            self.prev[ni] = pi
            self.next[pi] = ni
            hn = self.next[h]               # read after unlink (matches asm)
            self.prev[hn] = idx
            self.next[idx] = hn
            self.next[h] = idx
            self.prev[idx] = h
            self.head = idx
        return self.val[idx]

    def get_colour(self):                   # FUN_06005c8a
        if self._bit() == 0:
            c = self.read_bits(15)          # new RGB555 literal
            self.head = self.next[self.head]   # FUN_06005b9e: recycle LRU slot
            self.val[self.head] = c
            return c
        idx = self.read_bits(7)             # dict index
        return self._promote(idx)

    # --- chain-code boundary fill (FUN_06005ade) ------------------------------
    def contour(self, x, y, colour, fb, w, h):
        alive = True
        while True:
            d = self.read_bits(2)
            if d == 0:
                if self._bit() == 0:
                    return
                x += 2 if self._bit() else -2
            elif d == 1:
                x -= 1
            elif d == 3:
                x += 1
            # d == 2: no horizontal change
            if x >= w:
                break
            y += 1
            if y >= h:
                alive = False
            if alive:
                idx = y * STRIDE + x
                if 0 <= idx < len(fb):
                    fb[idx] = colour | ORMASK

    # --- main loop (FUN_060059f0) ---------------------------------------------
    def decode_body(self, w, h):
        fb = [0] * (h * STRIDE + STRIDE)
        x, y, colour = -1, 0, 0
        while True:
            run = self.read_gamma()
            n = run
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
                idx = y * STRIDE + x
                px = fb[idx]
                if px != 0:
                    colour = px
                fb[idx] = colour | ORMASK
            x += 1
            if x == w:
                y += 1
                if y == h:
                    return fb
                x = 0
            colour = self.get_colour()
            fb[y * STRIDE + x] = colour | ORMASK
            if self._bit() == 1:
                self.contour(x, y, colour, fb, w, h)

    # --- entry (FUN_06005cf8) -------------------------------------------------
    def decode(self):
        if not (self.read_bits(8) == 0x50 and self.read_bits(8) == 0x49
                and self.read_bits(8) == 0x43):
            raise ValueError("PIC magic not found at stream start")
        while self.read_bits(8) != 0x1A:
            pass
        while self.read_bits(8) != 0x00:
            pass
        self.read_bits(16)
        self.read_bits(16)
        w = self.read_bits(16)
        h = self.read_bits(16)
        print(f"  PIC header parsed: width={w} (0x{w:X}), height={h} (0x{h:X})")
        if not (0 < w <= 512 and 0 < h <= 512):
            raise ValueError(f"implausible dimensions {w}x{h}")
        fb = self.decode_body(w, h)
        return w, h, fb


def rgb555(c):
    c &= 0x7FFF
    r = (c & 0x1F) * 255 // 31
    g = ((c >> 5) & 0x1F) * 255 // 31
    b = ((c >> 10) & 0x1F) * 255 // 31
    return r, g, b


def write_bmp(path, w, h, fb):
    row_bytes = (w * 3 + 3) & ~3
    pixels = bytearray()
    for y in range(h - 1, -1, -1):       # BMP is bottom-up
        row = bytearray()
        for x in range(w):
            r, g, b = rgb555(fb[y * STRIDE + x])
            row += bytes((b, g, r))       # BMP stores BGR
        row += b"\x00" * (row_bytes - len(row))
        pixels += row
    size = 54 + len(pixels)
    hdr = b"BM" + struct.pack("<IHHI", size, 0, 0, 54)
    hdr += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(hdr + pixels)


def main():
    print(f"Extracting de-framed PIC stream from track @0x{MAGIC_TRACK_OFF:X} ...")
    stream = extract_stream(TRACK, MAGIC_TRACK_OFF)
    print(f"  stream bytes: {len(stream)} (0x{len(stream):X}); first 8: {stream[:8].hex()}")
    codec = Codec(stream)
    w, h, fb = codec.decode()
    nonzero = sum(1 for v in fb if v)
    print(f"  decoded; non-zero pixels: {nonzero} / {w*h}")
    write_bmp(OUT, w, h, fb)
    print(f"  wrote {OUT}")


if __name__ == "__main__":
    main()
