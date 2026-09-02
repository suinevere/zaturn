#!/usr/bin/env python3
"""/*----------------------
 | gen_cues.py
 | Description: GENERATES saturn/src/scene/game_cues.inc -- the situational
 |     CD-DA cues the original Sega Saturn Zork I played over a room's own
 |     theme, expressed against the Z-machine story the port actually runs.
 |
 |     The Saturn release drove these off its own object table; those numbers
 |     mean nothing here, so every object is resolved by short name out of the
 |     story file itself and every track comes from the measured table in
 |     docs/ZORK1_AUDIO_MAP.md. The one thing neither source states outright is
 |     which attribute bit is INVISIBLE -- the thief is in a room long before
 |     he is in it visibly, and a cue that ignored that would fire at random
 |     while he wanders. It is solved rather than assumed: every ZIL object
 |     whose DESC matches exactly one story object contributes its FLAGS list,
 |     and the bit set on every INVISIBLE object and on no other object is the
 |     answer. 116 objects agree on one bit; anything less than exactly one
 |     raises.
 |
 |     Refuses rather than guessing, the way gen_presentation.py does: a name
 |     that does not resolve to exactly one story object, a story whose release
 |     and serial are not 88 / 840726, or an ambiguous INVISIBLE bit all raise
 |     instead of writing a zero, because a zero here is a cue that silently
 |     never fires.
 | Author: suinevere
 | Dependencies: pathlib, re, sys, collections, zstory
 | Globals: ROOT, STORY, ZIL, OUT, RELEASE, SERIAL, RULES, DANGER, TAKE,
 |     DEATH, WIN, SWORD
 ----------------------*/"""
import collections
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import zstory

ROOT = pathlib.Path(__file__).resolve().parent.parent
STORY = ROOT / "saturn" / "cd" / "data" / "Z3" / "ZORK1.Z3"
ZIL = (ROOT / "cd" / "Zork I - The Great Underground Empire (Japan)"
            / "zork1" / "1dungeon.zil")
OUT = ROOT / "saturn" / "src" / "scene" / "game_cues.inc"

RELEASE = 88
SERIAL = "840726"

# (villain short name, room short name or None for anywhere, track, gated on
# visibility). Ordered: the first rule that matches wins, which is how the
# original's "troll present suppresses the thief cue" falls out -- in the Troll
# Room the troll's own rule is reached first.
RULES = [
    ("troll",   "The Troll Room", 14, False),
    ("cyclops", "Cyclops Room",  17, False),
    ("thief",   "Treasure Room", 16, True),
    ("thief",   None,            15, True),
]
SWORD = "sword"   # carrying it is what arms the danger cue
DANGER = 13       # a villain one room away
TAKE = 25         # one-shot on a successful take
DEATH = 19
WIN = 30


def load_story():
    """/*----------------------
     | load_story
     | Description: Opens the story and refuses anything but the release the
     |   measured table was taken from.
     | Author: suinevere
     | Dependencies: zstory
     | Globals: STORY, RELEASE, SERIAL
     | Params: N/A
     | Returns: the Story
     ----------------------*/"""
    s = zstory.Story(STORY)
    if s.release != RELEASE or s.serial != SERIAL:
        raise SystemExit(f"{STORY.name} is release {s.release} serial "
                         f"{s.serial}, expected {RELEASE} / {SERIAL}")
    return s


def attr_bits(story, num):
    """/*----------------------
     | attr_bits
     | Description: The attribute numbers set on one object, read off the first
     |   four bytes of its object-table entry.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: story -- the Story; num -- 1-based object number
     | Returns: a frozenset of attribute numbers
     ----------------------*/"""
    raw = story.raw
    entries = ((raw[0x0A] << 8) | raw[0x0B]) + 62
    e = entries + (num - 1) * 9
    v = int.from_bytes(raw[e:e + 4], "big")
    return frozenset(i for i in range(32) if v & (1 << (31 - i)))


def zil_flags():
    """/*----------------------
     | zil_flags
     | Description: Each ZIL object's DESC mapped to its FLAGS set. Objects are
     |   split on a `<` in column 1 rather than matched as balanced forms: a ZIL
     |   object closes with `>` at the end of its last property line, not on one
     |   of its own.
     | Author: suinevere
     | Dependencies: re, pathlib
     | Globals: ZIL
     | Params: N/A
     | Returns: dict of desc -> list of frozensets
     ----------------------*/"""
    text = ZIL.read_text(encoding="utf-8", errors="replace")
    out = {}
    for chunk in re.split(r"(?m)^<", text):
        if not chunk.startswith("OBJECT "):
            continue
        desc = re.search(r'\(DESC\s+"([^"]*)"\)', chunk)
        flags = re.search(r"\(FLAGS([^)]*)\)", chunk)
        if not desc:
            continue
        out.setdefault(desc.group(1), []).append(
            frozenset(flags.group(1).split()) if flags else frozenset())
    return out


