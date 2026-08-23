#!/usr/bin/env python3
"""Capture real room text from every story in tools/assets/Z3 into a C fixture.

The classifier's job is judgement, and the only way to know a change improved
it is to run it over the prose the games actually print. Static extraction --
decoding each story's object table directly (tools/zstory.py) -- is the
primary source: it recovers a room's stored description string without ever
running the game, so it reaches rooms neither a walkthrough nor a wander pass
would ever visit. It cannot reach everything, though: a room whose text is
computed by a routine rather than stored as a string decodes to garbage no
matter how it's read, and running the game live is the only way to see what
that routine actually prints. The runtime pass (driving host mojozork through
a walkthrough where one exists, and always through a fixed wander script)
exists to catch exactly that remainder; the two are unioned per story, deduped
by title, with the static text winning on a collision.

It never decides what the RIGHT category is. That belongs to blessed.inc, which
room_class_test writes; this file only records what the games say.

Determinism is a hard requirement. Static extraction reads bytes and does no
I/O beyond that, so it is deterministic by construction. The runtime pass's
wander script is fixed and checked in, its junk filter is a phrase list rather
than a heuristic, and the host interpreter's RNG is pinned to a fixed clock at
link time (see _TIME_SHIM) instead of seeding from wall-clock time -- so a
regeneration with an unchanged game library produces a byte-identical
rooms.inc. If that ever stops holding, the snapshot suite is broken even while
it is passing.

Run: python tools/gen_room_corpus.py
"""
import pathlib
import re
import subprocess
import sys
import tempfile

import zstory

ROOT = pathlib.Path(__file__).resolve().parent.parent
Z3 = ROOT / "tools" / "assets" / "Z3"
SOLUTIONS = ROOT / "tools" / "typeahead" / "solutions"
WANDER = ROOT / "tools" / "wander.txt"
OUT = ROOT / "test" / "corpus" / "rooms.inc"

TITLE_MAX = 64          # a room title longer than this is corpus noise, not a real name
MIN_BODY = 20           # a room prints prose, not a two-word acknowledgement

# Games whose interactive menu never leads anywhere: Infocom's two promotional
# sampler discs ask "press a letter to pick an excerpt," and the wander script
# only ever sends direction words, so neither pass ever reaches an excerpt.
# These are the only stories allowed to end up with zero rooms; the check in
# main() enforces that everything else is a real extraction bug to fix, not a
# fact to record.
NEVER_NAVIGATED = ("INFOSAM5", "INFOSAM7")


# ---------------------------------------------------------------------------
# Static extraction: decode room titles and descriptions straight out of the
# compiled story, without running it. See tools/zstory.py for the Z-string /
# object-table decoding this builds on.
# ---------------------------------------------------------------------------

def looks_like_prose(s):
    """Would a human reading this call it a sentence of room description?

    Static decoding cannot tell a real packed string address from a routine
    address or an unrelated numeric property value by construction alone --
    both are just a 2-byte word. Reading either as a string address either
    raises (out of range) or produces *something*, and for a routine address
    that something often lands inside real story text by coincidence (Zork
    I's action-routine property, misread as a string, reliably produces
    fragments containing real words like "cyclops" because the decoder walks
    through the abbreviation table). A route/garbage decode is usually short,
    starts mid-word with a stray leading space, and is heavy on short
    fragments; real prose starts with a capital letter (or a quote opening
    dialogue) and is mostly recognizable words. This is deliberately strict --
    admitting garbage silently corrupts the corpus, so false negatives (a
    real but odd-shaped description that gets rejected) are the safe failure
    mode, not false positives.
    """
    if not s or len(s) < 15:
        return False
    if s[0] not in "ABCDEFGHIJKLMNOPQRSTUVWXYZ\"'":
        return False
    if s.count("?") > 0:
        return False
    printable = sum(1 for c in s if c.isprintable())
    if printable / len(s) < 0.98:
        return False
    words = [w for w in s.split(" ") if w]
    if len(words) < 3:
        return False

    def good_word(w):
        w = w.strip("\"'.,!?;:()")
        if not w:
            return False
        alpha = sum(1 for c in w if c.isalpha())
        return alpha >= max(1, len(w) - 1)

    good = sum(1 for w in words if good_word(w))
    return good / len(words) >= 0.8


