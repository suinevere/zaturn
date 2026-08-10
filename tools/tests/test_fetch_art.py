"""Exercise the fetcher against a stub. No network, ever."""
import io
import json
import sys
import types
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_nouns
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

    def get(self, url, timeout=None, params=None, headers=None):
        self.calls.append((url, params, headers))
        return self.responses.pop(0)


class PingRaisesSession:
    """A FakeSession-alike whose .get raises only for one specific url,
    so a download-ping's connection failure can be pinned without also
    breaking the image bytes fetch that must still succeed after it."""

    def __init__(self, raising_url, exc, responses):
        self.raising_url = raising_url
        self.exc = exc
        self.responses = list(responses)
        self.calls = []

    def get(self, url, timeout=None, params=None, headers=None):
        self.calls.append((url, params, headers))
        if url == self.raising_url:
            raise self.exc
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


def test_reset_rejected_drops_only_metric_rejected_records():
    manifest = {
        "1": {"id": 1, "status": art_status.METRIC_REJECTED},
        "2": {"id": 2, "status": art_status.CANDIDATE},
        "3": {"id": 3, "status": art_status.METRIC_REJECTED},
        "4": {"id": 4, "status": art_status.ACCEPTED},
        "5": {"id": 5, "status": art_status.REJECTED},
    }
    dropped = fetch_art.reset_rejected(manifest)
    assert dropped == 2
    assert set(manifest) == {"2", "4", "5"}
    assert manifest["2"]["status"] == art_status.CANDIDATE
    assert manifest["4"]["status"] == art_status.ACCEPTED
    assert manifest["5"]["status"] == art_status.REJECTED


def test_main_reset_rejected_flag_drops_and_saves_without_fetching(monkeypatch, capsys):
    starting = {
        "1": {"id": 1, "status": art_status.METRIC_REJECTED},
        "2": {"id": 2, "status": art_status.CANDIDATE},
    }
    saved = {}

    def fail_if_called(*args, **kwargs):
        raise AssertionError("--reset-rejected must not fetch")

    monkeypatch.setattr(fetch_art, "load_manifest", lambda path: dict(starting))
    monkeypatch.setattr(fetch_art, "save_manifest",
                        lambda path, data: saved.update(data=data))
    monkeypatch.setattr(fetch_art, "PixabayFetcher", fail_if_called)

    assert fetch_art.main(["--reset-rejected"]) == 0
    assert set(saved["data"]) == {"2"}
    assert "dropped 1" in capsys.readouterr().out


def test_manifest_round_trips(tmp_path):
    path = tmp_path / "m.json"
    fetch_art.save_manifest(path, {"1": {"id": 1, "licence": fetch_art.LICENCE}})
    assert fetch_art.load_manifest(path)["1"]["id"] == 1
    assert fetch_art.load_manifest(tmp_path / "absent.json") == {}


def test_no_api_key_is_reported_not_raised(monkeypatch, capsys, tmp_path):
    monkeypatch.delenv("PIXABAY_API_KEY", raising=False)
    monkeypatch.setattr(fetch_art, "DOTENV_PATH", tmp_path / "absent.env")
    assert fetch_art.main([]) == 0
    assert "PIXABAY_API_KEY" in capsys.readouterr().out


def test_main_runs_the_real_chain_with_a_stubbed_fetcher(monkeypatch, capsys, tmp_path):
    """Exercises load -> nouns_by_mood -> build -> harvest against the real
    shipped vocabulary and classifier table, with only the network layer
    (PixabayFetcher itself) stubbed out."""
    monkeypatch.setenv("PIXABAY_API_KEY", "test-key")
    monkeypatch.setattr(fetch_art, "DOTENV_PATH", tmp_path / "absent.env")
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


def test_unsplash_fetcher_search_maps_hits_to_shared_hit_shape():
    payload = {"results": [
        {"id": "abc123",
         "urls": {"regular": "https://images.unsplash.com/abc123",
                  "full": "https://images.unsplash.com/abc123-full"},
         "links": {"html": "https://unsplash.com/photos/abc123",
                   "download_location":
                       "https://api.unsplash.com/photos/abc123/download"},
         "user": {"name": "Priya Shah",
                   "links": {"html": "https://unsplash.com/@priyashah"}}},
    ]}
    session = FakeSession([FakeResponse(200, payload)])
    f = fetch_art.UnsplashFetcher("access-key-fake", session=session, budget=50)
    hits = f.search("cracked sandstone canyon", per_page=12)
    assert hits == [{
        "id": "abc123",
        "page_url": "https://unsplash.com/photos/abc123",
        "image_url": "https://images.unsplash.com/abc123",
        "author": "Priya Shah",
        "author_url": "https://unsplash.com/@priyashah",
    }]
    assert f.used == 1


