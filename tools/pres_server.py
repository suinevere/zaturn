#!/usr/bin/env python3
"""Review server for assigning a picture and a CD-DA track to every room.

Description: The one review app, replacing the retired art_server (which
    curated downloaded pictures per category) and scene_server (which tagged
    rooms with a category). Neither question exists any more: there is exactly
    one supply of art -- Zork I's 74 room backgrounds and 31 CD-DA tracks --
    and the only remaining decision is which of them each room of the other
    thirty games gets.

    Zork I itself is not reviewable here. Its 110 rooms are measured off the
    original disc by tools/gen_presentation.py, and an opinion competing with a
    measurement is worth less than the measurement. It is shown read-only, as
    the reference the suggestions are derived from.

    Every verdict is written through to disk immediately (see pres_store.save),
    and every write is reversible: Undo replays the per-game stack, and any
    room can be revisited and changed from the game's own page. A session's
    verdicts are exactly the kind of state this project has lost before, and a
    crash must cost at most the one decision in flight.

    Suggestions are shown WITH their evidence, never bare. "4 of 4 Zork I
    FOREST rooms took this picture" and "2 of 13 CAVE rooms took this one" are
    both suggestions, and an interface that rendered them identically would be
    lying about one of them. Four confidence levels are distinguished and
    coloured: strong (the scene's rooms mostly agreed), weak (they did not),
    analogue (Zork I never had a room of this kind, so a human picked a visual
    stand-in), and none (no scene tag and no title rule matched).
Author: suinevere
Dependencies: flask, json, pathlib, sys, pres_store, scene_vocab
Globals: ROOT, PNG_DIR, app
Run: tools/.venv/Scripts/python.exe tools/pres_server.py   (serves on 8080)
"""
import json
import pathlib
import sys

from flask import Flask, abort, jsonify, render_template_string, request, send_from_directory

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import pres_store as store

ROOT = pathlib.Path(__file__).resolve().parent.parent
PNG_DIR = ROOT / "analysis" / "zork_bg" / "png"

app = Flask(__name__)

PAGE = """<!doctype html>
<meta charset="utf-8"><title>{{ title }}</title>
<style>
 :root{--bg:#14161a;--fg:#e8e6e3;--dim:#9aa0a6;--line:#2a2f36;--card:#1b1f25;
       --strong:#3fb950;--weak:#d29922;--analogue:#a371f7;--none:#6e7681;--pick:#58a6ff}
 *{box-sizing:border-box}
 body{margin:0;background:var(--bg);color:var(--fg);
      font:14px/1.5 ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
 header{position:sticky;top:0;background:var(--bg);border-bottom:1px solid var(--line);
        padding:10px 16px;display:flex;gap:14px;align-items:center;flex-wrap:wrap;z-index:9}
 a{color:var(--pick);text-decoration:none} a:hover{text-decoration:underline}
 .wrap{padding:16px;max-width:1400px;margin:0 auto}
 .bar{height:8px;background:var(--line);border-radius:4px;overflow:hidden;flex:1;min-width:120px}
 .bar>i{display:block;height:100%;background:var(--strong)}
 table{border-collapse:collapse;width:100%}
 th,td{border-bottom:1px solid var(--line);padding:6px 8px;text-align:left;vertical-align:top}
 th{color:var(--dim);font-weight:600;font-size:12px;text-transform:uppercase;letter-spacing:.04em}
 tr:hover td{background:#1a1e24}
 .tag{display:inline-block;padding:1px 7px;border-radius:10px;font-size:11px;font-weight:600}
 .c-strong{background:#12341c;color:var(--strong)} .c-weak{background:#3a2d0c;color:var(--weak)}
 .c-analogue{background:#2b1d43;color:var(--analogue)} .c-none{background:#23262b;color:var(--none)}
 .c-set{background:#0d2b45;color:var(--pick)}
 .card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:14px;margin-bottom:14px}
 .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(172px,1fr));gap:10px}
 .thumb{border:2px solid transparent;border-radius:6px;overflow:hidden;cursor:pointer;background:#000}
 .thumb.sel{border-color:var(--pick)}
 .thumb img{width:100%;display:block}
 .thumb .cap{padding:4px 6px;font-size:11px;color:var(--dim)}
 .desc{color:var(--dim);max-width:70ch}
 select,button{background:#22262c;color:var(--fg);border:1px solid var(--line);
               border-radius:6px;padding:6px 10px;font:inherit}
 button{cursor:pointer} button:hover{border-color:var(--pick)}
 .row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
 .muted{color:var(--dim)} .big{font-size:17px;font-weight:600}
 img.prev{width:320px;border-radius:6px;border:1px solid var(--line);background:#000}
</style>
<header>
  <span class="big"><a href="/">Room presentation</a></span>
  {{ header|safe }}
</header>
<div class="wrap">{{ body|safe }}</div>
{{ script|safe }}
"""