def decode_description(story, obj, prop):
    """The decoded text of obj's `prop` property, or None if it isn't a
    genuine word-valued string (wrong length, out-of-range address, or a
    decode that fails the prose check -- most often because `prop` on this
    particular object holds a routine address instead of a string one)."""
    if prop not in obj.properties:
        return None
    addr, length = obj.properties[prop]
    if length != 2:
        return None
    try:
        s = story.decode_word_property(addr)
    except Exception:
        return None
    return s if looks_like_prose(s) else None


def _property_scores(story, objects):
    """{property number: count of objects in `objects` whose value at that
    property decodes to real prose}, for every property 1-31."""
    scores = {}
    for prop in range(1, 32):
        scores[prop] = sum(1 for obj in objects if decode_description(story, obj, prop) is not None)
    return scores


def detect_description_property(story, hub_children):
    """Which property number holds room prose in this story.

    It isn't a constant across games (Zork I 11, Starcross 13, Seastalker 18
    are all attested), so this scores every property number 1-31 twice: once
    restricted to the rooms hub's children (find_rooms_hub), once across
    every object in the story. Restricting to the hub alone would seem like
    the obvious choice -- it's the actual room population -- but for a story
    where most rooms compute their description at runtime rather than
    storing it (Seastalker: only 1 of its 30 hub children has any usable
    static text at all), that sample is too thin to trust and the highest
    hub-scoped scorer is noise -- a property that happens to hold a full
    sentence on one or two objects for unrelated reasons (Seastalker's real
    description property 18 scores 1 there; a property holding "You have to
    go north..."-style movement hints scores 3 and would win outright). The
    fix used here: only properties that occur at least once among the hub's
    children are eligible at all (so a property that's simply never used by
    a room, like Zork I's action-routine property colliding with real text
    elsewhere in the file, can never win no matter how it scores elsewhere),
    and among those eligible, the one with the highest score story-wide wins,
    since the real description property is used far more consistently across
    the whole game than any coincidental one-off. Validated against all three
    property numbers the brief hands over as ground truth (Zork I 11,
    Starcross 13, Seastalker 18) and produces sensible, in-range answers for
    every other story checked by hand.

    Returns (property_number, story_wide_score); a very low score is itself
    informative (the caller prints it) -- it means this story's rooms mostly
    compute their description at runtime rather than storing it, which the
    static pass simply cannot reach for those rooms.
    """
    hub_scores = _property_scores(story, hub_children)
    global_scores = _property_scores(story, story.objects)
    eligible = [p for p in range(1, 32) if hub_scores[p] >= 1]
    pool = eligible if eligible else range(1, 32)
    best = max(pool, key=lambda p: (global_scores[p], -p))
    return best, global_scores[best]


def find_rooms_hub(story, min_children=5, high_prop=20):
    """The object that (almost) every room in this story is a child of.

    Z-machine v3 doesn't mark an object as "a room" anywhere explicit; a room
    is just an object other objects (and the player) end up parented to. What
    picks rooms out from every *other* kind of "things sit inside this"
    grouping a compiled story has (an abstract-topics bucket, a scenery/props
    bucket, item containers) is that rooms specifically carry direction/exit
    data, and that data is compiled to a cluster of high-numbered properties
    -- empirically property >= 20 across every story checked by hand (Zork I,
    Starcross, Sorcerer, Seastalker, Suspended, Witness, Moonmist all cleanly
    separate this way: the true rooms parent averages several such properties
    per child, while a topics/scenery parent of comparable size averages at
    or near zero). So: group objects by parent, and among parents with at
    least min_children of them, pick the one whose children carry the most
    high-numbered properties per child, on average. Returns (parent_object,
    [children]), or (None, []) if no parent has enough children to be a
    plausible hub at all (the two Infocom samplers, which barely have an
    object tree).
    """
    children_of = {}
    for obj in story.objects:
        if obj.parent:
            children_of.setdefault(obj.parent, []).append(obj)

    def score(children):
        hits = sum(1 for o in children for p in o.properties if p >= high_prop)
        return hits / len(children)

    candidates = [p for p, kids in children_of.items() if len(kids) >= min_children]
    if not candidates:
        return None, []
    best_parent = max(candidates, key=lambda p: (score(children_of[p]), len(children_of[p])))
    return story.by_num[best_parent], children_of[best_parent]


