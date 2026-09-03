#!/usr/bin/env python3
"""Emit saturn/src/engine/map_marks_data.inc: the passages Infocom drew a
narrow-passageway mark across, reconciled against the story's own exit graph.

Usage:
    python3 tools/gen_map_marks.py --cache DIR [--only STORY ...] [--report]
      > saturn/src/engine/map_marks_data.inc
    python3 tools/gen_map_marks.py --cache DIR --only ZORK1 --audit
      > docs/ZORK1_MAP_SCAN_AUDIT.md

--audit emits no C at all. It reseeds the same tracer at every box edge rather
than at the cross-bar clusters, so that it reads a page's whole line network,
and writes the comparison against the story's exit graph as a report for a
person to rule on. See audit() below.

This is the one thing on the map that cannot be derived from a compiled story.
The other four legend symbols the runtime draws -- the one-way arrowhead, the
U/D on a vertical exit, the dash for a conditional passage, the circle for an
exit returning to the room it left -- are all statements about the exit graph,
which the runtime already holds exactly. "Narrow passageway (baggage limit)" is
a statement about what a passage is conditional *on*, and a compiled story
records only that a condition exists, never what it tests. So it has to be read
off the drawing.

Reading a drawing is not exact, and the graph is, so the two are not equals.
The single rule this script exists to enforce:

    The scan may resolve a passage only when every exit on it is conditional.
    Where the graph has asserted anything -- a plain destination or a blocked
    message -- the graph wins outright and the disagreement is reported rather
    than applied.

That rule costs nothing on the passages this is for. Zork I's chimney is
conditional at both ends: Studio's UP is a routine exit whose destination the
story does not carry, and the Kitchen's DOWN is gated on a flag the game never
sets. Resolving those overrules no fact. It is what stops a misread line
anywhere else in the corpus from overwriting geography the story states
plainly.

The White Cliffs are the calibration. Zork I's DEFLATE flag gates them with the
refusal "The path is too narrow", which read from the source alone is a baggage
limit; Infocom drew those three lines plain, and they must not appear in the
output. Their absence is the evidence that this is reading the drawing rather
than guessing from the wording of a refusal -- see
tools/tests/test_gen_map_marks.py, which also holds the emitted table against
Zork I's ZIL, the one independent oracle on the disc.

THE MAPS ARE NOT REDISTRIBUTED. They are Activision's, scanned by the Infocom
Documentation Project (infodoc.plover.net) and reproduced there with permission.
This script downloads them into a local cache that is not part of the repository
and emits only object numbers and direction indices. Do not commit the cache.

Needs pymupdf, opencv-python, numpy and rapidocr-onnxruntime. Run by hand when
the maps or the story builds change, not by the Saturn build.
"""
import argparse
import os
import subprocess
import sys
import textwrap

import pymupdf

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
import mapscan
import trace_edges
import zstory
from gen_map_atlas import BASE_URL, MAPS

MARK_BAGGAGE = 1
"""Annotate this exit as a narrow passageway. Matches MARK_BAGGAGE in the
design note and the flag map_marks.h will carry; the value is a bit rather than
an enumerator because one exit can in principle be both annotated and
corrected."""

MARK_RETRACT = 2
"""The drawing shows no such passage in this direction, so do not offer it.
Only ever emitted against an exit the graph left conditional, so it withdraws
a possibility the story never asserted rather than contradicting one it did."""

_ITEMS = {}
"""The page-reading cache mapscan.page_items keeps its results in, module-wide
rather than per call, because OCR is far and away the slowest thing here and a
test suite calls marks_for several times over the same PDF. Nothing in it
depends on the tracer, so a stubbed hamburger_seeds still produces no marks --
which is the acceptance property the calibration test rests on."""


def story_graph(path):
    """({object: {"name": str, "exits": {direction index: (kind, dest)}}},
    release, serial) for one story.

    mapscan.room_graph with the one thing it does not carry, and keyed by
    direction index rather than name because that is what ships. A conditional
    exit four or five bytes long begins with the object it leads to, and
    room_model_refresh_room trusts that byte wherever it names an object that
    is itself a room -- which is how the runtime knows the trap door reaches
    the Cellar. room_graph reports every conditional exit's destination as 0,
    which is right for what it is used for (laying out an atlas from plain
    exits) and wrong here: it would make the Altar's descent look like a
    destination the drawing has to supply, when the story already has it, and
    the mark would ship a redundant correction. The same decode is repeated
    rather than pushed into room_graph so that gen_map_atlas's output stays
    byte-identical.
    """
    graph, release, serial = mapscan.room_graph(path)
    story = zstory.Story(path)
    raw = story.raw
    p2d = {v: k for k, v in mapscan.direction_props(raw).items()}
    rooms = {o.num for o in story.objects if any(p in p2d for p in o.properties)}
    out = {}
    for obj in story.objects:
        if obj.num not in graph:
            continue
        exits = {}
        for pnum, (addr, plen) in obj.properties.items():
            d = p2d.get(pnum)
            if d is None:
                continue
            kind, dest = graph[obj.num]["exits"][mapscan.DIRW[d]]
            if kind == "MAYBE" and plen in (4, 5) and raw[addr] in rooms:
                dest = raw[addr]
            exits[d] = (kind, dest)
        out[obj.num] = {"name": graph[obj.num]["name"], "exits": exits}
    return out, release, serial


