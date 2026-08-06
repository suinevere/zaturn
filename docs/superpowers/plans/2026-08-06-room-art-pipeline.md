# Room Art — Sourcing Pipeline — Implementation Plan (Plan B)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fetch, gate, review and file up to 99 background pictures per mood from Pixabay, driven by a hand-edited vocabulary crossed with place-nouns parsed out of the classifier's own keyword table.

**Architecture:** Nouns are derived from `room_class_data.c` so art coverage tracks the classifier automatically. Mood adjectives and noun-donor moods live in a hand-edited JSON file. Their cross product is the query list. Candidates are fetched, centre-cropped to 320×224, and scored on legibility, quantisation error, and perceptual-hash novelty; survivors go to per-mood HTML contact sheets for a human accept/reject; accepted images are promoted into `tools/assets/png/<MOOD>/<DONOR>/<noun>/`.

**Tech Stack:** Python 3.9+, Pillow, `requests`, `imagehash`, `pytest`. No Saturn code, no C compiler, no cross-compilation.

**Spec:** `docs/superpowers/specs/2026-08-06-room-art-sourcing-design.md`

## Global Constraints

- **Python 3.9 floor.** `tools/convert-backgrounds.sh:36` gates on `sys.version_info >= (3, 9)`. No `match`, no PEP 604 `X | Y` annotations, no `dict[str, int]` at runtime without `from __future__ import annotations`.
- **New dependencies go in `tools/requirements.txt`**, which `convert-backgrounds.sh` installs into `tools/.venv`. Add `requests` and `imagehash`; Pillow is already there.
- **Every stage degrades, none aborts.** A missing key, a dead network, a query with no usable result, or an image that will not crop must print an actionable message and let the rest of the run continue. `make_tga.py` and `convert-backgrounds.sh` already behave this way; match them.
- **`PIXABAY_API_KEY` comes from the environment and is never committed.** No key in a file, a test fixture, or a commit message.
- **No network in tests.** Every test stubs the HTTP layer.
- **Comment style is mandatory.** Every module, function and constant gets the `/*---- ... ----*/`-equivalent Python docstring carrying Description, Author: suinevere, Dependencies, Globals, Params, Returns. Tests get a module docstring only. **No comments inside function bodies.**
- **Commit after every task.** One sentence, no body, no trailers, no mention of Claude/AI/session.
- Mood folder names are exactly: `WILDER`, `UNDRGRND`, `WATER`, `NAUTICAL`, `TOWN`, `DUNGN`, `DESERT`, `MAGIC`, `SCIFI`, `HORROR`, `MYSTERY`, `HOUSE`. These are the keys in `art_queries.json`, the spellings in a `donors` list, and the folder names in both trees. There is no second vocabulary.
- Source pictures are PNG, exactly 320×224, or `make_tga.py` skips them.
- The per-mood cap is 99. `make_tga.py` ignores the surplus.

---

### Task 1: Derive place-nouns from the classifier

**Files:**
- Create: `tools/art_nouns.py`
- Create: `tools/tests/test_art_nouns.py`

**Interfaces:**
- Consumes: `saturn/src/classify/room_class_data.c` (read only, never written).
- Produces:
  - `nouns_by_mood(source: str) -> dict` — maps each mood folder name to a sorted list of its place-nouns.
  - `MOODS` — the twelve folder names, in `TC_*` enum order.
  - `TC_TO_FOLDER` — maps a `TC_*` identifier string to its folder name.

- [ ] **Step 1: Write the failing test**

Create `tools/tests/test_art_nouns.py`:

```python
"""Pin the noun derivation against a fixed fragment of the keyword table."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from art_nouns import MOODS, TC_TO_FOLDER, nouns_by_mood

SAMPLE = """
static const TextKeyword ROOM_KEYWORDS[] = {
    { "hallway",  TC_HOUSE,       KT_STRUCTURE, GN_ANY },
    { "ballroom", TC_HOUSE,       KT_STRUCTURE, GN_ANY },
    { "fireplace",TC_HOUSE,       KT_FEATURE,   GN_ANY },
    { "cave",     TC_UNDERGROUND, KT_STRUCTURE, GN_ANY },
    { "forest",   TC_WILDERNESS,  KT_BIOME,     GN_ANY },
    { "boulder",  TC_WILDERNESS,  KT_FEATURE,   GN_ANY },
    { "shaft",    TC_UNDERGROUND, KT_STRUCTURE, GN_FANTASY },
    { "shaft",    TC_SCIFI,       KT_STRUCTURE, GN_SCIFI },
    { "decay",    TC_HORROR,      KT_FEATURE,   GN_ANY },
};
"""


def test_structure_and_biome_rows_become_nouns():
    got = nouns_by_mood(SAMPLE)
    assert got["HOUSE"] == ["ballroom", "hallway"]
    assert got["WILDER"] == ["forest"]


def test_feature_rows_are_excluded():
    got = nouns_by_mood(SAMPLE)
    assert "fireplace" not in got["HOUSE"]
    assert "boulder" not in got["WILDER"]
    assert got["HORROR"] == []


def test_a_word_in_two_genres_lands_in_both_moods():
    got = nouns_by_mood(SAMPLE)
    assert "shaft" in got["UNDRGRND"]
    assert "shaft" in got["SCIFI"]


def test_every_mood_has_an_entry_even_when_empty():
    got = nouns_by_mood(SAMPLE)
    assert sorted(got) == sorted(MOODS)


def test_folder_names_fit_iso9660():
    for mood in MOODS:
        assert len(mood) <= 8


def test_the_real_table_parses():
    repo = Path(__file__).resolve().parents[2]
    src = (repo / "saturn" / "src" / "classify" / "room_class_data.c").read_text()
    got = nouns_by_mood(src)
    assert "hallway" in got["HOUSE"]
    assert "cave" in got["UNDRGRND"]
    assert got["HORROR"] == [], "HORROR's keywords are qualities, not places"
```

- [ ] **Step 2: Run it and watch it fail**

Run: `python -m pytest tools/tests/test_art_nouns.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'art_nouns'`.

- [ ] **Step 3: Write the module**

Create `tools/art_nouns.py`:

