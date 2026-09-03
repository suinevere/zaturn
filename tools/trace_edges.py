#!/usr/bin/env python3
"""Following the passages Infocom drew between the room boxes, and reading the
symbol each one carries.

mapscan.py finds the boxes and names them. This finds the lines. The two are
separate because a box is an enclosed region and a passage is an open stroke,
and the morphology that isolates one destroys the other: the box finder opens
the ink with long one-dimensional kernels precisely to erase everything that is
not a rule, which is to say precisely to erase the passages.

Coordinates are page pixels at mapscan.DPI throughout.
"""
import cv2
import numpy as np

import mapscan

BOX_PAD = 3
SHAFT_MIN = 120


def ink_mask(pdf, page):
    """Passage ink on one page: 255 where a drawn line is, 0 elsewhere.

    The room boxes are painted out rather than merely ignored. A box rule is
    darker and straighter than any passage, so a walk that reached one would
    follow it around the box perimeter and emerge on the wrong side; erasing
    the rules makes a box a wall the walk stops at, which is what it is.

    The stonework the maps are printed over is mid-grey and the passages are
    black, so a plain threshold separates them -- unlike the boxes, whose
    interiors are darker than the background on some pages and lighter on
    others.

    SHAFT_MIN=120 was checked against a measurement, not guessed: on Zork I's
    underground page the grey channel's [1, 5, 25, 50, 75] percentiles are
    [44.0, 84.0, 194.0, 206.0, 207.0]. The background stonework sits at and
    above the 25th percentile; 120 falls in the gap between the 5th and 25th
    percentile, below every pixel of stonework and above the darkest sliver
    of the page, which is passage and box ink.
    """
    img = mapscan.page_image(pdf, page)
    gray = cv2.cvtColor(img, cv2.COLOR_RGB2GRAY)
    mask = (gray < SHAFT_MIN).astype(np.uint8) * 255
    for (x, y, w, h) in mapscan.find_boxes(pdf, page):
        x0 = max(0, x - BOX_PAD)
        y0 = max(0, y - BOX_PAD)
        mask[y0:y + h + BOX_PAD, x0:x + w + BOX_PAD] = 0
    return mask


STEP = 2
"""Pixels advanced per step. Do not retune without re-running the sweep below.

A sweep against the old, cycle-prone _walk found this constant's behaviour
non-monotonic against the Timber-Drafty mark: 4, 7, 8, and 9 reached the far
box; 3, 5, 6, and 10 did not, with no pattern relating the value to the
outcome. That was a symptom of the missing cycle guard and gap-jump-first
ordering, not a property STEP itself has to satisfy -- once that fix was in
place STEP=2 (this value) was never the thing that needed to change. Anyone
tempted to nudge it to fix a stuck walk elsewhere should re-run that kind of
sweep first rather than trust that a value which "worked" wasn't just a
coincidence of where the mark's own ink happened to fall.
"""
LOOK = 30
"""How far ahead a stuck walk is allowed to look, in page pixels at mapscan.DPI.

Bounded to roughly the width of the baggage-limit mark's own cluster, not
larger. Infocom's three-bar mark is about 26px wide at DPI=200 (three bars
plus the two gaps between them), so a walk stuck inside it needs to see just
past the far side to find the passage resuming. 30 clears that with a small
margin. A much larger value would let the gap-jump in _walk's step 2 leap
past an unrelated crossing line and mistake it for the passage's own
resumption -- the "everything strictly between is blank" check guards against
jumping over ink, but not against a second passage that happens to be blank
at every pixel this one's heading crosses, which gets less and less unlikely
the further the jump reaches. Measured on Zork I's underground page: with the
mark itself resolved by the two gap-jump steps, the point where the walk
still had to fall through to open-space or box lookahead never needed more
than a few pixels beyond STEP, so 30 is headroom, not a value hand-fit to
one failure.
"""
JUMP_MAX = 3
"""How many gap jumps a walk may take in a row before it must find real ink.

LOOK bounds one hop; nothing bounded N of them, so a walk could in principle
chain hop after hop through the blank between letterforms and arrive at a run
it never had a line to -- the risk the gap jump was always carrying and which
nothing in _walk expressed. The cap is what a real mark needs plus headroom: a
baggage-limit mark is three bars struck across the passage, and the walk lands
on each bar's own ink between the gaps, so crossing one costs alternating
jumps and straight steps rather than a chain at all. Instrumented over the
whole shipped scan of Zork I, the longest run of consecutive jumps observed
anywhere was 1. Three leaves room for a mark whose bars a scan has thinned to
nothing without leaving room for a walk to cross a page on whitespace. The
counter resets on every straight step, so a passage that crosses several marks
along its length is not charged for the ones it has already left behind.
"""


