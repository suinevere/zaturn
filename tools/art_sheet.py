#!/usr/bin/env python3
"""/*----------------------
 | art_sheet.py
 | Description: Writes a page showing every drawn plate beside the room's own
 |     words, so a plate can be judged against what it was supposed to be.
 |
 |     The probe does this for the handful of rooms it draws. This does it for
 |     everything on disk, which is the difference between checking a change
 |     and reviewing the disc: 1,400 plates cannot be judged from filenames,
 |     and a plate that looks wrong and a plate that looks nothing like its
 |     room are two different problems with two different fixes.
 |
 |     Reads only. It writes one HTML file and touches no plate, no manifest
 |     and no sheet, so it is safe to run while a generation is in flight --
 |     which is when it is most wanted, because that is when there is something
 |     new to look at.
 | Author: suinevere
 | Dependencies: argparse, html, json, pathlib, sys
 | Globals: ROOT, ART, SHEET, ROOMS, OUT
 ----------------------*/"""
import argparse
import html
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import gen_art_source

ROOT = pathlib.Path(__file__).resolve().parent.parent
ART = ROOT / "tools" / "assets" / "art"
SHEET = ART / "room_prompts.json"
MANIFEST = ART / "manifest.json"
ROOMS = ROOT / "tools" / "assets" / "rooms"
OUT = ART / "art_sheet.html"
"""ROOT / ART / SHEET / ROOMS / OUT

Description: The plates, the prompts they were drawn from, the story files the
    descriptions come from, and the page. The page goes beside the plates so it
    can point at them with a bare filename and still open by double-clicking.
Author: suinevere
"""


def standing(entry, png):
    """/*----------------------
     | standing
     | Description: Whether the plate on disk was drawn from the prompt the
     |     sheet holds now, as (state, note).
     |
     |     A plate says nothing about its own currency. Prompts changed under a
     |     running job again and again, and the plates drawn before each change
     |     sat on disk looking finished -- two were reported as fresh failures
     |     when they were leftovers, one of them after a reset that could not
     |     see it. A page that shows a plate without saying which prompt made
     |     it is a page that wastes the reviewer's time.
     | Author: suinevere
     | Dependencies: gen_art_source
     | Globals: N/A
     | Params: entry -- a sheet entry; png -- its plate path
     | Returns: (state, note)
     ----------------------*/"""
    if not png.is_file():
        return "none", "not drawn yet"
    recorded = standing.by_source.get(png.name)
    if recorded is None:
        return "loose", ("on disk but not in the manifest -- a leftover from "
                         "before a reset, or drawn since the manifest was "
                         "last written")
    if not recorded.get("drawn_from"):
        return "unknown", ("drawn before the prompt was recorded, so whether "
                           "it is current cannot be told")
    if recorded["drawn_from"] != gen_art_source.fingerprint(entry):
        return "stale", ("STALE -- the prompt changed after this was drawn, "
                         "and the generator has not reached it again")
    return "current", ""


def rooms_of(stem):
    """/*----------------------
     | rooms_of
     | Description: One game's rooms, by object number.
     | Author: suinevere
     | Dependencies: json
     | Globals: ROOMS
     | Params: stem -- the story stem
     | Returns: {obj: (title, description)}
     ----------------------*/"""
    path = ROOMS / f"{stem}.json"
    if not path.is_file():
        return {}
    return {int(r["obj"]): ((r.get("title") or "").strip(),
                            (r.get("description") or "").strip())
            for r in json.loads(path.read_text(encoding="utf-8"))["rooms"]}


