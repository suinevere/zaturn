# Art Review Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the static contact sheets with a Flask app on `localhost:8080` that applies each verdict the moment it is clicked.

**Architecture:** `tools/art_server.py` reads `art_manifest.json` and writes exclusively through the existing `art_review.promote()` state machine, so the four transitions and the `png/` ↔ `candidates/` moves keep their one mutation-verified implementation. Pictures are served as files rather than embedded, which is what stops page weight scaling with pool size.

**Tech Stack:** Python 3.9+, Flask, `pytest`. No Saturn code, no C compiler, no network.

**Spec:** `docs/superpowers/specs/2026-08-08-art-review-server-design.md`

## Global Constraints

- **Python 3.9 floor.** `tools/convert-backgrounds.sh:36` gates on `sys.version_info >= (3, 9)`. No `match`, no PEP 604 `X | Y` annotations, no `dict[str, int]` at runtime.
- **Every stage degrades, none aborts.** A missing Flask, a bound port, a missing picture file must print an actionable message and exit 0 or render a placeholder — never raise.
- **Bind to `127.0.0.1` only.** Never `0.0.0.0`. Single-operator tool on one machine.
- **`art_status` constants only.** No bare status strings anywhere.
- **The manifest is the only truth.** `promote()` is the only writer of status and the only mover of files. The server must never assign a status itself.
- **Comment style is mandatory.** Every module, function and constant gets a docstring carrying Description, Author: suinevere, Dependencies, Globals, Params, Returns, using `N/A` where inapplicable. Tests get a module docstring only — the file header, nothing per-test. **No comments inside function bodies.**
- **No network in tests.** No live server, no sockets — Flask's `test_client()` throughout.
- **No API key in any file, fixture, or commit message.**
- **Run the whole suite, both directories:** `tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q`. Currently 129 passing.
- **Commit after every task.** One sentence, no body, no bullets, no trailers, no mention of Claude/AI/session.

## Mutation verification is not optional

Every task that pins a guard ends by breaking that guard, watching the named test
fail, and restoring it. This project has shipped **nine** tests that passed while
the thing they protected was broken — three of them in the last two days. A test
that passes with its subject broken is worth less than no test, because it also
buys false confidence. Paste the failing output into your report.

## File structure

| File | Responsibility | Change |
|---|---|---|
| `tools/art_server.py` | Routes, grouping, rendering, and the verdict controller | Create |
| `tools/tests/test_art_server.py` | Route and behaviour coverage | Create |
| `tools/requirements-review.txt` | Flask, kept out of the build venv | Create |
| `tools/art_review.py` | Loses `sheet`, `index_page`, `_cell`, `_group_section`, `_thumb_uri`, `_status_label`, `SHOWN`, and the `--sheets` branch | Modify |
| `tools/tests/test_art_review.py` | Loses the sheet tests | Modify |
| `tools/convert-backgrounds.sh` | Loses the promote step | Modify |
| `saturn/pre.makefile` | Message reverts to naming conversion alone | Modify |

`art_server.py` stays one module. Its routes, grouping and template are one
concern; splitting them would separate a route from the shape it renders.

---

### Task 1: Flask arrives without touching the build venv

**Files:**
- Create: `tools/requirements-review.txt`
- Create: `tools/art_server.py`
- Create: `tools/tests/test_art_server.py`

**Interfaces:**
- Consumes: `fetch_art.load_manifest`, `art_queries.load`.
- Produces:
  - `create_app(repo=None)` — returns a configured Flask app. `repo` overrides the repository root so tests point it at a `tmp_path`.
  - `main(argv)` — entry point; returns 0 always.

- [ ] **Step 1: Create the requirements file**

`tools/requirements-review.txt`:

```
# Dependencies for the local review server (tools/art_server.py) only.
# Deliberately NOT in requirements.txt: convert-backgrounds.sh installs that
# into the build venv on every build, and the build has no use for a web server.
#   pip install -r tools/requirements-review.txt
Flask>=3,<4
```

- [ ] **Step 2: Install it into the venv**

Run: `tools/.venv/Scripts/python.exe -m pip install -r tools/requirements-review.txt`

Expected: Flask installs. It is currently absent — `import flask` raises `ModuleNotFoundError` today.

- [ ] **Step 3: Write the failing test**

Create `tools/tests/test_art_server.py`:

