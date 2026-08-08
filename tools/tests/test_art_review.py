"""Cover cross-mood dedup, promotion into the source tree, and refetching."""
import json
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_review
import art_status
import fetch_art


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


def test_main_promote_with_no_path_prints_usage_and_touches_nothing(tmp_path):
    assets = tmp_path / "tools" / "assets"
    assets.mkdir(parents=True)
    rec = record(1, status=art_status.CANDIDATE)
    fetch_art.save_manifest(assets / "art_manifest.json", {"1": rec})

    assert art_review.main(["--promote"], repo=tmp_path) == 0

    saved = json.loads(
        (assets / "art_manifest.json").read_text(encoding="utf-8"))
    assert saved["1"]["status"] == art_status.CANDIDATE, \
        "with no path, --promote must apply nothing -- the folder-glob form " \
        "went with the browser round trip that produced verdicts*.json files"


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