def map_pages(pdf, names, cache):
    """Which pages of a PDF are maps, by gen_map_atlas.build_game's own test.

    Repeated rather than imported because build_game does the whole layout and
    validation as one pass and returns positions, not pages. The gates are its:
    a page must yield MIN_BOXES boxes naming rooms the story has, and (when it
    is read as boxes rather than as a floor plan's bare labels) that must be
    MIN_NAMED of everything it drew.
    """
    out = []
    for page in range(1, pymupdf.open(pdf).page_count + 1):
        named, total, mode = mapscan.page_items(pdf, page, names, cache)
        if len(named) < mapscan.MIN_BOXES:
            continue
        if mode == "box" and len(named) < mapscan.MIN_NAMED * total:
            continue
        out.append(page)
    return out


def _end_room(pdf, page, run, end, named, names):
    """The story room name one end of a run reaches, or None.

    A box end is already named -- page_items resolved its OCR reading against
    the story's own room names. An open end is a stub, which is how Infocom
    draws a passage whose far end is on another page, and trace_edges reads its
    parenthetical caption.
    """
    if end[0] == "box":
        return named[end[1]][4]
    return trace_edges.open_end_room(pdf, page, run, names)


def _pair(graph, name_a, name_b):
    """The two object numbers a run's endpoint names mean, in the order given,
    or None when the reading does not settle on one pair.

    A name is not an object. Zork I gives two rooms the name "Cave", and the
    one the Altar descends into is the one its own exit points at -- so the
    ambiguity is resolved against the graph rather than against the drawing's
    geometry, which is what gen_map_atlas has to do for a whole page at once
    and does not have to be redone here for two boxes. Where the graph joins no
    candidate pair (the chimney nearly qualifies: Studio's UP carries no
    destination at all) a pair of unambiguous names still resolves, since there
    is only one thing either can mean.
    """
    a_set = [o for o, r in graph.items() if mapscan.norm(r["name"]) == name_a]
    b_set = [o for o, r in graph.items() if mapscan.norm(r["name"]) == name_b]
    joined = [(a, b) for a in a_set for b in b_set if a != b
              and (b in [d for _, d in graph[a]["exits"].values()]
                   or a in [d for _, d in graph[b]["exits"].values()])]
    if len(joined) == 1:
        return joined[0]
    if not joined and len(a_set) == 1 and len(b_set) == 1:
        return (a_set[0], b_set[0])
    return None


def _toward(graph, a, b):
    """The direction indices of a's exits that could be the drawn passage to b.

    Either the graph says so outright, or it says nothing: a conditional or
    blocked exit whose destination the story does not carry is exactly the case
    the drawing is allowed to speak to. Blocked exits are candidates too, and
    deliberately -- their destination is never recorded, so leaving them out
    would hide the one kind of exit the contribution rule most needs to see in
    order to veto a pair.
    """
    return sorted(d for d, (kind, dest) in graph[a]["exits"].items()
                  if dest == b or (dest == 0 and kind != "OPEN"))


def _passages(pdf, page, graph, names, cache):
    """[((a, b), head)] for one page: each room pair a narrow-passageway mark
    is drawn across, and which of the two the arrowhead points at, or None.

    Every cluster is followed and every reading is verified, because a cluster
    is a lead rather than a placement: of the thirteen the underground page
    yields, four resolve to a room pair and the rest end in open space or come
    back to the box they left. Several clusters legitimately land on one
    passage -- Infocom repeats the mark along a run, four times on the Altar's
    descent -- so a caller keys by the pair, not by the cluster.
    """
    named, _, _ = mapscan.page_items(pdf, page, names, cache)
    boxes = [b[:4] for b in named]
    mask = trace_edges.ink_mask(pdf, page)
    out = []
    for (x, y, axis) in trace_edges.hamburger_seeds(mask):
        run = trace_edges.follow(mask, boxes, (x, y),
                                 (1, 0) if axis == "h" else (0, 1))
        if all(e[0] == "open" for e in run["ends"]):
            continue
        ends = [_end_room(pdf, page, run, e, named, names) for e in run["ends"]]
        if None in ends or ends[0] == ends[1]:
            continue
        pair = _pair(graph, ends[0], ends[1])
        if pair is None:
            continue
        arrow = trace_edges.arrow_end(mask, run)
        out.append((pair, None if arrow == 0 else pair[2 - arrow]))
    return out


def _veto(graph, a, b):
    """Why the drawing may not resolve this pair, or None if it may.

    The branch's one rule in its one implementation: the scan may resolve a
    passage only where every exit on it is conditional, and any exit the story
    has already asserted -- a plain destination or a refusal message -- wins
    outright. marks_for reports the reason it returns and moves on; _eligible
    asks only whether there is one. Two spellings of this cascade would agree
    until the day room_graph learned a fourth kind of exit, and the drift would
    show up as a shipped mark rather than as a failing test.
    """
    ab, ba = _toward(graph, a, b), _toward(graph, b, a)
    stated = [d for d in ab if graph[a]["exits"][d][0] == "OPEN"]
    stated += [d for d in ba if graph[b]["exits"][d][0] == "OPEN"]
    refused = [d for d in ab if graph[a]["exits"][d][0] == "BLOCKED"]
    refused += [d for d in ba if graph[b]["exits"][d][0] == "BLOCKED"]
    if stated:
        return ("the story states this passage outright, so the graph wins and"
                " no mark is emitted")
    if refused:
        return ("the story refuses a direction here with a message, and a"
                " refusal records no destination, so it cannot be shown to"
                " concern this pair rather than some other; no mark is emitted")
    if not ab and not ba:
        return ("the graph joins these rooms in neither direction, so there is"
                " no exit to annotate")
    return None


