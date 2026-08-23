"""Fetch, crop and score background candidates from Pixabay.

Description: Walks the query plan, asks Pixabay for each phrase, crops every hit
    to 320x224, scores it, and writes the survivors under <out>/<SCENE>/<noun>/.
    The path is the query's provenance: any picture on the disc can be traced
    back to the scene and noun that found it.

    Every failure degrades. No key, no network, a dead query or an image that will
    not decode all print and continue, because a run that aborts on hit 400 of 1200
    wastes the 399 before it.

    Nothing here decides what ships. Surviving the metric gate only earns a place
    in the review server (tools/art_server.py); a human accepts or rejects there.
Author: suinevere
Dependencies: requests, PIL, art_metrics, art_queries, art_nouns, art_status,
    scene_vocab
Globals: ENDPOINT, LICENCE
"""
import json
import os
import sys
import time
from collections import Counter, namedtuple
from datetime import date
from io import BytesIO
from pathlib import Path

from PIL import Image

import art_metrics
import art_nouns
import art_queries
import art_terms
import art_status
import scene_vocab as vocab

ENDPOINT = "https://pixabay.com/api/"
LICENCE = "Pixabay Content License"
UNSPLASH_ENDPOINT = "https://api.unsplash.com/search/photos"
DOTENV_PATH = Path(__file__).resolve().parent / ".env"

Candidate = namedtuple("Candidate", "query hit scores verdict phash path")