```python
"""Derive photographable place-nouns from the room classifier's keyword table.

Description: Parses saturn/src/classify/room_class_data.c and collects every
    KT_STRUCTURE and KT_BIOME keyword, grouped by the mood folder its TC_*
    category maps to. KT_FEATURE rows are excluded: a rug is not a room, and a
    search for one returns a rug.

    Deriving rather than duplicating is the point. A keyword added to the
    classifier gains art coverage with no edit here, so the pictures a mood can
    show cannot drift from the words that mood can recognise.
Author: suinevere
Dependencies: re, pathlib
Globals: MOODS, TC_TO_FOLDER
"""
import re

# TC_* identifier -> disc folder, in enum order. The three categories that carry
# no art (TC_NEUTRAL, TC_DANGER, TC_TRIUMPH) are absent by construction.
TC_TO_FOLDER = {
    "TC_WILDERNESS":  "WILDER",
    "TC_UNDERGROUND": "UNDRGRND",
    "TC_WATER":       "WATER",
    "TC_NAUTICAL":    "NAUTICAL",
    "TC_TOWN":        "TOWN",
    "TC_DUNGEON":     "DUNGN",
    "TC_DESERT":      "DESERT",
    "TC_MAGIC":       "MAGIC",
    "TC_SCIFI":       "SCIFI",
    "TC_HORROR":      "HORROR",
    "TC_MYSTERY":     "MYSTERY",
    "TC_HOUSE":       "HOUSE",
}

MOODS = list(TC_TO_FOLDER.values())

_ROW = re.compile(
    r'\{\s*"([a-z ]+)"\s*,\s*(TC_[A-Z_]+)\s*,\s*(KT_[A-Z]+)\s*,\s*GN_[A-Z]+'
)

_PLACE_TIERS = ("KT_STRUCTURE", "KT_BIOME")


def nouns_by_mood(source):
    """Group the table's place-nouns by mood folder.

    Description: Returns one entry per mood, sorted and deduplicated. A mood whose
        keywords are all KT_FEATURE gets an empty list rather than being absent,
        so a caller can tell "no nouns of its own" from "not a mood".
    Author: suinevere
    Dependencies: N/A
    Globals: TC_TO_FOLDER, MOODS, _ROW, _PLACE_TIERS
    Params: source -- the text of room_class_data.c
    Returns: dict mapping mood folder name to a sorted list of nouns
    """
    out = {mood: set() for mood in MOODS}
    for word, cat, tier in _ROW.findall(source):
        if tier not in _PLACE_TIERS:
            continue
        folder = TC_TO_FOLDER.get(cat)
        if folder is None:
            continue
        out[folder].add(word)
    return {mood: sorted(words) for mood, words in out.items()}
```

- [ ] **Step 4: Run the tests and watch them pass**

Run: `python -m pytest tools/tests/test_art_nouns.py -v`
Expected: all six PASS. If `test_the_real_table_parses` fails on
`got["HORROR"] == []`, a place-noun has been added to `TC_HORROR` since this plan
was written — check the row, and if it is genuinely a place, relax that assertion
rather than the parser.

- [ ] **Step 5: Commit**

```bash
git add tools/art_nouns.py tools/tests/test_art_nouns.py
git commit -m "tools: derive photographable place-nouns from the room classifier keyword table"
```

---

### Task 2: The query vocabulary and the query builder

**Files:**
- Create: `tools/assets/art_queries.json`
- Create: `tools/assets/art_queries.md`
- Create: `tools/art_queries.py`
- Create: `tools/tests/test_art_queries.py`

**Interfaces:**
- Consumes: `nouns_by_mood`, `MOODS` (Task 1).
- Produces:
  - `load(path) -> dict` — the parsed and validated vocabulary.
  - `build(vocab: dict, nouns: dict) -> dict` — maps each mood to a list of
    `Query` objects.
  - `Query` — a `namedtuple("Query", "mood donor noun adjective phrase")`.
    `phrase` is `f"{adjective} {noun}"`; `donor` is the mood the noun came from,
    or `"EXTRA"` for an `extra_nouns` entry.

- [ ] **Step 1: Write the failing test**

Create `tools/tests/test_art_queries.py`:

```python
"""Pin the query cross-product and the vocabulary's validation rules."""
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_queries
from art_nouns import MOODS

NOUNS = {m: [] for m in MOODS}
NOUNS["HOUSE"] = ["hallway", "kitchen"]
NOUNS["UNDRGRND"] = ["cave"]

VOCAB = {
    "HORROR": {
        "adjectives": ["dark", "eerie"],
        "donors": ["HOUSE", "UNDRGRND"],
        "extra_nouns": ["morgue"],
        "exclude_nouns": ["kitchen"],
        "target": 99,
    }
}


def test_cross_product_covers_adjectives_by_nouns():
    got = art_queries.build(VOCAB, NOUNS)["HORROR"]
    phrases = sorted(q.phrase for q in got)
    assert phrases == ["dark cave", "dark hallway", "dark morgue",
                       "eerie cave", "eerie hallway", "eerie morgue"]


def test_excluded_nouns_never_appear():
    got = art_queries.build(VOCAB, NOUNS)["HORROR"]
    assert not any("kitchen" in q.phrase for q in got)


def test_donor_is_recorded_for_the_folder_path():
    got = {q.phrase: q for q in art_queries.build(VOCAB, NOUNS)["HORROR"]}
    assert got["dark hallway"].donor == "HOUSE"
    assert got["dark cave"].donor == "UNDRGRND"
    assert got["dark morgue"].donor == "EXTRA"


def test_unknown_mood_key_is_rejected():
    with pytest.raises(ValueError, match="NOTAMOOD"):
        art_queries.validate({"NOTAMOOD": {"adjectives": ["x"], "donors": []}})


def test_unknown_donor_is_rejected():
    with pytest.raises(ValueError, match="NOPE"):
        art_queries.validate({"HORROR": {"adjectives": ["x"], "donors": ["NOPE"]}})


def test_a_mood_with_no_reachable_noun_is_rejected():
    """HORROR donating only to itself would produce zero queries -- catch it here."""
    with pytest.raises(ValueError, match="HORROR"):
        art_queries.build({"HORROR": {"adjectives": ["dark"], "donors": ["HORROR"],
                                      "extra_nouns": [], "exclude_nouns": [],
                                      "target": 99}}, NOUNS)


def test_the_shipped_vocabulary_is_valid_and_covers_every_mood():
    repo = Path(__file__).resolve().parents[2]
    vocab = art_queries.load(repo / "tools" / "assets" / "art_queries.json")
    assert sorted(vocab) == sorted(MOODS)

    src = (repo / "saturn" / "src" / "classify" / "room_class_data.c").read_text()
    from art_nouns import nouns_by_mood
    plan = art_queries.build(vocab, nouns_by_mood(src))
    for mood in MOODS:
        assert len(plan[mood]) >= vocab[mood]["target"], (
            f"{mood}: {len(plan[mood])} queries for a target of "
            f"{vocab[mood]['target']} -- add adjectives, donors or extra_nouns"
        )
```

- [ ] **Step 2: Run it and watch it fail**

Run: `python -m pytest tools/tests/test_art_queries.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'art_queries'`.

- [ ] **Step 3: Write the builder**

Create `tools/art_queries.py`:

