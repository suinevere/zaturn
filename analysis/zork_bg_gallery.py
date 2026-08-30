#!/usr/bin/env python3
"""/*----------------------
 | zork_bg_gallery.py
 | Description: Build a self-contained HTML gallery of the Zork I (Saturn) room backgrounds.
 | Author: suinevere
 | Dependencies: zork_cgl, PIL, csv, base64
 | Globals: AREA_ORDER, AREA_BLURB, CSS, JS
 ----------------------*/"""
import base64
import csv
import collections
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zork_cgl
from PIL import Image

# /*----------------------
#  | AREA_ORDER / AREA_BLURB
#  | Description: Archives in rough order of the journey through the map, and what each covers.
#  | Author: suinevere
#  ----------------------*/
AREA_ORDER = ["BHUS", "BWOD", "BBAR", "BCEL", "BMAZ", "BMIR",
              "BTMP", "BDED", "BDAM", "BRIV", "BMIN"]
AREA_BLURB = {
    "BHUS": "White House, inside and out",
    "BWOD": "The forest and the clearings",
    "BBAR": "Stone Barrow",
    "BCEL": "Cellar, Round Room, Cyclops, Treasure Room",
    "BMAZ": "The Maze",
    "BMIR": "Mirror rooms and the cave passages",
    "BTMP": "Temple, Egypt Room, Dome Room",
    "BDED": "Hades, shipped in a source folder named BHDS",
    "BDAM": "Flood Control Dam #3 and the Reservoir",
    "BRIV": "Frigid River, the canyon, Aragain Falls",
    "BMIN": "Coal Mine",
}


def encode_plates(raw_dir):
    """/*----------------------
     | encode_plates
     | Description: Re-encode every CGL frame as an 8bpp PNG data URI.
     | Author: suinevere
     | Dependencies: zork_cgl, PIL, base64
     | Globals: AREA_ORDER
     | Params: raw_dir -- directory holding the .CGL archives
     | Returns: dict of "AREA_NN" -> data URI string
     ----------------------*/"""
    out = {}
    for area in AREA_ORDER:
        buf = open(os.path.join(raw_dir, area + ".CGL"), "rb").read()
        for n, off, pal, px in zork_cgl.records(buf):
            im = Image.frombytes("P", (zork_cgl.WIDTH, zork_cgl.HEIGHT),
                                 px[:zork_cgl.WIDTH * zork_cgl.HEIGHT])
            flat = []
            for rgb in pal:
                flat.extend(rgb)
            im.putpalette(flat)
            bio = io.BytesIO()
            im.save(bio, "PNG", optimize=True)
            out[f"{area}_{n:02d}"] = ("data:image/png;base64,"
                                      + base64.b64encode(bio.getvalue()).decode())
    return out