def _lit(mask, x, y):
    """Whether a small neighbourhood of a point holds ink, which tolerates the
    one-pixel raggedness a scan leaves on a printed rule."""
    h, w = mask.shape
    x0, y0 = max(0, x - 1), max(0, y - 1)
    return mask[y0:min(h, y + 2), x0:min(w, x + 2)].any()


def _box_at(boxes, x, y):
    """The index of the box whose padded rectangle contains a point, or None."""
    for i, (bx, by, bw, bh) in enumerate(boxes):
        if (bx - BOX_PAD - STEP <= x <= bx + bw + BOX_PAD + STEP and
                by - BOX_PAD - STEP <= y <= by + bh + BOX_PAD + STEP):
            return i
    return None


def _gap_clear(mask, x, y, dx, dy, k):
    """Whether every point strictly between the current position and a
    candidate k steps ahead along the heading is unlit.

    This is what tells a baggage-mark gap apart from a line crossing this
    one: a crossing leaves ink somewhere in between, a gap does not. Checked
    pixel by pixel rather than in STEP-sized strides because the gaps this
    exists for are only two or three pixels wide.

    Reads the mask directly rather than through _lit. _lit's neighbourhood
    dilation exists to tolerate raggedness on ink that is already known to be
    there; used here it does the opposite of what "blank" needs -- a pixel
    one or two columns short of real ink reads as lit anyway, so a genuine
    two-pixel gap never tests as clear at all. What must be judged blank here
    is the gap itself, not a stroke's edge.
    """
    h, w = mask.shape
    for i in range(1, k):
        px, py = x + dx * i, y + dy * i
        if 0 <= px < w and 0 <= py < h and mask[py, px]:
            return False
    return True


