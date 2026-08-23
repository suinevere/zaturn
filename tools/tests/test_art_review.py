"""Cover cross-scene dedup, promotion into the source tree, and refetching."""
import json
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_review
import art_status
import fetch_art


def record(pid, game="ZORK1", scene="CAVE", noun="hallway", phash="0" * 16,
           status=art_status.CANDIDATE):
    """One manifest record in the shipping shape: a game, a scene, no donor."""
    return {"id": pid, "page_url": f"https://pixabay.com/photos/{pid}/",
            "image_url": "", "phrase": "dark hallway", "game": game,
            "scene": scene, "noun": noun,
            "licence": "Pixabay Content License",
            "fetched": "2026-08-06", "luminance": 70.0, "busyness": 4.0,
            "banding": 2.0, "verdict": "pass", "phash": phash, "status": status}


scene_record = record


def make_candidate(root, rec):
    """Write a picture where _rel expects it: <game>/<scene>/<id>.png."""
    d = root / rec["game"] / rec["scene"]
    d.mkdir(parents=True, exist_ok=True)
    p = d / f"{rec['id']}.png"
    Image.new("RGB", (320, 224), (60, 60, 60)).save(p, "PNG")
    return p


make_scene_candidate = make_candidate


def test_promote_moves_accepted_and_leaves_rejected(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    recs = {"1": record(1), "2": record(2)}
    for r in recs.values():
        make_candidate(cand, r)

    counts = art_review.promote({"1": "accept", "2": "reject"}, recs, cand, png)

    assert counts == {"CAVE": art_review.Counts(gained=1, lost=0)}
    assert (png / "ZORK1" / "CAVE" / "1.png").exists()
    assert not (png / "ZORK1" / "CAVE" / "2.png").exists()
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
    assert (cand / "ZORK1" / "CAVE" / "1.png").exists(), \
        "a stray re-fetch reappearing must not fool the guard into re-promoting"


def test_dedup_drops_a_near_duplicate_across_scenes():
    a = record(1, scene="CAVE", phash="ff00ff00ff00ff00")
    b = record(2, scene="PARLOR", phash="ff00ff00ff00ff01")
    c = record(3, scene="VILLAGE", phash="00ff00ff00ff00ff")
    kept = art_review.dedup([a, b, c])
    assert [r["id"] for r in kept] == [1, 3]


def test_dedup_keeps_everything_when_hashes_are_missing():
    recs = [record(1, phash=""), record(2, phash="")]
    assert len(art_review.dedup(recs)) == 2


def test_dedup_drops_a_candidate_matching_an_already_accepted_hash():
    """Cross-run blind spot: an id accepted and promoted in an earlier pass
    is gone from the candidates list by the next run, so dedup must be told
    about it separately or its duplicate sails through untouched."""
    new = record(2, scene="PARLOR", phash="ff00ff00ff00ff01")
    kept = art_review.dedup([new], already_accepted=["ff00ff00ff00ff00"])
    assert kept == []


def test_dedup_keeps_a_candidate_far_from_every_accepted_hash():
    new = record(2, scene="PARLOR", phash="00000000000000ff")
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

    assert (assets / "png" / "ZORK1" / "CAVE" / "1.png").exists()
    saved = fetch_art.load_manifest(manifest_path)
    assert saved["1"]["status"] == art_status.ACCEPTED


def make_promoted(root, rec):
    d = root / rec["game"] / rec["scene"]
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
    assert (cand / "ZORK1" / "CAVE" / "1.png").exists(), \
        "the file must return to candidates so it can be re-accepted"


def test_promote_re_accepts_a_rejected_image(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(1, status=art_status.REJECTED)
    make_candidate(cand, rec)
    manifest = {"1": rec}

    counts = art_review.promote({"1": "accept"}, manifest, cand, png)

    assert rec["status"] == art_status.ACCEPTED
    assert (png / "ZORK1" / "CAVE" / "1.png").exists()
    assert counts["CAVE"].gained == 1


def test_promote_counts_gains_and_losses_separately(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    up = record(1, scene="CAVE", status=art_status.REJECTED)
    down = record(2, scene="PARLOR", status=art_status.ACCEPTED)
    make_candidate(cand, up)
    make_promoted(png, down)
    manifest = {"1": up, "2": down}

    counts = art_review.promote({"1": "accept", "2": "reject"}, manifest,
                                cand, png)

    assert counts["CAVE"] == art_review.Counts(gained=1, lost=0), \
        "a lone gain in one mood must not be conflated with the other mood's loss"
    assert counts["PARLOR"] == art_review.Counts(gained=0, lost=1)


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
    assert (cand / "ZORK1" / "CAVE" / "1.png").exists()


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


def test_promote_unmark_returns_accepted_to_candidate_and_moves_file_back(
        tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(7, status=art_status.ACCEPTED)
    kept = make_promoted(png, rec)
    manifest = {"7": rec}

    counts = art_review.promote({"7": "unmark"}, manifest, cand, png)

    assert rec["status"] == art_status.CANDIDATE
    assert not kept.exists(), "the tracked png must be removed"
    assert (cand / "ZORK1" / "CAVE" / "7.png").exists(), \
        "the file must return to candidates so it stays in play"
    assert counts == {"CAVE": art_review.Counts(gained=0, lost=1)}, \
        "unmarking an accepted record is a pure loss"


def test_promote_unmark_on_a_candidate_is_a_no_op(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(11, status=art_status.CANDIDATE)
    kept = make_candidate(cand, rec)
    manifest = {"11": rec}

    counts = art_review.promote({"11": "unmark"}, manifest, cand, png)

    assert rec["status"] == art_status.CANDIDATE
    assert kept.exists()
    assert counts == {}, \
        "unmarking something already undecided changes nothing and reports nothing"


def test_promote_unmark_never_touches_a_metric_rejected_record(tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = record(23, status=art_status.METRIC_REJECTED)
    manifest = {"23": rec}

    art_review.promote({"23": "unmark"}, manifest, cand, png)

    assert rec["status"] == art_status.METRIC_REJECTED, \
        "a metric rejection is not a human decision and unmark must not touch it"


def test_main_reject_unmarked_sweeps_only_candidate_records(tmp_path, capsys):
    assets = tmp_path / "tools" / "assets"
    cand, png = assets / "candidates", assets / "png"
    c1 = record(1, status=art_status.CANDIDATE)
    c2 = record(2, status=art_status.CANDIDATE, noun="cellar")
    c3 = record(3, status=art_status.CANDIDATE, noun="attic")
    acc = record(4, status=art_status.ACCEPTED, noun="stairs")
    rej = record(5, status=art_status.REJECTED, noun="landing")
    other = record(6, scene="VILLAGE", noun="square",
                   status=art_status.CANDIDATE)
    for rec in (c1, c2, c3, other):
        make_candidate(cand, rec)
    make_promoted(png, acc)
    make_candidate(cand, rej)
    manifest = {"1": c1, "2": c2, "3": c3, "4": acc, "5": rej, "6": other}
    fetch_art.save_manifest(assets / "art_manifest.json", manifest)

    assert art_review.main(["--reject-unmarked"], repo=tmp_path) == 0

    saved = fetch_art.load_manifest(assets / "art_manifest.json")
    assert saved["1"]["status"] == art_status.REJECTED
    assert saved["2"]["status"] == art_status.REJECTED
    assert saved["3"]["status"] == art_status.REJECTED
    assert saved["6"]["status"] == art_status.REJECTED
    assert saved["4"]["status"] == art_status.ACCEPTED, \
        "an accepted record must never be swept"
    assert saved["5"]["status"] == art_status.REJECTED, \
        "an already-rejected record must stay rejected, not be touched twice"
    out = capsys.readouterr().out
    assert "CAVE" in out and "VILLAGE" in out


def test_main_reject_unmarked_scoped_to_one_scene_leaves_other_scenes_untouched(
        tmp_path):
    assets = tmp_path / "tools" / "assets"
    cand = assets / "candidates"
    h1 = record(1, status=art_status.CANDIDATE)
    h2 = record(2, status=art_status.CANDIDATE, noun="cellar")
    t1 = record(3, scene="VILLAGE", noun="square",
               status=art_status.CANDIDATE)
    for rec in (h1, h2, t1):
        make_candidate(cand, rec)
    manifest = {"1": h1, "2": h2, "3": t1}
    fetch_art.save_manifest(assets / "art_manifest.json", manifest)

    assert art_review.main(["--reject-unmarked", "CAVE"],
                           repo=tmp_path) == 0

    saved = fetch_art.load_manifest(assets / "art_manifest.json")
    assert saved["1"]["status"] == art_status.REJECTED
    assert saved["2"]["status"] == art_status.REJECTED
    assert saved["3"]["status"] == art_status.CANDIDATE, \
        "a mood-scoped sweep must leave every other mood's candidates alone"


def test_scene_of_prefers_scene_over_mood():
    rec = record(1, scene="CAVE")
    rec["scene"] = "CAVE"
    assert art_review.scene_of(rec) == "CAVE"


def test_scene_of_falls_back_to_mood_for_a_stale_record():
    """Nothing writes mood any more; the fallback exists so a manifest left
    over from before the rewrite degrades to an unknown scene the server
    drops, rather than a KeyError."""
    assert art_review.scene_of({"id": 1, "mood": "HORROR"}) == "HORROR"


def test_rel_is_game_then_scene_then_the_picture():
    """The layout make_tga.convert_game_tree walks. It globs a scene directory
    for files without recursing, so a noun segment -- which this path used to
    carry -- hides every picture from the disc build."""
    rec = record(1, game="ENCHANTR", scene="CAVE", noun="tunnel")
    assert art_review._rel(rec) == Path("ENCHANTR") / "CAVE" / "1.png"


def test_rel_degrades_rather_than_raising_on_a_record_with_no_game():
    """A manifest left over from before the game-first rewrite must render as
    an unknown pool the server quietly drops, not a KeyError that takes the
    page down."""
    stale = {"id": 1, "mood": "HORROR", "noun": "hallway"}
    assert art_review._rel(stale) == Path("UNKNOWN") / "HORROR" / "1.png"


def test_promote_moves_a_candidate_from_the_flat_scene_directory(
        tmp_path):
    cand, png = tmp_path / "c", tmp_path / "png"
    rec = scene_record(1, scene="CAVE", noun="tunnel")
    make_scene_candidate(cand, rec)
    manifest = {"1": rec}

    counts = art_review.promote({"1": "accept"}, manifest, cand, png)

    assert (png / "ZORK1" / "CAVE" / "1.png").exists()
    assert rec["status"] == art_status.ACCEPTED
    assert counts == {"CAVE": art_review.Counts(gained=1, lost=0)}


def test_promote_counts_are_grouped_by_scene(tmp_path):
    """One promote call spanning two scenes must report them apart, since the
    caller prints a line per scene."""
    cand, png = tmp_path / "candidates", tmp_path / "png"
    a = record(1, scene="CAVE", noun="tunnel")
    b = record(2, scene="PARLOR", noun="hallway")
    make_candidate(cand, a)
    make_candidate(cand, b)
    manifest = {"1": a, "2": b}
    counts = art_review.promote({"1": "accept", "2": "accept"}, manifest,
                                cand, png)
    assert counts["CAVE"].gained == 1
    assert counts["PARLOR"].gained == 1


def test_dedup_does_not_run_across_games(tmp_path):
    """Two stories ship their own copy of a picture -- art is duplicated per
    game by design -- so the same photograph in ZORK1 and ENCHANTR is not a
    duplicate to be dropped."""
    a = record(1, game="ZORK1", phash="ff00ff00ff00ff00")
    b = record(2, game="ENCHANTR", phash="ff00ff00ff00ff00")
    assert len(art_review.dedup([a])) == 1
    assert len(art_review.dedup([b])) == 1