CSS = """
:root{
  --bg:#EDEFEA; --card:#FFFFFF; --ink:#161C19; --ink-soft:#4A5651;
  --muted:#6E7A74; --rule:#D2D8D2; --rule-soft:#E3E7E2; --accent:#8A5E12;
  --chip:#E6EAE4; --plate-mat:#202723;
}
@media (prefers-color-scheme: dark){
  :root:not([data-theme="light"]){
    --bg:#0C110F; --card:#141B18; --ink:#DCE3DE; --ink-soft:#A8B4AE;
    --muted:#7E8C85; --rule:#2A342F; --rule-soft:#1C2521; --accent:#D2A047;
    --chip:#1D2622; --plate-mat:#080B0A;
  }
}
:root[data-theme="dark"]{
  --bg:#0C110F; --card:#141B18; --ink:#DCE3DE; --ink-soft:#A8B4AE;
  --muted:#7E8C85; --rule:#2A342F; --rule-soft:#1C2521; --accent:#D2A047;
  --chip:#1D2622; --plate-mat:#080B0A;
}
*{box-sizing:border-box}
body{
  background:var(--bg); color:var(--ink); margin:0;
  font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
  font-size:16px; line-height:1.6; -webkit-font-smoothing:antialiased;
}
.wrap{max-width:1180px; margin:0 auto; padding:0 24px 96px}
code{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; font-size:.92em}

header.top{border-bottom:1px solid var(--rule); padding:56px 0 30px; margin-bottom:44px}
.eyebrow{
  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  font-size:12px; letter-spacing:.16em; text-transform:uppercase;
  color:var(--accent); margin:0 0 14px;
}
h1{
  font-family:Georgia,"Iowan Old Style","Times New Roman",serif;
  font-weight:400; font-size:clamp(34px,6vw,58px); line-height:1.05;
  letter-spacing:-.015em; margin:0 0 16px; text-wrap:balance;
}
.lede{max-width:64ch; color:var(--ink-soft); font-size:17px; margin:0 0 30px}
.stats{display:flex; flex-wrap:wrap; gap:14px 44px}
.stat b{
  display:block; font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  font-size:25px; font-weight:500; font-variant-numeric:tabular-nums; color:var(--ink);
}
.stat span{font-size:11px; letter-spacing:.12em; text-transform:uppercase; color:var(--muted)}

h2{
  font-family:Georgia,"Iowan Old Style","Times New Roman",serif;
  font-weight:400; font-size:27px; letter-spacing:-.01em; margin:0 0 8px;
}
section.block{margin:0 0 66px}
.sub{color:var(--ink-soft); font-size:14.5px; margin:0 0 24px; max-width:72ch}
.cols{display:grid; grid-template-columns:repeat(auto-fit,minmax(330px,1fr)); gap:26px}

.tablewrap{overflow-x:auto; border:1px solid var(--rule); border-radius:3px; background:var(--card)}
table{border-collapse:collapse; width:100%; font-size:13.5px}
th{
  text-align:left; font-weight:500; font-size:11px; letter-spacing:.11em;
  text-transform:uppercase; color:var(--muted); padding:11px 14px; white-space:nowrap;
  border-bottom:1px solid var(--rule); background:var(--card); position:sticky; top:0;
}
td{padding:9px 14px; border-bottom:1px solid var(--rule-soft); vertical-align:top}
tbody tr:last-child td{border-bottom:0}
td.num{
  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  font-variant-numeric:tabular-nums; color:var(--ink-soft); white-space:nowrap;
}
td.room{
  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  font-variant-numeric:tabular-nums; color:var(--accent); font-weight:600;
}

.archive{margin:0 0 54px}
.archive-head{
  display:flex; align-items:baseline; gap:16px; flex-wrap:wrap;
  border-bottom:1px solid var(--rule); padding-bottom:10px; margin-bottom:22px;
}
.archive-head h3{
  margin:0; font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  font-size:19px; font-weight:600; letter-spacing:.02em;
}
.archive-head .blurb{color:var(--ink-soft); font-size:14px; flex:1 1 240px}
.archive-head .count{
  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  font-size:12px; color:var(--muted); font-variant-numeric:tabular-nums;
}
.sheet{display:grid; grid-template-columns:repeat(auto-fill,minmax(268px,1fr)); gap:26px 22px}
.sheet.items{grid-template-columns:repeat(auto-fill,minmax(132px,1fr)); gap:20px 16px}
.plate.item .frame{padding:5px}
figure.plate{margin:0; transition:opacity .18s ease, filter .18s ease}
figure.plate.dim{opacity:.15; filter:grayscale(1)}
.plate .frame{
  background:var(--plate-mat); border:1px solid var(--rule); border-radius:2px;
  padding:7px; line-height:0;
}
.plate img{width:100%; height:auto; display:block; image-rendering:pixelated}
figcaption{padding-top:9px}
.fname{
  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  font-size:11.5px; letter-spacing:.05em; color:var(--muted);
  display:flex; justify-content:space-between; gap:10px;
  border-bottom:1px solid var(--rule-soft); padding-bottom:6px; margin-bottom:8px;
}
.rooms{display:flex; flex-wrap:wrap; gap:5px}
.chip{
  display:inline-flex; align-items:baseline; gap:6px; background:var(--chip);
  border-radius:2px; padding:3px 8px; font-size:11.5px; color:var(--ink-soft);
}
.chip b{
  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  font-weight:600; color:var(--accent); font-variant-numeric:tabular-nums;
}
.chip.none{background:transparent; color:var(--muted); font-style:italic; padding-left:0}

.filterbar{
  display:flex; align-items:center; gap:14px; flex-wrap:wrap;
  margin:0 0 28px; padding:14px 16px; border:1px solid var(--rule);
  border-radius:3px; background:var(--card);
}
.filterbar label{font-size:11px; letter-spacing:.12em; text-transform:uppercase; color:var(--muted)}
.filterbar input{
  flex:1 1 240px; min-width:180px; background:transparent; border:0;
  border-bottom:1px solid var(--rule); color:var(--ink); font:inherit; font-size:15px;
  padding:4px 2px;
}
.filterbar input:focus{outline:none; border-bottom-color:var(--accent)}
.filterbar input::placeholder{color:var(--muted)}
.hits{
  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  font-size:12px; color:var(--muted); font-variant-numeric:tabular-nums;
}
:focus-visible{outline:2px solid var(--accent); outline-offset:2px}
footer{border-top:1px solid var(--rule); padding-top:20px; color:var(--muted); font-size:13px;
       max-width:78ch}
@media (prefers-reduced-motion:reduce){*{transition:none !important}}
"""

