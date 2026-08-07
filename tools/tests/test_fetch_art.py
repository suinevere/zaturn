"""Exercise the fetcher against a stub. No network, ever."""
import io
import json
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_status
import fetch_art
from art_queries import Query


def png_bytes(value, size=(640, 480)):
    buf = io.BytesIO()
    Image.new("RGB", size, (value, value, value)).save(buf, "PNG")
    return buf.getvalue()


class StubFetcher:
    """Stands in for search + download. Records what it was asked for."""

    def __init__(self, per_phrase=2, value=70):
        self.per_phrase = per_phrase
        self.value = value
        self.asked = []
        self.downloads = 0
        self._next = 1000
        self._ids = {}

    def search(self, phrase, per_page):
        self.asked.append(phrase)
        if phrase not in self._ids:
            ids = []
            for _ in range(self.per_phrase):
                self._next += 1
                ids.append(self._next)
            self._ids[phrase] = ids
        return [{"id": i, "page_url": f"https://pixabay.com/photos/{i}/",
                 "image_url": f"https://cdn.example/{i}.jpg"}
                for i in self._ids[phrase]]

    def download(self, url):
        self.downloads += 1
        return png_bytes(self.value)


PLAN = {"HORROR": [Query("HORROR", "HOUSE", "hallway", "dark", "dark hallway"),
                   Query("HORROR", "EXTRA", "morgue", "dark", "dark morgue")]}

PLAN2 = {
    "DESERT": [Query("DESERT", "DESERT", "oasis", "arid", "arid oasis"),
              Query("DESERT", "DESERT", "mesa", "arid", "arid mesa")],
    "TOWN":   [Query("TOWN", "TOWN", "alleyway", "quiet", "quiet alleyway")],
}


def test_harvest_writes_one_png_per_surviving_candidate(tmp_path):
    f = StubFetcher()
    got = fetch_art.harvest(PLAN, f, tmp_path, {}, per_mood_budget=99)
    assert len(got) == 4
    for c in got:
        assert c.path.exists()
        assert Image.open(c.path).size == (320, 224)


def test_provenance_lands_in_the_folder_path(tmp_path):
    got = fetch_art.harvest(PLAN, StubFetcher(), tmp_path, {}, per_mood_budget=99)
    rel = sorted(str(c.path.relative_to(tmp_path)).replace("\\", "/") for c in got)
    assert rel[0].startswith("HORROR/EXTRA/morgue/")
    assert rel[-1].startswith("HORROR/HOUSE/hallway/")


def test_rejected_candidates_are_recorded_but_not_written(tmp_path):
    f = StubFetcher(value=252)
    manifest = {}
    got = fetch_art.harvest(PLAN, f, tmp_path, manifest, per_mood_budget=99)
    assert got == []
    assert len(manifest) == 4
    assert all(r["verdict"] == "bright" for r in manifest.values())
    assert all(r["status"] == art_status.METRIC_REJECTED for r in manifest.values())
    assert all(r["luminance"] == 252.0 for r in manifest.values())
    assert all(r["busyness"] == 0.0 for r in manifest.values())
    assert all(r["banding"] == 0.0 for r in manifest.values())


def test_an_already_seen_id_is_not_downloaded_twice(tmp_path):
    f = StubFetcher()
    manifest = {}
    fetch_art.harvest(PLAN, f, tmp_path, manifest, per_mood_budget=99)
    before = f.downloads
    fetch_art.harvest(PLAN, f, tmp_path, manifest, per_mood_budget=99)
    assert f.downloads == before, "a known id must not be fetched again"


def test_budget_stops_the_run(tmp_path):
    f = StubFetcher()
    got = fetch_art.harvest(PLAN, f, tmp_path, {}, per_mood_budget=1)
    assert len(got) == 1


def test_budget_is_per_mood_not_global(tmp_path):
    """DESERT sorts before TOWN; a global counter would let DESERT alone
    exhaust the budget and leave TOWN with nothing -- the bug this pins."""
    f = StubFetcher(per_phrase=2)
    got = fetch_art.harvest(PLAN2, f, tmp_path, {}, per_mood_budget=1)
    by_mood = {}
    for c in got:
        by_mood[c.query.mood] = by_mood.get(c.query.mood, 0) + 1
    assert by_mood == {"DESERT": 1, "TOWN": 1}


def test_total_budget_caps_the_whole_run_across_moods(tmp_path):
    f = StubFetcher(per_phrase=2)
    got = fetch_art.harvest(PLAN2, f, tmp_path, {}, per_mood_budget=99,
                            total_budget=1)
    assert len(got) == 1


def test_a_download_failure_skips_that_hit_and_continues(tmp_path):
    class Flaky(StubFetcher):
        def download(self, url):
            self.downloads += 1
            if self.downloads == 1:
                raise IOError("connection reset")
            return png_bytes(70)

    got = fetch_art.harvest(PLAN, Flaky(), tmp_path, {}, per_mood_budget=99)
    assert len(got) == 3


def test_manifest_round_trips(tmp_path):
    path = tmp_path / "m.json"
    fetch_art.save_manifest(path, {"1": {"id": 1, "licence": fetch_art.LICENCE}})
    assert fetch_art.load_manifest(path)["1"]["id"] == 1
    assert fetch_art.load_manifest(tmp_path / "absent.json") == {}


def test_no_api_key_is_reported_not_raised(monkeypatch, capsys):
    monkeypatch.delenv("PIXABAY_API_KEY", raising=False)
    assert fetch_art.main([]) == 0
    assert "PIXABAY_API_KEY" in capsys.readouterr().out