def load_manifest(path):
    """Read the provenance manifest, or an empty one if it does not exist yet.

    Description: A missing manifest means "first run", not an error.
    Author: suinevere
    Dependencies: json
    Globals: N/A
    Params: path -- the manifest file
    Returns: dict keyed by "<game>:<id>"
    """
    path = Path(path)
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def save_manifest(path, data):
    """Write the provenance manifest, sorted so its diffs stay readable.

    Description: Creates the parent directory, so a first run on a clean
        checkout does not need one made for it.
    Author: suinevere
    Dependencies: json
    Globals: N/A
    Params: path -- the manifest file; data -- the dict to write
    Returns: N/A
    """
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with Path(path).open("w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2, sort_keys=True)
        fh.write("\n")


def harvest(plan, fetcher, out_dir, manifest, per_scene_budget, total_budget=None):
    """Fetch and gate every query in the plan, stopping each scene at its own budget.

    Description: The budget is per scene, not global -- scenes sort
        alphabetically and a shared counter let an early one starve every
        scene after it. `total_budget` is an optional ceiling across the
        whole run, for a small first calibration batch; the per-scene cap is
        what keeps every scene represented and stays in force regardless of
        it.

        A picture already fetched for this game is skipped without a
        download, which is what makes a re-run cheap and a partial run
        resumable. The key carries the game, so the same photograph can be
        fetched again for a different story -- art is duplicated per game by
        design, and two stories curate their pools independently.

        Each record's "source" and "licence" come from the fetcher's own
        .source/.licence attributes, defaulting to Pixabay's when absent, so
        harvest() never has to know which fetcher it was given. A hit's
        "author"/"author_url" (Unsplash only) land in the record only when
        the hit carries them, so Pixabay records never gain invented fields.
    Author: suinevere
    Dependencies: PIL, art_metrics, art_status
    Globals: LICENCE
    Params: plan -- scene -> [Query]; fetcher -- an object with .search/.download,
        and optionally .source/.licence;
        out_dir -- where surviving PNGs go; manifest -- mutated in place;
        per_scene_budget -- stop each scene after this many survivors;
        total_budget -- optional ceiling across all scenes combined; None
            means only the per-scene caps apply
    Returns: the list of surviving Candidate
    """
    out_dir = Path(out_dir)
    kept = []

    for scene in sorted(plan):
        scene_kept = 0
        for query in plan[scene]:
            if scene_kept >= per_scene_budget:
                break
            if total_budget is not None and len(kept) >= total_budget:
                return kept
            try:
                hits = fetcher.search(query.phrase, per_page=12)
            except Exception as exc:
                print(f"  {query.phrase}: search failed ({exc})")
                continue

            for hit in hits:
                if scene_kept >= per_scene_budget:
                    break
                if total_budget is not None and len(kept) >= total_budget:
                    return kept
                key = f"{query.game}:{hit['id']}"
                if key in manifest:
                    continue
                try:
                    im = Image.open(BytesIO(fetcher.download(hit["image_url"])))
                    im.load()
                except Exception as exc:
                    print(f"  {hit['image_url']}: download failed ({exc})")
                    continue

                scores = art_metrics.score(im)
                call = art_metrics.verdict(scores)
                record = {
                    "id": hit["id"], "page_url": hit["page_url"],
                    "image_url": hit["image_url"], "phrase": query.phrase,
                    "game": query.game, "scene": query.scene,
                    "noun": query.noun,
                    "source": getattr(fetcher, "source", "pixabay"),
                    "licence": getattr(fetcher, "licence", LICENCE),
                    "fetched": date.today().isoformat(),
                    "luminance": round(scores.luminance, 2),
                    "busyness": round(scores.busyness, 2),
                    "banding": round(scores.banding, 2),
                    "verdict": call, "phash": "",
                    "status": (art_status.METRIC_REJECTED if call != "pass"
                               else art_status.CANDIDATE),
                }
                if "author" in hit:
                    record["author"] = hit["author"]
                if "author_url" in hit:
                    record["author_url"] = hit["author_url"]
                manifest[key] = record
                if call != "pass":
                    continue

                dest = out_dir / query.game / query.scene
                dest.mkdir(parents=True, exist_ok=True)
                path = dest / f"{hit['id']}.png"
                art_metrics.crop(im).save(path, "PNG")
                record["phash"] = _phash(path)
                kept.append(Candidate(query, hit, scores, call, record["phash"], path))
                scene_kept += 1

    return kept


def _phash(path):
    """A perceptual hash, or "" when imagehash is unavailable.

    Description: Optional on purpose -- dedup is a quality improvement, not a
        correctness requirement, and a missing wheel must not stop a fetch.
    Author: suinevere
    Dependencies: imagehash (optional), PIL
    Globals: N/A
    Params: path -- a written PNG
    Returns: the hash as a string, or ""
    """
    try:
        import imagehash
    except ImportError:
        return ""
    return str(imagehash.phash(Image.open(path)))


class PixabayFetcher:
    """Search and download against the live Pixabay API.

    Description: Sleeps between calls to stay inside the documented 100
        requests-per-minute allowance, and treats any non-200 as an empty result
        rather than an exception, so one bad phrase cannot end a run.
    Author: suinevere
    Dependencies: requests
    Globals: ENDPOINT
    """

    source = "pixabay"
    licence = LICENCE

    def __init__(self, key, session=None, pause=0.7):
        if session is None:
            import requests
            session = requests.Session()
        self.key = key
        self.session = session
        self.pause = pause

    def search(self, phrase, per_page=12):
        """Query Pixabay for one phrase and map hits to the shape harvest() expects.

        Author: suinevere
        Dependencies: requests
        Globals: ENDPOINT
        Params: phrase -- a search phrase; per_page -- Pixabay's page size
        Returns: a list of {id, page_url, image_url} dicts, empty on any
            non-200 response
        """
        time.sleep(self.pause)
        r = self.session.get(ENDPOINT, timeout=30, params={
            "key": self.key, "q": phrase, "image_type": "photo",
            "orientation": "horizontal", "safesearch": "true",
            "min_width": 640, "per_page": per_page,
        })
        if r.status_code != 200:
            print(f"  {phrase}: HTTP {r.status_code}")
            return []
        return [{"id": h["id"], "page_url": h["pageURL"],
                 "image_url": h.get("largeImageURL") or h["webformatURL"]}
                for h in r.json().get("hits", [])]

    def download(self, url):
        """Fetch one image's raw bytes.

        Author: suinevere
        Dependencies: requests
        Globals: N/A
        Params: url -- an image_url returned by search()
        Returns: the response body as bytes
        """
        r = self.session.get(url, timeout=60)
        r.raise_for_status()
        return r.content


class UnsplashFetcher:
    """Search and download against the live Unsplash API, under a hard request budget.

    Description: Mirrors PixabayFetcher's shape -- search(phrase) returning
        hit dicts, download(url) returning bytes -- so harvest() runs against
        either without knowing which it has. Unlike Pixabay's roomy 100
        requests/minute, Unsplash's demo-tier quota is a brutal 50
        requests/hour, so every HTTP call this fetcher makes -- each search
        and each download-location ping Unsplash's API guidelines require --
        is counted against an explicit budget and refused once spent, never
        slept through. A non-200 search degrades to an empty list, matching
        PixabayFetcher; a failed or budget-skipped ping is reported and
        ignored, since the ping is a courtesy to Unsplash, not a condition
        of the image download that already succeeded.
    Author: suinevere
    Dependencies: requests
    Globals: UNSPLASH_ENDPOINT
    """

    source = "unsplash"
    licence = "Unsplash License"

    def __init__(self, access_key, session=None, budget=50):
        if session is None:
            import requests
            session = requests.Session()
        self.access_key = access_key
        self.session = session
        self.budget = budget
        self.used = 0
        self.skipped = 0
        self._download_locations = {}

    def _spend(self):
        """Reserve one request against the budget, if any remains.

        Author: suinevere
        Dependencies: N/A
        Globals: N/A
        Params: N/A
        Returns: True (and increments self.used) if budget remains;
            otherwise increments self.skipped and returns False
        """
        if self.used >= self.budget:
            self.skipped += 1
            return False
        self.used += 1
        return True

    def search(self, phrase, per_page=12):
        """Query Unsplash for one phrase and map hits to the shape harvest() expects.

        Description: Records each hit's links.download_location alongside
            its image_url, so a later download(url) call can find and ping
            it without harvest() ever passing more than the url through.
        Author: suinevere
        Dependencies: requests
        Globals: UNSPLASH_ENDPOINT
        Params: phrase -- a search phrase; per_page -- Unsplash's page size
        Returns: a list of {id, page_url, image_url, author, author_url}
            dicts, empty when the budget is spent or on any non-200 response
        """
        if not self._spend():
            print(f"  {phrase}: Unsplash budget exhausted "
                  f"({self.used}/{self.budget} used), skipping search")
            return []
        r = self.session.get(
            UNSPLASH_ENDPOINT, timeout=30,
            headers={"Authorization": f"Client-ID {self.access_key}"},
            params={"query": phrase, "per_page": per_page,
                    "orientation": "landscape"})
        if r.status_code != 200:
            print(f"  {phrase}: HTTP {r.status_code}")
            return []
        hits = []
        for h in r.json().get("results", []):
            urls = h.get("urls", {})
            image_url = urls.get("regular") or urls.get("full") or urls.get("small", "")
            links = h.get("links", {})
            location = links.get("download_location", "")
            if location:
                self._download_locations[image_url] = location
            user = h.get("user", {})
            hits.append({
                "id": h["id"], "page_url": links.get("html", ""),
                "image_url": image_url,
                "author": user.get("name", ""),
                "author_url": user.get("links", {}).get("html", ""),
            })
        return hits

    def download(self, url):
        """Ping Unsplash's required download-tracking endpoint, then fetch the bytes.

        Description: The ping fires only when this url's search() call
            recorded a download_location for it, and only while budget
            remains; either a failed ping or a budget-skipped one is
            reported and ignored, and the image bytes are always still
            fetched afterward.
        Author: suinevere
        Dependencies: requests
        Globals: N/A
        Params: url -- an image_url returned by search()
        Returns: the response body as bytes
        """
        location = self._download_locations.get(url, "")
        if location:
            if self._spend():
                try:
                    ping = self.session.get(
                        location, timeout=30,
                        headers={"Authorization": f"Client-ID {self.access_key}"})
                    if ping.status_code != 200:
                        print(f"  download ping failed for {url}: "
                              f"HTTP {ping.status_code}")
                except Exception as exc:
                    print(f"  download ping failed for {url}: {exc}")
            else:
                print(f"  Unsplash budget exhausted ({self.used}/{self.budget} "
                      f"used), skipping download ping for {url}")
        r = self.session.get(url, timeout=60)
        r.raise_for_status()
        return r.content

    def report(self):
        """Print how many requests this fetcher spent and how many it skipped.

        Author: suinevere
        Dependencies: N/A
        Globals: N/A
        Params: N/A
        Returns: N/A
        """
        print(f"  Unsplash: {self.used}/{self.budget} requests used, "
              f"{self.skipped} skipped")


def _parse_dotenv(path):
    """Parse a .env file's KEY=value lines into a dict.

    Description: Deliberately minimal -- python-dotenv is not a listed
        dependency and a handful of lines does not justify one. A line with no
        "=", an empty key, or a file that cannot be read or decoded degrades
        to "found nothing" rather than raising, matching this module's
        everything-degrades rule.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: path -- the .env file; absent, unreadable, or undecodable yields {}
    Returns: dict of parsed KEY -> value pairs
    """
    path = Path(path)
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError):
        return {}

    out = {}
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        key = key.strip()
        value = value.strip()
        if not key:
            continue
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        out[key] = value
    return out


