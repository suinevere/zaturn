"""Cover the local review server's routes, verdicts, filtering and grouping."""
import json
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_server
import art_status
import fetch_art


def record(pid, scene="CAVE", noun="hallway", status=art_status.CANDIDATE):
    return {"id": pid, "page_url": f"https://pixabay.com/photos/{pid}/",
            "image_url": "", "phrase": "dark hallway", "scene": scene,
            "noun": noun, "licence": "Pixabay Content License",
            "fetched": "2026-08-08", "luminance": 70.0, "busyness": 4.0,
            "banding": 2.0, "verdict": "pass", "phash": "0" * 16,
            "status": status}


def old_record(pid, mood="HORROR", donor="HOUSE", noun="hallway",
                status=art_status.CANDIDATE):
    return {"id": pid, "page_url": f"https://pixabay.com/photos/{pid}/",
            "image_url": "", "phrase": "dark hallway", "mood": mood,
            "donor": donor, "noun": noun, "licence": "Pixabay Content License",
            "fetched": "2026-08-08", "luminance": 70.0, "busyness": 4.0,
            "banding": 2.0, "verdict": "pass", "phash": "0" * 16,
            "status": status}


def write_png(root, rec):
    scene = rec.get("scene", rec.get("mood"))
    donor = rec.get("donor")
    parts = [scene, donor, rec["noun"]] if donor else [scene, rec["noun"]]
    d = root.joinpath(*parts)
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
    return art_server.create_app(repo=tmp_path).test_client()


def test_index_shows_asymmetric_per_scene_counts_in_the_right_columns(tmp_path):
    recs = [record(1, status=art_status.ACCEPTED),
            record(2, status=art_status.ACCEPTED),
            record(3, status=art_status.ACCEPTED),
            record(4, status=art_status.REJECTED),
            record(5, status=art_status.REJECTED),
            record(6, status=art_status.CANDIDATE)]
    client = build(tmp_path, recs, candidates=recs)

    page = client.get("/").get_data(as_text=True)

    import re as _re
    row = _re.search(r"<tr>(?:(?!<tr>).)*?CAVE(?:(?!<tr>).)*?</tr>",
                     page, _re.S).group(0)
    cells = _re.findall(r"<td>(.*?)</td>", row, _re.S)
    assert cells[1] == "3", "accepted column must show the accepted count"
    assert cells[2] == "2", "rejected column must show the rejected count"
    assert cells[3] == "1", "undecided column must show the undecided count"


