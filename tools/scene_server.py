#!/usr/bin/env python3
"""Review server for rooms the title rules refused.

Description: The human half of the tagging pipeline, in the shape of
    tools/art_server.py. Shows one refused group at a time -- title, captured
    description, and the scenes -- and writes the verdict to every object in
    the group, because a repeated title is a repeated place.

    Both JSON files are read-modify-written on every single verdict rather than
    held in memory and flushed at exit. A session's verdicts are exactly the
    kind of state this project has lost before, and a crash must cost at most
    the one decision in flight.

    Every write is reversible. Undo replays a per-story stack of prior values,
    Skip rotates a group to the back of the queue instead of dropping it, the
    /tagged page lists every verdict already given, and any room -- queued,
    tagged by rule or tagged by hand -- has its own page where its captured
    description can be reread and its tag changed or removed.

    The vocabulary can grow from that page. A room the 32 scenes do not
    describe is a real outcome, and the alternative to adding a scene is
    tagging it wrong forever.
Author: suinevere
Dependencies: flask, json, pathlib, re, sys, scene_vocab, room_scenes,
    scene_tracks
Globals: MOOD_TO_SCENES, NAME_RE
"""
import json
import pathlib
import re
import sys

from flask import Flask, jsonify, render_template_string, request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import room_scenes
import scene_tracks
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

NAME_RE = re.compile(r"^[A-Z][A-Z0-9_]{1,15}$")
"""NAME_RE

Description: What a new scene name may look like. Uppercase because the name
    becomes the C identifier `SC_<NAME>`, and short because it is also an
    8.3-adjacent directory name under tools/assets/png/<GAME>/.
Author: suinevere
"""


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
        turned back into a title and a description, which the /tagged list and
        every room page need. Missing or unreadable degrades to an empty
        inventory, which renders as bare object numbers rather than an error.
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


def generated_scene_n(root):
    """The SCENE_N the generated C header currently claims.

    Description: Adding a scene to the vocabulary leaves three generated
        tables a column short until the generators are re-run, and a build
        against the stale header fails in a way that does not mention the
        vocabulary at all. Comparing this against len(vocab.SCENES) is what
        lets the pages carry a banner that clears itself once the generators
        have run. A missing or unparsable header degrades to None, which
        reads as "cannot tell" and shows no banner.
    Author: suinevere
    Dependencies: pathlib, re
    Globals: N/A
    Params: root -- repo root
    Returns: the integer SCENE_N, or None
    """
    path = pathlib.Path(root) / "saturn" / "src" / "scene" / "scene_map.h"
    if not path.exists():
        return None
    try:
        found = re.search(r"#define\s+SCENE_N\s+(\d+)",
                          path.read_text(encoding="utf-8"))
    except OSError:
        return None
    return int(found.group(1)) if found else None


def append_scene(root, name, phrases):
    """Append one scene to the tagging vocabulary, on disk and in memory.

    Description: Appends, never inserts. scene_vocab.SCENES' order *is* the C
        enum value and a column index in three generated tables, so adding at
        the end is safe and moving anything silently repoints every row of
        every table. The name lands in SCENES and its search phrases in
        FETCH_NOUNS together, because art_queries.validate refuses a scene it
        cannot search, and a scene added to one but not the other would take
        the next fetch run down.

        Applies the same append to the live module object as well as the file,
        rather than reloading: a reload re-reads the module's own path, which
        is the shipping vocabulary even when this call was aimed at a test
        tree, and every reader goes through the module attribute, so rebinding
        it is what the reload would have achieved anyway.
    Author: suinevere
    Dependencies: pathlib, re, scene_vocab
    Globals: NAME_RE
    Params: root -- repo root; name -- the new scene name; phrases -- an
        iterable of stock-photo search phrases, empty for a default derived
        from the name
    Returns: (True, "") on success, or (False, reason)
    """
    if not NAME_RE.match(name or ""):
        return False, "a scene name is 2 to 16 characters of A-Z, 0-9 and _"
    if name in vocab.SCENE_INDEX:
        return False, f"{name} is already a scene"
    words = tuple(p.strip() for p in phrases if p and p.strip())
    if not words:
        words = (name.lower().replace("_", " "),)

    path = pathlib.Path(root) / "tools" / "scene_vocab.py"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return False, f"cannot read scene_vocab.py ({exc})"

    scenes_at = text.index("SCENES = (")
    scenes_end = text.index("\n)\n", scenes_at)
    text = text[:scenes_end] + f'\n    "{name}",' + text[scenes_end:]

    nouns_at = text.index("FETCH_NOUNS = {")
    nouns_end = text.index("\n}\n", nouns_at)
    row = '\n    "{}":{}({}),'.format(
        name, " " * max(1, 10 - len(name)),
        ", ".join(f'"{w}"' for w in words) + ("," if len(words) == 1 else ""))
    text = text[:nouns_end] + row + text[nouns_end:]

    try:
        path.write_text(text, encoding="utf-8")
    except OSError as exc:
        return False, f"cannot write scene_vocab.py ({exc})"
    vocab.SCENE_INDEX[name] = len(vocab.SCENES)
    vocab.SCENES = vocab.SCENES + (name,)
    vocab.FETCH_NOUNS[name] = words
    return True, ""


