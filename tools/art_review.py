"""Build per-mood contact sheets and promote what the reviewer accepted.

Description: The metric gate removes what is provably unusable; everything left
    is a judgement call, and one sheet per mood is the cheapest way to make a few
    hundred of them. Each thumbnail carries the phrase that found it, its three
    scores, and a link to its Pixabay page, so a doubtful picture can be checked
    at full size without leaving the sheet.

    Verdicts live in their own file rather than in the manifest, so a review
    session can be interrupted and resumed without a half-written manifest.

    HAMMING_MAX = 6 is out of the 64 bits a phash carries: under 10% of bits
    differing is the conventional near-duplicate threshold for perceptual
    hashes, tight enough that unrelated photos essentially never collide.
Author: suinevere
Dependencies: base64, html, json, pathlib
Globals: HAMMING_MAX
"""
import base64
import html
import json
from pathlib import Path

import art_status

HAMMING_MAX = 6


def _thumb_uri(path):
    """Embed a candidate PNG as a data: URI.

    Description: Keeps the sheet a single self-contained file, with no
        reference back to the candidates directory it was built from.
    Author: suinevere
    Dependencies: base64
    Globals: N/A
    Params: path -- the candidate PNG
    Returns: a data: URI string, or "" when the file is missing
    """
    path = Path(path)
    if not path.exists():
        return ""
    return "data:image/png;base64," + base64.b64encode(path.read_bytes()).decode()


def sheet(mood, records, candidates_dir):
    """Render one mood's candidates as a self-contained HTML review page.

    Description: Renders with zero candidates rather than refusing to -- the
        likely state of most moods on a first, small calibration run.
    Author: suinevere
    Dependencies: html
    Globals: N/A
    Params: mood -- the mood folder name; records -- the manifest dict;
        candidates_dir -- where fetch_art wrote the PNGs
    Returns: the HTML as a string
    """
    mine = [r for r in records.values()
            if r["mood"] == mood and r["status"] == art_status.CANDIDATE]
    mine.sort(key=lambda r: (r["donor"], r["noun"], r["id"]))

    cells = []
    for r in mine:
        src = _thumb_uri(Path(candidates_dir) / r["mood"] / r["donor"]
                         / r["noun"] / f"{r['id']}.png")
        cells.append(
            f'<figure data-id="{r["id"]}">'
            f'<img src="{src}" width="320" height="224" alt="">'
            f'<figcaption>{html.escape(r["phrase"])}<br>'
            f'lum {r["luminance"]} &middot; busy {r["busyness"]} '
            f'&middot; band {r["banding"]}<br>'
            f'<a href="{html.escape(r["page_url"], quote=True)}" '
            f'target="_blank">{r["id"]}</a><br>'
            f'<label><input type="checkbox" checked> keep</label>'
            f'</figcaption></figure>'
        )

    return (
        "<!doctype html><html><head><meta charset='utf-8'>"
        f"<title>{mood} &mdash; {len(mine)} candidates</title>"
        "<style>body{background:#111;color:#ddd;font:13px sans-serif}"
        "figure{display:inline-block;margin:8px;text-align:center}"
        "figcaption{max-width:320px}a{color:#8cf}</style></head><body>"
        f"<h1>{mood} &mdash; {len(mine)} candidates</h1>"
        + "".join(cells) +
        "<p><button onclick=\"save()\">Download verdicts.json</button></p>"
        "<script>function save(){var o={};"
        "document.querySelectorAll('figure').forEach(function(f){"
        "o[f.dataset.id]=f.querySelector('input').checked?'accept':'reject';});"
        "var a=document.createElement('a');"
        "a.href=URL.createObjectURL(new Blob([JSON.stringify(o,null,2)],"
        "{type:'application/json'}));a.download='verdicts.json';a.click();}"
        "</script></body></html>"
    )


def _hamming(a, b):
    """Hamming distance between two hex perceptual hashes.

    Description: Unequal lengths mean the hashes came from different phash
        settings and are not comparable, so they are treated as maximally far.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: a, b -- hex strings of equal length
    Returns: the bit distance, or a large number when they are not comparable
    """
    if not a or not b or len(a) != len(b):
        return 1 << 30
    return bin(int(a, 16) ^ int(b, 16)).count("1")


