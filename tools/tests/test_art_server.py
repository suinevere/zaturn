"""Cover the local review server's routes, verdicts, filtering and grouping."""
import json
import re
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_server
import art_status
import fetch_art


def record(pid, game="ZORK1", scene="CAVE", noun="hallway",
           status=art_status.CANDIDATE):
    return {"id": pid, "page_url": f"https://pixabay.com/photos/{pid}/",
            "image_url": "", "phrase": "dark hallway", "game": game,
            "scene": scene, "noun": noun,
            "licence": "Pixabay Content License",
            "fetched": "2026-08-08", "luminance": 70.0, "busyness": 4.0,
            "banding": 2.0, "verdict": "pass", "phash": "0" * 16,
            "status": status}


def key_of(rec):
    """The manifest key a record is stored under: game first, then its id."""
    return "{}:{}".format(rec["game"], rec["id"])


def write_png(root, rec):
    d = root / rec["game"] / rec["scene"]
    d.mkdir(parents=True, exist_ok=True)
    p = d / f"{rec['id']}.png"
    Image.new("RGB", (320, 224), (60, 60, 60)).save(p, "PNG")
    return p


def build(tmp_path, records, promoted=(), candidates=()):
    """An app whose games and scenes come from blessed tags, as in production.

    Description: The art server's game list is the scene server's output --
        tools/assets/scenes/<STEM>.json -- so a fixture that writes only a
        manifest would render an empty site however many records it holds.
        One blessed room per (game, scene) the records mention is enough.
    """
    assets = tmp_path / "tools" / "assets"
    (assets / "scenes").mkdir(parents=True)
    blessed = {}
    for i, rec in enumerate(records, start=1):
        blessed.setdefault(rec["game"], {})[str(i)] = rec["scene"]
    for game, rooms in blessed.items():
        (assets / "scenes" / f"{game}.json").write_text(
            json.dumps(rooms), encoding="utf-8")
    for rec in promoted:
        write_png(assets / "png", rec)
    for rec in candidates:
        write_png(assets / "candidates", rec)
    fetch_art.save_manifest(assets / "art_manifest.json",
                            {key_of(r): r for r in records})
    return art_server.create_app(repo=tmp_path).test_client()


def test_index_shows_one_row_per_game_with_its_counts_in_the_right_columns(
        tmp_path):
    """Games are the top level now, because art ships per game: a picture
    lives at png/<GAME>/<SCENE>/ and converts into that game's own 1..99
    range."""
    recs = [record(1, status=art_status.ACCEPTED),
            record(2, status=art_status.ACCEPTED),
            record(3, status=art_status.ACCEPTED),
            record(4, status=art_status.REJECTED),
            record(5, status=art_status.REJECTED),
            record(6, status=art_status.CANDIDATE)]
    client = build(tmp_path, recs, candidates=recs)

    page = client.get("/").get_data(as_text=True)

    row = re.search(r"<tr>(?:(?!<tr>).)*?ZORK1(?:(?!<tr>).)*?</tr>",
                    page, re.S).group(0)
    cells = re.findall(r"<td[^>]*>(.*?)</td>", row, re.S)
    assert cells[3] == "3", "accepted column must show the accepted count"
    assert cells[4] == "2", "rejected column must show the rejected count"
    assert cells[5] == "1", "undecided column must show the undecided count"


def test_index_lists_every_game_that_has_blessed_tags(tmp_path):
    recs = [record(1, game="ZORK1", status=art_status.ACCEPTED),
            record(2, game="PLNTFALL", scene="CORRIDOR")]
    client = build(tmp_path, recs, promoted=[recs[0]], candidates=[recs[1]])

    page = client.get("/").get_data(as_text=True)

    assert "ZORK1" in page and "PLNTFALL" in page


def test_index_names_each_game_genre(tmp_path):
    """The genre is what the search phrases are drawn from, so the reviewer
    must be able to see it without reading the source."""
    recs = [record(1, game="ZORK1"), record(2, game="PLNTFALL")]
    client = build(tmp_path, recs, candidates=recs)

    page = client.get("/").get_data(as_text=True)

    assert "FANTASY" in page and "SCIFI" in page


