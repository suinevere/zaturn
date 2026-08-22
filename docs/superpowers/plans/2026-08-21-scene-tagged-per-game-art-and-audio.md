# Scene-Tagged Per-Game Art and Audio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the keyword mood classifier with per-game room-object-to-scene tables authored offline, and move background art from shared mood folders to one flat folder per game.

**Architecture:** A room is identified by `(release, serial, object_number)` — all three already available at `mojozork.c:1322`. An offline Python pipeline decodes every story's object table, decides what it can by ordered title rules, refuses the rest into a human review queue that carries the room's description, and generates three C tables. At runtime the engine does three array lookups and no text analysis. Art moves to `GAME/NN.TGA`, with each scene owning an index range inside that game's `1..99`.

**Tech Stack:** Python 3.13 (`tools/.venv`), pytest, Flask (review server), C99 (SH-2 target, host gcc for tests), Z-machine v3 story files.

**Spec:** `docs/superpowers/specs/2026-08-21-scene-tagged-per-game-art-and-audio-design.md`

## Global Constraints

- **Never run `compile.bat`, `compile-cd.bat`, `make`, or the emulator.** The owner runs all builds. To check C changes, cross-compile the changed unit with `sh saturn/syntax-check.sh <file>` (writes no objects) or build the host test binary with `gcc`.
- **Never use `git stash`.** Curation state has been lost to a failed stash pop before. Commit or leave dirty.
- **Commit messages are one sentence.** No body, no bullets, no trailers. Never mention Claude, AI, or the session; never add a `Claude-Session:` line or a `claude.ai/code` URL.
- **Comment style:** every file, function and constant gets the `/*---- | name | Description: ... | Author: suinevere | Dependencies: | Globals: | Params: | Returns: ----*/` header block. `N/A` for fields that do not apply. **No comments inside function bodies.** Tests and generated files get a file header only.
- **Author of record is `suinevere`.**
- **Project layout:** the entry point is the only file in `src/` root; everything else lives in a subfolder named for its concern.
- **Never print via `SRL::Debug`** — `text_map.h` is the only text path.
- **Scene vocabulary is 32 entries and the Python list and the C enum must agree in membership AND order.** The enum value is a table index.
- **Every generated `.inc` must be byte-identical on regeneration** from an unchanged input tree.
- **A game may not exceed 99 images.**

---

## File Structure

**New — Python (`tools/`)**
- `scene_vocab.py` — the 32 scenes and the ordered title rules. Single source of truth; imported by the generator, the review server, and `art_nouns.py`.
- `gen_room_inventory.py` — decodes every story to `tools/assets/rooms/<STEM>.json`.
- `room_scenes.py` — rule pass, review-queue writer, blessed-verdict merge.
- `gen_scene_tables.py` — emits `game_rooms.inc` and `game_tracks.inc`.
- `scene_server.py` — Flask review UI.

**New — C (`saturn/src/scene/`)**
- `scene_map.h` / `scene_map.c` — `SC_*` enum, `scene_of_room`, `scene_game_index`.
- `game_rooms.inc`, `game_scenes.inc`, `game_tracks.inc` — generated.

**New — C (`saturn/src/sound/`)**
- `event_scan.h` / `event_scan.c` — `text_scan_event` and its two keyword tables, lifted out of the classifier.

**Modified**
- `tools/art_nouns.py`, `tools/make_tga.py`
- `saturn/src/sound/music.{h,c}`, `music_data.c`
- `saturn/src/video/display.{h,c}`
- `saturn/src/main.cxx`, `saturn/mojozork.c`

**Deleted**
- `saturn/src/classify/room_class.{c,h}`, `room_class_data.c`
- `test/room_class_test.c`, `test/corpus/blessed.inc`
- `saturn/src/video/category_art.inc`, `saturn/tests/test_room_genre.c`

**Scope of this plan:** all the code, plus **Zork I** blessed and on disc end to end. The other 30 games are data entry through these same tools and add no code.

---

### Task 1: Stop the manifest losing curation

**Files:**
- Modify: `.gitignore`
- Create: `tools/assets/art_manifest.snapshot.json`
- Test: `tools/tests/test_manifest_snapshot.py`

**Interfaces:**
- Consumes: nothing.
- Produces: `tools/art_status.py --snapshot` writes `tools/assets/art_manifest.snapshot.json`.

This is first because it protects every later task. `art_manifest.json` is tracked today, and being tracked has silently reverted curation twice — once to a failed stash pop, once to a branch switch — orphaning 365 images that exist on disk. This plan churns that manifest harder than anything before it.

- [ ] **Step 1: Write the failing test**

```python
# tools/tests/test_manifest_snapshot.py
"""Gate that the working manifest is untracked and the snapshot is not."""
import json
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def _tracked(rel):
    out = subprocess.run(["git", "ls-files", "--error-unmatch", rel],
                         cwd=REPO, capture_output=True, text=True)
    return out.returncode == 0


def test_working_manifest_is_not_tracked():
    assert not _tracked("tools/assets/art_manifest.json")


def test_snapshot_is_tracked():
    assert _tracked("tools/assets/art_manifest.snapshot.json")


def test_snapshot_is_valid_json_with_records():
    snap = REPO / "tools" / "assets" / "art_manifest.snapshot.json"
    data = json.loads(snap.read_text(encoding="utf-8"))
    assert isinstance(data, dict)
    assert "records" in data
```

- [ ] **Step 2: Run test to verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_manifest_snapshot.py -v`
Expected: FAIL — `test_working_manifest_is_not_tracked` fails (it IS tracked) and the snapshot file does not exist.

- [ ] **Step 3: Untrack the manifest and write the snapshot**

```bash
cp tools/assets/art_manifest.json tools/assets/art_manifest.snapshot.json
git rm --cached tools/assets/art_manifest.json
printf '\n# Working art curation state. Local only; the committed record is\n# art_manifest.snapshot.json, written at promote time by art_status.py --snapshot.\ntools/assets/art_manifest.json\ntools/assets/art_manifest.json.bak*\ntools/assets/art_manifest.json.reverted-*\n' >> .gitignore
```

- [ ] **Step 4: Add the snapshot writer to `tools/art_status.py`**

```python
def write_snapshot(repo=None):
    """/*----------------------
     | write_snapshot
     | Description: Copies the working manifest to the committed snapshot, which
     |   is the record of curation at the moment images reached the disc.
     | Author: suinevere
     | Dependencies: json, pathlib
     | Globals: N/A
     | Params: repo -- repo root; defaults to the one containing this file
     | Returns: the snapshot path
     ----------------------*/"""
    root = pathlib.Path(repo) if repo else pathlib.Path(__file__).resolve().parent.parent
    live = root / "tools" / "assets" / "art_manifest.json"
    snap = root / "tools" / "assets" / "art_manifest.snapshot.json"
    snap.write_text(live.read_text(encoding="utf-8"), encoding="utf-8")
    return snap
```

Wire it to a `--snapshot` flag in that file's existing `main(argv)`.

- [ ] **Step 5: Run tests to verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_manifest_snapshot.py -v`
Expected: 3 passed.

- [ ] **Step 6: Commit**

```bash
git add .gitignore tools/assets/art_manifest.snapshot.json tools/art_status.py tools/tests/test_manifest_snapshot.py
git commit -m "assets: untrack the working art manifest and commit a snapshot instead"
```

---

### Task 2: The scene vocabulary and title rules

**Files:**
- Create: `tools/scene_vocab.py`
- Test: `tools/tests/test_scene_vocab.py`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `SCENES: tuple[str, ...]` — 32 names, **enum order, never reordered**.
  - `SCENE_INDEX: dict[str, int]` — name to 0-based index.
  - `RULES: tuple[tuple[str, str], ...]` — ordered `(lowercase_substring, scene_name)`; first match wins.
  - `scene_for_title(title: str) -> str | None` — the scene, or `None` when no rule matches.
  - `FETCH_NOUNS: dict[str, tuple[str, ...]]` — scene to stock-photo search nouns.

- [ ] **Step 1: Write the failing test**

```python
# tools/tests/test_scene_vocab.py
"""The vocabulary is a table index, so order and membership are load-bearing."""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import scene_vocab as v


def test_thirty_two_scenes():
    assert len(v.SCENES) == 32


def test_names_are_unique():
    assert len(set(v.SCENES)) == len(v.SCENES)


def test_index_matches_position():
    for i, name in enumerate(v.SCENES):
        assert v.SCENE_INDEX[name] == i


def test_every_rule_names_a_real_scene():
    for pattern, scene in v.RULES:
        assert scene in v.SCENE_INDEX, f"{pattern} -> unknown scene {scene}"


def test_rule_patterns_are_lowercase():
    for pattern, _ in v.RULES:
        assert pattern == pattern.lower()


def test_plain_titles_resolve():
    assert v.scene_for_title("Forest") == "FOREST"
    assert v.scene_for_title("Sandy Beach") == "SHORE"
    assert v.scene_for_title("Kitchen") == "KITCHEN"
    assert v.scene_for_title("Maze") == "MAZE"


def test_priority_cases_resolve_to_the_more_specific_noun():
    assert v.scene_for_title("Shore Road") == "ROAD"
    assert v.scene_for_title("Wharf Road") == "ROAD"
    assert v.scene_for_title("Ocean Road") == "ROAD"
    assert v.scene_for_title("Upstairs Hallway") == "CORRIDOR"
    assert v.scene_for_title("Upstairs Closet") == "DARKROOM"
    assert v.scene_for_title("Hall of the Mountain King") == "CORRIDOR"


def test_shape_titles_refuse():
    for t in ("Dead End", "Cube", "Oddly-angled Room", "Land of Shadow",
              "The Troll Room", "Mirror Room", "Studio"):
        assert v.scene_for_title(t) is None, t


def test_every_scene_has_fetch_nouns():
    for name in v.SCENES:
        assert v.FETCH_NOUNS.get(name), name


def test_fetch_nouns_carry_no_dead_adjectives():
    dead = {"torchlit", "bustling", "creaking", "moored", "wooden",
            "dimly lit", "eerie", "foreboding", "ominous"}
    for name, nouns in v.FETCH_NOUNS.items():
        for n in nouns:
            assert n not in dead, f"{name}: {n} scored 0 in 589d6c5"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_scene_vocab.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'scene_vocab'`.

- [ ] **Step 3: Write the vocabulary**