def derive_direction_props(hub_children, desc_prop, high_prop=20, min_count=2):
    """Property numbers that look like per-direction exit data among the
    rooms hub's children: high-numbered (see find_rooms_hub) and carried by
    more than one child, so a single stray property on one odd object can't
    qualify alone."""
    freq = {}
    for obj in hub_children:
        for p in obj.properties:
            if p == desc_prop or p < high_prop:
                continue
            freq[p] = freq.get(p, 0) + 1
    return {p for p, n in freq.items() if n >= min_count}


def static_rooms(story):
    """This story's rooms as decoded directly from its object table.

    Returns (rows, desc_prop, desc_score, skipped) where rows is a list of
    (title, body), desc_prop/desc_score are what detect_description_property
    found (printed by the caller so a bad detection is visible), and skipped
    counts hub children that structurally look like a room (carry a direction
    property) but whose description property didn't decode to prose -- the
    routine-described case Step 4 asks to make visible rather than silently
    dropping.
    """
    hub, children = find_rooms_hub(story)
    if hub is None:
        return [], 0, 0, 0
    desc_prop, desc_score = detect_description_property(story, children)

    direction_props = derive_direction_props(children, desc_prop)
    rows = []
    skipped = 0
    for obj in children:
        if not obj.properties.keys() & direction_props:
            continue  # not room-shaped: a non-room sharing this parent
        title = obj.name.strip()
        if not title or len(title) > TITLE_MAX:
            continue
        body = decode_description(story, obj, desc_prop)
        if body is None:
            skipped += 1
            continue
        if len(body) < MIN_BODY:
            continue
        rows.append((title, body))
    return rows, desc_prop, desc_score, skipped


# ---------------------------------------------------------------------------
# Runtime capture: drive host mojozork through a walkthrough and a fixed
# wander script, for rooms whose description is computed by a routine rather
# than stored as a string -- the one class static extraction cannot reach.
# ---------------------------------------------------------------------------

# Turn output whose first line matches one of these is not a room. The wander
# pass walks into walls constantly, so this is load-bearing rather than tidy.
JUNK = (
    "you can't go that way", "it is pitch black", "i don't understand",
    "that's not a verb", "you have died", "you can't see", "i beg your pardon",
    "there is a wall", "that sentence isn't one i recognize",
)

# SUSPENDED's turns pass the title/body shape (two-plus lines, no trailing
# punctuation) without being a room: its blind robots report an "Internal map
# reference" instead of a description -- the whole premise of the game is that
# they can't see the room -- and what follows is dialogue, not prose. Checked
# as a title prefix, same spirit as JUNK against the body.
TITLE_JUNK = (
    "internal map reference",
)

# A death mid-walkthrough restarts the story without ever returning to the
# ">" prompt in between, so the death narration, the game's copyright banner,
# and the fresh opening room all land in one chunk together with a real,
# clean title (seen for real on SORCERER: "Twisted Forest" followed by a
# hellhound mauling, Infocom's boilerplate, and only then the description).
# Anywhere this banner shows up mid-body, the body is not trustworthy room
# text and the whole entry is dropped rather than kept half-right.
BODY_TAINT = (
    "copyright (c)", "all rights reserved",
)