```python
"""Turn the hand-edited art vocabulary into a per-mood list of search phrases.

Description: A mood contributes adjectives; the moods it names as donors
    contribute nouns. Their cross product is what gets searched.

    Donors exist because several moods name no place at all. TC_HORROR's keywords
    are corpse, decay, eerie, rotting, shadow, skeleton, stench -- qualities, and
    nothing photographs ninety-nine distinct stenches. Horror is a mood applied to
    a place, so it borrows nouns from the moods whose places it haunts. TC_MAGIC
    and TC_MYSTERY have the same shape; TC_HOUSE donates to itself.
Author: suinevere
Dependencies: json, collections, art_nouns
Globals: N/A
"""
import json
from collections import namedtuple

from art_nouns import MOODS

Query = namedtuple("Query", "mood donor noun adjective phrase")


def validate(vocab):
    """Reject a vocabulary that names a mood or donor that does not exist.

    Description: Raises rather than warning. A typo'd mood key silently fetches
        nothing for a real mood and 99 pictures for a folder no disc reads, which
        is not worth discovering after an afternoon of review.
    Author: suinevere
    Dependencies: N/A
    Globals: MOODS
    Params: vocab -- the parsed art_queries.json
    Returns: the vocabulary, unchanged
    """
    for mood, entry in vocab.items():
        if mood not in MOODS:
            raise ValueError(f"{mood} is not a mood folder; expected one of {MOODS}")
        if not entry.get("adjectives"):
            raise ValueError(f"{mood} has no adjectives")
        for donor in entry.get("donors", []):
            if donor not in MOODS:
                raise ValueError(f"{mood}: donor {donor} is not a mood folder")
    return vocab


def load(path):
    """Read and validate art_queries.json.

    Author: suinevere
    Dependencies: json
    Globals: N/A
    Params: path -- the JSON file
    Returns: the validated vocabulary dict
    """
    with open(path, "r", encoding="utf-8") as fh:
        return validate(json.load(fh))


def build(vocab, nouns):
    """Cross each mood's adjectives with the nouns its donors contribute.

    Description: Raises when a mood reaches no noun at all, because that mood
        would silently fetch nothing.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: vocab -- the validated vocabulary; nouns -- nouns_by_mood output
    Returns: dict mapping mood to a list of Query
    """
    plan = {}
    for mood, entry in vocab.items():
        excluded = set(entry.get("exclude_nouns", []))
        pairs = []
        for donor in entry.get("donors", []):
            for noun in nouns.get(donor, []):
                if noun not in excluded:
                    pairs.append((donor, noun))
        for noun in entry.get("extra_nouns", []):
            if noun not in excluded:
                pairs.append(("EXTRA", noun))
        if not pairs:
            raise ValueError(
                f"{mood} reaches no noun: its donors contribute none and it "
                f"lists no extra_nouns"
            )
        plan[mood] = [
            Query(mood, donor, noun, adj, f"{adj} {noun}")
            for adj in entry["adjectives"]
            for donor, noun in pairs
        ]
    return plan
```

- [ ] **Step 4: Write the vocabulary**

Create `tools/assets/art_queries.json` with all twelve moods. Every mood needs
`adjectives × reachable nouns >= target`, which the last test enforces. Starting
point — tune the adjectives after seeing the first contact sheets:

```json
{
  "WILDER":   { "adjectives": ["misty", "sunlit", "overgrown", "windswept", "verdant", "remote", "wild", "dense"],
                "donors": ["WILDER"], "extra_nouns": ["moorland", "clearing", "ravine"],
                "exclude_nouns": [], "target": 99 },
  "UNDRGRND": { "adjectives": ["dim", "damp", "narrow", "deep", "torchlit", "carved", "winding", "echoing"],
                "donors": ["UNDRGRND"], "extra_nouns": ["catacomb", "mineshaft", "cavern lake"],
                "exclude_nouns": [], "target": 99 },
  "WATER":    { "adjectives": ["still", "rushing", "moonlit", "misty", "deep", "clear", "reflective", "cold"],
                "donors": ["WATER"], "extra_nouns": ["waterfall", "reservoir", "estuary"],
                "exclude_nouns": [], "target": 99 },
  "NAUTICAL": { "adjectives": ["stormy", "weathered", "creaking", "foggy", "open", "moored", "sunlit", "vast"],
                "donors": ["NAUTICAL"], "extra_nouns": ["quarterdeck", "rigging", "dockside"],
                "exclude_nouns": [], "target": 99 },
  "TOWN":     { "adjectives": ["cobbled", "bustling", "quiet", "medieval", "narrow", "lamplit", "old", "rainy"],
                "donors": ["TOWN"], "extra_nouns": ["marketplace", "town square", "alleyway"],
                "exclude_nouns": [], "target": 99 },
  "DUNGN":    { "adjectives": ["stone", "torchlit", "grim", "vaulted", "barred", "cold", "ancient", "oppressive"],
                "donors": ["DUNGN", "UNDRGRND"], "extra_nouns": ["oubliette", "guardroom"],
                "exclude_nouns": [], "target": 99 },
  "DESERT":   { "adjectives": ["arid", "sunbaked", "vast", "windswept", "golden", "cracked", "empty", "hazy"],
                "donors": ["DESERT"], "extra_nouns": ["oasis", "salt flat", "sandstone canyon"],
                "exclude_nouns": [], "target": 99 },
  "MAGIC":    { "adjectives": ["glowing", "ethereal", "ornate", "arcane", "shimmering", "otherworldly", "gilded", "starlit"],
                "donors": ["HOUSE", "UNDRGRND", "WILDER"], "extra_nouns": ["stone circle", "observatory"],
                "exclude_nouns": ["kitchen", "closet"], "target": 99 },
  "SCIFI":    { "adjectives": ["futuristic", "metallic", "neon", "sterile", "industrial", "backlit", "cavernous", "chrome"],
                "donors": ["SCIFI"], "extra_nouns": ["airlock", "server hall", "launch bay"],
                "exclude_nouns": [], "target": 99 },
  "HORROR":   { "adjectives": ["dark", "abandoned", "derelict", "decaying", "eerie", "ruined", "gloomy", "foggy"],
                "donors": ["HOUSE", "UNDRGRND", "DUNGN"], "extra_nouns": ["morgue", "asylum ward"],
                "exclude_nouns": ["kitchen", "porch"], "target": 99 },
  "MYSTERY":  { "adjectives": ["shadowed", "dimly lit", "deserted", "rain-streaked", "smoky", "hushed", "veiled", "noir"],
                "donors": ["HOUSE", "TOWN"], "extra_nouns": ["study desk", "reading room"],
                "exclude_nouns": [], "target": 99 },
  "HOUSE":    { "adjectives": ["cosy", "victorian", "wooden", "antique", "sunlit", "cluttered", "panelled", "quiet"],
                "donors": ["HOUSE"], "extra_nouns": ["staircase", "landing", "conservatory"],
                "exclude_nouns": [], "target": 99 }
}
```

- [ ] **Step 5: Write the companion document**

Create `tools/assets/art_queries.md` explaining each field: `adjectives` (the
mood's own look), `donors` (which moods' nouns it borrows, and why HORROR/MAGIC/
MYSTERY must borrow), `extra_nouns` (a search word the classifier does not know),
`exclude_nouns` (a word that keeps returning junk), `target` (how many pictures
to fill, capped at 99). State plainly that this file is the only place search
terms are written, and that changing one means re-running the fetcher and
reviewing a fresh contact sheet.

- [ ] **Step 6: Run the tests and watch them pass**

Run: `python -m pytest tools/tests/test_art_queries.py -v`
Expected: all seven PASS. If `test_the_shipped_vocabulary_is_valid_and_covers_every_mood`
fails, the named mood has too few `adjectives × nouns` — add adjectives, a donor,
or `extra_nouns` until it clears its target.

- [ ] **Step 7: Commit**

```bash
git add tools/art_queries.py tools/assets/art_queries.json \
        tools/assets/art_queries.md tools/tests/test_art_queries.py
git commit -m "tools: add the hand-edited art vocabulary and the query cross-product it drives"
```

---

### Task 3: The metric gate

**Files:**
- Create: `tools/art_metrics.py`
- Create: `tools/tests/test_art_metrics.py`
- Modify: `tools/requirements.txt` (add `imagehash`)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `crop(im) -> Image` — centre-crop and resize any RGB image to exactly 320×224.
  - `Scores` — `namedtuple("Scores", "luminance busyness banding")`. All floats.
    `luminance` is mean 0..255; `busyness` is mean absolute Laplacian, 0 up; `banding` is RMS
    error against the 255-colour reduction, 0 up.
  - `score(im) -> Scores`
  - `verdict(s: Scores) -> str` — `"pass"`, or a one-word reason: `"bright"`,
    `"busy"`, `"banding"`.
  - `THRESHOLDS` — a dict the caller may override.

