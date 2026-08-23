"""Art fetch vocabulary must come from scene_vocab, never a separately-kept copy."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


def test_nouns_come_from_the_scene_vocabulary():
    import art_nouns
    import scene_vocab
    for scene in scene_vocab.SCENES:
        assert art_nouns.nouns_for_scene(scene) == scene_vocab.FETCH_NOUNS[scene]


def test_a_genre_without_an_override_falls_through_to_the_base_vocabulary():
    """MODERN overrides nothing, because the base vocabulary is already
    contemporary. Falling through is the designed answer, not a gap."""
    import art_nouns
    import scene_vocab
    for scene in scene_vocab.SCENES:
        assert (art_nouns.nouns_for_scene(scene, "MODERN")
                == scene_vocab.FETCH_NOUNS[scene])


def test_the_same_scene_searches_differently_in_two_genres():
    """The whole point: a corridor in Planetfall is a spaceship corridor and
    in Deadline a wood-panelled manor hallway, and one search cannot find
    both."""
    import art_nouns
    scifi = set(art_nouns.nouns_for_scene("CORRIDOR", "SCIFI"))
    noir = set(art_nouns.nouns_for_scene("CORRIDOR", "DETECTIVE"))
    assert scifi and noir
    assert not scifi & noir


def test_every_genre_override_names_a_real_scene_and_a_real_genre():
    """GENRE_NOUNS is hand-authored, not derived; guard it against vocabulary
    drift the way MOOD_TO_SCENES is guarded."""
    import art_nouns
    import scene_vocab
    for genre, scenes in art_nouns.GENRE_NOUNS.items():
        assert genre in art_nouns.GENRES, genre
        for scene, phrases in scenes.items():
            assert scene in scene_vocab.SCENE_INDEX, (genre, scene)
            assert phrases, (genre, scene)


def test_every_game_with_blessed_tags_has_a_genre():
    """A story that reaches the art server without a genre searches as MODERN
    and quietly gets neutral pictures, which is the failure this catches."""
    import art_nouns
    import gen_scene_tables
    for stem, _release, _serial in gen_scene_tables.GAMES:
        assert stem in art_nouns.GAME_GENRE, stem


def test_an_unlisted_game_degrades_to_the_neutral_genre():
    import art_nouns
    assert art_nouns.genre_for_game("NOTAGAME") == art_nouns.DEFAULT_GENRE


def test_nouns_for_game_skips_a_scene_with_no_phrases():
    """art_queries.validate refuses a scene it cannot search, so an empty one
    must never reach it."""
    import art_nouns
    got = art_nouns.nouns_for_game("ZORK1", ["FOREST", "NOT_A_SCENE"])
    assert set(got) == {"FOREST"}


def test_unknown_scene_has_no_nouns():
    import art_nouns
    assert art_nouns.nouns_for_scene("NOT_A_SCENE") == ()


def test_no_reference_to_the_deleted_classifier():
    src = Path(__file__).resolve().parents[2] / "tools" / "art_nouns.py"
    assert "room_class_data" not in src.read_text(encoding="utf-8")