def test_unsplash_fetcher_search_prefers_regular_falls_back_to_full():
    payload = {"results": [
        {"id": "noregular",
         "urls": {"full": "https://images.unsplash.com/noregular-full"},
         "links": {"html": "", "download_location": ""},
         "user": {"name": "", "links": {}}},
    ]}
    session = FakeSession([FakeResponse(200, payload)])
    f = fetch_art.UnsplashFetcher("access-key-fake", session=session, budget=50)
    hits = f.search("hazy mesa", per_page=12)
    assert hits[0]["image_url"] == "https://images.unsplash.com/noregular-full"


def test_unsplash_fetcher_search_returns_empty_on_non_200():
    session = FakeSession([FakeResponse(503, {})])
    f = fetch_art.UnsplashFetcher("access-key-fake", session=session, budget=50)
    assert f.search("cracked sandstone canyon", per_page=12) == []
    assert f.used == 1, "the failed call still spent one request"


def test_unsplash_fetcher_download_fires_the_download_location_ping():
    session = FakeSession([
        FakeResponse(200, content=b""),
        FakeResponse(200, content=b"realimagebytes"),
    ])
    f = fetch_art.UnsplashFetcher("access-key-fake", session=session, budget=50)
    f._download_locations["https://images.unsplash.com/p7"] = \
        "https://api.unsplash.com/photos/p7/download"
    result = f.download("https://images.unsplash.com/p7")
    assert result == b"realimagebytes"
    called_urls = [c[0] for c in session.calls]
    assert "https://api.unsplash.com/photos/p7/download" in called_urls, \
        "download() must ping links.download_location, per Unsplash's API guidelines"
    assert f.used == 1, "the ping must count against the budget"


def test_unsplash_fetcher_download_ping_http_failure_does_not_lose_the_image():
    session = FakeSession([
        FakeResponse(503, content=b"ping error body"),
        FakeResponse(200, content=b"keptbytes"),
    ])
    f = fetch_art.UnsplashFetcher("access-key-fake", session=session, budget=50)
    f._download_locations["https://images.unsplash.com/z9"] = \
        "https://api.unsplash.com/photos/z9/download"
    assert f.download("https://images.unsplash.com/z9") == b"keptbytes"


def test_unsplash_fetcher_download_ping_connection_error_does_not_lose_the_image():
    ping_url = "https://api.unsplash.com/photos/m4/download"
    session = PingRaisesSession(ping_url, IOError("connection reset"),
                                responses=[FakeResponse(200, content=b"survives")])
    f = fetch_art.UnsplashFetcher("access-key-fake", session=session, budget=50)
    f._download_locations["https://images.unsplash.com/m4"] = ping_url
    assert f.download("https://images.unsplash.com/m4") == b"survives"


def test_unsplash_fetcher_search_budget_stops_the_run_and_reports_skips(capsys):
    session = FakeSession([FakeResponse(200, {"results": []})])
    f = fetch_art.UnsplashFetcher("access-key-fake", session=session, budget=1)
    f.search("first phrase", per_page=12)
    assert f.used == 1
    hits = f.search("second phrase", per_page=12)
    assert hits == [], "a spent budget must degrade to an empty search, not raise"
    assert f.skipped == 1
    assert "budget" in capsys.readouterr().out.lower()


def test_unsplash_fetcher_download_ping_skipped_when_budget_exhausted_but_image_still_fetched(capsys):
    session = FakeSession([FakeResponse(200, content=b"stillfetched")])
    f = fetch_art.UnsplashFetcher("access-key-fake", session=session, budget=0)
    f._download_locations["https://images.unsplash.com/q3"] = \
        "https://api.unsplash.com/photos/q3/download"
    result = f.download("https://images.unsplash.com/q3")
    assert result == b"stillfetched"
    assert f.skipped == 1
    assert "budget" in capsys.readouterr().out.lower()


def test_unsplash_fetcher_report_prints_used_and_skipped_counts(capsys):
    f = fetch_art.UnsplashFetcher("access-key-fake",
                                  session=FakeSession([]), budget=7)
    f.used = 5
    f.skipped = 3
    f.report()
    out = capsys.readouterr().out
    assert "5" in out and "7" in out and "3" in out