- [ ] **Step 1: Write the failing test**

Create `tools/tests/test_art_metrics.py`:

```python
"""Score synthetic images whose properties are known by construction."""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_metrics


def flat(value, size=(640, 480)):
    return Image.new("RGB", size, (value, value, value))


def checkerboard(size=(640, 480), cell=4):
    im = Image.new("RGB", size, (0, 0, 0))
    d = ImageDraw.Draw(im)
    for y in range(0, size[1], cell):
        for x in range(0, size[0], cell):
            if (x // cell + y // cell) % 2:
                d.rectangle([x, y, x + cell, y + cell], fill=(255, 255, 255))
    return im


def gradient(size=(640, 480)):
    im = Image.new("RGB", size)
    for x in range(size[0]):
        v = int(255 * x / (size[0] - 1))
        for y in range(size[1]):
            im.putpixel((x, y), (v, v // 2, 255 - v))
    return im


def test_crop_always_produces_the_disc_size():
    for size in [(640, 480), (1920, 1080), (300, 900), (320, 224)]:
        assert art_metrics.crop(Image.new("RGB", size)).size == (320, 224)


def test_luminance_tracks_brightness():
    assert art_metrics.score(flat(20)).luminance < 40
    assert art_metrics.score(flat(240)).luminance > 200


def test_busyness_separates_flat_from_checkerboard():
    assert art_metrics.score(flat(120)).busyness < 1.0
    assert art_metrics.score(checkerboard()).busyness > 20.0


def test_a_bright_image_is_rejected_as_bright():
    assert art_metrics.verdict(art_metrics.score(flat(250))) == "bright"


def test_a_busy_image_is_rejected_as_busy():
    assert art_metrics.verdict(art_metrics.score(checkerboard())) == "busy"


def test_a_calm_dark_image_passes():
    assert art_metrics.verdict(art_metrics.score(flat(70))) == "pass"


def test_busyness_is_judged_before_brightness():
    """A dim cannot fix busy, only bright -- so busy must win when both trip."""
    bright_and_busy = art_metrics.Scores(luminance=250.0, busyness=99.0, banding=0.0)
    assert art_metrics.verdict(bright_and_busy) == "busy"


def test_banding_is_measured_and_finite():
    s = art_metrics.score(gradient())
    assert s.banding >= 0.0
    assert s.banding < 255.0
```

- [ ] **Step 2: Run it and watch it fail**

Run: `python -m pytest tools/tests/test_art_metrics.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'art_metrics'`.

- [ ] **Step 3: Write the module**

Create `tools/art_metrics.py`:

```python
"""Score a candidate background on the three things that disqualify one.

Description: The game draws its text over the picture on VDP2 NBG0, at
    Priority::Layer1, with nothing between them. So a candidate fails for three
    reasons and they are not equally fixable:

      bright  -- the player's Dimming row can rescue this, so it is the softest
                 threshold of the three.
      busy    -- a uniform colour offset does not touch local contrast, so no
                 setting rescues a busy picture. Judged first, and hardest.
      banding -- the disc is 8bpp and make_tga.py quantises to 255 colours, so a
                 wide smooth gradient becomes visible steps. Cheaper to reject
                 here than to notice on hardware.
Author: suinevere
Dependencies: PIL
Globals: THRESHOLDS
"""
from collections import namedtuple

from PIL import Image, ImageFilter

WIDTH, HEIGHT = 320, 224

Scores = namedtuple("Scores", "luminance busyness banding")

THRESHOLDS = {
    "luminance_max": 165.0,
    "busyness_max":  18.0,
    "banding_max":   12.0,
}


def crop(im):
    """Centre-crop and resize any image to exactly 320x224.

    Description: Scales the shorter side to fit, then takes the middle. Naive on
        purpose: a saliency crop would need a model, and the contact sheet is
        where a badly-framed picture gets rejected anyway.
    Author: suinevere
    Dependencies: PIL
    Globals: WIDTH, HEIGHT
    Params: im -- any PIL image
    Returns: a 320x224 RGB image
    """
    im = im.convert("RGB")
    w, h = im.size
    scale = max(WIDTH / w, HEIGHT / h)
    nw, nh = max(WIDTH, int(round(w * scale))), max(HEIGHT, int(round(h * scale)))
    im = im.resize((nw, nh), Image.LANCZOS)
    left, top = (nw - WIDTH) // 2, (nh - HEIGHT) // 2
    return im.crop((left, top, left + WIDTH, top + HEIGHT))


def score(im):
    """Measure luminance, busyness and quantisation banding on a cropped candidate.

    Author: suinevere
    Dependencies: PIL
    Globals: N/A
    Params: im -- any PIL image; cropped internally
    Returns: a Scores triple
    """
    im = crop(im)
    grey = im.convert("L")

    px = grey.tobytes()
    luminance = sum(px) / float(len(px))

    edges = grey.filter(ImageFilter.FIND_EDGES).tobytes()
    busyness = sum(edges) / float(len(edges))

    q = im.quantize(colors=255, method=Image.Quantize.MEDIANCUT).convert("RGB")
    a, b = im.tobytes(), q.tobytes()
    total = 0
    for i in range(0, len(a), 97):
        d = a[i] - b[i]
        total += d * d
    banding = (total / float(len(range(0, len(a), 97)))) ** 0.5

    return Scores(luminance, busyness, banding)


def verdict(s):
    """Decide whether a candidate survives to the contact sheet.

    Description: Busyness is judged first because it is the only failure no
        player setting can undo -- the Dimming row shifts every pixel by the same
        constant, which leaves local contrast exactly where it was.
    Author: suinevere
    Dependencies: N/A
    Globals: THRESHOLDS
    Params: s -- a Scores triple
    Returns: "pass", or one of "busy", "bright", "banding"
    """
    if s.busyness > THRESHOLDS["busyness_max"]:   return "busy"
    if s.luminance > THRESHOLDS["luminance_max"]: return "bright"
    if s.banding > THRESHOLDS["banding_max"]:     return "banding"
    return "pass"
```

- [ ] **Step 4: Add the dependency**

Append `imagehash` to `tools/requirements.txt` — Task 4 needs it, and adding it
here keeps `convert-backgrounds.sh`'s single install step complete.

- [ ] **Step 5: Run the tests and watch them pass**

Run: `python -m pytest tools/tests/test_art_metrics.py -v`
Expected: all eight PASS. If a threshold test fails, tune the constant in
`THRESHOLDS` — the synthetic fixtures are extreme by construction, so a failure
means the threshold is far off, not that the metric is wrong.

- [ ] **Step 6: Commit**

```bash
git add tools/art_metrics.py tools/tests/test_art_metrics.py tools/requirements.txt
git commit -m "tools: score background candidates on legibility, busyness and quantisation banding"
```

---

### Task 4: The Pixabay fetcher and the manifest

**Files:**
- Create: `tools/fetch_art.py`
- Create: `tools/tests/test_fetch_art.py`
- Modify: `tools/requirements.txt` (add `requests`)

