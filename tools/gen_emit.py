#!/usr/bin/env python3
"""/*----------------------
 | gen_emit.py
 | Description: The one way the generators put a generated source file on disk.
 |
 |     Writing is skipped when the bytes are already what they would be. A
 |     generator here is idempotent by design and several tests prove it by
 |     regenerating and diffing, so the common case is a rewrite that changes
 |     nothing -- and a rewrite that changes nothing still moves the mtime,
 |     which is what make and every staleness check in this repo read. That is
 |     not free: it made saturn/tests/test_hwram_budget.py skip its whole file
 |     for the rest of a session, because test_gen_presentation.py had rewritten
 |     game_presentation.inc byte for byte and the link map then looked older
 |     than a source that had not actually changed.
 | Author: suinevere
 | Dependencies: pathlib
 | Globals: N/A
 ----------------------*/"""
import pathlib


def write_if_changed(path, text):
    """/*----------------------
     | write_if_changed
     | Description: Writes text to path as LF bytes, unless that is already
     |     exactly what is there. Compared as bytes rather than as text so no
     |     newline translation can make an identical file look different.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: N/A
     | Params: path -- where to write; text -- the whole file
     | Returns: True if the file was written, False if it already matched
     ----------------------*/"""
    path = pathlib.Path(path)
    data = text.encode("utf-8")
    if path.is_file() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True
