"""The manifest record status vocabulary, defined once for every module that reads it.

Description: fetch_art.py writes CANDIDATE or METRIC_REJECTED; art_review.py
    reads CANDIDATE, then writes ACCEPTED or REJECTED. Metric and human
    rejections are kept apart on purpose: METRIC_REJECTED is the only corpus
    that will ever let the metric gate's guessed thresholds (see
    art_metrics.THRESHOLDS) be tuned against real photographs, and collapsing
    it into REJECTED after one review pass would destroy that data.

    Also carries write_snapshot/main: tools/assets/art_manifest.json is local
    curation state, untracked so a stash pop or branch switch can never wipe
    it again; art_manifest.snapshot.json is the tracked copy taken at promote
    time.
Author: suinevere
Dependencies: pathlib
Globals: CANDIDATE, ACCEPTED, REJECTED, METRIC_REJECTED
"""
import pathlib

CANDIDATE = "candidate"
ACCEPTED = "accepted"
REJECTED = "rejected"
METRIC_REJECTED = "metric_rejected"


def write_snapshot(repo=None):
    """Copies the working manifest to the committed snapshot, which
    is the record of curation at the moment images reached the disc.
    Author: suinevere
    Dependencies: pathlib
    Globals: N/A
    Params: repo -- repo root; defaults to the one containing this file
    Returns: the snapshot path
    """
    root = pathlib.Path(repo) if repo else pathlib.Path(__file__).resolve().parent.parent
    live = root / "tools" / "assets" / "art_manifest.json"
    snap = root / "tools" / "assets" / "art_manifest.snapshot.json"
    snap.write_text(live.read_text(encoding="utf-8"), encoding="utf-8")
    return snap


def main(argv, repo=None):
    """Command-line entry point for manifest-snapshot maintenance.

    Description: The only operation today is --snapshot, which promotes
        the working manifest to the tracked snapshot via write_snapshot.
        Unrecognised argv prints usage and returns 0, matching the other
        tools/*.py entry points in this pipeline.
    Author: suinevere
    Dependencies: write_snapshot
    Globals: N/A
    Params: argv -- ["--snapshot"]; repo -- optional repository root
        override, for tests
    Returns: 0 always
    """
    if argv and argv[0] == "--snapshot":
        path = write_snapshot(repo=repo)
        print(f"  wrote {path}")
        return 0

    print("  usage: art_status.py --snapshot")
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv[1:]))