def _walk(mask, boxes, x, y, dx, dy):
    """One direction of a stroke, from a seed to wherever it ends.

    At each position: step along the heading if it is lit; otherwise look
    ahead along the same heading for resumed, unvisited ink with nothing but
    blank between -- a real gap, jumped without turning -- before trying
    either perpendicular; only once both those fail does it turn a corner.
    That order is forced by what a baggage-limit mark is: three bars struck
    across the passage, so at the mark there is lit ink perpendicular to the
    stroke on both sides of every gap. Trying perpendiculars before the gap
    jump walks onto a bar instead of over it, and turns straight back at the
    bar's other edge -- an infinite cycle a visited set alone cannot break,
    since each step in the cycle is a position never accepted before it
    loops. The gap jump has to run first so the walk never takes that first
    wrong turn.

    A turn may run at most once before the walk next makes straight or
    gap-jumped progress. Studio's chimney stub is drawn as a hand-inked
    diagonal squiggle, not a straight rule, and its genuine tip -- the ink
    just stops -- sits a couple of pixels to one side of where a due-heading
    walk arrives. The first turn there is correct: it follows the squiggle's
    own curl onto its last few lit pixels. But nothing at the tip continues
    in any direction except back the way the squiggle came, so the very next
    step has to turn again -- and turning twice with no straight step between
    lands the walk back inside the stroke it just climbed, offset by exactly
    STEP, which dodges the exact-point visited set and retraces the whole
    passage into the room it started from. A single turn negotiates one real
    corner, which is the L-shaped Timber-to-Drafty route this function was
    built against; a second turn immediately after, with no forward step to
    show a new corridor was actually found, means the walk is boxed into a
    dead end rather than turning a corner, and the honest answer is the open
    end it already stands on, not a walk back into the ink already covered.
    """
    h, w = mask.shape
    pts = [(x, y)]
    visited = {(x, y)}
    just_turned = False
    jumps = 0

    def try_step(cx, cy):
        if not (0 <= cx < w and 0 <= cy < h):
            return False
        if (cx, cy) in visited:
            return False
        return _lit(mask, cx, cy)

    while True:
        moved = None
        turned = False
        jumped = False

        sx, sy = x + dx * STEP, y + dy * STEP
        if try_step(sx, sy):
            moved = (sx, sy, dx, dy)

        if moved is None and jumps < JUMP_MAX:
            for k in range(STEP + 1, LOOK + 1):
                jx, jy = x + dx * k, y + dy * k
                if not (0 <= jx < w and 0 <= jy < h):
                    break
                if (jx, jy) in visited or not mask[jy, jx]:
                    continue
                if _gap_clear(mask, x, y, dx, dy, k):
                    moved = (jx, jy, dx, dy)
                    jumped = True
                    break

        if moved is None and not just_turned:
            for cdx, cdy in ((-dy, dx), (dy, -dx)):
                px, py = x + cdx * STEP, y + cdy * STEP
                if try_step(px, py):
                    moved = (px, py, cdx, cdy)
                    turned = True
                    break

        if moved is None:
            for k in range(STEP, LOOK + 1):
                px, py = x + dx * k, y + dy * k
                if not (0 <= px < w and 0 <= py < h):
                    break
                hit = _box_at(boxes, px, py)
                if hit is not None:
                    return pts, ("box", hit)
            return pts, ("open", (x, y))

        x, y, dx, dy = moved
        just_turned = turned
        jumps = jumps + 1 if jumped else 0
        pts.append((x, y))
        visited.add((x, y))
        hit = _box_at(boxes, x, y)
        if hit is not None:
            return pts, ("box", hit)
        if len(pts) > 4000:
            return pts, ("open", (x, y))


def follow(mask, boxes, seed, heading):
    """The whole stroke a seed sits on, walked out in both directions.

    Returns its points in order and the two things it ends at, each either a
    room box or an open point -- an open end is a labelled stub, which is how
    Infocom draws a passage whose far end is on another page.
    """
    dx, dy = heading
    fwd, end_a = _walk(mask, boxes, seed[0], seed[1], dx, dy)
    back, end_b = _walk(mask, boxes, seed[0], seed[1], -dx, -dy)
    pts = list(reversed(back)) + fwd[1:]
    return {"points": pts, "ends": [end_b, end_a]}


HEAD_WIDEN = 3
"""Minimum extra ink width, in pixels, an end must carry over the run's own
baseline width before it is called an arrowhead.

Not a fixed idea of shaft width -- the shaft varies between pages, which is
why this measures each end against a baseline drawn from the run's own
interior rather than against a constant. HEAD_WIDEN only has to clear the
few pixels of scan jitter a shaft's width carries along its own length; 3
does that without demanding the wide, unmistakable flare of a real
arrowhead.
"""
LABEL_NEAR = 180
"""The radius, in page pixels, passed to mapscan.label_near: how far around
a stub's open end it crops and OCRs, and (as a taxicab bound) how far a
candidate fragment inside that crop may sit from the open point and still
be taken as the label for this stub rather than some other one.

Measured directly: Studio's chimney stub ends at (724, 2523) and the OCR
engine reads its own caption, isolated, as one fragment with bounding box
(733, 2507)-(843, 2529) -- a taxicab distance of 69px from the open end.
180 clears that with better than 2x headroom. Checked against this page's
own crowding rather than assumed: a crop of this radius around that open
end contains two neighbouring rooms' bare names (no parenthesis, so already
excluded by the want= filter open_end_room passes) and exactly one
parenthetical fragment, "(to Kitchen)" itself -- nothing else on the page
carries a "(" close enough to be mistaken for this stub's own caption at
this radius.
"""