```python
#!/usr/bin/env python3
"""Scene vocabulary and title rules.

Description: The 32 scenes a room can be tagged with, the ordered title rules
    that decide the obvious ones, and the stock-photo nouns each scene is
    fetched with. Single source of truth: the C enum in
    saturn/src/scene/scene_map.h is generated against SCENES, and
    tools/art_nouns.py derives its query vocabulary from FETCH_NOUNS.

    SCENES order is a table index in three generated C tables. Appending is
    safe; reordering silently repoints every row.

    RULES is ordered and first-match-wins, so priority is expressed by position
    rather than by weights. "Shore Road" is a road, so `road` precedes `shore`.

    Every noun in FETCH_NOUNS is a word a photographer types. 589d6c5 measured
    that experience adjectives never return a keeper -- `torchlit` 0/14,
    `bustling` 0/12, against `misty` 85% -- and a guard test carries the dead
    list by name.
Author: suinevere
Dependencies: N/A
Globals: SCENES, SCENE_INDEX, RULES, FETCH_NOUNS
"""

SCENES = (
    "FOREST", "GARDEN", "DESERT", "ROCKY", "SHORE", "RIVER", "ROAD",
    "CAVE", "MAZE", "MINE", "PIT", "CRYPT",
    "HOUSE_EXT", "VILLAGE", "CASTLE", "DOCK",
    "PARLOR", "KITCHEN", "BEDROOM", "BATHROOM", "LIBRARY", "DARKROOM",
    "CORRIDOR", "OFFICE", "LAB", "STORAGE", "CELL", "THEATER",
    "TEMPLE",
    "SHIP_EXT", "SHIP_INT", "SPACE",
)

SCENE_INDEX = {name: i for i, name in enumerate(SCENES)}

RULES = (
    ("dead end", None),
    ("road", "ROAD"), ("driveway", "ROAD"), ("street", "ROAD"),
    ("trail", "ROAD"), ("path", "ROAD"),
    ("closet", "DARKROOM"), ("cupboard", "DARKROOM"), ("wardrobe", "DARKROOM"),
    ("attic", "DARKROOM"), ("garret", "DARKROOM"), ("cellar", "DARKROOM"),
    ("basement", "DARKROOM"),
    ("hallway", "CORRIDOR"), ("corridor", "CORRIDOR"), ("staircase", "CORRIDOR"),
    ("stairway", "CORRIDOR"), ("stairs", "CORRIDOR"), ("landing", "CORRIDOR"),
    ("hall", "CORRIDOR"),
    ("of house", "HOUSE_EXT"), ("outside house", "HOUSE_EXT"),
    ("behind house", "HOUSE_EXT"), ("porch", "HOUSE_EXT"), ("tent", "HOUSE_EXT"),
    ("forest", "FOREST"), ("woods", "FOREST"), ("grove", "FOREST"),
    ("thicket", "FOREST"),
    ("garden", "GARDEN"), ("orchard", "GARDEN"), ("courtyard", "GARDEN"),
    ("desert", "DESERT"), ("dune", "DESERT"), ("oasis", "DESERT"),
    ("canyon", "ROCKY"), ("gorge", "ROCKY"), ("ravine", "ROCKY"),
    ("cliff", "ROCKY"), ("ledge", "ROCKY"), ("precipice", "ROCKY"),
    ("bluff", "ROCKY"), ("mountain", "ROCKY"), ("volcano", "ROCKY"),
    ("summit", "ROCKY"), ("peak", "ROCKY"),
    ("beach", "SHORE"), ("shore", "SHORE"), ("ocean", "SHORE"),
    ("sea", "SHORE"), ("lake", "SHORE"), ("pond", "SHORE"),
    ("reservoir", "SHORE"),
    ("river", "RIVER"), ("stream", "RIVER"), ("brook", "RIVER"),
    ("creek", "RIVER"), ("falls", "RIVER"), ("rapids", "RIVER"),
    ("cavern", "CAVE"), ("cave", "CAVE"), ("grotto", "CAVE"),
    ("tunnel", "CAVE"), ("passage", "CAVE"), ("crawl", "CAVE"),
    ("labyrinth", "MAZE"), ("maze", "MAZE"),
    ("quarry", "MINE"), ("mine", "MINE"), ("shaft", "MINE"),
    ("chasm", "PIT"), ("abyss", "PIT"), ("crevice", "PIT"), ("pit", "PIT"),
    ("catacomb", "CRYPT"), ("mausoleum", "CRYPT"), ("crypt", "CRYPT"),
    ("tomb", "CRYPT"),
    ("village", "VILLAGE"), ("town", "VILLAGE"), ("plaza", "VILLAGE"),
    ("square", "VILLAGE"),
    ("castle", "CASTLE"), ("fortress", "CASTLE"), ("tower", "CASTLE"),
    ("ruin", "CASTLE"),
    ("wharf", "DOCK"), ("dock", "DOCK"), ("pier", "DOCK"),
    ("harbour", "DOCK"), ("harbor", "DOCK"), ("quay", "DOCK"),
    ("living room", "PARLOR"), ("sitting room", "PARLOR"), ("parlour", "PARLOR"),
    ("parlor", "PARLOR"), ("lounge", "PARLOR"), ("dining", "PARLOR"),
    ("foyer", "PARLOR"),
    ("kitchen", "KITCHEN"), ("pantry", "KITCHEN"), ("galley", "KITCHEN"),
    ("bedroom", "BEDROOM"), ("bunk", "BEDROOM"),
    ("bathroom", "BATHROOM"), ("washroom", "BATHROOM"),
    ("lavatory", "BATHROOM"), ("restroom", "BATHROOM"),
    ("library", "LIBRARY"), ("study", "LIBRARY"),
    ("laboratory", "LAB"), ("lab", "LAB"),
    ("office", "OFFICE"), ("cubicle", "OFFICE"),
    ("storeroom", "STORAGE"), ("storage", "STORAGE"),
    ("warehouse", "STORAGE"), ("supply", "STORAGE"),
    ("dungeon", "CELL"), ("prison", "CELL"), ("jail", "CELL"), ("cell", "CELL"),
    ("theatre", "THEATER"), ("theater", "THEATER"),
    ("auditorium", "THEATER"), ("stage", "THEATER"),
    ("cathedral", "TEMPLE"), ("chapel", "TEMPLE"), ("church", "TEMPLE"),
    ("temple", "TEMPLE"), ("shrine", "TEMPLE"), ("altar", "TEMPLE"),
    ("forecastle", "SHIP_EXT"), ("deck", "SHIP_EXT"),
    ("stateroom", "SHIP_INT"), ("cabin", "SHIP_INT"), ("berth", "SHIP_INT"),
    ("engine", "SHIP_INT"), ("boiler", "SHIP_INT"), ("reactor", "SHIP_INT"),
    ("bridge", "SHIP_INT"),
    ("airlock", "SPACE"), ("orbit", "SPACE"), ("space", "SPACE"),
)

FETCH_NOUNS = {
    "FOREST":    ("forest", "woodland", "pine grove", "birch woods"),
    "GARDEN":    ("garden", "orchard", "courtyard", "hedge"),
    "DESERT":    ("desert", "sand dune", "oasis", "arid plain"),
    "ROCKY":     ("canyon", "cliff", "rock ledge", "mountain"),
    "SHORE":     ("beach", "rocky shore", "ocean", "lake"),
    "RIVER":     ("river", "stream", "waterfall", "rapids"),
    "ROAD":      ("dirt road", "country lane", "cobbled street", "footpath"),
    "CAVE":      ("cave", "cavern", "rock tunnel", "grotto"),
    "MAZE":      ("stone maze", "hedge maze", "labyrinth"),
    "MINE":      ("mine tunnel", "mine shaft", "quarry"),
    "PIT":       ("chasm", "crevasse", "deep pit"),
    "CRYPT":     ("crypt", "catacomb", "stone tomb"),
    "HOUSE_EXT": ("cottage", "farmhouse", "clapboard house", "canvas tent"),
    "VILLAGE":   ("village street", "old town square", "market square"),
    "CASTLE":    ("castle", "stone tower", "ruined fortress"),
    "DOCK":      ("wharf", "wooden pier", "harbour", "quay"),
    "PARLOR":    ("parlour", "sitting room", "dining room", "entrance hall"),
    "KITCHEN":   ("kitchen", "pantry", "ship galley"),
    "BEDROOM":   ("bedroom", "bunk room"),
    "BATHROOM":  ("bathroom", "washroom"),
    "LIBRARY":   ("library", "bookshelves", "study desk"),
    "DARKROOM":  ("attic", "cellar", "cluttered closet"),
    "CORRIDOR":  ("stone corridor", "narrow hallway", "wooden staircase"),
    "OFFICE":    ("vintage office", "desk", "filing cabinet"),
    "LAB":       ("laboratory", "control room", "instrument panel"),
    "STORAGE":   ("storeroom", "warehouse", "crates", "shelving"),
    "CELL":      ("prison cell", "dungeon", "iron bars"),
    "THEATER":   ("theatre stage", "auditorium", "empty theatre"),
    "TEMPLE":    ("temple", "stone altar", "chapel", "shrine"),
    "SHIP_EXT":  ("ship deck", "sailing ship", "bow of ship"),
    "SHIP_INT":  ("ship cabin", "engine room", "ship bridge"),
    "SPACE":     ("airlock", "earth from orbit", "starfield"),
}


def scene_for_title(title):
    """/*----------------------
     | scene_for_title
     | Description: The scene a room title names, or None when no rule matches.
     |   First match in RULES wins, so ordering is the priority mechanism. A rule
     |   whose scene is None is an explicit refusal -- "Dead End" names a shape,
     |   and matching it early stops "end" like patterns claiming it later.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: RULES
     | Params: title -- a room's short name
     | Returns: a scene name, or None
     ----------------------*/"""
    t = title.lower()
    for pattern, scene in RULES:
        if pattern in t:
            return scene
    return None
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_scene_vocab.py -v`
Expected: 10 passed.

- [ ] **Step 5: Commit**

```bash
git add tools/scene_vocab.py tools/tests/test_scene_vocab.py
git commit -m "tools: add the scene vocabulary and its ordered title rules"
```

---

### Task 3: Room inventory, static pass

**Files:**
- Create: `tools/gen_room_inventory.py`
- Test: `tools/tests/test_room_inventory.py`

**Interfaces:**
- Consumes: `tools/zstory.py` (`Story`), `tools/gen_room_corpus.py`
  (`find_rooms_hub`, `detect_description_property`, `derive_direction_props`,
  `decode_description`).
- Produces:
  - `static_rooms(story) -> list[dict]` with keys `obj` (int), `title` (str), `description` (str or None).
  - `inventory_for(path) -> dict` with keys `story`, `release`, `serial`, `desc_prop`, `rooms`.
  - Writes `tools/assets/rooms/<STEM>.json`.

The object number is the whole point of this pass — `gen_room_corpus.py` keys by title and cannot distinguish Zork I's fifteen `Maze` rooms.

- [ ] **Step 1: Write the failing test**

```python
# tools/tests/test_room_inventory.py
"""Static extraction must recover object numbers, not just titles."""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import gen_room_inventory as inv

Z3 = REPO / "tools" / "assets" / "Z3"


def test_zork1_room_count_and_identity():
    d = inv.inventory_for(Z3 / "ZORK1.Z3")
    assert d["release"] == 88
    assert d["serial"] == "840726"
    assert d["count"] == 110


def test_object_numbers_are_unique_and_in_v3_range():
    d = inv.inventory_for(Z3 / "ZORK1.Z3")
    objs = [r["obj"] for r in d["rooms"]]
    assert len(set(objs)) == len(objs)
    assert all(1 <= o <= 255 for o in objs)


def test_duplicate_titles_are_separate_rows():
    d = inv.inventory_for(Z3 / "ZORK1.Z3")
    mazes = [r for r in d["rooms"] if r["title"] == "Maze"]
    assert len(mazes) == 15
    assert len({r["obj"] for r in mazes}) == 15


def test_routine_described_rooms_have_no_static_description():
    d = inv.inventory_for(Z3 / "ZORK1.Z3")
    by_title = {r["title"]: r for r in d["rooms"] if r["title"] == "West of House"}
    assert by_title["West of House"]["description"] is None


def test_stored_descriptions_are_prose():
    d = inv.inventory_for(Z3 / "ZORK1.Z3")
    attic = next(r for r in d["rooms"] if r["title"] == "Attic")
    assert attic["description"] and attic["description"][0].isupper()


def test_rows_are_sorted_by_object_number_for_determinism():
    d = inv.inventory_for(Z3 / "ZORK1.Z3")
    objs = [r["obj"] for r in d["rooms"]]
    assert objs == sorted(objs)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_room_inventory.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'gen_room_inventory'`.