def render(title, header, body, script=""):
    """Renders one page in the shared chrome."""
    return render_template_string(PAGE, title=title, header=header, body=body,
                                  script=script)


def conf_tag(c):
    """A confidence as a coloured pill."""
    label = {"strong": "strong", "weak": "weak", "analogue": "analogue",
             "none": "no basis"}.get(c, c)
    return f'<span class="tag c-{c}">{label}</span>'


@app.route("/png/<name>")
def png(name):
    """/*----------------------
     | png
     | Description: Serves one picture from analysis/zork_bg/png. Served from
     |     there rather than copied into a static folder because that directory
     |     is the extracted output of the original disc and is already the
     |     record of what each picture looks like; a copy would be a second
     |     thing to keep in step.
     | Author: suinevere
     | Dependencies: flask
     | Globals: PNG_DIR
     | Params: name -- the PNG filename
     | Returns: the image
     ----------------------*/"""
    if "/" in name or "\\" in name or not name.endswith(".png"):
        abort(404)
    return send_from_directory(PNG_DIR, name)


def game_progress(stem, pool):
    """/*----------------------
     | game_progress
     | Description: How much of one game is decided, and how its undecided rooms
     |     break down by the confidence of the suggestion waiting for them --
     |     which is what says whether the remaining work is a quick accept-all
     |     or a real sit-down.
     | Author: suinevere
     | Dependencies: pres_store
     | Globals: N/A
     | Params: stem -- the story stem; pool -- the catalogue
     | Returns: a dict of counts
     ----------------------*/"""
    rooms = store.rooms(stem)
    tags = store.scenes(stem)
    saved = store.load(stem)["rooms"]
    out = {"total": len(rooms), "done": 0,
           "strong": 0, "weak": 0, "analogue": 0, "none": 0}
    for r in rooms:
        if str(r["obj"]) in saved:
            out["done"] += 1
            continue
        scene, origin = store.scene_of(r["obj"], r["title"], tags)
        s = store.suggest(scene, pool["scene_defaults"], origin)
        out[s["confidence"]] += 1
    return out


@app.route("/")
def index():
    """/*----------------------
     | index
     | Description: Every game, how far along it is, and what the remaining work
     |     looks like.
     | Author: suinevere
     | Dependencies: flask, pres_store
     | Globals: N/A
     | Params: N/A
     | Returns: the page
     ----------------------*/"""
    pool = store.pool()
    rows = []
    tot = done = 0
    for stem in store.games():
        p = game_progress(stem, pool)
        tot += p["total"]
        done += p["done"]
        pct = (100 * p["done"]) // p["total"] if p["total"] else 0
        rows.append(f"""<tr>
          <td><a href="/g/{stem}">{stem}</a></td>
          <td>{p['done']} / {p['total']}</td>
          <td style="min-width:160px"><div class="bar"><i style="width:{pct}%"></i></div></td>
          <td>{conf_tag('strong')} {p['strong']}</td>
          <td>{conf_tag('weak')} {p['weak']}</td>
          <td>{conf_tag('analogue')} {p['analogue']}</td>
          <td>{conf_tag('none')} {p['none']}</td>
        </tr>""")

    pct = (100 * done) // tot if tot else 0
    body = f"""
    <div class="card">
      <div class="row"><span class="big">{done} of {tot} rooms decided</span>
      <div class="bar" style="max-width:320px"><i style="width:{pct}%"></i></div></div>
      <p class="muted">Every picture and track comes from Zork I's original Saturn
      release: {len(pool['images'])} room backgrounds and {len(pool['tracks'])} CD-DA
      tracks. Zork I's own 110 rooms are measured off that disc and are not
      assigned here &mdash; see <a href="/reference">the reference</a>, which is
      where these suggestions come from.</p>
    </div>
    <table><tr><th>Game</th><th>Decided</th><th></th>
    <th colspan="4">Undecided, by strength of the waiting suggestion</th></tr>
    {''.join(rows)}</table>"""
    return render("Room presentation", "", body)


