#!/usr/bin/env python3
"""Review server for rooms the title rules refused.

Description: The human half of the tagging pipeline, in the shape of
    tools/art_server.py. Shows one refused group at a time -- title, captured
    description, and the 32 scenes -- and writes the verdict to every object in
    the group, because a repeated title is a repeated place.

    Both JSON files are read-modify-written on every single verdict rather than
    held in memory and flushed at exit. A session's verdicts are exactly the
    kind of state this project has lost before, and a crash must cost at most
    the one decision in flight.
Author: suinevere
Dependencies: flask, json, pathlib, re, sys, scene_vocab, art_nouns
Globals: N/A
"""
import json
import pathlib
import re
import sys

from flask import Flask, jsonify, render_template_string, request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import art_nouns
import scene_vocab as vocab

TC_NAMES = (
    "TC_NEUTRAL", "TC_WILDERNESS", "TC_UNDERGROUND", "TC_WATER", "TC_NAUTICAL",
    "TC_TOWN", "TC_DUNGEON", "TC_DESERT", "TC_MAGIC", "TC_SCIFI", "TC_HORROR",
    "TC_MYSTERY", "TC_HOUSE", "TC_DANGER", "TC_TRIUMPH",
)

"""MOOD_TO_SCENES

Description: A hand-picked, approximate narrowing from each retired TC_* mood
    folder to the two-to-four SCENES a room tagged with that mood is most
    likely to actually be. Advisory only -- the hint highlights buttons, it
    never picks one.
Author: suinevere
"""
MOOD_TO_SCENES = {
    "WILDER": ("FOREST", "GARDEN", "ROCKY", "ROAD"),
    "UNDRGRND": ("CAVE", "MAZE", "MINE", "PIT"),
    "WATER": ("SHORE", "RIVER", "DOCK"),
    "NAUTICAL": ("SHIP_EXT", "SHIP_INT", "DOCK"),
    "TOWN": ("VILLAGE", "ROAD", "DOCK"),
    "DUNGN": ("CRYPT", "CELL", "MAZE", "CORRIDOR"),
    "DESERT": ("DESERT", "ROCKY"),
    "MAGIC": ("TEMPLE", "LIBRARY", "CRYPT"),
    "SCIFI": ("SPACE", "SHIP_INT", "LAB"),
    "HORROR": ("CRYPT", "DARKROOM", "CELL"),
    "MYSTERY": ("LIBRARY", "OFFICE", "PARLOR", "CORRIDOR"),
    "HOUSE": ("PARLOR", "KITCHEN", "BEDROOM", "BATHROOM", "HOUSE_EXT"),
}

_BLESSED_ROW = re.compile(r"^\s*(\d+),\s*/\*\s*\d+:\s*(.+?)\s*\*/\s*,?\s*$")


def load_hints(root):
    """Read the retired mood classifier's blessed corpus as a title hint.

    Description: Parses test/corpus/blessed.inc, keyed by title alone --
        the same title in a different story is the same kind of room often
        enough to be worth a hint, and it is only ever a hint. A missing
        corpus (as in every test fixture) degrades to no hints at all.
    Author: suinevere
    Dependencies: pathlib, re
    Globals: TC_NAMES, _BLESSED_ROW
    Params: root -- repo root
    Returns: dict mapping lowercased room title to a TC_* mood folder name
    """
    path = pathlib.Path(root) / "test" / "corpus" / "blessed.inc"
    if not path.exists():
        return {}
    hints = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = _BLESSED_ROW.match(line)
        if not m:
            continue
        idx = int(m.group(1))
        if idx >= len(TC_NAMES):
            continue
        folder = art_nouns.TC_TO_FOLDER.get(TC_NAMES[idx])
        if folder is None:
            continue
        hints[m.group(2).strip().lower()] = folder
    return hints