- [ ] **Step 3: Write the static pass**

```python
#!/usr/bin/env python3
"""Decode every story's rooms, keyed by object number.

Description: The primary source for scene tagging. gen_room_corpus.py already
    decodes titles and descriptions, but it keys by title and dedupes on it,
    which cannot distinguish Zork I's fifteen "Maze" rooms from one another.
    This module keeps the object number, which is what the runtime looks a room
    up by (mojozork.c reads it from global 0x10).

    Room-shaped means "a child of the rooms hub that carries at least one
    direction property" -- find_rooms_hub's judgement, reused rather than
    re-derived. A room whose description property holds a routine address
    decodes to garbage and is recorded with description None rather than
    dropped; those rooms are exactly the ones the runtime pass exists to reach.

    Deterministic by construction: it reads bytes and sorts by object number.
Author: suinevere
Dependencies: json, pathlib, sys, zstory, gen_room_corpus
Globals: ROOT, Z3, OUT
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import gen_room_corpus as corpus
import zstory

ROOT = pathlib.Path(__file__).resolve().parent.parent
Z3 = ROOT / "tools" / "assets" / "Z3"
OUT = ROOT / "tools" / "assets" / "rooms"


def static_rooms(story):
    """/*----------------------
     | static_rooms
     | Description: Every room-shaped object of `story` as {obj, title,
     |   description}, sorted by object number. description is None when the
     |   property holds a routine rather than a stored string.
     | Author: suinevere
     | Dependencies: gen_room_corpus
     | Globals: N/A
     | Params: story -- a zstory.Story
     | Returns: (rows, desc_prop)
     ----------------------*/"""
    hub, children = corpus.find_rooms_hub(story)
    if hub is None:
        return [], 0
    desc_prop, _ = corpus.detect_description_property(story, children)
    direction_props = corpus.derive_direction_props(children, desc_prop)
    rows = []
    for obj in children:
        if not obj.properties.keys() & direction_props:
            continue
        title = obj.name.strip()
        if not title or len(title) > corpus.TITLE_MAX:
            continue
        rows.append({
            "obj": obj.number,
            "title": title,
            "description": corpus.decode_description(story, obj, desc_prop),
        })
    rows.sort(key=lambda r: r["obj"])
    return rows, desc_prop


def inventory_for(path):
    """/*----------------------
     | inventory_for
     | Description: One story's full inventory record, ready to serialise.
     | Author: suinevere
     | Dependencies: zstory
     | Globals: N/A
     | Params: path -- a .Z3 story file
     | Returns: dict with story, release, serial, desc_prop, count, rooms
     ----------------------*/"""
    story = zstory.Story(path)
    rows, desc_prop = static_rooms(story)
    return {
        "story": pathlib.Path(path).name,
        "release": story.release,
        "serial": story.serial,
        "desc_prop": desc_prop,
        "count": len(rows),
        "rooms": rows,
    }


def main(argv):
    """/*----------------------
     | main
     | Description: Writes one JSON inventory per story into tools/assets/rooms.
     | Author: suinevere
     | Dependencies: json
     | Globals: Z3, OUT
     | Params: argv -- unused
     | Returns: 0
     ----------------------*/"""
    OUT.mkdir(parents=True, exist_ok=True)
    for path in sorted(Z3.glob("*.Z3")):
        d = inventory_for(path)
        dst = OUT / (path.stem + ".json")
        dst.write_text(json.dumps(d, indent=1, sort_keys=True) + "\n",
                       encoding="utf-8")
        print(f"{path.name:14} {d['count']:4} rooms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
```

If `zstory.Story` objects do not expose `.number`, add it: the object index is
already the loop counter when the table is parsed, so store it on the object as
it is built.

- [ ] **Step 4: Run tests to verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_room_inventory.py -v`
Expected: 6 passed.

- [ ] **Step 5: Generate the inventories**

Run: `tools/.venv/Scripts/python.exe tools/gen_room_inventory.py`
Expected: 31 lines, `ZORK1.Z3` reporting 110 rooms, `SPLBRKR.Z3` reporting 7.

- [ ] **Step 6: Commit**

```bash
git add tools/gen_room_inventory.py tools/tests/test_room_inventory.py tools/assets/rooms/
git commit -m "tools: decode every story's rooms keyed by object number"
```

---

### Task 4: Room inventory, runtime description pass

**Files:**
- Modify: `tools/gen_room_inventory.py`
- Test: `tools/tests/test_room_inventory_runtime.py`

**Interfaces:**
- Consumes: Task 3's `inventory_for`; `gen_room_corpus.build_mojozork`, `run`, `rooms_from`.
- Produces: `merge_runtime(inv, captured) -> dict` — fills `description` for rows whose static description is `None`, and sets `source` to `"static"`, `"runtime"` or `None` on every row.

405 of the 990 rooms that need human review have no stored description. This
pass is the only way to see what those rooms print.

- [ ] **Step 1: Write the failing test**

```python
# tools/tests/test_room_inventory_runtime.py
"""Runtime capture fills descriptions static decoding cannot reach."""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import gen_room_inventory as inv


def _inv(rows):
    return {"story": "T.Z3", "release": 1, "serial": "000000",
            "desc_prop": 11, "count": len(rows), "rooms": rows}


def test_runtime_text_fills_a_missing_description():
    d = _inv([{"obj": 5, "title": "Cellar", "description": None}])
    out = inv.merge_runtime(d, {"Cellar": "You are in a dark and damp cellar."})
    assert out["rooms"][0]["description"] == "You are in a dark and damp cellar."
    assert out["rooms"][0]["source"] == "runtime"


def test_static_text_wins_over_runtime():
    d = _inv([{"obj": 5, "title": "Attic", "description": "A dusty attic."}])
    out = inv.merge_runtime(d, {"Attic": "Something else entirely."})
    assert out["rooms"][0]["description"] == "A dusty attic."
    assert out["rooms"][0]["source"] == "static"


def test_duplicate_titles_all_receive_the_capture():
    d = _inv([{"obj": 5, "title": "Maze", "description": None},
              {"obj": 6, "title": "Maze", "description": None}])
    out = inv.merge_runtime(d, {"Maze": "This is a maze of twisty passages."})
    assert all(r["description"].startswith("This is a maze") for r in out["rooms"])


def test_unreached_room_keeps_none_and_null_source():
    d = _inv([{"obj": 5, "title": "Cube", "description": None}])
    out = inv.merge_runtime(d, {})
    assert out["rooms"][0]["description"] is None
    assert out["rooms"][0]["source"] is None
```

- [ ] **Step 2: Run test to verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_room_inventory_runtime.py -v`
Expected: FAIL — `AttributeError: module 'gen_room_inventory' has no attribute 'merge_runtime'`.

- [ ] **Step 3: Add the merge**

```python
def merge_runtime(inv, captured):
    """/*----------------------
     | merge_runtime
     | Description: Fills descriptions the static pass could not decode from a
     |   {title: text} map captured by driving the interpreter, and stamps every
     |   row's source. Static wins on a collision: it is the string the story
     |   actually stores, while a capture is one particular visit under one
     |   particular game state.
     |
     |   A capture attaches to EVERY row sharing that title. Runtime output
     |   carries no object number, so a duplicated title cannot be resolved to
     |   one object -- and does not need to be, because rooms are duplicated
     |   precisely when they are the same place repeated.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: inv -- an inventory dict; captured -- {title: description}
     | Returns: the same dict, rows mutated in place
     ----------------------*/"""
    for row in inv["rooms"]:
        if row["description"]:
            row["source"] = "static"
            continue
        text = captured.get(row["title"])
        if text:
            row["description"] = text
            row["source"] = "runtime"
        else:
            row["source"] = None
    return inv
```

Then in `main`, after `inventory_for`, drive the interpreter for stories that
have a walkthrough under `tools/typeahead/solutions/<STEM>.WIN`, always
appending `tools/wander.txt`, exactly as `gen_room_corpus.main` already does,
and pass the resulting `{title: body}` map to `merge_runtime`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_room_inventory_runtime.py -v`
Expected: 4 passed.

- [ ] **Step 5: Regenerate and check coverage improved**

Run: `tools/.venv/Scripts/python.exe tools/gen_room_inventory.py`
Expected: Zork I still reports 110 rooms, and fewer than 23 of them now have a
null description.

- [ ] **Step 6: Commit**

```bash
git add tools/gen_room_inventory.py tools/tests/test_room_inventory_runtime.py tools/assets/rooms/
git commit -m "tools: fill routine-computed room descriptions from a runtime capture pass"
```

---

### Task 5: Rule pass, review queue, and the precedence rule

**Files:**
- Create: `tools/room_scenes.py`
- Test: `tools/tests/test_room_scenes.py`

**Interfaces:**
- Consumes: `scene_vocab.scene_for_title`, `SCENE_INDEX`; Task 3/4's inventory JSON.
- Produces:
  - `decide(rooms) -> (decided: dict[int, str], refused: list[dict])`
  - `merge(existing_blessed, decided, refused) -> (blessed: dict[int, str], review: list[dict])`
  - Writes `tools/assets/scenes/<STEM>.json` and `tools/assets/scenes/<STEM>.review.json`.

The precedence rule is the load-bearing part: **a human verdict is never
overwritten by a rule re-run.** This is what lets the vocabulary keep changing
without discarding review work.

- [ ] **Step 1: Write the failing test**

```python
# tools/tests/test_room_scenes.py
"""Rules decide what they can; humans own everything they have ruled on."""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import room_scenes as rs


ROOMS = [
    {"obj": 1, "title": "Forest", "description": "Trees.", "source": "static"},
    {"obj": 2, "title": "Dead End", "description": "A dead end.", "source": "static"},
    {"obj": 3, "title": "Cube", "description": None, "source": None},
]


def test_rules_decide_what_they_can():
    decided, refused = rs.decide(ROOMS)
    assert decided == {1: "FOREST"}


def test_refusals_carry_the_description():
    _, refused = rs.decide(ROOMS)
    titles = {r["title"]: r for r in refused}
    assert set(titles) == {"Dead End", "Cube"}
    assert titles["Dead End"]["description"] == "A dead end."