JS = """
const q = document.getElementById('q');
const plates = [...document.querySelectorAll('figure.plate')];
const rows = [...document.querySelectorAll('#roomindex tbody tr')];
const hits = document.getElementById('hits');
function apply(){
  const t = q.value.trim().toLowerCase();
  let n = 0;
  plates.forEach(p => p.classList.toggle('dim', !!t && !p.dataset.search.includes(t)));
  rows.forEach(r => {
    const m = !t || r.dataset.search.includes(t);
    r.style.display = m ? '' : 'none';
    if (m) n++;
  });
  hits.textContent = t ? n + ' of 110 rooms' : '110 rooms';
}
q.addEventListener('input', apply);
apply();
"""

HEAD_TMPL = """<header class="top">
<p class="eyebrow">Zork I &middot; Sega Saturn &middot; Japan &middot; 1996</p>
<h1>Great Underground Plates</h1>
<p class="lede">Every pre-rendered room background on the disc, pulled out of the eleven
<code>B*.CGL</code> archives on data track 1 and matched to the rooms that display it. The plates
are 8-bit and 320&times;240, each carrying its own 256-colour palette, which is why every region
of the map is graded a different colour.</p>
<div class="stats">
<div class="stat"><b>{nplates}</b><span>plates</span></div>
<div class="stat"><b>19</b><span>item pictures</span></div>
<div class="stat"><b>110</b><span>rooms</span></div>
<div class="stat"><b>11</b><span>archives</span></div>
<div class="stat"><b>31</b><span>CD-DA tracks</span></div>
<div class="stat"><b>320&times;240</b><span>every plate</span></div>
</div></header>"""

FORMAT_SECTION = """<section class="block"><h2>How a room finds its plate</h2>
<p class="sub">A <code>.CGL</code> is a chain of 4-byte-aligned records: a 512-byte RGB555 palette
followed by an LZSS stream that expands to one 320&times;240 frame. <code>0ZORK.BIN</code> carries a
16-byte presentation record per room at file offset <code>0x75060</code>, and the loader at
<code>0x0600b180</code> fetches the frame as <code>cgl_buffer + record[+12] + 512</code>, stepping
over the frame&rsquo;s own palette.</p>
<div class="cols">
<div class="tablewrap"><table><thead><tr><th>Field</th><th>Type</th><th>Meaning</th></tr></thead>
<tbody>
<tr><td class="num">+0</td><td class="num">u16</td><td>CD-DA track number, 0 for silence</td></tr>
<tr><td class="num">+2</td><td class="num">u16</td><td>Sound-effect bank index</td></tr>
<tr><td class="num">+4</td><td class="num">u16</td><td>Archive index, alphabetical</td></tr>
<tr><td class="num">+6</td><td class="num">u16</td><td>Unused, always zero</td></tr>
<tr><td class="num">+8</td><td class="num">u32</td><td>Byte length of the frame record</td></tr>
<tr><td class="num">+12</td><td class="num">u32</td><td>Byte offset of the frame record</td></tr>
</tbody></table></div>
<div class="tablewrap"><table><thead><tr><th>Index</th><th>Archive</th><th>Sound bank</th></tr>
</thead><tbody>
<tr><td class="num">0</td><td class="num">BBAR</td><td class="num">SEALL</td></tr>
<tr><td class="num">1</td><td class="num">BCEL</td><td class="num">SEMINA</td></tr>
<tr><td class="num">2</td><td class="num">BDAM</td><td class="num">SEMINB</td></tr>
<tr><td class="num">3</td><td class="num">BDED</td><td class="num">SEMIR</td></tr>
<tr><td class="num">4</td><td class="num">BHUS</td><td class="num">SEDAM</td></tr>
<tr><td class="num">5</td><td class="num">BMAZ</td><td class="num">SECEL</td></tr>
<tr><td class="num">6</td><td class="num">BMIN</td><td class="num">SEHDS</td></tr>
<tr><td class="num">7</td><td class="num">BMIR</td><td class="num">SERIV</td></tr>
<tr><td class="num">8</td><td class="num">BRIV</td><td class="num">SEWOD</td></tr>
<tr><td class="num">9</td><td class="num">BTMP</td><td class="num">SEMAZ</td></tr>
<tr><td class="num">10</td><td class="num">BWOD</td><td class="num">SEBAR</td></tr>
</tbody></table></div></div></section>"""