def page(entries, drawn_only):
    """/*----------------------
     | page
     | Description: The sheet: one row per room, the plate on the left and what
     |     the room says on the right, grouped by game.
     | Author: suinevere
     | Dependencies: html, pathlib
     | Globals: ART
     | Params: entries -- sheet entries in order; drawn_only -- skip rooms with
     |     no plate yet
     | Returns: the page as a string
     ----------------------*/"""
    standing.by_source = {}
    if MANIFEST.is_file():
        standing.by_source = {
            p["source"]: p for p in
            json.loads(MANIFEST.read_text(encoding="utf-8")).get("plates", [])}
    said, rows, games = {}, [], []
    shown = missing = silent = 0
    counts = {"current": 0, "stale": 0, "loose": 0, "unknown": 0, "none": 0}
    for e in entries:
        if e["game"] not in said:
            said[e["game"]] = rooms_of(e["game"])
        title, prose = said[e["game"]].get(int(e["obj"]), ("", ""))
        png = ART / f"{e['name']}.png"
        if drawn_only and not png.is_file():
            continue
        if e["game"] not in games:
            games.append(e["game"])
            rows.append(f'<h2 id="{e["game"]}">{html.escape(e["game"])}</h2>')
        shown += 1
        state, note = standing(e, png)
        counts[state] += 1
        if png.is_file():
            pic = (f'<img class="{state}" src="{e["name"]}.png" '
                   f'loading="lazy" alt="">')
        else:
            missing += 1
            pic = '<div class="none">not drawn yet</div>'
        if prose:
            words = f'<p class="says">{html.escape(prose)}</p>'
        else:
            silent += 1
            words = ('<p class="says none-said">the story file describes this '
                     'room with nothing at all &mdash; the picture is invented '
                     'from the title</p>')
        extra = ""
        if note and state != "none":
            extra += f'<p class="state {state}">{html.escape(note)}</p>'
        if e.get("compose_from"):
            extra += (f'<p class="flag">composed over '
                      f'{html.escape(e["compose_from"])} at denoise '
                      f'{e.get("denoise", 0.6)}</p>')
        if e.get("negative"):
            extra += (f'<p class="flag">also refuses '
                      f'{html.escape(e["negative"])}</p>')
        rows.append(
            f'<section><div class="pic">{pic}</div><div class="txt">'
            f'<h3>{html.escape(e["name"])} &mdash; '
            f'{html.escape(title or e.get("shows", ""))}</h3>'
            f'{words}<p class="prompt">{html.escape(e["prompt"])}</p>'
            f'{extra}</div></section>')
    nav = " ".join(f'<a href="#{g}">{g}</a>' for g in games)
    style = ("body{background:#111;color:#ddd;font:14px/1.55 system-ui;"
             "margin:0;padding:1.5rem}"
             "h1{font-size:19px;margin:0 0 .3rem}"
             "h2{font-size:16px;color:#fc9;margin:2.5rem 0 .6rem;"
             "border-bottom:1px solid #333;padding-bottom:.3rem}"
             "h3{font-size:14px;color:#9cf;margin:0 0 .4rem}"
             "nav{font-size:12px;margin:.6rem 0 1.2rem}"
             "nav a{color:#89a;margin-right:.7rem;text-decoration:none}"
             "section{display:flex;gap:1rem;margin:0 0 1.1rem;"
             "background:#171717;border-radius:6px;padding:.7rem}"
             ".pic{flex:0 0 320px}"
             "img{width:320px;display:block;border:1px solid #333;"
             "border-radius:3px}"
             ".none{width:320px;aspect-ratio:4/3;display:grid;"
             "place-items:center;border:1px dashed #444;color:#666;"
             "border-radius:3px}"
             ".txt{flex:1;min-width:0}"
             ".says{color:#e8e0c8;background:#1e1c16;border-left:3px solid #7a6;"
             "padding:.5rem .7rem;margin:0 0 .5rem;border-radius:0 4px 4px 0}"
             ".none-said{color:#e0b0b0;background:#221818;border-left-color:#a55}"
             ".prompt,.flag{color:#999;font-family:ui-monospace,monospace;"
             "font-size:11.5px;background:#1a1a1a;padding:.45rem .6rem;"
             "margin:0 0 .35rem;border-radius:4px;overflow-wrap:anywhere}"
             ".flag{color:#c9a;background:#1d1a20}"
             ".state{font-size:12px;padding:.4rem .6rem;margin:0 0 .35rem;"
             "border-radius:4px}"
             ".state.stale{color:#ffd7a8;background:#3a2410;"
             "border:1px solid #a05a10}"
             ".state.loose{color:#ffc9c9;background:#3a1414;"
             "border:1px solid #a03030}"
             ".state.unknown{color:#cfcfcf;background:#242424;"
             "border:1px solid #555}"
             "img.stale{border-color:#a05a10}"
             "img.loose{border-color:#a03030}"
             "@media(max-width:900px){section{display:block}"
             ".pic,img,.none{width:100%;flex:none}}")
    return ("<!doctype html><meta charset=utf-8><title>room art</title>"
            f"<style>{style}</style>"
            f"<h1>{shown} rooms &mdash; {counts['current']} current, "
            f"{counts['stale']} stale, {counts['loose']} not in the "
            f"manifest, {counts['unknown']} unrecorded, {missing} not "
            f"drawn; {silent} the story file says nothing about</h1>"
            f"<nav>{nav}</nav>" + "".join(rows))


def main(argv=None):
    """/*----------------------
     | main
     | Description: Writes the sheet.
     | Author: suinevere
     | Dependencies: argparse, json
     | Globals: SHEET, OUT
     | Params: argv -- command line
     | Returns: 0
     ----------------------*/"""
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--game", action="append", default=[],
                    help="only this game, by stem; repeatable")
    ap.add_argument("--all", action="store_true",
                    help="include rooms with no plate drawn yet")
    ap.add_argument("--no-prose", action="store_true",
                    help="only rooms the story file describes with nothing, "
                         "which are the ones a picture can least be judged by")
    ap.add_argument("--out", default=str(OUT))
    args = ap.parse_args(argv)

    if not SHEET.is_file():
        raise SystemExit(f"art_sheet: no {SHEET}; run tools/gen_room_prompts.py")
    entries = json.loads(SHEET.read_text(encoding="utf-8"))["batch"]
    if args.game:
        want = {g.upper() for g in args.game}
        entries = [e for e in entries if e["game"] in want]
        if not entries:
            raise SystemExit(f"art_sheet: no rooms for {', '.join(sorted(want))}")
    if args.no_prose:
        keep = []
        cache = {}
        for e in entries:
            cache.setdefault(e["game"], rooms_of(e["game"]))
            if not cache[e["game"]].get(int(e["obj"]), ("", ""))[1]:
                keep.append(e)
        entries = keep

    out = pathlib.Path(args.out)
    out.write_text(page(entries, drawn_only=not args.all), encoding="utf-8")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