```python
"""Cover the local review server's routes, verdicts, filtering and grouping."""
import json
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_server
import art_status
import fetch_art


def record(pid, mood="HORROR", donor="HOUSE", noun="hallway",
           status=art_status.CANDIDATE):
    return {"id": pid, "page_url": f"https://pixabay.com/photos/{pid}/",
            "image_url": "", "phrase": "dark hallway", "mood": mood,
            "donor": donor, "noun": noun, "licence": "Pixabay Content License",
            "fetched": "2026-08-08", "luminance": 70.0, "busyness": 4.0,
            "banding": 2.0, "verdict": "pass", "phash": "0" * 16,
            "status": status}


def write_png(root, rec):
    d = root / rec["mood"] / rec["donor"] / rec["noun"]
    d.mkdir(parents=True, exist_ok=True)
    p = d / f"{rec['id']}.png"
    Image.new("RGB", (320, 224), (60, 60, 60)).save(p, "PNG")
    return p


def build(tmp_path, records, promoted=(), candidates=()):
    assets = tmp_path / "tools" / "assets"
    assets.mkdir(parents=True)
    for rec in promoted:
        write_png(assets / "png", rec)
    for rec in candidates:
        write_png(assets / "candidates", rec)
    fetch_art.save_manifest(assets / "art_manifest.json",
                            {str(r["id"]): r for r in records})
    (assets / "art_queries.json").write_text(
        json.dumps({m: {"adjectives": ["dark"], "donors": [m],
                        "extra_nouns": ["hallway"], "exclude_nouns": [],
                        "target": 99}
                    for m in ("HORROR", "WATER")}),
        encoding="utf-8")
    return art_server.create_app(repo=tmp_path).test_client()


def test_index_lists_every_mood_with_its_counts(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    rej = record(2, status=art_status.REJECTED)
    und = record(3, status=art_status.CANDIDATE)
    client = build(tmp_path, [acc, rej, und], promoted=[acc],
                   candidates=[rej, und])

    page = client.get("/").get_data(as_text=True)

    assert "HORROR" in page
    assert "99" in page, "the per-mood target must be shown"


def test_index_reads_the_target_from_the_vocabulary(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    page = client.get("/").get_data(as_text=True)

    assert "99" in page, "target comes from art_queries.json, not a constant"
```

- [ ] **Step 4: Run it and verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_server.py -q`
Expected: FAIL — `ModuleNotFoundError: No module named 'art_server'`.

- [ ] **Step 5: Implement the module skeleton and the index route**

Create `tools/art_server.py`:

```python
"""Serve the candidate review UI on loopback so a verdict applies on click.

Description: Replaces the static contact sheets. Pictures are served as files
    rather than embedded, so page weight no longer scales with pool size, and a
    verdict is applied the moment it is clicked, so there is no download-and-
    promote round trip to forget. The manifest stays the only truth: every
    status change goes through art_review.promote, which owns the four
    transitions and the png/candidates moves.
Author: suinevere
Dependencies: flask, art_queries, art_review, art_status, fetch_art
Globals: N/A
"""
import sys
from pathlib import Path

import art_queries
import art_review
import art_status
import fetch_art

HOST = "127.0.0.1"
PORT = 8080


def _assets(repo):
    """Locate the asset tree under a repository root.

    Description: One place that knows the layout, so a test pointing at a
        tmp_path and a real run differ only in what they pass here.
    Author: suinevere
    Dependencies: pathlib
    Globals: N/A
    Params: repo -- the repository root
    Returns: the tools/assets directory as a Path
    """
    return Path(repo) / "tools" / "assets"


def _targets(assets):
    """Read each mood's picture goal from the shipped vocabulary.

    Description: The goal lives in art_queries.json rather than in this file,
        so retuning the vocabulary retunes the progress bars with it. A missing
        or unreadable vocabulary degrades to an empty map and the UI simply
        shows no target.
    Author: suinevere
    Dependencies: art_queries
    Globals: N/A
    Params: assets -- the tools/assets directory
    Returns: dict mapping mood to its integer target
    """
    try:
        vocab = art_queries.load(assets / "art_queries.json")
    except Exception:
        return {}
    out = {}
    for mood, entry in vocab.items():
        out[mood] = entry.get("target", 0)
    return out


def create_app(repo=None):
    """Build the Flask app rooted at a repository.

    Description: Takes the root as an argument rather than deriving it once at
        import, so tests can point a whole app at a tmp_path without touching
        the real asset tree.
    Author: suinevere
    Dependencies: flask
    Globals: N/A
    Params: repo -- repository root; defaults to this file's parent's parent
    Returns: a configured Flask app
    """
    from flask import Flask, render_template_string

    repo = Path(repo) if repo else Path(__file__).resolve().parents[1]
    assets = _assets(repo)
    app = Flask(__name__)

    @app.route("/")
    def index():
        manifest = fetch_art.load_manifest(assets / "art_manifest.json")
        targets = _targets(assets)
        rows = []
        for mood in sorted(targets):
            mine = [r for r in manifest.values() if r["mood"] == mood]
            rows.append({
                "mood": mood,
                "accepted": sum(1 for r in mine
                                if r["status"] == art_status.ACCEPTED),
                "rejected": sum(1 for r in mine
                                if r["status"] == art_status.REJECTED),
                "undecided": sum(1 for r in mine
                                 if r["status"] == art_status.CANDIDATE),
                "target": targets[mood],
            })
        return render_template_string(INDEX_HTML, rows=rows)

    return app