def test_index_lists_every_scene_with_its_counts(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    rej = record(2, status=art_status.REJECTED)
    und = record(3, status=art_status.CANDIDATE)
    client = build(tmp_path, [acc, rej, und], promoted=[acc],
                   candidates=[rej, und])

    page = client.get("/").get_data(as_text=True)

    assert "CAVE" in page
    for scene in art_server.vocab.SCENES:
        assert scene in page, f"{scene}: every scene must get a row"


def test_index_shows_the_flat_per_scene_target(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    page = client.get("/").get_data(as_text=True)

    assert str(art_server.PER_SCENE_TARGET) in page


def test_index_still_lists_every_scene_with_an_old_shape_manifest(tmp_path):
    """A record left over from before the scene migration carries mood and
    donor, not scene -- the index must not crash and must still show every
    scene, even though the old mood never names a current scene. WILDER is
    a genuine one of the twelve legacy moods and, unlike DESERT, does not
    collide with a real scene name."""
    acc = old_record(1, mood="WILDER", donor="WILDER", noun="dune",
                     status=art_status.ACCEPTED)
    client = build(tmp_path, [acc], promoted=[acc])

    page = client.get("/").get_data(as_text=True)

    for scene in art_server.vocab.SCENES:
        assert scene in page


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
    assert (assets / "png" / "CAVE" / "hallway" / "1.png").exists()
    saved = json.loads(
        (assets / "art_manifest.json").read_text(encoding="utf-8"))
    assert saved["1"]["status"] == art_status.ACCEPTED


def test_verdict_un_accepts_and_moves_the_file_back(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    client = build(tmp_path, [acc], promoted=[acc])
    assets = tmp_path / "tools" / "assets"

    resp = client.post("/verdict", json={"id": "1", "verdict": "reject"})

    assert resp.get_json()["status"] == art_status.REJECTED
    assert not (assets / "png" / "CAVE" / "hallway" / "1.png").exists()
    assert (assets / "candidates" / "CAVE" / "hallway" / "1.png").exists()


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


def test_groups_are_sorted_by_noun_and_counted(tmp_path):
    recs = [record(1, noun="attic", status=art_status.ACCEPTED),
            record(2, noun="attic", status=art_status.REJECTED),
            record(3, noun="attic", status=art_status.REJECTED),
            record(4, noun="tomb"),
            record(5, noun="vault"),
            record(6, noun="lobby")]
    by_id = {str(r["id"]): r for r in recs}

    groups = art_server.groups_for(by_id, "CAVE", "all")

    assert [g["noun"] for g in groups] == \
        ["attic", "lobby", "tomb", "vault"]
    attic = groups[0]
    assert (attic["accepted"], attic["rejected"], attic["undecided"]) == \
        (1, 2, 0)


def test_groups_no_longer_have_a_donor_key(tmp_path):
    recs = [record(1, noun="attic")]
    by_id = {str(r["id"]): r for r in recs}

    groups = art_server.groups_for(by_id, "CAVE", "all")

    assert "donor" not in groups[0], \
        "every scene names a place directly; there is nothing to donate"


def test_groups_for_counts_describe_the_whole_group_not_the_filtered_view(
        tmp_path):
    recs = [record(1, noun="attic", status=art_status.ACCEPTED),
            record(2, noun="attic", status=art_status.ACCEPTED),
            record(3, noun="attic", status=art_status.ACCEPTED),
            record(4, noun="attic", status=art_status.REJECTED),
            record(5, noun="attic", status=art_status.REJECTED),
            record(6, noun="attic", status=art_status.CANDIDATE)]
    by_id = {str(r["id"]): r for r in recs}

    groups = art_server.groups_for(by_id, "CAVE", "undecided")

    assert len(groups) == 1
    group = groups[0]
    assert (group["accepted"], group["rejected"], group["undecided"]) == \
        (3, 2, 1), "counts must describe the whole group, not the filtered view"
    assert [r["id"] for r in group["records"]] == [6], \
        "the filtered view still holds only what the status filter wants"


def test_groups_filter_by_status(tmp_path):
    recs = [record(1, status=art_status.ACCEPTED),
            record(2, status=art_status.CANDIDATE)]
    by_id = {str(r["id"]): r for r in recs}

    groups = art_server.groups_for(by_id, "CAVE", "undecided")

    ids = [r["id"] for g in groups for r in g["records"]]
    assert ids == [2]


def test_groups_never_include_metric_rejected(tmp_path):
    recs = [record(1, status=art_status.METRIC_REJECTED),
            record(2, status=art_status.CANDIDATE)]
    by_id = {str(r["id"]): r for r in recs}

    groups = art_server.groups_for(by_id, "CAVE", "all")

    ids = [r["id"] for g in groups for r in g["records"]]
    assert ids == [2], "no file has ever existed for a metric rejection"


def test_groups_for_matches_old_shape_records_by_mood(tmp_path):
    """A pre-migration record with no "scene" key must still group under
    its legacy mood, since scene_of() falls back to mood. WILDER is a
    genuine one of the twelve legacy moods and, unlike CAVE (never a
    legacy mood) or DESERT (a mood that collides with a real scene), it
    cannot pass this test by accident."""
    recs = [old_record(1, mood="WILDER", donor="WILDER", noun="tunnel")]
    by_id = {str(r["id"]): r for r in recs}

    groups = art_server.groups_for(by_id, "WILDER", "all")

    assert [g["noun"] for g in groups] == ["tunnel"]


def test_scene_route_reaches_a_migrated_record_that_still_carries_its_legacy_mood(
        tmp_path):
    """Migration only adds a scene key; mood is never deleted. The actual
    HTTP route -- not just groups_for, which the earlier gap hid behind --
    must reach a record once it carries scene, even with mood still on it."""
    rec = old_record(1, mood="WILDER", donor="WILDER", noun="tunnel")
    rec["scene"] = "CAVE"
    client = build(tmp_path, [rec], candidates=[rec])

    resp = client.get("/scene/CAVE")

    assert resp.status_code == 200
    assert 'data-id="1"' in resp.get_data(as_text=True)


def test_scene_route_404s_for_a_legacy_mood_name_not_in_the_vocabulary(
        tmp_path):
    """A record with no scene key groups under its mood in groups_for, but
    the route for that legacy mood name must still 404 -- legacy names are
    never routed, only additively re-tagged onto real scenes."""
    rec = old_record(1, mood="WILDER", donor="WILDER", noun="tunnel")
    client = build(tmp_path, [rec], candidates=[rec])

    assert client.get("/scene/WILDER").status_code == 404


def test_scene_page_defaults_to_undecided(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    und = record(2, status=art_status.CANDIDATE)
    client = build(tmp_path, [acc, und], promoted=[acc], candidates=[und])

    page = client.get("/scene/CAVE").get_data(as_text=True)

    assert 'data-id="2"' in page
    assert 'data-id="1"' not in page, \
        "a resumed pass shows what is left, not what is done"


def test_scene_page_all_shows_every_decided_record(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    und = record(2, status=art_status.CANDIDATE)
    client = build(tmp_path, [acc, und], promoted=[acc], candidates=[und])

    page = client.get("/scene/CAVE?status=all").get_data(as_text=True)

    assert 'data-id="1"' in page and 'data-id="2"' in page


def test_scene_page_shows_the_group_heading(tmp_path):
    und = record(1, noun="attic")
    client = build(tmp_path, [und], candidates=[und])

    page = client.get("/scene/CAVE").get_data(as_text=True)

    assert "attic" in page


def test_scene_page_group_heading_shows_the_right_counts_in_the_right_slots(
        tmp_path):
    recs = [record(1, noun="attic", status=art_status.ACCEPTED),
            record(2, noun="attic", status=art_status.ACCEPTED),
            record(3, noun="attic", status=art_status.ACCEPTED),
            record(4, noun="attic", status=art_status.ACCEPTED),
            record(5, noun="attic", status=art_status.REJECTED),
            record(6, noun="attic", status=art_status.CANDIDATE),
            record(7, noun="attic", status=art_status.CANDIDATE)]
    client = build(tmp_path, recs, candidates=recs)

    page = client.get("/scene/CAVE?status=all").get_data(as_text=True)

    assert "4 accepted" in page, "the heading must carry the accepted count"
    assert "1 rejected" in page, "the heading must carry the rejected count"
    assert "2 undecided" in page, "the heading must carry the undecided count"


def test_scene_page_placeholder_for_an_accepted_record_missing_from_disk(
        tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    client = build(tmp_path, [acc])

    page = client.get("/scene/CAVE?status=all").get_data(as_text=True)

    assert 'data-id="1"' in page, "the tile must still render"
    assert "no local copy" in page, \
        "status alone must not be trusted; the file is not on disk"
    assert 'data-id="1" tabindex="0"' in page, \
        "no picture to click, so the tile must stay focusable for the A key"


def test_scene_page_404s_for_an_unknown_scene(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    assert client.get("/scene/NOPE").status_code == 404


def test_scene_page_loses_no_record_to_grouping(tmp_path):
    recs = [record(1, noun="attic"),
            record(2, noun="cellar"),
            record(3, noun="tomb"),
            record(4, scene="SHORE", noun="lake")]
    client = build(tmp_path, recs, candidates=recs)

    page = client.get("/scene/CAVE?status=all").get_data(as_text=True)

    import re as _re
    shown = set(_re.findall(r'data-id="(\d+)"', page))
    assert shown == {"1", "2", "3"}, \
        "grouping reorders and labels; it must not drop or borrow a record"


def test_scene_page_renders_a_placeholder_for_a_missing_picture(tmp_path):
    und = record(1, status=art_status.CANDIDATE)
    client = build(tmp_path, [und])

    page = client.get("/scene/CAVE").get_data(as_text=True)

    assert 'data-id="1"' in page, "the verdict must stay clickable"
    assert "pixabay.com" in page


def test_verdict_unmark_returns_accepted_to_candidate_and_moves_the_file_back(
        tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    client = build(tmp_path, [acc], promoted=[acc])
    assets = tmp_path / "tools" / "assets"

    resp = client.post("/verdict", json={"id": "1", "verdict": "unmark"})

    assert resp.get_json()["status"] == art_status.CANDIDATE
    assert not (assets / "png" / "CAVE" / "hallway" / "1.png").exists()
    assert (assets / "candidates" / "CAVE" / "hallway" / "1.png").exists()
    saved = json.loads(
        (assets / "art_manifest.json").read_text(encoding="utf-8"))
    assert saved["1"]["status"] == art_status.CANDIDATE


def test_verdict_unmark_returns_refreshed_counts(tmp_path):
    a = record(1, status=art_status.ACCEPTED)
    b = record(2, status=art_status.ACCEPTED)
    c = record(3, status=art_status.CANDIDATE)
    client = build(tmp_path, [a, b, c], promoted=[a, b], candidates=[c])

    body = client.post("/verdict",
                       json={"id": "1", "verdict": "unmark"}).get_json()

    assert body["status"] == art_status.CANDIDATE
    assert body["accepted"] == 1
    assert body["undecided"] == 2


def test_verdict_400s_for_banana(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    resp = client.post("/verdict", json={"id": "1", "verdict": "banana"})

    assert resp.status_code == 400


def test_scene_page_has_no_accept_or_reject_buttons_but_has_a_zoom_control(
        tmp_path):
    a = record(1, status=art_status.CANDIDATE)
    b = record(2, status=art_status.CANDIDATE, noun="cellar")
    client = build(tmp_path, [a, b], candidates=[a, b])

    page = client.get("/scene/CAVE").get_data(as_text=True)

    assert ">accept</button>" not in page, \
        "clicking the picture is the accept control now"
    assert ">reject</button>" not in page, \
        "reject is no longer a separate button"
    assert ">zoom</button>" in page, \
        "enlarge moved to an explicit caption control"


def test_scene_page_only_figures_are_in_the_tab_order(tmp_path):
    a = record(1, status=art_status.ACCEPTED)
    b = record(2, status=art_status.CANDIDATE, noun="cellar")
    client = build(tmp_path, [a, b], promoted=[a], candidates=[b])

    import re as _re
    page = client.get("/scene/CAVE?status=all").get_data(as_text=True)

    figures = _re.findall(r"<figure.*?</figure>", page, _re.S)
    assert len(figures) == 2
    for figure_html in figures:
        close = figure_html.index(">") + 1
        inner = figure_html[close:-len("</figure>")]
        controls = _re.findall(r"<(?:a|button|img)\b[^>]*>", inner)
        assert controls, "expected at least one focusable control to check"
        for control in controls:
            assert 'tabindex="-1"' in control, \
                "every control inside a figure must be out of the tab order"


def test_groups_sort_when_pixabay_int_ids_and_unsplash_str_ids_mix(tmp_path):
    recs = [record(100, noun="pier", status=art_status.ACCEPTED),
            record("kAeovMEDpcE", noun="pier", status=art_status.ACCEPTED),
            record(9, noun="pier", status=art_status.ACCEPTED)]
    by_id = {str(r["id"]): r for r in recs}

    groups = art_server.groups_for(by_id, "CAVE", "accepted")

    assert [r["id"] for r in groups[0]["records"]] == \
        [9, 100, "kAeovMEDpcE"]