def marks_for(story, pdf, disagreements=None):
    """[(room, direction index, destination, flags)] for one story.

    The reconciliation itself. A pair the drawing marks contributes nothing
    unless every exit joining it is conditional; where it does contribute, both
    directions are annotated unless the drawing shows an arrowhead, in which
    case the direction it points away from is withdrawn instead. A destination
    is emitted only where the story carries none.

    An arrowhead is only believed when every trace of the passage found one at
    the same end. The two marks are not equals: a baggage annotation adds a
    symbol to an exit the player can still take, while a retraction takes the
    exit off the display, so a head that one trace sees and another misses is
    not evidence enough to remove anything. Infocom repeats the mark along a
    run and each repeat is traced separately, so a passage routinely has more
    than one reading to agree; arrow_end's own docstring records that the
    Altar-to-Cave head is found by one of its two traces and missed by the
    other, which is exactly the split this rule is written for. Costing that
    passage its head costs it nothing, because the Cave has no exit back for a
    retraction to have applied to.
    """
    graph, _, _ = story_graph(os.path.join(mapscan.Z3DIR, story + ".Z3"))
    names = {mapscan.norm(r["name"]) for r in graph.values()} - {""}
    report = [] if disagreements is None else disagreements

    seen = {}
    for page in map_pages(pdf, names, _ITEMS):
        for pair, head in _passages(pdf, page, graph, names, _ITEMS):
            entry = seen.setdefault(frozenset(pair), [pair, []])
            entry[1].append(head)

    marks = []
    for (a, b), heads in seen.values():
        ab, ba = _toward(graph, a, b), _toward(graph, b, a)
        label = "%s (%d) -- %s (%d)" % (graph[a]["name"], a, graph[b]["name"], b)
        veto = _veto(graph, a, b)
        if veto is not None:
            report.append(label + ": " + veto)
            continue
        aimed = {h for h in heads if h is not None}
        if len(aimed) > 1:
            report.append(label + ": its traces put the arrowhead at different"
                          " ends, so which way it runs is unsettled and no mark"
                          " is emitted")
            continue
        head = None
        if aimed and None not in heads:
            head = next(iter(aimed))
        elif aimed:
            report.append(label + ": %d of %d traces found an arrowhead, so it"
                          " is annotated both ways rather than retracting a"
                          " direction on a split reading"
                          % (len(heads) - heads.count(None), len(heads)))
        runs = [(a, b, ab), (b, a, ba)]
        if head is not None:
            runs = [r for r in runs if r[1] == head]
            back = ab if head == a else ba
            marks += [(head, d, 0, MARK_RETRACT) for d in back]
        for src, dst, dirs in runs:
            for d in dirs:
                dest = dst if graph[src]["exits"][d][1] == 0 else 0
                marks.append((src, d, dest, MARK_BAGGAGE))
    return sorted(_one_per_exit(marks))


def _one_per_exit(marks):
    """The marks, with at most one row per (room, direction).

    The shipped table is keyed on that pair -- map_marks_for looks a mark up
    by the exit it annotates -- and nothing above guarantees it. A conditional
    exit whose destination the story does not record is a candidate for every
    passage its room is drawn on, so a room carrying two drawn passages can be
    offered two different destinations for one direction. That is a reading
    the scan cannot settle rather than something to pick a winner from, so it
    stops the run loudly here instead of shipping whichever row the sort
    happened to put second. An exact repeat is not a conflict and collapses.
    """
    out = {}
    for mark in marks:
        key = mark[:2]
        if key in out and out[key] != mark:
            raise AssertionError(
                "two different marks for room %d direction %s: %r and %r"
                % (key[0], mapscan.DIRW[key[1]], out[key], mark))
        out[key] = mark
    return list(out.values())


EDGE_OFFSET = trace_edges.BOX_PAD + 2
"""Pixels outside a box's own rectangle that an --audit edge seed is placed at.

Pinned from both sides by trace_edges' own two constants. ink_mask erases a
band BOX_PAD wide around every box, so a seed inside that band sits on blank
mask with nothing for the walk to pick up; and _box_at counts any point within
BOX_PAD + STEP of a box as inside it, so a seed beyond that would no longer
have its inward walk terminate on the box it was seeded from. Two pixels past
the erased band satisfies the first and stays under the second, which is what
makes one end of every edge-seeded run a known room for free.
"""

ZORK1_ATLAS_ROOMS = 84
"""How many Zork I rooms gen_map_atlas places, for the traced-room count to be
read against. Not derived here: the atlas is laid out by a different pass over
the same pages and this is the figure it reports, quoted so the audit's own
coverage has something to be a fraction of."""


def _edge_seeds(box):
    """[((x, y), (dx, dy))]: the four points just outside a box's edge
    midpoints, each with the heading that leads away from the box."""
    x, y, w, h = box
    cx, cy = x + w // 2, y + h // 2
    return [((cx, y - EDGE_OFFSET), (0, -1)),
            ((cx, y + h + EDGE_OFFSET), (0, 1)),
            ((x - EDGE_OFFSET, cy), (-1, 0)),
            ((x + w + EDGE_OFFSET, cy), (1, 0))]


