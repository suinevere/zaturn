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
from collections import namedtuple
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


SHOWN = (art_status.ACCEPTED, art_status.REJECTED, art_status.CANDIDATE)


def sheet(mood, records, candidates_dir, png_dir):
    """Render one mood's whole review history as a self-contained page.

    Description: Shows accepted, rejected and undecided pictures together with
        the decision currently stored for each, because curation is taste
        applied repeatedly and a verdict has to be reversible. Metric rejections
        are absent by necessity: the fetcher only ever writes gate-passing
        images, so no file has existed for them. A record whose picture is gone
        -- every rejected image in a fresh clone -- still gets a tile, so the
        decision stays flippable with no pixels and no network.
    Author: suinevere
    Dependencies: html, art_status
    Globals: SHOWN
    Params: mood -- the mood folder name; records -- the manifest dict;
        candidates_dir -- the git-ignored tree; png_dir -- tools/assets/png
    Returns: the HTML as a string
    """
    mine = [r for r in records.values()
            if r["mood"] == mood and r["status"] in SHOWN]
    mine.sort(key=lambda r: (r["donor"], r["noun"], r["id"]))

    cells = []
    for r in mine:
        root = png_dir if r["status"] == art_status.ACCEPTED else candidates_dir
        src = _thumb_uri(Path(root) / _rel(r))
        label = {art_status.ACCEPTED: "accepted",
                 art_status.REJECTED: "rejected"}.get(r["status"], "undecided")
        checked = " checked" if r["status"] != art_status.REJECTED else ""
        art = (f'<img src="{src}" width="320" height="224" alt="">' if src
               else '<div class="gone">no local copy</div>')
        cells.append(
            f'<figure data-id="{r["id"]}" data-status="{label}">'
            f'{art}'
            f'<figcaption>{html.escape(r["phrase"])}<br>'
            f'<b>{label}</b> &middot; lum {r["luminance"]} '
            f'&middot; busy {r["busyness"]} &middot; band {r["banding"]}<br>'
            f'<a href="{html.escape(r["page_url"], quote=True)}" '
            f'target="_blank">{r["id"]}</a><br>'
            f'<label><input type="checkbox"{checked}> keep</label>'
            f'</figcaption></figure>'
        )

    return (
        "<!doctype html><html><head><meta charset='utf-8'>"
        f"<title>{mood} &mdash; {len(mine)} candidates</title>"
        "<style>body{background:#111;color:#ddd;font:13px sans-serif}"
        "figure{display:inline-block;margin:8px;text-align:center}"
        "figcaption{max-width:320px}a{color:#8cf}"
        ".gone{width:320px;height:224px;background:#222;color:#666;"
        "display:flex;align-items:center;justify-content:center}"
        "</style></head><body>"
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


Counts = namedtuple("Counts", "gained lost")


def _rel(rec):
    """Build a record's path relative to either image tree.

    Description: The candidates tree and the source tree share one layout, so
        one relative path locates a picture in both.
    Author: suinevere
    Dependencies: pathlib
    Globals: N/A
    Params: rec -- a manifest record
    Returns: Path of <mood>/<donor>/<noun>/<id>.png
    """
    return (Path(rec["mood"]) / rec["donor"] / rec["noun"]
            / "{}.png".format(rec["id"]))


def _move_if_absent(src, dst):
    """Move a picture between trees, but never over an existing destination.

    Description: A destination that already exists means the move has happened
        before and the source is a leftover -- moving it again would re-promote
        a picture the status guard has already settled. See the reconciliation
        rule in this feature's plan.
    Author: suinevere
    Dependencies: pathlib
    Globals: N/A
    Params: src -- where the picture is; dst -- where it belongs
    Returns: True if the picture now sits at dst, False if neither tree has it
    """
    src, dst = Path(src), Path(dst)
    if dst.exists():
        return True
    if not src.exists():
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(src.read_bytes())
    src.unlink()
    return True


def promote(verdicts, manifest, candidates_dir, png_dir):
    """Apply human verdicts, moving each picture to match its new status.

    Description: The manifest is the decision, and the file location follows it
        -- so a verdict is recorded even when no picture is on disk, which is
        the state of every rejected image in a fresh clone. A metric rejection
        is not a human decision and is never overwritten here; art_status
        explains why the two must not blur together.
    Author: suinevere
    Dependencies: pathlib, art_status
    Globals: N/A
    Params: verdicts -- id -> "accept"/"reject"; manifest -- mutated in place;
        candidates_dir -- the git-ignored tree; png_dir -- tools/assets/png
    Returns: dict mapping mood to a Counts of gained and lost pictures
    """
    counts = {}
    for key, call in verdicts.items():
        rec = manifest.get(key)
        if rec is None or rec["status"] == art_status.METRIC_REJECTED:
            continue

        want = (art_status.ACCEPTED if call == "accept"
                else art_status.REJECTED)
        was = rec["status"]
        rel = _rel(rec)
        cand, png = Path(candidates_dir) / rel, Path(png_dir) / rel

        if want == art_status.ACCEPTED:
            placed = _move_if_absent(cand, png)
        else:
            placed = _move_if_absent(png, cand)
        if not placed:
            print("  {}: no local copy; recording the verdict only".format(rel))

        rec["status"] = want
        if was == want:
            continue
        gained = 1 if want == art_status.ACCEPTED else 0
        lost = 1 if was == art_status.ACCEPTED else 0
        prev = counts.get(rec["mood"], Counts(0, 0))
        counts[rec["mood"]] = Counts(prev.gained + gained, prev.lost + lost)
    return counts


def main(argv, repo=None):
    """Write the contact sheets, or promote a downloaded verdicts file.

    Description: The two subcommands are separate runs on purpose -- a review
        session (opening sheets, checking boxes) happens between them. `repo`
        defaults to the real repository root; tests pass a tmp_path so a run
        never writes sheets or promotions into the working tree.
    Author: suinevere
    Dependencies: fetch_art
    Globals: N/A
    Params: argv -- ["--sheets"] or ["--promote", "<verdicts.json>"];
        repo -- optional repository root override, for tests
    Returns: 0 always
    """
    import fetch_art
    from art_nouns import MOODS

    repo = repo or Path(__file__).resolve().parents[1]
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
            page.write_text(sheet(mood, kept, assets / "candidates",
                                  assets / "png"), encoding="utf-8")
            print(f"  {page}")
        return 0

    if argv and argv[0] == "--promote":
        if len(argv) >= 2:
            paths = [Path(argv[1])]
        else:
            paths = sorted((assets / "sheets").glob("verdicts*.json"))
        if not paths:
            print("  no verdicts files found in {}".format(assets / "sheets"))
            return 0
        counts = {}
        for path in paths:
            with open(path, "r", encoding="utf-8") as fh:
                verdicts = json.load(fh)
            for mood, got in promote(verdicts, manifest,
                                     assets / "candidates",
                                     assets / "png").items():
                prev = counts.get(mood, Counts(0, 0))
                counts[mood] = Counts(prev.gained + got.gained,
                                      prev.lost + got.lost)
        fetch_art.save_manifest(manifest_path, manifest)
        for mood in sorted(counts):
            print(f"  {mood}: +{counts[mood].gained} -{counts[mood].lost}")
        return 0

    print("  usage: art_review.py --sheets | --promote <verdicts.json>")
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv[1:]))