def test_harvest_records_unsplash_source_licence_author_fields(tmp_path):
    payload = {"results": [
        {"id": "u1",
         "urls": {"regular": "https://images.unsplash.com/u1"},
         "links": {"html": "https://unsplash.com/photos/u1",
                   "download_location":
                       "https://api.unsplash.com/photos/u1/download"},
         "user": {"name": "Asymmetric Artist",
                   "links": {"html": "https://unsplash.com/@asymmetric"}}},
    ]}
    session = FakeSession([
        FakeResponse(200, payload),
        FakeResponse(200, content=b""),
        FakeResponse(200, content=png_bytes(70)),
    ])
    f = fetch_art.UnsplashFetcher("access-key-fake", session=session, budget=50)
    plan = {"HORROR": [Query("HORROR", "HOUSE", "hallway", "dark", "dark hallway")]}
    manifest = {}
    got = fetch_art.harvest(plan, f, tmp_path, manifest, per_mood_budget=99)
    assert len(got) == 1
    rec = manifest["u1"]
    assert rec["source"] == "unsplash"
    assert rec["licence"] == "Unsplash License"
    assert rec["author"] == "Asymmetric Artist"
    assert rec["author_url"] == "https://unsplash.com/@asymmetric"


def test_harvest_continues_cleanly_when_unsplash_budget_runs_out(tmp_path):
    payload = {"results": [
        {"id": "b1", "urls": {"regular": "https://images.unsplash.com/b1"},
         "links": {"html": "https://unsplash.com/photos/b1",
                   "download_location": ""},
         "user": {"name": "First", "links": {"html": "https://unsplash.com/@first"}}}
    ]}
    session = FakeSession([FakeResponse(200, payload),
                           FakeResponse(200, content=png_bytes(70))])
    f = fetch_art.UnsplashFetcher("access-key-fake", session=session, budget=1)
    plan = {"HORROR": [Query("HORROR", "HOUSE", "hallway", "dark", "one"),
                       Query("HORROR", "EXTRA", "morgue", "dark", "two")]}
    manifest = {}
    got = fetch_art.harvest(plan, f, tmp_path, manifest, per_mood_budget=99)
    assert len(got) == 1, "the second query's search must degrade, not crash the run"
    assert f.used == 1
    assert f.skipped == 1


def test_harvest_records_pixabay_source_field_via_real_fetcher(tmp_path):
    payload = {"hits": [
        {"id": 501, "pageURL": "https://pixabay.com/photos/501/",
         "largeImageURL": "https://cdn.example/large501.jpg"},
    ]}
    session = FakeSession([FakeResponse(200, payload),
                           FakeResponse(200, content=png_bytes(70))])
    f = fetch_art.PixabayFetcher("key-fake", session=session, pause=0)
    plan = {"HORROR": [Query("HORROR", "HOUSE", "hallway", "dark", "dark hallway")]}
    manifest = {}
    got = fetch_art.harvest(plan, f, tmp_path, manifest, per_mood_budget=99)
    assert len(got) == 1
    assert manifest["501"]["source"] == "pixabay"
    assert manifest["501"]["licence"] == fetch_art.LICENCE
    assert "author" not in manifest["501"], \
        "Pixabay hits carry no author field, so none must be invented"


def test_main_source_flag_selects_unsplash_and_reports_missing_key(monkeypatch, capsys, tmp_path):
    monkeypatch.delenv("UNSPLASH_ID", raising=False)
    monkeypatch.setattr(fetch_art, "DOTENV_PATH", tmp_path / "absent.env")
    assert fetch_art.main(["--source", "unsplash"]) == 0
    assert "UNSPLASH_ID" in capsys.readouterr().out


def test_main_budget_flag_is_parsed_without_disturbing_positional_args(monkeypatch, tmp_path):
    monkeypatch.setenv("PIXABAY_API_KEY", "test-key-fake")
    monkeypatch.setattr(fetch_art, "DOTENV_PATH", tmp_path / "absent.env")
    saved = {}
    seen_budget = {}

    monkeypatch.setattr(fetch_art, "load_manifest", lambda path: {})
    monkeypatch.setattr(fetch_art, "save_manifest",
                        lambda path, data: saved.update(data=data))
    monkeypatch.setattr(fetch_art, "PixabayFetcher",
                        lambda key: StubFetcher(per_phrase=1, value=70))

    def fake_harvest(plan, fetcher, out_dir, manifest, per_mood_budget, total_budget=None):
        seen_budget["per_mood_budget"] = per_mood_budget
        return []

    monkeypatch.setattr(fetch_art, "harvest", fake_harvest)
    assert fetch_art.main(["3", "--budget", "10"]) == 0
    assert seen_budget["per_mood_budget"] == 3, \
        "--budget must not shift the positional per_mood_budget argument"