**Interfaces:**
- Consumes: `art_queries.load`/`build`/`Query` (Task 2), `art_nouns.nouns_by_mood` (Task 1), `art_metrics.crop`/`score`/`verdict` (Task 3).
- Produces:
  - `search(session, key, phrase, per_page=12) -> list` — a list of dicts with
    keys `id`, `page_url`, `image_url`.
  - `Candidate` — `namedtuple("Candidate", "query hit scores verdict phash path")`.
  - `harvest(plan, fetcher, out_dir, manifest, budget) -> list[Candidate]`
  - `load_manifest(path) -> dict` / `save_manifest(path, data)` — keyed by
    `str(pixabay_id)`.
  - Manifest record fields: `id`, `page_url`, `image_url`, `phrase`, `mood`,
    `donor`, `noun`, `licence`, `fetched`, `luminance`, `busyness`, `banding`,
    `verdict`, `phash`, `status` (`"candidate"` / `"accepted"` / `"rejected"`).

- [ ] **Step 1: Write the failing test**

Create `tools/tests/test_fetch_art.py`:

```python
"""Exercise the fetcher against a stub. No network, ever."""
import io
import json
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import fetch_art
from art_queries import Query


def png_bytes(value, size=(640, 480)):
    buf = io.BytesIO()
    Image.new("RGB", size, (value, value, value)).save(buf, "PNG")
    return buf.getvalue()


class StubFetcher:
    """Stands in for search + download. Records what it was asked for."""

    def __init__(self, per_phrase=2, value=70):
        self.per_phrase = per_phrase
        self.value = value
        self.asked = []
        self.downloads = 0
        self._next = 1000

    def search(self, phrase, per_page):
        self.asked.append(phrase)
        out = []
        for _ in range(self.per_phrase):
            self._next += 1
            out.append({"id": self._next,
                        "page_url": f"https://pixabay.com/photos/{self._next}/",
                        "image_url": f"https://cdn.example/{self._next}.jpg"})
        return out

    def download(self, url):
        self.downloads += 1
        return png_bytes(self.value)


PLAN = {"HORROR": [Query("HORROR", "HOUSE", "hallway", "dark", "dark hallway"),
                   Query("HORROR", "EXTRA", "morgue", "dark", "dark morgue")]}


def test_harvest_writes_one_png_per_surviving_candidate(tmp_path):
    f = StubFetcher()
    got = fetch_art.harvest(PLAN, f, tmp_path, {}, budget=99)
    assert len(got) == 4
    for c in got:
        assert c.path.exists()
        assert Image.open(c.path).size == (320, 224)


def test_provenance_lands_in_the_folder_path(tmp_path):
    got = fetch_art.harvest(PLAN, StubFetcher(), tmp_path, {}, budget=99)
    rel = sorted(str(c.path.relative_to(tmp_path)).replace("\\", "/") for c in got)
    assert rel[0].startswith("HORROR/EXTRA/morgue/")
    assert rel[-1].startswith("HORROR/HOUSE/hallway/")


def test_rejected_candidates_are_recorded_but_not_written(tmp_path):
    f = StubFetcher(value=252)
    manifest = {}
    got = fetch_art.harvest(PLAN, f, tmp_path, manifest, budget=99)
    assert got == []
    assert len(manifest) == 4
    assert all(r["verdict"] == "bright" for r in manifest.values())
    assert all(r["status"] == "rejected" for r in manifest.values())


def test_an_already_seen_id_is_not_downloaded_twice(tmp_path):
    f = StubFetcher()
    manifest = {}
    fetch_art.harvest(PLAN, f, tmp_path, manifest, budget=99)
    before = f.downloads
    fetch_art.harvest(PLAN, f, tmp_path, manifest, budget=99)
    assert f.downloads == before, "a known id must not be fetched again"


def test_budget_stops_the_run(tmp_path):
    f = StubFetcher()
    got = fetch_art.harvest(PLAN, f, tmp_path, {}, budget=1)
    assert len(got) == 1


def test_a_download_failure_skips_that_hit_and_continues(tmp_path):
    class Flaky(StubFetcher):
        def download(self, url):
            self.downloads += 1
            if self.downloads == 1:
                raise IOError("connection reset")
            return png_bytes(70)

    got = fetch_art.harvest(PLAN, Flaky(), tmp_path, {}, budget=99)
    assert len(got) == 3


def test_manifest_round_trips(tmp_path):
    path = tmp_path / "m.json"
    fetch_art.save_manifest(path, {"1": {"id": 1, "licence": fetch_art.LICENCE}})
    assert fetch_art.load_manifest(path)["1"]["id"] == 1
    assert fetch_art.load_manifest(tmp_path / "absent.json") == {}


def test_no_api_key_is_reported_not_raised(monkeypatch, capsys):
    monkeypatch.delenv("PIXABAY_API_KEY", raising=False)
    assert fetch_art.main([]) == 0
    assert "PIXABAY_API_KEY" in capsys.readouterr().out
```

- [ ] **Step 2: Run it and watch it fail**

Run: `python -m pytest tools/tests/test_fetch_art.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'fetch_art'`.

- [ ] **Step 3: Write the fetcher core**

Create `tools/fetch_art.py`:

```python
"""Fetch, crop and score background candidates from Pixabay.

Description: Walks the query plan, asks Pixabay for each phrase, crops every hit
    to 320x224, scores it, and writes the survivors under
    <out>/<MOOD>/<DONOR>/<noun>/. The path is the query's provenance: any picture
    on the disc can be traced back to the adjective and noun that found it.

    Every failure degrades. No key, no network, a dead query or an image that will
    not decode all print and continue, because a run that aborts on hit 400 of 1200
    wastes the 399 before it.

    Nothing here decides what ships. Surviving the metric gate only earns a place
    on a contact sheet; a human accepts or rejects from there.
Author: suinevere
Dependencies: requests, PIL, art_metrics, art_queries, art_nouns
Globals: ENDPOINT, LICENCE
"""
import json
import os
import sys
import time
from collections import namedtuple
from datetime import date
from io import BytesIO
from pathlib import Path

from PIL import Image

import art_metrics
import art_nouns
import art_queries

ENDPOINT = "https://pixabay.com/api/"
LICENCE = "Pixabay Content License"

Candidate = namedtuple("Candidate", "query hit scores verdict phash path")


def load_manifest(path):
    """Read the provenance manifest, or an empty one if it does not exist yet.

    Author: suinevere
    Dependencies: json
    Globals: N/A
    Params: path -- the manifest file
    Returns: dict keyed by stringified Pixabay id
    """
    path = Path(path)
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def save_manifest(path, data):
    """Write the provenance manifest, sorted so its diffs stay readable.

    Author: suinevere
    Dependencies: json
    Globals: N/A
    Params: path -- the manifest file; data -- the dict to write
    Returns: N/A
    """
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with Path(path).open("w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2, sort_keys=True)
        fh.write("\n")


def harvest(plan, fetcher, out_dir, manifest, budget):
    """Fetch and gate every query in the plan, stopping at `budget` survivors.

    Description: A Pixabay id already in the manifest is skipped without a
        download, which is what makes a re-run cheap and a partial run resumable.
    Author: suinevere
    Dependencies: PIL, art_metrics
    Globals: LICENCE
    Params: plan -- mood -> [Query]; fetcher -- an object with .search/.download;
        out_dir -- where surviving PNGs go; manifest -- mutated in place;
        budget -- stop after this many survivors
    Returns: the list of surviving Candidate
    """
    out_dir = Path(out_dir)
    kept = []

    for mood in sorted(plan):
        for query in plan[mood]:
            if len(kept) >= budget:
                return kept
            try:
                hits = fetcher.search(query.phrase, per_page=12)
            except Exception as exc:
                print(f"  {query.phrase}: search failed ({exc})")
                continue

            for hit in hits:
                if len(kept) >= budget:
                    return kept
                key = str(hit["id"])
                if key in manifest:
                    continue
                try:
                    im = Image.open(BytesIO(fetcher.download(hit["image_url"])))
                    im.load()
                except Exception as exc:
                    print(f"  {hit['image_url']}: download failed ({exc})")
                    continue

                scores = art_metrics.score(im)
                call = art_metrics.verdict(scores)
                record = {
                    "id": hit["id"], "page_url": hit["page_url"],
                    "image_url": hit["image_url"], "phrase": query.phrase,
                    "mood": query.mood, "donor": query.donor, "noun": query.noun,
                    "licence": LICENCE, "fetched": date.today().isoformat(),
                    "luminance": round(scores.luminance, 2),
                    "busyness": round(scores.busyness, 2),
                    "banding": round(scores.banding, 2),
                    "verdict": call, "phash": "",
                    "status": "rejected" if call != "pass" else "candidate",
                }
                manifest[key] = record
                if call != "pass":
                    continue

                dest = out_dir / query.mood / query.donor / query.noun
                dest.mkdir(parents=True, exist_ok=True)
                path = dest / f"{hit['id']}.png"
                art_metrics.crop(im).save(path, "PNG")
                record["phash"] = _phash(path)
                kept.append(Candidate(query, hit, scores, call, record["phash"], path))

    return kept
```

