#!/usr/bin/env python3
"""/*----------------------
 | cgl_archive.py
 | Description: Packs CGL records into B*.CGL archives -- the container half of
 |     the writer, where cgl_encode.py is the record half.
 |
 |     An archive is nothing but its records laid end to end. Each one is
 |     already padded to four bytes by cgl_encode.record, which is what lets
 |     the next record start where the last one ended and what makes a frame's
 |     offset simply the length of everything before it. Nothing in the file
 |     says where the records are: the runtime reaches a frame through
 |     IMAGE_FRAME's generated offset, because a record's end is only found by
 |     decompressing it and walking to a late frame would cost every earlier
 |     one. That is why this returns the placements as well as the bytes --
 |     they are the only record of where anything is, and they have to reach
 |     the table.
 |
 |     Archives roll at a cap rather than growing without bound. room_art.cxx
 |     reads a whole archive into Low Work RAM and holds it there beside the
 |     game's trie and the save scratch, so an archive's size is a hard claim
 |     on a megabyte that already has four claimants
 |     (saturn/tests/test_lwram_budget.py). The cap is well under the largest
 |     archive the original disc shipped, so adding generated art cannot
 |     make that budget worse than the measured frames already make it.
 | Author: suinevere
 | Dependencies: hashlib, string, cgl_encode, zork_cgl
 | Globals: CAP, MAX_STEM
 ----------------------*/"""
import hashlib
import pathlib
import string
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "analysis"))

import cgl_encode
import zork_cgl

CAP = 262144
MAX_STEM = 8
"""CAP / MAX_STEM

Description: The byte ceiling one generated archive may reach, and the longest
    archive stem room_art.cxx's load_area will build a filename out of (it
    copies at most eight characters before appending ".CGL"). The cap is 256 KB
    against BCEL's 418,264: a generated archive that stays under it can never be
    the largest thing the LWRAM budget is measured against.
Author: suinevere
"""


