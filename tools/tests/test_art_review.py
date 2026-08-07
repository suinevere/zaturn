"""Cover sheet generation, cross-mood dedup, and promotion into the source tree."""
import json
import re
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_review
import art_status


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
    html = art_review.sheet("HORROR", {"1": rec}, tmp_path)
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