def _network(pdf, page, names, cache):
    """({frozenset({name, name}): [(name, name), [head name or None, ...]]},
    stats) for one page: every room pair the drawing joins with a line.

    Same tracer as _passages, seeded differently. _passages starts at detected
    cross-bar clusters, which finds only the passages carrying a mark; this
    starts at every box's four edge midpoints, which finds whatever leaves a
    box through the middle of one of its sides. Neither seeding is complete,
    and this one's blind spot is stated rather than hidden: Infocom does not
    always leave a box from the centre of an edge, and a line joining a box
    nearer a corner is not seeded at all. That is the main reason a page's
    traced pairs fall short of the exits its rooms actually have, and why the
    counts this returns are reported alongside every conclusion drawn from
    them.

    Runs are deduplicated by their resolved endpoint pair rather than by seed,
    because the two boxes at either end of one line are each seeded toward the
    other and a two-way passage is therefore traced at least twice. The
    arrowhead reading of every trace is kept, not just the first, so a caller
    can see whether the traces of one passage agreed about which end carries
    the head.
    """
    named, _, _ = mapscan.page_items(pdf, page, names, cache)
    boxes = [b[:4] for b in named]
    mask = trace_edges.ink_mask(pdf, page)
    runs = {}
    stats = {"boxes": len(boxes), "seeds": 0, "self": 0, "unread": 0, "traced": 0}
    for box in boxes:
        for seed, heading in _edge_seeds(box):
            stats["seeds"] += 1
            run = trace_edges.follow(mask, boxes, seed, heading)
            ends = [_end_room(pdf, page, run, e, named, names)
                    for e in run["ends"]]
            if None in ends:
                stats["unread"] += 1
                continue
            if ends[0] == ends[1]:
                stats["self"] += 1
                continue
            stats["traced"] += 1
            arrow = trace_edges.arrow_end(mask, run)
            entry = runs.setdefault(frozenset(ends), [tuple(ends), []])
            entry[1].append(None if arrow == 0 else ends[2 - arrow])
    stats["pairs"] = len(runs)
    return runs, stats


def _asserted(graph, a, b):
    """The direction indices of a's exits that name b outright.

    Distinct from _toward, which also returns the exits whose destination the
    story does not carry. Those are what the drawing is allowed to speak to;
    these are what it would be contradicting.
    """
    return sorted(d for d, (_kind, dest) in graph[a]["exits"].items()
                  if dest == b)


def _cite(graph, a, b):
    """"west MAYBE, down MAYBE(46)" -- every exit of a the drawn passage to b
    could be, with its kind and any destination the story carries, so a reader
    can apply the contribution rule to the row without rerunning anything."""
    out = []
    for d in _toward(graph, a, b):
        kind, dest = graph[a]["exits"][d]
        out.append("%s %s%s" % (mapscan.DIRW[d], kind,
                                "(%d)" % dest if dest else ""))
    return ", ".join(out) if out else "none"


def _eligible(graph, a, b):
    """Whether the all-MAYBE rule would let the scan resolve this pair.

    Not a restatement of marks_for's test but the same call: _veto is the rule,
    and this asks it for a yes or no where marks_for asks it for the sentence
    to report.
    """
    return _veto(graph, a, b) is None


def _settled_head(heads):
    """The end every trace of a passage put the arrowhead at, or None.

    marks_for's policy, and for the same reason: arrow_end misses a real head
    about as often as it finds one on a second trace of the same wedge, so a
    head one trace saw and another did not is not a reading to build a
    directional claim on. The audit reports the split rather than resolving it.
    """
    if not heads or None in heads:
        return None
    return heads[0] if len(set(heads)) == 1 else None


def _bucket(graph, pair, head):
    """Which of the four buckets a drawn pair falls in, and why.

    agree, drawn only, direction differs; "graph only" is not decided here
    because it is a pair with no drawing at all. A pair the drawing joins and
    the graph does not assert is "drawn only" whatever its arrowhead says,
    since there is no graph direction for the head to differ from. Where the
    graph does assert the join, the arrowhead is checked against it: an
    arrowhead is a claim that the passage runs one way only, so it differs from
    the graph both when the graph has no exit in the drawn direction and when
    the graph has one in the direction the arrowhead denies.
    """
    a, b = pair
    ab, ba = _asserted(graph, a, b), _asserted(graph, b, a)
    if not ab and not ba:
        return "drawn only", "the graph asserts no exit between these rooms"
    if head is None:
        return "agree", "the graph joins them and the drawing marks no direction"
    other = b if head == a else a
    into = ab if head == b else ba
    back = ba if head == b else ab
    if not into:
        return ("direction differs",
                "the head points at %s but the graph gives %s no exit into it"
                % (_label(graph, head), _label(graph, other)))
    if back:
        return ("direction differs",
                "the head makes this one way into %s but the graph runs both"
                " ways" % _label(graph, head))
    return ("agree", "the graph runs one way into %s and so does the drawing"
            % _label(graph, head))


def _label(graph, obj):
    """"Kitchen (203)" -- a room named the way every row of the report names
    it, since a name alone is not an object on a map with four Forests."""
    return "%s (%d)" % (graph[obj]["name"], obj)


def _cite_pair(graph, a, b):
    """Both ends' exit states in one table cell: what the graph offers in
    either direction of a pair the drawing joins."""
    return "%s: %s; %s: %s" % (_label(graph, a), _cite(graph, a, b),
                               _label(graph, b), _cite(graph, b, a))


def _drawn_direction(graph, heads):
    """How the arrowheads read across every trace of one passage, in words.

    heads are object numbers or None, one per trace, in the order the traces
    were taken.
    """
    n = "%d trace%s" % (len(heads), "" if len(heads) == 1 else "s")
    head = _settled_head(heads)
    if head is not None:
        return "one way into %s, on all of %s" % (_label(graph, head), n)
    found = [h for h in heads if h is not None]
    if not found:
        return "no arrowhead, on %s" % n
    if len(set(found)) > 1:
        return ("traces put the head at different ends, %d of %s found one"
                % (len(found), n))
    return ("head into %s on %d of %s, so unsettled"
            % (_label(graph, found[0]), len(found), n))