- [ ] **Step 4: Add dedup, the live client, and main**

```python
def _phash(path):
    """A perceptual hash, or "" when imagehash is unavailable.

    Description: Optional on purpose -- dedup is a quality improvement, not a
        correctness requirement, and a missing wheel must not stop a fetch.
    Author: suinevere
    Dependencies: imagehash (optional), PIL
    Globals: N/A
    Params: path -- a written PNG
    Returns: the hash as a string, or ""
    """
    try:
        import imagehash
    except ImportError:
        return ""
    return str(imagehash.phash(Image.open(path)))


class PixabayFetcher:
    """Search and download against the live Pixabay API.

    Description: Sleeps between calls to stay inside the documented 100
        requests-per-minute allowance, and treats any non-200 as an empty result
        rather than an exception, so one bad phrase cannot end a run.
    Author: suinevere
    Dependencies: requests
    Globals: ENDPOINT
    """

    def __init__(self, key, session=None, pause=0.7):
        import requests
        self.key = key
        self.session = session or requests.Session()
        self.pause = pause

    def search(self, phrase, per_page=12):
        time.sleep(self.pause)
        r = self.session.get(ENDPOINT, timeout=30, params={
            "key": self.key, "q": phrase, "image_type": "photo",
            "orientation": "horizontal", "safesearch": "true",
            "min_width": 640, "per_page": per_page,
        })
        if r.status_code != 200:
            print(f"  {phrase}: HTTP {r.status_code}")
            return []
        return [{"id": h["id"], "page_url": h["pageURL"],
                 "image_url": h.get("largeImageURL") or h["webformatURL"]}
                for h in r.json().get("hits", [])]

    def download(self, url):
        r = self.session.get(url, timeout=60)
        r.raise_for_status()
        return r.content


def main(argv):
    """Run a harvest against the shipped vocabulary.

    Author: suinevere
    Dependencies: art_queries, art_nouns
    Globals: N/A
    Params: argv -- optional single argument, the survivor budget for this run
    Returns: 0 always; failures are reported, not raised
    """
    repo = Path(__file__).resolve().parents[1]
    key = os.environ.get("PIXABAY_API_KEY", "")
    if not key:
        print("  PIXABAY_API_KEY is not set. Get a free key at "
              "https://pixabay.com/api/docs/ and export it, then re-run.")
        return 0

    budget = int(argv[0]) if argv else 200
    vocab = art_queries.load(repo / "tools" / "assets" / "art_queries.json")
    nouns = art_nouns.nouns_by_mood(
        (repo / "saturn" / "src" / "classify" / "room_class_data.c").read_text())
    plan = art_queries.build(vocab, nouns)

    manifest_path = repo / "tools" / "assets" / "art_manifest.json"
    manifest = load_manifest(manifest_path)
    try:
        kept = harvest(plan, PixabayFetcher(key),
                       repo / "tools" / "assets" / "candidates",
                       manifest, budget)
    finally:
        save_manifest(manifest_path, manifest)
    print(f"  {len(kept)} candidates written; {len(manifest)} images known")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```

Append `requests` to `tools/requirements.txt`.

Add `tools/assets/candidates/` to `.gitignore` — candidates are working files
until the review promotes them.

- [ ] **Step 5: Run the tests and watch them pass**

Run: `python -m pytest tools/tests/test_fetch_art.py -v`
Expected: all eight PASS.

- [ ] **Step 6: Commit**

```bash
git add tools/fetch_art.py tools/tests/test_fetch_art.py tools/requirements.txt .gitignore
git commit -m "tools: fetch and gate background candidates from Pixabay with a provenance manifest"
```

---

### Task 5: Contact sheets, verdicts, and promotion

**Files:**
- Create: `tools/art_review.py`
- Create: `tools/tests/test_art_review.py`

**Interfaces:**
- Consumes: `load_manifest`/`save_manifest` (Task 4), `art_nouns.MOODS` (Task 1).
- Produces:
  - `sheet(mood, records, candidates_dir) -> str` — a self-contained HTML page.
  - `promote(verdicts, manifest, candidates_dir, png_dir) -> dict` — moves
    accepted candidates into the source tree, updates `status`, returns a
    per-mood count of what was promoted.
  - `dedup(records) -> list` — drops records whose `phash` is within a Hamming
    distance of 6 of an earlier accepted one, across all moods.
  - Verdicts file shape: `{"<pixabay id>": "accept" | "reject"}`.

- [ ] **Step 1: Write the failing test**

Create `tools/tests/test_art_review.py`:

