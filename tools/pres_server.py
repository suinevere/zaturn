#!/usr/bin/env python3
"""Review server for assigning a picture and a CD-DA track to every room.

Description: The one review app, replacing the retired art_server (which
    curated downloaded pictures per category) and scene_server (which tagged
    rooms with a category). Neither question exists any more: there is exactly
    one supply of art -- Zork I's 74 room backgrounds and the fourteen CD-DA
    tracks a room is allowed to name -- and the only remaining decision is
    which of them each room of the other thirty games gets.

    Zork I itself is not reviewable here. Its 110 rooms are measured off the
    original disc by tools/gen_presentation.py, and an opinion competing with a
    measurement is worth less than the measurement. It is shown read-only, as
    the reference the suggestions are derived from.

    Nothing is confirmed twice. Every room starts holding the suggestion
    pres_store.bless wrote for it, clicking a picture stores that picture and
    choosing a track stores that track -- each on its own, immediately, with no
    Save and no Accept in between. A room is accepted when it names both, and
    that is the only sense of accepted the app has.

    What a confirming click used to carry, the table carries instead: every row
    shows how well founded its pairing is, whether or not it has been touched.
    "4 of 4 Zork I FOREST rooms took this picture" and "2 of 13 CAVE rooms took
    this one" are both suggestions, and an interface that rendered them
    identically would be lying about one of them. Five bases are distinguished
    and coloured: chosen (a human overruled the evidence), strong (the scene's
    rooms mostly agreed), weak (they did not), analogue (Zork I never had a
    room of this kind, so a human picked a visual stand-in), and none (no scene
    tag and no title rule matched).

    Every verdict is written through to disk immediately (see pres_store.save),
    and every write is reversible: Undo replays the per-game stack, and any
    room can be revisited and changed from the game's own page. A session's
    verdicts are exactly the kind of state this project has lost before, and a
    crash must cost at most the one decision in flight.
Author: suinevere
Dependencies: flask, html, image_looks, pathlib, sys, game_genre, pres_store,
    room_groups, room_guess, scene_vocab
Globals: ROOT, PNG_DIR, app
Run: tools/.venv/Scripts/python.exe tools/pres_server.py   (serves on 8080)
"""
import html
import pathlib
import sys

