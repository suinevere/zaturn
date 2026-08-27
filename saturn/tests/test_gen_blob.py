#!/usr/bin/env python3
"""Host test that tools/gen_blob.py round-trips bytes exactly: every byte of the
input appears in the generated array, in order, and the emitted length matches.
Parses the generated C rather than compiling it, so the test needs no toolchain."""
import pathlib, re, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
GEN = ROOT / "tools" / "gen_blob.py"


def test_round_trip():
    payload = bytes(range(256)) * 3 + b"\x00\xff\x7f"
    with tempfile.TemporaryDirectory() as td:
        src = pathlib.Path(td) / "payload.bin"
        src.write_bytes(payload)
        out = pathlib.Path(td) / "blob.c"
        subprocess.run([sys.executable, str(GEN), str(out), f"story={src}"],
                       check=True)
        text = out.read_text(encoding="utf-8")

    body = re.search(r"netbin_story_bytes\[\d+\] = \{(.*?)\};", text, re.S)
    assert body, "no netbin_story_bytes array emitted"
    got = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", body.group(1)))
    assert got == payload, f"round-trip mismatch: {len(got)} vs {len(payload)} bytes"

    declared = re.search(r"netbin_story_len = (\d+)u;", text)
    assert declared, "no netbin_story_len emitted"
    assert int(declared.group(1)) == len(payload)
    assert "#ifdef NETBIN" in text, "arrays must be NETBIN-guarded"

    # The accessors are generated, not hand-appended: this file is regenerated
    # whenever the payload changes, and anything added by hand would be lost.
    assert "netbin_story_data(void) { return netbin_story_bytes; }" in text
    assert "netbin_story_size(void) { return netbin_story_len; }" in text
    assert "netbin_story_data(void) { return 0; }" in text, "no non-NETBIN stub"
    assert "netbin_story_size(void) { return 0u; }" in text, "no non-NETBIN stub"


if __name__ == "__main__":
    test_round_trip()
    print("ok")