# Most games prompt with a bare "> ". A few dress it up -- SUSPENDED links each
# turn to whichever robot is in control ("(FC linked to Iris)>"), and the
# Infocom sampler discs ask "(Your choice:) >". Splitting on a literal "\n>"
# misses those entirely (the ">" is never preceded by "\n"), which silently
# drops every turn of those stories. Matching an optional "(...)" prefix
# between the newline and the ">" catches both without touching the ordinary
# case, where the group just matches zero characters.
PROMPT = re.compile(r"\n(?:\([^\n]*?\)\s*)?>")


# mojozork.c seeds its Z-machine RNG from wall-clock time on host builds
# (saturn/mojozork.c:2360, `random_seed = (int) time(NULL);` -- the Saturn
# target reseeds from a fixed constant instead, so this is host-only). Combat,
# ambient events, and party NPCs all draw on that RNG, so left alone, the same
# walkthrough can die to a different troll swing or wander into a different
# room on every run. That is exactly the nondeterminism this generator cannot
# tolerate. mojozork.c is off limits for this task, so the fix happens at
# link time instead: this shim provides mojozork's only two time() call sites
# a fixed clock, without editing a single line the interpreter ships with.
# It is written into the same throwaway build tempdir as the binary itself --
# nothing under version control changes.
_TIME_SHIM = """
#include <time.h>
time_t time(time_t *out) {
    const time_t fixed = (time_t) 1420070400; /* 2015-01-01 00:00:00 UTC */
    if (out) *out = fixed;
    return fixed;
}
"""


def build_mojozork(tmp):
    """Compile the host interpreter once; returns the binary path."""
    exe = tmp / "mojozork"
    shim = tmp / "fixed_time.c"
    shim.write_text(_TIME_SHIM, encoding="utf-8")
    subprocess.run(["gcc", "-O2", "-o", str(exe),
                    str(ROOT / "saturn" / "mojozork.c"), str(shim)], check=True)
    return exe


def run(exe, story_path, commands):
    """Feed commands to the interpreter and return its turn chunks.

    mojozork echoes a prompt before each turn's output, so splitting on it gives
    one chunk per command. Chunk 0 is the banner plus the opening room and is
    discarded wholesale -- 'look' is issued as the second command so the opening
    room comes back as a clean chunk of its own.
    """
    script = "verbose\nlook\n" + "\n".join(commands) + "\n"
    proc = subprocess.run([str(exe), str(story_path)], input=script, capture_output=True,
                          text=True, errors="replace", timeout=120)
    return PROMPT.split(proc.stdout)[1:]


def rooms_from(chunks):
    """Every chunk that looks like a room entry, as (title, body)."""
    found = []
    for chunk in chunks:
        lines = [ln.rstrip() for ln in chunk.strip("\n").split("\n")]
        lines = [ln for ln in lines if ln.strip()]
        if len(lines) < 2:
            continue
        title = lines[0].strip()
        if not title or len(title) > TITLE_MAX:
            continue
        # A room title is a bare noun phrase. Anything ending in sentence
        # punctuation -- including a closing quote, which means it's the tail
        # of a line of dialogue -- is the interpreter talking, not a place.
        if title[-1] in ".!?,;:\"'":
            continue
        # A bracketed or parenthesized first line is always mojozork's own
        # aside (disambiguation questions, "[Maximum verbosity.]", a
        # supporter/vehicle gloss like "(on the bed)") or the game's, never a
        # room name -- confirmed by hand across every one of these seen in the
        # 31-game sweep. A mid-title '?' or '>' is the same tell mid-sentence
        # rather than at the end (a disambiguation prompt, a raw echoed
        # prompt).
        if title[0] in "([" or "?" in title or ">" in title:
            continue
        if title.lower() in [j for j in JUNK]:
            continue
        if any(title.lower().startswith(p) for p in TITLE_JUNK):
            continue
        body = " ".join(lines[1:]).strip()
        if len(body) < MIN_BODY:
            continue
        if any(body.lower().startswith(j) for j in JUNK):
            continue
        if any(t in body.lower() for t in BODY_TAINT):
            continue
        found.append((title, body))
    return found