def _audit_pairs(pdf, pages, graph, names, cache):
    """(per-page counts, {name pair: [names, heads, pages]}, names drawn).

    Everything --audit reads off a story's pages before any judgement is made
    about it. The third return bounds the graph-only bucket: an exit to a room
    no page drew a box for is not a passage the drawing was ever asked about,
    and counting it would report the tracer's page coverage as though it were
    its line coverage.
    """
    stats, drawn, drawn_names = [], {}, set()
    for page in pages:
        named, _, _ = mapscan.page_items(pdf, page, names, cache)
        drawn_names |= {b[4] for b in named}
        runs, st = _network(pdf, page, names, cache)
        st["page"] = page
        stats.append(st)
        for key, (ends, heads) in runs.items():
            entry = drawn.setdefault(key, [ends, [], set()])
            entry[1] += heads
            entry[2].add(page)
    return stats, drawn, drawn_names


def _shipped(story, pdf, pages, graph, names):
    """({frozenset(pair): pair}, marks): the passages the cross-bar seeding
    resolved into shipped marks.

    Recomputed through marks_for rather than handed in, so that the audit
    cannot agree with itself by sharing a variable with the thing it is
    checking. A cluster-seeded pair counts as shipping a mark when one of the
    exits it could annotate is a row in the emitted table.
    """
    marks = marks_for(story, pdf)
    keyed = {(r, d) for r, d, _dest, _flags in marks}
    out = {}
    for page in pages:
        for pair, _head in _passages(pdf, page, graph, names, _ITEMS):
            a, b = pair
            dirs = {(a, d) for d in _toward(graph, a, b)}
            dirs |= {(b, d) for d in _toward(graph, b, a)}
            if dirs & keyed:
                out[frozenset(pair)] = pair
    return out, marks


AUDIT_PREAMBLE = """\
This report changes nothing. No file it describes was edited to match it, no
mark was added or withdrawn because of it, and nothing in
`saturn/src/engine/map_marks_data.inc` is derived from it. It is a listing of
where the lines Infocom drew and the exits the compiled story carries do not
say the same thing, produced so that a person can look at them.

A disagreement here is a question for the owner, not a defect to be
auto-corrected. The left-hand side of every row is a tracer reading scanned
print, and the right-hand side is the exit graph, which is exact. When the two
differ the likelier explanation is that the scan is wrong, and the last section
of this report gives the measured rate at which it is.

Only the all-`MAYBE` rows are eligible for resolution at all. The rule the
shipped generator enforces is that the scan may resolve a passage only when
every exit on it is `RM_EXIT_MAYBE` -- conditional, with the story recording
that a condition exists and never what it tests. If any exit on the pair is
`OPEN` (a plain destination) or `BLOCKED` (a refusal message), the graph has
asserted something, the graph wins outright, and the disagreement is reported
rather than applied. Every row below cites the graph's own exit states so that
the rule can be applied to it by hand, and one column says what applying it
gives.
"""

AUDIT_METHOD = """
## How this was read

    python tools/gen_map_marks.py --cache tools/assets/cache --only ZORK1 \\
        --audit > docs/ZORK1_MAP_SCAN_AUDIT.md

The generator's ordinary path seeds `trace_edges.follow` at the cross-bar
clusters `trace_edges.hamburger_seeds` detects, which finds only the passages
carrying a narrow-passageway mark. `--audit` seeds the same tracer at every
box's four edge midpoints instead, so that it reads the page's whole line
network, and deduplicates the runs by their resolved endpoint pair. A pair is
drawn here when one run or more resolved a room at both of its ends: a box end
is a box the walk terminated in, and an open end is a labelled stub whose
parenthetical caption named a room, which is how Infocom draws a passage whose
far end is on another page.

The four buckets are:

- **agree** -- the graph asserts an exit between the two rooms and the drawn
  direction is consistent with it.
- **drawn only** -- a run joins the two rooms and the graph asserts no exit
  between them in either direction.
- **graph only** -- the graph asserts an exit between two rooms both of which
  were drawn on a traced page, and no run joined them.
- **direction differs** -- both join them and the drawn arrowhead is not
  consistent with the graph's directions, either because the graph gives no
  exit in the direction the head points or because it gives one in the
  direction the head denies.

An arrowhead is believed only where every trace of a passage found one at the
same end, which is `marks_for`'s policy and is held to for its reason:
`trace_edges.arrow_end` misses a real head about as often as a second trace of
the same wedge finds it. A split reading is reported as split rather than
resolved either way.
"""


def _table(out, head, rows):
    """A GitHub-flavoured Markdown table, or a line saying the bucket is empty.

    A table with no rows renders as nothing at all, which on the page reads
    exactly like a bucket the tool forgot to print.
    """
    if not rows:
        out.write("None.\n")
        return
    out.write("| " + " | ".join(head) + " |\n")
    out.write("|" + "|".join(["---"] * len(head)) + "|\n")
    for r in rows:
        out.write("| " + " | ".join(str(c) for c in r) + " |\n")


def _graph_only(graph, drawn, drawn_names):
    """[(pair label, exit states, eligible, one-way)] for every pair the graph
    asserts between two rooms the drawing named and no run joined.

    Bounded to rooms some traced page actually drew a box for, because an exit
    to a room no page names is not a passage the drawing was ever asked about,
    and listing it would inflate the bucket with the tracer's own page
    coverage rather than its line coverage.

    A pair is collected once, as an unordered pair, and only then asked which
    way round the graph asserts it. Deduplicating the other way -- keeping an
    exit only where its destination outranks its source by object number -- is
    what an earlier version of this did, and it drops a one-way exit drawn from
    the higher-numbered room outright: neither room's own loop ever emits it,
    since the lower-numbered one has no exit back to name. Zork I's shipped
    Altar (212) to Cave (46) passage is exactly that shape, and escaped only
    because the drawing does trace it and the drawn check excludes it before
    the ordering test can matter.
    """
    pairs = set()
    for a, room in graph.items():
        for _d, (_kind, dest) in room["exits"].items():
            if dest and dest in graph and dest != a:
                pairs.add((min(a, dest), max(a, dest)))
    out = []
    for a, b in sorted(pairs):
        if not _asserted(graph, a, b) and not _asserted(graph, b, a):
            continue
        na, nb = mapscan.norm(graph[a]["name"]), mapscan.norm(graph[b]["name"])
        if na == nb or na not in drawn_names or nb not in drawn_names:
            continue
        if frozenset((na, nb)) in drawn:
            continue
        out.append(("%s -- %s" % (_label(graph, a), _label(graph, b)),
                    _cite_pair(graph, a, b),
                    "yes" if _eligible(graph, a, b) else "no",
                    not (_asserted(graph, a, b) and _asserted(graph, b, a))))
    return sorted(set(out))