def stems(prefix, n):
    """/*----------------------
     | stems
     | Description: The archive stems a run of n archives would be called, so a
     |     caller can name them before the bytes exist. Lettered rather than
     |     numbered: every archive on the original disc is letters only, and so
     |     are the regexes two other generators read PRES_AREA back with.
     |     Refuses a stem too long for load_area's eight-character buffer, and
     |     refuses to run out of names, rather than producing a filename the
     |     runtime would look for and not find.
     |
     |     Two letters, not one. One letter was enough while a picture served
     |     many rooms; a picture per room is about 140 archives and the single
     |     letter ran out at 34, which is where this refused -- correctly, but
     |     it stopped the run. Two letters name 676, which is past anything the
     |     LWRAM budget would allow on a disc.
     | Author: suinevere
     | Dependencies: string
     | Globals: MAX_STEM
     | Params: prefix -- the stem prefix; n -- how many archives
     | Returns: a list of n stems
     ----------------------*/"""
    alphabet = string.ascii_uppercase
    if n > len(alphabet) ** 2:
        raise ValueError(f"{n} archives is more than the {len(alphabet) ** 2} "
                         "a two-letter suffix can name; raise the cap or split "
                         "the manifest")
    out = [prefix + alphabet[i // len(alphabet)] + alphabet[i % len(alphabet)]
           for i in range(n)]
    for s in out:
        if len(s) > MAX_STEM:
            raise ValueError(f"archive stem {s!r} is longer than the {MAX_STEM} "
                             "characters load_area builds a filename from")
    return out


def pack(records, cap=CAP, keys=None):
    """/*----------------------
     | pack
     | Description: Lays complete CGL records end to end into as many archives
     |     as the cap needs, and says where each one landed.
     |
     |     A record larger than the cap on its own is placed alone rather than
     |     refused -- the cap is a budget preference and a single oversized
     |     frame is still a frame, but it gets an archive to itself so it
     |     cannot drag a neighbour over with it.
     |
     |     `keys` groups the records: an archive never spans two keys, so a
     |     boundary between archives is always a boundary between whatever the
     |     key names. That is how the original disc was laid out and it is the
     |     whole reason a resident archive is affordable. Its eleven archives are
     |     places -- the cellar, the maze, the river -- and every one of its 54
     |     archive crossings is also a CD-DA track change, all 54, because each
     |     non-silent track lives in exactly one archive. The reload therefore
     |     lands on the step where the music was changing anyway and interrupts
     |     nothing. Records are expected already grouped by key; a key that
     |     reappears later simply opens another archive.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: CAP
     | Params: records -- complete records from cgl_encode.record;
     |     cap -- the byte ceiling one archive may reach; keys -- one grouping
     |     key per record, or None to pack purely by size
     | Returns: (list of archive bytes,
     |     list of (archive index, offset, length) parallel to records)
     ----------------------*/"""
    archives = []
    placements = []
    cur = bytearray()
    last = None
    for n, rec in enumerate(records):
        if len(rec) & 3:
            raise ValueError(f"record of {len(rec)} bytes is not 4-byte aligned; "
                             "the next record in the archive would start inside it")
        key = keys[n] if keys is not None else None
        if cur and (len(cur) + len(rec) > cap or key != last):
            archives.append(bytes(cur))
            cur = bytearray()
        last = key
        placements.append((len(archives), len(cur), len(rec)))
        cur += rec
    if cur:
        archives.append(bytes(cur))
    return archives, placements


def verify(archives, placements, frames):
    """/*----------------------
     | verify
     | Description: Decodes every archive back through the reader and checks
     |     that each placement names a record that reproduces the pixels it was
     |     built from. The placements are the only thing that says where a frame
     |     is, and a wrong one shows up on the console as a room that silently
     |     draws no picture, so they are checked here rather than trusted.
     |
     |     The palettes are deliberately not compared: a CLUT entry is RGB555
     |     and the round trip through five bits per channel is lossy in the low
     |     bits by design.
     | Author: suinevere
     | Dependencies: zork_cgl
     | Globals: N/A
     | Params: archives -- the packed bytes; placements -- what pack returned;
     |     frames -- the pixel buffers the records were built from, in the same
     |     order
     | Returns: N/A; raises ValueError naming the first frame that disagrees
     ----------------------*/"""
    decoded = {}
    for a, buf in enumerate(archives):
        for _idx, off, _pal, pix in zork_cgl.records(buf):
            decoded[(a, off)] = pix
    for i, ((a, off, length), want) in enumerate(zip(placements, frames)):
        got = decoded.get((a, off))
        if got is None:
            raise ValueError(f"frame {i}: nothing decodes at offset {off} of "
                             f"archive {a} -- the record boundaries have drifted")
        if got != want:
            raise ValueError(f"frame {i}: the record at offset {off} of archive "
                             f"{a} decodes to different pixels than it was built "
                             "from")
        if off + length > len(archives[a]):
            raise ValueError(f"frame {i}: offset {off} plus length {length} runs "
                             f"past archive {a}, which is {len(archives[a])} bytes")


def build(frames, prefix, cap=CAP, keys=None):
    """/*----------------------
     | build
     | Description: The whole job in one call: encode each (palette, pixels)
     |     frame, pack them into archives, verify every placement decodes, and
     |     hand back what a manifest needs to write down.
     | Author: suinevere
     | Dependencies: hashlib, cgl_encode
     | Globals: CAP
     | Params: frames -- a sequence of (palette, pixels); prefix -- the archive
     |     stem prefix; cap -- the byte ceiling one archive may reach;
     |     keys -- one grouping key per frame, so an archive never spans two
     | Returns: ({stem: bytes}, [{"archive": stem, "offset": int,
     |     "length": int}], {stem: sha256 hex})
     ----------------------*/"""
    records = [cgl_encode.record(pal, pix) for pal, pix in frames]
    archives, placements = pack(records, cap, keys)
    verify(archives, placements, [pix for _pal, pix in frames])
    names = stems(prefix, len(archives))
    blobs = {names[i]: archives[i] for i in range(len(archives))}
    rows = [{"archive": names[a], "offset": off, "length": length}
            for a, off, length in placements]
    sums = {name: hashlib.sha256(blob).hexdigest() for name, blob in blobs.items()}
    return blobs, rows, sums