def solve_flag(story, flag):
    """/*----------------------
     | solve_flag
     | Description: The attribute number a named ZIL flag compiles to, by
     |   intersecting the attribute sets of every object that carries the flag
     |   and subtracting every set from an object that does not. Only objects
     |   whose short name resolves to exactly one story object and exactly one
     |   ZIL declaration take part, so a duplicated name cannot pollute either
     |   side.
     | Author: suinevere
     | Dependencies: attr_bits, zil_flags, collections
     | Globals: N/A
     | Params: story -- the Story; flag -- the ZIL flag name
     | Returns: the attribute number
     ----------------------*/"""
    by_desc = zil_flags()
    seen = collections.Counter(o.name for o in story.objects)
    have, lack = [], []
    for o in story.objects:
        if seen[o.name] != 1:
            continue
        decl = by_desc.get(o.name)
        if not decl or len(set(decl)) != 1:
            continue
        bucket = have if flag in decl[0] else lack
        bucket.append(attr_bits(story, o.num))
    if not have:
        raise SystemExit(f"no object carries {flag}; cannot solve it")
    cand = set.intersection(*[set(h) for h in have])
    for l in lack:
        cand -= set(l)
    if len(cand) != 1:
        raise SystemExit(f"{flag} solves to {sorted(cand)}, not one bit "
                         f"({len(have)} carry it, {len(lack)} do not)")
    return cand.pop()


def obj_by_name(story, name):
    """/*----------------------
     | obj_by_name
     | Description: The one story object with a given short name. Raises unless
     |   exactly one matches -- a cue aimed at the wrong object would only show
     |   up as music that never plays.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: story -- the Story; name -- the short name to match
     | Returns: the object number
     ----------------------*/"""
    hits = [o.num for o in story.objects if o.name == name]
    if len(hits) != 1:
        raise SystemExit(f"{name!r} matches {len(hits)} objects, expected 1")
    return hits[0]


def build():
    """/*----------------------
     | build
     | Description: Resolves every name against the story and returns the rows
     |   the .inc needs.
     | Author: suinevere
     | Dependencies: load_story, obj_by_name, solve_flag
     | Globals: RULES, SWORD, DANGER, TAKE, DEATH, WIN
     | Params: N/A
     | Returns: (rules, fields) where rules is a list of
     |   (villain, room, track, unseen) and fields is a dict
     ----------------------*/"""
    story = load_story()
    invisible = solve_flag(story, "INVISIBLE")
    rules = [(obj_by_name(story, villain),
              obj_by_name(story, room) if room else 0,
              track, 1 if unseen else 0)
             for villain, room, track, unseen in RULES]
    fields = {
        "invisible": invisible,
        "sword": obj_by_name(story, SWORD),
        "danger": DANGER, "take": TAKE, "death": DEATH, "win": WIN,
    }
    return rules, fields


def emit(rules, fields):
    """/*----------------------
     | emit
     | Description: Writes game_cues.inc.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: OUT, RELEASE, SERIAL, RULES
     | Params: rules, fields -- build()'s output
     | Returns: the text written
     ----------------------*/"""
    lines = [
        "/*----------------------",
        " | game_cues.inc",
        " | Description: GENERATED FILE -- do not edit by hand; produced by",
        " |   tools/gen_cues.py. The situational CD-DA cues the original",
        " |   Saturn release played over a room's own theme, resolved to the",
        " |   Z-machine object numbers this port actually walks. Rules are",
        " |   ordered and the first match wins. track 0 anywhere means the",
        " |   cue is not offered for that game.",
        " | Author: suinevere",
        " ----------------------*/",
        "typedef struct {",
        "    unsigned short villain;",
        "    unsigned short room;",
        "    unsigned char  track;",
        "    unsigned char  unseen;",
        "} CueRule;",
        "typedef struct {",
        "    unsigned short release;",
        "    const char *serial;",
        "    const CueRule *rules;",
        "    unsigned char  nrules;",
        "    unsigned char  invisible;",
        "    unsigned short sword;",
        "    unsigned char  danger;",
        "    unsigned char  take;",
        "    unsigned char  death;",
        "    unsigned char  win;",
        "} GameCueMap;",
        "#define CUE_GAME_N 1",
        "",
        f"static const CueRule ZORK1_CUE_RULES[{len(rules)}] = {{",
    ]
    for (villain, room, track, unseen), src in zip(rules, RULES):
        where = f"in {src[1]}" if src[1] else "anywhere"
        lines.append(f"    {{ {villain:3d}, {room:3d}, {track:2d}, {unseen} }},"
                     f"   /* {src[0]} {where} */")
    lines += [
        "};",
        "",
        "static const GameCueMap GAME_CUE_MAP[CUE_GAME_N] = {",
        f'    {{ {RELEASE}, "{SERIAL}", ZORK1_CUE_RULES, {len(rules)}, '
        f'{fields["invisible"]}, {fields["sword"]},',
        f'      {fields["danger"]}, {fields["take"]}, {fields["death"]}, '
        f'{fields["win"]} }},',
        "};",
        "",
    ]
    text = "\n".join(lines)
    OUT.write_text(text, encoding="utf-8")
    return text


def main():
    """/*----------------------
     | main
     | Description: Generates the table and reports what it resolved.
     | Author: suinevere
     | Dependencies: build, emit
     | Globals: OUT
     | Params: N/A
     | Returns: N/A
     ----------------------*/"""
    rules, fields = build()
    emit(rules, fields)
    print(f"INVISIBLE = attribute {fields['invisible']}, sword = object "
          f"{fields['sword']}")
    for (villain, room, track, unseen), src in zip(rules, RULES):
        print(f"  track {track:2d}  object {villain:3d} ({src[0]}) in "
              f"{src[1] or 'anywhere'}" + ("  [visible only]" if unseen else ""))
    print(f"-> {OUT}")


if __name__ == "__main__":
    main()