@app.route("/reference")
def reference():
    """/*----------------------
     | reference
     | Description: What Zork I did, per scene tag -- the whole evidential basis
     |     for every suggestion this app makes, shown plainly so a suggestion can
     |     be argued with rather than merely accepted.
     | Author: suinevere
     | Dependencies: flask, pres_store
     | Globals: N/A
     | Params: N/A
     | Returns: the page
     ----------------------*/"""
    pool = store.pool()
    by_index = {i["index"]: i for i in pool["images"]}
    rows = []
    for scene, d in sorted(pool["scene_defaults"].items(),
                           key=lambda kv: (kv[1]["source"], -kv[1]["n"])):
        img = by_index.get(d["image"], {})
        conf = ("analogue" if d["source"] == "analogue"
                else "strong" if d["n"] and (100 * d["image_support"]) // d["n"] >= 60
                else "weak")
        basis = ("no Zork I room carries this tag &mdash; hand-picked stand-in"
                 if d["source"] == "analogue"
                 else f"{d['image_support']} of {d['n']} rooms took it; "
                      f"{d['track_support']} of {d['n']} took track {d['track']}")
        rows.append(f"""<tr>
          <td><b>{scene}</b></td><td>{conf_tag(conf)}</td>
          <td><img src="/png/{img.get('png','')}" style="width:128px;border-radius:4px"></td>
          <td>#{d['image']} {img.get('png','')}<br><span class="muted">{
              ', '.join(img.get('rooms', [])[:3])}</span></td>
          <td>track {d['track']}</td>
          <td class="muted">{basis}</td>
        </tr>""")

    body = f"""<div class="card"><p>Zork I is the only game with both a scene tag
    and a measured picture on every room, so it is the only evidence there is for
    what a room of a given kind should look like. Fourteen of the thirty-two
    scenes appear in it; the other eighteen carry a hand-picked visual analogue
    instead, marked as such.</p></div>
    <table><tr><th>Scene</th><th>Basis</th><th>Picture</th><th></th>
    <th>Track</th><th>Evidence</th></tr>{''.join(rows)}</table>"""
    return render("Reference", '<a href="/">&larr; all games</a>', body)


@app.route("/g/<stem>")
def game(stem):
    """/*----------------------
     | game
     | Description: One game's rooms, each with its current verdict or the
     |     suggestion waiting for it. The whole game on one page rather than a
     |     one-room-at-a-time queue, because assigning a picture is a comparative
     |     judgement -- neighbouring rooms of a game should mostly agree, and a
     |     queue hides exactly that.
     | Author: suinevere
     | Dependencies: flask, pres_store
     | Globals: N/A
     | Params: stem -- the story stem
     | Returns: the page
     ----------------------*/"""
    if stem not in store.games():
        abort(404)
    pool = store.pool()
    by_index = {i["index"]: i for i in pool["images"]}
    tags = store.scenes(stem)
    saved = store.load(stem)["rooms"]

    rows = []
    for r in store.rooms(stem):
        key = str(r["obj"])
        scene, origin = store.scene_of(r["obj"], r["title"], tags)
        sug = store.suggest(scene, pool["scene_defaults"], origin)
        cur = saved.get(key)
        image = cur["image"] if cur else sug["image"]
        track = cur["track"] if cur else sug["track"]
        img = by_index.get(image, {})
        state = ('<span class="tag c-set">set</span>' if cur
                 else conf_tag(sug["confidence"]))
        thumb = (f'<img src="/png/{img["png"]}" style="width:96px;border-radius:4px">'
                 if img else '<span class="muted">&mdash;</span>')
        rows.append(f"""<tr id="r{r['obj']}">
          <td class="muted">{r['obj']}</td>
          <td><b>{r['title']}</b><br>
              <span class="muted">{scene or 'untagged'}</span></td>
          <td>{state}</td>
          <td><a href="/g/{stem}/{r['obj']}">{thumb}</a></td>
          <td>#{image or '-'}<br><span class="muted">track {track}</span></td>
          <td class="desc">{(r['description'] or '')[:190]}</td>
          <td><button onclick="accept({r['obj']},{image},{track})"
                 {'disabled' if cur or not image else ''}>Accept</button>
              <a href="/g/{stem}/{r['obj']}">Choose</a></td>
        </tr>""")

    p = game_progress(stem, pool)
    hdr = (f'<a href="/">&larr; all games</a> <span class="muted">{stem}</span>'
           f' <span class="muted">{p["done"]}/{p["total"]} decided</span>'
           f' <button onclick="acceptAll()">Accept every strong suggestion</button>'
           f' <button onclick="undo()">Undo</button>')

    script = f"""<script>
    async function post(u,b){{const r=await fetch(u,{{method:'POST',
      headers:{{'Content-Type':'application/json'}},body:JSON.stringify(b)}});
      if(!r.ok){{alert('failed');return null}} return r.json()}}
    async function accept(obj,image,track){{
      await post('/api/assign',{{game:'{stem}',obj:obj,image:image,track:track}});
      location.reload()}}
    async function acceptAll(){{
      if(!confirm('Accept the suggestion for every undecided room whose basis is strong?'))return;
      await post('/api/accept_strong',{{game:'{stem}'}}); location.reload()}}
    async function undo(){{await post('/api/undo',{{game:'{stem}'}}); location.reload()}}
    </script>"""

    body = f"""<table><tr><th>Obj</th><th>Room</th><th>Basis</th><th>Picture</th>
      <th>Choice</th><th>Description</th><th></th></tr>{''.join(rows)}</table>"""
    return render(stem, hdr, body, script)


