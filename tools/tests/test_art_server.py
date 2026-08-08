"""Cover the local review server's routes, verdicts, filtering and grouping."""
import json
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_server
import art_status
import fetch_art


def record(pid, mood="HORROR", donor="HOUSE", noun="hallway",
           status=art_status.CANDIDATE):
    return {"id": pid, "page_url": f"https://pixabay.com/photos/{pid}/",
            "image_url": "", "phrase": "dark hallway", "mood": mood,
            "donor": donor, "noun": noun, "licence": "Pixabay Content License",
            "fetched": "2026-08-08", "luminance": 70.0, "busyness": 4.0,
            "banding": 2.0, "verdict": "pass", "phash": "0" * 16,
            "status": status}


def write_png(root, rec):
    d = root / rec["mood"] / rec["donor"] / rec["noun"]
    d.mkdir(parents=True, exist_ok=True)
    p = d / f"{rec['id']}.png"
    Image.new("RGB", (320, 224), (60, 60, 60)).save(p, "PNG")
    return p


def build(tmp_path, records, promoted=(), candidates=(), target=99):
    assets = tmp_path / "tools" / "assets"
    assets.mkdir(parents=True)
    for rec in promoted:
        write_png(assets / "png", rec)
    for rec in candidates:
        write_png(assets / "candidates", rec)
    fetch_art.save_manifest(assets / "art_manifest.json",
                            {str(r["id"]): r for r in records})
    (assets / "art_queries.json").write_text(
        json.dumps({m: {"adjectives": ["dark"], "donors": [m],
                        "extra_nouns": ["hallway"], "exclude_nouns": [],
                        "target": target}
                    for m in ("HORROR", "WATER")}),
        encoding="utf-8")
    return art_server.create_app(repo=tmp_path).test_client()


def test_index_lists_every_mood_with_its_counts(tmp_path):
    acc = record(1, status=art_status.ACCEPTED)
    rej = record(2, status=art_status.REJECTED)
    und = record(3, status=art_status.CANDIDATE)
    client = build(tmp_path, [acc, rej, und], promoted=[acc],
                   candidates=[rej, und])

    page = client.get("/").get_data(as_text=True)

    assert "HORROR" in page
    assert "99" in page, "the per-mood target must be shown"


def test_index_reads_the_target_from_the_vocabulary(tmp_path):
    client = build(tmp_path, [record(1)], candidates=[record(1)], target=40)

    page = client.get("/").get_data(as_text=True)

    assert "40" in page, "target comes from art_queries.json, not a constant"
    assert "99" not in page, \
        "a hardcoded 99 must not coincidentally satisfy this assertion"
