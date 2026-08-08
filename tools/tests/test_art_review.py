"""Cover sheet generation, cross-mood dedup, and promotion into the source tree."""
import json
import os
import re
import sys
import time
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_review
import art_status
import fetch_art
from art_nouns import MOODS


def record(pid, mood="HORROR", donor="HOUSE", noun="hallway", phash="0" * 16,
           status=art_status.CANDIDATE):
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
    html = art_review.sheet("HORROR", {"1": rec}, tmp_path, tmp_path / "png")
    assert "<html" in html and "</html>" in html
    assert "data:image/png;base64," in html, "thumbnails must be embedded"

    srcs = re.findall(r'src="([^"]*)"', html)
    assert srcs, "expected at least one img src to check"
    assert all(src.startswith("data:") for src in srcs), \
        "every img src must be an embedded data: URI, not a remote reference"

    hrefs = re.findall(r'href="([^"]*)"', html)
    assert all(not href.startswith("http") or "pixabay.com/photos/" in href
               for href in hrefs), \
        "only the deliberate full-size Pixabay page link may point off-page"

    assert "dark hallway" in html
    assert "pixabay.com/photos/1/" in html


def test_sheet_with_no_candidates_still_renders(tmp_path):
    html = art_review.sheet("HORROR", {}, tmp_path, tmp_path / "png")
    assert "<html" in html and "</html>" in html
    assert "0 candidates" in html


def test_sheet_covers_only_its_own_mood(tmp_path):
    recs = {"1": record(1, mood="HORROR"), "2": record(2, mood="HOUSE")}
    for r in recs.values():
        make_candidate(tmp_path, r)
    html = art_review.sheet("HORROR", recs, tmp_path, tmp_path / "png")
    assert "photos/1/" in html
    assert "photos/2/" not in html


def test_sheet_escapes_a_quote_in_the_phrase_and_page_url(tmp_path):
    """A quote in either field must not break out of its HTML attribute --
    a Pixabay URL with a query string is exactly where one could appear."""
    rec = record(1)
    rec["phrase"] = 'dark "haunted" hallway'
    rec["page_url"] = 'https://pixabay.com/photos/1/?ref="x"'
    make_candidate(tmp_path, rec)
    html = art_review.sheet("HORROR", {"1": rec}, tmp_path, tmp_path / "png")
    assert '"x"' not in html
    assert "&quot;" in html