def load_dotenv_into_environ(path=None, env=None):
    """Fill unset variables in `env` from a .env file.

    Description: An already-set variable always wins, so a one-off
        `PIXABAY_API_KEY=... python fetch_art.py` still overrides the file.
        Never prints or otherwise surfaces a value -- a secret in a report is
        still a leak.
    Author: suinevere
    Dependencies: N/A
    Globals: DOTENV_PATH
    Params: path -- the .env file; defaults to DOTENV_PATH (tools/.env,
        beside this module rather than the current working directory);
        env -- the mapping to fill; defaults to os.environ
    Returns: N/A
    """
    if path is None:
        path = DOTENV_PATH
    if env is None:
        env = os.environ
    for key, value in _parse_dotenv(path).items():
        env.setdefault(key, value)


def _resolve_scene_filter(raw):
    """Validate a comma-separated --scene argument against the scene vocabulary.

    Description: Matches each name case-insensitively against
        scene_vocab.SCENE_INDEX; anything else is reported by the caller and
        dropped rather than raising, so one typo does not sink an otherwise
        valid run. Order is preserved and duplicates collapsed, but neither
        is load-bearing for the caller, which only filters a dict by
        membership.
    Author: suinevere
    Dependencies: scene_vocab
    Globals: N/A
    Params: raw -- the --scene argument's raw string, e.g. "FOREST,CAVE" or "cave"
    Returns: (valid, invalid) -- valid is a deduplicated list of canonical
        scene names; invalid is the list of names (as typed) that matched
        nothing in scene_vocab.SCENE_INDEX
    """
    valid = []
    invalid = []
    seen = set()
    for name in raw.split(","):
        name = name.strip()
        if not name:
            continue
        canonical = name.upper()
        if canonical in vocab.SCENE_INDEX:
            if canonical not in seen:
                valid.append(canonical)
                seen.add(canonical)
        else:
            invalid.append(name)
    return valid, invalid


