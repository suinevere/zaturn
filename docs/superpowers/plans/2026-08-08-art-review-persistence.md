# Art Review Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every human review decision reversible, sourced from the committed manifest rather than from files or browser state that a fresh clone does not have.

**Architecture:** `art_manifest.json` becomes the single source of truth. `sheet()` renders stored status instead of only undecided candidates, `promote()` becomes a state machine that moves files to match the status it writes, and an index page ties the twelve mood sheets together. `candidates/`, `sheets/` and `localStorage` are all disposable caches rebuilt from the manifest.

**Tech Stack:** Python 3.9+, Pillow, `pytest`. No Saturn code, no C compiler, no network.

**Spec:** `docs/superpowers/specs/2026-08-08-art-review-persistence-design.md`

## Global Constraints

- **Python 3.9 floor.** `tools/convert-backgrounds.sh:36` gates on `sys.version_info >= (3, 9)`. No `match`, no PEP 604 `X | Y` annotations, no `dict[str, int]` at runtime without `from __future__ import annotations`.
- **Every stage degrades, none aborts.** A missing file, an unreadable manifest, or a failed download must print an actionable message and let the rest of the run continue.
- **No network in tests.** Every test stubs the HTTP layer.
- **Sheets stay self-contained.** Every `img src` is a `data:` URI. The only external links are to `pixabay.com`. A Task 5 reviewer verified this directly and it must survive.
- **Comment style is mandatory.** Every module, function and constant gets a docstring carrying Description, Author: suinevere, Dependencies, Globals, Params, Returns, using `N/A` where inapplicable. Tests get a module docstring only. **No comments inside function bodies.**
- **Status vocabulary comes from `art_status`.** No bare status strings anywhere.
- **No API key in any file, fixture, or commit message.**
- **Run the whole suite, both directories:** `tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q`. A `test_make_tga.py` break once survived to `main` because every command in an earlier plan was `pytest saturn/tests/` and nobody looked at `tools/tests/`.
- **Commit after every task.** One sentence, no body, no bullets, no trailers, no mention of Claude/AI/session.

## The reconciliation rule

This is the one design decision the spec left implicit, and both Task 1 and Task 7
depend on it.

Two situations both look like "the candidate file exists while the status says
`ACCEPTED`", and they need opposite handling:

- **A stray leftover.** The image was promoted, and something re-created a file
  under `candidates/`. Re-promoting it would defeat the status guard. An existing
  test, `test_promotion_is_idempotent_even_if_the_source_file_reappears`, pins
  this exact case and must keep passing.
- **A refetched clone.** A fresh checkout had no pixels, `--sheets --refetch`
  restored the file into `candidates/`, and the tracked `png/` copy is genuinely
  absent. Here the file *should* move.

The distinguisher is whether the destination already exists:

> **Move a file only when the destination is missing.** If the destination is
> already there, leave the source alone.

That satisfies both cases with one rule, and it makes `promote()` self-healing
without making it re-promote.

## File structure

| File | Responsibility | Change |
|---|---|---|
| `tools/art_review.py` | Sheets, index, dedup, promotion | Modified throughout |
| `tools/tests/test_art_review.py` | Coverage for all of the above | Extended |

No new modules. `art_review.py` is ~250 lines and stays coherent; splitting it
would separate the sheet from the promotion it round-trips with.

---

### Task 1: Promotion becomes a state machine

**Files:**
- Modify: `tools/art_review.py` — `promote()`, plus new `_rel()`, `_move_if_absent()`, `Counts`
- Test: `tools/tests/test_art_review.py`

**Interfaces:**
- Consumes: `art_status.ACCEPTED`, `REJECTED`, `CANDIDATE`, `METRIC_REJECTED`.
- Produces:
  - `Counts` — `namedtuple("Counts", "gained lost")`.
  - `promote(verdicts, manifest, candidates_dir, png_dir) -> dict` mapping mood to a `Counts`. Signature unchanged; return type changes from `dict[str, int]` to `dict[str, Counts]`.
  - `_rel(rec) -> Path` — the `<mood>/<donor>/<noun>/<id>.png` relative path.
  - `_move_if_absent(src, dst) -> bool` — copies and unlinks only when `dst` does not exist; returns whether the file now sits at `dst`.

- [ ] **Step 1: Write the failing tests**

Add to `tools/tests/test_art_review.py`:

