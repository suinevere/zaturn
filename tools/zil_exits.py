#!/usr/bin/env python3
"""Zork I's conditional and routine exits, read from its ZIL source.

This repository carries the ZIL for exactly one story, and that is what makes
Zork I the calibration game: a passage traced off the scanned map can be
scored against the source rather than against the exit graph the scan is meant
to audit. Nothing here is shipped -- it exists to measure the scanner.

Exit clauses wrap. SOUTH-TEMPLE's DOWN spans three lines and WHITE-CLIFFS-
SOUTH's NORTH spans two, so a room's whole block is collapsed to one line
before it is matched; a line-at-a-time reader silently misses both.
"""
import re

DIRS = ("NORTH", "SOUTH", "EAST", "WEST", "NE", "NW", "SE", "SW",
        "UP", "DOWN", "IN", "OUT")

_ROOM = re.compile(r"<ROOM\s+([A-Z0-9-]+)")
_COND = re.compile(r"\(\s*(" + "|".join(DIRS) +
                   r")\s+TO\s+([A-Z0-9-]+)\s+IF\s+([A-Z0-9-]+)")
_PER = re.compile(r"\(\s*(" + "|".join(DIRS) + r")\s+PER\s+([A-Z0-9-]+)")


def _blocks(path):
    """(room symbol, its whole definition collapsed to one line) for each room."""
    with open(path, "r", encoding="latin-1") as f:
        text = f.read()
    out = []
    for chunk in text.split("<ROOM ")[1:]:
        m = _ROOM.match("<ROOM " + chunk)
        if m:
            out.append((m.group(1), " ".join(chunk.split())))
    return out


def conditional_exits(path):
    """{flag: sorted [(room, direction, destination)]} for every IF-gated exit.

    The flag is the interesting key: what a passage is conditional *on* is the
    thing the compiled story never records, and the thing the map draws.
    """
    found = {}
    for room, body in _blocks(path):
        for direction, dest, flag in _COND.findall(body):
            found.setdefault(flag, []).append((room, direction, dest))
    return {k: sorted(v) for k, v in found.items()}


def routine_exits(path):
    """Sorted [(room, direction, routine)] for every PER exit.

    A routine exit's destination is computed, so it exists nowhere in the
    compiled data and the map's own drawing is the only record of where it
    goes.
    """
    out = []
    for room, body in _blocks(path):
        for direction, routine in _PER.findall(body):
            out.append((room, direction, routine))
    return sorted(out)
