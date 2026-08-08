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
import socket
import sys
from pathlib import Path

import art_queries
import art_review
import art_status
import fetch_art

"""HOST / PORT

Description: The interface and port the review UI listens on. HOST is
    0.0.0.0 -- every interface, not just loopback -- so the operator can
    review from another machine on the LAN by hostname. That is a deliberate
    exposure with a real consequence: there is no authentication, and
    POST /verdict moves and deletes files inside the repository, so anyone
    who can reach this port can re-curate the pool. Acceptable only on a
    trusted network. PORT is fixed rather than hunted for, because the
    operator keeps that URL open.
Author: suinevere
"""
HOST = "0.0.0.0"
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


SHOWN = (art_status.ACCEPTED, art_status.REJECTED, art_status.CANDIDATE)

FILTERS = {
    "undecided": (art_status.CANDIDATE,),
    "accepted": (art_status.ACCEPTED,),
    "rejected": (art_status.REJECTED,),
    "all": SHOWN,
}


def groups_for(records, mood, status):
    """Split one mood's pictures into donor/noun sections.

    Description: The sections mirror the on-disk tree
        tools/assets/png/<MOOD>/<DONOR>/<noun>/, which is the subdivision the
        owner is filling toward a target -- so each carries its own counts and a
        thin noun is visible without arithmetic. Counts describe the whole
        group, not the filtered view, because "two accepted here already" is
        what decides whether to keep a third. METRIC_REJECTED never appears:
        the fetcher writes no file for one, so there is nothing to show.
    Author: suinevere
    Dependencies: art_status
    Globals: SHOWN, FILTERS
    Params: records -- the manifest dict; mood -- the mood folder name;
        status -- one of FILTERS' keys; anything else is treated as "all"
    Returns: list of group dicts sorted by (donor, noun)
    """
    wanted = FILTERS.get(status, SHOWN)
    mine = [r for r in records.values()
            if r["mood"] == mood and r["status"] in SHOWN]
    keys = sorted({(r["donor"], r["noun"]) for r in mine})
    out = []
    for donor, noun in keys:
        group = [r for r in mine
                 if r["donor"] == donor and r["noun"] == noun]
        shown = sorted((r for r in group if r["status"] in wanted),
                       key=lambda r: r["id"])
        out.append({
            "donor": donor, "noun": noun, "records": shown,
            "accepted": sum(1 for r in group
                            if r["status"] == art_status.ACCEPTED),
            "rejected": sum(1 for r in group
                            if r["status"] == art_status.REJECTED),
            "undecided": sum(1 for r in group
                             if r["status"] == art_status.CANDIDATE),
        })
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
        """Render the mood list with each mood's counts and progress toward target.

        Description: Moods come from the vocabulary rather than the manifest,
            so a mood with zero pictures still gets a row and a visible target;
            a manifest record whose mood has no vocabulary entry is silently
            left out of every row rather than crashing the page.
        Author: suinevere
        Dependencies: flask, fetch_art, art_queries, art_status
        Globals: N/A
        Params: N/A
        Returns: rendered HTML listing every mood in the vocabulary
        """
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

    @app.route("/mood/<mood>")
    def mood_page(mood):
        """Render one mood's pictures grouped by donor/noun with a status filter.

        Description: Defaults to the undecided view because a resumed review
            pass wants to see what is left, not what is already settled. The
            `have` set drives the placeholder branch in the template: a record
            can exist in the manifest with no file on disk yet, and the
            verdict buttons must still work for it.
        Author: suinevere
        Dependencies: flask, art_review, fetch_art
        Globals: N/A
        Params: mood -- the mood folder name from the URL
        Returns: rendered HTML; 404 for a mood with no vocabulary entry
        """
        from flask import abort, render_template_string, request
        if mood not in _targets(assets):
            abort(404)
        manifest = fetch_art.load_manifest(assets / "art_manifest.json")
        status = request.args.get("status", "undecided")
        groups = groups_for(manifest, mood, status)
        have = set()
        for root in (assets / "png", assets / "candidates"):
            for g in groups:
                for r in g["records"]:
                    if (Path(root) / art_review._rel(r)).exists():
                        have.add(str(r["id"]))
        return render_template_string(MOOD_HTML, mood=mood, groups=groups,
                                      status=status, have=have)

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


