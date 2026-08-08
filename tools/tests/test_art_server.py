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


def build(tmp_path, records, promoted=(), candidates=(), target=99):
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
                        "target": target}
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
    client = build(tmp_path, [record(1)], candidates=[record(1)], target=40)

    page = client.get("/").get_data(as_text=True)

    assert "40" in page, "target comes from art_queries.json, not a constant"
    assert "99" not in page, \
        "a hardcoded 99 must not coincidentally satisfy this assertion"


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


def test_groups_are_sorted_and_counted(tmp_path):
    recs = [record(1, donor="HOUSE", noun="attic",
                   status=art_status.ACCEPTED),
            record(2, donor="HOUSE", noun="attic",
                   status=art_status.REJECTED),
            record(3, donor="HOUSE", noun="attic",
                   status=art_status.REJECTED),
            record(4, donor="CRYPT", noun="tomb"),
            record(5, donor="ABBEY", noun="vault"),
            record(6, donor="MOTEL", noun="lobby")]
    by_id = {str(r["id"]): r for r in recs}

    groups = art_server.groups_for(by_id, "HORROR", "all")

    assert [(g["donor"], g["noun"]) for g in groups] == \
        [("ABBEY", "vault"), ("CRYPT", "tomb"), ("HOUSE", "attic"),
         ("MOTEL", "lobby")]
    attic = groups[2]
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