def _csv(names):
    """A scene list as the page shows it, and as it is typed back in.

    Description: One spelling for the empty case -- the empty string -- so a
        cleared field and an absent entry cannot look different.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: names -- an iterable of scene names, or None
    Returns: a comma-separated string, possibly empty
    """
    return ", ".join(sorted(names or ()))


def _parse_scenes(text):
    """Scene names from what someone typed against one track.

    Description: Accepts commas, spaces or both, and upper-cases, so a name
        typed in lower case is not a new scene. Raising rather than dropping:
        a name the page accepted and the vocabulary does not have would look
        authored here and be silence on the disc.
    Author: suinevere
    Dependencies: scene_vocab
    Globals: N/A
    Params: text -- the raw field value
    Returns: a sorted list of distinct scene names
    """
    out = set()
    for word in (text or "").replace(",", " ").split():
        name = word.strip().upper()
        if name not in vocab.SCENE_INDEX:
            raise ValueError(f"{word!r} is not a scene")
        out.add(name)
    return sorted(out)


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
 .stale{background:#fee;border-left:3px solid #c00;padding:.5rem .6rem;
        font-size:13px;margin:0 0 1rem}
 a{color:#06c}
</style>
{% if stale %}<p class="stale">{{ stale }}</p>{% endif %}
<h1>{{ stem }} <small id="left">{{ left }} left</small>
  — <a href="/game/{{ stem }}/tagged">review {{ tagged }} already tagged</a>
  · <a href="/game/{{ stem }}/tracks">music</a></h1>
<div id="legend">
 One click = one tag = this room's whole picture and music. There is no second
 tag and no overlap.<br>
 <b>Yellow</b> buttons are the retired mood classifier's guess for this exact
 room, narrowed to a handful of scenes. A hint, not a limit — any of the
 {{ scenes|length }} is a legal answer, and yellow is often wrong.<br>
 <b>Skip</b> sends the room to the back of the queue, it does not drop it.
 <b>Back</b> undoes the last verdict or skip and puts you on it again.
</div>
<h2 id="title"><span id="title-text">{{ group.title if group else 'queue clear' }}</span>
  {% if hint_mood %}<small id="hint-mood">(was {{ hint_mood }})</small>{% endif %}</h2>
<div id="scope">{{ scope }}</div>
<div id="desc">{{ group.description or '(no description captured)' if group else '' }}</div>
<p><a id="roomlink" href="{{ '/game/' ~ stem ~ '/room/' ~ group.obj if group else '#' }}">
  open this room's own page (add a scene, or change it later) →</a></p>
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
  document.getElementById('roomlink').href =
      d.group ? '/game/{{ stem }}/room/' + d.group.obj : '#';
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
 .stale{background:#fee;border-left:3px solid #c00;padding:.5rem .6rem;
        font-size:13px;margin:0 0 1rem}
 a{color:#06c}
</style>
{% if stale %}<p class="stale">{{ stale }}</p>{% endif %}
<h1>{{ stem }} — {{ rows|length }} tagged
  <small>(<a href="/game/{{ stem }}">back to the queue, {{ left }} left</a>)</small></h1>
<p style="color:#555;font-size:13px">Change a tag here and it is written
immediately; pick <b>— unset —</b> to untag a room and send it back to the
queue. Click a title to open that room, reread its captured description and
retag it there. <b>rule</b> means the title rules decided it and no human has
looked; <b>you</b> means someone chose it. Changing a rule verdict pins it, so
re-running <code>tools/room_scenes.py</code> will never overwrite it again.</p>
<table>
<tr><th>title</th><th>objects</th><th>scene</th><th>source</th></tr>
{% for r in rows %}
<tr><td><a href="/game/{{ stem }}/room/{{ r.objs[0] }}">{{ r.title }}</a></td>
  <td class="src">{% for o in r.objs %}<a
    href="/game/{{ stem }}/room/{{ o }}">{{ o }}</a>{{ ", " if not loop.last }}{% endfor %}</td>
  <td><select onchange="retag(this, {{ r.objs|tojson }})">
    <option value="">— unset —</option>
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
    body: JSON.stringify({story: '{{ stem }}', objs: objs,
                          scene: sel.value || null})});
  if (!r.ok) { alert('rejected'); return; }
  if (!sel.value) { sel.closest('tr').remove(); return; }
  const cell = sel.closest('tr').lastElementChild;
  cell.textContent = 'you';
  cell.className = 'src changed';
}
</script>"""


ROOM_PAGE = """<!doctype html><title>{{ stem }} — {{ room.title }}</title>
<style>
 body{font:15px system-ui;margin:2rem;max-width:56rem}
 #desc{color:#444;line-height:1.5;margin:.5rem 0 1rem;white-space:pre-wrap}
 .grid{display:grid;grid-template-columns:repeat(4,1fr);gap:.4rem}
 button{padding:.6rem;font:inherit;cursor:pointer}
 .hint{background:#ffd;font-weight:600}
 .current{outline:3px solid #4a8;font-weight:700}
 #scope{color:#a00;font-size:13px;margin:0 0 .8rem}
 .stale{background:#fee;border-left:3px solid #c00;padding:.5rem .6rem;
        font-size:13px;margin:0 0 1rem}
 fieldset{margin:1.4rem 0 0;border:1px solid #ddd;padding:.6rem .8rem}
 legend{font-size:13px;color:#666}
 input{font:inherit;padding:.3rem}
 #note{margin-top:.8rem;color:#060;font-size:13px}
 a{color:#06c}
</style>
{% if stale %}<p class="stale">{{ stale }}</p>{% endif %}
<p><a href="/game/{{ stem }}">← queue ({{ left }} left)</a> ·
   <a href="/game/{{ stem }}/tagged">tagged list</a> ·
   <a href="/game/{{ stem }}/tracks">music</a></p>
<h1>{{ room.title }}
  {% if hint_mood %}<small style="color:#888">(was {{ hint_mood }})</small>{% endif %}</h1>
<p style="color:#666;font-size:13px">object {{ room.obj }} ·
  currently <b id="current">{{ current or 'untagged' }}</b>
  {% if queued %}· still in the review queue{% endif %}</p>
<div id="desc">{{ room.description or '(no description captured)' }}</div>
<p id="scope">
  <label><input type="checkbox" id="only" {{ 'checked' if siblings|length < 2 }}
    {{ 'disabled' if siblings|length < 2 }}> this room only</label>
  {% if siblings|length > 1 %}— otherwise it applies to all {{ siblings|length }} rooms titled “{{ room.title }}” ({{ siblings|join(', ') }}){% endif %}
</p>
<div class="grid">
{% for s in scenes %}<button data-scene="{{ s }}"
  class="{{ 'hint' if s in hint_scenes else '' }}{{ ' current' if s == current else '' }}"
  onclick="retag('{{ s }}')">{{ s }}</button>{% endfor %}
</div>
<p><button onclick="retag(null)">— unset —</button>
  <small style="color:#666">drops your verdict. The room returns to the review
  queue, unless the title rules have an answer of their own, in which case it
  reverts to that.</small></p>
<fieldset><legend>none of these describe it?</legend>
  <p style="color:#666;font-size:13px;margin:.2rem 0 .6rem">Adds a scene to the
  vocabulary and tags this room with it. Appending is safe; the new scene lands
  at the end of the enum and gets its own art folder. You will need to re-run
  both generators afterwards — the banner will say so.</p>
  <input id="newname" placeholder="MISTROOM" size="14">
  <input id="newphrases" placeholder="search phrases, comma separated" size="40">
  <button onclick="addScene()">add scene and tag this room</button>
</fieldset>
<p id="note"></p>
<script>
const OBJ = {{ room.obj }};
const SIBS = {{ siblings|tojson }};
function targets() {
  return document.getElementById('only').checked ? [OBJ] : SIBS;
}
async function retag(scene) {
  const r = await fetch('/retag', {method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({story: '{{ stem }}', objs: targets(), scene: scene})});
  if (!r.ok) { alert('rejected'); return; }
  document.getElementById('current').textContent = scene || 'untagged';
  document.querySelectorAll('button[data-scene]').forEach(function (b) {
    b.classList.toggle('current', b.dataset.scene === scene);
  });
  document.getElementById('note').textContent =
      scene ? ('tagged ' + targets().length + ' room(s) ' + scene)
            : 'untagged; back in the queue';
}
async function addScene() {
  const name = document.getElementById('newname').value.trim().toUpperCase();
  const phrases = document.getElementById('newphrases').value;
  const r = await fetch('/scene/new', {method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({story: '{{ stem }}', objs: targets(),
                          name: name, phrases: phrases})});
  const d = await r.json();
  if (!r.ok) { document.getElementById('note').textContent = d.error; return; }
  location.reload();
}
</script>"""


TRACKS_PAGE = """<!doctype html><title>music</title>
<style>
 body{font:15px system-ui;margin:2rem;max-width:66rem}
 .cols{display:flex;gap:2rem;align-items:flex-start}
 table{border-collapse:collapse}
 td,th{border-bottom:1px solid #ddd;padding:.3rem .5rem;text-align:left}
 th{font-size:13px;color:#666}
 input{font:inherit;padding:.25rem;width:22rem}
 .num{color:#888;width:3rem}
 .none{color:#a60;font-size:13px}
 .one{color:#060;font-size:13px}
 .many{color:#06c;font-size:13px}
 .stale{background:#fee;border-left:3px solid #c00;padding:.5rem .6rem;
        font-size:13px;margin:0 0 1rem}
 .note{color:#555;font-size:13px;line-height:1.5;border-left:3px solid #cde;
       padding-left:.6rem;margin:0 0 1rem}
 a{color:#06c}
</style>
{% if stale %}<p class="stale">{{ stale }}</p>{% endif %}
{% if stem %}<p><a href="/game/{{ stem }}">← {{ stem }} queue ({{ left }} left)</a> ·
   <a href="/game/{{ stem }}/tagged">tagged list</a></p>{% endif %}
<h1>Music — which scenes each track belongs to</h1>
<div class="note">
 One row per CD-DA track. Type the scene tags that track should play
 under, comma separated; clear the field to retire the track.<br>
 <b>Not per game.</b> The thirty-one tracks are most of the disc and every
 story shares them, so a scene sounds the same whichever game is loaded.<br>
 A scene named by <b>exactly one</b> track is static — it always plays that
 track. Named by several, the engine picks among them. Named by none, it
 falls back to the neutral pool.<br>
 Saved immediately; <code>tools/gen_scene_tables.py</code> compiles it.
</div>
<div class="cols">
<table><tr><th>track</th><th>scenes</th></tr>
{% for r in rows %}
<tr><td class="num">{{ r.track }}</td>
  <td><input value="{{ r.scenes }}" data-track="{{ r.track }}"
        onchange="save(this)" placeholder="FOREST, ROCKY"></td></tr>
{% endfor %}
</table>
<table><tr><th>scene</th><th>plays</th></tr>
{% for p in plays %}
<tr><td>{{ p.scene }}</td>
  <td id="plays-{{ p.scene }}" class="x">{{ p.tracks or "neutral pool" }}</td></tr>
{% endfor %}
</table>
</div>
<script>
function paint() {
  document.querySelectorAll("[id^=plays-]").forEach(function (td) {
    var n = td.textContent.split(",").filter(function (x) {
      return x.trim() && x.indexOf("neutral") === -1; }).length;
    td.className = n === 0 ? "none" : (n === 1 ? "one" : "many");
  });
}
async function save(input) {
  const r = await fetch("/tracks", {method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify({track: input.dataset.track,
                          scenes: input.value})});
  const d = await r.json();
  if (!r.ok) { alert(d.error); return; }
  input.value = d.written;
  for (const scene in d.plays) {
    const td = document.getElementById("plays-" + scene);
    if (td) td.textContent = d.plays[scene] || "neutral pool";
  }
  paint();
}
paint();
</script>"""


def create_app(repo=None):
    """Build the review app, rooted at repo so tests can point it elsewhere.

    Description: The review app, rooted at `repo` so tests can point it at a
        temporary tree instead of the working copy. Holds one undo stack per
        story for the life of the process; the durable way back to an older
        verdict is the /tagged page and the per-room pages, not this stack.
    Author: suinevere
    Dependencies: flask, scene_vocab, room_scenes, art_nouns
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

    def stale_note():
        want = len(vocab.SCENES)
        have = generated_scene_n(root)
        if have is not None and have != want:
            return (f"The vocabulary has {want} scenes but the generated C header "
                    f"still says SCENE_N {have}. Re-run "
                    f"tools/gen_scene_tables.py and tools/make_tga.py before the "
                    f"next disc build.")
        if scene_tracks.is_stale(root):
            return ("The music table is behind tools/assets/tracks.json. Re-run "
                    "tools/gen_scene_tables.py before the next disc build.")
        return ""

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

    def rebuild_review(stem, blessed):
        """Re-derive the queue from the inventory for the current blessed set.

        Description: Untagging a room must put it back in front of the
            reviewer, and the only authority on whether a room belongs in the
            queue is room_scenes: it belongs there when the title rules refuse
            it and no human has ruled. Re-deriving rather than hand-inserting
            keeps the two files' one invariant -- no object both tagged and
            queued -- true by construction.
        """
        rooms = load_rooms(root, stem)
        if not rooms:
            return None
        decided, refused = room_scenes.decide(rooms)
        existing = {int(k): v for k, v in blessed.items()}
        merged, review = room_scenes.merge(existing, decided, refused)
        return {str(k): v for k, v in sorted(merged.items())}, review

    def remember(stem, objs, blessed, group, index, rebuild=False):
        history.setdefault(stem, []).append(
            {"prior": {str(o): blessed.get(str(o)) for o in objs},
             "group": group, "index": index, "rebuild": rebuild})

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

    def apply_tag(stem, objs, scene):
        """Write or clear one tag across a set of objects, undoably."""
        blessed, review = load(stem)
        remember(stem, objs, blessed, None, None, rebuild=True)
        for o in objs:
            if scene is None:
                blessed.pop(str(o), None)
            else:
                blessed[str(o)] = scene
        rebuilt = rebuild_review(stem, blessed)
        if rebuilt is not None:
            blessed, review = rebuilt
        save(stem, blessed, review)
        return blessed, review

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
                                      tagged=len(blessed), stale=stale_note(),
                                      **s)

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
                                      scenes=vocab.SCENES, left=len(review),
                                      stale=stale_note())

    @app.route("/game/<stem>/room/<int:obj>")
    def room(stem, obj):
        """One room: its captured description, its tag, and every way to change it.

        Description: Reachable for any room in the inventory, queued or
            tagged, by rule or by hand -- a decision made three hundred rooms
            ago is worth as much as the one in front of you, and the queue
            alone can never return you to it.
        Author: suinevere
        Dependencies: flask, scene_vocab, room_scenes
        Globals: N/A
        Params: stem -- story stem; obj -- Z-machine object number
        Returns: rendered HTML; 404 when the story or object is unknown
        """
        from flask import abort
        rooms = load_rooms(root, stem)
        row = next((r for r in rooms if r["obj"] == obj), None)
        if row is None:
            abort(404)
        blessed, review = load(stem)
        siblings = sorted(r["obj"] for r in rooms
                          if r["title"] == row["title"])
        hint_mood, hint_scenes = hint_for(stem, {"title": row["title"]})
        queued = any(obj in g["objs"] for g in review)
        return render_template_string(
            ROOM_PAGE, stem=stem, room=row, scenes=vocab.SCENES,
            current=blessed.get(str(obj)), siblings=siblings, queued=queued,
            hint_mood=hint_mood, hint_scenes=hint_scenes, left=len(review),
            stale=stale_note())

    @app.route("/tracks")
    @app.route("/game/<stem>/tracks")
    def tracks_page(stem=None):
        """Author which scenes each CD-DA track belongs to.

        Description: One row per track, because that is the decision being
            made: you listen to track 17 and say where it belongs. The
            scene-first view is the derived one and appears alongside as the
            column that says what each scene will actually play.

            Not per game. The thirty-one tracks are already most of the disc
            and every story shares them, so a scene sounds the same whichever
            game is loaded; `stem` only decides where the back links point.
        Author: suinevere
        Dependencies: flask, scene_tracks, scene_vocab
        Globals: N/A
        Params: stem -- a story stem for the navigation links, or None
        Returns: rendered HTML
        """
        data = scene_tracks.load(root)
        inverted = scene_tracks.by_scene(data)
        rows = [{"track": t, "scenes": _csv(data.get(t))}
                for t in scene_tracks.tracks()]
        plays = [{"scene": scene,
                  "tracks": ", ".join(str(t) for t in inverted.get(scene, ()))}
                 for scene in vocab.SCENES]
        return render_template_string(
            TRACKS_PAGE, stem=stem, rows=rows, plays=plays, stale=stale_note(),
            left=len(load(stem)[1]) if stem else 0)

    @app.route("/tracks", methods=["POST"])
    def tracks_write():
        """Write one track's scene list.

        Description: Replaces that track's list outright; a track names its
            scenes and nothing else needs touching, which is the whole reason
            the document is keyed this way round. An empty value drops the
            track. Rejects an unknown scene rather than dropping it, since a
            name that compiles to no bit would look authored on the page and
            be silence on the disc.
        Author: suinevere
        Dependencies: flask, scene_tracks, scene_vocab
        Globals: N/A
        Params: N/A -- reads {"track", "scenes"} from the body
        Returns: the written value and every scene's refreshed track list; 400
            for an unknown scene or a track the disc does not have
        """
        d = request.get_json(force=True)
        try:
            track = int(d.get("track"))
        except (TypeError, ValueError):
            return jsonify(error="not a track number"), 400
        if not scene_tracks.TRACK_MIN <= track <= scene_tracks.TRACK_MAX:
            return jsonify(error=f"track {track} is outside "
                                 f"{scene_tracks.TRACK_MIN}.."
                                 f"{scene_tracks.TRACK_MAX}"), 400
        try:
            scenes = _parse_scenes(d.get("scenes"))
        except ValueError as exc:
            return jsonify(error=str(exc)), 400

        data = scene_tracks.load(root)
        if scenes:
            data[track] = scenes
        else:
            data.pop(track, None)
        scene_tracks.save(root, data)

        inverted = scene_tracks.by_scene(scene_tracks.load(root))
        return jsonify(
            written=_csv(scenes), stale=stale_note(),
            plays={scene: ", ".join(str(t) for t in inverted.get(scene, ()))
                   for scene in vocab.SCENES})

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
        """Set or clear the tag on a set of objects.

        Description: A null scene is UNSET, not an error: it drops the
            blessed entries and re-derives the queue, so the room comes back
            to be decided again rather than silently becoming untagged and
            unreachable.
        Author: suinevere
        Dependencies: flask, scene_vocab
        Globals: N/A
        Params: N/A -- reads {"story", "objs", "scene"} from the body
        Returns: the story's refreshed queue state; 400 for a scene that is
            not in the vocabulary and not null
        """
        d = request.get_json(force=True)
        scene = d.get("scene")
        if scene is not None and scene not in vocab.SCENE_INDEX:
            return jsonify(error="unknown scene"), 400
        _, review = apply_tag(d["story"], d.get("objs") or [], scene)
        return jsonify(**state(d["story"], review))

    @app.route("/scene/new", methods=["POST"])
    def scene_new():
        """Add a scene to the vocabulary and tag the given objects with it.

        Description: The two halves are one action on purpose. A scene added
            and not used is a column of zeros in three generated tables and an
            empty art folder; the reason to add one is always the room in
            front of you.
        Author: suinevere
        Dependencies: flask, scene_vocab
        Globals: N/A
        Params: N/A -- reads {"story", "objs", "name", "phrases"} from the body
        Returns: the refreshed queue state plus the new name; 400 with a
            reason when the name is malformed or already taken
        """
        d = request.get_json(force=True)
        name = (d.get("name") or "").strip().upper()
        phrases = (d.get("phrases") or "").split(",")
        ok, why = append_scene(root, name, phrases)
        if not ok:
            return jsonify(error=why), 400
        _, review = apply_tag(d["story"], d.get("objs") or [], name)
        return jsonify(name=name, stale=stale_note(),
                       **state(d["story"], review))

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
        if entry.get("rebuild"):
            rebuilt = rebuild_review(d["story"], blessed)
            if rebuilt is not None:
                blessed, review = rebuilt
        elif entry["group"] is not None:
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