```python
def make_promoted(root, rec):
    d = root / rec["mood"] / rec["donor"] / rec["noun"]
    d.mkdir(parents=True, exist_ok=True)
    p = d / f"{rec['id']}.png"
    Image.new("RGB", (320, 224), (90, 90, 90)).save(p, "PNG")
    return p


def test_promote_unaccepts_by_moving_the_file_back(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.ACCEPTED)
    kept = make_promoted(png, rec)
    manifest = {"1": rec}

    art_review.promote({"1": "reject"}, manifest, cand, png)

    assert rec["status"] == art_status.REJECTED
    assert not kept.exists(), "the tracked png must be removed"
    assert (cand / "HORROR" / "HOUSE" / "hallway" / "1.png").exists(), \
        "the file must return to candidates so it can be re-accepted"


def test_promote_re_accepts_a_rejected_image(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.REJECTED)
    make_candidate(cand, rec)
    manifest = {"1": rec}

    counts = art_review.promote({"1": "accept"}, manifest, cand, png)

    assert rec["status"] == art_status.ACCEPTED
    assert (png / "HORROR" / "HOUSE" / "hallway" / "1.png").exists()
    assert counts["HORROR"].gained == 1


def test_promote_counts_gains_and_losses_separately(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    up = record(1, status=art_status.REJECTED)
    down = record(2, status=art_status.ACCEPTED)
    make_candidate(cand, up)
    make_promoted(png, down)
    manifest = {"1": up, "2": down}

    counts = art_review.promote({"1": "accept", "2": "reject"}, manifest,
                                cand, png)

    assert counts["HORROR"] == art_review.Counts(gained=1, lost=1)


def test_promote_never_touches_a_metric_rejected_record(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.METRIC_REJECTED)
    manifest = {"1": rec}

    art_review.promote({"1": "accept"}, manifest, cand, png)

    assert rec["status"] == art_status.METRIC_REJECTED, \
        "a metric rejection is not a human decision and has no file to move"


def test_promote_records_the_decision_when_no_file_exists(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.REJECTED)
    manifest = {"1": rec}

    art_review.promote({"1": "accept"}, manifest, cand, png)

    assert rec["status"] == art_status.ACCEPTED, \
        "a fresh clone has no pixels; the manifest still holds the decision"


def test_promote_leaves_a_stray_candidate_alone_when_already_promoted(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.ACCEPTED)
    make_promoted(png, rec)
    stray = make_candidate(cand, rec)
    manifest = {"1": rec}

    counts = art_review.promote({"1": "accept"}, manifest, cand, png)

    assert stray.exists(), "destination exists, so the stray must not be moved"
    assert counts == {}, "no status changed, so nothing was gained or lost"
```

- [ ] **Step 2: Run them and verify they fail**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q -k "unaccepts or re_accepts or gains_and_losses or metric_rejected or no_file_exists or stray"`

Expected: FAIL. `Counts` does not exist, and `promote` returns early on any non-`CANDIDATE` record.

- [ ] **Step 3: Implement**

Add `from collections import namedtuple` to the imports. Replace `promote()` and add the two helpers:

```python
Counts = namedtuple("Counts", "gained lost")


def _rel(rec):
    """Build a record's path relative to either image tree.

    Description: The candidates tree and the source tree share one layout, so
        one relative path locates a picture in both.
    Author: suinevere
    Dependencies: pathlib
    Globals: N/A
    Params: rec -- a manifest record
    Returns: Path of <mood>/<donor>/<noun>/<id>.png
    """
    return (Path(rec["mood"]) / rec["donor"] / rec["noun"]
            / "{}.png".format(rec["id"]))


def _move_if_absent(src, dst):
    """Move a picture between trees, but never over an existing destination.

    Description: A destination that already exists means the move has happened
        before and the source is a leftover -- moving it again would re-promote
        a picture the status guard has already settled. See the reconciliation
        rule in this feature's plan.
    Author: suinevere
    Dependencies: pathlib
    Globals: N/A
    Params: src -- where the picture is; dst -- where it belongs
    Returns: True if the picture now sits at dst, False if neither tree has it
    """
    src, dst = Path(src), Path(dst)
    if dst.exists():
        return True
    if not src.exists():
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(src.read_bytes())
    src.unlink()
    return True


def promote(verdicts, manifest, candidates_dir, png_dir):
    """Apply human verdicts, moving each picture to match its new status.

    Description: The manifest is the decision, and the file location follows it
        -- so a verdict is recorded even when no picture is on disk, which is
        the state of every rejected image in a fresh clone. A metric rejection
        is not a human decision and is never overwritten here; art_status
        explains why the two must not blur together.
    Author: suinevere
    Dependencies: pathlib, art_status
    Globals: N/A
    Params: verdicts -- id -> "accept"/"reject"; manifest -- mutated in place;
        candidates_dir -- the git-ignored tree; png_dir -- tools/assets/png
    Returns: dict mapping mood to a Counts of gained and lost pictures
    """
    counts = {}
    for key, call in verdicts.items():
        rec = manifest.get(key)
        if rec is None or rec["status"] == art_status.METRIC_REJECTED:
            continue

        want = (art_status.ACCEPTED if call == "accept"
                else art_status.REJECTED)
        was = rec["status"]
        rel = _rel(rec)
        cand, png = Path(candidates_dir) / rel, Path(png_dir) / rel

        if want == art_status.ACCEPTED:
            placed = _move_if_absent(cand, png)
        else:
            placed = _move_if_absent(png, cand)
        if not placed:
            print("  {}: no local copy; recording the verdict only".format(rel))

        rec["status"] = want
        if was == want:
            continue
        gained = 1 if want == art_status.ACCEPTED else 0
        lost = 1 if was == art_status.ACCEPTED else 0
        prev = counts.get(rec["mood"], Counts(0, 0))
        counts[rec["mood"]] = Counts(prev.gained + gained, prev.lost + lost)
    return counts
```

- [ ] **Step 4: Update the caller in `main()`**

The `--promote` branch prints `+{counts[mood]}`, which is now a `Counts`. Change that one line to:

```python
        for mood in sorted(counts):
            print(f"  {mood}: +{counts[mood].gained} -{counts[mood].lost}")
```

- [ ] **Step 5: Run the new tests and the two existing promotion tests**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q`

Expected: PASS, including `test_promote_moves_accepted_and_leaves_rejected` and `test_promotion_is_idempotent_even_if_the_source_file_reappears`. If the idempotency test fails, the reconciliation rule has been implemented backwards — `_move_if_absent` must return early when the destination exists.

- [ ] **Step 6: Mutation-verify the reconciliation rule**