def test_human_verdict_survives_a_rule_rerun():
    blessed, review = rs.merge({2: "MAZE"}, {1: "FOREST"}, [
        {"obj": 2, "title": "Dead End", "description": "A dead end."},
        {"obj": 3, "title": "Cube", "description": None},
    ])
    assert blessed[2] == "MAZE"
    assert [r["obj"] for r in review] == [3]


def test_human_verdict_beats_a_conflicting_rule():
    blessed, _ = rs.merge({1: "MAZE"}, {1: "FOREST"}, [])
    assert blessed[1] == "MAZE"


def test_review_is_sorted_by_object_for_determinism():
    _, review = rs.merge({}, {}, [
        {"obj": 9, "title": "B", "description": None},
        {"obj": 3, "title": "A", "description": None},
    ])
    assert [r["obj"] for r in review] == [3, 9]


def test_blessed_scenes_must_be_in_the_vocabulary():
    import pytest
    with pytest.raises(ValueError):
        rs.merge({1: "NOT_A_SCENE"}, {}, [])


def test_identical_titles_group_into_one_review_entry():
    _, review = rs.merge({}, {}, [
        {"obj": 5, "title": "Maze", "description": "Twisty."},
        {"obj": 6, "title": "Maze", "description": "Twisty."},
    ])
    assert len(review) == 1
    assert review[0]["objs"] == [5, 6]
```

- [ ] **Step 2: Run test to verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_room_scenes.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'room_scenes'`.

- [ ] **Step 3: Write the rule pass and merge**

```python
#!/usr/bin/env python3
"""Decide room scenes by rule, refuse the rest into a review queue.

Description: Stage two and three of the tagging pipeline. decide() runs the
    ordered title rules over one story's inventory and splits it into what the
    rules settle and what they refuse. merge() folds a fresh rule pass into the
    verdicts a human has already given.

    The precedence rule: a human verdict is authoritative and is never
    overwritten by a rule re-run. Editing the vocabulary re-decides only rooms
    nobody has ruled on, so the review work survives every change to the rules.
    The alternative -- regenerating wholesale -- is how 365 curated images came
    to be orphaned from their manifest.

    Refusal is the designed output of a rule that does not match, not a failure.
    A title naming a shape ("Dead End", "Cube", "Oddly-angled Room") cannot be
    resolved from the title, and guessing is worse than asking.
Author: suinevere
Dependencies: json, pathlib, sys, scene_vocab
Globals: ROOT, ROOMS, SCENES_DIR
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import scene_vocab as vocab

ROOT = pathlib.Path(__file__).resolve().parent.parent
ROOMS = ROOT / "tools" / "assets" / "rooms"
SCENES_DIR = ROOT / "tools" / "assets" / "scenes"


def decide(rooms):
    """/*----------------------
     | decide
     | Description: Splits one story's rooms into rule-decided and refused.
     | Author: suinevere
     | Dependencies: scene_vocab
     | Globals: N/A
     | Params: rooms -- inventory rows
     | Returns: (decided {obj: scene}, refused [row])
     ----------------------*/"""
    decided, refused = {}, []
    for row in rooms:
        scene = vocab.scene_for_title(row["title"])
        if scene:
            decided[row["obj"]] = scene
        else:
            refused.append(row)
    return decided, refused


def merge(existing_blessed, decided, refused):
    """/*----------------------
     | merge
     | Description: Folds a rule pass into existing human verdicts. Human wins
     |   everywhere. Refusals a human has already ruled on drop out of the queue;
     |   what remains is grouped by title so repeated rooms cost one decision.
     | Author: suinevere
     | Dependencies: scene_vocab
     | Globals: N/A
     | Params: existing_blessed -- {obj: scene} already ruled; decided -- {obj:
     |   scene} from the rules; refused -- rows the rules would not decide
     | Returns: (blessed {obj: scene}, review [{title, description, objs}])
     ----------------------*/"""
    for obj, scene in existing_blessed.items():
        if scene not in vocab.SCENE_INDEX:
            raise ValueError(f"object {obj}: {scene} is not in the vocabulary")

    blessed = dict(decided)
    blessed.update(existing_blessed)

    groups = {}
    for row in refused:
        if row["obj"] in existing_blessed:
            continue
        key = row["title"]
        g = groups.setdefault(key, {"title": key,
                                    "description": row.get("description"),
                                    "objs": []})
        g["objs"].append(row["obj"])
        if not g["description"] and row.get("description"):
            g["description"] = row["description"]

    review = sorted(groups.values(), key=lambda g: min(g["objs"]))
    for g in review:
        g["objs"].sort()
        g["obj"] = g["objs"][0]
    return blessed, review
```

Add a `main(argv)` that, per story, reads `tools/assets/rooms/<STEM>.json`,
loads any existing `tools/assets/scenes/<STEM>.json`, runs `decide` then
`merge`, and writes both files with
`json.dumps(..., indent=1, sort_keys=True)`.

**Key types.** JSON object keys are always strings, but `decide` and `merge`
work in `int` object numbers. `main` must convert on the way in and on the way
out, or a rule-decided `7` and a human-blessed `"7"` become two entries and the
human one stops winning:

```python
    existing = {int(k): v for k, v in json.loads(path.read_text()).items()}
    blessed, review = merge(existing, decided, refused)
    out = {str(k): v for k, v in sorted(blessed.items())}
```

`gen_scene_tables.room_bytes` calls `int(obj)` for the same reason, and
`scene_server` writes `str(o)`. Every reader tolerates both; every writer emits
strings.

- [ ] **Step 4: Run tests to verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_room_scenes.py -v`
Expected: 7 passed.

- [ ] **Step 5: Run the pass over the whole library and check the rate**

Run: `tools/.venv/Scripts/python.exe tools/room_scenes.py`
Expected: roughly 977 of 1967 rooms decided; Zork I reporting about 65 decided
and about 45 refused.

- [ ] **Step 6: Commit**

```bash
git add tools/room_scenes.py tools/tests/test_room_scenes.py tools/assets/scenes/
git commit -m "tools: decide room scenes by title rule and queue the refusals for review"
```

---

### Task 6: The review server

**Files:**
- Create: `tools/scene_server.py`
- Test: `tools/tests/test_scene_server.py`

**Interfaces:**
- Consumes: `scene_vocab.SCENES`, `room_scenes` file layout, `test/corpus/blessed.inc` (hint only).
- Produces: `create_app(repo=None) -> flask.Flask` with routes `/`, `/game/<stem>`, `/verdict` (POST), `/skip` (POST).

Modelled on `tools/art_server.py`, which is the established pattern in this
repo for human review loops.

- [ ] **Step 1: Write the failing test**

```python
# tools/tests/test_scene_server.py
"""The review loop: show a refusal, take a verdict, never lose one."""
import json
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import scene_server


@pytest.fixture
def app(tmp_path):
    scenes = tmp_path / "tools" / "assets" / "scenes"
    scenes.mkdir(parents=True)
    (scenes / "ZORK1.json").write_text(json.dumps({"1": "FOREST"}))
    (scenes / "ZORK1.review.json").write_text(json.dumps([
        {"obj": 7, "objs": [7, 8], "title": "Maze", "description": "Twisty."},
        {"obj": 9, "objs": [9], "title": "Cube", "description": None},
    ]))
    a = scene_server.create_app(tmp_path)
    a.config["TESTING"] = True
    return a


def test_index_lists_games_with_outstanding_reviews(app):
    r = app.test_client().get("/")
    assert r.status_code == 200
    assert b"ZORK1" in r.data


def test_game_page_shows_title_and_description(app):
    r = app.test_client().get("/game/ZORK1")
    assert b"Maze" in r.data
    assert b"Twisty." in r.data


def test_game_page_offers_every_scene(app):
    r = app.test_client().get("/game/ZORK1")
    for name in ("FOREST", "MAZE", "SHIP_INT", "SPACE"):
        assert name.encode() in r.data


def test_verdict_writes_every_object_in_the_group(app, tmp_path):
    c = app.test_client()
    c.post("/verdict", json={"story": "ZORK1", "obj": 7, "scene": "MAZE"})
    blessed = json.loads(
        (tmp_path / "tools" / "assets" / "scenes" / "ZORK1.json").read_text())
    assert blessed["7"] == "MAZE"
    assert blessed["8"] == "MAZE"


def test_verdict_removes_the_group_from_the_queue(app, tmp_path):
    c = app.test_client()
    c.post("/verdict", json={"story": "ZORK1", "obj": 7, "scene": "MAZE"})
    review = json.loads(
        (tmp_path / "tools" / "assets" / "scenes" / "ZORK1.review.json").read_text())
    assert [g["obj"] for g in review] == [9]


def test_verdict_rejects_a_scene_outside_the_vocabulary(app):
    r = app.test_client().post("/verdict",
                               json={"story": "ZORK1", "obj": 7, "scene": "NOPE"})
    assert r.status_code == 400


def test_existing_verdicts_are_never_dropped(app, tmp_path):
    c = app.test_client()
    c.post("/verdict", json={"story": "ZORK1", "obj": 9, "scene": "CAVE"})
    blessed = json.loads(
        (tmp_path / "tools" / "assets" / "scenes" / "ZORK1.json").read_text())
    assert blessed["1"] == "FOREST"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_scene_server.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'scene_server'`.

- [ ] **Step 3: Write the server**

```python
#!/usr/bin/env python3
"""Review server for rooms the title rules refused.

Description: The human half of the tagging pipeline, in the shape of
    tools/art_server.py. Shows one refused group at a time -- title, captured
    description, and the 32 scenes -- and writes the verdict to every object in
    the group, because a repeated title is a repeated place.

    Both JSON files are read-modify-written on every single verdict rather than
    held in memory and flushed at exit. A session's verdicts are exactly the
    kind of state this project has lost before, and a crash must cost at most
    the one decision in flight.
Author: suinevere
Dependencies: flask, json, pathlib, sys, scene_vocab
Globals: N/A
"""
import json
import pathlib
import sys

from flask import Flask, jsonify, render_template_string, request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import scene_vocab as vocab

PAGE = """<!doctype html><title>{{ stem }} — scenes</title>
<style>
 body{font:15px system-ui;margin:2rem;max-width:56rem}
 #desc{color:#444;line-height:1.5;margin:.5rem 0 1rem}
 .grid{display:grid;grid-template-columns:repeat(4,1fr);gap:.4rem}
 button{padding:.6rem;font:inherit;cursor:pointer}
 .hint{background:#ffd}
</style>
<h1>{{ stem }} <small id="left">{{ left }} left</small></h1>
<h2 id="title">{{ group.title if group else 'queue clear' }}</h2>
<div id="desc">{{ group.description or '(no description captured)' if group else '' }}</div>
<div class="grid">
{% for s in scenes %}<button onclick="verdict('{{ s }}')">{{ s }}</button>{% endfor %}
</div>
<p><button onclick="verdict(null)">Skip</button></p>
<script>
let obj = {{ group.obj if group else 'null' }};
async function verdict(scene) {
  if (obj === null) return;
  const r = await fetch(scene ? '/verdict' : '/skip', {
    method: 'POST', headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({story: '{{ stem }}', obj: obj, scene: scene})});
  if (!r.ok) { alert('rejected'); return; }
  const d = await r.json();
  obj = d.group ? d.group.obj : null;
  document.getElementById('title').textContent = d.group ? d.group.title : 'queue clear';
  document.getElementById('desc').textContent =
      d.group ? (d.group.description || '(no description captured)') : '';
  document.getElementById('left').textContent = d.left + ' left';
}
</script>"""


