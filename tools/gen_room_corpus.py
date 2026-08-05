#!/usr/bin/env python3
"""Capture real room text from every story in tools/assets/Z3 into a C fixture.

The classifier's job is judgement, and the only way to know a change improved it
is to run it over the prose the games actually print. This drives host mojozork
twice per game -- once through its winning walkthrough where one exists, once
through a fixed wander script -- and keeps whatever rooms either pass reached.

It never decides what the RIGHT category is. That belongs to blessed.inc, which
room_class_test writes; this file only records what the games say.

Determinism is a hard requirement: the wander script is fixed and checked in, and
the junk filter is a phrase list rather than a heuristic, so a regeneration with
an unchanged game library produces a byte-identical rooms.inc. If that ever stops
holding, the snapshot suite is broken even while it is passing.

Run: python tools/gen_room_corpus.py
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
Z3 = ROOT / "tools" / "assets" / "Z3"
SOLUTIONS = ROOT / "tools" / "typeahead" / "solutions"
WANDER = ROOT / "tools" / "wander.txt"
OUT = ROOT / "test" / "corpus" / "rooms.inc"

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

TITLE_MAX = 64          # matches TEXT_TITLE_MAX in room_class.c
MIN_BODY = 20           # a room prints prose, not a two-word acknowledgement

# Some stories never clear the title filter, for reasons specific to each --
# not a capture bug, a fact about how that game presents itself. Named here so
# the barren check below can tell "expected" apart from "something broke."
#
#   INFOSAM5, INFOSAM7 -- Infocom's promotional sampler discs: a "press a
#     letter to pick an excerpt" menu, nothing else. The wander script only
#     ever sends direction words, so they never pick an excerpt and never
#     print a room.
#   SUSPENDD -- caught by TITLE_JUNK above; its blind robots narrate dialogue,
#     never a room description.
#   MOONMIST, SEASTLKR, WITNESS -- these three print a bare-noun title only on
#     a room's very first visit, as part of an unbroken paragraph the '\n>'
#     split never isolates cleanly; every *revisit* -- which is what a fixed,
#     generic wander/solution script mostly produces -- names the room
#     parenthesized instead, e.g. "(Kemp's office)", indistinguishable by
#     shape from this same engine's disambiguation asides like "(Which Linder
#     do you mean...)". Since parenthesized text is rejected everywhere else
#     in this file specifically because it's usually the interpreter talking,
#     staying consistent here means these three contribute nothing.
#   SORCERER -- under this fixed script, the walkthrough and the wander pass
#     each reach exactly one clean two-line room ("Twisted Forest") and it is
#     the same chunk both times: a hellhound kills the player there, and
#     because a mid-walkthrough death restarts the story without a "> " in
#     between, that one capture is the BODY_TAINT case above. Nothing else in
#     either pass ever clears the title filter, so with the taint dropped
#     there is nothing left.
KNOWN_BARREN = ("INFOSAM5", "INFOSAM7", "SUSPENDD", "MOONMIST", "SEASTLKR", "WITNESS", "SORCERER")

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


def story_header(path):
    """The Z-header's release number (0x02, big-endian) and 6-char serial (0x12)."""
    raw = path.read_bytes()
    release = (raw[0x02] << 8) | raw[0x03]
    serial = raw[0x12:0x18].decode("ascii", "replace")
    return release, serial


def run(exe, story, commands):
    """Feed commands to the interpreter and return its turn chunks.

    mojozork echoes a prompt before each turn's output, so splitting on it gives
    one chunk per command. Chunk 0 is the banner plus the opening room and is
    discarded wholesale -- 'look' is issued as the second command so the opening
    room comes back as a clean chunk of its own.
    """
    script = "verbose\nlook\n" + "\n".join(commands) + "\n"
    proc = subprocess.run([str(exe), str(story)], input=script, capture_output=True,
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
    """A C string literal for s, with the escapes C actually needs."""
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
        for story in stories:
            stem = story.stem.upper()
            release, serial = story_header(story)
            seen = {}
            passes = []

            win = SOLUTIONS / f"{stem}.WIN"
            if win.is_file() and win.stat().st_size > 0:
                cmds = [ln.strip() for ln in win.read_text(errors="replace").splitlines()
                        if ln.strip() and not ln.strip().startswith("#")]
                for title, body in rooms_from(run(exe, story, cmds)):
                    seen.setdefault(title, body)
                passes.append("solution")

            for title, body in rooms_from(run(exe, story, wander)):
                seen.setdefault(title, body)
            passes.append("wander")

            print(f"  {stem:10s} {len(seen):4d} rooms  ({', '.join(passes)})")
            if not seen:
                barren.append(stem)
            for title in sorted(seen):
                rows.append((release, serial, title, seen[title]))

    unexpected_barren = [s for s in barren if s not in KNOWN_BARREN]
    if unexpected_barren:
        print(f"FAIL: produced no rooms at all: {', '.join(unexpected_barren)}")
        return 1
    if barren:
        print(f"NOTE: no qualifying rooms, expected (see KNOWN_BARREN): {', '.join(barren)}")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write("/*----------------------\n")
        f.write(" | rooms.inc\n")
        f.write(" | Description: GENERATED FILE -- do not edit by hand; produced by\n")
        f.write(" |   tools/gen_room_corpus.py. Real room text captured from every story\n")
        f.write(" |   in tools/assets/Z3, used by test/room_class_test.c. Carries no\n")
        f.write(" |   expected categories: those live in blessed.inc.\n")
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