Delete the `if dst.exists(): return True` guard from `_move_if_absent`, re-run, and confirm both `test_promotion_is_idempotent_even_if_the_source_file_reappears` and `test_promote_leaves_a_stray_candidate_alone_when_already_promoted` FAIL. Restore the guard and confirm they pass. Paste both outputs in your report.

Then change `want` to always be `art_status.ACCEPTED` and confirm `test_promote_unaccepts_by_moving_the_file_back` fails. Restore.

A test that passes both ways is worthless. This project has shipped six of them.

- [ ] **Step 7: Run the full suite and commit**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q
git add tools/art_review.py tools/tests/test_art_review.py
git commit -m "art_review: promote every verdict as a status transition so a decision can be reversed"
```

---

### Task 2: Promote a whole folder of verdicts

**Files:**
- Modify: `tools/art_review.py` — `main()` `--promote` branch
- Test: `tools/tests/test_art_review.py`

**Interfaces:**
- Consumes: `promote()` and `Counts` from Task 1.
- Produces: `main(["--promote"])` with no path argument. `main(["--promote", path])` keeps working.

- [ ] **Step 1: Write the failing test**

```python
def test_main_promote_with_no_path_applies_every_verdicts_file(tmp_path):
    assets = tmp_path / "tools" / "assets"
    sheets = assets / "sheets"
    sheets.mkdir(parents=True)
    a = record(1, mood="HORROR")
    b = record(2, mood="WILDER", donor="WILDER", noun="canyon")
    for rec in (a, b):
        make_candidate(assets / "candidates", rec)
    fetch_art.save_manifest(assets / "art_manifest.json", {"1": a, "2": b})

    (sheets / "verdicts.json").write_text('{"1": "accept"}', encoding="utf-8")
    (sheets / "verdicts(1).json").write_text('{"2": "accept"}',
                                             encoding="utf-8")

    assert art_review.main(["--promote"], repo=tmp_path) == 0

    saved = json.loads((assets / "art_manifest.json").read_text(encoding="utf-8"))
    assert saved["1"]["status"] == art_status.ACCEPTED
    assert saved["2"]["status"] == art_status.ACCEPTED, \
        "every verdicts file in the folder must be applied, not just the first"


def test_main_promote_with_no_files_says_so(tmp_path, capsys):
    assets = tmp_path / "tools" / "assets"
    (assets / "sheets").mkdir(parents=True)
    fetch_art.save_manifest(assets / "art_manifest.json", {})

    assert art_review.main(["--promote"], repo=tmp_path) == 0
    assert "no verdicts" in capsys.readouterr().out.lower()
```

- [ ] **Step 2: Run and verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q -k "promote_with_no"`
Expected: FAIL — `main` falls through to the usage message because `len(argv) >= 2` is false.

- [ ] **Step 3: Implement**

Replace the `--promote` branch condition and body:

```python
    if argv and argv[0] == "--promote":
        if len(argv) >= 2:
            paths = [Path(argv[1])]
        else:
            paths = sorted((assets / "sheets").glob("verdicts*.json"))
        if not paths:
            print("  no verdicts files found in {}".format(assets / "sheets"))
            return 0
        counts = {}
        for path in paths:
            with open(path, "r", encoding="utf-8") as fh:
                verdicts = json.load(fh)
            for mood, got in promote(verdicts, manifest,
                                     assets / "candidates",
                                     assets / "png").items():
                prev = counts.get(mood, Counts(0, 0))
                counts[mood] = Counts(prev.gained + got.gained,
                                      prev.lost + got.lost)
        fetch_art.save_manifest(manifest_path, manifest)
        for mood in sorted(counts):
            print(f"  {mood}: +{counts[mood].gained} -{counts[mood].lost}")
        return 0
```

- [ ] **Step 4: Run tests and verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q`
Expected: PASS, including the existing `test_main_promote_moves_accepted_and_updates_the_manifest_on_disk`.

- [ ] **Step 5: Mutation-verify**

Change `paths` in the no-argument branch to `paths[:1]` and confirm `test_main_promote_with_no_path_applies_every_verdicts_file` FAILS on record `"2"`. Restore.

- [ ] **Step 6: Full suite and commit**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q
git add tools/art_review.py tools/tests/test_art_review.py
git commit -m "art_review: apply every verdicts file in the sheets folder when --promote is given no path"
```

---

### Task 3: Sheets show every decision

**Files:**
- Modify: `tools/art_review.py` — `sheet()`, `_thumb_uri()`
- Test: `tools/tests/test_art_review.py`

**Interfaces:**
- Consumes: `art_status`, `_rel()` from Task 1.
- Produces: `sheet(mood, records, candidates_dir, png_dir) -> str`. **The signature gains a fourth positional parameter.** Four existing tests call it with three arguments and must be updated to pass `png_dir`.

- [ ] **Step 1: Write the failing tests**

