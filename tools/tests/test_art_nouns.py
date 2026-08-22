"""Art fetch vocabulary must come from scene_vocab, never a separately-kept copy."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


def test_nouns_come_from_the_scene_vocabulary():
    import art_nouns
    import scene_vocab
    for scene in scene_vocab.SCENES:
        assert art_nouns.nouns_for_scene(scene) == scene_vocab.FETCH_NOUNS[scene]


def test_unknown_scene_has_no_nouns():
    import art_nouns
    assert art_nouns.nouns_for_scene("NOT_A_SCENE") == ()


def test_no_reference_to_the_deleted_classifier():
    src = Path(__file__).resolve().parents[2] / "tools" / "art_nouns.py"
    assert "room_class_data" not in src.read_text(encoding="utf-8")