PAGE = """<!doctype html><title>{{ stem }} — scenes</title>
<style>
 body{font:15px system-ui;margin:2rem;max-width:56rem}
 #desc{color:#444;line-height:1.5;margin:.5rem 0 1rem}
 .grid{display:grid;grid-template-columns:repeat(4,1fr);gap:.4rem}
 button{padding:.6rem;font:inherit;cursor:pointer}
 .hint{background:#ffd}
</style>
<h1>{{ stem }} <small id="left">{{ left }} left</small></h1>
<h2 id="title"><span id="title-text">{{ group.title if group else 'queue clear' }}</span>
  {% if hint_mood %}<small id="hint-mood">(was {{ hint_mood }})</small>{% endif %}</h2>
<div id="desc">{{ group.description or '(no description captured)' if group else '' }}</div>
<div class="grid">
{% for s in scenes %}<button data-scene="{{ s }}"
  class="{{ 'hint' if s in hint_scenes else '' }}"
  onclick="verdict('{{ s }}')">{{ s }}</button>{% endfor %}
</div>
<p><button onclick="verdict(null)">Skip</button></p>
<script>
let obj = {{ group.obj if group else 'null' }};
function applyHint(mood, scenes) {
  document.getElementById('hint-mood') && document.getElementById('hint-mood').remove();
  if (mood) {
    const h = document.createElement('small');
    h.id = 'hint-mood';
    h.textContent = ' (was ' + mood + ')';
    document.getElementById('title').appendChild(h);
  }
  document.querySelectorAll('button[data-scene]').forEach(function (b) {
    b.classList.toggle('hint', scenes.indexOf(b.dataset.scene) !== -1);
  });
}
async function verdict(scene) {
  if (obj === null) return;
  const r = await fetch(scene ? '/verdict' : '/skip', {
    method: 'POST', headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({story: '{{ stem }}', obj: obj, scene: scene})});
  if (!r.ok) { alert('rejected'); return; }
  const d = await r.json();
  obj = d.group ? d.group.obj : null;
  document.getElementById('title-text').textContent = d.group ? d.group.title : 'queue clear';
  document.getElementById('desc').textContent =
      d.group ? (d.group.description || '(no description captured)') : '';
  document.getElementById('left').textContent = d.left + ' left';
  applyHint(d.hint_mood, d.hint_scenes || []);
}
</script>"""


def create_app(repo=None):
    """Build the review app, rooted at repo so tests can point it elsewhere.

    Description: The review app, rooted at `repo` so tests can point it at a
        temporary tree instead of the working copy.
    Author: suinevere
    Dependencies: flask, scene_vocab
    Globals: N/A
    Params: repo -- repo root; defaults to the one containing this file
    Returns: a flask.Flask
    """
    root = pathlib.Path(repo) if repo else pathlib.Path(__file__).resolve().parent.parent
    scenes_dir = root / "tools" / "assets" / "scenes"
    hints = load_hints(root)
    app = Flask(__name__)

    def hint_for(group):
        if group is None:
            return None, ()
        mood = hints.get(group["title"].strip().lower())
        return mood, MOOD_TO_SCENES.get(mood, ())

    def load(stem):
        b = scenes_dir / f"{stem}.json"
        r = scenes_dir / f"{stem}.review.json"
        return (json.loads(b.read_text(encoding="utf-8")) if b.exists() else {},
                json.loads(r.read_text(encoding="utf-8")) if r.exists() else [])

    def save(stem, blessed, review):
        (scenes_dir / f"{stem}.json").write_text(
            json.dumps(blessed, indent=1, sort_keys=True) + "\n", encoding="utf-8")
        (scenes_dir / f"{stem}.review.json").write_text(
            json.dumps(review, indent=1, sort_keys=True) + "\n", encoding="utf-8")

    def take(stem, obj, scene):
        blessed, review = load(stem)
        group = next((g for g in review if g["obj"] == obj), None)
        if group is None:
            return None, blessed, review
        if scene is not None:
            for o in group["objs"]:
                blessed[str(o)] = scene
        review = [g for g in review if g["obj"] != obj]
        save(stem, blessed, review)
        return group, blessed, review

    @app.route("/")
    def index():
        rows = sorted(p.stem.replace(".review", "")
                      for p in scenes_dir.glob("*.review.json"))
        return "<h1>scene review</h1>" + "".join(
            f'<p><a href="/game/{s}">{s}</a></p>' for s in rows)

    @app.route("/game/<stem>")
    def game(stem):
        _, review = load(stem)
        group = review[0] if review else None
        hint_mood, hint_scenes = hint_for(group)
        return render_template_string(PAGE, stem=stem, scenes=vocab.SCENES,
                                      group=group, left=len(review),
                                      hint_mood=hint_mood, hint_scenes=hint_scenes)

    @app.route("/verdict", methods=["POST"])
    def verdict():
        d = request.get_json(force=True)
        if d.get("scene") not in vocab.SCENE_INDEX:
            return jsonify(error="unknown scene"), 400
        group, _, review = take(d["story"], d["obj"], d["scene"])
        if group is None:
            return jsonify(error="unknown group"), 404
        next_group = review[0] if review else None
        hint_mood, hint_scenes = hint_for(next_group)
        return jsonify(group=next_group, left=len(review),
                       hint_mood=hint_mood, hint_scenes=hint_scenes)

    @app.route("/skip", methods=["POST"])
    def skip():
        d = request.get_json(force=True)
        group, _, review = take(d["story"], d["obj"], None)
        if group is None:
            return jsonify(error="unknown group"), 404
        next_group = review[0] if review else None
        hint_mood, hint_scenes = hint_for(next_group)
        return jsonify(group=next_group, left=len(review),
                       hint_mood=hint_mood, hint_scenes=hint_scenes)

    return app


def main(argv):
    """Serve the review app on 8081; 8080 is the art server.

    Description: Serves the review app on 8081; 8080 is the art server.
    Author: suinevere
    Dependencies: flask
    Globals: N/A
    Params: argv -- unused
    Returns: 0
    """
    create_app().run(host="0.0.0.0", port=8081, debug=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