```python
def test_sheet_shows_accepted_rejected_and_undecided_together(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    acc = record(1, status=art_status.ACCEPTED)
    rej = record(2, status=art_status.REJECTED)
    und = record(3, status=art_status.CANDIDATE)
    make_promoted(png, acc)
    for rec in (rej, und):
        make_candidate(cand, rec)
    recs = {"1": acc, "2": rej, "3": und}

    html_out = art_review.sheet("HORROR", recs, cand, png)

    for pid in ("1", "2", "3"):
        assert 'data-id="{}"'.format(pid) in html_out
    assert "accepted" in html_out and "rejected" in html_out


def test_sheet_checks_the_box_to_match_stored_status(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    acc = record(1, status=art_status.ACCEPTED)
    rej = record(2, status=art_status.REJECTED)
    make_promoted(png, acc)
    make_candidate(cand, rej)

    html_out = art_review.sheet("HORROR", {"1": acc, "2": rej}, cand, png)

    accepted_tile = html_out.split('data-id="1"')[1].split("</figure>")[0]
    rejected_tile = html_out.split('data-id="2"')[1].split("</figure>")[0]
    assert "checked" in accepted_tile
    assert "checked" not in rejected_tile, \
        "a rejected image must open unticked or the decision silently flips back"


def test_sheet_hides_metric_rejected_records(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.METRIC_REJECTED)

    html_out = art_review.sheet("HORROR", {"1": rec}, cand, png)

    assert 'data-id="1"' not in html_out, \
        "the fetcher never writes these to disk, so no tile can be rendered"


def test_sheet_renders_a_placeholder_when_the_file_is_missing(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.REJECTED)

    html_out = art_review.sheet("HORROR", {"1": rec}, cand, png)

    assert 'data-id="1"' in html_out, "the decision must stay flippable"
    assert "no local copy" in html_out
    assert "pixabay.com" in html_out


def test_sheet_stays_self_contained_with_a_placeholder(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.REJECTED)

    html_out = art_review.sheet("HORROR", {"1": rec}, cand, png)

    srcs = re.findall(r'src="([^"]*)"', html_out)
    assert all(s.startswith("data:") for s in srcs), \
        "a placeholder must not reintroduce a remote image reference"
```

- [ ] **Step 2: Run and verify they fail**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q -k "sheet"`
Expected: FAIL with `TypeError` on the four-argument calls, and on missing status labels.

- [ ] **Step 3: Implement**

Replace `sheet()`:

```python
SHOWN = (art_status.ACCEPTED, art_status.REJECTED, art_status.CANDIDATE)