def audit(story, pdf, out, cache):
    """Write the whole-network disagreement report for one story to out.

    The one mode of this script whose output a person reads rather than a test
    asserts, and the only one that emits no C. It compares what the tracer can
    read off the whole drawing against the story's exit graph, buckets each
    room pair, and cites the graph's exit states on every row so that the
    contribution rule can be applied to the listing by hand.
    """
    graph, release, serial = story_graph(
        os.path.join(mapscan.Z3DIR, story + ".Z3"))
    names = {mapscan.norm(r["name"]) for r in graph.values()} - {""}
    pages = map_pages(pdf, names, cache)
    stats, drawn, drawn_names = _audit_pairs(pdf, pages, graph, names, cache)
    shipped, marks = _shipped(story, pdf, pages, graph, names)

    by_name = {}
    for obj, room in graph.items():
        by_name.setdefault(mapscan.norm(room["name"]), []).append(obj)

    buckets = {"agree": [], "drawn only": [], "direction differs": []}
    unkeyed = []
    for key in sorted(drawn, key=sorted):
        ends, heads, on = drawn[key]
        pg = ", ".join(str(p) for p in sorted(on))
        pair = _pair(graph, ends[0], ends[1])
        if pair is None:
            unkeyed.append(("%s -- %s" % ends, pg, len(heads),
                            "%d and %d objects carry these two names"
                            % (len(by_name.get(ends[0], [])),
                               len(by_name.get(ends[1], [])))))
            continue
        a, b = pair
        heads = [None if h is None else (a if h == ends[0] else b)
                 for h in heads]
        bucket, why = _bucket(graph, pair, _settled_head(heads))
        label = "%s -- %s" % (_label(graph, a), _label(graph, b))
        if frozenset(pair) in shipped:
            label += " **[ships a mark]**"
        buckets[bucket].append(
            (frozenset(pair), label, pg, _drawn_direction(graph, heads),
             _cite_pair(graph, a, b),
             "yes" if _eligible(graph, a, b) else "no", why))

    w = out.write
    w("# %s map scan audit\n\n" % story)
    w(AUDIT_PREAMBLE)
    w("\n%s, release %d serial %s, against pages %s of the Infocom "
      "Documentation Project's scan -- which is not redistributed with this "
      "repository and is read out of a local cache.\n"
      % (story, release, serial, ", ".join(str(p) for p in pages)))
    w(AUDIT_METHOD)

    w("\n## What the tracer actually read\n\n")
    _table(out, ["page", "boxes named", "seeds", "runs ending in the box they "
                 "left", "runs whose far end read as no room", "runs resolving "
                 "two rooms", "distinct pairs"],
           [(s["page"], s["boxes"], s["seeds"], s["self"], s["unread"],
             s["traced"], s["pairs"]) for s in stats])
    touched = set()
    for ends, _heads, _on in drawn.values():
        touched |= set(ends)
    w("\n%d distinct room names appear at one end or other of a traced run, "
      "against the %d rooms `gen_map_atlas` places for this story, the %d room "
      "objects the compiled story carries, and the %d distinct names those "
      "objects go by. The shortfall is this seeding's recall and not Infocom "
      "drawing fewer rooms: an edge midpoint only finds a line that leaves a "
      "box through the middle of one of its four sides, and plenty of lines on "
      "these pages leave nearer a corner.\n"
      % (len(touched), ZORK1_ATLAS_ROOMS, len(graph), len(by_name)))

    graph_only = _graph_only(graph, drawn, drawn_names)
    w("\n## Bucket counts\n\n")
    _table(out, ["bucket", "pairs"],
           [("agree", len(buckets["agree"])),
            ("drawn only", len(buckets["drawn only"])),
            ("graph only", len(graph_only)),
            ("direction differs", len(buckets["direction differs"])),
            ("drawn, not keyable to one object pair", len(unkeyed))])

    eligible = [r for r in buckets["drawn only"] if r[5] == "yes"]
    if buckets["direction differs"]:
        w("\nThe direction-differs count rests entirely on"
          " `trace_edges.arrow_end`, whose two shape constants its own"
          " docstring records as having no measured margin left in either"
          " direction, so treat those rows as prompts to look at the page.\n")
    else:
        w("\nThe direction-differs count is zero, and that is a check that did"
          " not run rather than a check that passed. Every drawn pair here"
          " either carried no arrowhead at all or carried one its traces could"
          " not agree on, so nothing was ever compared against the graph's own"
          " directions. `trace_edges.arrow_end`'s own docstring records that"
          " its two shape constants have no measured margin left in either"
          " direction and that it misses a real head about as often as a"
          " second trace of the same wedge finds one; a zero here is that"
          " silence, not evidence that Infocom drew no one-way passage this"
          " graph disagrees with.\n")

    for name in ("agree", "drawn only", "direction differs"):
        w("\n## %s\n\n" % name.capitalize())
        _table(out, ["pair", "page", "drawn direction", "graph exit states",
                     "all-MAYBE, so resolvable", "reading"],
               [r[1:] for r in buckets[name]])
        if name == "direction differs" and not buckets[name]:
            w("\nEmpty because no drawn pair carried an arrowhead every one of"
              " its traces agreed on, so no drawn direction was ever put to"
              " the graph. Read this section as unexercised, not as clean.\n")

    w("\n## Graph only\n\n")
    w("Pairs the graph asserts between two rooms both drawn on a traced page, "
      "which no run joined. Nothing is drawn here for the rule to resolve, so "
      "the last column says only whether the rule would permit the scan to "
      "speak about the pair at all -- and the only thing it could say against "
      "a pair the drawing does not show is a retraction.\n\n")
    _table(out, ["pair", "graph exit states", "all-MAYBE"],
           [r[:3] for r in graph_only])
    w("\n%d of these %d pairs are joined by an exit in one direction only. "
      "That number is worth stating because an earlier version of this "
      "enumeration deduplicated pairs by requiring an exit's destination to "
      "outrank its source by object number, which drops a one-way exit drawn "
      "from the higher-numbered room from both loop iterations at once and "
      "reported it nowhere at all. The bucket is now collected as unordered "
      "pairs and only then asked which way round the graph asserts it.\n"
      % (sum(1 for r in graph_only if r[3]), len(graph_only)))

    w("\n## Drawn, but not keyable to one object pair\n\n")
    w("Runs whose two endpoint names do not settle on a single pair of "
      "objects, because Zork I gives several rooms the same short name -- "
      "fifteen of them are called Maze. These are readings the audit cannot "
      "bucket, not readings it rejects.\n\n")
    _table(out, ["names", "page", "traces", "why"], unkeyed)

    w("\n## The passages that ship\n\n")
    w("`saturn/src/engine/map_marks_data.inc` carries %d marks for this story, "
      "from %d passages the cross-bar seeding resolved. Whether this report's "
      "independent edge seeding found each of them again is a check on the "
      "seeding, not on the marks; the rows are tagged **[ships a mark]** in "
      "the buckets above.\n\n" % (len(marks), len(shipped)))
    placed = {}
    for name, rows in buckets.items():
        for r in rows:
            placed[r[0]] = (r[1], name)
    _table(out, ["passage", "bucket it appears in"],
           [placed.get(key, ("%s -- %s" % (_label(graph, pair[0]),
                                           _label(graph, pair[1])),
                             "**not found by the edge seeding**"))
            for key, pair in sorted(shipped.items(), key=lambda kv: sorted(kv[1]))])

    w("\n## What this audit is worth\n\n")
    w("The cross-bar seeding this repository already trusts yields about "
      "thirteen seeds on the underground page of which four or five resolve "
      "cleanly, and it was scored against Zork I's own ZIL source before "
      "anything shipped from it. The edge seeding here has no such oracle, so "
      "its own rate has to stand in its place: %d seeds across %d pages "
      "produced %d runs that resolved a room at both ends, while %d ended "
      "back in the box they left and %d reached something that read as no "
      "room at all -- %.0f%% of every seed laid down.\n\n"
      % (sum(s["seeds"] for s in stats), len(stats),
         sum(s["traced"] for s in stats), sum(s["self"] for s in stats),
         sum(s["unread"] for s in stats),
         100.0 * sum(s["unread"] for s in stats)
         / max(1, sum(s["seeds"] for s in stats))))
    if eligible:
        w("**%d of the %d drawn-only pairs are eligible under the all-MAYBE "
          "rule.** Those are the rows where the drawing says something the "
          "graph has not already settled and the contribution rule would "
          "permit the scan to speak, so they are the ones to rule on:%s.\n\n"
          % (len(eligible), len(buckets["drawn only"]),
             "".join("\n\n- " + r[1] for r in eligible)))
    else:
        vetoed = [r for r in buckets["drawn only"]
                  if _toward(graph, *sorted(r[0]))
                  or _toward(graph, *sorted(r[0], reverse=True))]
        w("**Not one of the %d drawn-only pairs is eligible under the "
          "all-MAYBE rule.** %d are vetoed by an exit the story has already "
          "asserted, a plain destination or a refusal message; on the "
          "remaining %d the graph offers no exit the drawing could be about "
          "in either direction, so there is nothing to annotate at all. Read "
          "across the whole "
          "drawn network, that is this report's single most useful finding: "
          "the contribution rule "
          "permits the scan to add nothing beyond the %d passages already "
          "shipped. The edge seeding turned up no new passage the rule would "
          "let anyone act on, so there is no pending change here waiting to be "
          "approved -- only the rejected readings listed above, kept because a "
          "scan that dropped them silently would be indistinguishable from a "
          "scan that saw nothing.\n\n"
          % (len(buckets["drawn only"]), len(vetoed),
             len(buckets["drawn only"]) - len(vetoed), len(shipped)))
    w("The buckets are not equally strong and should not be read as though "
      "they were. A **drawn only** row is the tracer claiming a passage the "
      "story does not carry, which is either a real conditional passage or a "
      "misread line, and the all-MAYBE column is what separates the rows worth "
      "looking at from the ones the graph already forbids. A **graph only** "
      "row is much weaker evidence of anything, because this seeding is known "
      "not to find every line: that count is dominated by lines leaving a box "
      "away from an edge midpoint, by exits drawn across a page boundary, and "
      "by pairs whose names could not be keyed to objects. **Direction "
      "differs** rests entirely on `trace_edges.arrow_end`, whose two shape "
      "constants its own docstring records as having no measured margin left "
      "in either direction, so a row there is a prompt to look at the page "
      "rather than a finding -- and its count of %d says how much of that "
      "check actually ran, not how much of it passed.\n"
      % len(buckets["direction differs"]))