@app.route("/g/<stem>/<int:obj>")
def room(stem, obj):
    """/*----------------------
     | room
     | Description: One room, with every picture in the pool laid out to choose
     |     from and every track offered by name. The suggestion is shown with the
     |     evidence behind it rather than pre-applied, so accepting it is a
     |     decision rather than a default.
     | Author: suinevere
     | Dependencies: flask, pres_store
     | Globals: N/A
     | Params: stem -- the story stem; obj -- the room's object number
     | Returns: the page
     ----------------------*/"""
    if stem not in store.games():
        abort(404)
    pool = store.pool()
    rec = next((r for r in store.rooms(stem) if r["obj"] == obj), None)
    if rec is None:
        abort(404)

    tags = store.scenes(stem)
    scene, origin = store.scene_of(obj, rec["title"], tags)
    sug = store.suggest(scene, pool["scene_defaults"], origin)
    cur = store.load(stem)["rooms"].get(str(obj))
    image = cur["image"] if cur else sug["image"]
    track = cur["track"] if cur else sug["track"]

    thumbs = []
    for i in pool["images"]:
        sel = " sel" if i["index"] == image else ""
        used = ", ".join(i["rooms"][:2])
        thumbs.append(f"""<div class="thumb{sel}" id="t{i['index']}"
             onclick="pick({i['index']})">
          <img src="/png/{i['png']}" loading="lazy">
          <div class="cap">#{i['index']} {i['area']} &mdash; {used}</div></div>""")

    opts = []
    for t in pool["tracks"]:
        selected = " selected" if t["track"] == track else ""
        label = ("silence" if t["track"] == 0
                 else f"track {t['track']} &mdash; {t['length']} &mdash; {t['role'][:54]}")
        opts.append(f'<option value="{t["track"]}"{selected}>{label}</option>')

    hdr = (f'<a href="/g/{stem}">&larr; {stem}</a>'
           f' <span class="muted">object {obj}</span>')
    body = f"""
    <div class="card">
      <div class="big">{rec['title']}</div>
      <p class="desc">{rec['description']}</p>
      <p class="muted">scene tag: <b>{scene or 'none'}</b>
         ({'read off the title' if origin == 'title' else 'stored' if scene else 'none'})</p>
      <div class="row">{conf_tag(sug['confidence'])}
        <span class="muted">{sug['why']}</span></div>
    </div>
    <div class="card">
      <div class="row">
        <label>Track <select id="track">{''.join(opts)}</select></label>
        <button onclick="save()">Save</button>
        <button onclick="clearPic()">No picture (hold what is showing)</button>
        <span class="muted" id="pickmsg">picture #<b id="pick">{image or '-'}</b></span>
      </div>
    </div>
    <div class="grid">{''.join(thumbs)}</div>
    <script>
    let chosen = {image or 0};
    function pick(i){{
      document.querySelectorAll('.thumb').forEach(e=>e.classList.remove('sel'));
      const el=document.getElementById('t'+i); if(el) el.classList.add('sel');
      chosen=i; document.getElementById('pick').textContent=i;
    }}
    function clearPic(){{chosen=0;
      document.querySelectorAll('.thumb').forEach(e=>e.classList.remove('sel'));
      document.getElementById('pick').textContent='-';}}
    async function save(){{
      const t=parseInt(document.getElementById('track').value,10);
      const r=await fetch('/api/assign',{{method:'POST',
        headers:{{'Content-Type':'application/json'}},
        body:JSON.stringify({{game:'{stem}',obj:{obj},image:chosen,track:t}})}});
      if(!r.ok){{alert('save failed');return}}
      location.href='/g/{stem}#r{obj}';
    }}
    </script>"""
    return render(f"{stem} {rec['title']}", hdr, body)


