#!/usr/bin/env python3
"""/*----------------------
 | zexits.py
 | Description: The room-to-room exit graph of a v3 story, read off the story
 |     file itself. This is the only description of a game's geography that
 |     exists for all thirty-one of them: the authored map atlas covers
 |     eighteen and places 843 of their rooms, the scene tags cover about half
 |     the rooms, and the exit graph covers every room of every story because
 |     the interpreter could not run without it.
 |
 |     A v3 direction property's LENGTH says what kind of exit it is: 1 byte is
 |     a plain exit and the byte is the room, 2 is a refusal message, 3 is a
 |     routine that decides at run time and yields no room, 4 is a
 |     flag-conditional exit and 5 is a door exit -- both of which carry the
 |     destination in byte 0. Byte 0 is trusted only where it names an object
 |     that is itself a room, the same guard room_model.c applies, because
 |     nothing in the format distinguishes a destination byte from any other.
 |
 |     saturn/tests/test_exit_dests.py decodes the same bytes independently and
 |     on purpose: it is the host oracle room_model.c is checked against, and
 |     an oracle that shared code with anything would stop being one. This
 |     module is for tooling and is not that oracle.
 | Author: suinevere
 | Dependencies: pathlib
 | Globals: ROOT, Z3_DIR, DIR_WORDS
 ----------------------*/"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
Z3_DIR = ROOT / "saturn" / "cd" / "data" / "Z3"

DIR_WORDS = ("north", "east", "west", "south", "ne", "nw", "se", "sw",
             "up", "down", "in", "out")
"""DIR_WORDS

Description: The twelve canonical direction words, in room_model.h's RM_* order
    so an index here means the same thing it does there.
Author: suinevere
"""


def rd16(raw, addr):
    """One big-endian word."""
    return (raw[addr] << 8) | raw[addr + 1]


def story(stem):
    """/*----------------------
     | story
     | Description: One story's bytes, by stem.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: Z3_DIR
     | Params: stem -- the story stem
     | Returns: the raw bytes, or None when the story is not on the disc
     ----------------------*/"""
    p = Z3_DIR / f"{stem}.Z3"
    return p.read_bytes() if p.is_file() else None


def _dict_layout(raw):
    """The dictionary's entry length, count and first entry address."""
    d = rd16(raw, 0x08)
    nsep = raw[d]
    return raw[d + 1 + nsep], rd16(raw, d + 2 + nsep), d + 1 + nsep + 3


def _word(raw, off):
    """A dictionary entry's six letters, through the A0 alphabet only."""
    a0 = "      abcdefghijklmnopqrstuvwxyz"
    out = []
    for k in (0, 2):
        x = rd16(raw, off + k)
        out += [a0[(x >> 10) & 31], a0[(x >> 5) & 31], a0[x & 31]]
    return "".join(out).rstrip()


def direction_properties(raw):
    """/*----------------------
     | direction_properties
     | Description: Property number -> DIR_WORDS index, for the twelve
     |     directions. A dictionary entry flagged FL_DIR is not enough on its
     |     own: a story can flag a verb the same way, so the decoded text must
     |     also match one of the canonical words, and exactly one of the
     |     entry's data bytes must look like a property number.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: DIR_WORDS
     | Params: raw -- the story bytes
     | Returns: {property number: direction index}
     ----------------------*/"""
    elen, n, first = _dict_layout(raw)
    out = {}
    for k in range(n):
        off = first + k * elen
        if not (raw[off + 4] & 0x10):
            continue
        found = [raw[off + i] for i in range(5, elen) if 1 <= raw[off + i] <= 31]
        if len(found) != 1:
            continue
        w = _word(raw, off)
        for i, canon in enumerate(DIR_WORDS):
            j = 0
            while j < len(canon) and j < 6 and j < len(w) and canon[j] == w[j]:
                j += 1
            if (j == len(canon) or j == 6) and j == len(w):
                out[found[0]] = i
                break
    return out


def object_tables(raw):
    """/*----------------------
     | object_tables
     | Description: Object number -> property-list address. Where the object
     |     entries stop is recorded nowhere, so it is inferred from the lowest
     |     property-table address seen so far, tracked as a running minimum
     |     because nothing guarantees object 1 holds the lowest.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: raw -- the story bytes
     | Returns: {object number: property-list address}
     ----------------------*/"""
    entries = rd16(raw, 0x0A) + 62
    out, lowest, n = {}, None, 1
    while True:
        e = entries + (n - 1) * 9
        if e + 9 > len(raw) or (lowest is not None and e >= lowest):
            break
        paddr = rd16(raw, e + 7)
        if paddr == 0 or paddr >= len(raw):
            break
        lowest = paddr if lowest is None else min(lowest, paddr)
        out[n] = paddr
        n += 1
    return out


def properties(raw, paddr):
    """/*----------------------
     | properties
     | Description: One object's own property list.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: raw -- the story bytes; paddr -- the property-list address
     | Returns: {property number: data bytes}
     ----------------------*/"""
    a = paddr + 1 + 2 * raw[paddr]
    out = {}
    while a < len(raw) and raw[a] != 0:
        size = raw[a]
        plen = (size >> 5) + 1
        if a + 1 + plen > len(raw):
            break
        out[size & 31] = raw[a + 1:a + 1 + plen]
        a += 1 + plen
    return out


def graph(raw):
    """/*----------------------
     | graph
     | Description: The exit graph: every room, and where each of its
     |     directions leads. A room is any object carrying at least one
     |     direction property, which is the same test the runtime applies, and
     |     a destination is kept only when it names one -- a 4- or 5-byte
     |     property's byte 0 is a room number in a door or conditional exit and
     |     is arbitrary data in anything else, and nothing in the format says
     |     which it is except whether the object it names is a room.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: raw -- the story bytes
     | Returns: {room object: {direction index: destination room}}
     ----------------------*/"""
    dirs = direction_properties(raw)
    tables = object_tables(raw)
    props = {o: properties(raw, a) for o, a in tables.items()}
    rooms = {o for o, p in props.items() if any(n in dirs for n in p)}

    out = {}
    for o in sorted(rooms):
        exits = {}
        for num, data in props[o].items():
            if num not in dirs:
                continue
            if len(data) in (1, 4, 5):
                dest = data[0]
                if dest in rooms:
                    exits[dirs[num]] = dest
        out[o] = exits
    return out


def neighbours(exits):
    """/*----------------------
     | neighbours
     | Description: The graph made undirected. A one-way exit still joins two
     |     rooms into the same place -- the trap door into the Cellar is one
     |     way and the Cellar is plainly part of what is under the house -- so
     |     direction is dropped once geography is the question.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: exits -- graph()'s result
     | Returns: {room: set of adjacent rooms}
     ----------------------*/"""
    adj = {o: set() for o in exits}
    for o, e in exits.items():
        for dest in e.values():
            if dest == o:
                continue
            adj[o].add(dest)
            adj.setdefault(dest, set()).add(o)
    return adj