def create_app(repo=None):
    """/*----------------------
     | create_app
     | Description: The review app, rooted at `repo` so tests can point it at a
     |   temporary tree instead of the working copy.
     | Author: suinevere
     | Dependencies: flask, scene_vocab
     | Globals: N/A
     | Params: repo -- repo root; defaults to the one containing this file
     | Returns: a flask.Flask
     ----------------------*/"""
    root = pathlib.Path(repo) if repo else pathlib.Path(__file__).resolve().parent.parent
    scenes_dir = root / "tools" / "assets" / "scenes"
    app = Flask(__name__)

    def load(stem):
        b = scenes_dir / f"{stem}.json"
        r = scenes_dir / f"{stem}.review.json"
        return (json.loads(b.read_text(encoding="utf-8")) if b.exists() else {},
                json.loads(r.read_text(encoding="utf-8")) if r.exists() else [])

    def save(stem, blessed, review):
        (scenes_dir / f"{stem}.json").write_text(
            json.dumps(blessed, indent=1, sort_keys=True) + "\n", encoding="utf-8")
        (scenes_dir / f"{stem}.review.json").write_text(
            json.dumps(review, indent=1, sort_keys=True) + "\n", encoding="utf-8")

    def take(stem, obj, scene):
        blessed, review = load(stem)
        group = next((g for g in review if g["obj"] == obj), None)
        if group is None:
            return None, blessed, review
        if scene is not None:
            for o in group["objs"]:
                blessed[str(o)] = scene
        review = [g for g in review if g["obj"] != obj]
        save(stem, blessed, review)
        return group, blessed, review

    @app.route("/")
    def index():
        rows = sorted(p.stem.replace(".review", "")
                      for p in scenes_dir.glob("*.review.json"))
        return "<h1>scene review</h1>" + "".join(
            f'<p><a href="/game/{s}">{s}</a></p>' for s in rows)

    @app.route("/game/<stem>")
    def game(stem):
        _, review = load(stem)
        return render_template_string(PAGE, stem=stem, scenes=vocab.SCENES,
                                      group=review[0] if review else None,
                                      left=len(review))

    @app.route("/verdict", methods=["POST"])
    def verdict():
        d = request.get_json(force=True)
        if d.get("scene") not in vocab.SCENE_INDEX:
            return jsonify(error="unknown scene"), 400
        group, _, review = take(d["story"], d["obj"], d["scene"])
        if group is None:
            return jsonify(error="unknown group"), 404
        return jsonify(group=review[0] if review else None, left=len(review))

    @app.route("/skip", methods=["POST"])
    def skip():
        d = request.get_json(force=True)
        group, _, review = take(d["story"], d["obj"], None)
        if group is None:
            return jsonify(error="unknown group"), 404
        return jsonify(group=review[0] if review else None, left=len(review))

    return app


