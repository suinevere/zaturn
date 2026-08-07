"""Fetch, crop and score background candidates from Pixabay.

Description: Walks the query plan, asks Pixabay for each phrase, crops every hit
    to 320x224, scores it, and writes the survivors under
    <out>/<MOOD>/<DONOR>/<noun>/. The path is the query's provenance: any picture
    on the disc can be traced back to the adjective and noun that found it.

    Every failure degrades. No key, no network, a dead query or an image that will
    not decode all print and continue, because a run that aborts on hit 400 of 1200
    wastes the 399 before it.

    Nothing here decides what ships. Surviving the metric gate only earns a place
    on a contact sheet; a human accepts or rejects from there.
Author: suinevere
Dependencies: requests, PIL, art_metrics, art_queries, art_nouns, art_status
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
import art_status

ENDPOINT = "https://pixabay.com/api/"
LICENCE = "Pixabay Content License"

Candidate = namedtuple("Candidate", "query hit scores verdict phash path")


def load_manifest(path):
    """Read the provenance manifest, or an empty one if it does not exist yet.

    Description: A missing manifest means "first run", not an error.
    Author: suinevere
    Dependencies: json
    Globals: N/A
    Params: path -- the manifest file
    Returns: dict keyed by stringified Pixabay id
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


def harvest(plan, fetcher, out_dir, manifest, per_mood_budget, total_budget=None):
    """Fetch and gate every query in the plan, stopping each mood at its own budget.

    Description: The budget is per mood, not global -- moods sort alphabetically
        and a shared counter let an early one (DESERT) starve every mood after
        it. `total_budget` is an optional ceiling across the whole run, for a
        small first calibration batch; the per-mood cap is what keeps every
        mood represented and stays in force regardless of it.

        A Pixabay id already in the manifest is skipped without a download,
        which is what makes a re-run cheap and a partial run resumable.
    Author: suinevere
    Dependencies: PIL, art_metrics, art_status
    Globals: LICENCE
    Params: plan -- mood -> [Query]; fetcher -- an object with .search/.download;
        out_dir -- where surviving PNGs go; manifest -- mutated in place;
        per_mood_budget -- stop each mood after this many survivors;
        total_budget -- optional ceiling across all moods combined; None means
            only the per-mood caps apply
    Returns: the list of surviving Candidate
    """
    out_dir = Path(out_dir)
    kept = []

    for mood in sorted(plan):
        mood_kept = 0
        for query in plan[mood]:
            if mood_kept >= per_mood_budget:
                break
            if total_budget is not None and len(kept) >= total_budget:
                return kept
            try:
                hits = fetcher.search(query.phrase, per_page=12)
            except Exception as exc:
                print(f"  {query.phrase}: search failed ({exc})")
                continue

            for hit in hits:
                if mood_kept >= per_mood_budget:
                    break
                if total_budget is not None and len(kept) >= total_budget:
                    return kept
                key = str(hit["id"])
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
                    "mood": query.mood, "donor": query.donor, "noun": query.noun,
                    "licence": LICENCE, "fetched": date.today().isoformat(),
                    "luminance": round(scores.luminance, 2),
                    "busyness": round(scores.busyness, 2),
                    "banding": round(scores.banding, 2),
                    "verdict": call, "phash": "",
                    "status": (art_status.METRIC_REJECTED if call != "pass"
                               else art_status.CANDIDATE),
                }
                manifest[key] = record
                if call != "pass":
                    continue

                dest = out_dir / query.mood / query.donor / query.noun
                dest.mkdir(parents=True, exist_ok=True)
                path = dest / f"{hit['id']}.png"
                art_metrics.crop(im).save(path, "PNG")
                record["phash"] = _phash(path)
                kept.append(Candidate(query, hit, scores, call, record["phash"], path))
                mood_kept += 1

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

    def __init__(self, key, session=None, pause=0.7):
        import requests
        self.key = key
        self.session = session or requests.Session()
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


def main(argv):
    """Run a harvest against the shipped vocabulary.

    Description: Defaults to 99 survivors per mood -- matching both the
        vocabulary's own "target" and make_tga.convert_tree's per-mood cap --
        and no overall ceiling, so a default run can fetch up to 99 * len(MOODS)
        candidates across the twelve moods.
    Author: suinevere
    Dependencies: art_queries, art_nouns
    Globals: N/A
    Params: argv -- [] | [per_mood_budget] | [per_mood_budget, total_budget]
    Returns: 0 always; failures are reported, not raised
    """
    repo = Path(__file__).resolve().parents[1]
    key = os.environ.get("PIXABAY_API_KEY", "")
    if not key:
        print("  PIXABAY_API_KEY is not set. Get a free key at "
              "https://pixabay.com/api/docs/ and export it, then re-run.")
        return 0

    per_mood_budget = int(argv[0]) if argv else 99
    total_budget = int(argv[1]) if len(argv) > 1 else None
    vocab = art_queries.load(repo / "tools" / "assets" / "art_queries.json")
    nouns = art_nouns.nouns_by_mood(
        (repo / "saturn" / "src" / "classify" / "room_class_data.c").read_text())
    plan = art_queries.build(vocab, nouns)

    manifest_path = repo / "tools" / "assets" / "art_manifest.json"
    manifest = load_manifest(manifest_path)
    try:
        kept = harvest(plan, PixabayFetcher(key),
                       repo / "tools" / "assets" / "candidates",
                       manifest, per_mood_budget, total_budget)
    finally:
        save_manifest(manifest_path, manifest)
    by_mood = Counter(c.query.mood for c in kept)
    for mood in sorted(by_mood):
        print(f"  {mood}: {by_mood[mood]}")
    print(f"  {len(kept)} candidates written; {len(manifest)} images known")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
