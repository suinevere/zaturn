"""Gate that the working manifest is untracked and the snapshot is not."""
import json
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def _tracked(rel):
    out = subprocess.run(["git", "ls-files", "--error-unmatch", rel],
                         cwd=REPO, capture_output=True, text=True)
    return out.returncode == 0


def test_working_manifest_is_not_tracked():
    assert not _tracked("tools/assets/art_manifest.json")


def test_snapshot_is_tracked():
    assert _tracked("tools/assets/art_manifest.snapshot.json")


def test_snapshot_records_carry_the_shipping_shape():
    """Empty is legal -- curation restarts from zero whenever the art model
    changes -- but a record that is there must name the game it belongs to,
    since the whole tree is keyed on that now."""
    snap = REPO / "tools" / "assets" / "art_manifest.snapshot.json"
    data = json.loads(snap.read_text(encoding="utf-8"))
    assert isinstance(data, dict)
    for key, rec in data.items():
        assert "status" in rec, key
        assert "game" in rec and "scene" in rec, key
        assert key == "{}:{}".format(rec["game"], rec["id"]), key