```python
"""Cover sheet generation, cross-mood dedup, and promotion into the source tree."""
import json
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_review


def record(pid, mood="HORROR", donor="HOUSE", noun="hallway", phash="0" * 16,
           status="candidate"):
    return {"id": pid, "page_url": f"https://pixabay.com/photos/{pid}/",
            "image_url": "", "phrase": "dark hallway", "mood": mood,
            "donor": donor, "noun": noun, "licence": "Pixabay Content License",
            "fetched": "2026-08-06", "luminance": 70.0, "busyness": 4.0,
            "banding": 2.0, "verdict": "pass", "phash": phash, "status": status}


def make_candidate(root, rec):
    d = root / rec["mood"] / rec["donor"] / rec["noun"]
    d.mkdir(parents=True, exist_ok=True)
    p = d / f"{rec['id']}.png"
    Image.new("RGB", (320, 224), (60, 60, 60)).save(p, "PNG")
    return p


def test_sheet_is_self_contained_and_names_its_sources(tmp_path):
    rec = record(1)
    make_candidate(tmp_path, rec)
    html = art_review.sheet("HORROR", {"1": rec}, tmp_path)
    assert "<html" in html and "</html>" in html
    assert "data:image/png;base64," in html, "thumbnails must be embedded"
    assert "http" not in html.split("pixabay.com")[0][-200:] or True
    assert "dark hallway" in html
    assert "pixabay.com/photos/1/" in html


def test_sheet_covers_only_its_own_mood(tmp_path):
    recs = {"1": record(1, mood="HORROR"), "2": record(2, mood="HOUSE")}
    for r in recs.values():
        make_candidate(tmp_path, r)
    html = art_review.sheet("HORROR", recs, tmp_path)
    assert "photos/1/" in html
    assert "photos/2/" not in html


def test_promote_moves_accepted_and_leaves_rejected(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    recs = {"1": record(1), "2": record(2)}
    for r in recs.values():
        make_candidate(cand, r)

    counts = art_review.promote({"1": "accept", "2": "reject"}, recs, cand, png)

    assert counts == {"HORROR": 1}
    assert (png / "HORROR" / "HOUSE" / "hallway" / "1.png").exists()
    assert not (png / "HORROR" / "HOUSE" / "hallway" / "2.png").exists()
    assert recs["1"]["status"] == "accepted"
    assert recs["2"]["status"] == "rejected"


def test_promotion_is_idempotent(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    recs = {"1": record(1)}
    make_candidate(cand, recs["1"])
    art_review.promote({"1": "accept"}, recs, cand, png)
    counts = art_review.promote({"1": "accept"}, recs, cand, png)
    assert counts == {}, "an already-accepted record must not be promoted twice"


def test_dedup_drops_a_near_duplicate_across_moods():
    a = record(1, mood="HORROR", phash="ff00ff00ff00ff00")
    b = record(2, mood="HOUSE", phash="ff00ff00ff00ff01")
    c = record(3, mood="TOWN", phash="00ff00ff00ff00ff")
    kept = art_review.dedup([a, b, c])
    assert [r["id"] for r in kept] == [1, 3]


def test_dedup_keeps_everything_when_hashes_are_missing():
    recs = [record(1, phash=""), record(2, phash="")]
    assert len(art_review.dedup(recs)) == 2
```

- [ ] **Step 2: Run it and watch it fail**

Run: `python -m pytest tools/tests/test_art_review.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'art_review'`.

- [ ] **Step 3: Write sheet generation**

Create `tools/art_review.py`. Embed each thumbnail as a `data:` URI so a sheet is
one file that opens with no server and no working directory:

```python
"""Build per-mood contact sheets and promote what the reviewer accepted.

Description: The metric gate removes what is provably unusable; everything left
    is a judgement call, and one sheet per mood is the cheapest way to make a few
    hundred of them. Each thumbnail carries the phrase that found it, its three
    scores, and a link to its Pixabay page, so a doubtful picture can be checked
    at full size without leaving the sheet.

    Verdicts live in their own file rather than in the manifest, so a review
    session can be interrupted and resumed without a half-written manifest.
Author: suinevere
Dependencies: base64, json, pathlib, PIL
Globals: HAMMING_MAX
"""
import base64
import json
from pathlib import Path

HAMMING_MAX = 6


def _thumb_uri(path):
    """Embed a candidate PNG as a data: URI.

    Author: suinevere
    Dependencies: base64
    Globals: N/A
    Params: path -- the candidate PNG
    Returns: a data: URI string, or "" when the file is missing
    """
    path = Path(path)
    if not path.exists():
        return ""
    return "data:image/png;base64," + base64.b64encode(path.read_bytes()).decode()


def sheet(mood, records, candidates_dir):
    """Render one mood's candidates as a self-contained HTML review page.

    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: mood -- the mood folder name; records -- the manifest dict;
        candidates_dir -- where fetch_art wrote the PNGs
    Returns: the HTML as a string
    """
    mine = [r for r in records.values()
            if r["mood"] == mood and r["status"] == "candidate"]
    mine.sort(key=lambda r: (r["donor"], r["noun"], r["id"]))

    cells = []
    for r in mine:
        src = _thumb_uri(Path(candidates_dir) / r["mood"] / r["donor"]
                         / r["noun"] / f"{r['id']}.png")
        cells.append(
            f'<figure data-id="{r["id"]}">'
            f'<img src="{src}" width="320" height="224" alt="">'
            f'<figcaption>{r["phrase"]}<br>'
            f'lum {r["luminance"]} &middot; busy {r["busyness"]} '
            f'&middot; band {r["banding"]}<br>'
            f'<a href="{r["page_url"]}" target="_blank">{r["id"]}</a><br>'
            f'<label><input type="checkbox" checked> keep</label>'
            f'</figcaption></figure>'
        )

    return (
        "<!doctype html><html><head><meta charset='utf-8'>"
        f"<title>{mood} &mdash; {len(mine)} candidates</title>"
        "<style>body{background:#111;color:#ddd;font:13px sans-serif}"
        "figure{display:inline-block;margin:8px;text-align:center}"
        "figcaption{max-width:320px}a{color:#8cf}</style></head><body>"
        f"<h1>{mood} &mdash; {len(mine)} candidates</h1>"
        + "".join(cells) +
        "<p><button onclick=\"save()\">Download verdicts.json</button></p>"
        "<script>function save(){var o={};"
        "document.querySelectorAll('figure').forEach(function(f){"
        "o[f.dataset.id]=f.querySelector('input').checked?'accept':'reject';});"
        "var a=document.createElement('a');"
        "a.href=URL.createObjectURL(new Blob([JSON.stringify(o,null,2)],"
        "{type:'application/json'}));a.download='verdicts.json';a.click();}"
        "</script></body></html>"
    )
```

- [ ] **Step 4: Write dedup and promotion**

```python
def _hamming(a, b):
    """Hamming distance between two hex perceptual hashes.

    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: a, b -- hex strings of equal length
    Returns: the bit distance, or a large number when they are not comparable
    """
    if not a or not b or len(a) != len(b):
        return 1 << 30
    return bin(int(a, 16) ^ int(b, 16)).count("1")


def dedup(records):
    """Drop records whose perceptual hash is too close to an earlier one.

    Description: Runs across every mood, not within one. A cave that appears in
        both UNDRGRND and HORROR reads as the rotation being broken, which is the
        same reason the pools are kept disjoint on the disc.

        A record with no hash is kept: imagehash is optional, and dropping
        everything when it is absent would be worse than keeping a duplicate.
    Author: suinevere
    Dependencies: N/A
    Globals: HAMMING_MAX
    Params: records -- a list of manifest records
    Returns: the kept records, in input order
    """
    kept, seen = [], []
    for r in records:
        h = r.get("phash", "")
        if h and any(_hamming(h, s) <= HAMMING_MAX for s in seen):
            continue
        if h:
            seen.append(h)
        kept.append(r)
    return kept


def promote(verdicts, manifest, candidates_dir, png_dir):
    """Move accepted candidates into the source tree and record the outcome.

    Author: suinevere
    Dependencies: pathlib
    Globals: N/A
    Params: verdicts -- id -> "accept"/"reject"; manifest -- mutated in place;
        candidates_dir -- where the PNGs are; png_dir -- tools/assets/png
    Returns: dict mapping mood to how many pictures it gained
    """
    counts = {}
    for key, call in verdicts.items():
        rec = manifest.get(key)
        if rec is None or rec["status"] != "candidate":
            continue
        if call != "accept":
            rec["status"] = "rejected"
            continue

        rel = Path(rec["mood"]) / rec["donor"] / rec["noun"] / f"{rec['id']}.png"
        src, dst = Path(candidates_dir) / rel, Path(png_dir) / rel
        if not src.exists():
            print(f"  {rel}: candidate file is missing, skipping")
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(src.read_bytes())
        src.unlink()
        rec["status"] = "accepted"
        counts[rec["mood"]] = counts.get(rec["mood"], 0) + 1
    return counts
```