FOOTER = """<footer>Extracted from <code>Zork I - The Great Underground Empire (Japan)
(Track 01).bin</code>. Four room titles &mdash; 0, 5, 41 and 92 &mdash; live inside multi-part
message banks rather than as plain strings, and were resolved from the bank text plus the
room&rsquo;s archive. <code>BBAR_01</code> is the one plate no room references; it belongs to the
<code>HUS_BAR.TPG</code> transition.</footer>"""


def items_section(items_dir):
    """/*----------------------
     | items_section
     | Description: Render the OITEM.CZ item pictures as an inline grid.
     | Author: suinevere
     | Dependencies: base64, glob, PIL
     | Globals: N/A
     | Params: items_dir -- directory of item_NN.png files
     | Returns: an HTML section str, or "" when no items were extracted
     ----------------------*/"""
    import glob
    files = sorted(glob.glob(os.path.join(items_dir, "item_*.png")))
    if not files:
        return ""
    h = ["""<section class="block"><h2>Item pictures</h2>
<p class="sub">Unlike the room plates, these come out of <code>OITEM.CZ</code> &mdash; a container
holding nineteen LZSS streams of 5120 bytes (64&times;80 at 8bpp) followed by nineteen 512-byte
CLUTs, one per picture. They are the objects the game shows in the inventory panel. Disc order;
the names the game attaches to them are not decoded yet.</p>
<div class="sheet items">"""]
    for f in files:
        b64 = base64.b64encode(open(f, "rb").read()).decode()
        name = os.path.basename(f)[:-4]
        h.append(f'<figure class="plate item"><div class="frame">'
                 f'<img src="data:image/png;base64,{b64}" width="64" height="80"'
                 f' alt="{name}"></div><figcaption>'
                 f'<div class="fname"><span>{name}.png</span><span>64&times;80</span></div>'
                 f'</figcaption></figure>')
    h.append("</div></section>")
    return "\n".join(h)


def audio_section(tracks_csv):
    """/*----------------------
     | audio_section
     | Description: Render the CD-DA track table from cd_tracks.csv.
     | Author: suinevere
     | Dependencies: csv
     | Globals: N/A
     | Params: tracks_csv -- path to cd_tracks.csv
     | Returns: an HTML section str
     ----------------------*/"""
    rows = list(csv.DictReader(open(tracks_csv, encoding="utf-8")))
    h = ["""<section class="block"><h2>Sound</h2>
<p class="sub">Field <code>+0</code> of the same room record is a raw disc track number. The
room-change handler at <code>0x06048c1c</code> hands it to the looping player at
<code>0x0602a4d8</code>, which fills a CDC play spec with start track / index 1, end track /
index 99, mode <code>0x0F</code> &mdash; repeat until the room changes. A second player at
<code>0x0602a578</code> is identical apart from mode <code>0x00</code>, play once. Twelve of the
thirty-one audio tracks are room music; ten rooms play nothing at all.</p>
<div class="tablewrap"><table><thead><tr>
<th>Track</th><th>Length</th><th>Vol</th><th>Rooms</th><th>Used for</th></tr></thead><tbody>"""]
    for r in rows:
        n = int(r["rooms"])
        detail = r["role"] if not n else r["room_list"]
        h.append(f'<tr><td class="room">{r["track"]}</td><td class="num">{r["length"]}</td>'
                 f'<td class="num">{r["volume"]}</td><td class="num">{n or "&mdash;"}</td>'
                 f'<td>{detail}</td></tr>')
    h.append("</tbody></table></div>")
    h.append("""<p class="sub" style="margin-top:18px">Ten rooms are scored silent
(track 0): Barrow Entrance, Grating Room, Atlantis Room, Base of Dam, Slide Room and all five
Frigid River rooms. Playback volume comes from a per-track byte table at
<code>0x0608ef74</code> &mdash; 127 everywhere except track 32, which is set to zero.</p>
</section>""")
    return "\n".join(h)