MOOD_HTML = """<!doctype html><meta charset="utf-8"><title>{{ mood }}</title>
<style>body{background:#111;color:#ddd;font:13px sans-serif;margin:24px}
a{color:#8cf}h2{margin:26px 0 6px;font-size:14px;border-bottom:1px solid #333}
figure{display:inline-block;margin:6px;text-align:center;width:320px}
figcaption{font-size:11px}
.gone{width:320px;height:224px;background:#222;color:#666;display:flex;
align-items:center;justify-content:center}
figure.accepted{outline:3px solid #4a8}figure.rejected{opacity:.35}
figure:focus{outline:3px solid #8cf}
#big{position:fixed;inset:0;background:#000d;display:none;
align-items:center;justify-content:center}#big img{max-width:95vw}</style>
<p><a href="/">&larr; all moods</a> &middot;
{% for f in ["undecided","accepted","rejected","all"] %}
<a href="/mood/{{ mood }}?status={{ f }}">{{ f }}</a>
{% endfor %}</p>
<h1>{{ mood }}</h1>
{% for g in groups %}
<h2>{{ g.donor }} / {{ g.noun }} &mdash;
{{ g.accepted }} accepted &middot; {{ g.rejected }} rejected &middot;
{{ g.undecided }} undecided</h2>
{% for r in g.records %}
<figure data-id="{{ r.id }}" tabindex="0" class="{{ r.status }}">
{% if r.id|string in have %}<img src="/image/{{ r.id }}" width="320" height="224" alt="">
{% else %}<div class="gone">no local copy</div>{% endif %}
<figcaption>{{ r.phrase }}<br>
<a href="{{ r.page_url }}" target="_blank">{{ r.id }}</a>
<span class="st">{{ r.status }}</span><br>
<button onclick="v('{{ r.id }}','accept')">accept</button>
<button onclick="v('{{ r.id }}','reject')">reject</button>
</figcaption></figure>
{% endfor %}{% endfor %}
<div id="big" onclick="this.style.display='none'"><img></div>
<script>
function v(id, call){
  fetch('/verdict', {method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({id:id, verdict:call})})
  .then(function(r){ return r.json(); })
  .then(function(d){
    var f = document.querySelector('figure[data-id="'+d.id+'"]');
    f.className = d.status;
    f.querySelector('.st').textContent = d.status;
  });
}
document.querySelectorAll('figure img').forEach(function(img){
  img.addEventListener('click', function(){
    var b = document.getElementById('big');
    b.querySelector('img').src = img.src;
    b.style.display = 'flex';
  });
});
document.addEventListener('keydown', function(e){
  var f = document.activeElement;
  if (!f || f.tagName !== 'FIGURE') return;
  if (e.key === 'a' || e.key === 'A') { v(f.dataset.id, 'accept'); next(f); }
  if (e.key === 'r' || e.key === 'R') { v(f.dataset.id, 'reject'); next(f); }
  if (e.key === 'ArrowRight') next(f);
  if (e.key === 'ArrowLeft') prev(f);
});
function all(){ return Array.prototype.slice.call(
  document.querySelectorAll('figure')); }
function next(f){ var a = all(), i = a.indexOf(f);
  if (i > -1 && a[i+1]) a[i+1].focus(); }
function prev(f){ var a = all(), i = a.indexOf(f);
  if (i > 0) a[i-1].focus(); }
</script>
"""


def main(argv):
    """Run the review server on every interface.

    Description: A missing Flask, or a port already bound, prints an actionable
        line and returns 0 rather than raising -- the everything-degrades rule
        the rest of these tools follow. Prints both the loopback and the
        hostname URL, because the whole point of binding every interface is
        reaching it from another machine and the operator needs the name to
        type. A hostname that will not resolve degrades to printing the
        loopback URL alone rather than failing to start.
    Author: suinevere
    Dependencies: flask, socket
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
    print(f"  review UI: http://127.0.0.1:{PORT}")
    try:
        print(f"             http://{socket.gethostname()}:{PORT}  (this LAN)")
    except OSError:
        pass
    print("  no authentication -- anyone who can reach this port can re-curate.")
    app = create_app()
    try:
        app.run(host=HOST, port=PORT, debug=False)
    except OSError as exc:
        print(f"  cannot bind {HOST}:{PORT} ({exc}).")
        print("  Something else is using it; free that port and re-run.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
