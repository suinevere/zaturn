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

    Every write is reversible. Undo replays a per-story stack of prior values,
    Skip rotates a group to the back of the queue instead of dropping it, and
    the /tagged page lists every verdict already given so any of them can be
    changed long after the session that made it.
Author: suinevere
Dependencies: flask, json, pathlib, sys, scene_vocab, room_scenes
Globals: N/A
"""
import json
import pathlib
import sys

from flask import Flask, jsonify, render_template_string, request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import room_scenes
import scene_vocab as vocab

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

def load_hints(root):
    """Read the retired mood classifier's blessed judgments as a title hint.

    Description: Parses tools/assets/blessed_moods.json, a one-time extraction
        of the deleted test/corpus/blessed.inc (recovered from git history at
        commit cd97b35 -- the .inc itself was a generated test oracle for a
        test that is correctly gone, but its 855 hand-blessed room->mood
        judgments are still the reviewer's only lead on 390 rooms library-wide
        that have no captured description). Keyed by (serial, title): 135 of
        the corpus's 801 unique titles recur across more than one story
        release ("Kitchen", "Maze", "Dead End", "Closet"...), so a title-only
        hint borrows the wrong game's mood routinely, and it is worst exactly
        where it matters most, since a room with no description has the title
        plus this hint as its whole basis for a decision. Scoping to the same
        story means a room with no same-story hint shows none at all, which is
        correct: a wrong hint is worse than no hint. A missing or unreadable
        JSON (as in every test fixture) degrades to no hints at all.
    Author: suinevere
    Dependencies: pathlib, json
    Globals: N/A
    Params: root -- repo root
    Returns: dict mapping (serial, lowercased room title) to a TC_* mood
        folder name
    """
    path = pathlib.Path(root) / "tools" / "assets" / "blessed_moods.json"
    if not path.exists():
        return {}
    try:
        by_serial = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return {}
    hints = {}
    for serial, titles in by_serial.items():
        for title, folder in titles.items():
            hints[(serial, title)] = folder
    return hints


def load_rooms(root, stem):
    """Read one story's captured room inventory.

    Description: The inventory is the only place an object number can be
        turned back into a title, which the /tagged page needs to describe a
        verdict given in an earlier session. Missing or unreadable degrades to
        an empty inventory, which renders as a tagged list with bare object
        numbers rather than an error.
    Author: suinevere
    Dependencies: pathlib, json
    Globals: N/A
    Params: root -- repo root; stem -- a story stem, e.g. "ZORK1"
    Returns: list of inventory rows
    """
    path = pathlib.Path(root) / "tools" / "assets" / "rooms" / f"{stem}.json"
    if not path.exists():
        return []
    try:
        return json.loads(path.read_text(encoding="utf-8")).get("rooms", [])
    except (json.JSONDecodeError, OSError):
        return []


def load_serial(root, stem):
    """Look up a story's Z-machine serial from its captured room data.

    Description: blessed.inc keys its rows by serial, not by story stem, so
        scoping a hint to "the same game" means resolving stem -> serial
        first. Reads tools/assets/rooms/<stem>.json, which gen_scene_tables.py
        already established carries a "serial" field alongside "release".
        Missing or unreadable degrades to None, which hint_for (in
        create_app) treats as "no hint available" rather than an error.
    Author: suinevere
    Dependencies: pathlib, json
    Globals: N/A
    Params: root -- repo root; stem -- a story stem, e.g. "ZORK1"
    Returns: the serial string, or None
    """
    path = pathlib.Path(root) / "tools" / "assets" / "rooms" / f"{stem}.json"
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8")).get("serial")
    except (json.JSONDecodeError, OSError):
        return None


PAGE = """<!doctype html><title>{{ stem }} — scenes</title>
<style>
 body{font:15px system-ui;margin:2rem;max-width:56rem}
 #desc{color:#444;line-height:1.5;margin:.5rem 0 1rem}
 .grid{display:grid;grid-template-columns:repeat(4,1fr);gap:.4rem}
 button{padding:.6rem;font:inherit;cursor:pointer}
 .hint{background:#ffd;font-weight:600}
 #legend{color:#555;font-size:13px;line-height:1.5;margin:.2rem 0 1rem;
         border-left:3px solid #ffd;padding-left:.6rem}
 #scope{color:#a00;font-size:13px;margin:0 0 .8rem}
 .bar{margin-top:1.2rem;display:flex;gap:.6rem;align-items:center}
 .bar small{color:#666}
 a{color:#06c}
</style>
<h1>{{ stem }} <small id="left">{{ left }} left</small>
  — <a href="/game/{{ stem }}/tagged">review {{ tagged }} already tagged</a></h1>
<div id="legend">
 One click = one tag = this room's whole picture and music. There is no second
 tag and no overlap.<br>
 <b>Yellow</b> buttons are the retired mood classifier's guess for this exact
 room, narrowed to a handful of scenes. A hint, not a limit — any of the 32 is
 a legal answer, and yellow is often wrong.<br>
 <b>Skip</b> sends the room to the back of the queue, it does not drop it.
 <b>Back</b> undoes the last verdict or skip and puts you on it again.
</div>
<h2 id="title"><span id="title-text">{{ group.title if group else 'queue clear' }}</span>
  {% if hint_mood %}<small id="hint-mood">(was {{ hint_mood }})</small>{% endif %}</h2>
<div id="scope">{{ scope }}</div>
<div id="desc">{{ group.description or '(no description captured)' if group else '' }}</div>
<div class="grid">
{% for s in scenes %}<button data-scene="{{ s }}"
  class="{{ 'hint' if s in hint_scenes else '' }}"
  onclick="verdict('{{ s }}')">{{ s }}</button>{% endfor %}
</div>
<p class="bar">
  <button id="back" onclick="undo()" {{ '' if undoable else 'disabled' }}>← Back</button>
  <button onclick="verdict(null)">Skip →</button>
  <small id="undo-note">{{ undo_note }}</small>
</p>
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
function render(d) {
  obj = d.group ? d.group.obj : null;
  document.getElementById('title-text').textContent =
      d.group ? d.group.title : 'queue clear';
  document.getElementById('desc').textContent =
      d.group ? (d.group.description || '(no description captured)') : '';
  document.getElementById('scope').textContent = d.scope || '';
  document.getElementById('left').textContent = d.left + ' left';
  document.getElementById('back').disabled = !d.undoable;
  document.getElementById('undo-note').textContent = d.undo_note || '';
  applyHint(d.hint_mood, d.hint_scenes || []);
}
async function post(url, body) {
  const r = await fetch(url, {method: 'POST',
    headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)});
  if (!r.ok) { alert('rejected'); return null; }
  return await r.json();
}
async function verdict(scene) {
  if (obj === null) return;
  const d = await post(scene ? '/verdict' : '/skip',
                       {story: '{{ stem }}', obj: obj, scene: scene});
  if (d) render(d);
}
async function undo() {
  const d = await post('/undo', {story: '{{ stem }}'});
  if (d) render(d);
}
document.addEventListener('keydown', function (e) {
  if (e.key === 'Backspace' && e.target === document.body) { e.preventDefault(); undo(); }
  if (e.key === ' ' && e.target === document.body) { e.preventDefault(); verdict(null); }
});
</script>"""


TAGGED_PAGE = """<!doctype html><title>{{ stem }} — tagged</title>
<style>
 body{font:15px system-ui;margin:2rem;max-width:64rem}
 table{border-collapse:collapse;width:100%}
 td,th{border-bottom:1px solid #ddd;padding:.35rem .5rem;text-align:left}
 th{font-size:13px;color:#666}
 select{font:inherit;padding:.2rem}
 .src{color:#888;font-size:13px}
 .changed{color:#a00}
 a{color:#06c}
</style>
<h1>{{ stem }} — {{ rows|length }} tagged
  <small>(<a href="/game/{{ stem }}">back to the queue, {{ left }} left</a>)</small></h1>
<p style="color:#555;font-size:13px">Change any tag here and it is written
immediately. <b>rule</b> means the title rules decided it and no human has
looked; <b>you</b> means someone chose it. Changing a rule verdict pins it, so
re-running <code>tools/room_scenes.py</code> will never overwrite it again.</p>
<table>
<tr><th>title</th><th>objects</th><th>scene</th><th>source</th></tr>
{% for r in rows %}
<tr><td>{{ r.title }}</td>
  <td class="src">{{ r.objs|join(', ') }}</td>
  <td><select onchange="retag(this, {{ r.objs|tojson }})">
    {% for s in scenes %}<option value="{{ s }}"
      {{ 'selected' if s == r.scene else '' }}>{{ s }}</option>{% endfor %}
  </select></td>
  <td class="src {{ 'changed' if r.source != 'rule' else '' }}">{{ r.source }}</td></tr>
{% endfor %}
</table>
<script>
async function retag(sel, objs) {
  const r = await fetch('/retag', {method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({story: '{{ stem }}', objs: objs, scene: sel.value})});
  if (!r.ok) { alert('rejected'); return; }
  sel.parentElement.parentElement.lastElementChild.textContent = 'you';
  sel.parentElement.parentElement.lastElementChild.className = 'src changed';
}
</script>"""


def create_app(repo=None):
    """Build the review app, rooted at repo so tests can point it elsewhere.

    Description: The review app, rooted at `repo` so tests can point it at a
        temporary tree instead of the working copy. Holds one undo stack per
        story for the life of the process; the durable way back to an older
        verdict is the /tagged page, not this stack.
    Author: suinevere
    Dependencies: flask, scene_vocab, room_scenes
    Globals: N/A
    Params: repo -- repo root; defaults to the one containing this file
    Returns: a flask.Flask
    """
    root = pathlib.Path(repo) if repo else pathlib.Path(__file__).resolve().parent.parent
    scenes_dir = root / "tools" / "assets" / "scenes"
    hints = load_hints(root)
    serials = {}
    history = {}
    app = Flask(__name__)

    def serial_for(stem):
        if stem not in serials:
            serials[stem] = load_serial(root, stem)
        return serials[stem]

    def hint_for(stem, group):
        if group is None:
            return None, ()
        serial = serial_for(stem)
        if serial is None:
            return None, ()
        mood = hints.get((serial, group["title"].strip().lower()))
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

    def remember(stem, objs, blessed, group, index):
        history.setdefault(stem, []).append(
            {"prior": {str(o): blessed.get(str(o)) for o in objs},
             "group": group, "index": index})

    def scope_of(group):
        if group is None or len(group["objs"]) < 2:
            return ""
        return (f"one click tags all {len(group['objs'])} rooms titled "
                f"“{group['title']}”")

    def undo_note(stem):
        depth = len(history.get(stem, ()))
        return f"{depth} undoable" if depth else "nothing to undo"

    def state(stem, review):
        group = review[0] if review else None
        hint_mood, hint_scenes = hint_for(stem, group)
        return dict(group=group, left=len(review), scope=scope_of(group),
                    hint_mood=hint_mood, hint_scenes=hint_scenes,
                    undoable=bool(history.get(stem)), undo_note=undo_note(stem))

    def take(stem, obj, scene):
        blessed, review = load(stem)
        index = next((i for i, g in enumerate(review) if g["obj"] == obj), None)
        if index is None:
            return None, blessed, review
        group = review[index]
        remember(stem, group["objs"] if scene is not None else [], blessed,
                 group, index)
        if scene is not None:
            for o in group["objs"]:
                blessed[str(o)] = scene
            review = [g for g in review if g["obj"] != obj]
        else:
            review = [g for g in review if g["obj"] != obj] + [group]
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
        blessed, review = load(stem)
        s = state(stem, review)
        return render_template_string(PAGE, stem=stem, scenes=vocab.SCENES,
                                      tagged=len(blessed), **s)

    @app.route("/game/<stem>/tagged")
    def tagged(stem):
        blessed, review = load(stem)
        rooms = load_rooms(root, stem)
        titles = {row["obj"]: row["title"] for row in rooms}
        decided, _ = room_scenes.decide(rooms)
        groups = {}
        for key, scene in blessed.items():
            obj = int(key)
            title = titles.get(obj, f"object {obj}")
            source = "rule" if decided.get(obj) == scene else "you"
            g = groups.setdefault((title, scene, source),
                                  {"title": title, "scene": scene,
                                   "source": source, "objs": []})
            g["objs"].append(obj)
        rows = sorted(groups.values(), key=lambda g: (g["title"], min(g["objs"])))
        for g in rows:
            g["objs"].sort()
        return render_template_string(TAGGED_PAGE, stem=stem, rows=rows,
                                      scenes=vocab.SCENES, left=len(review))

    @app.route("/verdict", methods=["POST"])
    def verdict():
        d = request.get_json(force=True)
        if d.get("scene") not in vocab.SCENE_INDEX:
            return jsonify(error="unknown scene"), 400
        group, _, review = take(d["story"], d["obj"], d["scene"])
        if group is None:
            return jsonify(error="unknown group"), 404
        return jsonify(**state(d["story"], review))

    @app.route("/skip", methods=["POST"])
    def skip():
        d = request.get_json(force=True)
        group, _, review = take(d["story"], d["obj"], None)
        if group is None:
            return jsonify(error="unknown group"), 404
        return jsonify(**state(d["story"], review))

    @app.route("/retag", methods=["POST"])
    def retag():
        d = request.get_json(force=True)
        if d.get("scene") not in vocab.SCENE_INDEX:
            return jsonify(error="unknown scene"), 400
        objs = d.get("objs") or []
        blessed, review = load(d["story"])
        remember(d["story"], objs, blessed, None, None)
        for o in objs:
            blessed[str(o)] = d["scene"]
        save(d["story"], blessed, review)
        return jsonify(**state(d["story"], review))

    @app.route("/undo", methods=["POST"])
    def undo():
        d = request.get_json(force=True)
        stack = history.get(d["story"]) or []
        if not stack:
            return jsonify(error="nothing to undo"), 409
        entry = stack.pop()
        blessed, review = load(d["story"])
        for key, prior in entry["prior"].items():
            if prior is None:
                blessed.pop(key, None)
            else:
                blessed[key] = prior
        if entry["group"] is not None:
            review = [g for g in review if g["obj"] != entry["group"]["obj"]]
            review.insert(entry["index"], entry["group"])
        save(d["story"], blessed, review)
        return jsonify(**state(d["story"], review))

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
