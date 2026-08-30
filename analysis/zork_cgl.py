#!/*----------------------
# | zork_cgl.py
# | Description: Reader for Zork I (Saturn, JP) room-background archives (B*.CGL).
# | Author: suinevere
# | Dependencies: struct, PIL (only for save_png)
# | Globals: N/A
# ----------------------*/
"""A ``B*.CGL`` file is a back-to-back chain of 4-byte-aligned records:

    [256-entry RGB555 LE CLUT = 512 bytes][LZSS stream -> 320x240 8bpp pixels]

The LZSS is the same Okumura variant used by ``*.CGZ``/``*.SLD`` (4-byte LE
decompressed size, 4 KiB ring init 0, write pointer at n-18).  Every record
carries its own palette, so each room background is fully self-contained.
"""
import struct

# /*----------------------
#  | Module constants
#  | Description: Fixed geometry of a CGL record.
#  | Author: suinevere
#  ----------------------*/
PAL_BYTES = 512
WIDTH = 320
HEIGHT = 240
FRAME_BYTES = WIDTH * HEIGHT


def _lzss(buf, start):
    """/*----------------------
     | _lzss
     | Description: Decompress one LZSS stream and report where it ended.
     | Author: suinevere
     | Dependencies: struct
     | Globals: N/A
     | Params: buf -- whole file bytes; start -- offset of the 4-byte size header
     | Returns: (decompressed bytes, offset just past the stream)
     ----------------------*/"""
    size = struct.unpack_from("<I", buf, start)[0]
    n = 4096
    ring = bytearray(n)
    r = n - 18
    out = bytearray()
    i = start + 4
    L = len(buf)
    flags = 0
    nbits = 0
    while i < L and len(out) < size:
        if nbits == 0:
            flags = buf[i]; i += 1; nbits = 8
        bit = flags & 1; flags >>= 1; nbits -= 1
        if bit:
            c = buf[i]; i += 1
            out.append(c); ring[r] = c; r = (r + 1) & (n - 1)
        else:
            if i + 1 >= L:
                break
            b0 = buf[i]; b1 = buf[i + 1]; i += 2
            off = ((b1 & 0xf0) << 4) | b0
            ln = (b1 & 0x0f) + 3
            for k in range(ln):
                c = ring[(off + k) & (n - 1)]
                out.append(c); ring[r] = c; r = (r + 1) & (n - 1)
    return bytes(out), i


def load_clut(buf, off=0):
    """/*----------------------
     | load_clut
     | Description: Expand a 256-entry RGB555 little-endian CLUT to 8-bit RGB.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: PAL_BYTES
     | Params: buf -- file bytes; off -- offset of the CLUT
     | Returns: list of 256 (r, g, b) tuples
     ----------------------*/"""
    pal = []
    for i in range(off, off + PAL_BYTES, 2):
        v = buf[i] | (buf[i + 1] << 8)
        pal.append(((v & 0x1f) * 255 // 31,
                    ((v >> 5) & 0x1f) * 255 // 31,
                    ((v >> 10) & 0x1f) * 255 // 31))
    return pal


def records(buf):
    """/*----------------------
     | records
     | Description: Walk every CLUT+image record in a CGL archive.
     | Author: suinevere
     | Dependencies: struct, _lzss, load_clut
     | Globals: PAL_BYTES, FRAME_BYTES
     | Params: buf -- whole .CGL file bytes
     | Returns: yields (index, file_offset, palette, pixel bytes)
     ----------------------*/"""
    pos = 0
    idx = 0
    while pos + PAL_BYTES + 4 <= len(buf):
        size = struct.unpack_from("<I", buf, pos + PAL_BYTES)[0]
        if size == 0 or size > 1 << 20:
            break
        pal = load_clut(buf, pos)
        data, nxt = _lzss(buf, pos + PAL_BYTES)
        if len(data) != size:
            break
        yield idx, pos, pal, data
        idx += 1
        pos = (nxt + 3) & ~3


def save_png(path, pal, pixels, width=WIDTH, height=HEIGHT):
    """/*----------------------
     | save_png
     | Description: Write one 8bpp CGL frame out as a paletted PNG.
     | Author: suinevere
     | Dependencies: PIL.Image
     | Globals: WIDTH, HEIGHT
     | Params: path -- output file; pal -- 256 RGB tuples; pixels -- 8bpp bytes
     | Returns: N/A
     ----------------------*/"""
    from PIL import Image
    im = Image.frombytes("P", (width, height), pixels[:width * height])
    flat = []
    for rgb in pal:
        flat.extend(rgb)
    im.putpalette(flat)
    im.convert("RGB").save(path)
