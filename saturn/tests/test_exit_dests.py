#!/usr/bin/env python3
"""Host oracle for room_model.c's conditional-exit destination decode.

Reads the shipped v3 story images directly -- not a fixture -- and decodes
their object and property tables independently of room_model.c, so a
regression in the C decode is caught against the same bytes the console
reads rather than against a hand-picked byte array that could quietly drift
from what a real story actually contains.

A v3 direction property's length encodes its kind: 1 byte is a plain exit
(the room), 2 is a refusal message (no room), 3 is a routine decision (no
room recoverable), 4 is a flag-conditional exit and 5 is a door exit --
both of which carry the destination room in byte 0. This is what
room_model.c's decode loop reads; this file checks that byte 0, for every
4- and 5-byte direction property in every shipped story, actually names an
object that is itself a room (one that carries a direction property of its
own) -- the same guard is_room applies in room_model.c.
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
Z3_DIR = ROOT / "saturn" / "cd" / "data" / "Z3"
ENGINE_DIR = ROOT / "saturn" / "src" / "engine"
DUMP_EXITS_SRC = ROOT / "saturn" / "tests" / "dump_exits.c"
ROOM_MODEL_SRC = ENGINE_DIR / "room_model.c"

A0 = "abcdefghijklmnopqrstuvwxyz"
A1 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
A2 = [None, "\n", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
      ".", ",", "!", "?", "_", "#", "'", '"', "/", "\\", "-", ":", "(", ")"]

RM_DIR_WORD = ("north", "east", "west", "south", "ne", "nw", "se", "sw",
               "up", "down", "in", "out")


def rd16(raw, addr):
    return (raw[addr] << 8) | raw[addr + 1]


def dict_layout(raw):
    dictaddr = rd16(raw, 0x08)
    nsep = raw[dictaddr]
    elen = raw[dictaddr + 1 + nsep]
    n = rd16(raw, dictaddr + 2 + nsep)
    first = dictaddr + 1 + nsep + 3
    return elen, n, first


def decode_dict_word(raw, off):
    """A dictionary entry's four text bytes, decoded through the A0
    alphabet only (z-chars 6..31 as lowercase letters, everything else as
    a space) -- room_model.c's decode_word decodes no more than this
    either, since every direction word is plain lowercase."""
    a0 = "      abcdefghijklmnopqrstuvwxyz"
    out = []
    for k in (0, 2):
        x = rd16(raw, off + k)
        out.append(a0[(x >> 10) & 31])
        out.append(a0[(x >> 5) & 31])
        out.append(a0[x & 31])
    text = "".join(out)
    while text.endswith(" "):
        text = text[:-1]
    return text


def _matches_canon(word, canon):
    j = 0
    while j < len(canon) and j < 6 and j < len(word) and canon[j] == word[j]:
        j += 1
    return (j == len(canon) or j == 6) and j == len(word)


def direction_property_index(raw):
    """{property number: RM_* index (0..11, RM_DIR_WORD's own order)}.

    Mirrors room_model.c's bind: a dictionary entry flagged FL_DIR (bit
    0x10 of its flag byte) names a direction property only when exactly
    one of its data bytes falls in 1..31 *and* its own decoded text
    matches one of the twelve canonical direction words -- the FL_DIR flag
    alone is not enough, since a story can flag other words (a verb like
    "land") the same way without them ever being compared against
    RM_DIR_WORD in the real bind, so they never end up mapped to a
    direction there either. The index is what dump_exits.c prints as
    `dir`, taken straight from the RM_N..RM_OUT enum room_model.h defines
    in this same order.
    """
    elen, n, first = dict_layout(raw)
    out = {}
    for k in range(n):
        off = first + k * elen
        flags = raw[off + 4]
        if not (flags & 0x10):
            continue
        found = [raw[off + i] for i in range(5, elen) if 1 <= raw[off + i] <= 31]
        if len(found) != 1:
            continue
        word = decode_dict_word(raw, off)
        for idx, canon in enumerate(RM_DIR_WORD):
            if _matches_canon(word, canon):
                out[found[0]] = idx
                break
    return out


def direction_properties(raw):
    """The set of property numbers direction_property_index resolves."""
    return set(direction_property_index(raw).keys())


def object_property_tables(raw):
    """Every object's property-list address, keyed by 1-based object number.

    Object entries run from just past the 62-byte default-property table,
    9 bytes apiece, for as many objects as fit before the property tables
    themselves begin. That boundary is not recorded anywhere, so it is
    inferred: the lowest property-table address seen so far bounds how many
    more entries can exist, tracked as a running minimum rather than taken
    from the first object alone, since nothing guarantees object 1 holds
    the lowest address.
    """
    objtab = rd16(raw, 0x0A)
    entries = objtab + 62
    out = {}
    min_prop = None
    n = 1
    while True:
        e = entries + (n - 1) * 9
        if e + 9 > len(raw):
            break
        if min_prop is not None and e >= min_prop:
            break
        paddr = rd16(raw, e + 7)
        if paddr == 0 or paddr >= len(raw):
            break
        if min_prop is None or paddr < min_prop:
            min_prop = paddr
        out[n] = paddr
        n += 1
    return out


def object_properties(raw, paddr):
    """An object's own property list: {number: data bytes}, from its table."""
    a = paddr + 1 + 2 * raw[paddr]
    props = {}
    while a < len(raw) and raw[a] != 0:
        size = raw[a]
        plen = (size >> 5) + 1
        if a + 1 + plen > len(raw):
            break
        props[size & 31] = raw[a + 1:a + 1 + plen]
        a += 1 + plen
    return props


def decode_zstring(raw, addr, abbr_addr, allow_abbr=True):
    zc = []
    a = addr
    while a + 1 < len(raw):
        w = rd16(raw, a)
        a += 2
        zc += [(w >> 10) & 0x1F, (w >> 5) & 0x1F, w & 0x1F]
        if w & 0x8000:
            break
    return emit_zchars(raw, zc, abbr_addr, allow_abbr)


def emit_zchars(raw, zc, abbr_addr, allow_abbr):
    out, alpha, i, n = [], 0, 0, len(zc)
    while i < n:
        c = zc[i]
        if c == 0:
            out.append(" "); alpha = 0; i += 1; continue
        if c <= 3:
            if allow_abbr and i + 1 < n and abbr_addr:
                idx = 32 * (c - 1) + zc[i + 1]
                entry = abbr_addr + 2 * idx
                if entry + 1 < len(raw):
                    aa = rd16(raw, entry) * 2
                    if aa + 1 < len(raw):
                        out.append(decode_zstring(raw, aa, abbr_addr, False))
            i += 2 if i + 1 < n else 1
            alpha = 0
            continue
        if c == 4:
            alpha = 1; i += 1; continue
        if c == 5:
            alpha = 2; i += 1; continue
        if alpha == 2 and c == 6:
            if i + 2 < n:
                zs = (zc[i + 1] << 5) | zc[i + 2]
                if 32 <= zs < 127:
                    out.append(chr(zs))
                i += 3
            else:
                i += 1
            alpha = 0
            continue
        ch = A0[c - 6] if alpha == 0 else A1[c - 6] if alpha == 1 else A2[c - 6]
        if ch:
            out.append(ch)
        alpha = 0
        i += 1
    return "".join(out)


def object_name(raw, paddr, abbr_addr):
    if raw[paddr] == 0:
        return ""
    return decode_zstring(raw, paddr + 1, abbr_addr)


class Story:
    """One decoded story: its rooms and, for each, its direction properties."""

    def __init__(self, path):
        self.path = path
        self.raw = path.read_bytes()
        self.abbr_addr = rd16(self.raw, 0x18)
        self.tables = object_property_tables(self.raw)
        self.dir_index = direction_property_index(self.raw)
        self.dir_props = set(self.dir_index.keys())
        self.rooms = {
            n for n, paddr in self.tables.items()
            if self.dir_props & set(object_properties(self.raw, paddr))
        }

    def props(self, obj):
        return object_properties(self.raw, self.tables[obj])

    def name(self, obj):
        return object_name(self.raw, self.tables[obj], self.abbr_addr)

    def conditional_exits(self):
        """Every (object, prop, data) triple whose length is 4 or 5, for
        rooms only -- the population room_model.c's guard applies to."""
        out = []
        for obj in self.rooms:
            for prop, data in self.props(obj).items():
                if prop in self.dir_props and len(data) in (4, 5):
                    out.append((obj, prop, data))
        return out


_DUMP_EXITS_BUILT = False
_DUMP_EXITS_BIN = None


def dump_exits_binary():
    """Path to a built dump_exits, or None when it cannot be built.

    Built once and cached for the whole session -- test_all_shipped_stories
    calls this once per story, and a full rebuild each time would dwarf the
    cost of everything else this file does. Returns None rather than raising
    when no C compiler is on PATH, so callers can turn that into a skip.
    """
    global _DUMP_EXITS_BUILT, _DUMP_EXITS_BIN
    if _DUMP_EXITS_BUILT:
        return _DUMP_EXITS_BIN
    _DUMP_EXITS_BUILT = True

    cc = shutil.which("gcc") or shutil.which("cc")
    if cc is None:
        return None

    out_dir = pathlib.Path(tempfile.gettempdir()) / "zaturn_test_exit_dests"
    out_dir.mkdir(exist_ok=True)
    exe = out_dir / ("dump_exits.exe" if os.name == "nt" else "dump_exits")
    cmd = [cc, "-O2", "-Wall", "-Wextra", "-I", str(ENGINE_DIR),
           "-o", str(exe), str(DUMP_EXITS_SRC), str(ROOM_MODEL_SRC)]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if result.returncode != 0 or not exe.exists():
        return None

    _DUMP_EXITS_BIN = exe
    return exe


def c_exits(exe, story_path):
    """The C decoder's own exits for one story: {(room, dir): (dest, state)}.

    None when room_model_bind itself refuses the story -- the C decoder is
    entitled to refuse a story the Python decode above parses more loosely,
    and that is not a disagreement to cross-check.
    """
    result = subprocess.run([str(exe), str(story_path)], capture_output=True,
                             text=True, timeout=30)
    if result.returncode != 0:
        return None
    out = {}
    for line in result.stdout.splitlines():
        room, direction, dest, state = (int(x) for x in line.split())
        out[(room, direction)] = (dest, state)
    return out


def all_stories():
    return sorted(Z3_DIR.glob("*.Z3"))


def load_stories():
    """Every shipped story, decoded, plus the name and reason for any that
    were not -- a story this decoder cannot parse is worth knowing by name,
    not silently absorbed into a lower floor."""
    stories = {}
    failures = {}
    for path in all_stories():
        try:
            stories[path.name] = Story(path)
        except (IndexError, ValueError) as exc:
            failures[path.name] = str(exc)
    return stories, failures


STORIES, LOAD_FAILURES = load_stories()


def test_shipped_stories_were_loaded():
    assert not LOAD_FAILURES, f"stories failed to decode: {LOAD_FAILURES}"
    all_names = {p.name for p in all_stories()}
    assert STORIES.keys() == all_names, (
        f"decoded {sorted(STORIES.keys())}, expected {sorted(all_names)}")


def test_zork1_every_conditional_exit_names_a_room():
    zork1 = STORIES["ZORK1.Z3"]
    exits = zork1.conditional_exits()
    assert len(exits) == 31, f"expected 31 length-4/5 direction properties in ZORK1.Z3, found {len(exits)}"
    misses = [(obj, prop, data.hex()) for obj, prop, data in exits if data[0] not in zork1.rooms]
    assert not misses, f"byte 0 does not name a room for: {misses}"


def _dest(zork1, room_id, prop):
    props = zork1.props(room_id)
    assert prop in props, f"object {room_id} has no property {prop}"
    data = props[prop]
    assert len(data) in (4, 5), f"object {room_id} property {prop} is {len(data)} bytes, not 4 or 5"
    return data


def test_zork1_five_measured_passages():
    zork1 = STORIES["ZORK1.Z3"]

    BEHIND_HOUSE, KITCHEN, CELLAR = 79, 203, 72
    LIVING_ROOM, STRANGE_PASSAGE, STUDIO = 193, 51, 94
    KITCHEN_WINDOW, TRAP_DOOR = 235, 183
    WEST, EAST, UP, DOWN = 29, 30, 23, 22

    assert zork1.name(BEHIND_HOUSE) == "Behind House"
    data = _dest(zork1, BEHIND_HOUSE, WEST)
    assert len(data) == 5 and data[0] == KITCHEN and data[1] == KITCHEN_WINDOW

    assert zork1.name(KITCHEN) == "Kitchen"
    data = _dest(zork1, KITCHEN, EAST)
    assert len(data) == 5 and data[0] == BEHIND_HOUSE and data[1] == KITCHEN_WINDOW

    assert zork1.name(CELLAR) == "Cellar"
    data = _dest(zork1, CELLAR, UP)
    assert len(data) == 5 and data[0] == LIVING_ROOM and data[1] == TRAP_DOOR

    assert zork1.name(LIVING_ROOM) == "Living Room"
    data = _dest(zork1, LIVING_ROOM, WEST)
    assert len(data) == 4 and data[0] == STRANGE_PASSAGE

    data = _dest(zork1, KITCHEN, DOWN)
    assert len(data) == 4 and data[0] == STUDIO


def test_all_shipped_stories_floor():
    """A floor, not an equality, so adding a story to the disc does not break
    this suite. 843/853 is what this decoder measures today; the design
    doc's 1217/1396 came from .superpowers/sdd/2026-09-01-map-passage-marks/
    validate_dest_reference.py's consensus heuristic, which never matches a
    dictionary entry's decoded word against the twelve canonical direction
    words -- so it both misses "in"/"out" as real directions and folds in
    unrelated properties (an object-strength or a container-capacity byte
    that happens to often point at another object carrying the same
    property) as if they were direction exits, inflating both the room set
    and the total. This decoder matches room_model.c's own bind exactly."""
    hit = miss = 0
    for story in STORIES.values():
        for obj, prop, data in story.conditional_exits():
            if data[0] in story.rooms:
                hit += 1
            else:
                miss += 1
    total = hit + miss
    assert total >= 800, f"only {total} length-4/5 direction properties found across shipped stories"
    assert hit >= 800, f"only {hit}/{total} length-4/5 direction properties name a room"


def test_c_decoder_matches_python_for_every_shipped_story():
    """The actual regression bridge: for every length-4/5 direction property
    in every shipped story, the C decode room_model.c produces (via
    dump_exits.c, using only the public room_model_bind /
    room_model_refresh_room / room_model_get API) must equal what this
    file's own independent Python decode derives from the same bytes.

    This is the check that fails if the fix in room_model_refresh_room is
    reverted -- every other assertion in this file is a property of the
    story bytes alone and would stay green either way."""
    exe = dump_exits_binary()
    if exe is None:
        pytest.skip("no C compiler on PATH -- cannot build dump_exits, "
                     "C/Python cross-check skipped")

    compared = 0
    for name, story in STORIES.items():
        c = c_exits(exe, story.path)
        if c is None:
            continue
        for obj, prop, data in story.conditional_exits():
            idx = story.dir_index[prop]
            want_dest = data[0] if data[0] in story.rooms else 0
            got = c.get((obj, idx))
            assert got is not None, (
                f"{name}: dump_exits recorded no exit for room {obj} dir {idx}")
            got_dest, got_state = got
            assert got_state == 3, (
                f"{name}: room {obj} dir {idx} state {got_state}, expected "
                f"RM_EXIT_MAYBE (3)")
            assert got_dest == want_dest, (
                f"{name}: room {obj} dir {idx} dest {got_dest}, Python "
                f"decode says byte 0 is {data[0]!r} and expects dest "
                f"{want_dest}")
            compared += 1
    assert compared > 0, "cross-check compared nothing -- dump_exits or the story parsing is broken"


def map_marks_story_graph():
    """tools/gen_map_marks.story_graph, or None when its imports are missing.

    The generator pulls in pymupdf, opencv and an OCR runtime through mapscan,
    none of which this suite otherwise needs, so a checkout without them skips
    this comparison the way a checkout without a C compiler skips the one
    above rather than failing for a reason that has nothing to do with the
    decode.
    """
    tools = str(ROOT / "tools")
    if tools not in sys.path:
        sys.path.insert(0, tools)
    try:
        import gen_map_marks
    except ImportError:
        return None
    return gen_map_marks.story_graph


def test_gen_map_marks_decodes_the_same_destinations_for_every_shipped_story():
    """The third copy of the plen-4/5 rule, held to the same bytes as the
    other two.

    tools/gen_map_marks.story_graph repeats room_model_refresh_room's
    conditional-destination decode instead of putting it in
    mapscan.room_graph, so that gen_map_atlas keeps emitting a byte-identical
    table. A rule written down three times drifts unless something compares
    the copies, and this file is already where two of them are compared, over
    every story on the disc rather than over a fixture -- so the generator's
    copy is checked here rather than in a test of its own that could go green
    against a decode this file had already found wrong.
    """
    story_graph = map_marks_story_graph()
    if story_graph is None:
        pytest.skip("tools/gen_map_marks.py imports are unavailable -- "
                    "generator/Python cross-check skipped")

    compared = 0
    for name, story in STORIES.items():
        graph, _release, _serial = story_graph(str(story.path))
        for obj, prop, data in story.conditional_exits():
            idx = story.dir_index[prop]
            want_dest = data[0] if data[0] in story.rooms else 0
            assert obj in graph, f"{name}: gen_map_marks has no room {obj}"
            got = graph[obj]["exits"].get(idx)
            assert got is not None, (
                f"{name}: gen_map_marks recorded no exit for room {obj} "
                f"dir {idx}")
            got_kind, got_dest = got
            assert got_kind == "MAYBE", (
                f"{name}: room {obj} dir {idx} kind {got_kind!r}, expected "
                f"MAYBE for a {len(data)}-byte direction property")
            assert got_dest == want_dest, (
                f"{name}: room {obj} dir {idx} dest {got_dest}, this file's "
                f"decode says byte 0 is {data[0]!r} and expects {want_dest}")
            compared += 1
    assert compared > 0, (
        "generator cross-check compared nothing -- story_graph or the story "
        "parsing is broken")


if __name__ == "__main__":
    import sys
    failures = 0
    for fn_name, fn in sorted(globals().items()):
        if fn_name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"ok   {fn_name}")
            except pytest.skip.Exception as e:
                print(f"SKIP {fn_name}\n  {e}")
            except AssertionError as e:
                print(f"FAIL {fn_name}\n  {e}")
                failures += 1
    print()
    if failures:
        print(f"{failures} failure(s)")
        sys.exit(1)
    print("test_exit_dests: OK")