WIDTH_SCAN_RADIUS = 6
"""Pixels scanned to each side of a point, perpendicular to the run, when
measuring its ink width -- so width() counts lit pixels across a window
2*WIDTH_SCAN_RADIUS+1 = 13 wide.

Measured directly on Zork I's underground page, not inherited from the
brief that supplied this figure unverified: a clean, mark-free stretch of
the Timber-to-Drafty rule (four points sampled clear of both room boxes and
the baggage cluster) reads 5, 5, 6, 6px thick by direct cross-section scan
of the mask -- matching BAR_MIN's own independent measurement of "the
rule's own ~6px thickness" above. The Studio chimney's arrowhead, the one
real head in this corpus, peaks at 10-11px by the same direct scan. A
window has to be at least as wide as the feature it is meant to measure
without clipping it, or a real head's width reads as an artifact of the
window's own ceiling rather than the ink; 13 clears the measured 10-11px
peak by 2-3px of margin while a window as small as 9 (radius 4) would clip
that same peak down to a 3px margin over the 6px baseline -- exactly
HEAD_WIDEN's threshold, with no room for scan jitter. 13 was chosen for
that headroom, not because a smaller value happened to fail this one test.
"""


HEAD_RUN = 3
"""Consecutive points an end's ink must stay wide before it is an arrowhead.

This is the shape test that tells a head from the two things measured to sit
at a run's end and read every bit as wide as one. Infocom's baggage-limit mark
breaks the shaft between its bars, so a cluster's width profile alternates --
13, 13, 0, 13, 13, 0, 13, 13 where the Altar-to-Cave run crosses one, at
DPI=200 -- and never holds more than two points together. A box's own border
rule survives ink_mask's erasure by a row or two on some boxes (Studio's, on
the underground page, sits one row outside the erased band) and reads as a
single wide point at the very end, a block of one. A drawn head is solid ink:
the chimney's reads 10, 10, 11 and Altar-to-Cave's 9, 10, 10, both three
unbroken points. 3 is the smallest block length that separates all three, and
raising it to 4 would lose the chimney, whose whole wedge is three points
wide.

So this constant is pinned from both sides at once, with no margin in either
direction, and the margin on HEAD_WIDEN is no better: of the four heads read
on Zork I, the fourth clears the shaft by exactly HEAD_WIDEN-1 and is already
missed. What a second corpus tests is not whether the shape test is the right
idea -- a solid wedge really is a different shape from a broken cluster -- but
whether these two numbers have any room in them at all, and the honest answer
today is that they have none. A head that slips through does not leave a mark
missing: arrow_end returns 0, the caller reads the passage as two-way, and it
annotates both directions instead of retracting one. That is the failure this
was built to fail toward, since a missing retraction leaves an exit the player
can still see and a spurious one takes an exit away.
"""


def _head_peak(widths, baseline, look):
    """The widest point of an arrowhead within look points of a profile's
    start, or 0 if there is none.

    A head is HEAD_RUN consecutive points at least HEAD_WIDEN wider than the
    shaft, none of them saturating the measuring window. Saturation is a
    second, independent reason to reject a mark cluster: a bar fills all
    2*WIDTH_SCAN_RADIUS+1 pixels of the window while the three heads measured
    on Zork I peak at 10, 10 and 11 against a window of 13, so a reading at
    the ceiling is one where the window, not the ink, decided the answer.
    Without it the Altar-to-Cave head would be judged against a cluster that
    lands one point outside look on the trace that finds it and one point
    inside on another, which is a margin too thin to rest on.

    The whole block, not just its first point, stays inside look. Blocks are
    what is being searched for, so a loop that started one anywhere in the
    first look points would read up to HEAD_RUN-1 points past them and into
    the very interior the baseline is taken from, which is what arrow_end's
    disjointness argument forbids. Capping the start instead keeps the search
    window exactly the widths[:look] arrow_end compares against its own
    interior.
    """
    best = 0
    for i in range(max(0, look - HEAD_RUN + 1)):
        block = widths[i:i + HEAD_RUN]
        if len(block) < HEAD_RUN:
            break
        if min(block) - baseline >= HEAD_WIDEN and max(block) < 2 * WIDTH_SCAN_RADIUS + 1:
            best = max(best, max(block))
    return best


