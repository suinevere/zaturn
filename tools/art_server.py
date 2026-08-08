"""Serve the candidate review UI on loopback so a verdict applies on click.

Description: Replaces the static contact sheets. Pictures are served as files
    rather than embedded, so page weight no longer scales with pool size, and a
    verdict is applied the moment it is clicked, so there is no download-and-
    promote round trip to forget. The manifest stays the only truth: every
    status change goes through art_review.promote, which owns the four
    transitions and the png/candidates moves.
Author: suinevere
Dependencies: flask, art_queries, art_review, art_status, fetch_art
Globals: N/A
"""
import sys
from pathlib import Path

import art_queries
import art_review
import art_status
import fetch_art

HOST = "127.0.0.1"
PORT = 8080


def _assets(repo):
    """Locate the asset tree under a repository root.

    Description: One place that knows the layout, so a test pointing at a
        tmp_path and a real run differ only in what they pass here.
    Author: suinevere
    Dependencies: pathlib
    Globals: N/A
    Params: repo -- the repository root
    Returns: the tools/assets directory as a Path
    """
    return Path(repo) / "tools" / "assets"


def _targets(assets):
    """Read each mood's picture goal from the shipped vocabulary.

    Description: The goal lives in art_queries.json rather than in this file,
        so retuning the vocabulary retunes the progress bars with it. A missing
        or unreadable vocabulary degrades to an empty map and the UI simply
        shows no target.
    Author: suinevere
    Dependencies: art_queries
    Globals: N/A
    Params: assets -- the tools/assets directory
    Returns: dict mapping mood to its integer target
    """
    try:
        vocab = art_queries.load(assets / "art_queries.json")
    except Exception:
        return {}
    out = {}
    for mood, entry in vocab.items():
        out[mood] = entry.get("target", 0)
    return out


def create_app(repo=None):
    """Build the Flask app rooted at a repository.

    Description: Takes the root as an argument rather than deriving it once at
        import, so tests can point a whole app at a tmp_path without touching
        the real asset tree.
    Author: suinevere
    Dependencies: flask
    Globals: N/A
    Params: repo -- repository root; defaults to this file's parent's parent
    Returns: a configured Flask app
    """
    from flask import Flask, render_template_string

    repo = Path(repo) if repo else Path(__file__).resolve().parents[1]
    assets = _assets(repo)
    app = Flask(__name__)

    @app.route("/")
    def index():
        manifest = fetch_art.load_manifest(assets / "art_manifest.json")
        targets = _targets(assets)
        rows = []
        for mood in sorted(targets):
            mine = [r for r in manifest.values() if r["mood"] == mood]
            rows.append({
                "mood": mood,
                "accepted": sum(1 for r in mine
                                if r["status"] == art_status.ACCEPTED),
                "rejected": sum(1 for r in mine
                                if r["status"] == art_status.REJECTED),
                "undecided": sum(1 for r in mine
                                 if r["status"] == art_status.CANDIDATE),
                "target": targets[mood],
            })
        return render_template_string(INDEX_HTML, rows=rows)

    @app.route("/image/<pid>")
    def image(pid):
        """Serve one picture's bytes from whichever tree currently holds it.

        Description: A record's status determines which tree its file lives
            in, but the route does not trust status -- it looks at both trees
            and serves whichever actually has the file, so a manifest that is
            momentarily out of step with disk still serves what is there.
        Author: suinevere
        Dependencies: flask, fetch_art, art_review
        Globals: N/A
        Params: pid -- the record id from the URL
        Returns: the PNG file, or 404 when the id is unknown or no file exists
        """
        from flask import abort, send_file
        manifest = fetch_art.load_manifest(assets / "art_manifest.json")
        rec = manifest.get(str(pid))
        if rec is None:
            abort(404)
        rel = art_review._rel(rec)
        for root in (assets / "png", assets / "candidates"):
            path = Path(root) / rel
            if path.exists():
                return send_file(str(path), mimetype="image/png")
        abort(404)

    @app.route("/verdict", methods=["POST"])
    def verdict():
        """Apply one verdict through promote and report the mood's refreshed counts.

        Description: The manifest is the only truth, so this route never
            assigns rec["status"] itself -- it hands the call to
            art_review.promote, which owns all four transitions and the
            png/candidates moves, then reads the record's status back out of
            the manifest promote just mutated.
        Author: suinevere
        Dependencies: flask, art_review, art_status, fetch_art
        Globals: N/A
        Params: N/A -- reads JSON {"id", "verdict"} from the request body
        Returns: JSON {"id", "status", "accepted", "rejected", "undecided"};
            404 for an unknown id, 400 for a verdict that is not accept/reject
        """
        from flask import abort, jsonify, request
        body = request.get_json(silent=True) or {}
        pid = str(body.get("id", ""))
        call = body.get("verdict", "")
        if call not in ("accept", "reject"):
            abort(400)
        path = assets / "art_manifest.json"
        manifest = fetch_art.load_manifest(path)
        rec = manifest.get(pid)
        if rec is None:
            abort(404)
        art_review.promote({pid: call}, manifest,
                           assets / "candidates", assets / "png")
        fetch_art.save_manifest(path, manifest)
        mood = rec["mood"]
        mine = [r for r in manifest.values() if r["mood"] == mood]
        return jsonify({
            "id": pid,
            "status": rec["status"],
            "accepted": sum(1 for r in mine
                            if r["status"] == art_status.ACCEPTED),
            "rejected": sum(1 for r in mine
                            if r["status"] == art_status.REJECTED),
            "undecided": sum(1 for r in mine
                             if r["status"] == art_status.CANDIDATE),
        })

    return app


INDEX_HTML = """<!doctype html><meta charset="utf-8"><title>Room art review</title>
<style>body{background:#111;color:#ddd;font:13px sans-serif;margin:24px}
table{border-collapse:collapse}td,th{padding:5px 14px;border-bottom:1px solid #333;text-align:right}
td:first-child,th:first-child{text-align:left}a{color:#8cf}
.bar{background:#222;width:120px;height:9px;display:inline-block}
.bar i{background:#4a8;height:9px;display:block}</style>
<h1>Room art review</h1>
<table><tr><th>mood</th><th>accepted</th><th>rejected</th><th>undecided</th>
<th>of target</th><th></th></tr>
{% for r in rows %}<tr>
<td><a href="/mood/{{ r.mood }}">{{ r.mood }}</a></td>
<td>{{ r.accepted }}</td><td>{{ r.rejected }}</td><td>{{ r.undecided }}</td>
<td>{{ r.accepted }} / {{ r.target }}</td>
<td><span class="bar"><i style="width:{{ (100 * r.accepted // r.target) if r.target else 0 }}%"></i></span></td>
</tr>{% endfor %}
</table>
"""


def main(argv):
    """Run the review server on loopback.

    Description: A missing Flask, or a port already bound, prints an actionable
        line and returns 0 rather than raising -- the everything-degrades rule
        the rest of these tools follow. The port is fixed rather than hunted
        for, because the operator keeps that URL open.
    Author: suinevere
    Dependencies: flask
    Globals: HOST, PORT
    Params: argv -- unused, accepted so the entry point matches its siblings
    Returns: 0 always
    """
    try:
        import flask                                    # noqa: F401
    except ImportError:
        print("  Flask is not installed. Run:")
        print("    python -m pip install -r tools/requirements-review.txt")
        return 0
    app = create_app()
    try:
        app.run(host=HOST, port=PORT, debug=False)
    except OSError as exc:
        print(f"  cannot bind {HOST}:{PORT} ({exc}).")
        print("  Something else is using it; free that port and re-run.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