def build(csv_path, raw_dir, out_path):
    """/*----------------------
     | build
     | Description: Render the gallery page with every plate inlined as a data URI.
     | Author: suinevere
     | Dependencies: encode_plates, csv, collections
     | Globals: AREA_ORDER, AREA_BLURB, CSS, JS, HEAD_TMPL, FORMAT_SECTION, FOOTER
     | Params: csv_path -- room_backgrounds.csv; raw_dir -- .CGL dir; out_path -- .html to write
     | Returns: N/A
     ----------------------*/"""
    rows = list(csv.DictReader(open(csv_path, encoding="utf-8")))
    plates = encode_plates(raw_dir)
    by_img = collections.defaultdict(list)
    for r in rows:
        by_img[r["image"][:-4]].append(r)

    h = ["<title>Great Underground Plates</title>", f"<style>{CSS}</style>",
         '<div class="wrap">', HEAD_TMPL.format(nplates=len(plates)), FORMAT_SECTION]

    h.append("""<section class="block"><h2>The contact sheet</h2>
<p class="sub">Type a room name or an archive to pick it out. Matching plates stay lit and the room
index below narrows to match.</p>
<div class="filterbar"><label for="q">Filter</label>
<input id="q" type="search" placeholder="cellar, maze, BRIV, rainbow" autocomplete="off">
<span class="hits" id="hits">110 rooms</span></div>""")

    for area in AREA_ORDER:
        keys = sorted(k for k in plates if k.startswith(area + "_"))
        h.append('<div class="archive"><div class="archive-head">'
                 f'<h3>{area}.CGL</h3><span class="blurb">{AREA_BLURB[area]}</span>'
                 f'<span class="count">{len(keys)} plates</span></div><div class="sheet">')
        for k in keys:
            users = by_img.get(k, [])
            search = " ".join([k.lower()] + [u["title"].lower() for u in users])
            chips = "".join(
                f'<span class="chip"><b>{u["room"]}</b>{u["title"].title()}</span>'
                for u in users) or '<span class="chip none">no room uses this plate</span>'
            h.append(f'<figure class="plate" data-search="{search}">'
                     f'<div class="frame"><img src="{plates[k]}" width="320" height="240"'
                     f' alt="{k}"></div><figcaption>'
                     f'<div class="fname"><span>{k}.png</span><span>{len(users)}</span></div>'
                     f'<div class="rooms">{chips}</div></figcaption></figure>')
        h.append("</div></div>")
    h.append("</section>")
    h.append(items_section(os.path.join(
        os.path.dirname(os.path.dirname(csv_path)), "zork_ui", "items")))

    h.append("""<section class="block"><h2>Room index</h2>
<p class="sub">Room numbers are the engine&rsquo;s own 0&ndash;109 index into the presentation
table. Titles are the headers the Japanese build prints on entry, taken from its
<code>msg777</code> string table at entry <code>3 &times; room</code>.</p>
<div class="tablewrap"><table id="roomindex"><thead><tr>
<th>#</th><th>Room</th><th>Plate</th><th>Archive</th><th>Frame</th>
<th>Offset</th><th>CD track</th><th>Sound</th></tr></thead><tbody>""")
    for r in rows:
        s = (f'{r["room"]} {r["title"].lower()} {r["image"][:-4].lower()} '
             f'{r["se_bank"].lower()}')
        track = r["cd_track"] if r["cd_track"] != "0" else "&mdash;"
        h.append(f'<tr data-search="{s}"><td class="room">{r["room"]}</td>'
                 f'<td>{r["title"].title()}</td><td class="num">{r["image"][:-4]}</td>'
                 f'<td class="num">{r["area_archive"]}</td><td class="num">{r["frame"]}</td>'
                 f'<td class="num">0x{int(r["frame_offset"]):06X}</td>'
                 f'<td class="num">{track}</td><td class="num">{r["se_bank"]}</td></tr>')
    h.append("</tbody></table></div></section>")
    h.append(audio_section(os.path.join(os.path.dirname(csv_path), "cd_tracks.csv")))
    h.append(FOOTER)
    h.append(f"</div><script>{JS}</script>")
    open(out_path, "w", encoding="utf-8").write("\n".join(h))
    print(f"{out_path}: {os.path.getsize(out_path) / 1e6:.2f} MB")


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    build(os.path.join(here, "zork_bg", "room_backgrounds.csv"),
          os.path.join(here, "zork_bg", "raw"),
          os.path.join(here, "zork_bg", "gallery.html"))
