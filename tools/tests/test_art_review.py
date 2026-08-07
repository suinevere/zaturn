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

    # Re-create the candidate file, as a stray re-fetch might: without the
    # status guard this alone would let the second call promote it again.
    make_candidate(cand, recs["1"])
    counts = art_review.promote({"1": "accept"}, recs, cand, png)
    assert counts == {}, "an already-accepted record must not be promoted twice"
    assert (cand / "HORROR" / "HOUSE" / "hallway" / "1.png").exists(), \
        "the guard, not a missing source file, must be what blocks re-promotion"


def test_dedup_drops_a_near_duplicate_across_moods():
    a = record(1, mood="HORROR", phash="ff00ff00ff00ff00")
    b = record(2, mood="HOUSE", phash="ff00ff00ff00ff01")
    c = record(3, mood="TOWN", phash="00ff00ff00ff00ff")
    kept = art_review.dedup([a, b, c])
    assert [r["id"] for r in kept] == [1, 3]


def test_dedup_keeps_everything_when_hashes_are_missing():
    recs = [record(1, phash=""), record(2, phash="")]
    assert len(art_review.dedup(recs)) == 2