def c_string(s):
    """A C string literal for s, with the escapes C actually needs.

    The Z-machine's punctuation alphabet (zstory.A2) includes a literal
    newline character, so a decoded description can contain one -- ADVENT's
    "Dead End, near Vending Machine" is a real example, a paragraph break
    before "Hmmm... There is a message here...". Left as a raw byte, it broke
    the C string literal outright (an unescaped newline inside "..." is not
    legal C and every compiler rejects it) and would have gone uncaught by
    every check in this file, since none of them compile the output. It's
    collapsed to a single space here rather than escaped to "\\n", to match
    how the runtime-capture side already flattens multi-line output into one
    space-joined body -- static and runtime rows should look the same shape
    of text for the same kind of content.
    """
    s = " ".join(s.split())
    s = s.replace("\\", "\\\\").replace('"', '\\"')
    return '"' + s + '"'


def main():
    if not Z3.is_dir():
        print(f"FAIL: no game library at {Z3} -- run tools/assets/games.bat first")
        return 1

    stories = sorted(Z3.glob("*.Z3"))
    if not stories:
        print(f"FAIL: no .Z3 files under {Z3}")
        return 1

    wander = [ln.strip() for ln in WANDER.read_text().splitlines() if ln.strip()]
    rows = []
    barren = []

    with tempfile.TemporaryDirectory() as td:
        exe = build_mojozork(pathlib.Path(td))
        for story_path in stories:
            stem = story_path.stem.upper()
            story = zstory.Story(story_path)  # also the "decodes successfully" check

            static, desc_prop, desc_score, skipped = static_rooms(story)
            seen = {}
            for title, body in static:
                seen.setdefault(title, body)
            n_static = len(seen)

            passes = []
            win = SOLUTIONS / f"{stem}.WIN"
            if win.is_file() and win.stat().st_size > 0:
                cmds = [ln.strip() for ln in win.read_text(errors="replace").splitlines()
                        if ln.strip() and not ln.strip().startswith("#")]
                for title, body in rooms_from(run(exe, story_path, cmds)):
                    seen.setdefault(title, body)
                passes.append("solution")

            for title, body in rooms_from(run(exe, story_path, wander)):
                seen.setdefault(title, body)
            passes.append("wander")
            n_runtime = len(seen) - n_static

            print(f"  {stem:10s} descprop={desc_prop:2d}({desc_score:3d})  "
                  f"static={n_static:3d}  runtime={n_runtime:3d}  skipped={skipped:3d}  "
                  f"({', '.join(passes)})")
            if not seen:
                barren.append(stem)
            for title in sorted(seen):
                rows.append((story.release, story.serial, title, seen[title]))

    unexpected_barren = [s for s in barren if s not in NEVER_NAVIGATED]
    if unexpected_barren:
        print(f"FAIL: produced no rooms at all: {', '.join(unexpected_barren)}")
        return 1
    if barren:
        print(f"NOTE: no rooms, expected (menu-only disc, never navigated): {', '.join(barren)}")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write("/*----------------------\n")
        f.write(" | rooms.inc\n")
        f.write(" | Description: GENERATED FILE -- do not edit by hand; produced by\n")
        f.write(" |   tools/gen_room_corpus.py. Room titles and text captured from every\n")
        f.write(" |   story in tools/assets/Z3 -- statically decoded from each story's\n")
        f.write(" |   object table where possible, filled in by driving host mojozork\n")
        f.write(" |   where it isn't. Used by test/room_class_test.c. Carries no expected\n")
        f.write(" |   categories: those live in blessed.inc.\n")
        f.write(" | Author: suinevere\n")
        f.write(" ----------------------*/\n")
        f.write(f"#define CORPUS_N {len(rows)}\n")
        f.write("static const CorpusRoom CORPUS[CORPUS_N] = {\n")
        for release, serial, title, body in rows:
            f.write(f"    {{ {release}, {c_string(serial)}, {c_string(title)},\n")
            f.write(f"      {c_string(body)} }},\n")
        f.write("};\n")

    print(f"gen_room_corpus: {len(rows)} rooms from {len(stories)} stories -> {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
