#!/usr/bin/env python3
"""/*----------------------
 | extract_bg.py
 | Description: Pulls Zork I's twelve BG archives -- the eleven room-background
 |     archives (B*.CGL) and the item-picture container (OITEM.CZ) -- out of
 |     the original Japanese Saturn disc's data track and stages them for
 |     injection into /BG by tools/assets/games.bat.
 |
 |     Every extracted archive is checked against BG_MANIFEST by size and
 |     SHA-256, and the run refuses on any mismatch. That check is
 |     load-bearing rather than defensive: game_presentation.inc records a byte
 |     offset and length per frame inside each archive, measured against these
 |     exact bytes. A different disc revision would not fail to open -- it
 |     would decompress from the wrong offset and show garbage, or hang the
 |     LZSS loop, with nothing upstream to say why.
 |
 |     The data track is the one music.bat already downloads and discards (its
 |     "Skipping Track 1" line), so this adds no new source of bytes and
 |     nothing copyrighted to the repo.
 | Author: suinevere
 | Dependencies: hashlib, pathlib, sys, argparse, saturn_translate.iso
 | Globals: ROOT, BG_MANIFEST
 | Run: python tools/extract_bg.py <track01.bin | disc-dir> -o tools/assets/BG
 ----------------------*/"""
import argparse
import hashlib
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from saturn_translate.iso import SaturnImage

BG_MANIFEST = {
    "BBAR.CGL": (73732, "8d6c38e430fd5595a90681600fbb71dac7a466bdc590bd57f754af29c455dcb2"),
    "BCEL.CGL": (418264, "57767a197aa8e481d04cf759ccfe604c7903b3d8a4f46041f00b322d887b203e"),
    "BDAM.CGL": (252756, "6817b0128aa70b232de8f223df15305fdd0f755936f5dab31de0436526a354b9"),
    "BDED.CGL": (57076, "708e4f11c6c69784e51ecdd05a4842d59409f90592d3b8cc3321df45851f31f8"),
    "BHUS.CGL": (128928, "27c9b4cf1b0ef5be661dd9228840317e80356dc24366b0ffbf186a3fe790b16c"),
    "BMAZ.CGL": (66272, "6537c7e48b77a7bcf339a07d77c356f8777b4f7e1cd47de9338abcc6a6f484e1"),
    "BMIN.CGL": (235648, "dcb00e52a46fb40395a64b2335bb3e9d4daf5c50b301b00d0435c56e623bf58f"),
    "BMIR.CGL": (116160, "7829ecbceb712e508f7dde63348937990d4dccd83c6795159033ff42c5ae9cb4"),
    "BRIV.CGL": (367500, "f3b82d161763c0b228648950e58ed21adb1f86fcdfec016dbf22384e30d41efc"),
    "BTMP.CGL": (158404, "6a93c873e8ca7387805de4d1309b2388372d29e280bd17fa1b77bf4460dcb67c"),
    "BWOD.CGL": (126700, "8d6d95b32ef0b14cce2f8620965a1de4e2317c7380fecc3c4390277252cda163"),
    "OITEM.CZ": (40840, "04344f3bbc6404ab6163e0d2df16614e4fc67d53855a1472baab3cfe9f54a2e0"),
}
"""BG_MANIFEST

Description: name -> (size, sha256) for the twelve BG archives, taken from the
    Japanese Saturn release of Zork I. The frame offsets in
    saturn/src/scene/game_presentation.inc are only meaningful against these
    exact bytes, and saturn/src/scene/oitem_records.inc records OITEM.CZ's own
    per-picture offsets against them just as hard, so this is the identity
    check on the whole presentation feature, not just on the download.
Author: suinevere
"""