def _stub_main_for_mood_capture(monkeypatch, tmp_path, seen):
    """Wire main() so the plan reaching harvest() is captured, not fetched.

    Description: Shared rigging for the --mood tests below -- a real
        PixabayFetcher key so main() proceeds past the key check, and a
        fake harvest() that records the mood keys (and each mood's own
        query set, whose phrases are disjoint mood to mood, so a passing
        assertion cannot be a coincidence -- two moods can and do reach
        equal noun counts) instead of touching the network.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: monkeypatch -- pytest fixture; tmp_path -- pytest fixture;
        seen -- dict this function fills with "moods" and "counts"
    Returns: N/A
    """
    monkeypatch.setenv("PIXABAY_API_KEY", "test-key-fake")
    monkeypatch.setattr(fetch_art, "DOTENV_PATH", tmp_path / "absent.env")
    monkeypatch.setattr(fetch_art, "load_manifest", lambda path: {})
    monkeypatch.setattr(fetch_art, "save_manifest", lambda path, data: None)
    monkeypatch.setattr(fetch_art, "PixabayFetcher",
                        lambda key: StubFetcher(per_phrase=1, value=70))

    def fake_harvest(plan, fetcher, out_dir, manifest, per_mood_budget, total_budget=None):
        seen["moods"] = sorted(plan)
        seen["counts"] = {mood: len(queries) for mood, queries in plan.items()}
        seen["phrases"] = {mood: {q.phrase for q in queries}
                           for mood, queries in plan.items()}
        return []

    monkeypatch.setattr(fetch_art, "harvest", fake_harvest)


def test_main_mood_flag_restricts_the_plan_to_the_named_mood(monkeypatch, tmp_path):
    seen = {}
    _stub_main_for_mood_capture(monkeypatch, tmp_path, seen)
    assert fetch_art.main(["--mood", "SCIFI"]) == 0
    assert seen["moods"] == ["SCIFI"], \
        "every other mood must be entirely unqueried, not just budget-starved"
    assert seen["counts"]["SCIFI"] > 0


def test_main_mood_flag_accepts_multiple_comma_separated_moods(monkeypatch, tmp_path):
    seen = {}
    _stub_main_for_mood_capture(monkeypatch, tmp_path, seen)
    assert fetch_art.main(["--mood", "SCIFI,TOWN"]) == 0
    assert seen["moods"] == ["SCIFI", "TOWN"]
    assert not seen["phrases"]["SCIFI"] & seen["phrases"]["TOWN"], \
        "SCIFI and TOWN must draw from genuinely different vocabularies, " \
        "so this assertion cannot pass by fixture coincidence. Disjoint " \
        "phrases, not unequal counts: two moods can hold equal noun counts."


def test_main_mood_flag_is_case_insensitive(monkeypatch, tmp_path):
    seen = {}
    _stub_main_for_mood_capture(monkeypatch, tmp_path, seen)
    assert fetch_art.main(["--mood", "scifi"]) == 0
    assert seen["moods"] == ["SCIFI"]


def test_main_mood_flag_reports_an_unknown_mood_but_still_runs_the_known_ones(
        monkeypatch, tmp_path, capsys):
    seen = {}
    _stub_main_for_mood_capture(monkeypatch, tmp_path, seen)
    assert fetch_art.main(["--mood", "SCIFI,NOTAMOOD"]) == 0
    assert seen["moods"] == ["SCIFI"]
    out = capsys.readouterr().out
    assert "NOTAMOOD" in out


def test_main_mood_flag_all_unknown_prints_the_valid_list_and_fetches_nothing(
        monkeypatch, tmp_path, capsys):
    monkeypatch.setenv("PIXABAY_API_KEY", "test-key-fake")
    monkeypatch.setattr(fetch_art, "DOTENV_PATH", tmp_path / "absent.env")

    def fail_if_called(*args, **kwargs):
        raise AssertionError("an all-unknown --mood must not reach harvest")

    monkeypatch.setattr(fetch_art, "PixabayFetcher", fail_if_called)
    monkeypatch.setattr(fetch_art, "harvest", fail_if_called)

    assert fetch_art.main(["--mood", "NOTAMOOD,ALSOFAKE"]) == 0
    out = capsys.readouterr().out
    for mood in art_nouns.MOODS:
        assert mood in out, f"the everything-degrades message must name {mood}"


