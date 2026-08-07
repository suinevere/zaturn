"""Gate the disc-side contract the mood folders rest on.

This is a source and layout test, not a hardware test -- the runtime half is
verified on hardware per the plan's Task 4 Step 7, which is the only way to
observe CD-DA surviving a directory change.
"""
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TITLE = (REPO / "saturn" / "src" / "video" / "title.cxx").read_text()
MAIN = (REPO / "saturn" / "src" / "main.cxx").read_text()


def test_boot_scan_is_gone():
    assert "display_scan_images" not in TITLE
    assert "display_scan_images" not in MAIN


def test_every_mood_entry_returns_to_root():
    """cd_enter_mood must be paired with cd_enter_root on every path that uses it."""
    enters = len(re.findall(r"\bcd_enter_mood\s*\(", TITLE))
    roots = len(re.findall(r"\bcd_enter_root\s*\(", TITLE))
    assert enters > 0, "cd_enter_mood is not used"
    assert roots >= enters, f"{enters} mood entries but only {roots} returns to root"


def test_no_flat_tga_names_remain():
    assert not re.search(r'"[A-Z]{4,8}\d\.TGA"', TITLE), \
        "a flat, pre-folder TGA filename is still hard-coded"