def arrow_end(mask, run):
    """Which end of a run carries an arrowhead: 0 none, 1 ends[1], 2 ends[0].

    0 covers two different readings, and a caller deciding a passage is
    two-way must accept both: no end carried a head, or both did. A drawn
    double head is a real thing and means the passage runs both ways, so the
    two collapse to the same answer here rather than being told apart; what
    they have in common is that neither licenses withdrawing a direction.

    A head is simply ink wider than the shaft. Measuring width perpendicular
    to the run at each end and comparing against a shaft baseline avoids
    having to know how wide the shaft is on a given scan, which varies
    between pages.

    Two things a single sampled point cannot survive, both measured on the
    Studio chimney: an arrowhead is a wedge, not a step, so its width peaks
    a few points back from the literal tip rather than at it -- sampling
    two points in from the end (as a first attempt at this function did)
    lands on the wedge's narrow point and misses the widening entirely,
    where the chimney's own width profile reads 4 at that point against 11
    at the true peak four points further in. And a run's middle is not a
    safe stand-in for its baseline width when a baggage-limit mark or a
    box's own imperfectly-erased border rule can sit near either end: on
    this run the single sampled midpoint lands inside exactly such a
    widened patch (13, more than double the shaft's own width), which
    would have hidden a real head at either end behind an inflated
    baseline. Scanning a window near each end and taking its max survives
    the first; taking the median of the interior, unwindowed points as the
    baseline survives the second, since a handful of contaminated samples
    cannot move a median where they would swing a mean or a single point.

    The end window is bounded by CLUSTER_SPAN, already the file's own
    measured ceiling on how many pixels a baggage-limit cluster's three
    bars can span -- converting that to points via STEP keeps the search
    for a wedge from reaching so far in that it finds the run's ordinary
    middle instead. On a run too short for that span to leave a non-empty
    interior on both sides, the span is shrunk to a third of the run's own
    length instead of silently falling back to measuring the baseline over
    the whole run (points, plural, an earlier version of this function did
    exactly that, which let the interior baseline reabsorb the very
    end-window points it exists to keep separate) -- three equal thirds are
    disjoint by construction for any run at least this function's own
    minimum length, so the two end windows can never leak into the baseline
    they are being compared against, however short the run.

    What a window's maximum width cannot survive is that the two things
    which contaminate an end are exactly as wide as a head, so taking the
    widest point in each window and comparing the two answers whichever
    contaminant is worse rather than which end is drawn with a head. Both
    were measured on Zork I's underground page, and both make that reading
    wrong: seeded from its own mark cluster rather than from Studio's box
    edge, the chimney's Studio end reads 13 against the arrowhead's 11 and
    the head is reported at the wrong end of the passage; on Altar-to-Cave,
    whose head is drawn at the Cave box, a mark cluster sits within the
    Altar end's window and reads 13 against the head's 10, again reversing
    the answer. _head_peak replaces the maximum with a shape test, which is
    what actually separates the three -- see HEAD_RUN.

    Read against four drawn heads rather than the one the previous
    window-and-maximum version had: the chimney from both of its ends (the
    stub out of Studio on the underground page and the stub into the
    Kitchen on the above-ground one, which between them are the only case
    in this corpus where the same passage is drawn twice and the two
    readings have to agree), and Altar-to-Cave, whose route the mark
    repeats along four times and which two separate seeds trace. The fourth
    of those Altar-to-Cave traces still reports no head at all: its own
    path through the wedge reads 8, 9 where the other reads 9, 10, 10, so
    the head falls a pixel short of HEAD_WIDEN.

    That miss is the shape of this function's error, and a caller
    reconciling several traces of one passage has to choose which way to
    resolve a split. Believing a head found by any one trace maximises
    recall and is the wrong choice where the answer withdraws a direction
    from the reader, because it lets a single misread end delete a real
    exit; gen_map_marks therefore believes a head only when every trace of
    the passage agrees, and treats a split as no head. That is a caller's
    policy rather than this function's, and a caller doing something
    cheaper with the answer -- an audit listing, say -- is entitled to the
    opposite one.
    """
    pts = run["points"]
    if len(pts) < 6:
        return 0

    def width(i):
        x, y = pts[i]
        dx = pts[min(len(pts) - 1, i + 1)][0] - pts[max(0, i - 1)][0]
        h, w = mask.shape
        n = 0
        for k in range(-WIDTH_SCAN_RADIUS, WIDTH_SCAN_RADIUS + 1):
            px, py = (x + k, y) if dx == 0 else (x, y + k)
            if 0 <= px < w and 0 <= py < h and mask[py, px]:
                n += 1
        return n

    widths = [width(i) for i in range(len(pts))]
    span = min(CLUSTER_SPAN // STEP, len(pts) // 3)
    interior = sorted(widths[span:-span])
    baseline = interior[len(interior) // 2]
    a = _head_peak(widths, baseline, span)
    b = _head_peak(widths[::-1], baseline, span)
    if a and b:
        return 0
    if b:
        return 1
    if a:
        return 2
    return 0


def open_end_room(pdf, page, run, names):
    """The story room a stub's parenthetical label names, or None.

    Infocom draws a passage whose far end is on another page as a stub with
    a label -- "(to Kitchen)" beside Studio, "(from Studio)" beside the
    Kitchen. Reading whatever text sits on the page near the stub's open end
    is mapscan.label_near's job (it crops and OCRs rather than going through
    mapscan.read_labels, whose page-wide merge fuses this exact label into a
    blob spanning most of the page on this map -- see label_near's own
    docstring for the measured evidence). What is left here is interpreting
    that text as a passage: picking out the parenthetical, stripping the
    "to "/"from " Infocom always prefixes it with, and matching what remains
    against the story's own closed set of room names with the same
    nearest-match mapscan uses for the boxes, so a garbled reading fails to
    match rather than matching confidently and wrongly.
    """
    opens = [e[1] for e in run["ends"] if e[0] == "open"]
    if not opens:
        return None
    ox, oy = opens[0]
    pick = mapscan.label_near(pdf, page, ox, oy, LABEL_NEAR, want=lambda t: "(" in t)
    if pick is None:
        return None
    inner = pick[pick.find("(") + 1:]
    inner = inner[:inner.find(")")] if ")" in inner else inner
    inner = mapscan.norm(inner)
    for lead in ("to ", "from "):
        if inner.startswith(lead):
            inner = inner[len(lead):]
    by_norm = {mapscan.norm(n): n for n in names}
    hit = mapscan.match_name(inner, list(by_norm))
    return by_norm.get(hit) if hit else None


BAR_MIN, BAR_MAX = 12, 19
"""Length range, in mask pixels, of one bar of Infocom's baggage-limit mark,
measured along the run it crosses (its "long side").

The starting estimate here was 5-22, on the assumption that a cross-bar
prints about as thin as the rule it crosses. It does not: on Zork I's
underground page (DPI=200), opening the ink with a 1x5 (or 5x1) kernel and
asking connectedComponentsWithStats for each survivor's long side pulled the
Timber-Drafty and Altar-Cave rule itself into the same blob as the bars,
because the rule's own ~6px thickness clears BAR_MIN=5 and the two fuse into
one shape wide enough to fail BAR_MAX outright -- the mark vanished, not
just mis-measured. Opening at 1x12 breaks that fusion (a 6px-thick rule does
not survive a 12-tall opening) while every genuine bar -- measured 16, 17,
17 on Timber-Drafty and 12, 17, 17, 17 on Altar-Cave -- still does. BAR_MAX=19
sits just above the largest of those with a few pixels of margin, still well
short of the hatched wall and mirror survivors, which ran 24px and up in the
same measurement once separated from the rule fusion problem.

BAR_MIN has no such margin: one of the four measured Altar-Cave bars was
12px, exactly the floor, so this bound carries zero slack in either
direction, unlike BAR_MAX. A real bar one pixel shorter on some other page
in the corpus is dropped by this check alone, before GAP_MIN or ALIGN_TOL
ever run.
"""
BAR_THICK_MAX = 7
"""Max width of a bar across its own stroke (its "short side"), in pixels.

Not one of the four constants the task brief named, but needed for the same
reason BAR_MIN had to move: the brief's own literal code rejected any bar
thicker than 3px, on the same too-thin assumption BAR_MIN's docstring
corrects. Measured cross-bar thickness on Timber-Drafty was 4, 4, 5px and on
Altar-Cave 3-6px -- a printed stroke at DPI=200, not a hairline. 7 admits
every measured bar with a pixel of margin.
"""
CLUSTER_SPAN = 18
"""Max centre-to-centre pixels from the first to the third bar of a cluster.

The starting estimate of 26 was read off the tiny legend glyph on page 3,
which renders far more compactly than the mark actually prints on the
underground page. Measured directly against the drawn passages: the
Timber-Drafty cluster spans 13.9px centre-to-centre, and two of the four
Altar-Cave repeats span 12.5 and 13.9px. 18 clears the largest of those with
room for scan jitter while staying well under twice that span, which matters
because CLUSTER_SPAN alone (without GAP_MIN below) is what rejects a false
trio built from bars belonging to two different, unrelated repeats of the
mark along the same busy run.
"""
GAP_MIN = 4
"""Min pixels between the centres of two consecutive bars in a cluster.

Not one of the brief's four named constants either. CLUSTER_SPAN bounds the
total width of a trio but not how the three are spaced inside it, and this
page carries enough short, similarly-sized ink -- letterforms, dashes,
hatching fragments -- that plenty of accidental collinear trios satisfy
BAR_MIN/BAR_MAX/BAR_THICK_MAX/CLUSTER_SPAN by coincidence: an unrelated
component sitting close beside two real bars of a repeat, for instance, or
three fragments of an entirely different mark. A genuine cluster's own two
gaps measured 6.9 and 6.9px on Timber-Drafty and 6.0 and 6.5px on
Altar-Cave; GAP_MIN=4 is comfortably below every genuine gap measured and
above zero, so it costs nothing against real marks while cutting the
measured false-seed count on this page from 29 to 13 with CLUSTER_SPAN and
ALIGN_TOL otherwise unchanged.
"""
ALIGN_TOL = 2
"""Max pixels a cluster's three bar centres may drift off the line running
across the axis (rows for a horizontal run, columns for a vertical one).

The brief's literal code used 3. Measured against the real clusters this
page carries, tightening to 2 loses none of them -- Timber-Drafty's three
bars sit within 0.3px of each other's row, Altar-Cave's within 0.5px -- and
further trims the page's accidental-trio count.
"""
FILL_MAX = 0.99
"""Max filled fraction of a bar's own bounding box, guarding against a
perfectly solid (fill=1.0) survivor no other check happens to catch.

The brief's starting value of 0.55 assumed a cross-bar prints as a near-1px
rule with little of its bounding box actually inked, so a fill near 1.0
would mark it as hatching instead. Measured fill on this page's genuine
bars was 0.88, 0.97 and 0.97 -- as solid as the hatching it is meant to
reject, because BAR_THICK_MAX's correction (bars print 3-7px thick, not 1px)
means a short, thick stroke's bounding box is inherently almost entirely
ink. FILL_MAX can no longer do the discriminating work the brief assigned
it -- that now falls to BAR_MAX, BAR_THICK_MAX and GAP_MIN, which measurably
separate the mark from the Temple's granite wall and the mirrors without
relying on fill. 0.99 keeps the check as a named ceiling against a fill=1.0
anomaly the length and thickness bounds happen not to exclude, without
rejecting any bar actually measured on this page.
"""


def _bars(mask, axis):
    """Short strokes perpendicular to one axis: (centre x, centre y, length).

    A bar crossing a horizontal run is itself vertical, so the horizontal case
    opens the ink with a vertical kernel and vice versa.
    """
    k = (1, BAR_MIN) if axis == "h" else (BAR_MIN, 1)
    opened = cv2.morphologyEx(mask, cv2.MORPH_OPEN,
                              cv2.getStructuringElement(cv2.MORPH_RECT, k))
    n, _lbl, stats, cent = cv2.connectedComponentsWithStats(opened, 8)
    out = []
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        long_side, short_side = (h, w) if axis == "h" else (w, h)
        if not (BAR_MIN <= long_side <= BAR_MAX):
            continue
        if short_side > BAR_THICK_MAX:
            continue
        if area > FILL_MAX * w * h and short_side > 1:
            continue
        out.append((int(cent[i][0]), int(cent[i][1]), long_side))
    return out


def hamburger_seeds(mask):
    """Where Infocom's narrow-passageway mark sits: [(x, y, axis)].

    The mark is three short bars struck through a passage, and Infocom
    repeats it along a whole run rather than marking a single point (the
    legend's own sample line carries two, the Altar-Cave route four) -- so a
    run carries several and each lands here. Callers follow the stroke a
    seed sits on and treat the run, not the seed, as the passage.

    Three collinear, evenly-spaced bars within CLUSTER_SPAN make a cluster.
    Requiring three is what rejects the pair of ticks Infocom uses elsewhere;
    requiring them evenly spaced (GAP_MIN, checked against both gaps rather
    than only the total span) is what rejects the accidental trios this
    page's ordinary letterforms and dashes otherwise supply in quantity once
    BAR_THICK_MAX is loosened enough to admit the mark's own, non-hairline
    bars. The trio's along-axis positions are sorted before either is
    measured: the bars are ordered cross-axis first, so ALIGN_TOL's allowance
    for drift -- and int() truncation of a centre that straddles a row -- can
    hand the window three bars of one cluster out of order, which would give a
    negative gap and a span shorter than the cluster really is.

    This is the whole of what separates a genuine mark from the Temple's
    granite west wall and the mirrors on this page, and it is a coincidence
    of what this page happens to contain, not a guarantee. Real bars turned
    out thick (3-7px) and near-solid (fill 0.88-0.97) rather than the
    one-pixel, mostly-empty stroke the brief assumed, so once BAR_THICK_MAX
    and FILL_MAX are loosened to admit them, neither constant separates a
    bar from hatching by shape any more -- FILL_MAX's own docstring says so.
    What is left doing the rejecting is that this page's wall and mirror
    hatching happen to survive the length/thickness window only as isolated
    single or double candidates, never as three same-length, evenly-spaced,
    collinear ones. Nothing in this algorithm forbids hatching from forming
    such a trio; it simply does not happen here. A page elsewhere in the
    corpus where hatching, a repeated dash pattern, or dense text does line
    up into three evenly-spaced bars of matching length will produce false
    seeds from this function, undetected by any check above.

    Even on this page the gates are not exhaustive: of the 13 seeds measured
    on Zork I's underground page, only 4-5 resolve cleanly to a real room
    pair on both ends when followed -- the other 8-9 resolve to nothing
    useful (an open end, or the same box on both ends) despite passing every
    check here. Callers should treat a seed as a lead to verify by following
    it, not as a placement already confirmed.
    """
    seeds = []
    for axis in ("h", "v"):
        bars = sorted(_bars(mask, axis),
                      key=lambda b: (b[1], b[0]) if axis == "h" else (b[0], b[1]))
        for i in range(len(bars) - 2):
            trio = bars[i:i + 3]
            if axis == "h":
                if max(abs(b[1] - trio[0][1]) for b in trio) > ALIGN_TOL:
                    continue
                pos = [b[0] for b in trio]
            else:
                if max(abs(b[0] - trio[0][0]) for b in trio) > ALIGN_TOL:
                    continue
                pos = [b[1] for b in trio]
            pos = sorted(pos)
            span = pos[2] - pos[0]
            if not (0 < span <= CLUSTER_SPAN):
                continue
            if pos[1] - pos[0] < GAP_MIN or pos[2] - pos[1] < GAP_MIN:
                continue
            seeds.append((trio[1][0], trio[1][1], axis))
    return seeds