def main(argv):
    """/*----------------------
     | main
     | Description: Serves the review app on 8081; 8080 is the art server.
     | Author: suinevere
     | Dependencies: flask
     | Globals: N/A
     | Params: argv -- unused
     | Returns: 0
     ----------------------*/"""
    create_app().run(host="0.0.0.0", port=8081, debug=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
```

Once the tests pass, add the `blessed.inc` hint: where a room's title has an
entry there, render its old mood name beside the title and give that scene's
button the `hint` class. It is a mood not a scene and must **never** be
auto-applied, but it narrows 32 buttons to three or four.

- [ ] **Step 4: Run tests to verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_scene_server.py -v`
Expected: 7 passed.

- [ ] **Step 5: Commit**

```bash
git add tools/scene_server.py tools/tests/test_scene_server.py
git commit -m "tools: add the scene review server for clearing refused rooms"
```

---

### Task 7: Bless Zork I

**Files:**
- Modify: `tools/assets/scenes/ZORK1.json`, `tools/assets/scenes/ZORK1.review.json`

**Interfaces:**
- Consumes: Task 6's server.
- Produces: a `ZORK1.json` covering all 110 objects, and an empty `ZORK1.review.json`.

This is human work, not code. It is a task because everything downstream needs
its output.

- [ ] **Step 1: Start the server**

```bash
tools/.venv/Scripts/pythonw.exe tools/scene_server.py 2> scene_server.log &
```

Then open `http://localhost:8081/game/ZORK1`.

Do not run this with `python.exe` in the foreground — use `pythonw.exe` detached
with stderr redirected, the same recipe the art server uses.

- [ ] **Step 2: Clear the queue**

About 45 groups. Expected outcome, for a sanity check rather than as a target:
the fifteen `Maze` rooms and the five `Dead End` rooms land on `MAZE`;
`Cellar`, `East of Chasm`, `The Troll Room`, `Gallery`, `Studio` on `CAVE` or
`DARKROOM`; `Living Room` on `PARLOR`; `West of House`, `North of House`,
`South of House`, `Behind House` on `HOUSE_EXT`; `Up a Tree` on `FOREST`;
`Dam`, `Dam Base`, `Reservoir*` on `SHORE` or `RIVER`; `Torch Room`, `Dome
Room`, `Altar`, `Temple`, `Egyptian Room` on `TEMPLE` or `CRYPT`; `Land of the
Dead`, `Entrance to Hades` on `CRYPT`.

Leave genuinely undecidable rooms unassigned. Unassigned is a supported state.

- [ ] **Step 3: Verify completeness**

```bash
tools/.venv/Scripts/python.exe -c "
import json, pathlib
b = json.loads(pathlib.Path('tools/assets/scenes/ZORK1.json').read_text())
r = json.loads(pathlib.Path('tools/assets/scenes/ZORK1.review.json').read_text())
inv = json.loads(pathlib.Path('tools/assets/rooms/ZORK1.json').read_text())
print('blessed', len(b), 'of', inv['count'], 'rooms; queue', len(r))
print('scenes used:', sorted(set(b.values())))
"
```

Expected: queue 0, and 12–18 distinct scenes.

- [ ] **Step 4: Commit**

```bash
git add tools/assets/scenes/ZORK1.json tools/assets/scenes/ZORK1.review.json
git commit -m "assets: bless every Zork I room with a scene"
```

---

### Task 8: Generate the room and track tables

**Files:**
- Create: `tools/gen_scene_tables.py`
- Create: `saturn/src/scene/game_rooms.inc`, `saturn/src/scene/game_tracks.inc`
- Test: `tools/tests/test_gen_scene_tables.py`

**Interfaces:**
- Consumes: `scene_vocab.SCENES`, `tools/assets/scenes/<STEM>.json`, `tools/assets/rooms/<STEM>.json`.
- Produces:
  - `GAMES: list[tuple[str, int, str]]` — `(stem, release, serial)` in generated table order, sorted by stem.
  - Emits `GAME_ROOMS[]` / `GAME_ROOM_MAP[]` into `game_rooms.inc` and `SCENE_TRACKS[][]` into `game_tracks.inc`.

- [ ] **Step 1: Write the failing test**

```python
# tools/tests/test_gen_scene_tables.py
"""Generated C tables: correct, bounded, and byte-identical on regeneration."""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import gen_scene_tables as g
import scene_vocab as vocab

INC = REPO / "saturn" / "src" / "scene" / "game_rooms.inc"


def test_regeneration_is_byte_identical():
    before = INC.read_bytes()
    g.main([])
    assert INC.read_bytes() == before


def test_every_game_has_a_row():
    text = INC.read_text(encoding="utf-8")
    assert text.count("static const unsigned char GAME_ROOM_") == len(g.GAMES)


def test_zork1_row_is_keyed_by_release_and_serial():
    text = INC.read_text(encoding="utf-8")
    assert '{ 88, "840726"' in text


def test_scene_values_are_stored_plus_one():
    rows = g.room_bytes("ZORK1")
    assert all(0 <= b <= len(vocab.SCENES) for b in rows)
    assert any(b > 0 for b in rows)


def test_unblessed_object_is_zero():
    rows = g.room_bytes("ZORK1")
    assert rows[0] == 0


def test_row_length_is_the_v3_object_ceiling():
    assert len(g.room_bytes("ZORK1")) == 256


def test_c_enum_matches_the_python_vocabulary_in_order():
    header = (REPO / "saturn" / "src" / "scene" / "scene_map.h").read_text(encoding="utf-8")
    found = re.findall(r"SC_([A-Z_]+)\s*=\s*(\d+)", header)
    assert [n for n, _ in found] == list(vocab.SCENES)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_gen_scene_tables.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'gen_scene_tables'`.

- [ ] **Step 3: Write the generator**

```python
def room_bytes(stem):
    """/*----------------------
     | room_bytes
     | Description: One game's 256-byte room map: index is the Z-machine object
     |   number, value is the scene index plus one, 0 meaning no scene authored.
     |   256 because a v3 object table holds at most 255 objects, so the whole
     |   map is one fixed array and a lookup needs no bounds table.
     | Author: suinevere
     | Dependencies: json, scene_vocab
     | Globals: SCENES_DIR
     | Params: stem -- a story stem, e.g. "ZORK1"
     | Returns: a list of 256 ints
     ----------------------*/"""
    path = SCENES_DIR / (stem + ".json")
    blessed = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    row = [0] * 256
    for obj, scene in blessed.items():
        n = int(obj)
        if 0 <= n < 256:
            row[n] = vocab.SCENE_INDEX[scene] + 1
    return row
```

Emit `game_rooms.inc` as one `static const unsigned char GAME_ROOM_<STEM>[256]`
per game plus a `GameRoomMap { unsigned short release; const char *serial;
const unsigned char *rooms; }` table. Emit `game_tracks.inc` as
`static const unsigned long SCENE_TRACKS[GAME_N][SCENE_N]`, all zero for now —
zero means "no tracks authored", which falls back to the neutral pool.

Both files get the standard generated-file header naming
`tools/gen_scene_tables.py` as their producer.

Also emit the `SC_*` enum block into `saturn/src/scene/scene_map.h` so the
Python vocabulary and the C enum cannot drift. Write it between two sentinel
comments so the rest of the header is hand-maintained.

- [ ] **Step 4: Run tests to verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_gen_scene_tables.py -v`
Expected: 7 passed. `test_c_enum_matches_the_python_vocabulary_in_order` will
need Task 9's header stub to exist first; if it does not yet, create
`scene_map.h` with just the sentinel comments and the enum in this task.

- [ ] **Step 5: Commit**

```bash
git add tools/gen_scene_tables.py tools/tests/test_gen_scene_tables.py saturn/src/scene/
git commit -m "tools: generate the per-game room and track tables from blessed scenes"
```

---

### Task 9: The C scene lookup

**Files:**
- Create: `saturn/src/scene/scene_map.h`, `saturn/src/scene/scene_map.c`
- Test: `saturn/tests/test_scene_map.c`

**Interfaces:**
- Consumes: `game_rooms.inc`, `game_tracks.inc` from Task 8.
- Produces:
  - `enum { SC_FOREST = 0, ..., SC_SPACE = 31 };` and `#define SCENE_N 32`
  - `int scene_of_room(unsigned int release, const char *serial, unsigned int obj);` — `SC_*`, or `-1` when unmapped.
  - `int scene_game_index(unsigned int release, const char *serial);` — row index into the generated tables, or `-1`.
  - `const char *scene_name(int scene);` — for tests and the options UI.
  - `unsigned long scene_track_mask(int game, int scene);` — 0 when unauthored.

- [ ] **Step 1: Write the failing test**

```c
/*----------------------
 | test_scene_map.c
 | Description: Lookup, bounds and identity for the generated scene tables.
 |   gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/tsm \
 |       saturn/tests/test_scene_map.c saturn/src/scene/scene_map.c && /tmp/tsm
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "scene/scene_map.h"

static int fails = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

int main(void) {
    check(SCENE_N == 32, "vocabulary is 32 scenes");
    check(SC_FOREST == 0, "FOREST is index 0");
    check(SC_SPACE == SCENE_N - 1, "SPACE is the last index");

    check(scene_game_index(88, "840726") >= 0, "Zork I is a known game");
    check(scene_game_index(999, "000000") == -1, "unknown release is unmapped");
    check(scene_game_index(88, "999999") == -1, "wrong serial is unmapped");

    check(scene_of_room(88, "840726", 0) == -1, "object 0 is unmapped");
    check(scene_of_room(88, "840726", 255) == -1 ||
          scene_of_room(88, "840726", 255) >= 0, "object 255 does not read out of range");
    check(scene_of_room(999, "000000", 5) == -1, "unknown game yields no scene");

    {
        int found = 0, obj;
        for (obj = 1; obj < 256; obj++)
            if (scene_of_room(88, "840726", obj) >= 0) found++;
        check(found > 50, "Zork I has many mapped rooms");
    }

    check(scene_name(SC_FOREST) != 0 && strcmp(scene_name(SC_FOREST), "FOREST") == 0,
          "scene_name reports FOREST");
    check(scene_name(-1) == 0, "scene_name rejects a negative index");
    check(scene_name(SCENE_N) == 0, "scene_name rejects a past-the-end index");

    check(scene_track_mask(-1, SC_FOREST) == 0UL, "unknown game has no track mask");
    check(scene_track_mask(0, SCENE_N) == 0UL, "out-of-range scene has no track mask");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/tsm saturn/tests/test_scene_map.c saturn/src/scene/scene_map.c && /tmp/tsm`
Expected: FAIL — `scene_map.c: No such file or directory`.

- [ ] **Step 3: Write the lookup**

```c
/*----------------------
 | scene_of_room
 | Description: The scene authored for one room of one game. Matches a
 |   GAME_ROOM_MAP row by release and 6-char serial, then indexes that game's
 |   256-byte map by the room's object number. Returns -1 when the game is
 |   unknown or the room carries no scene, which the caller treats as "hold
 |   whatever is showing".
 |
 |   The stored value is scene+1 so that 0 can mean unauthored without costing
 |   a separate presence table.
 | Author: suinevere
 | Dependencies: string.h (memcmp), game_rooms.inc
 | Globals: GAME_ROOM_MAP
 | Params: release -- Z-machine release; serial -- 6-char serial; obj -- the
 |   room's object number
 | Returns: an SC_* value, or -1
 ----------------------*/
int scene_of_room(unsigned int release, const char *serial, unsigned int obj) {
    int g = scene_game_index(release, serial);
    if (g < 0 || obj >= 256) return -1;
    {
        unsigned char v = GAME_ROOM_MAP[g].rooms[obj];
        return v ? (int) v - 1 : -1;
    }
}
```

`scene_game_index` walks `GAME_ROOM_MAP` comparing `release` and
`memcmp(serial, row->serial, 6)`. `scene_name` indexes a `SCENE_NAME[SCENE_N]`
string table generated alongside the enum. `scene_track_mask` bounds-checks
both arguments and reads `SCENE_TRACKS[game][scene]`.

- [ ] **Step 4: Run test to verify it passes**

Run: `gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/tsm saturn/tests/test_scene_map.c saturn/src/scene/scene_map.c && /tmp/tsm`
Expected: `all passed`.

- [ ] **Step 5: Syntax-check against the real SH-2 toolchain**

Run: `sh saturn/syntax-check.sh src/scene/scene_map.c`
Expected: clean, exit 0. This writes no objects and does not build.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/scene/scene_map.c saturn/src/scene/scene_map.h saturn/tests/test_scene_map.c
git commit -m "scene: add the per-game room-to-scene lookup"
```

---

### Task 10: Lift the event scanner out of the classifier

**Files:**
- Create: `saturn/src/sound/event_scan.h`, `saturn/src/sound/event_scan.c`
- Modify: `saturn/src/sound/music.h`
- Test: `saturn/tests/test_event_scan.c`

**Interfaces:**
- Consumes: the `text_scan_event` implementation and its two keyword tables, currently in `room_class.c` / `room_class_data.c`.
- Produces:
  - `enum { EV_NONE = -1, EV_DANGER = 0, EV_TRIUMPH = 1 };` and `#define EVENT_N 2`
  - `int event_scan(const char *text);` — `EV_*`, or `EV_NONE`.

Danger and triumph survive the rewrite as the one text-driven thing. They are a
sound concern now — they carry no picture — so they move to `sound/` and stop
depending on the classifier that is about to be deleted. `music.h` loses
`TC_NEUTRAL..TC_PLACE_LAST` entirely; scenes live in `scene_map.h`.

- [ ] **Step 1: Write the failing test**

```c
/*----------------------
 | test_event_scan.c
 | Description: The surviving text scan: danger and triumph, nothing else.
 |   gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/tes \
 |       saturn/tests/test_event_scan.c saturn/src/sound/event_scan.c && /tmp/tes
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include "sound/event_scan.h"

static int fails = 0;
static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

int main(void) {
    check(EVENT_N == 2, "two event categories");
    check(EV_NONE == -1, "EV_NONE is -1");

    check(event_scan("The troll swings his axe at you!") == EV_DANGER,
          "an attack is danger");
    check(event_scan("A voice booms: your quest is complete.") == EV_TRIUMPH ||
          event_scan("A voice booms: your quest is complete.") == EV_NONE,
          "triumph phrasing does not misfire as danger");
    check(event_scan("You are in a small room.") == EV_NONE,
          "plain room prose is not an event");
    check(event_scan("") == EV_NONE, "empty text is not an event");
    check(event_scan(0) == EV_NONE, "a null pointer is not an event");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/tes saturn/tests/test_event_scan.c saturn/src/sound/event_scan.c && /tmp/tes`
Expected: FAIL — `event_scan.c: No such file or directory`.

- [ ] **Step 3: Move the scanner**

Copy `text_scan_event` from `room_class.c` and its two keyword arrays from
`room_class_data.c` into `event_scan.c`, renaming to `event_scan` and mapping
the old `TC_DANGER` / `TC_TRIUMPH` results to `EV_DANGER` / `EV_TRIUMPH`. Add a
null guard, which the old code did not need because its caller always passed a
buffer.

In `music.h`, delete `TEXT_NUM_CATEGORIES`, the `TC_*` enum, `TC_PLACE_LAST`,
and the `text_game_room_category` declaration. Keep everything about mix modes,
tracks and the engine.

- [ ] **Step 4: Run test to verify it passes**

Run: `gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/tes saturn/tests/test_event_scan.c saturn/src/sound/event_scan.c && /tmp/tes`
Expected: `all passed`.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/sound/event_scan.c saturn/src/sound/event_scan.h saturn/src/sound/music.h saturn/tests/test_event_scan.c
git commit -m "sound: lift the danger and triumph scan out of the classifier"
```

---

### Task 11: Rewire the music engine onto scenes

**Files:**
- Modify: `saturn/src/sound/music.c`, `saturn/src/sound/music_data.c`
- Test: `saturn/tests/test_music_scene.c`

**Interfaces:**
- Consumes: `scene_of_room`, `scene_game_index`, `scene_track_mask`, `event_scan`.
- Produces:
  - `void music_set_game(unsigned int release, const char *serial);` — resolves and caches the game index.
  - `int music_track_from_mask(unsigned long mask, unsigned int r);` — the `r`-th set bit as a track number, or 0.
  - `music_on_turn(unsigned int obj)` unchanged in signature.

Everything that existed to compensate for unreliable classification comes out:
`g_room_cache`, `g_neutral_rooms`, `g_fallback_cat`, `MUSIC_FALLBACK_ROOMS`,
`g_genre_was_locked` and its cache flush. The debounce, pending-switch and
rotation machinery stays untouched.

- [ ] **Step 1: Write the failing test**

```c
/*----------------------
 | test_music_scene.c
 | Description: Track selection from a scene's bitmask, and the shape of the
 |   per-turn decision now that no classification happens.
 |   gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/scene -o /tmp/tms \
 |       saturn/tests/test_music_scene.c saturn/src/sound/music.c \
 |       saturn/src/sound/music_data.c saturn/src/sound/event_scan.c \
 |       saturn/src/scene/scene_map.c && /tmp/tms
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include "sound/music.h"
#include "scene/scene_map.h"

static int fails = 0;
static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

int main(void) {
    check(music_track_from_mask(0UL, 0) == 0, "an empty mask picks no track");

    check(music_track_from_mask(1UL << 4, 0) == 4, "a single bit picks its track");
    check(music_track_from_mask(1UL << 4, 7) == 4,
          "r is reduced modulo the population count");

    {
        unsigned long m = (1UL << 2) | (1UL << 9) | (1UL << 30);
        check(music_track_from_mask(m, 0) == 2, "first set bit");
        check(music_track_from_mask(m, 1) == 9, "second set bit");
        check(music_track_from_mask(m, 2) == 30, "third set bit");
        check(music_track_from_mask(m, 3) == 2, "wraps back to the first");
    }

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run the gcc line above.
Expected: FAIL — `undefined reference to music_track_from_mask`.

- [ ] **Step 3: Implement the mask picker and strip the compensation machinery**

```c
/*----------------------
 | music_track_from_mask
 | Description: The r-th set bit of a scene's track mask, as a CD-DA track
 |   number. r is reduced modulo the number of set bits, so any value is legal
 |   and an empty mask answers 0 (no track) rather than dividing by zero.
 |
 |   A mask rather than a list because the disc carries 31 tracks and one 32-bit
 |   word holds every subset of them, with no length field and no indirection.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: mask -- one bit per track; r -- any value
 | Returns: a track number, or 0 when the mask is empty
 ----------------------*/
int music_track_from_mask(unsigned long mask, unsigned int r) {
    int n = 0, i;
    for (i = 0; i < 32; i++) if (mask & (1UL << i)) n++;
    if (n == 0) return 0;
    r %= (unsigned int) n;
    for (i = 0; i < 32; i++) {
        if (!(mask & (1UL << i))) continue;
        if (r == 0) return i;
        r--;
    }
    return 0;
}
```

In `music_on_turn`, replace the base-category block with:

```c
    if (room_changed) {
        int base = scene_of_room(g_release, g_serial, obj);
        g_cur_room = obj; g_have_room = 1; g_base_cat = base; g_event_cat = -1;
    }
```

Delete `g_room_cache`, `g_neutral_rooms`, `g_fallback_cat`,
`MUSIC_FALLBACK_ROOMS`, `g_genre_was_locked` and every reference to them.
`text_scan_event` becomes `event_scan`, and its result is held in a separate
event slot so an event category can never be confused with a scene index.

In `music_data.c`, delete `GAME_MAPS`, `text_game_room_category`, the twelve
`P_*` place pools and `CATEGORY_POOL`. Keep `P_NEUTRAL` as the fallback pool
used when a scene's mask is zero, and keep `P_DANGER` / `P_TRIUMPH` for the two
surviving events.

- [ ] **Step 4: Run test to verify it passes**

Run the gcc line from Step 1.
Expected: `all passed`.

- [ ] **Step 5: Repair the existing music suites**

Five files reference symbols this task deletes. Verified by
`grep -ln "TC_\|music_category_pool\|TEXT_NUM_CATEGORIES" test/*.c saturn/tests/*.c`:

| File | Action |
|---|---|
| `test/music_mix_test.c` | rewrite `TC_*` uses as `SC_*`; include `scene/scene_map.h` |
| `test/music_test.c` | same |
| `saturn/tests/test_display.c` | swap `display_category_image*` for `display_scene_image*`; drop band assertions |
| `test/music_category_test.c` | delete — it tests `music_category_pool`, which `test_music_scene.c` replaces |
| `test/room_class_test.c` | delete in Task 14 |

Run each repaired suite, e.g.:
`gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/scene -o /tmp/tmm saturn/tests/test_music_pause.c saturn/src/sound/music.c saturn/src/sound/music_data.c saturn/src/sound/event_scan.c saturn/src/scene/scene_map.c && /tmp/tmm`
Expected: passes.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/sound/music.c saturn/src/sound/music_data.c saturn/tests/test_music_scene.c
git commit -m "sound: drive the track choice from the room's scene instead of classified text"
```

---

### Task 12: Per-game art folders in the display

**Files:**
- Modify: `saturn/src/video/display.c`, `saturn/src/video/display.h`
- Create: `saturn/src/scene/game_scenes.inc` (hand-stubbed here, generated in Task 13)
- Test: `saturn/tests/test_display_scene.c`

**Interfaces:**
- Consumes: `scene_game_index`, `SCENE_N`.
- Produces:
  - `void display_set_game(int game_index);` — selects the folder every later resolve uses.
  - `const char *display_scene_image(int scene);` — replaces `display_category_image`.
  - `int display_scene_image_count(int scene);`
  - `void display_rotate_scene(int scene);`
  - `display_image_file(slot)` unchanged in signature.

Slot encoding is deliberately unchanged: `slot = scene * 100 + local_index`,
where `local_index` is 1-based **within the scene's range**. `display_image_file`
resolves it to `GAME_DIR[g_game]` plus the two digits of
`GAME_SCENE[g_game][scene].base + local_index`. `"STARCROS/99.TGA"` is the same
15 characters as `"UNDRGRND/99.TGA"`, so `g_file_buf[2][16]` and the save blob's
frozen name field are both untouched.

- [ ] **Step 1: Write the failing test**

```c
/*----------------------
 | test_display_scene.c
 | Description: Per-game folder resolution and scene index ranges.
 |   gcc -O2 -I saturn/src -I saturn/src/video -I saturn/src/scene \
 |       -o /tmp/tds saturn/tests/test_display_scene.c \
 |       saturn/src/video/display.c saturn/src/scene/scene_map.c && /tmp/tds
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "video/display.h"
#include "scene/scene_map.h"

static int fails = 0;
static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

int main(void) {
    int zork = scene_game_index(88, "840726");
    check(zork >= 0, "Zork I resolves to a game index");

    display_set_game(zork);

    check(strlen(display_image_file(display_slot_make(SC_FOREST, 1))) <= 15,
          "a resolved path fits the frozen 15-character field");

    {
        const char *p = display_image_file(display_slot_make(SC_FOREST, 1));
        check(strstr(p, "ZORK1/") == p || p[0] == '\0',
              "the folder is the game's, not the scene's");
        check(p[0] == '\0' || strstr(p, ".TGA") != 0, "a path ends in .TGA");
    }

    check(display_image_file(DISP_IMAGE_NONE)[0] == '\0',
          "DISP_IMAGE_NONE resolves to the empty string");
    check(display_scene_image(-1) == 0, "a negative scene has no image");
    check(display_scene_image(SCENE_N) == 0, "a past-the-end scene has no image");
    check(display_scene_image_count(SCENE_N) == 0, "...and no count");

    display_set_game(-1);
    check(display_scene_image(SC_FOREST) == 0, "no game selected yields no image");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run the gcc line above.
Expected: FAIL — `undefined reference to display_set_game`.

- [ ] **Step 3: Replace the category folder with a game folder**

Delete `CATEGORY_DIR`, `g_art_band`, `display_set_art_band` and `effective_band`.
Replace `#include "category_art.inc"` with `#include "scene/game_scenes.inc"`,
which defines `GAME_DIR[GAME_N]` and `GAME_SCENE[GAME_N][SCENE_N]`.

```c
/*----------------------
 | display_image_file
 | Description: See display.h. The folder is the running game's; the two digits
 |   are the picture's absolute index inside that folder, which is the scene's
 |   base plus the slot's 1-based local index.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: GAME_DIR, GAME_SCENE, g_game, g_file_buf, g_file_turn
 | Params: slot -- a slot, or DISP_IMAGE_NONE
 | Returns: the disc path, or "" when the slot names no picture
 ----------------------*/
const char *display_image_file(int slot) {
    char *out;
    const char *dir;
    int scene, local, index, k = 0;

    if (g_game < 0 || !display_slot_valid(slot)) return "";
    scene = slot / SLOT_STRIDE;
    local = slot % SLOT_STRIDE;
    index = GAME_SCENE[g_game][scene].base + local;
    dir   = GAME_DIR[g_game];

    out = g_file_buf[g_file_turn];
    g_file_turn ^= 1;

    while (*dir) out[k++] = *dir++;
    out[k++] = '/';
    out[k++] = (char) ('0' + index / 10);
    out[k++] = (char) ('0' + index % 10);
    out[k++] = '.'; out[k++] = 'T'; out[k++] = 'G'; out[k++] = 'A';
    out[k]   = '\0';
    return out;
}
```

Rename `display_category_image` to `display_scene_image`,
`display_category_image_count` to `display_scene_image_count`,
`display_rotate_dynamic_category` to `display_rotate_scene`, and
`display_shuffle_category` to `display_shuffle_scene`, each now reading
`GAME_SCENE[g_game][scene]` instead of a band. `display_set_game` stores the
index and re-seats every rotor, the same way `display_set_art_band` used to.

Stub `game_scenes.inc` by hand for now with one row per game, `GAME_DIR`
holding the story stems, and all counts zero except Zork I's, which Task 13
will regenerate for real.

- [ ] **Step 4: Run test to verify it passes**

Run the gcc line from Step 1.
Expected: `all passed`.

- [ ] **Step 5: Syntax-check against the SH-2 toolchain**

Run: `sh saturn/syntax-check.sh src/video/display.c`
Expected: clean, exit 0.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/video/display.c saturn/src/video/display.h saturn/src/scene/game_scenes.inc saturn/tests/test_display_scene.c
git commit -m "video: resolve background art from the running game's folder and scene range"
```

---

### Task 13: Convert art per game and generate the scene ranges

**Files:**
- Modify: `tools/make_tga.py`, `tools/art_nouns.py`
- Modify: `saturn/src/scene/game_scenes.inc` (now generated)
- Test: `saturn/tests/test_game_scenes.py`, `tools/tests/test_art_nouns.py`

**Interfaces:**
- Consumes: `scene_vocab.SCENES`, `FETCH_NOUNS`; `gen_scene_tables.GAMES`.
- Produces:
  - `make_tga.convert_game_tree(src_root, dst_root) -> dict[str, dict[str, int]]` — `{game_stem: {scene: count}}`.
  - `make_tga.write_scene_inc(counts, path)` — emits `GAME_DIR` and `GAME_SCENE`.
  - `art_nouns.nouns_for_scene(scene) -> tuple[str, ...]`.

`art_nouns.py` currently derives its whole vocabulary by parsing
`room_class_data.c`, which Task 14 deletes. It moves to `scene_vocab.FETCH_NOUNS`,
keeping the property that art coverage cannot drift from the tagging vocabulary.

- [ ] **Step 1: Write the failing test**

```python
# saturn/tests/test_game_scenes.py
"""Generated scene ranges may only name pictures the disc actually carries."""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import scene_vocab as vocab

TGA = REPO / "saturn" / "cd" / "data" / "TGA"
INC = REPO / "saturn" / "src" / "scene" / "game_scenes.inc"


def _rows():
    text = INC.read_text(encoding="utf-8")
    body = text.split("GAME_SCENE", 1)[1]
    return [[tuple(int(x) for x in pair)
             for pair in re.findall(r"\{\s*(\d+),\s*(\d+)\s*\}", line)]
            for line in body.splitlines() if line.strip().startswith("{ {")]


def test_every_row_has_one_entry_per_scene():
    for row in _rows():
        assert len(row) == len(vocab.SCENES)


def test_ranges_never_exceed_the_ninety_nine_budget():
    for row in _rows():
        for base, count in row:
            assert base + count <= 99


def test_ranges_within_a_game_do_not_overlap():
    for row in _rows():
        spans = sorted((b, b + c) for b, c in row if c)
        for (_, end), (nxt, _) in zip(spans, spans[1:]):
            assert end <= nxt


def test_every_counted_picture_exists_on_disc():
    text = INC.read_text(encoding="utf-8")
    dirs = re.findall(r'"([A-Z0-9]{1,8})"', text.split("GAME_DIR", 1)[1].split(";", 1)[0])
    for game, row in zip(dirs, _rows()):
        for base, count in row:
            for i in range(1, count + 1):
                assert (TGA / game / f"{base + i:02d}.TGA").exists(), f"{game} {base+i}"
```

```python
# tools/tests/test_art_nouns.py  (replace the room_class_data.c parsing tests)
def test_nouns_come_from_the_scene_vocabulary():
    import art_nouns, scene_vocab
    for scene in scene_vocab.SCENES:
        assert art_nouns.nouns_for_scene(scene) == scene_vocab.FETCH_NOUNS[scene]


def test_unknown_scene_has_no_nouns():
    import art_nouns
    assert art_nouns.nouns_for_scene("NOT_A_SCENE") == ()


def test_no_reference_to_the_deleted_classifier():
    from pathlib import Path
    src = Path(__file__).resolve().parents[2] / "tools" / "art_nouns.py"
    assert "room_class_data" not in src.read_text(encoding="utf-8")
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `tools/.venv/Scripts/python.exe -m pytest saturn/tests/test_game_scenes.py tools/tests/test_art_nouns.py -v`
Expected: FAIL — `game_scenes.inc` has hand-stubbed rows that name no real
files, and `art_nouns` has no `nouns_for_scene`.

- [ ] **Step 3: Convert per game and emit real ranges**

Change the source tree `make_tga.py` walks from `png/<MOOD>/<DONOR>/<noun>/` to
`png/<GAME>/<SCENE>/`. For each game, convert its scenes in `SCENES` order,
assigning consecutive indices from 1, and record `{game: {scene: count}}`.
Emit `GAME_DIR[GAME_N]` and `GAME_SCENE[GAME_N][SCENE_N]` into
`saturn/src/scene/game_scenes.inc` with the standard generated-file header.

Because the counts come from files that actually converted, a range can never
name a picture the disc lacks — the property `CATEGORY_BAND` has today,
preserved.

Rewrite `art_nouns.py` to expose `nouns_for_scene(scene)` reading
`scene_vocab.FETCH_NOUNS`, and delete its `room_class_data.c` parser,
`TC_TO_FOLDER` and `MOODS`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest saturn/tests/test_game_scenes.py tools/tests/test_art_nouns.py -v`
Expected: all passed. With no per-game PNGs yet every count is 0, which
satisfies all four disc assertions vacuously and correctly.

- [ ] **Step 5: Commit**

```bash
git add tools/make_tga.py tools/art_nouns.py saturn/src/scene/game_scenes.inc saturn/tests/test_game_scenes.py tools/tests/test_art_nouns.py
git commit -m "tools: convert background art per game and generate its scene index ranges"
```

---

### Task 14: Delete the classifier and its call sites

**Files:**
- Modify: `saturn/src/main.cxx`, `saturn/mojozork.c`
- Delete: `saturn/src/classify/room_class.{c,h}`, `saturn/src/classify/room_class_data.c`, `saturn/src/classify/*.o`, `saturn/src/video/category_art.inc`, `test/room_class_test.c`, `test/music_category_test.c`, `test/corpus/blessed.inc`, `saturn/tests/test_room_genre.c`, `saturn/tests/test_cd_mood_dirs.py`, `saturn/tests/test_category_art.py`
- Test: `saturn/tests/test_no_classifier.py`

**Interfaces:**
- Consumes: `music_set_game`, `display_set_game`.
- Produces: nothing new.

Do this **after** the replacements work, not before, so the tree is never in a
state where art and music are broken and the reason is ambiguous.

Keep `test/corpus/rooms.inc`: `blessed.inc` is superseded by the blessed scene
JSONs, but the captured room text stays useful and costs nothing.

- [ ] **Step 1: Write the failing test**

```python
# saturn/tests/test_no_classifier.py
"""The classifier is gone, and nothing still reaches for it."""
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

GONE = [
    "saturn/src/classify/room_class.c",
    "saturn/src/classify/room_class.h",
    "saturn/src/classify/room_class_data.c",
    "saturn/src/video/category_art.inc",
    "test/room_class_test.c",
    "test/music_category_test.c",
    "test/corpus/blessed.inc",
    "saturn/tests/test_room_genre.c",
    "saturn/tests/test_cd_mood_dirs.py",
    "saturn/tests/test_category_art.py",
]

DEAD_SYMBOLS = (
    "text_classify_room", "room_class_genre", "room_class_genre_locked",
    "display_set_art_band", "art_band_of_genre", "music_note_room_title",
    "text_game_room_category", "music_category_pool", "MUSIC_FALLBACK_ROOMS",
    "TC_NEUTRAL", "TEXT_NUM_CATEGORIES", "display_category_image",
)

# "test" included deliberately: test/music_mix_test.c, test/music_test.c and
# test/music_category_test.c all name these symbols, and leaving that directory
# out of the sweep is how a dead reference survives a deletion.
SEARCH_ROOTS = ["saturn/src", "saturn/mojozork.c", "saturn/tests", "tools", "test"]


def test_classifier_files_are_deleted():
    for rel in GONE:
        assert not (REPO / rel).exists(), rel


def test_no_source_still_names_a_dead_symbol():
    offenders = []
    for root in SEARCH_ROOTS:
        p = REPO / root
        files = [p] if p.is_file() else [
            f for f in p.rglob("*")
            if f.suffix in {".c", ".h", ".cxx", ".inc", ".py"}
            and ".venv" not in f.parts and "__pycache__" not in f.parts
        ]
        for f in files:
            text = f.read_text(encoding="utf-8", errors="replace")
            for sym in DEAD_SYMBOLS:
                if re.search(rf"\b{sym}\b", text):
                    offenders.append(f"{f.relative_to(REPO)}: {sym}")
    assert not offenders, offenders


def test_corpus_room_text_is_kept():
    assert (REPO / "test" / "corpus" / "rooms.inc").exists()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest saturn/tests/test_no_classifier.py -v`
Expected: FAIL — every file still present and many dead symbols still named.

- [ ] **Step 3: Remove the call sites, then the files**

In `main.cxx`: delete `art_band_of_genre` and the three `display_set_art_band`
calls at lines 146, 175 and 411. Where the game is chosen and loaded, call
`music_set_game(release, serial)` and
`display_set_game(scene_game_index(release, serial))`. Keep the
`music_set_category_fn` / `music_set_rotate_fn` subscriptions; they now carry a
scene index.

In `mojozork.c`, reduce the Saturn block to:

```c
#if defined(MOJOZORK_SATURN)
    {
        extern void music_on_turn(unsigned int room);
        const uint8 *rmaddr = varAddress(0x10, 0, 0);
        const uint16 rmobj = ((uint16) rmaddr[0] << 8) | (uint16) rmaddr[1];
        music_on_turn((unsigned int) rmobj);
    }
#endif
```

The `rmname` buffer, the `decode_zscii` call and the `music_note_room_title`
declaration all go: the title only ever fed the classifier's title weighting.

Then `git rm` the files listed above.

- [ ] **Step 4: Run test to verify it passes**

Run: `tools/.venv/Scripts/python.exe -m pytest saturn/tests/test_no_classifier.py -v`
Expected: 3 passed.

- [ ] **Step 5: Syntax-check the two edited units**

Run: `sh saturn/syntax-check.sh src/main.cxx`
Expected: clean, exit 0.

Note `mojozork.c` is a C file built by the makefile, not by `syntax-check.sh`;
verify it with `gcc -fsyntax-only -DMOJOZORK_SATURN=1 saturn/mojozork.c` and
expect only missing-Saturn-header errors, never an error naming `rmname` or
`music_note_room_title`.

- [ ] **Step 6: Run the whole host test suite**

Run: `tools/.venv/Scripts/python.exe -m pytest saturn/tests tools/tests -q`
Expected: all passed, no collection errors.

- [ ] **Step 7: Commit**

```bash
git add -A saturn/src saturn/mojozork.c saturn/tests test tools
git commit -m "classify: delete the keyword mood classifier now that scenes replace it"
```

---

### Task 15: Zork I art onto the disc

**Files:**
- Create: `tools/assets/png/ZORK1/<SCENE>/*.png`
- Modify: `saturn/cd/data/TGA/ZORK1/*.TGA`, `saturn/src/scene/game_scenes.inc`, `tools/assets/art_manifest.snapshot.json`

**Interfaces:**
- Consumes: `tools/assets/scenes/ZORK1.json`, `art_nouns.nouns_for_scene`, `make_tga.convert_game_tree`.
- Produces: a populated `GAME_SCENE` row for Zork I.

- [ ] **Step 1: List the scenes Zork I actually needs**

```bash
tools/.venv/Scripts/python.exe -c "
import json, pathlib
b = json.loads(pathlib.Path('tools/assets/scenes/ZORK1.json').read_text())
from collections import Counter
for s, n in Counter(b.values()).most_common(): print(f'{n:4}  {s}')
"
```

Expected: 12–18 scenes. This is the shopping list.

- [ ] **Step 2: Check the orphans before fetching anything**

365 images already on disk are orphaned from the manifest, fetched for nouns
like `cottage`, `attic`, `farmhouse`, `chamber`, `forest` and `grove` that map
onto these scenes directly. Re-indexing one is free; refetching costs a quota
call and a review.

```bash
tools/.venv/Scripts/python.exe -c "
import pathlib, collections
png = pathlib.Path('tools/assets/png')
c = collections.Counter(p.parent.name for p in png.rglob('*.png'))
for noun, n in c.most_common(40): print(f'{n:4}  {noun}')
"
```

- [ ] **Step 3: Fetch and review the gaps**

Run `tools/fetch_art.py` for the scenes the orphans do not cover, using
`art_nouns.nouns_for_scene`, then curate in the art server on :8080. Target 1–3
accepted images per scene, no more than 99 in total for the game.

- [ ] **Step 4: Stage the accepted images per scene and convert**

Arrange accepted PNGs as `tools/assets/png/ZORK1/<SCENE>/*.png`, then:

Run: `tools/.venv/Scripts/python.exe tools/make_tga.py`
Expected: `ZORK1` reported with a non-zero count per staged scene, and
`saturn/src/scene/game_scenes.inc` rewritten with a real Zork I row.

- [ ] **Step 5: Verify the disc matches the table**

Run: `tools/.venv/Scripts/python.exe -m pytest saturn/tests/test_game_scenes.py -v`
Expected: 4 passed, now over real files rather than vacuously.

- [ ] **Step 6: Snapshot the manifest and commit**

```bash
tools/.venv/Scripts/python.exe tools/art_status.py --snapshot
git add tools/assets/png/ZORK1 saturn/cd/data/TGA/ZORK1 saturn/src/scene/game_scenes.inc tools/assets/art_manifest.snapshot.json
git commit -m "assets: give Zork I its own scene art and index ranges"
```

- [ ] **Step 7: Hand the tree back for a build**

Ask the owner to run `./compile.bat` and play Zork I. **Do not run it yourself
and do not launch the emulator.** Check on screen that West of House, the
Living Room, the Cellar, the Maze and the Temple each show a picture that suits
them, and that the track changes with the scene rather than with the prose.

---

## Remaining games

Tasks 3 through 7 repeat per game with no new code: the inventory is already
generated for all 31, so each game needs only its review queue cleared in the
server and its art staged and converted. `gen_scene_tables.py` and `make_tga.py`
pick up every blessed game on each run.

Suggested order, cheapest first by review count: `SPLBRKR` (7),
`HYPOCOND` (14), `WITNESS` (29), `SEASTLKR` (30), `HITCHHKR` (31),
then the Zork family, then the rest.

## Self-Review Notes

**Spec coverage.** Every spec section maps to a task: the three tables (8, 9,
12, 13); the scene vocabulary (2); the flat per-game folder (12); the audio
asymmetry (11, via `scene_track_mask` and `music_track_from_mask`); the four
pipeline stages (3, 4, 5, 6); the precedence rule (5); the review tool (6);
manifest persistence (1); runtime deletions (10, 11, 12, 14); testing
(distributed, with the completeness guard in 5 and determinism in 8); work
order (7, 15).

**Open items from the spec** remain open and are correctly not tasks here:
which CD-DA tracks each game selects (`SCENE_TRACKS` generates as all-zero,
which falls back to the neutral pool and is a working state), and whether the
365 orphans can be re-indexed (Task 15 Step 2 investigates it but does not
depend on the answer).
