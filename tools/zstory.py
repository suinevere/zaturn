#!/usr/bin/env python3
"""Pure Z-machine v3 story decoding: header, Z-strings, abbreviations, object table.

Nothing here decides what counts as a room -- that judgement (description
property detection, room identification, routine-vs-string filtering) lives in
tools/gen_room_corpus.py. This module only turns story bytes into Python data:
the header fields, the abbreviation table, and the object table (name,
attributes, parent/sibling/child, and a {property number: (value_addr, length)}
map per object).

Starting point was .superpowers/sdd/2026-08-05-room-categorization-tiers/
zstring-probe-reference.py, a verified-working probe that decoded Zork I's 250
objects and their names. The Z-string tables (A0/A1/A2) and the word/zchar/
decode functions are carried over from it essentially unchanged; what's new
here is packaging them as a reusable Story object instead of a one-shot script.

All 31 stories under tools/assets/Z3 are Z-machine version 3 (verified by
reading raw[0] for each file), so this module only implements the v3 object
format: 31 property-default words, then 9-byte object entries (4 bytes
attributes, 1 byte parent, 1 byte sibling, 1 byte child, 2 bytes property
table address), and size-byte properties where the low 5 bits are the
property number and the top 3 bits are (byte length - 1).
"""
import pathlib

A0 = "abcdefghijklmnopqrstuvwxyz"
A1 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
A2 = " \n0123456789.,!?_#'\"/\\-:()"


def _words(raw, addr):
    """The sequence of 16-bit words making up one Z-string, MSB-terminated."""
    out = []
    while True:
        w = (raw[addr] << 8) | raw[addr + 1]
        out.append(w)
        addr += 2
        if w & 0x8000:
            return out


def _zchars(words):
    """Each word packs three 5-bit Z-characters."""
    out = []
    for w in words:
        out += [(w >> 10) & 31, (w >> 5) & 31, w & 31]
    return out


def decode(raw, addr, abbrev=None, depth=0):
    """Decode the Z-string starting at byte address addr into a Python str.

    abbrev, if given, is the story's 96-entry abbreviation table (decoded
    strings), used to expand z-chars 1-3. depth guards against an abbreviation
    that (incorrectly, but the format doesn't forbid it structurally) refers
    to another abbreviation -- real stories never nest, so depth just caps
    recursion rather than trying to detect the cycle precisely.
    """
    return _decode_chars(raw, _zchars(_words(raw, addr)), abbrev, depth)


def _decode_chars(raw, zs, abbrev, depth):
    s, alpha, i = "", 0, 0
    while i < len(zs):
        z = zs[i]
        if z in (1, 2, 3) and depth < 2 and abbrev is not None:
            i += 1
            if i >= len(zs):
                break
            idx = 32 * (z - 1) + zs[i]
            if idx < len(abbrev):
                s += abbrev[idx]
            i += 1
            continue
        if z == 4:
            alpha = 1; i += 1; continue
        if z == 5:
            alpha = 2; i += 1; continue
        if z == 0:
            s += " "; alpha = 0; i += 1; continue
        if alpha == 2 and z == 6:
            if i + 2 < len(zs):
                s += chr((zs[i + 1] << 5) | zs[i + 2])
            i += 3; alpha = 0; continue
        tbl = (A0, A1, A2)[alpha]
        k = z - 6
        s += tbl[k] if 0 <= k < len(tbl) else "?"
        alpha = 0
        i += 1
    return s


def _load_abbrev(raw):
    """The story's 96 abbreviation strings, or [] if it defines none."""
    base = (raw[0x18] << 8) | raw[0x19]
    if base == 0:
        return []
    out = []
    for i in range(96):
        p = base + i * 2
        waddr = ((raw[p] << 8) | raw[p + 1]) * 2
        try:
            out.append(decode(raw, waddr, None, 9))
        except Exception:
            out.append("")
    return out


class ZObject:
    """One row of the object table: identity, tree position, properties.

    properties maps property number -> (value_byte_address, value_byte_length).
    It holds every property the object carries, not just ones a caller cares
    about -- gen_room_corpus.py's scoring passes need to see all of them.
    """
    __slots__ = ("num", "name", "parent", "sibling", "child", "properties")

    def __init__(self, num, name, parent, sibling, child, properties):
        self.num = num
        self.name = name
        self.parent = parent
        self.sibling = sibling
        self.child = child
        self.properties = properties


def _load_objects(raw, abbrev):
    objbase = (raw[0x0A] << 8) | raw[0x0B]
    entries = objbase + 62  # 31 property-default words
    first_prop = (raw[entries + 7] << 8) | raw[entries + 8]
    count = (first_prop - entries) // 9
    out = []
    for n in range(count):
        e = entries + n * 9
        parent = raw[e + 4]
        sibling = raw[e + 5]
        child = raw[e + 6]
        paddr = (raw[e + 7] << 8) | raw[e + 8]
        tlen = raw[paddr]
        try:
            name = decode(raw, paddr + 1, abbrev) if tlen else ""
        except Exception:
            name = ""
        p = paddr + 1 + tlen * 2
        properties = {}
        while raw[p] != 0:
            size = raw[p]
            num = size & 31
            ln = (size >> 5) + 1
            properties[num] = (p + 1, ln)
            p += 1 + ln
        out.append(ZObject(n + 1, name, parent, sibling, child, properties))
    return out


class Story:
    """A loaded, decoded v3 story file: header fields, abbreviations, objects."""

    def __init__(self, path):
        self.path = pathlib.Path(path)
        self.raw = self.path.read_bytes()
        self.version = self.raw[0]
        self.release = (self.raw[0x02] << 8) | self.raw[0x03]
        self.serial = self.raw[0x12:0x18].decode("ascii", "replace")
        self.abbrev = _load_abbrev(self.raw)
        self.objects = _load_objects(self.raw, self.abbrev)
        self.by_num = {o.num: o for o in self.objects}

    def decode_word_property(self, value_addr):
        """Read the 2-byte value at value_addr as a packed string address and
        decode it. Raises (IndexError, etc.) if that address is out of range
        or otherwise not a valid Z-string -- callers decide what "invalid"
        means for their purposes (a routine address decodes without raising
        but produces garbage, which is a policy call, not this module's)."""
        word = (self.raw[value_addr] << 8) | self.raw[value_addr + 1]
        paddr = word * 2
        return decode(self.raw, paddr, self.abbrev)