def _scenes_for_game(repo, game):
    """The distinct scenes one game's blessed rooms actually use.

    Description: Reads tools/assets/scenes/<game>.json (the blessed
        object->scene verdicts gen_scene_tables.py also reads) and collects
        its distinct scene values. Lets --game narrow a fetch run to only
        the scenes a story actually needs, the same shopping list Task 15's
        brief builds by hand from this same file. A game with no blessed
        scenes file yet degrades to an empty list rather than raising.
    Author: suinevere
    Dependencies: json, pathlib
    Globals: N/A
    Params: repo -- repo root; game -- a story stem, e.g. "ZORK1"
    Returns: a sorted list of scene names, possibly empty
    """
    path = Path(repo) / "tools" / "assets" / "scenes" / f"{game}.json"
    if not path.exists():
        return []
    blessed = json.loads(path.read_text(encoding="utf-8"))
    return sorted(set(blessed.values()))


def reset_rejected(manifest):
    """Drop every manifest record whose status is METRIC_REJECTED, in place.

    Description: Undoes the fetcher's own dedup for metric-rejected images
        only. A CANDIDATE, ACCEPTED or REJECTED record survives untouched --
        only a metric rejection is worth re-scoring, since it is the only
        status a threshold change can flip. Once its record is gone, the
        image's Pixabay id is no longer in the manifest, so harvest()'s
        `if key in manifest: continue` no longer skips it and the next
        ordinary run re-downloads and re-scores it under the current
        thresholds.
    Author: suinevere
    Dependencies: art_status
    Globals: N/A
    Params: manifest -- dict keyed by stringified Pixabay id, mutated in place
    Returns: the number of records dropped
    """
    dropped = [key for key, record in manifest.items()
               if record.get("status") == art_status.METRIC_REJECTED]
    for key in dropped:
        del manifest[key]
    return len(dropped)


