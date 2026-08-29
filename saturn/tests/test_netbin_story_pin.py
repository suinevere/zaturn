#!/usr/bin/env python3
"""Host test that src/input/netbin_story.c is exactly the disc's ZORK1.Z3 trimmed
by tools/trim_z3_vocab.py.

netbin_story.c is generated and committed, so nothing in the build regenerates
it and nothing else notices when the disc story and the embedded copy drift
apart. This pins both halves at once: the bytes must match the disc file, and
they must match it *trimmed*, so an accidental regeneration from the untrimmed
story puts 63 KB back into the netbin and fails here rather than at the loader.

Parses the generated C rather than compiling it, so the test needs no toolchain.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from trim_z3_vocab import trim  # noqa: E402

BLOB = ROOT / "saturn" / "src" / "input" / "netbin_story.c"
STORY = ROOT / "saturn" / "cd" / "data" / "Z3" / "ZORK1.Z3"


def embedded_bytes():
    text = BLOB.read_text(encoding="utf-8")
    body = re.search(r"netbin_story_bytes\[(\d+)\] = \{(.*?)\};", text, re.S)
    assert body, "no netbin_story_bytes array in netbin_story.c"
    data = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", body.group(2)))
    assert len(data) == int(body.group(1)), "array length disagrees with its bound"
    declared = re.search(r"netbin_story_len = (\d+)u;", text)
    assert declared, "no netbin_story_len in netbin_story.c"
    assert int(declared.group(1)) == len(data), "netbin_story_len disagrees with the array"
    return data


def test_blob_is_the_trimmed_disc_story():
    assert STORY.exists(), f"{STORY} missing -- the disc story is the source of truth"
    want = trim(STORY.read_bytes())
    got = embedded_bytes()
    assert got == want, (
        f"netbin_story.c holds {len(got)} bytes, expected {len(want)}. "
        f"Regenerate: python3 tools/trim_z3_vocab.py saturn/cd/data/Z3/ZORK1.Z3 /tmp/vocab.z3 "
        f"&& python3 tools/gen_blob.py saturn/src/input/netbin_story.c story=/tmp/vocab.z3")


def test_trim_actually_dropped_high_memory():
    full = STORY.read_bytes()
    assert len(trim(full)) < len(full), "trim kept the whole story -- the cut is not happening"


if __name__ == "__main__":
    test_blob_is_the_trimmed_disc_story()
    test_trim_actually_dropped_high_memory()
    print("ok")