def dedup(records, already_accepted=None):
    """Drop records whose perceptual hash is too close to an earlier one.

    Description: Runs across every mood, not within one. A cave that appears in
        both UNDRGRND and HORROR reads as the rotation being broken, which is the
        same reason the pools are kept disjoint on the disc.

        `already_accepted` seeds `seen` before any candidate is judged, so a
        picture promoted in an earlier review pass still blocks its duplicate
        today -- without it, dedup only sees the current run's candidates and
        a later fetch can re-promote a picture already on the disc.

        A record with no hash is kept: imagehash is optional, and dropping
        everything when it is absent would be worse than keeping a duplicate.
    Author: suinevere
    Dependencies: N/A
    Globals: HAMMING_MAX
    Params: records -- a list of manifest records under review;
        already_accepted -- optional iterable of phashes already promoted
    Returns: the kept records, in input order
    """
    kept = []
    seen = [h for h in (already_accepted or []) if h]
    for r in records:
        h = r.get("phash", "")
        if h and any(_hamming(h, s) <= HAMMING_MAX for s in seen):
            continue
        if h:
            seen.append(h)
        kept.append(r)
    return kept


def promote(verdicts, manifest, candidates_dir, png_dir):
    """Move accepted candidates into the source tree and record the outcome.

    Description: A human reject is recorded as REJECTED, distinct from the
        METRIC_REJECTED a failed metric gate writes -- the two must not blur
        into one status, see art_status for why.
    Author: suinevere
    Dependencies: pathlib, art_status
    Globals: N/A
    Params: verdicts -- id -> "accept"/"reject"; manifest -- mutated in place;
        candidates_dir -- where the PNGs are; png_dir -- tools/assets/png
    Returns: dict mapping mood to how many pictures it gained
    """
    counts = {}
    for key, call in verdicts.items():
        rec = manifest.get(key)
        if rec is None or rec["status"] != art_status.CANDIDATE:
            continue
        if call != "accept":
            rec["status"] = art_status.REJECTED
            continue

        rel = Path(rec["mood"]) / rec["donor"] / rec["noun"] / f"{rec['id']}.png"
        src, dst = Path(candidates_dir) / rel, Path(png_dir) / rel
        if not src.exists():
            print(f"  {rel}: candidate file is missing, skipping")
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(src.read_bytes())
        src.unlink()
        rec["status"] = art_status.ACCEPTED
        counts[rec["mood"]] = counts.get(rec["mood"], 0) + 1
    return counts


def main(argv):
    """Write the contact sheets, or promote a downloaded verdicts file.

    Description: The two subcommands are separate runs on purpose -- a review
        session (opening sheets, checking boxes) happens between them.
    Author: suinevere
    Dependencies: fetch_art
    Globals: N/A
    Params: argv -- ["--sheets"] or ["--promote", "<verdicts.json>"]
    Returns: 0 always
    """
    import fetch_art
    from art_nouns import MOODS

    repo = Path(__file__).resolve().parents[1]
    assets = repo / "tools" / "assets"
    manifest_path = assets / "art_manifest.json"
    manifest = fetch_art.load_manifest(manifest_path)

    if argv and argv[0] == "--sheets":
        candidates = [r for r in manifest.values()
                      if r["status"] == art_status.CANDIDATE]
        already_accepted = [r.get("phash", "") for r in manifest.values()
                            if r["status"] == art_status.ACCEPTED]
        kept = {str(r["id"]): r for r in dedup(candidates, already_accepted)}
        out = assets / "sheets"
        out.mkdir(parents=True, exist_ok=True)
        for mood in MOODS:
            page = out / f"{mood}.html"
            page.write_text(sheet(mood, kept, assets / "candidates"),
                            encoding="utf-8")
            print(f"  {page}")
        return 0

    if len(argv) >= 2 and argv[0] == "--promote":
        with open(argv[1], "r", encoding="utf-8") as fh:
            verdicts = json.load(fh)
        counts = promote(verdicts, manifest, assets / "candidates",
                         assets / "png")
        fetch_art.save_manifest(manifest_path, manifest)
        for mood in sorted(counts):
            print(f"  {mood}: +{counts[mood]}")
        return 0

    print("  usage: art_review.py --sheets | --promote <verdicts.json>")
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv[1:]))