def main(argv):
    """Run a harvest against the scene vocabulary.

    Description: Defaults to 99 survivors per scene -- matching the disc's
        1..99 per-game index cap -- and no overall ceiling, so a default run
        can fetch up to 99 * len(scene_vocab.SCENES) candidates across every
        scene. Reads PIXABAY_API_KEY and UNSPLASH_ID from tools/.env when
        not already exported, so an explicit export still wins over the
        file.

        `--source pixabay|unsplash|both` picks which fetcher(s) run;
        default "pixabay" so every existing invocation's behaviour is
        unchanged. `--budget N` caps the Unsplash fetcher's own request
        count (search calls plus download pings) at N; default 50, the
        owner's stated hourly quota. Both flags are pulled out of argv
        before the positional per_scene_budget/total_budget are read, so
        their position in argv does not matter.

        `--reset-rejected` bypasses all of that: it drops every
        METRIC_REJECTED manifest record, saves, and returns without
        fetching or requiring an API key, so a threshold change can be
        followed by an ordinary run that re-downloads and re-scores what it
        just freed.

        `--scene NAME[,NAME...]` restricts the plan handed to harvest() to
        those scenes only, checked case-insensitively against
        scene_vocab.SCENE_INDEX before any fetcher is built. An unknown name
        is reported and dropped, not fatal; if every named scene is unknown,
        the run prints the valid names and returns 0 without building a
        fetcher or fetching anything -- the everything-degrades rule.
        Absent, every scene in the vocabulary is planned.

        `--game STEM` is required, and names both the scenes to fetch (read
        from tools/assets/scenes/<STEM>.json, the story's own blessed tags)
        and the genre those scenes are searched in -- a sci-fi corridor and
        a detective's corridor are not the same photograph, and
        art_nouns.GAME_GENRE is where that judgement lives. Combined with
        `--scene`, the final selection is their intersection; a game with no
        blessed scenes file yet reports so and returns 0.
    Author: suinevere
    Dependencies: art_queries, art_nouns, art_status, scene_vocab
    Globals: DOTENV_PATH
    Params: argv -- [] | [per_scene_budget] | [per_scene_budget, total_budget],
        with a required "--game STEM" and optional "--reset-rejected",
        "--source X", "--budget N" and "--scene NAME[,NAME...]" anywhere in
        argv
    Returns: 0 always; failures are reported, not raised
    """
    repo = Path(__file__).resolve().parents[1]
    manifest_path = repo / "tools" / "assets" / "art_manifest.json"

    argv = list(argv)
    if "--reset-rejected" in argv:
        manifest = load_manifest(manifest_path)
        dropped = reset_rejected(manifest)
        save_manifest(manifest_path, manifest)
        print(f"  dropped {dropped} metric-rejected record(s); "
              f"{len(manifest)} images still known")
        return 0

    source = "pixabay"
    if "--source" in argv:
        i = argv.index("--source")
        source = argv[i + 1]
        del argv[i:i + 2]

    budget = None
    if "--budget" in argv:
        i = argv.index("--budget")
        budget = int(argv[i + 1])
        del argv[i:i + 2]

    selected_scenes = None
    if "--scene" in argv:
        i = argv.index("--scene")
        scene_arg = argv[i + 1]
        del argv[i:i + 2]
        selected_scenes, unknown_scenes = _resolve_scene_filter(scene_arg)
        for bad in unknown_scenes:
            print(f"  {bad}: not a known scene, ignoring")
        if not selected_scenes:
            print("  no valid --scene names given; choose one or more of: "
                  + ", ".join(vocab.SCENES))
            return 0

    game = None
    if "--game" in argv:
        i = argv.index("--game")
        game = argv[i + 1]
        del argv[i:i + 2]
    if game is None:
        print("  --game STEM is required: art is fetched, curated and shipped "
              "per game, so a run without one has nowhere to put its pictures.")
        return 0
    game_scenes = set(_scenes_for_game(repo, game))
    if not game_scenes:
        print(f"  {game}: no blessed scenes file, nothing to fetch")
        return 0
    selected_scenes = (sorted(game_scenes & set(selected_scenes))
                        if selected_scenes is not None
                        else sorted(game_scenes))
    if not selected_scenes:
        print(f"  {game}'s scenes and --scene do not overlap; nothing to fetch")
        return 0

    load_dotenv_into_environ()

    fetchers = []
    if source in ("pixabay", "both"):
        key = os.environ.get("PIXABAY_API_KEY", "")
        if not key:
            print("  PIXABAY_API_KEY is not set. Get a free key at "
                  "https://pixabay.com/api/docs/ and export it, then re-run.")
            if source == "pixabay":
                return 0
        else:
            fetchers.append(PixabayFetcher(key))
    if source in ("unsplash", "both"):
        access_key = os.environ.get("UNSPLASH_ID", "")
        if not access_key:
            print("  UNSPLASH_ID is not set. Get a free Access Key at "
                  "https://unsplash.com/developers and export it, then re-run.")
            if source == "unsplash":
                return 0
        else:
            fetchers.append(UnsplashFetcher(
                access_key, budget=budget if budget is not None else 50))

    if not fetchers:
        return 0

    per_scene_budget = int(argv[0]) if argv else 99
    total_budget = int(argv[1]) if len(argv) > 1 else None
    genre = art_nouns.genre_for_game(game)
    terms = art_terms.load(repo)
    for problem in art_terms.validate(terms):
        print(f"  search_terms.json: {problem}")
    filters = art_terms.game_terms(terms, game)
    nouns = art_nouns.nouns_for_game(game, selected_scenes, terms)
    if not nouns:
        print(f"  {game}: none of its scenes have search phrases")
        return 0
    plan = art_queries.build(nouns, game)
    print(f"  {game} as {genre}: {len(nouns)} scene(s), "
          f"{sum(len(v) for v in plan.values())} queries"
          + (f", filtered by {' '.join(filters)}" if filters else ""))

    manifest = load_manifest(manifest_path)
    kept = []
    try:
        for fetcher in fetchers:
            kept.extend(harvest(plan, fetcher,
                                repo / "tools" / "assets" / "candidates",
                                manifest, per_scene_budget, total_budget))
            report = getattr(fetcher, "report", None)
            if report is not None:
                report()
    finally:
        save_manifest(manifest_path, manifest)
    by_scene = Counter(c.query.scene for c in kept)
    for scene in sorted(by_scene):
        print(f"  {scene}: {by_scene[scene]}")
    print(f"  {len(kept)} candidates written; {len(manifest)} images known")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