def sheet(mood, records, candidates_dir, png_dir):
    """Render one mood's whole review history as a self-contained page.

    Description: Shows accepted, rejected and undecided pictures together with
        the decision currently stored for each, because curation is taste
        applied repeatedly and a verdict has to be reversible. Metric rejections
        are absent by necessity: the fetcher only ever writes gate-passing
        images, so no file has existed for them. A record whose picture is gone
        -- every rejected image in a fresh clone -- still gets a tile, so the
        decision stays flippable with no pixels and no network.
    Author: suinevere
    Dependencies: html, art_status
    Globals: SHOWN
    Params: mood -- the mood folder name; records -- the manifest dict;
        candidates_dir -- the git-ignored tree; png_dir -- tools/assets/png
    Returns: the HTML as a string
    """
    mine = [r for r in records.values()
            if r["mood"] == mood and r["status"] in SHOWN]
    mine.sort(key=lambda r: (r["donor"], r["noun"], r["id"]))

    cells = []
    for r in mine:
        root = png_dir if r["status"] == art_status.ACCEPTED else candidates_dir
        src = _thumb_uri(Path(root) / _rel(r))
        label = {art_status.ACCEPTED: "accepted",
                 art_status.REJECTED: "rejected"}.get(r["status"], "undecided")
        checked = " checked" if r["status"] != art_status.REJECTED else ""
        art = (f'<img src="{src}" width="320" height="224" alt="">' if src
               else '<div class="gone">no local copy</div>')
        cells.append(
            f'<figure data-id="{r["id"]}" data-status="{label}">'
            f'{art}'
            f'<figcaption>{html.escape(r["phrase"])}<br>'
            f'<b>{label}</b> &middot; lum {r["luminance"]} '
            f'&middot; busy {r["busyness"]} &middot; band {r["banding"]}<br>'
            f'<a href="{html.escape(r["page_url"], quote=True)}" '
            f'target="_blank">{r["id"]}</a><br>'
            f'<label><input type="checkbox"{checked}> keep</label>'
            f'</figcaption></figure>'
        )

    return (
        "<!doctype html><html><head><meta charset='utf-8'>"
        f"<title>{mood} &mdash; {len(mine)} pictures</title>"
        "<style>body{background:#111;color:#ddd;font:13px sans-serif}"
        "figure{display:inline-block;margin:8px;text-align:center}"
        "figcaption{max-width:320px}a{color:#8cf}"
        ".gone{width:320px;height:224px;background:#222;color:#666;"
        "display:flex;align-items:center;justify-content:center}"
        "</style></head><body>"
        f"<h1>{mood} &mdash; {len(mine)} pictures</h1>"
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

- [ ] **Step 4: Update the four existing three-argument `sheet()` calls**

In `test_art_review.py`, the calls at `test_sheet_is_self_contained_and_names_its_sources`, `test_sheet_with_no_candidates_still_renders`, `test_sheet_covers_only_its_own_mood` and `test_sheet_escapes_a_quote_in_the_phrase_and_page_url` pass `tmp_path` as the candidates directory. Add a fourth argument, `tmp_path / "png"`, to each. Do not change what they assert.

- [ ] **Step 5: Run tests and verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q`
Expected: PASS.

- [ ] **Step 6: Mutation-verify**

Force `checked` to always be `" checked"` and confirm `test_sheet_checks_the_box_to_match_stored_status` FAILS. Restore.

Change `SHOWN` to include `art_status.METRIC_REJECTED` and confirm `test_sheet_hides_metric_rejected_records` FAILS. Restore.

Replace the placeholder `<div>` with an `<img src="{r['image_url']}">` and confirm `test_sheet_stays_self_contained_with_a_placeholder` FAILS. Restore.

- [ ] **Step 7: Full suite and commit**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q
git add tools/art_review.py tools/tests/test_art_review.py
git commit -m "art_review: render every decided picture in its mood sheet with the verdict already stored for it"
```

---

### Task 4: Dedup stops eating decided pictures

**Files:**
- Modify: `tools/art_review.py` — `main()` `--sheets` branch
- Test: `tools/tests/test_art_review.py`

**Interfaces:**
- Consumes: `dedup()` unchanged, `sheet()` from Task 3.
- Produces: no new names. `main(["--sheets"])` now passes decided records straight through and dedups only `CANDIDATE` records.

- [ ] **Step 1: Write the failing test**

```python
def test_main_sheets_never_dedups_away_a_decided_picture(tmp_path):
    assets = tmp_path / "tools" / "assets"
    assets.mkdir(parents=True)
    twin = "f" * 16
    acc = record(1, phash=twin, status=art_status.ACCEPTED)
    rej = record(2, phash=twin, status=art_status.REJECTED)
    make_promoted(assets / "png", acc)
    make_candidate(assets / "candidates", rej)
    fetch_art.save_manifest(assets / "art_manifest.json",
                            {"1": acc, "2": rej})

    assert art_review.main(["--sheets"], repo=tmp_path) == 0

    page = (assets / "sheets" / "HORROR.html").read_text(encoding="utf-8")
    assert 'data-id="2"' in page, \
        "a rejected picture must survive dedup or its verdict cannot be reversed"
    assert 'data-id="1"' in page
```

- [ ] **Step 2: Run and verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q -k "never_dedups_away"`
Expected: FAIL — record `"2"` shares a phash with an accepted image, so the current `dedup(candidates, already_accepted)` drops it.

- [ ] **Step 3: Implement**

Replace the `--sheets` branch body up to the loop:

```python
    if argv and argv[0] == "--sheets":
        candidates = [r for r in manifest.values()
                      if r["status"] == art_status.CANDIDATE]
        decided = [r for r in manifest.values()
                   if r["status"] in (art_status.ACCEPTED,
                                      art_status.REJECTED)]
        already_accepted = [r.get("phash", "") for r in manifest.values()
                            if r["status"] == art_status.ACCEPTED]
        kept = {str(r["id"]): r
                for r in decided + dedup(candidates, already_accepted)}
        out = assets / "sheets"
        out.mkdir(parents=True, exist_ok=True)
        for mood in MOODS:
            page = out / f"{mood}.html"
            page.write_text(sheet(mood, kept, assets / "candidates",
                                  assets / "png"), encoding="utf-8")
            print(f"  {page}")
        return 0
```

- [ ] **Step 4: Run tests and verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q`
Expected: PASS, including the existing `test_main_sheets_writes_one_page_per_mood_and_seeds_dedup_from_accepted`, which still requires a *candidate* duplicate to be dropped.

- [ ] **Step 5: Mutation-verify**

Change `decided + dedup(...)` to `dedup(decided + candidates, already_accepted)` and confirm the new test FAILS. Restore.

- [ ] **Step 6: Full suite and commit**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q
git add tools/art_review.py tools/tests/test_art_review.py
git commit -m "art_review: scope dedup to undecided candidates so a decided picture keeps its tile"
```

---

### Task 5: Marks survive closing the tab

**Files:**
- Modify: `tools/art_review.py` — `sheet()` script block
- Test: `tools/tests/test_art_review.py`

**Interfaces:**
- Consumes: `sheet()` from Task 3.
- Produces: no Python names. The page gains `localStorage` persistence keyed `zaturn-art:<MOOD>:<id>` and a **Clear marks** button.

Storage is best-effort. `file://` origin handling differs between browsers, so every access is wrapped and a failure must leave the page fully usable.

- [ ] **Step 1: Write the failing test**

```python
def test_sheet_persists_marks_and_offers_to_clear_them(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.CANDIDATE)
    make_candidate(cand, rec)

    html_out = art_review.sheet("HORROR", {"1": rec}, cand, png)

    assert "localStorage" in html_out
    assert "zaturn-art:HORROR:" in html_out, \
        "marks must be namespaced per mood or two sheets collide"
    assert "Clear marks" in html_out
    assert "try{" in html_out, \
        "file:// storage can throw; the page must survive it"
```

- [ ] **Step 2: Run and verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q -k "persists_marks"`
Expected: FAIL — the script block has no storage code.

- [ ] **Step 3: Implement**

Replace the `<script>` block in `sheet()`'s return with this, interpolating `mood`:

```python
        "<p><button onclick=\"save()\">Download verdicts.json</button> "
        "<button onclick=\"clearMarks()\">Clear marks</button></p>"
        "<script>"
        f"var NS='zaturn-art:{mood}:';"
        "function box(f){return f.querySelector('input');}"
        "function store(k,v){try{localStorage.setItem(k,v);}catch(e){}}"
        "function load(k){try{return localStorage.getItem(k);}"
        "catch(e){return null;}}"
        "document.querySelectorAll('figure').forEach(function(f){"
        "var k=NS+f.dataset.id,m=load(k);"
        "if(m!==null){box(f).checked=(m==='accept');f.dataset.marked='1';}"
        "box(f).addEventListener('change',function(){"
        "store(k,box(f).checked?'accept':'reject');f.dataset.marked='1';});});"
        "function clearMarks(){"
        "document.querySelectorAll('figure').forEach(function(f){"
        "try{localStorage.removeItem(NS+f.dataset.id);}catch(e){}});"
        "location.reload();}"
        "function save(){var o={};"
        "document.querySelectorAll('figure').forEach(function(f){"
        "o[f.dataset.id]=box(f).checked?'accept':'reject';});"
        "var a=document.createElement('a');"
        "a.href=URL.createObjectURL(new Blob([JSON.stringify(o,null,2)],"
        "{type:'application/json'}));a.download='verdicts.json';a.click();}"
        "</script></body></html>"
```

Add one style rule so an unexported mark is visibly distinct from a committed one:

```
"figure[data-marked] figcaption b{color:#fd6}"
```

- [ ] **Step 4: Run tests and verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q`
Expected: PASS. The existing self-containment tests must still pass — no external script or style was added.

- [ ] **Step 5: Mutation-verify**

Drop the `f"var NS='zaturn-art:{mood}:';"` line to a bare `var NS='zaturn-art:';` and confirm `test_sheet_persists_marks_and_offers_to_clear_them` FAILS on the namespacing assertion. Restore.

- [ ] **Step 6: Full suite and commit**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q
git add tools/art_review.py tools/tests/test_art_review.py
git commit -m "art_review: keep unexported review marks in browser storage with a button to discard them"
```

---

### Task 6: An index over the twelve sheets

**Files:**
- Modify: `tools/art_review.py` — new `index_page()`, `main()` `--sheets` branch
- Test: `tools/tests/test_art_review.py`

**Interfaces:**
- Consumes: `art_status`, `MOODS` from `art_nouns`.
- Produces: `index_page(records, target=99) -> str`. `main(["--sheets"])` also writes `sheets/index.html`.

- [ ] **Step 1: Write the failing test**

```python
def test_index_counts_each_mood_and_links_to_its_sheet():
    recs = {"1": record(1, status=art_status.ACCEPTED),
            "2": record(2, status=art_status.REJECTED),
            "3": record(3, status=art_status.CANDIDATE),
            "4": record(4, status=art_status.METRIC_REJECTED)}

    page = art_review.index_page(recs)

    row = [r for r in page.split("<tr") if "HORROR.html" in r][0]
    cells = re.findall(r"<td>(\d+)</td>", row)
    assert cells == ["1", "1", "1"], \
        ("accepted, rejected, undecided -- one each, and the metric "
         "rejection must not be counted anywhere")
    assert 'href="HORROR.html"' in page
    for mood in MOODS:
        assert mood in page, "every mood needs a row even at zero"


def test_index_flags_a_mood_that_accepted_nothing():
    recs = {"1": record(1, status=art_status.REJECTED)}

    page = art_review.index_page(recs)

    assert "empty" in page.lower(), \
        "a mood with no accepted pictures is the thing the index exists to show"


def test_main_sheets_also_writes_the_index(tmp_path):
    assets = tmp_path / "tools" / "assets"
    assets.mkdir(parents=True)
    fetch_art.save_manifest(assets / "art_manifest.json", {})

    assert art_review.main(["--sheets"], repo=tmp_path) == 0
    assert (assets / "sheets" / "index.html").exists()
```

- [ ] **Step 2: Run and verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q -k "index"`
Expected: FAIL — `index_page` does not exist.

- [ ] **Step 3: Implement**

```python
def index_page(records, target=99):
    """Summarise every mood's review state and link into its sheet.

    Description: A starved mood is invisible from inside a single sheet -- the
        first curation sitting left SCIFI with nothing accepted out of
        twenty-four and nothing surfaced it. Metric rejections are excluded
        because they are not human decisions and carry no reviewable tile.
    Author: suinevere
    Dependencies: art_status, art_nouns
    Globals: N/A
    Params: records -- the manifest dict; target -- the per-mood picture goal
    Returns: the HTML as a string
    """
    rows = []
    for mood in MOODS:
        mine = [r for r in records.values() if r["mood"] == mood]
        acc = sum(1 for r in mine if r["status"] == art_status.ACCEPTED)
        rej = sum(1 for r in mine if r["status"] == art_status.REJECTED)
        und = sum(1 for r in mine if r["status"] == art_status.CANDIDATE)
        flag = ' class="empty"' if acc == 0 else ""
        rows.append(
            f'<tr{flag}><td><a href="{mood}.html">{mood}</a></td>'
            f"<td>{acc}</td><td>{rej}</td><td>{und}</td>"
            f"<td>{100 * acc // target}%</td></tr>"
        )

    return (
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<title>Room art review</title>"
        "<style>body{background:#111;color:#ddd;font:13px sans-serif}"
        "table{border-collapse:collapse}td,th{padding:4px 12px;"
        "border-bottom:1px solid #333;text-align:right}"
        "td:first-child,th:first-child{text-align:left}"
        "a{color:#8cf}tr.empty td{color:#f88}</style></head><body>"
        f"<h1>Room art review &mdash; target {target} per mood</h1>"
        "<table><tr><th>mood</th><th>accepted</th><th>rejected</th>"
        "<th>undecided</th><th>of target</th></tr>"
        + "".join(rows) +
        "</table></body></html>"
    )
```

In the `--sheets` branch, after the `for mood in MOODS:` loop:

```python
        idx = out / "index.html"
        idx.write_text(index_page(kept), encoding="utf-8")
        print(f"  {idx}")
```

Add a nav strip to `sheet()` immediately after `<body>`. `sheet()` gains no new
parameter — it derives its neighbours from `MOODS`, which it can already import:

```python
    here = MOODS.index(mood) if mood in MOODS else 0
    prev_mood = MOODS[(here - 1) % len(MOODS)]
    next_mood = MOODS[(here + 1) % len(MOODS)]
    nav = (f'<p><a href="index.html">&larr; all moods</a> &middot; '
           f'<a href="{prev_mood}.html">{prev_mood}</a> &middot; '
           f'<a href="{next_mood}.html">{next_mood}</a></p>')
```

Interpolate `nav` immediately after `<body>` in the returned string, and add
`from art_nouns import MOODS` to `art_review.py`'s imports if it is not already
there. Wrapping with `%` makes the twelve moods a ring, so no page is a dead end.

Extend the Task 5 test file with:

```python
def test_sheet_links_to_its_neighbouring_moods(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"

    html_out = art_review.sheet(MOODS[0], {}, cand, png)

    assert 'href="index.html"' in html_out
    assert 'href="{}.html"'.format(MOODS[1]) in html_out
    assert 'href="{}.html"'.format(MOODS[-1]) in html_out, \
        "the first mood wraps to the last so no page is a dead end"
```

- [ ] **Step 4: Run tests and verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q`
Expected: PASS.

- [ ] **Step 5: Mutation-verify**

Change the `flag` condition to `acc > 0` and confirm `test_index_flags_a_mood_that_accepted_nothing` FAILS. Restore.

Count `METRIC_REJECTED` records into `rej` and confirm `test_index_counts_each_mood_and_links_to_its_sheet` FAILS. Restore.

- [ ] **Step 6: Full suite and commit**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q
git add tools/art_review.py tools/tests/test_art_review.py
git commit -m "art_review: add an index page showing each mood's review state and flagging empty ones"
```

---

### Task 7: Restore missing pixels on request

**Files:**
- Modify: `tools/art_review.py` — `main()` `--sheets` branch, new `refetch_missing()`
- Test: `tools/tests/test_art_review.py`

**Interfaces:**
- Consumes: `_rel()` from Task 1, `fetch_art.PixabayFetcher`.
- Produces: `refetch_missing(records, candidates_dir, png_dir, fetcher) -> int` returning how many pictures were restored. `main(["--sheets", "--refetch"])`.

Without `--refetch`, `--sheets` performs no network access, exactly as today.

- [ ] **Step 1: Write the failing tests**

```python
class StubFetcher:
    def __init__(self, blobs):
        self.blobs = blobs
        self.asked = []

    def download(self, url):
        self.asked.append(url)
        if url not in self.blobs:
            raise OSError("404")
        return self.blobs[url]


def png_bytes():
    from io import BytesIO
    buf = BytesIO()
    Image.new("RGB", (320, 224), (10, 20, 30)).save(buf, "PNG")
    return buf.getvalue()


def test_refetch_restores_a_missing_rejected_picture(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.REJECTED)
    rec["image_url"] = "https://example.invalid/1.png"
    fetcher = StubFetcher({rec["image_url"]: png_bytes()})

    n = art_review.refetch_missing({"1": rec}, cand, png, fetcher)

    assert n == 1
    assert (cand / "HORROR" / "HOUSE" / "hallway" / "1.png").exists()


def test_refetch_skips_a_picture_already_on_disk(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.REJECTED)
    make_candidate(cand, rec)
    fetcher = StubFetcher({})

    assert art_review.refetch_missing({"1": rec}, cand, png, fetcher) == 0
    assert fetcher.asked == [], "no network for a picture that is already here"


def test_refetch_degrades_when_a_download_fails(tmp_path, capsys):
    cand, png = tmp_path / "c", tmp_path / "png"
    a = record(1, status=art_status.REJECTED)
    b = record(2, status=art_status.REJECTED, noun="cellar")
    a["image_url"] = "https://example.invalid/gone.png"
    b["image_url"] = "https://example.invalid/2.png"
    fetcher = StubFetcher({b["image_url"]: png_bytes()})

    n = art_review.refetch_missing({"1": a, "2": b}, cand, png, fetcher)

    assert n == 1, "one failure must not abort the rest of the run"
    assert "gone.png" in capsys.readouterr().out


def test_main_sheets_makes_no_network_call_without_the_flag(tmp_path):
    assets = tmp_path / "tools" / "assets"
    assets.mkdir(parents=True)
    rec = record(1, status=art_status.REJECTED)
    rec["image_url"] = "https://example.invalid/1.png"
    fetch_art.save_manifest(assets / "art_manifest.json", {"1": rec})

    assert art_review.main(["--sheets"], repo=tmp_path) == 0
    page = (assets / "sheets" / "HORROR.html").read_text(encoding="utf-8")
    assert "no local copy" in page
```

- [ ] **Step 2: Run and verify they fail**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q -k "refetch or no_network"`
Expected: FAIL — `refetch_missing` does not exist.

- [ ] **Step 3: Implement**

```python
def refetch_missing(records, candidates_dir, png_dir, fetcher):
    """Re-download pictures the manifest knows about but the disk has lost.

    Description: A fresh clone has no candidates tree, so every rejected
        picture's pixels are gone while its verdict survives in the manifest.
        Restores them into the candidates tree from the recorded image_url.
        A failed download reports and is skipped -- the sheet falls back to a
        placeholder tile and the run continues.
    Author: suinevere
    Dependencies: PIL, pathlib, art_status
    Globals: N/A
    Params: records -- the manifest dict; candidates_dir -- the git-ignored
        tree; png_dir -- tools/assets/png; fetcher -- anything with download()
    Returns: how many pictures were restored
    """
    restored = 0
    for rec in records.values():
        if rec["status"] not in SHOWN:
            continue
        rel = _rel(rec)
        if (Path(png_dir) / rel).exists() or (Path(candidates_dir)
                                              / rel).exists():
            continue
        url = rec.get("image_url", "")
        if not url:
            print("  {}: no image_url recorded, cannot restore".format(rel))
            continue
        try:
            im = Image.open(BytesIO(fetcher.download(url)))
            im.load()
        except Exception as exc:
            print("  {}: refetch failed ({})".format(url, exc))
            continue
        dst = Path(candidates_dir) / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        im.convert("RGB").save(dst, "PNG")
        restored += 1
    return restored
```

Add `from io import BytesIO` and `from PIL import Image` to the imports, and update the module docstring's Dependencies line to include them.

In `main()`, inside the `--sheets` branch before building `kept`:

```python
        if "--refetch" in argv:
            key = os.environ.get("PIXABAY_API_KEY", "")
            if not key:
                fetch_art.load_dotenv_into_environ()
                key = os.environ.get("PIXABAY_API_KEY", "")
            if key:
                n = refetch_missing(manifest, assets / "candidates",
                                    assets / "png",
                                    fetch_art.PixabayFetcher(key))
                print(f"  restored {n} missing picture(s)")
            else:
                print("  PIXABAY_API_KEY is not set; keeping placeholders")
```

Add `import os` to the imports.

- [ ] **Step 4: Run tests and verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q`
Expected: PASS.

- [ ] **Step 5: Mutation-verify**

Remove the two `.exists()` short-circuits and confirm `test_refetch_skips_a_picture_already_on_disk` FAILS on `fetcher.asked`. Restore.

Change `except Exception` to re-raise and confirm `test_refetch_degrades_when_a_download_fails` FAILS. Restore.

- [ ] **Step 6: Full suite and commit**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q
git add tools/art_review.py tools/tests/test_art_review.py
git commit -m "art_review: restore missing pictures from their recorded urls when --sheets is given --refetch"
```

---

### Task 8: The fresh-clone round-trip

**Files:**
- Test: `tools/tests/test_art_review.py`

**Interfaces:**
- Consumes: everything from Tasks 1–7. No production code changes.

This is the property the whole design exists to guarantee, and it is worth its own
end-to-end test rather than trusting the unit tests to imply it.

- [ ] **Step 1: Write the test**

```python
def test_a_manifest_only_clone_round_trips_a_decision(tmp_path):
    assets = tmp_path / "tools" / "assets"
    assets.mkdir(parents=True)
    rec = record(1, status=art_status.REJECTED)
    fetch_art.save_manifest(assets / "art_manifest.json", {"1": rec})

    assert art_review.main(["--sheets"], repo=tmp_path) == 0
    page = (assets / "sheets" / "HORROR.html").read_text(encoding="utf-8")
    assert 'data-id="1"' in page and "no local copy" in page

    (assets / "sheets" / "verdicts.json").write_text(
        '{"1": "accept"}', encoding="utf-8")
    assert art_review.main(["--promote"], repo=tmp_path) == 0

    saved = json.loads(
        (assets / "art_manifest.json").read_text(encoding="utf-8"))
    assert saved["1"]["status"] == art_status.ACCEPTED, \
        "no pixels and no network, and the decision still reverses"

    assert art_review.main(["--sheets"], repo=tmp_path) == 0
    page = (assets / "sheets" / "HORROR.html").read_text(encoding="utf-8")
    tile = page.split('data-id="1"')[1].split("</figure>")[0]
    assert "checked" in tile and "accepted" in tile
```

- [ ] **Step 2: Run it**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_review.py -q -k "round_trips"`
Expected: PASS. If it fails, an earlier task regressed — fix that task rather than weakening this test.

- [ ] **Step 3: Mutation-verify**

In `promote()`, restore the old `if rec["status"] != art_status.CANDIDATE: continue` guard and confirm this test FAILS at the `ACCEPTED` assertion. Restore.

- [ ] **Step 4: Full suite and commit**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q
git add tools/tests/test_art_review.py
git commit -m "tests: pin that a manifest-only clone can reverse a decision with no pixels and no network"
```

---

### Task 9: Regenerate the sheets against the real manifest

**Files:**
- Modify: `tools/assets/sheets/*.html` (git-ignored, regenerated not committed)

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Regenerate**

```bash
tools/.venv/Scripts/python.exe tools/art_review.py --sheets
```

Expected: thirteen files written — twelve moods plus `index.html`.

- [ ] **Step 2: Verify against the known state**

The manifest holds 120 accepted, 168 rejected and 124 metric-rejected records. Confirm:

- `index.html` totals 120 accepted and 168 rejected across all moods.
- SCIFI's row shows 0 accepted and is flagged.
- Each mood page tiles exactly its accepted plus rejected count, and no metric-rejected id appears.
- Every `img src` still starts with `data:`.

- [ ] **Step 3: Report, do not commit**

`tools/assets/sheets/` is git-ignored. Nothing to commit here. Report the index totals so they can be checked against the numbers above.

---

## Done when

- `tools/.venv/Scripts/python.exe -m pytest tools/tests/ saturn/tests/ -q` passes.
- Every mutation listed in Tasks 1–8 was performed, observed to fail, and restored, with the failing output recorded in the task report.
- A rejected picture can be re-accepted, and an accepted one un-accepted, with the file moving between `png/` and `candidates/` to match.
- `--promote` with no argument applies every verdicts file in `tools/assets/sheets/`.
- `--sheets` makes no network call; `--sheets --refetch` restores missing pictures and degrades on failure.
- A manifest with no image files renders flippable placeholder tiles and round-trips a decision offline.
- `sheets/index.html` shows per-mood counts and flags a mood with nothing accepted.
- No API key appears anywhere: `git log -p | grep -i pixabay_api_key` returns nothing but the environment variable's name.