@app.route("/api/assign", methods=["POST"])
def api_assign():
    """/*----------------------
     | api_assign
     | Description: Records one room's verdict.
     |     Refuses an object number that is not one of this game's rooms. The
     |     store would accept it and gen_presentation.py would happily emit it --
     |     a row for an object the story never uses, which nothing would ever
     |     read and nothing would ever report as wrong.
     | Author: suinevere
     | Dependencies: flask, pres_store
     | Globals: N/A
     | Params: N/A (JSON body: game, obj, image, track)
     | Returns: the new counts
     ----------------------*/"""
    b = request.get_json(force=True)
    stem = b.get("game")
    if stem not in store.games():
        abort(400)
    obj = int(b["obj"])
    if obj not in {r["obj"] for r in store.rooms(stem)}:
        abort(400)
    image, track = int(b.get("image", 0)), int(b.get("track", 0))
    pool = store.pool()
    if image and not any(i["index"] == image for i in pool["images"]):
        abort(400)
    if not any(t["track"] == track for t in pool["tracks"]):
        abort(400)
    store.assign(stem, obj, image, track)
    return jsonify(game_progress(stem, pool))


@app.route("/api/accept_strong", methods=["POST"])
def api_accept_strong():
    """/*----------------------
     | api_accept_strong
     | Description: Accepts the waiting suggestion for every undecided room whose
     |     basis is strong, and only those. Deliberately refuses to bulk-accept
     |     weak, analogue or unfounded suggestions: those are the ones a human is
     |     here to look at, and a button that swept them all in would leave no
     |     record of which had been considered.
     | Author: suinevere
     | Dependencies: flask, pres_store
     | Globals: N/A
     | Params: N/A (JSON body: game)
     | Returns: the new counts and how many were taken
     ----------------------*/"""
    b = request.get_json(force=True)
    stem = b.get("game")
    if stem not in store.games():
        abort(400)
    pool = store.pool()
    tags = store.scenes(stem)
    saved = store.load(stem)["rooms"]
    n = 0
    for r in store.rooms(stem):
        if str(r["obj"]) in saved:
            continue
        scene, origin = store.scene_of(r["obj"], r["title"], tags)
        s = store.suggest(scene, pool["scene_defaults"], origin)
        if s["confidence"] == "strong" and s["image"]:
            store.assign(stem, r["obj"], s["image"], s["track"])
            n += 1
    out = game_progress(stem, pool)
    out["accepted"] = n
    return jsonify(out)


@app.route("/api/undo", methods=["POST"])
def api_undo():
    """/*----------------------
     | api_undo
     | Description: Reverses one game's last verdict.
     | Author: suinevere
     | Dependencies: flask, pres_store
     | Globals: N/A
     | Params: N/A (JSON body: game)
     | Returns: the reversed object number and the new counts
     ----------------------*/"""
    b = request.get_json(force=True)
    stem = b.get("game")
    if stem not in store.games():
        abort(400)
    obj = store.undo(stem)
    out = game_progress(stem, store.pool())
    out["undone"] = obj
    return jsonify(out)


if __name__ == "__main__":
    if not PNG_DIR.is_dir():
        print(f"pres_server: {PNG_DIR} is missing -- the picture previews come "
              "from there. Run analysis/zork_room_backgrounds.py first.",
              file=sys.stderr)
    app.run(host="127.0.0.1", port=8080, debug=False)
