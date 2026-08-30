#!/usr/bin/env python3
"""/*----------------------
 | zork_z3objs.py
 | Description: Minimal Z-machine v3 object-table reader (numbers + short names).
 | Author: suinevere
 | Dependencies: struct
 | Globals: N/A
 ----------------------*/"""
import struct

A0 = "      abcdefghijklmnopqrstuvwxyz"
A1 = "      ABCDEFGHIJKLMNOPQRSTUVWXYZ"
A2 = "       \n0123456789.,!?_#'\"/\-:()"


def _zchars(data, addr):
    """/*----------------------
     | _zchars
     | Description: Pull the 5-bit Z-characters of one Z-string.
     | Author: suinevere
     | Dependencies: struct
     | Globals: N/A
     | Params: data -- story bytes; addr -- byte address of the string
     | Returns: (list of z-chars, byte address just past the string)
     ----------------------*/"""
    out = []
    while True:
        w = struct.unpack_from(">H", data, addr)[0]
        addr += 2
        out += [(w >> 10) & 0x1f, (w >> 5) & 0x1f, w & 0x1f]
        if w & 0x8000:
            return out, addr


def ztext(data, addr, abbrev_tbl):
    """/*----------------------
     | ztext
     | Description: Decode a v3 Z-string, expanding abbreviations.
     | Author: suinevere
     | Dependencies: _zchars, struct
     | Globals: A0, A1, A2
     | Params: data -- story bytes; addr -- string address; abbrev_tbl -- table addr
     | Returns: the decoded str
     ----------------------*/"""
    zc, _ = _zchars(data, addr)
    out = []
    alpha = 0
    i = 0
    while i < len(zc):
        c = zc[i]
        if c in (1, 2, 3) and abbrev_tbl:
            i += 1
            if i >= len(zc):
                break
            idx = 32 * (c - 1) + zc[i]
            wa = struct.unpack_from(">H", data, abbrev_tbl + idx * 2)[0]
            out.append(ztext(data, wa * 2, abbrev_tbl))
        elif c == 4:
            alpha = 1
        elif c == 5:
            alpha = 2
        elif c == 0:
            out.append(" ")
        elif alpha == 2 and c == 6:
            i += 2
            out.append(chr(((zc[i - 1] & 0x1f) << 5) | (zc[i] & 0x1f)))
            alpha = 0
        else:
            out.append((A0, A1, A2)[alpha][c])
            alpha = 0
        if c not in (4, 5):
            alpha = 0 if c not in (1, 2, 3) else alpha
        i += 1
    return "".join(out)


def objects(path):
    """/*----------------------
     | objects
     | Description: Enumerate every object in a v3 story file.
     | Author: suinevere
     | Dependencies: struct, ztext
     | Globals: N/A
     | Params: path -- .z3 file path
     | Returns: list of (object number, short name)
     ----------------------*/"""
    d = open(path, "rb").read()
    objtab = struct.unpack_from(">H", d, 0x0A)[0]
    abbrev = struct.unpack_from(">H", d, 0x18)[0]
    first = objtab + 62
    # The first object's property table marks the end of the object entries.
    p0 = struct.unpack_from(">H", d, first + 7)[0]
    count = (p0 - first) // 9
    out = []
    for n in range(1, count + 1):
        e = first + (n - 1) * 9
        prop = struct.unpack_from(">H", d, e + 7)[0]
        nlen = d[prop]
        name = ztext(d, prop + 1, abbrev) if nlen else ""
        out.append((n, name))
    return out