def test_index_shows_the_per_game_disc_target(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    page = client.get("/").get_data(as_text=True)

    assert str(art_server.PER_GAME_TARGET) in page


def test_game_page_lists_only_the_scenes_that_game_is_tagged_with(tmp_path):
    """Fetching art for a scene no room was tagged with would put pictures on
    the disc that nothing can ever show, so the shopping list is the blessed
    tags and never the whole 32-name vocabulary."""
    recs = [record(1, scene="CAVE"), record(2, scene="SHORE")]
    client = build(tmp_path, recs, candidates=recs)

    page = client.get("/game/ZORK1").get_data(as_text=True)

    assert "CAVE" in page and "SHORE" in page
    assert "BATHROOM" not in page, "an untagged scene must not be offered"


def test_game_page_shows_the_search_phrases_the_genre_produces(tmp_path):
    recs = [record(1, game="PLNTFALL", scene="CORRIDOR")]
    client = build(tmp_path, recs, candidates=recs)

    page = client.get("/game/PLNTFALL").get_data(as_text=True)

    assert "spaceship corridor" in page


def test_game_page_404s_for_a_game_with_no_blessed_tags(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    assert client.get("/game/NOTAGAME").status_code == 404


def test_one_game_pool_never_leaks_into_another(tmp_path):
    """Two stories curate independently -- art is duplicated per game -- so a
    picture accepted for ZORK1 must not appear in PLNTFALL's counts or pages."""
    a = record(1, game="ZORK1", scene="CAVE", status=art_status.ACCEPTED)
    b = record(2, game="PLNTFALL", scene="CAVE", status=art_status.CANDIDATE)
    client = build(tmp_path, [a, b], promoted=[a], candidates=[b])

    page = client.get("/game/PLNTFALL/CAVE?status=all").get_data(as_text=True)

    assert "PLNTFALL:2" in page
    assert "ZORK1:1" not in page


def test_image_route_serves_an_accepted_picture_from_png(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    client = build(tmp_path, [acc], promoted=[acc])

    resp = client.get("/image/ZORK1:1")

    assert resp.status_code == 200
    assert resp.data[:8] == b"\x89PNG\r\n\x1a\n"


def test_image_route_serves_a_rejected_picture_from_candidates(tmp_path):
    rej = record(2, status=art_status.REJECTED)
    client = build(tmp_path, [rej], candidates=[rej])

    assert client.get("/image/ZORK1:2").status_code == 200


def test_image_route_404s_for_an_unknown_id(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    assert client.get("/image/ZORK1:9999").status_code == 404


def test_image_route_404s_when_the_file_is_missing(tmp_path):
    client = build(tmp_path, [record(1)])

    assert client.get("/image/ZORK1:1").status_code == 404, \
        "a fresh clone has the record but no pixels"


def test_verdict_accepts_and_moves_the_file_into_png(tmp_path):
    und = record(1, status=art_status.CANDIDATE)
    client = build(tmp_path, [und], candidates=[und])
    assets = tmp_path / "tools" / "assets"

    resp = client.post("/verdict", json={"id": "ZORK1:1", "verdict": "accept"})

    assert resp.get_json()["status"] == art_status.ACCEPTED
    assert (assets / "png" / "ZORK1" / "CAVE" / "1.png").exists()
    saved = json.loads(
        (assets / "art_manifest.json").read_text(encoding="utf-8"))
    assert saved["ZORK1:1"]["status"] == art_status.ACCEPTED


def test_verdict_un_accepts_and_moves_the_file_back(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    client = build(tmp_path, [acc], promoted=[acc])
    assets = tmp_path / "tools" / "assets"

    resp = client.post("/verdict", json={"id": "ZORK1:1", "verdict": "reject"})

    assert resp.get_json()["status"] == art_status.REJECTED
    assert not (assets / "png" / "ZORK1" / "CAVE" / "1.png").exists()
    assert (assets / "candidates" / "ZORK1" / "CAVE" / "1.png").exists()


def test_verdict_is_idempotent(tmp_path):
    und = record(1, status=art_status.CANDIDATE)
    client = build(tmp_path, [und], candidates=[und])

    first = client.post("/verdict", json={"id": "ZORK1:1", "verdict": "accept"})
    second = client.post("/verdict", json={"id": "ZORK1:1", "verdict": "accept"})

    assert first.get_json()["status"] == art_status.ACCEPTED
    assert second.get_json()["status"] == art_status.ACCEPTED
    assert second.get_json()["accepted"] == 1, \
        "applying the same verdict twice must not double-count"


def test_verdict_returns_refreshed_counts(tmp_path):
    a = record(1, status=art_status.CANDIDATE)
    b = record(2, status=art_status.CANDIDATE)
    client = build(tmp_path, [a, b], candidates=[a, b])

    body = client.post("/verdict",
                       json={"id": "ZORK1:1", "verdict": "accept"}).get_json()

    assert body["accepted"] == 1 and body["undecided"] == 1


def test_verdict_records_the_decision_with_no_file_present(tmp_path):
    rej = record(1, status=art_status.REJECTED)
    client = build(tmp_path, [rej])

    body = client.post("/verdict",
                       json={"id": "ZORK1:1", "verdict": "accept"}).get_json()

    assert body["status"] == art_status.ACCEPTED, \
        "the manifest is the decision; the file location merely follows it"


def test_verdict_404s_for_an_unknown_id(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    assert client.post("/verdict",
                       json={"id": "ZORK1:9999", "verdict": "accept"}).status_code == 404


def test_verdict_400s_for_a_word_that_is_not_a_verdict(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    assert client.post("/verdict",
                       json={"id": "ZORK1:1", "verdict": "maybe"}).status_code == 400


def test_verdict_never_touches_a_metric_rejected_record(tmp_path):
    mr = record(1, status=art_status.METRIC_REJECTED)
    client = build(tmp_path, [mr])

    client.post("/verdict", json={"id": "ZORK1:1", "verdict": "accept"})

    assets = tmp_path / "tools" / "assets"
    saved = json.loads(
        (assets / "art_manifest.json").read_text(encoding="utf-8"))
    assert saved["ZORK1:1"]["status"] == art_status.METRIC_REJECTED


def test_groups_are_sorted_by_noun_and_counted(tmp_path):
    recs = [record(1, noun="attic", status=art_status.ACCEPTED),
            record(2, noun="attic", status=art_status.REJECTED),
            record(3, noun="attic", status=art_status.REJECTED),
            record(4, noun="tomb"),
            record(5, noun="vault"),
            record(6, noun="lobby")]
    by_id = {key_of(r): r for r in recs}

    groups = art_server.groups_for(by_id, "ZORK1", "CAVE", "all")

    assert [g["noun"] for g in groups] == \
        ["attic", "lobby", "tomb", "vault"]
    attic = groups[0]
    assert (attic["accepted"], attic["rejected"], attic["undecided"]) == \
        (1, 2, 0)


def test_groups_no_longer_have_a_donor_key(tmp_path):
    recs = [record(1, noun="attic")]
    by_id = {key_of(r): r for r in recs}

    groups = art_server.groups_for(by_id, "ZORK1", "CAVE", "all")

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
    by_id = {key_of(r): r for r in recs}

    groups = art_server.groups_for(by_id, "ZORK1", "CAVE", "undecided")

    assert len(groups) == 1
    group = groups[0]
    assert (group["accepted"], group["rejected"], group["undecided"]) == \
        (3, 2, 1), "counts must describe the whole group, not the filtered view"
    assert [r["id"] for r in group["records"]] == [6], \
        "the filtered view still holds only what the status filter wants"


def test_groups_filter_by_status(tmp_path):
    recs = [record(1, status=art_status.ACCEPTED),
            record(2, status=art_status.CANDIDATE)]
    by_id = {key_of(r): r for r in recs}

    groups = art_server.groups_for(by_id, "ZORK1", "CAVE", "undecided")

    ids = [r["id"] for g in groups for r in g["records"]]
    assert ids == [2]


def test_groups_never_include_metric_rejected(tmp_path):
    recs = [record(1, status=art_status.METRIC_REJECTED),
            record(2, status=art_status.CANDIDATE)]
    by_id = {key_of(r): r for r in recs}

    groups = art_server.groups_for(by_id, "ZORK1", "CAVE", "all")

    ids = [r["id"] for g in groups for r in g["records"]]
    assert ids == [2], "no file has ever existed for a metric rejection"


def test_scene_page_defaults_to_undecided(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    und = record(2, status=art_status.CANDIDATE)
    client = build(tmp_path, [acc, und], promoted=[acc], candidates=[und])

    page = client.get("/game/ZORK1/CAVE").get_data(as_text=True)

    assert 'data-id="ZORK1:2"' in page
    assert 'data-id="ZORK1:1"' not in page, \
        "a resumed pass shows what is left, not what is done"


def test_scene_page_all_shows_every_decided_record(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    und = record(2, status=art_status.CANDIDATE)
    client = build(tmp_path, [acc, und], promoted=[acc], candidates=[und])

    page = client.get("/game/ZORK1/CAVE?status=all").get_data(as_text=True)

    assert 'data-id="ZORK1:1"' in page and 'data-id="ZORK1:2"' in page


def test_scene_page_shows_the_group_heading(tmp_path):
    und = record(1, noun="attic")
    client = build(tmp_path, [und], candidates=[und])

    page = client.get("/game/ZORK1/CAVE").get_data(as_text=True)

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

    page = client.get("/game/ZORK1/CAVE?status=all").get_data(as_text=True)

    assert "4 accepted" in page, "the heading must carry the accepted count"
    assert "1 rejected" in page, "the heading must carry the rejected count"
    assert "2 undecided" in page, "the heading must carry the undecided count"


def test_scene_page_placeholder_for_an_accepted_record_missing_from_disk(
        tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    client = build(tmp_path, [acc])

    page = client.get("/game/ZORK1/CAVE?status=all").get_data(as_text=True)

    assert 'data-id="ZORK1:1"' in page, "the tile must still render"
    assert "no local copy" in page, \
        "status alone must not be trusted; the file is not on disk"
    assert 'data-id="ZORK1:1" tabindex="0"' in page, \
        "no picture to click, so the tile must stay focusable for the A key"


def test_scene_page_404s_for_an_unknown_scene(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    assert client.get("/game/ZORK1/NOPE").status_code == 404


def test_scene_page_loses_no_record_to_grouping(tmp_path):
    recs = [record(1, noun="attic"),
            record(2, noun="cellar"),
            record(3, noun="tomb"),
            record(4, scene="SHORE", noun="lake")]
    client = build(tmp_path, recs, candidates=recs)

    page = client.get("/game/ZORK1/CAVE?status=all").get_data(as_text=True)

    import re as _re
    shown = set(_re.findall(r'data-id="([A-Za-z0-9_:]+)"', page))
    assert shown == {"ZORK1:1", "ZORK1:2", "ZORK1:3"}, \
        "grouping reorders and labels; it must not drop or borrow a record"


def test_scene_page_renders_a_placeholder_for_a_missing_picture(tmp_path):
    und = record(1, status=art_status.CANDIDATE)
    client = build(tmp_path, [und])

    page = client.get("/game/ZORK1/CAVE").get_data(as_text=True)

    assert 'data-id="ZORK1:1"' in page, "the verdict must stay clickable"
    assert "pixabay.com" in page


def test_verdict_unmark_returns_accepted_to_candidate_and_moves_the_file_back(
        tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    client = build(tmp_path, [acc], promoted=[acc])
    assets = tmp_path / "tools" / "assets"

    resp = client.post("/verdict", json={"id": "ZORK1:1", "verdict": "unmark"})

    assert resp.get_json()["status"] == art_status.CANDIDATE
    assert not (assets / "png" / "ZORK1" / "CAVE" / "1.png").exists()
    assert (assets / "candidates" / "ZORK1" / "CAVE" / "1.png").exists()
    saved = json.loads(
        (assets / "art_manifest.json").read_text(encoding="utf-8"))
    assert saved["ZORK1:1"]["status"] == art_status.CANDIDATE


def test_verdict_unmark_returns_refreshed_counts(tmp_path):
    a = record(1, status=art_status.ACCEPTED)
    b = record(2, status=art_status.ACCEPTED)
    c = record(3, status=art_status.CANDIDATE)
    client = build(tmp_path, [a, b, c], promoted=[a, b], candidates=[c])

    body = client.post("/verdict",
                       json={"id": "ZORK1:1", "verdict": "unmark"}).get_json()

    assert body["status"] == art_status.CANDIDATE
    assert body["accepted"] == 1
    assert body["undecided"] == 2


def test_verdict_400s_for_banana(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)])

    resp = client.post("/verdict", json={"id": "ZORK1:1", "verdict": "banana"})

    assert resp.status_code == 400


def test_scene_page_has_no_accept_or_reject_buttons_but_has_a_zoom_control(
        tmp_path):
    a = record(1, status=art_status.CANDIDATE)
    b = record(2, status=art_status.CANDIDATE, noun="cellar")
    client = build(tmp_path, [a, b], candidates=[a, b])

    page = client.get("/game/ZORK1/CAVE").get_data(as_text=True)

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
    page = client.get("/game/ZORK1/CAVE?status=all").get_data(as_text=True)

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
    by_id = {key_of(r): r for r in recs}

    groups = art_server.groups_for(by_id, "ZORK1", "CAVE", "accepted")

    assert [r["id"] for r in groups[0]["records"]] == \
        [9, 100, "kAeovMEDpcE"]