def emit(tables, out):
    """Write map_marks_data.inc, in the shape gen_map_atlas.emit writes the
    atlas: one array per story, then a table keyed on the release and serial in
    the Z-machine header, then the count."""
    w = out.write
    w("/*----------------------\n")
    w(" | map_marks_data.inc\n")
    w(" | Description: Generated by tools/gen_map_marks.py -- do not edit. The\n")
    w(" |   passages Infocom's own maps mark as narrow, reconciled against each\n")
    w(" |   story's exit graph under the rule that the drawing may resolve a passage\n")
    w(" |   only where every exit on it is conditional. See that script for the\n")
    w(" |   derivation and map_marks.h for what the runtime does with it.\n")
    w(" | Author: suinevere\n")
    w(" ----------------------*/\n")
    for t in tables:
        w("\n/*----------------------\n")
        w(f" | MAP_MARKS_{t['story']}\n")
        w(f" | Description: {t['story']}, release {t['release']} serial {t['serial']}.\n")
        w(f" |   {len(t['marks'])} marks, ascending by object number then direction. A\n")
        w(" |   non-zero dest is the drawing supplying a destination the compiled story\n")
        w(" |   does not carry; where the story carries one it stays zero.\n")
        if t["disagreements"]:
            w(" |\n |   Passages the drawing marks that the story or the drawing's own\n")
            w(" |   traces did not settle, each line saying what was done about it,\n")
            w(" |   because a scan that silently dropped or quietly downgraded one\n")
            w(" |   would be indistinguishable from a scan that saw nothing:\n")
            for line in t["disagreements"]:
                for i, chunk in enumerate(textwrap.wrap(line, 66)):
                    w(" |     %s%s\n" % ("" if i == 0 else "  ", chunk))
        w(" | Author: suinevere\n")
        w(" ----------------------*/\n")
        w(f"static const MapMark MAP_MARKS_{t['story']}[] = {{\n")
        for room, direction, dest, flags in t["marks"]:
            note = "%s %s" % (t["names"].get(room, ""), mapscan.DIRW[direction])
            if dest:
                note += " -> %s" % t["names"].get(dest, dest)
            w(f"    {{ {room:3d}, {direction:2d}, {dest:3d}, {flags} }},"
              f"   /* {note} */\n")
        w("};\n")
    w("\n/*----------------------\n")
    w(" | MAP_MARKS_STORIES / MAP_MARKS_STORY_N\n")
    w(" | Description: Every story with a marks table, keyed by the release and\n")
    w(" |   serial in its Z-machine header. Object numbers are assigned by the\n")
    w(" |   compiler, so a table is only valid for the exact build it was derived\n")
    w(" |   from and both fields must match before it is used.\n")
    w(" | Author: suinevere\n")
    w(" ----------------------*/\n")
    w("static const MapMarkStory MAP_MARKS_STORIES[] = {\n")
    for t in tables:
        w(f'    {{ {t["release"]}u, "{t["serial"]}", MAP_MARKS_{t["story"]},\n')
        w(f'      (unsigned short) (sizeof MAP_MARKS_{t["story"]} /\n')
        w(f'                        sizeof MAP_MARKS_{t["story"]}[0]) }},\n')
    w("};\n\n")
    w("#define MAP_MARKS_STORY_N \\\n")
    w("    ((int) (sizeof MAP_MARKS_STORIES / sizeof MAP_MARKS_STORIES[0]))\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", required=True,
                    help="directory for downloaded map PDFs; not part of the repo")
    ap.add_argument("--only", nargs="*", help="limit to these story stems")
    ap.add_argument("--audit", action="store_true",
                    help="seed the tracer at every box edge instead of at the"
                         " cross-bar clusters and write the whole-network"
                         " disagreement report to stdout, emitting no C")
    ap.add_argument("--report", action="store_true",
                    help="write the per-game report to stderr and emit nothing")
    args = ap.parse_args()

    os.makedirs(args.cache, exist_ok=True)
    tables = []
    for story in (args.only or sorted(MAPS)):
        if story not in MAPS:
            print(f"{story}: no map known", file=sys.stderr)
            continue
        if not os.path.exists(os.path.join(mapscan.Z3DIR, story + ".Z3")):
            print(f"{story}: not on the disc", file=sys.stderr)
            continue
        pdf = os.path.join(args.cache, story + ".pdf")
        if not os.path.exists(pdf):
            subprocess.run(["curl", "-sSL", "-o", pdf, BASE_URL + MAPS[story]],
                           check=True)
        if args.audit:
            audit(story, pdf, sys.stdout, _ITEMS)
            continue
        graph, release, serial = story_graph(
            os.path.join(mapscan.Z3DIR, story + ".Z3"))
        disagreements = []
        marks = marks_for(story, pdf, disagreements)
        if marks:
            tables.append({"story": story, "marks": marks, "release": release,
                           "serial": serial, "disagreements": disagreements,
                           "names": {o: r["name"] for o, r in graph.items()}})
            print(f"  OK   {story:9s} {len(marks):3d} marks, "
                  f"{len(disagreements)} noted", file=sys.stderr)
        else:
            print(f"  none {story:9s} no passage resolved, "
                  f"{len(disagreements)} noted", file=sys.stderr)
        for line in disagreements:
            print(f"         {line}", file=sys.stderr)

    if args.audit or args.report:
        return
    if not tables:
        print("nothing to emit", file=sys.stderr)
        sys.exit(1)
    emit(tables, sys.stdout)


if __name__ == "__main__":
    main()