def test_promote_moves_accepted_and_leaves_rejected(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    recs = {"1": record(1), "2": record(2)}
    for r in recs.values():
        make_candidate(cand, r)

    counts = art_review.promote({"1": "accept", "2": "reject"}, recs, cand, png)

    assert counts == {"HORROR": art_review.Counts(gained=1, lost=0)}
    assert (png / "HORROR" / "HOUSE" / "hallway" / "1.png").exists()
    assert not (png / "HORROR" / "HOUSE" / "hallway" / "2.png").exists()
    assert recs["1"]["status"] == art_status.ACCEPTED
    assert recs["2"]["status"] == art_status.REJECTED


def test_promotion_is_idempotent_even_if_the_source_file_reappears(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    recs = {"1": record(1)}
    make_candidate(cand, recs["1"])
    art_review.promote({"1": "accept"}, recs, cand, png)

    make_candidate(cand, recs["1"])
    counts = art_review.promote({"1": "accept"}, recs, cand, png)
    assert counts == {}, "an already-accepted record must not be promoted twice"
    assert (cand / "HORROR" / "HOUSE" / "hallway" / "1.png").exists(), \
        "a stray re-fetch reappearing must not fool the guard into re-promoting"


def test_dedup_drops_a_near_duplicate_across_moods():
    a = record(1, mood="HORROR", phash="ff00ff00ff00ff00")
    b = record(2, mood="HOUSE", phash="ff00ff00ff00ff01")
    c = record(3, mood="TOWN", phash="00ff00ff00ff00ff")
    kept = art_review.dedup([a, b, c])
    assert [r["id"] for r in kept] == [1, 3]


def test_dedup_keeps_everything_when_hashes_are_missing():
    recs = [record(1, phash=""), record(2, phash="")]
    assert len(art_review.dedup(recs)) == 2


def test_dedup_drops_a_candidate_matching_an_already_accepted_hash():
    """Cross-run blind spot: an id accepted and promoted in an earlier pass
    is gone from the candidates list by the next run, so dedup must be told
    about it separately or its duplicate sails through untouched."""
    new = record(2, mood="HOUSE", phash="ff00ff00ff00ff01")
    kept = art_review.dedup([new], already_accepted=["ff00ff00ff00ff00"])
    assert kept == []


def test_dedup_keeps_a_candidate_far_from_every_accepted_hash():
    new = record(2, mood="HOUSE", phash="00000000000000ff")
    kept = art_review.dedup([new], already_accepted=["ff00ff00ff00ff00"])
    assert [r["id"] for r in kept] == [2]


def test_main_sheets_writes_one_page_per_mood_and_seeds_dedup_from_accepted(tmp_path):
    assets = tmp_path / "tools" / "assets"
    cand = assets / "candidates"
    new = record(1, mood="HORROR", phash="ff00ff00ff00ff01")
    already = record(2, mood="HORROR", phash="ff00ff00ff00ff00",
                     status=art_status.ACCEPTED)
    make_candidate(cand, new)
    fetch_art.save_manifest(assets / "art_manifest.json",
                            {"1": new, "2": already})

    assert art_review.main(["--sheets"], repo=tmp_path) == 0

    pages = [p for p in (assets / "sheets").glob("*.html") if p.stem != "index"]
    assert len(pages) == len(MOODS)
    horror_html = (assets / "sheets" / "HORROR.html").read_text(encoding="utf-8")
    assert 'data-id="1"' not in horror_html, \
        "candidate 1 is a near-duplicate of the already-accepted candidate 2"
    assert 'data-id="2"' in horror_html, \
        "a decided record must never be dropped by dedup"


def test_main_promote_moves_accepted_and_updates_the_manifest_on_disk(tmp_path):
    assets = tmp_path / "tools" / "assets"
    cand = assets / "candidates"
    rec = record(1)
    make_candidate(cand, rec)
    manifest_path = assets / "art_manifest.json"
    fetch_art.save_manifest(manifest_path, {"1": rec})

    verdicts_path = tmp_path / "verdicts.json"
    verdicts_path.write_text(json.dumps({"1": "accept"}), encoding="utf-8")

    assert art_review.main(["--promote", str(verdicts_path)], repo=tmp_path) == 0

    assert (assets / "png" / "HORROR" / "HOUSE" / "hallway" / "1.png").exists()
    saved = fetch_art.load_manifest(manifest_path)
    assert saved["1"]["status"] == art_status.ACCEPTED


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
    up = record(1, mood="HORROR", status=art_status.REJECTED)
    down = record(2, mood="HOUSE", status=art_status.ACCEPTED)
    make_candidate(cand, up)
    make_promoted(png, down)
    manifest = {"1": up, "2": down}

    counts = art_review.promote({"1": "accept", "2": "reject"}, manifest,
                                cand, png)

    assert counts["HORROR"] == art_review.Counts(gained=1, lost=0), \
        "a lone gain in one mood must not be conflated with the other mood's loss"
    assert counts["HOUSE"] == art_review.Counts(gained=0, lost=1)


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


def test_main_promote_resolves_a_same_id_conflict_in_favor_of_the_newer_file(tmp_path):
    assets = tmp_path / "tools" / "assets"
    sheets = assets / "sheets"
    sheets.mkdir(parents=True)
    rec = record(1, status=art_status.CANDIDATE)
    make_candidate(assets / "candidates", rec)
    fetch_art.save_manifest(assets / "art_manifest.json", {"1": rec})

    older = sheets / "verdicts.json"
    newer = sheets / "verdicts(1).json"
    older.write_text('{"1": "reject"}', encoding="utf-8")
    newer.write_text('{"1": "accept"}', encoding="utf-8")
    now = time.time()
    os.utime(older, (now - 100, now - 100))
    os.utime(newer, (now, now))

    assert art_review.main(["--promote"], repo=tmp_path) == 0

    saved = json.loads((assets / "art_manifest.json").read_text(encoding="utf-8"))
    assert saved["1"]["status"] == art_status.ACCEPTED, \
        "the more recently modified verdicts file must win a same-id conflict"


def test_main_promote_with_no_files_says_so(tmp_path, capsys):
    assets = tmp_path / "tools" / "assets"
    (assets / "sheets").mkdir(parents=True)
    fetch_art.save_manifest(assets / "art_manifest.json", {})

    assert art_review.main(["--promote"], repo=tmp_path) == 0
    assert "no verdicts" in capsys.readouterr().out.lower()


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


def test_sheet_falls_back_to_candidates_for_an_accepted_record_restored_there(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.ACCEPTED)
    make_candidate(cand, rec)

    html_out = art_review.sheet("HORROR", {"1": rec}, cand, png)

    tile = html_out.split('data-id="1"')[1].split("</figure>")[0]
    assert "data:image/png;base64," in tile, \
        "refetch_missing always restores into candidates_dir, even for an " \
        "accepted record, so the sheet must fall back there instead of " \
        "showing a placeholder"
    assert "no local copy" not in tile


def test_sheet_stays_self_contained_with_a_placeholder(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.REJECTED)

    html_out = art_review.sheet("HORROR", {"1": rec}, cand, png)

    srcs = re.findall(r'src="([^"]*)"', html_out)
    assert all(s.startswith("data:") for s in srcs), \
        "a placeholder must not reintroduce a remote image reference"


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

    row = [r for r in page.split("<tr") if "HORROR.html" in r][0]
    assert "empty" in row, \
        "a mood with no accepted pictures is the thing the index exists to show"


def test_main_sheets_also_writes_the_index(tmp_path):
    assets = tmp_path / "tools" / "assets"
    assets.mkdir(parents=True)
    fetch_art.save_manifest(assets / "art_manifest.json", {})

    assert art_review.main(["--sheets"], repo=tmp_path) == 0
    assert (assets / "sheets" / "index.html").exists()


def test_sheet_links_to_its_neighbouring_moods(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"

    html_out = art_review.sheet(MOODS[0], {}, cand, png)

    assert 'href="index.html"' in html_out
    assert 'href="{}.html"'.format(MOODS[1]) in html_out
    assert 'href="{}.html"'.format(MOODS[-1]) in html_out, \
        "the first mood wraps to the last so no page is a dead end"


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
    rec["image_url"] = "https://example.invalid/1.png"
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
    tile = page.split('data-id="1"')[1].split("</figure>")[0]
    assert "no local copy" in tile


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
