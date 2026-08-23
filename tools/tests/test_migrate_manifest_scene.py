"""Additive scene-tagging: gains a scene where the noun maps, touches nothing else, and is idempotent."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import migrate_manifest_scene as m
import scene_vocab as vocab


def record(noun, **extra):
    rec = {"id": "1", "page_url": "https://pixabay.com/photos/1/",
           "image_url": "", "phrase": "x", "mood": "HORROR", "donor": "HOUSE",
           "noun": noun, "licence": "Pixabay Content License",
           "fetched": "2026-08-06", "luminance": 70.0, "busyness": 4.0,
           "banding": 2.0, "verdict": "pass", "phash": "0" * 16,
           "status": "candidate"}
    rec.update(extra)
    return rec


def test_build_noun_scene_map_covers_the_six_added_nouns():
    mapping = m.build_noun_scene_map()
    assert mapping["clearing"] == "FOREST"
    assert mapping["boat"] == "SHIP_EXT"
    assert mapping["building"] == "VILLAGE"
    assert mapping["ballroom"] == "PARLOR"
    assert mapping["vault"] == "CRYPT"
    assert mapping["alcove"] == "CAVE"


def test_migrate_adds_scene_only():
    manifest = {"1": record("cottage")}
    before = dict(manifest["1"])
    gained, already, unmapped = m.migrate(manifest)
    assert (gained, already, unmapped) == (1, 0, 0)
    assert manifest["1"]["scene"] == "HOUSE_EXT"
    for key, value in before.items():
        assert manifest["1"][key] == value


def test_migrate_never_overwrites_an_existing_scene():
    manifest = {"1": record("cottage", scene="LIBRARY")}
    gained, already, unmapped = m.migrate(manifest)
    assert (gained, already, unmapped) == (0, 1, 0)
    assert manifest["1"]["scene"] == "LIBRARY"


def test_migrate_leaves_unmappable_nouns_untouched():
    manifest = {"1": record("chamber")}
    gained, already, unmapped = m.migrate(manifest)
    assert (gained, already, unmapped) == (0, 0, 1)
    assert "scene" not in manifest["1"]


def test_migrate_is_idempotent():
    manifest = {"1": record("cottage"), "2": record("chamber")}
    m.migrate(manifest)
    snapshot = {k: dict(v) for k, v in manifest.items()}
    gained, already, unmapped = m.migrate(manifest)
    assert (gained, already, unmapped) == (0, 1, 1)
    assert manifest == snapshot


def test_every_added_noun_scene_is_a_real_scene():
    mapping = m.build_noun_scene_map()
    for scene in mapping.values():
        assert scene in vocab.SCENE_INDEX