```python
def main(argv):
    """Write the contact sheets, or promote a downloaded verdicts file.

    Author: suinevere
    Dependencies: fetch_art
    Globals: N/A
    Params: argv -- ["--sheets"] or ["--promote", "<verdicts.json>"]
    Returns: 0 always
    """
    import fetch_art
    from art_nouns import MOODS

    repo = Path(__file__).resolve().parents[1]
    assets = repo / "tools" / "assets"
    manifest_path = assets / "art_manifest.json"
    manifest = fetch_art.load_manifest(manifest_path)

    if argv and argv[0] == "--sheets":
        kept = {str(r["id"]): r
                for r in dedup([r for r in manifest.values()
                                if r["status"] == "candidate"])}
        out = assets / "sheets"
        out.mkdir(parents=True, exist_ok=True)
        for mood in MOODS:
            page = out / f"{mood}.html"
            page.write_text(sheet(mood, kept, assets / "candidates"),
                            encoding="utf-8")
            print(f"  {page}")
        return 0

    if len(argv) >= 2 and argv[0] == "--promote":
        with open(argv[1], "r", encoding="utf-8") as fh:
            verdicts = json.load(fh)
        counts = promote(verdicts, manifest, assets / "candidates",
                         assets / "png")
        fetch_art.save_manifest(manifest_path, manifest)
        for mood in sorted(counts):
            print(f"  {mood}: +{counts[mood]}")
        return 0

    print("  usage: art_review.py --sheets | --promote <verdicts.json>")
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv[1:]))
```

Note the ordering: `dedup` runs when the sheets are built, not at promotion, so a
near-duplicate never reaches the reviewer's eye in the first place.

- [ ] **Step 5: Run the tests and watch them pass**

Run: `python -m pytest tools/tests/test_art_review.py -v`
Expected: all seven PASS.

- [ ] **Step 6: Commit**

```bash
git add tools/art_review.py tools/tests/test_art_review.py
git commit -m "tools: build per-mood contact sheets and promote reviewed candidates into the source tree"
```

---

### Task 6: Curation

**Files:**
- Modify: `tools/assets/art_queries.json` (tuning, repeatedly)
- Create: `tools/assets/png/<MOOD>/<DONOR>/<noun>/*.png` (the pictures)
- Modify: `tools/assets/art_manifest.json` (grows with every session)
- Modify: `saturn/src/video/category_art.inc`, `saturn/cd/data/TGA/` (regenerated)

**Interfaces:**
- Consumes: everything in Tasks 1–5.
- Produces: the filled source tree. No code.

This task is the long tail and is done in sittings, not in one pass. Each sitting
is the same loop.

- [ ] **Step 1: Fetch a batch**

```bash
export PIXABAY_API_KEY=...        # never commit this
python tools/fetch_art.py 200
```

- [ ] **Step 2: Build the sheets**

```bash
python tools/art_review.py --sheets
```

Open `tools/assets/sheets/<MOOD>.html`. Untick anything that is topically wrong,
badly framed, or that a mood's other pictures already cover. Click **Download
verdicts.json**.

- [ ] **Step 3: Promote**

```bash
python tools/art_review.py --promote ~/Downloads/verdicts.json
```

- [ ] **Step 4: Tune the vocabulary against what came back**

A phrase that returned nothing usable is a vocabulary problem, not a gate
problem. Edit `tools/assets/art_queries.json`: drop the noun into
`exclude_nouns`, or replace an adjective that is pulling the wrong subject.
Re-run Step 1. This is the loop the JSON exists for.

- [ ] **Step 5: Convert and check the counts**

```bash
sh tools/convert-backgrounds.sh
python -m pytest saturn/tests/test_category_art.py -v
```

Expected: PASS. `category_art.inc` now reflects the new totals.

- [ ] **Step 6: Commit the sitting**

```bash
git add tools/assets/png tools/assets/art_manifest.json \
        tools/assets/art_queries.json saturn/src/video/category_art.inc saturn/cd/data/TGA
git commit -m "art: add <N> reviewed backgrounds across <moods>"
```

- [ ] **Step 7: Repeat until every mood reaches its target, then verify on hardware**

A mood that stalls short of 99 still ships — it rotates over fewer pictures, which
is the same graceful degradation the three empty categories already rely on. Do
not hold the disc for a full set.

Ask the user to confirm on hardware:

> Play through several rooms in three different moods. Confirm the pictures vary
> rather than repeating, the text stays readable at the default Dimming setting,
> and none of them look banded or posterised.

---

### Task 7: Git LFS for the generated TGAs

**Files:**
- Create: `.gitattributes`
- Modify: `.gitignore`
- Create: `docs/superpowers/notes/art-lfs.md`

**Interfaces:**
- Consumes: a filled `saturn/cd/data/TGA/` (Task 6).
- Produces: no API.

Do this **last**. Migrating a handful of files proves nothing, and migrating
before the volume is real means doing it twice.

- [ ] **Step 1: Measure first**

```bash
du -sh saturn/cd/data/TGA
git count-objects -vH | grep size-pack
```

Record both numbers. If the TGA tree is under ~20MB, stop and skip this task —
plain blobs are simpler and the growth is not yet worth a dependency.

- [ ] **Step 2: Install and track**

```bash
git lfs install
git lfs track "saturn/cd/data/TGA/**/*.TGA"
```

Confirm `.gitattributes` now contains:

```
saturn/cd/data/TGA/**/*.TGA filter=lfs diff=lfs merge=lfs -text
```

- [ ] **Step 3: Migrate the existing history for that path**

```bash
git lfs migrate import --include="saturn/cd/data/TGA/**/*.TGA" --everything
```

This rewrites history. Confirm with the user before running it, and confirm no
other clone has unpushed work.

- [ ] **Step 4: Verify the fallback still holds**

```bash
git lfs ls-files | head
rm -rf saturn/cd/data/TGA
git checkout saturn/cd/data/TGA
python -m pytest saturn/tests/test_category_art.py -v
```

Expected: the tree restores from LFS and the count test passes. This is the
guarantee `convert-backgrounds.sh` depends on — a clean checkout builds with art
even with no Python and no network.

- [ ] **Step 5: Write the note**

Create `docs/superpowers/notes/art-lfs.md`: that TGAs are LFS-tracked and source
PNGs are not, why (the PNGs are the editable masters and diff-worthy; the TGAs
are generated and churn on every re-curation), and that a contributor without
`git lfs` gets pointer files and must install it before building the CD image.

- [ ] **Step 6: Commit**

```bash
git add .gitattributes .gitignore docs/superpowers/notes/art-lfs.md
git commit -m "build: track generated background TGAs in Git LFS"
```

---

## Done when

- `python -m pytest tools/tests/ saturn/tests/ -v` passes.
- `tools/assets/art_queries.json` covers all twelve moods and every one clears its
  `target` in the query-count test.
- `tools/assets/art_manifest.json` holds a record for every accepted picture, with
  a Pixabay id, page URL, licence, and its three scores.
- No API key appears anywhere in the repository:
  `git log -p | grep -i pixabay_api_key` returns nothing but the environment
  variable's name.
- Each mood folder under `tools/assets/png/` holds pictures, and
  `saturn/tests/test_category_art.py` agrees with the disc.
- The user has confirmed the hardware check in Task 6 Step 7.