from flask import Flask, abort, jsonify, render_template_string, request, send_from_directory

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import game_genre as genre_vocab
import image_looks
import pres_store as store
import room_groups
import room_guess

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
 tr.link{cursor:pointer}
 tr.genre td{background:#191d23;border-top:2px solid var(--line);padding:10px 8px}
 .tag{display:inline-block;padding:1px 7px;border-radius:10px;font-size:11px;font-weight:600}
 .c-strong{background:#12341c;color:var(--strong)} .c-weak{background:#3a2d0c;color:var(--weak)}
 .c-analogue{background:#2b1d43;color:var(--analogue)} .c-none{background:#23262b;color:var(--none)}
 .c-chosen{background:#0d2b45;color:var(--pick)}
 .c-guess{background:#37231f;color:#e08a6a}
 .card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:14px;margin-bottom:14px}
 .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(172px,1fr));gap:10px}
 .thumb{position:relative;border:2px solid transparent;border-radius:6px;overflow:hidden;
        cursor:pointer;background:#000}
 .thumb.sel{border-color:var(--pick)}
 .thumb img{width:100%;display:block}
 .thumb .cap{padding:4px 6px;font-size:11px;color:var(--dim)}
 .zoom{position:absolute;top:4px;right:4px;padding:0 6px;border-radius:4px;background:#000a;
       border:1px solid var(--line);color:var(--fg);font-size:13px;cursor:zoom-in}
 .zoom:hover{border-color:var(--pick)}
 img.zoomable{cursor:zoom-in}
 .strip{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
 .cell{width:132px;cursor:pointer;border:1px solid var(--line);border-radius:6px;
       overflow:hidden;background:#000}
 .cell:hover{border-color:var(--pick)}
 .cell img{width:100%;display:block}
 .cell .cap{padding:3px 5px;font-size:11px;color:var(--dim);background:var(--card)}
 .area{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
 .desc{color:var(--dim);max-width:70ch}
 select,button{background:#22262c;color:var(--fg);border:1px solid var(--line);
               border-radius:6px;padding:6px 10px;font:inherit}
 button{cursor:pointer} button:hover{border-color:var(--pick)}
 .row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
 .muted{color:var(--dim)} .big{font-size:17px;font-weight:600}
 #lb{position:fixed;inset:0;background:#000d;z-index:50;padding:20px;cursor:zoom-out;
     display:none;align-items:center;justify-content:center;flex-direction:column;gap:10px}
 #lb.on{display:flex}
 #lb img{max-width:100%;max-height:calc(100vh - 90px);image-rendering:pixelated;
         border:1px solid var(--line);background:#000}
</style>
<header>
  <span class="big"><a href="/">Room presentation</a></span>
  {{ header|safe }}
</header>
<div class="wrap">{{ body|safe }}</div>
<div id="lb"><img id="lbimg" alt=""><div class="muted" id="lbcap"></div></div>
<script>
function lbOpen(src, cap){
  document.getElementById('lbimg').src = src;
  document.getElementById('lbcap').textContent = cap || '';
  document.getElementById('lb').classList.add('on');
}
function lbClose(){ document.getElementById('lb').classList.remove('on'); }
document.getElementById('lb').addEventListener('click', lbClose);
document.addEventListener('keydown', function(e){ if(e.key === 'Escape') lbClose(); });
document.addEventListener('click', function(e){
  var z = e.target.closest('[data-zoom]');
  if(z){ e.preventDefault(); lbOpen(z.dataset.zoom, z.dataset.cap || ''); return; }
  var im = e.target.closest('img.zoomable');
  if(!im) return;
  e.preventDefault(); e.stopPropagation();
  lbOpen(im.src, im.dataset.cap || '');
});
function rowClick(e, href){
  if(e.target.closest('select, option, button, a, input, label, img.zoomable, [data-zoom]'))
    return;
  location = href;
}
async function post(u, b){
  const r = await fetch(u, {method:'POST', headers:{'Content-Type':'application/json'},
                           body: JSON.stringify(b)});
  if(!r.ok){ alert('failed'); return null }
  return r.json();
}
</script>
{{ script|safe }}
"""


def render(title, header, body, script=""):
    """Renders one page in the shared chrome."""
    return render_template_string(PAGE, title=title, header=header, body=body,
                                  script=script)


def basis_tag(b):
    """How well founded a pairing is, as a coloured pill."""
    label = {"chosen": "chosen", "strong": "strong", "weak": "weak",
             "analogue": "analogue", "guess": "best guess",
             "none": "no basis"}.get(b, b)
    return f'<span class="tag c-{b}">{label}</span>'


def state_tag(rec):
    """How much of a room's pairing exists, as a coloured pill."""
    if store.accepted(rec):
        return '<span class="tag c-strong">accepted</span>'
    if rec and rec.get("image"):
        return '<span class="tag c-weak">no track</span>'
    if rec:
        return '<span class="tag c-weak">no picture</span>'
    return '<span class="tag c-none">unset</span>'


def track_options(offered, track, short=False, mood=None):
    """/*----------------------
     | track_options
     | Description: The track menu's options, with one marked selected. The
     |     short form drops the role text: the game page carries one menu per
     |     room, and the full label repeated a hundred and forty times is most
     |     of the page's weight for a sentence the room's own page already says.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: offered -- store.tracks()'s records; track -- the one to select;
     |     short -- whether to drop the description; mood -- store.mood()
     | Returns: the option tags
     ----------------------*/"""
    out = []
    for t in offered:
        sel = " selected" if t["track"] == track else ""
        words = ", ".join((mood or {}).get(t["track"], {}).get("mood", []))
        if not t["track"]:
            label = "silence"
        elif short:
            label = f"track {t['track']} &mdash; {t['length']}"
        elif words:
            label = f"track {t['track']} &mdash; {t['length']} &mdash; {words}"
        else:
            label = f"track {t['track']} &mdash; {t['length']} &mdash; {t['role'][:54]}"
        out.append(f'<option value="{t["track"]}"{sel}>{label}</option>')
    return "".join(out)


def zoomable(png, cap, width):
    """/*----------------------
     | zoomable
     | Description: One picture that opens full size when clicked. The class is
     |     what the shared handler looks for, and what rowClick looks for to
     |     stand down -- a row is one large link and the click has already
     |     reached it by the time the shared handler runs, so the row has to
     |     decline the click rather than have it taken away afterwards.
     | Author: suinevere
     | Dependencies: html
     | Globals: N/A
     | Params: png -- the filename, empty for no picture; cap -- the caption
     |     shown under the enlargement; width -- the thumbnail width in px
     | Returns: an img tag, or a dash when there is no picture
     ----------------------*/"""
    if not png:
        return '<span class="muted">&mdash;</span>'
    return (f'<img class="zoomable" src="/png/{png}" data-cap="{html.escape(cap, True)}"'
            f' style="width:{width}px;border-radius:4px">')


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
     | Description: How much of one game is accepted, and how every one of its
     |     rooms breaks down by the strength of the association behind its
     |     current pairing -- all of them, not just the untouched ones. Once the
     |     suggestions are blessed in there are no untouched rooms left to
     |     count, and the question worth asking becomes how much of the table
     |     rests on evidence, how much on a stand-in, and how much on a guess.
     | Author: suinevere
     | Dependencies: pres_store
     | Globals: N/A
     | Params: stem -- the story stem; pool -- the catalogue
     | Returns: a dict of counts
     ----------------------*/"""
    rooms = store.rooms(stem)
    saved = store.load(stem)["rooms"]
    sug = room_guess.blessing(stem, pool)
    out = {"total": len(rooms), "done": 0, "part": 0, "chosen": 0, "strong": 0,
           "weak": 0, "analogue": 0, "guess": 0, "none": 0}
    for r in rooms:
        rec = saved.get(str(r["obj"]))
        s = sug.get(r["obj"]) or {"image": 0, "track": 0, "confidence": "none"}
        if store.accepted(rec):
            out["done"] += 1
        elif rec:
            out["part"] += 1
        out[store.basis(rec, s)] += 1
    return out


@app.route("/")
def index():
    """/*----------------------
     | index
     | Description: Every game, how much of it is accepted, and what the whole
     |     of its table rests on.
     | Author: suinevere
     | Dependencies: flask, pres_store
     | Globals: N/A
     | Params: N/A
     | Returns: the page
     ----------------------*/"""
    pool = store.pool()
    offered = store.tracks(pool)
    rows = []
    tot = done = 0
    for genre, stems in genre_vocab.by_genre(store.games()):
        held = [(s, game_progress(s, pool)) for s in stems]
        n_rooms = sum(p["total"] for _s, p in held)
        name = genre or "unfiled"
        note = (genre_vocab.genre_note(genre) if genre
                else "not in tools/game_genre.py -- nobody has said what it sounds like")
        control = (f'<label>Track <select id="gt-{name}">'
                   f'{track_options(offered, None)}</select></label>'
                   f' <button onclick="genreTrack(\'{name}\')">Set every room in these'
                   f' {len(held)} games</button>') if genre else ""
        rows.append(f"""<tr class="genre">
          <td colspan="2"><b>{name}</b>
              <span class="muted">&mdash; {note}</span></td>
          <td class="muted">{len(held)} games, {n_rooms} rooms</td>
          <td colspan="6">{control}</td>
        </tr>""")
        for stem, p in held:
            tot += p["total"]
            done += p["done"]
            pct = (100 * p["done"]) // p["total"] if p["total"] else 0
            rows.append(f"""<tr class="link" onclick="rowClick(event,'/g/{stem}')">
              <td><a href="/g/{stem}">{stem}</a></td>
              <td>{p['done']} / {p['total']}</td>
              <td style="min-width:160px"><div class="bar"><i style="width:{pct}%"></i></div></td>
              <td>{basis_tag('chosen')} {p['chosen']}</td>
              <td>{basis_tag('strong')} {p['strong']}</td>
              <td>{basis_tag('weak')} {p['weak']}</td>
              <td>{basis_tag('analogue')} {p['analogue']}</td>
              <td>{basis_tag('guess')} {p['guess']}</td>
              <td>{basis_tag('none')} {p['none']}</td>
            </tr>""")

    pct = (100 * done) // tot if tot else 0
    body = f"""
    <div class="card">
      <div class="row"><span class="big">{done} of {tot} rooms name both a picture
      and a track</span>
      <div class="bar" style="max-width:320px"><i style="width:{pct}%"></i></div></div>
      <p class="muted">Every picture and track comes from Zork I's original Saturn
      release: {len(pool['images'])} room backgrounds and {len(store.tracks(pool))}
      CD-DA tracks &mdash; the pool a room is allowed to draw from, silence
      included. The disc's other fifteen tracks are the villain cues, the rank
      fanfares and the ending, which the runtime decides for itself every turn
      and which a room may not claim. Zork I's own 110 rooms are measured off
      that disc and are not assigned here &mdash; see
      <a href="/reference">the reference</a>, which is where these suggestions
      come from.</p>
    </div>
    <table><tr><th>Game</th><th>Accepted</th><th></th>
    <th colspan="6">Every room, by the strength of the association behind it</th></tr>
    {''.join(rows)}</table>"""
    script = """<script>
    async function genreTrack(g){
      const sel = document.getElementById('gt-' + g);
      const t = parseInt(sel.value, 10);
      if(!confirm('Give every room of every ' + g + ' game ' +
                  sel.options[sel.selectedIndex].text +
                  '? Every room changed is one step on that game's undo stack.')) return;
      const j = await post('/api/genre_track', {genre:g, track:t});
      if(j) location.reload();
    }
    </script>"""
    return render("Room presentation", "", body, script)


@app.route("/reference")
def reference():
    """/*----------------------
     | reference
     | Description: What Zork I did, per scene tag -- the whole evidential basis
     |     for every suggestion this app makes, shown plainly so a suggestion can
     |     be argued with rather than merely inherited.
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
        why = ("no Zork I room carries this tag &mdash; hand-picked stand-in"
               if d["source"] == "analogue"
               else f"{d['image_support']} of {d['n']} rooms took it; "
                    f"{d['track_support']} of {d['n']} took track {d['track']}")
        cap = f"#{d['image']} {image_looks.looks(d['image'])} - {scene}"
        rows.append(f"""<tr>
          <td><b>{scene}</b></td><td>{basis_tag(conf)}</td>
          <td>{zoomable(img.get('png', ''), cap, 128)}</td>
          <td>#{d['image']}<br><span class="muted">{
              html.escape(image_looks.looks(d['image']))}</span></td>
          <td>{'silence' if not d['track'] else 'track ' + str(d['track'])}</td>
          <td class="muted">{why}</td>
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
     | Description: One game as its areas -- runs of adjacent rooms of the same
     |     kind, from room_groups -- each with the one track its rooms share.
     |
     |     The room is the wrong unit for a track and always was. Zork I did not
     |     give a track to a room, it gave one to the forest and one to the
     |     maze; the per-room table is how that was stored, not how it was
     |     decided, and a page that asked for a track a hundred and forty times
     |     was asking the same question over and over. The picture is the other
     |     way round -- every room is its own picture, and choosing one is what
     |     the room's own page is for, which is where a click on any room here
     |     goes.
     |
     |     An area whose rooms disagree says so rather than picking a winner to
     |     show: "mixed" is a real state, usually meaning a hand-made exception
     |     inside an otherwise uniform area, and quietly displaying the majority
     |     would hide it and then overwrite it on the next unrelated change.
     | Author: suinevere
     | Dependencies: flask, html, game_genre, pres_store, room_groups
     | Globals: N/A
     | Params: stem -- the story stem
     | Returns: the page
     ----------------------*/"""
    if stem not in store.games():
        abort(404)
    pool = store.pool()
    by_index = {i["index"]: i for i in pool["images"]}
    saved = store.load(stem)["rooms"]
    mood = store.mood()
    areas = room_groups.groups(stem, pool)
    sug = room_guess.suggestions(stem, pool)
    guessed = room_guess.area_tracks(stem, pool, areas, sug)

    allowed = genre_vocab.tracks_for(stem)
    every = store.tracks(pool)
    offered = [t for t in every if t["track"] in allowed]

    titles = {r["obj"]: r["title"] for r in store.rooms(stem)}
    descs = {r["obj"]: r["description"] or "" for r in store.rooms(stem)}

    cards = []
    for area in areas:
        held = area["rooms"]
        guess_track, guess_how = guessed[area["id"]]
        tracks = []
        cells = []
        acc = 0
        for obj in held:
            rec = saved.get(str(obj))
            one_sug = sug.get(obj, {"image": 0, "confidence": "none"})
            image = rec["image"] if rec else one_sug["image"]
            tracks.append(rec["track"] if rec else guess_track)
            acc += 1 if store.accepted(rec) else 0
            img = by_index.get(image, {})
            cap = f"#{image} {img.get('png', '')} - {titles.get(obj, '')}"
            shot = (zoomable(img.get("png", ""), cap, 130) if img
                    else '<div class="cap muted">no picture</div>')
            cells.append(f"""<div class="cell" title="{html.escape(descs.get(obj,'')[:160], True)}"
                 onclick="rowClick(event,'/g/{stem}/{obj}')">
              {shot}
              <div class="cap">{html.escape(titles.get(obj, '') or str(obj))}
                {state_tag(rec)}</div></div>""")

        one = tracks[0] if tracks and len(set(tracks)) == 1 else None
        guess_tag = (f'<span class="tag c-guess">best guess</span>'
                     if one is not None and one == guess_track
                     and guess_how != "measured" else "")
        mixed = ('<span class="tag c-weak">mixed</span>' if one is None
                 else "")
        words = ", ".join(mood.get(one, {}).get("mood", [])) if one else ""
        here = (offered if one is None or one in allowed
                else [t for t in every if t["track"] in set(allowed) | {one}])
        cards.append(f"""<div class="card" id="a{area['id']}">
          <div class="area">
            <span class="big">{html.escape(area['label'])}</span>
            <span class="tag c-{'chosen' if area['scene'] else 'none'}"
              >{area['scene'] or 'untagged'}</span>
            <span class="muted">{len(held)} rooms, {acc} accepted</span>
            {mixed}{guess_tag}
            <label>Track <select onchange="areaTrack({area['id']},this.value)"
              >{track_options(here, one, mood=mood)}</select></label>
            <span class="muted">{words}</span>
          </div>
          <div class="strip">{''.join(cells)}</div>
        </div>""")

    p = game_progress(stem, pool)
    hdr = (f'<a href="/">&larr; all games</a> <span class="muted">{stem}</span>'
           f' <span class="tag c-chosen">{genre_vocab.genre_of(stem) or "unfiled"}</span>'
           f' <span class="muted" id="count">{p["done"]}/{p["total"]} accepted,'
           f' {p["part"]} half-set</span>'
           f' <label>Every room: <select id="alltrack">'
           f'{track_options(offered, None, mood=mood)}</select></label>'
           f' <button onclick="trackAll()">Set that track everywhere</button>'
           f' <button onclick="bless()">Bless every unset room</button>'
           f' <button onclick="undo()">Undo</button>')

    script = f"""<script>
    async function areaTrack(id, track){{
      const j = await post('/api/area_track', {{game:'{stem}', area:id,
                                                track:parseInt(track,10)}});
      if(j) location.reload();
    }}
    async function trackAll(){{
      const sel = document.getElementById('alltrack');
      const t = parseInt(sel.value, 10);
      if(!confirm('Give all {p["total"]} rooms of {stem} ' +
                  sel.options[sel.selectedIndex].text +
                  '? Every room changed is one step on the undo stack.')) return;
      const j = await post('/api/track_all', {{game:'{stem}', track:t}});
      if(j) location.reload();
    }}
    async function bless(){{
      const j = await post('/api/bless', {{game:'{stem}'}});
      if(j) location.reload();
    }}
    async function undo(){{
      await post('/api/undo', {{game:'{stem}'}});
      location.reload();
    }}
    </script>"""

    intro = f"""<div class="card"><p class="muted">{len(cards)} areas, grouped by
    running the exit graph and the scene tags together: an area is a run of
    adjacent rooms of the same kind. Measured against Zork I, whose real areas
    are known from which picture archive each room was drawn from, this puts
    91% of rooms in an area whose majority archive is their own. A track is set
    for a whole area; a picture is set on the room's own page, which any room
    here opens.</p></div>"""
    return render(stem, hdr, intro + "".join(cards), script)


@app.route("/g/<stem>/<int:obj>")
def room(stem, obj):
    """/*----------------------
     | room
     | Description: One room, with every picture in the pool laid out to choose
     |     from and every track a room may name offered beside them. A click on a
     |     picture and a change of the track menu are each a verdict on their
     |     own, stored the moment they happen: there is no Save, because a Save
     |     button only ever adds a way to lose the decision just made.
     |
     |     The corner badge on each picture enlarges it instead of taking it, so
     |     the two things one wants to do with a candidate -- look at it properly
     |     and choose it -- do not have to share a click.
     | Author: suinevere
     | Dependencies: flask, html, pres_store
     | Globals: N/A
     | Params: stem -- the story stem; obj -- the room's object number
     | Returns: the page
     ----------------------*/"""
    if stem not in store.games():
        abort(404)
    pool = store.pool()
    rm = next((r for r in store.rooms(stem) if r["obj"] == obj), None)
    if rm is None:
        abort(404)

    sug = room_guess.blessing(stem, pool).get(
        obj, {"image": 0, "track": 0, "scene": None, "confidence": "none", "why": ""})
    scene = sug["scene"]
    cur = store.load(stem)["rooms"].get(str(obj))
    image = cur["image"] if cur else sug["image"]
    track = cur["track"] if cur else sug["track"]
    by_index = {i["index"]: i for i in pool["images"]}

    thumbs = []
    for i in pool["images"]:
        sel = " sel" if i["index"] == image else ""
        used = image_looks.looks(i["index"]) or ", ".join(i["rooms"][:2])
        cap = html.escape(f"#{i['index']} {i['png']} - {used}", True)
        thumbs.append(f"""<div class="thumb{sel}" id="t{i['index']}"
             onclick="thumbClick(event,{i['index']})">
          <img src="/png/{i['png']}" loading="lazy">
          <span class="zoom" title="full size" data-zoom="/png/{i['png']}"
                data-cap="{cap}">&#9974;</span>
          <div class="cap">#{i['index']} {html.escape(used)}</div></div>""")

    allowed = genre_vocab.tracks_for(stem, {track})
    opts = track_options([t for t in store.tracks(pool) if t["track"] in allowed],
                         track, mood=store.mood())
    img = by_index.get(image, {})
    png_map = ", ".join(f"{i['index']}:'{i['png']}'" for i in pool["images"])
    hdr = (f'<a href="/g/{stem}">&larr; {stem}</a>'
           f' <span class="muted">object {obj}</span>')
    body = f"""
    <div class="card">
      <div class="big">{html.escape(rm['title'] or '')}</div>
      <p class="desc">{html.escape(rm['description'] or '')}</p>
      <p class="muted">scene: <b>{scene or 'none'}</b></p>
      <div class="row"><span id="basis">{basis_tag(store.basis(cur, sug))}</span>
        <span class="muted">{sug['why']}</span></div>
    </div>
    <div class="card">
      <div class="row">
        <span id="prev">{zoomable(img.get('png', ''),
                                  f"#{image} {image_looks.looks(image)}", 160)}</span>
        <label>Track <select id="track" onchange="setTrack()">{opts}</select></label>
        <button onclick="pick(0)">No picture (hold what is showing)</button>
        <span id="state"></span>
        <span class="muted">picture #<b id="pick">{image or '-'}</b></span>
        <span class="muted" id="saved"></span>
      </div>
    </div>
    <div class="grid">{''.join(thumbs)}</div>
    <script>
    let chosen = {image or 0};
    let chosenTrack = {track or 0};
    const PNG = {{{png_map}}};
    function mark(){{
      document.querySelectorAll('.thumb').forEach(e => e.classList.remove('sel'));
      const el = document.getElementById('t' + chosen);
      if(el) el.classList.add('sel');
      document.getElementById('pick').textContent = chosen || '-';
      document.getElementById('prev').innerHTML = chosen
        ? '<img class="zoomable" src="/png/' + PNG[chosen] + '" data-cap="#' + chosen +
          ' ' + PNG[chosen] + '" style="width:160px;border-radius:4px">'
        : '<span class="muted">&mdash;</span>';
      document.getElementById('state').innerHTML = (chosen && chosenTrack)
        ? '<span class="tag c-strong">accepted</span>'
        : (chosen ? '<span class="tag c-weak">no track</span>'
                  : '<span class="tag c-weak">no picture</span>');
    }}
    async function put(){{
      const j = await post('/api/assign', {{game:'{stem}', obj:{obj},
                                            image:chosen, track:chosenTrack}});
      if(!j) return;
      document.getElementById('basis').innerHTML = j.room.basis;
      document.getElementById('saved').textContent = 'stored';
    }}
    function pick(i){{ chosen = i; mark(); put(); }}
    function thumbClick(e, i){{ if(e.target.closest('[data-zoom]')) return; pick(i); }}
    function setTrack(){{
      chosenTrack = parseInt(document.getElementById('track').value, 10);
      mark(); put();
    }}
    mark();
    </script>"""
    return render(f"{stem} {rm['title']}", hdr, body)


def _image_for(stem, pool):
    """/*----------------------
     | _image_for
     | Description: A picture for any room of one game that has no record yet,
     |     for the sweeps that set a track and must not blank the picture the
     |     page was showing beside it.
     | Author: suinevere
     | Dependencies: room_guess
     | Globals: N/A
     | Params: stem -- the story stem; pool -- the catalogue
     | Returns: a callable taking an object number and returning an image index
     ----------------------*/"""
    sug = room_guess.suggestions(stem, pool)
    return lambda obj: sug.get(obj, {}).get("image", 0)


@app.route("/api/assign", methods=["POST"])
def api_assign():
    """/*----------------------
     | api_assign
     | Description: Records one room's verdict.
     |     Refuses an object number that is not one of this game's rooms. The
     |     store would accept it and gen_presentation.py would happily emit it --
     |     a row for an object the story never uses, which nothing would ever
     |     read and nothing would ever report as wrong. Refuses a track outside
     |     the neutral pool for the same reason: the cues, the fanfares and the
     |     ending are the runtime's to issue, and a room naming one has written
     |     down a decision the engine will not honour.
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
    if not any(t["track"] == track for t in store.tracks(pool)):
        abort(400)
    store.assign(stem, obj, image, track)
    rec = {"image": image, "track": track}
    sug = room_guess.blessing(stem, pool).get(
        obj, {"image": 0, "track": 0, "confidence": "none"})
    out = game_progress(stem, pool)
    out["room"] = {"state": state_tag(rec), "basis": basis_tag(store.basis(rec, sug))}
    return jsonify(out)


@app.route("/api/area_track", methods=["POST"])
def api_area_track():
    """/*----------------------
     | api_area_track
     | Description: Gives every room of one area the same track. The area is
     |     named by id and its rooms are recomputed here rather than sent up
     |     from the page: areas are derived, the client has no standing to
     |     assert which rooms are in one, and a stale page would otherwise
     |     write a track into rooms that had since moved out of it.
     | Author: suinevere
     | Dependencies: flask, game_genre, pres_store, room_groups
     | Globals: N/A
     | Params: N/A (JSON body: game, area, track)
     | Returns: the new counts and how many rooms changed
     ----------------------*/"""
    b = request.get_json(force=True)
    stem = b.get("game")
    if stem not in store.games():
        abort(400)
    track = int(b.get("track", 0))
    pool = store.pool()
    if not any(t["track"] == track for t in store.tracks(pool)):
        abort(400)
    area = next((a for a in room_groups.groups(stem, pool)
                 if a["id"] == int(b.get("area", -1))), None)
    if area is None:
        abort(400)
    n = store.set_rooms_track(stem, area["rooms"], track,
                              image_for=_image_for(stem, pool))
    out = game_progress(stem, pool)
    out["changed"] = n
    return jsonify(out)


@app.route("/api/track_all", methods=["POST"])
def api_track_all():
    """/*----------------------
     | api_track_all
     | Description: Gives every room of one game the same track. A game whose
     |     rooms all sound alike is a real answer -- most of these stories have
     |     no scene tags at all, and one deliberate theme throughout beats a
     |     hundred rooms of silence -- and setting it a room at a time is the
     |     kind of work that does not get finished.
     | Author: suinevere
     | Dependencies: flask, pres_store
     | Globals: N/A
     | Params: N/A (JSON body: game, track)
     | Returns: the new counts and how many rooms changed
     ----------------------*/"""
    b = request.get_json(force=True)
    stem = b.get("game")
    if stem not in store.games():
        abort(400)
    track = int(b.get("track", 0))
    pool = store.pool()
    if not any(t["track"] == track for t in store.tracks(pool)):
        abort(400)
    n = store.set_all_tracks(stem, track, image_for=_image_for(stem, pool))
    out = game_progress(stem, pool)
    out["changed"] = n
    return jsonify(out)


@app.route("/api/bless", methods=["POST"])
def api_bless():
    """/*----------------------
     | api_bless
     | Description: Writes the standing suggestion into every room of one game
     |     that has no record yet, whatever its basis. Weak and analogue
     |     suggestions are taken too: holding them back only meant the table sat
     |     half empty waiting on a confirming click that recorded nothing the
     |     app did not already know. What that click used to carry -- that a
     |     human had looked -- the basis column now carries without it.
     | Author: suinevere
     | Dependencies: flask, pres_store
     | Globals: N/A
     | Params: N/A (JSON body: game)
     | Returns: the new counts and how many rooms were written
     ----------------------*/"""
    b = request.get_json(force=True)
    stem = b.get("game")
    if stem not in store.games():
        abort(400)
    n = store.bless(stem, room_guess.blessing(stem, store.pool()).get)
    out = game_progress(stem, store.pool())
    out["blessed"] = n
    return jsonify(out)


@app.route("/api/genre_track", methods=["POST"])
def api_genre_track():
    """/*----------------------
     | api_genre_track
     | Description: Gives every room of every game in one genre the same track.
     |     The genre is the unit most of these stories can actually be decided
     |     at: six mysteries want the same close, watchful theme, and the
     |     room-level evidence that would say otherwise does not exist for them.
     |
     |     Zork I is not swept, because store.games() does not list it -- its
     |     rows are measured off the original disc, and the genre table carries
     |     it only so that a table claiming to cover the disc does.
     | Author: suinevere
     | Dependencies: flask, game_genre, pres_store
     | Globals: N/A
     | Params: N/A (JSON body: genre, track)
     | Returns: the genre, how many games it holds and how many rooms changed
     ----------------------*/"""
    b = request.get_json(force=True)
    name = b.get("genre")
    if name not in dict(genre_vocab.GENRES):
        abort(400)
    track = int(b.get("track", 0))
    pool = store.pool()
    if not any(t["track"] == track for t in store.tracks(pool)):
        abort(400)
    stems = [s for s in store.games() if genre_vocab.genre_of(s) == name]
    n = sum(store.set_all_tracks(s, track, image_for=_image_for(s, pool))
            for s in stems)
    return jsonify({"genre": name, "games": len(stems), "changed": n})


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
