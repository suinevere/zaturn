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


def test_snapshot_is_valid_json_with_records():
    snap = REPO / "tools" / "assets" / "art_manifest.snapshot.json"
    data = json.loads(snap.read_text(encoding="utf-8"))
    assert isinstance(data, dict)
    assert len(data) > 0
    sample = next(iter(data.values()))
    assert "status" in sample
