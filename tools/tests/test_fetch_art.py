"""Exercise the fetcher against a stub. No network, ever."""
import io
import json
import sys
import types
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


class FakeResponse:
    """Stands in for a requests.Response."""

    def __init__(self, status_code=200, payload=None, content=b""):
        self.status_code = status_code
        self._payload = payload or {}
        self.content = content

    def json(self):
        return self._payload

    def raise_for_status(self):
        if self.status_code >= 400:
            raise RuntimeError(f"HTTP {self.status_code}")


class FakeSession:
    """Stands in for a requests.Session. Never touches the network."""

    def __init__(self, responses):
        self.responses = list(responses)
        self.calls = []

    def get(self, url, timeout=None, params=None):
        self.calls.append((url, params))
        return self.responses.pop(0)


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


def test_main_runs_the_real_chain_with_a_stubbed_fetcher(monkeypatch, capsys):
    """Exercises load -> nouns_by_mood -> build -> harvest against the real
    shipped vocabulary and classifier table, with only the network layer
    (PixabayFetcher itself) stubbed out."""
    monkeypatch.setenv("PIXABAY_API_KEY", "test-key")
    saved = {}

    def fake_save_manifest(path, data):
        saved["data"] = data

    monkeypatch.setattr(fetch_art, "load_manifest", lambda path: {})
    monkeypatch.setattr(fetch_art, "save_manifest", fake_save_manifest)
    monkeypatch.setattr(fetch_art, "PixabayFetcher",
                        lambda key: StubFetcher(per_phrase=1, value=70))

    assert fetch_art.main(["1"]) == 0
    assert saved["data"], "the chain must reach harvest and write manifest entries"
    assert all("mood" in r and "phrase" in r for r in saved["data"].values())
    assert any(r["status"] == art_status.CANDIDATE for r in saved["data"].values())
    out = capsys.readouterr().out
    assert "candidates written" in out


def test_pixabay_fetcher_search_maps_hits_and_prefers_large_image():
    payload = {"hits": [
        {"id": 42, "pageURL": "https://pixabay.com/photos/42/",
         "largeImageURL": "https://cdn.example/large42.jpg",
         "webformatURL": "https://cdn.example/web42.jpg"},
        {"id": 43, "pageURL": "https://pixabay.com/photos/43/",
         "webformatURL": "https://cdn.example/web43.jpg"},
    ]}
    session = FakeSession([FakeResponse(200, payload)])
    f = fetch_art.PixabayFetcher("key", session=session, pause=0)
    hits = f.search("dark hallway", per_page=12)
    assert hits[0] == {"id": 42, "page_url": "https://pixabay.com/photos/42/",
                       "image_url": "https://cdn.example/large42.jpg"}
    assert hits[1]["image_url"] == "https://cdn.example/web43.jpg", \
        "must fall back to webformatURL when largeImageURL is absent"


def test_pixabay_fetcher_search_returns_empty_on_non_200():
    session = FakeSession([FakeResponse(503, {})])
    f = fetch_art.PixabayFetcher("key", session=session, pause=0)
    assert f.search("dark hallway", per_page=12) == []


def test_pixabay_fetcher_download_returns_response_content():
    session = FakeSession([FakeResponse(200, content=b"pngbytes")])
    f = fetch_art.PixabayFetcher("key", session=session, pause=0)
    assert f.download("https://cdn.example/x.jpg") == b"pngbytes"


def test_phash_reflects_image_content_not_a_constant(tmp_path, monkeypatch):
    """A stubbed imagehash module, so this pins _phash's own wiring
    regardless of whether the real optional dependency is installed."""
    class FakeHash:
        def __init__(self, digest):
            self._digest = digest

        def __str__(self):
            return self._digest

    def fake_phash(im):
        px = im.convert("L").tobytes()
        return FakeHash(format(sum(px) % (16 ** 16), "016x"))

    monkeypatch.setitem(sys.modules, "imagehash",
                        types.SimpleNamespace(phash=fake_phash))

    p1, p2 = tmp_path / "a.png", tmp_path / "b.png"
    Image.new("RGB", (320, 224), (10, 10, 10)).save(p1, "PNG")
    Image.new("RGB", (320, 224), (245, 245, 245)).save(p2, "PNG")

    h1, h2 = fetch_art._phash(p1), fetch_art._phash(p2)
    assert h1 and h2
    assert h1 != h2, "a constant _phash would collapse every candidate to one hash"