def find_data_track(path):
    """/*----------------------
     | find_data_track
     | Description: Resolves the caller's argument to one data-track image. A
     |     file is taken as-is; a directory is searched for a track-01 .bin, the
     |     name music.bat's own skip rule matches, so both halves agree on which
     |     of a 32-track dump is the data track.
     | Author: suinevere
     | Dependencies: pathlib, re
     | Globals: N/A
     | Params: path -- a file or a directory holding an extracted disc dump
     | Returns: a pathlib.Path to the data track
     ----------------------*/"""
    import re

    p = pathlib.Path(path)
    if p.is_file():
        return p
    if not p.is_dir():
        raise SystemExit(f"extract_bg: no such file or directory: {p}")
    hits = [f for f in sorted(p.rglob("*.bin"))
            if re.search(r"Track\s*0?1(?:[^0-9]|$)", f.name, re.IGNORECASE)]
    if not hits:
        raise SystemExit(f"extract_bg: no track-01 .bin under {p}")
    return hits[0]


def extract(track, out_dir):
    """/*----------------------
     | extract
     | Description: Writes the twelve archives into out_dir, verifying each
     |     against BG_MANIFEST before it lands. Verification happens on the
     |     bytes in memory rather than on the file afterwards, so a disc that
     |     fails the check leaves no partial staging directory for the injection
     |     step to find and ship.
     | Author: suinevere
     | Dependencies: hashlib, pathlib, saturn_translate.iso
     | Globals: BG_MANIFEST
     | Params: track -- path to the data track; out_dir -- staging directory
     | Returns: the number of archives written
     ----------------------*/"""
    img = SaturnImage.from_file(str(track))
    blobs = {}
    for name, (size, digest) in BG_MANIFEST.items():
        try:
            data = img.extract("/" + name)
        except Exception as exc:
            raise SystemExit(f"extract_bg: {name} not on {track}: {exc}")
        if len(data) != size:
            raise SystemExit(
                f"extract_bg: {name} is {len(data)} bytes, expected {size} -- "
                "this is not the disc game_presentation.inc was measured against")
        got = hashlib.sha256(data).hexdigest()
        if got != digest:
            raise SystemExit(
                f"extract_bg: {name} sha256 {got}, expected {digest} -- "
                "this is not the disc game_presentation.inc was measured against")
        blobs[name] = data

    out_dir = pathlib.Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, data in blobs.items():
        (out_dir / name).write_bytes(data)
        print(f" -> {name} ({len(data)} bytes)")
    return len(blobs)


def staged(out_dir):
    """/*----------------------
     | staged
     | Description: Whether out_dir already holds all twelve archives at the
     |     right size. Lets a repeated build skip a 600 MB download without
     |     re-hashing, while still refusing a directory that is half-written.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: BG_MANIFEST
     | Params: out_dir -- staging directory
     | Returns: True when the staging directory is complete
     ----------------------*/"""
    out_dir = pathlib.Path(out_dir)
    for name, (size, _) in BG_MANIFEST.items():
        f = out_dir / name
        if not f.is_file() or f.stat().st_size != size:
            return False
    return True


def main(argv):
    """/*----------------------
     | main
     | Description: Command line entry. --check answers "is the staging
     |     directory already complete" through the exit code alone, so the shell
     |     halves of the pipeline can gate the download on it without parsing
     |     output.
     | Author: suinevere
     | Dependencies: argparse
     | Globals: ROOT
     | Params: argv -- argument list without the program name
     | Returns: 0 on success, 1 when --check finds the staging incomplete
     ----------------------*/"""
    ap = argparse.ArgumentParser(description="Stage Zork I's BG archives for /BG injection")
    ap.add_argument("track", nargs="?", help="data-track .bin, or a directory holding one")
    ap.add_argument("-o", "--out", default=str(ROOT / "tools" / "assets" / "BG"),
                    help="staging directory (default tools/assets/BG)")
    ap.add_argument("--check", action="store_true",
                    help="exit 0 if the staging directory is already complete, 1 otherwise")
    args = ap.parse_args(argv)

    if args.check:
        return 0 if staged(args.out) else 1
    if not args.track:
        ap.error("a data track is required unless --check is given")

    track = find_data_track(args.track)
    print(f"Extracting room-background archives from {track.name}")
    n = extract(track, args.out)
    print(f"Staged {n} archives -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