INDEX_HTML = """<!doctype html><meta charset="utf-8"><title>Room art review</title>
<style>body{background:#111;color:#ddd;font:13px sans-serif;margin:24px}
table{border-collapse:collapse}td,th{padding:5px 14px;border-bottom:1px solid #333;text-align:right}
td:first-child,th:first-child{text-align:left}a{color:#8cf}
.bar{background:#222;width:120px;height:9px;display:inline-block}
.bar i{background:#4a8;height:9px;display:block}</style>
<h1>Room art review</h1>
<table><tr><th>mood</th><th>accepted</th><th>rejected</th><th>undecided</th>
<th>of target</th><th></th></tr>
{% for r in rows %}<tr>
<td><a href="/mood/{{ r.mood }}">{{ r.mood }}</a></td>
<td>{{ r.accepted }}</td><td>{{ r.rejected }}</td><td>{{ r.undecided }}</td>
<td>{{ r.accepted }} / {{ r.target }}</td>
<td><span class="bar"><i style="width:{{ (100 * r.accepted // r.target) if r.target else 0 }}%"></i></span></td>
</tr>{% endfor %}
</table>
"""


def main(argv):
    """Run the review server on loopback.

    Description: A missing Flask, or a port already bound, prints an actionable
        line and returns 0 rather than raising -- the everything-degrades rule
        the rest of these tools follow. The port is fixed rather than hunted
        for, because the operator keeps that URL open.
    Author: suinevere
    Dependencies: flask
    Globals: HOST, PORT
    Params: argv -- unused, accepted so the entry point matches its siblings
    Returns: 0 always
    """
    try:
        import flask                                    # noqa: F401
    except ImportError:
        print("  Flask is not installed. Run:")
        print("    python -m pip install -r tools/requirements-review.txt")
        return 0
    app = create_app()
    try:
        app.run(host=HOST, port=PORT, debug=False)
    except OSError as exc:
        print(f"  cannot bind {HOST}:{PORT} ({exc}).")
        print("  Something else is using it; free that port and re-run.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```

- [ ] **Step 6: Run tests and verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_server.py -q`
Expected: PASS, 2 tests.

- [ ] **Step 7: Mutation-verify**

Replace `_targets`'s return with a hardcoded `{"HORROR": 99, "WATER": 99}` and confirm `test_index_reads_the_target_from_the_vocabulary` still passes — it will, because the value coincides. That is the test being too weak. Strengthen it: change the fixture's `target` to `40` and assert `"40"` appears and `"99"` does not, then re-run the mutation and confirm it FAILS. Restore.

This is exactly the failure mode that has bitten this project nine times: an assertion that cannot distinguish the right answer from a plausible wrong one.

- [ ] **Step 8: Full suite and commit**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q
git add tools/requirements-review.txt tools/art_server.py tools/tests/test_art_server.py
git commit -m "art_server: serve a review index showing each mood's counts against its vocabulary target"
```

---

### Task 2: Serve a picture from whichever tree holds it

**Files:**
- Modify: `tools/art_server.py`
- Test: `tools/tests/test_art_server.py`

**Interfaces:**
- Consumes: `create_app` from Task 1, `art_review._rel(rec) -> Path`.
- Produces: route `GET /image/<pid>` returning the PNG bytes, 404 when the id is unknown or no file exists.

- [ ] **Step 1: Write the failing tests**

```python
def test_image_route_serves_an_accepted_picture_from_png(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    client = build(tmp_path, [acc], promoted=[acc])

    resp = client.get("/image/1")

    assert resp.status_code == 200
    assert resp.data[:8] == b"\x89PNG\r\n\x1a\n"


def test_image_route_serves_a_rejected_picture_from_candidates(tmp_path):
    rej = record(2, status=art_status.REJECTED)
    client = build(tmp_path, [rej], candidates=[rej])

    assert client.get("/image/2").status_code == 200


def test_image_route_404s_for_an_unknown_id(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    assert client.get("/image/9999").status_code == 404


def test_image_route_404s_when_the_file_is_missing(tmp_path):
    client = build(tmp_path, [record(1)])

    assert client.get("/image/1").status_code == 404, \
        "a fresh clone has the record but no pixels"
```

- [ ] **Step 2: Run and verify they fail**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_server.py -q -k image`
Expected: FAIL with 404 on the first two — the route does not exist.

- [ ] **Step 3: Implement**

Add inside `create_app`, after the index route:

```python
    @app.route("/image/<pid>")
    def image(pid):
        from flask import abort, send_file
        manifest = fetch_art.load_manifest(assets / "art_manifest.json")
        rec = manifest.get(str(pid))
        if rec is None:
            abort(404)
        rel = art_review._rel(rec)
        for root in (assets / "png", assets / "candidates"):
            path = Path(root) / rel
            if path.exists():
                return send_file(str(path), mimetype="image/png")
        abort(404)
```

- [ ] **Step 4: Run tests and verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_server.py -q`
Expected: PASS.

- [ ] **Step 5: Mutation-verify**

Drop `assets / "candidates"` from the search tuple and confirm `test_image_route_serves_a_rejected_picture_from_candidates` FAILS. Restore. Paste the output.

- [ ] **Step 6: Full suite and commit**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q
git add tools/art_server.py tools/tests/test_art_server.py
git commit -m "art_server: serve each picture from whichever tree currently holds it"
```

---

### Task 3: A verdict applies on click

**Files:**
- Modify: `tools/art_server.py`
- Test: `tools/tests/test_art_server.py`

**Interfaces:**
- Consumes: `art_review.promote(verdicts, manifest, candidates_dir, png_dir) -> dict` mapping mood to `art_review.Counts(gained, lost)`.
- Produces: route `POST /verdict` taking JSON `{"id": "...", "verdict": "accept"|"reject"}`, returning JSON `{"id", "status", "accepted", "rejected", "undecided"}` for that record's mood. 404 for an unknown id, 400 for a verdict that is neither word.

- [ ] **Step 1: Write the failing tests**

```python
def test_verdict_accepts_and_moves_the_file_into_png(tmp_path):
    und = record(1, status=art_status.CANDIDATE)
    client = build(tmp_path, [und], candidates=[und])
    assets = tmp_path / "tools" / "assets"

    resp = client.post("/verdict", json={"id": "1", "verdict": "accept"})

    assert resp.get_json()["status"] == art_status.ACCEPTED
    assert (assets / "png" / "HORROR" / "HOUSE" / "hallway" / "1.png").exists()
    saved = json.loads(
        (assets / "art_manifest.json").read_text(encoding="utf-8"))
    assert saved["1"]["status"] == art_status.ACCEPTED


def test_verdict_un_accepts_and_moves_the_file_back(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    client = build(tmp_path, [acc], promoted=[acc])
    assets = tmp_path / "tools" / "assets"

    resp = client.post("/verdict", json={"id": "1", "verdict": "reject"})

    assert resp.get_json()["status"] == art_status.REJECTED
    assert not (assets / "png" / "HORROR" / "HOUSE" / "hallway"
                / "1.png").exists()
    assert (assets / "candidates" / "HORROR" / "HOUSE" / "hallway"
            / "1.png").exists()


def test_verdict_is_idempotent(tmp_path):
    und = record(1, status=art_status.CANDIDATE)
    client = build(tmp_path, [und], candidates=[und])

    first = client.post("/verdict", json={"id": "1", "verdict": "accept"})
    second = client.post("/verdict", json={"id": "1", "verdict": "accept"})

    assert first.get_json()["status"] == art_status.ACCEPTED
    assert second.get_json()["status"] == art_status.ACCEPTED
    assert second.get_json()["accepted"] == 1, \
        "applying the same verdict twice must not double-count"


def test_verdict_returns_refreshed_counts(tmp_path):
    a = record(1, status=art_status.CANDIDATE)
    b = record(2, status=art_status.CANDIDATE)
    client = build(tmp_path, [a, b], candidates=[a, b])

    body = client.post("/verdict",
                       json={"id": "1", "verdict": "accept"}).get_json()

    assert body["accepted"] == 1 and body["undecided"] == 1


def test_verdict_records_the_decision_with_no_file_present(tmp_path):
    rej = record(1, status=art_status.REJECTED)
    client = build(tmp_path, [rej])

    body = client.post("/verdict",
                       json={"id": "1", "verdict": "accept"}).get_json()

    assert body["status"] == art_status.ACCEPTED, \
        "the manifest is the decision; the file location merely follows it"


def test_verdict_404s_for_an_unknown_id(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    assert client.post("/verdict",
                       json={"id": "9999", "verdict": "accept"}).status_code == 404


def test_verdict_400s_for_a_word_that_is_not_a_verdict(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    assert client.post("/verdict",
                       json={"id": "1", "verdict": "maybe"}).status_code == 400


def test_verdict_never_touches_a_metric_rejected_record(tmp_path):
    mr = record(1, status=art_status.METRIC_REJECTED)
    client = build(tmp_path, [mr])

    client.post("/verdict", json={"id": "1", "verdict": "accept"})

    assets = tmp_path / "tools" / "assets"
    saved = json.loads(
        (assets / "art_manifest.json").read_text(encoding="utf-8"))
    assert saved["1"]["status"] == art_status.METRIC_REJECTED
```

- [ ] **Step 2: Run and verify they fail**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_server.py -q -k verdict`
Expected: FAIL — the route does not exist, so every one 404s.

- [ ] **Step 3: Implement**

Add inside `create_app`:

```python
    @app.route("/verdict", methods=["POST"])
    def verdict():
        from flask import abort, jsonify, request
        body = request.get_json(silent=True) or {}
        pid = str(body.get("id", ""))
        call = body.get("verdict", "")
        if call not in ("accept", "reject"):
            abort(400)
        path = assets / "art_manifest.json"
        manifest = fetch_art.load_manifest(path)
        rec = manifest.get(pid)
        if rec is None:
            abort(404)
        art_review.promote({pid: call}, manifest,
                           assets / "candidates", assets / "png")
        fetch_art.save_manifest(path, manifest)
        mood = rec["mood"]
        mine = [r for r in manifest.values() if r["mood"] == mood]
        return jsonify({
            "id": pid,
            "status": rec["status"],
            "accepted": sum(1 for r in mine
                            if r["status"] == art_status.ACCEPTED),
            "rejected": sum(1 for r in mine
                            if r["status"] == art_status.REJECTED),
            "undecided": sum(1 for r in mine
                             if r["status"] == art_status.CANDIDATE),
        })
```

`promote()` already skips `METRIC_REJECTED`, so the last test passes without a
guard here. Do not add one — a second guard would drift from the first.

- [ ] **Step 4: Run tests and verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_server.py -q`
Expected: PASS.

- [ ] **Step 5: Mutation-verify twice**

Remove the `fetch_art.save_manifest(path, manifest)` line and confirm
`test_verdict_accepts_and_moves_the_file_into_png` FAILS on the on-disk
assertion. Restore.

Change `if call not in ("accept", "reject")` to `if False` and confirm
`test_verdict_400s_for_a_word_that_is_not_a_verdict` FAILS. Restore.

Paste both.

- [ ] **Step 6: Full suite and commit**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q
git add tools/art_server.py tools/tests/test_art_server.py
git commit -m "art_server: apply a verdict through promote the moment it is posted"
```

---

### Task 4: The mood page, grouped and filtered

**Files:**
- Modify: `tools/art_server.py`
- Test: `tools/tests/test_art_server.py`

**Interfaces:**
- Consumes: everything above.
- Produces:
  - `groups_for(records, mood, status) -> list` — list of dicts `{"donor", "noun", "records", "accepted", "rejected", "undecided"}`, sorted by `(donor, noun)`, each `records` list sorted by id.
  - Route `GET /mood/<MOOD>`, accepting `?status=undecided|accepted|rejected|all`, defaulting to `undecided`. 404 for a mood with no vocabulary entry.

- [ ] **Step 1: Write the failing tests**

```python
def test_groups_are_sorted_and_counted(tmp_path):
    recs = [record(1, donor="HOUSE", noun="attic",
                   status=art_status.ACCEPTED),
            record(2, donor="HOUSE", noun="attic",
                   status=art_status.REJECTED),
            record(3, donor="HOUSE", noun="attic",
                   status=art_status.REJECTED),
            record(4, donor="CRYPT", noun="tomb")]
    by_id = {str(r["id"]): r for r in recs}

    groups = art_server.groups_for(by_id, "HORROR", "all")

    assert [(g["donor"], g["noun"]) for g in groups] == \
        [("CRYPT", "tomb"), ("HOUSE", "attic")]
    attic = groups[1]
    assert (attic["accepted"], attic["rejected"], attic["undecided"]) == \
        (1, 2, 0)


def test_groups_filter_by_status(tmp_path):
    recs = [record(1, status=art_status.ACCEPTED),
            record(2, status=art_status.CANDIDATE)]
    by_id = {str(r["id"]): r for r in recs}

    groups = art_server.groups_for(by_id, "HORROR", "undecided")

    ids = [r["id"] for g in groups for r in g["records"]]
    assert ids == [2]


def test_groups_never_include_metric_rejected(tmp_path):
    recs = [record(1, status=art_status.METRIC_REJECTED),
            record(2, status=art_status.CANDIDATE)]
    by_id = {str(r["id"]): r for r in recs}

    groups = art_server.groups_for(by_id, "HORROR", "all")

    ids = [r["id"] for g in groups for r in g["records"]]
    assert ids == [2], "no file has ever existed for a metric rejection"


def test_mood_page_defaults_to_undecided(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    und = record(2, status=art_status.CANDIDATE)
    client = build(tmp_path, [acc, und], promoted=[acc], candidates=[und])

    page = client.get("/mood/HORROR").get_data(as_text=True)

    assert 'data-id="2"' in page
    assert 'data-id="1"' not in page, \
        "a resumed pass shows what is left, not what is done"


def test_mood_page_all_shows_every_decided_record(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    und = record(2, status=art_status.CANDIDATE)
    client = build(tmp_path, [acc, und], promoted=[acc], candidates=[und])

    page = client.get("/mood/HORROR?status=all").get_data(as_text=True)

    assert 'data-id="1"' in page and 'data-id="2"' in page


def test_mood_page_shows_the_group_heading(tmp_path):
    und = record(1, donor="HOUSE", noun="attic")
    client = build(tmp_path, [und], candidates=[und])

    page = client.get("/mood/HORROR").get_data(as_text=True)

    assert "HOUSE / attic" in page


def test_mood_page_404s_for_an_unknown_mood(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    assert client.get("/mood/NOPE").status_code == 404


def test_mood_page_loses_no_record_to_grouping(tmp_path):
    recs = [record(1, donor="HOUSE", noun="attic"),
            record(2, donor="HOUSE", noun="cellar"),
            record(3, donor="CRYPT", noun="tomb"),
            record(4, mood="WATER", donor="WATER", noun="lake")]
    client = build(tmp_path, recs, candidates=recs)

    page = client.get("/mood/HORROR?status=all").get_data(as_text=True)

    import re as _re
    shown = set(_re.findall(r'data-id="(\d+)"', page))
    assert shown == {"1", "2", "3"}, \
        "grouping reorders and labels; it must not drop or borrow a record"


def test_mood_page_renders_a_placeholder_for_a_missing_picture(tmp_path):
    und = record(1, status=art_status.CANDIDATE)
    client = build(tmp_path, [und])

    page = client.get("/mood/HORROR").get_data(as_text=True)

    assert 'data-id="1"' in page, "the verdict must stay clickable"
    assert "pixabay.com" in page
```

- [ ] **Step 2: Run and verify they fail**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_server.py -q -k "groups or mood_page"`
Expected: FAIL — `groups_for` does not exist and the route 404s.

- [ ] **Step 3: Implement `groups_for` at module level**

```python
SHOWN = (art_status.ACCEPTED, art_status.REJECTED, art_status.CANDIDATE)

FILTERS = {
    "undecided": (art_status.CANDIDATE,),
    "accepted": (art_status.ACCEPTED,),
    "rejected": (art_status.REJECTED,),
    "all": SHOWN,
}


def groups_for(records, mood, status):
    """Split one mood's pictures into donor/noun sections.

    Description: The sections mirror the on-disk tree
        tools/assets/png/<MOOD>/<DONOR>/<noun>/, which is the subdivision the
        owner is filling toward a target -- so each carries its own counts and a
        thin noun is visible without arithmetic. Counts describe the whole
        group, not the filtered view, because "two accepted here already" is
        what decides whether to keep a third. METRIC_REJECTED never appears:
        the fetcher writes no file for one, so there is nothing to show.
    Author: suinevere
    Dependencies: art_status
    Globals: SHOWN, FILTERS
    Params: records -- the manifest dict; mood -- the mood folder name;
        status -- one of FILTERS' keys; anything else is treated as "all"
    Returns: list of group dicts sorted by (donor, noun)
    """
    wanted = FILTERS.get(status, SHOWN)
    mine = [r for r in records.values()
            if r["mood"] == mood and r["status"] in SHOWN]
    keys = sorted({(r["donor"], r["noun"]) for r in mine})
    out = []
    for donor, noun in keys:
        group = [r for r in mine
                 if r["donor"] == donor and r["noun"] == noun]
        shown = sorted((r for r in group if r["status"] in wanted),
                       key=lambda r: r["id"])
        out.append({
            "donor": donor, "noun": noun, "records": shown,
            "accepted": sum(1 for r in group
                            if r["status"] == art_status.ACCEPTED),
            "rejected": sum(1 for r in group
                            if r["status"] == art_status.REJECTED),
            "undecided": sum(1 for r in group
                             if r["status"] == art_status.CANDIDATE),
        })
    return out
```

- [ ] **Step 4: Implement the route and template**

Add inside `create_app`:

```python
    @app.route("/mood/<mood>")
    def mood_page(mood):
        from flask import abort, render_template_string, request
        if mood not in _targets(assets):
            abort(404)
        manifest = fetch_art.load_manifest(assets / "art_manifest.json")
        status = request.args.get("status", "undecided")
        groups = groups_for(manifest, mood, status)
        have = set()
        for root in (assets / "png", assets / "candidates"):
            for g in groups:
                for r in g["records"]:
                    if (Path(root) / art_review._rel(r)).exists():
                        have.add(str(r["id"]))
        return render_template_string(MOOD_HTML, mood=mood, groups=groups,
                                      status=status, have=have)
```

And at module level:

```python
MOOD_HTML = """<!doctype html><meta charset="utf-8"><title>{{ mood }}</title>
<style>body{background:#111;color:#ddd;font:13px sans-serif;margin:24px}
a{color:#8cf}h2{margin:26px 0 6px;font-size:14px;border-bottom:1px solid #333}
figure{display:inline-block;margin:6px;text-align:center;width:320px}
figcaption{font-size:11px}
.gone{width:320px;height:224px;background:#222;color:#666;display:flex;
align-items:center;justify-content:center}
figure.accepted{outline:3px solid #4a8}figure.rejected{opacity:.35}
figure:focus{outline:3px solid #8cf}
#big{position:fixed;inset:0;background:#000d;display:none;
align-items:center;justify-content:center}#big img{max-width:95vw}</style>
<p><a href="/">&larr; all moods</a> &middot;
{% for f in ["undecided","accepted","rejected","all"] %}
<a href="/mood/{{ mood }}?status={{ f }}">{{ f }}</a>
{% endfor %}</p>
<h1>{{ mood }}</h1>
{% for g in groups %}
<h2>{{ g.donor }} / {{ g.noun }} &mdash;
{{ g.accepted }} accepted &middot; {{ g.rejected }} rejected &middot;
{{ g.undecided }} undecided</h2>
{% for r in g.records %}
<figure data-id="{{ r.id }}" tabindex="0" class="{{ r.status }}">
{% if r.id|string in have %}<img src="/image/{{ r.id }}" width="320" height="224" alt="">
{% else %}<div class="gone">no local copy</div>{% endif %}
<figcaption>{{ r.phrase }}<br>
<a href="{{ r.page_url }}" target="_blank">{{ r.id }}</a>
<span class="st">{{ r.status }}</span><br>
<button onclick="v('{{ r.id }}','accept')">accept</button>
<button onclick="v('{{ r.id }}','reject')">reject</button>
</figcaption></figure>
{% endfor %}{% endfor %}
<div id="big" onclick="this.style.display='none'"><img></div>
<script>
function v(id, call){
  fetch('/verdict', {method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({id:id, verdict:call})})
  .then(function(r){ return r.json(); })
  .then(function(d){
    var f = document.querySelector('figure[data-id="'+d.id+'"]');
    f.className = d.status;
    f.querySelector('.st').textContent = d.status;
  });
}
document.querySelectorAll('figure img').forEach(function(img){
  img.addEventListener('click', function(){
    var b = document.getElementById('big');
    b.querySelector('img').src = img.src;
    b.style.display = 'flex';
  });
});
document.addEventListener('keydown', function(e){
  var f = document.activeElement;
  if (!f || f.tagName !== 'FIGURE') return;
  if (e.key === 'a' || e.key === 'A') { v(f.dataset.id, 'accept'); next(f); }
  if (e.key === 'r' || e.key === 'R') { v(f.dataset.id, 'reject'); next(f); }
  if (e.key === 'ArrowRight') next(f);
  if (e.key === 'ArrowLeft') prev(f);
});
function all(){ return Array.prototype.slice.call(
  document.querySelectorAll('figure')); }
function next(f){ var a = all(), i = a.indexOf(f);
  if (i > -1 && a[i+1]) a[i+1].focus(); }
function prev(f){ var a = all(), i = a.indexOf(f);
  if (i > 0) a[i-1].focus(); }
</script>
"""
```

- [ ] **Step 5: Run tests and verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_server.py -q`
Expected: PASS.

- [ ] **Step 6: Mutation-verify twice**

Change `wanted = FILTERS.get(status, SHOWN)` to `wanted = SHOWN` and confirm
`test_mood_page_defaults_to_undecided` FAILS. Restore.

Change `keys = sorted(...)` to `keys = list(...)` over a set and confirm
`test_groups_are_sorted_and_counted` FAILS — a set's order is not the sorted
order. If it happens to pass, the fixture is too small to expose it; add a third
donor until it does. Restore.

Paste both.

- [ ] **Step 7: Full suite and commit**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q
git add tools/art_server.py tools/tests/test_art_server.py
git commit -m "art_server: render each mood grouped by donor and noun with a status filter"
```

---

### Task 5: Retire the static sheets

**Files:**
- Modify: `tools/art_review.py` — delete `_thumb_uri`, `_status_label`, `_cell`, `_group_section`, `sheet`, `index_page`, `SHOWN`, and the `--sheets` branch of `main`
- Modify: `tools/tests/test_art_review.py` — delete the tests covering those
- Modify: `tools/convert-backgrounds.sh` — drop the promote step
- Modify: `saturn/pre.makefile` — revert the message

**Interfaces:**
- Consumes: the server, which now covers this ground.
- Produces: `main(argv)` supporting only `--promote <path>`; the no-argument folder-glob form goes with the browser round trip that produced those files.

- [ ] **Step 1: Delete the sheet renderer**

Remove from `tools/art_review.py`: `_thumb_uri`, `_status_label`, `_cell`, `_group_section`, `sheet`, and `index_page`. Remove the now-unused `base64` and `html` imports. Update the module docstring, which describes contact sheets, to describe what the module still does: dedup, promotion, and refetching.

**`SHOWN` stays.** `refetch_missing` uses it to decide which records are worth
restoring, so it is not sheet-only. `art_server.py` defines its own copy in Task
4 rather than importing this one — the two answer different questions (which
records have a picture worth fetching, versus which are reviewable) and happen
to hold the same three statuses today. Do not merge them.

`dedup`, `_hamming`, `HAMMING_MAX`, `Counts`, `_rel`, `_move_if_absent`,
`promote`, and `refetch_missing` all stay.

- [ ] **Step 2: Reduce `main` to the promote path**

`main(argv, repo=None)` keeps only:

```python
    if argv and argv[0] == "--promote" and len(argv) >= 2:
        with open(argv[1], "r", encoding="utf-8") as fh:
            verdicts = json.load(fh)
        counts = promote(verdicts, manifest, assets / "candidates",
                         assets / "png")
        fetch_art.save_manifest(manifest_path, manifest)
        for mood in sorted(counts):
            print(f"  {mood}: +{counts[mood].gained} -{counts[mood].lost}")
        return 0

    print("  usage: art_review.py --promote <verdicts.json>")
    print("  review runs at http://127.0.0.1:8080 -- python tools/art_server.py")
    return 0
```

- [ ] **Step 3: Delete the sheet tests**

From `tools/tests/test_art_review.py`, delete every test naming `sheet` or
`index_page`, and the `--sheets` `main` tests. Keep every `promote`, `dedup`,
`refetch_missing`, and `--promote` test. Keep `make_promoted` and
`make_candidate` — the remaining tests use them.

`test_a_manifest_only_clone_round_trips_a_decision` depends on `--sheets`. Its
property — a manifest-only clone can reverse a decision — now belongs to the
server, and Task 3's `test_verdict_records_the_decision_with_no_file_present`
already covers the promote half. Delete it here rather than leave it broken.

- [ ] **Step 4: Drop the promote step from the build**

In `tools/convert-backgrounds.sh`, delete the `art_review.py --promote` block
added earlier along with its comment. The failure mode it guarded — a verdicts
file downloaded but never applied — cannot occur once verdicts apply on click.

In `saturn/pre.makefile`, revert the message to:

```
	$(info ****** Converting PNG backgrounds to TGA ******)
```

- [ ] **Step 5: Run the full suite**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q`
Expected: PASS. Report the count and how it moved from 129 — a drop is expected
here, since sheet coverage was deleted and the server's lives in its own file.

- [ ] **Step 6: Verify the build still works**

Run: `sh tools/convert-backgrounds.sh`
Expected: converts, prints per-mood counts, exits 0. Run it a second time and
confirm `git status` shows no change to `saturn/src/video/category_art.inc` —
the step must still be idempotent.

- [ ] **Step 7: Commit**

```bash
git add tools/art_review.py tools/tests/test_art_review.py \
        tools/convert-backgrounds.sh saturn/pre.makefile
git commit -m "art_review: retire the static contact sheets now the review server replaces them"
```

---

### Task 6: Run it against the real tree

**Files:** none — verification only.

- [ ] **Step 1: Start the server**

Run: `tools/.venv/Scripts/python.exe tools/art_server.py`
Expected: serves on `http://127.0.0.1:8080`. Leave it running for the checks
below, then stop it.

- [ ] **Step 2: Check the index against known numbers**

The manifest currently holds 119 accepted, 169 rejected, 480 undecided and 391
metric-rejected across 1159 records. Confirm the index totals match the first
three, that SCIFI shows 0 accepted, and that no metric-rejected record is
counted anywhere.

- [ ] **Step 3: Check a mood page**

Open `/mood/HORROR`. Confirm the default view shows only undecided pictures,
that group headings read `DONOR / noun` with counts, that pictures load, and
that `?status=all` shows accepted and rejected too.

- [ ] **Step 4: Check a verdict round trip**

Accept one picture. Confirm the tile updates without a page reload, the file
appears under `tools/assets/png/<MOOD>/<DONOR>/<noun>/`, and `git status` shows
it as a new file. Reject the same picture and confirm it returns to
`tools/assets/candidates/` and the `png/` copy is gone.

- [ ] **Step 5: Report, do not commit**

Report the index totals and whether they matched. Any picture promoted during
this check is a real curation decision — leave it, and say which id you touched
so the owner can undo it if it was only a test.

---

## Done when

- `tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q` passes.
- Every mutation named in Tasks 1–4 was performed, observed to fail, and restored, with output in the task report.
- `python tools/art_server.py` serves the review UI on `127.0.0.1:8080`, and a missing Flask or a bound port prints an actionable line and exits 0.
- A verdict applies on click: manifest written, file moved, tile updated, no page reload.
- The mood page groups by `DONOR / noun` with per-group counts and filters by status, defaulting to undecided.
- `METRIC_REJECTED` records appear nowhere in the UI.
- `sheet`, `index_page` and `--sheets` are gone, and `convert-backgrounds.sh` no longer runs promote.
- No API key appears anywhere: `git log -p | grep -i pixabay_api_key` returns nothing but the environment variable's name.