def test_main_without_mood_flag_plans_all_twelve_moods_as_before(monkeypatch, tmp_path):
    seen = {}
    _stub_main_for_mood_capture(monkeypatch, tmp_path, seen)
    assert fetch_art.main([]) == 0
    assert seen["moods"] == sorted(art_nouns.MOODS)
    assert len(set(seen["counts"].values())) > 1, \
        "the real vocabulary's per-mood query counts differ; a flattened " \
        "fixture would defeat the point of asserting against them"


def test_dotenv_key_found_in_file_when_environment_is_empty(tmp_path):
    env_path = tmp_path / ".env"
    env_path.write_text("PIXABAY_API_KEY=abc123\n", encoding="utf-8")
    env = {}
    fetch_art.load_dotenv_into_environ(env_path, env)
    assert env["PIXABAY_API_KEY"] == "abc123"


def test_explicit_env_var_wins_over_dotenv_file(tmp_path):
    env_path = tmp_path / ".env"
    env_path.write_text("PIXABAY_API_KEY=fromfile\n", encoding="utf-8")
    env = {"PIXABAY_API_KEY": "fromenv"}
    fetch_art.load_dotenv_into_environ(env_path, env)
    assert env["PIXABAY_API_KEY"] == "fromenv"


def test_missing_dotenv_file_is_harmless(tmp_path):
    env = {}
    fetch_art.load_dotenv_into_environ(tmp_path / "absent.env", env)
    assert env == {}


def test_dotenv_handles_quotes_whitespace_comments_and_embedded_equals(tmp_path):
    env_path = tmp_path / ".env"
    env_path.write_text(
        "\n"
        "# a comment line\n"
        '  PIXABAY_API_KEY = "abc=123"  \n'
        "SINGLE='quoted value'\n",
        encoding="utf-8",
    )
    env = {}
    fetch_art.load_dotenv_into_environ(env_path, env)
    assert env["PIXABAY_API_KEY"] == "abc=123"
    assert env["SINGLE"] == "quoted value"


def test_malformed_dotenv_lines_are_ignored_not_raised(tmp_path):
    env_path = tmp_path / ".env"
    env_path.write_text(
        "this line has no equals sign\n"
        "=novalue\n"
        "PIXABAY_API_KEY=abc123\n",
        encoding="utf-8",
    )
    env = {}
    fetch_art.load_dotenv_into_environ(env_path, env)
    assert env == {"PIXABAY_API_KEY": "abc123"}


def test_dotenv_default_path_is_beside_the_module_not_the_cwd():
    assert fetch_art.DOTENV_PATH == Path(fetch_art.__file__).resolve().parent / ".env"


def test_dotenv_loader_never_prints_the_value(tmp_path, capsys):
    env_path = tmp_path / ".env"
    env_path.write_text("PIXABAY_API_KEY=super-secret-value\n", encoding="utf-8")
    fetch_art.load_dotenv_into_environ(env_path, {})
    assert "super-secret-value" not in capsys.readouterr().out


def test_undecodable_dotenv_parses_to_empty(tmp_path):
    env_path = tmp_path / ".env"
    env_path.write_bytes(b"\xff\xfe garbage\n")
    assert fetch_art._parse_dotenv(env_path) == {}


def test_undecodable_dotenv_load_is_harmless(tmp_path):
    env_path = tmp_path / ".env"
    env_path.write_bytes(b"\xff\xfe garbage\n")
    env = {}
    fetch_art.load_dotenv_into_environ(env_path, env)
    assert env == {}


def test_undecodable_dotenv_does_not_crash_main(monkeypatch, capsys, tmp_path):
    env_path = tmp_path / ".env"
    env_path.write_bytes(b"\xff\xfe garbage\n")
    monkeypatch.delenv("PIXABAY_API_KEY", raising=False)
    monkeypatch.setattr(fetch_art, "DOTENV_PATH", env_path)
    assert fetch_art.main([]) == 0
    assert "PIXABAY_API_KEY" in capsys.readouterr().out


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
