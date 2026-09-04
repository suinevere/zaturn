#!/usr/bin/env python3
"""/*----------------------
 | probe_prompts.py
 | Description: Draws a handful of rooms at their NEW prompt beside the plate
 |     already drawn at the old one, so a prompt change can be judged before it
 |     is spent on hundreds of rooms.
 |
 |     Nothing here touches the manifest. That is the whole point: a plate's
 |     picture index is its position in the manifest and the manifest is
 |     append-only, so twenty throwaway drawings appended to it would be twenty
 |     slots that could never be reclaimed. The probe writes loose PNGs into a
 |     directory git ignores, and an index.html to look at them in, and the
 |     directory is deleted once the answer is known.
 |
 |     The pairing is against the plate on disk rather than a fresh drawing of
 |     the old prompt, because the plate on disk is the thing actually shipping
 |     and redrawing the old prompt would only prove the sampler is
 |     deterministic.
 | Author: suinevere
 | Dependencies: argparse, html, json, pathlib, sys, forge_client,
 |     gen_room_prompts
 | Globals: ROOT, ART, SHEET, OUT, PINNED
 ----------------------*/"""
import argparse
import html
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import forge_client
import gen_room_prompts

ROOT = pathlib.Path(__file__).resolve().parent.parent
ART = ROOT / "tools" / "assets" / "art"
SHEET = ART / "room_prompts.json"
OUT = ART / "probe"
REFS = ART / "refs"
"""ROOT / ART / SHEET / OUT

Description: Where the new prompts are read from, where the old plates already
    sit, and where the probe's drawings go. OUT is beside the plates rather
    than in a temp directory so index.html can point at the old plate with a
    relative path and still work when it is opened by double-clicking it.
Author: suinevere
"""

PINNED = ["leatherg_145", "leatherg_172", "hitchhkr_121", "ballyhoo_62",
          "ballyhoo_4", "advent_130", "holywood_182",
          "sorcerer_116", "seastlkr_254", "plntfall_44", "plntfall_46"]
"""PINNED

Description: The rooms whose plates were rejected by eye. Drawn first and
    always, because a change that does not fix the pictures that prompted it
    has not been demonstrated by any number of other pictures.
Author: suinevere
"""


def sheet():
    """/*----------------------
     | sheet
     | Description: The current prompt sheet, by name.
     | Author: suinevere
     | Dependencies: json
     | Globals: SHEET
     | Params: N/A
     | Returns: {name: batch entry}
     ----------------------*/"""
    if not SHEET.is_file():
        raise SystemExit(f"probe_prompts: no {SHEET}; "
                         "run tools/gen_room_prompts.py")
    return {e["name"]: e
            for e in json.loads(SHEET.read_text(encoding="utf-8"))["batch"]}


def pick(entries, count, only_silent=False):
    """/*----------------------
     | pick
     | Description: The rooms to probe: the rejected ones first, then rooms that
     |     already have a plate to compare against, spread across games so one
     |     game's vocabulary cannot carry the verdict on its own.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: ART, PINNED
     | Params: entries -- the sheet by name; count -- how many in total;
     |     only_silent -- draw only rooms the story file describes with nothing
     | Returns: a list of names
     ----------------------*/"""
    if only_silent:
        said = descriptions(entries)
        silent = {n for n in entries if not said.get(n, ("", ""))[1]}
        entries = {n: e for n, e in entries.items() if n in silent}
    out = [n for n in PINNED if n in entries]
    seen = {n.rsplit("_", 1)[0] for n in out}
    rest = sorted(n for n in entries
                  if n not in out and (ART / f"{n}.png").is_file())
    # One pass taking only from games not represented yet and a second with
    # that dropped, so a short list is broad and a long one still fills.
    for wide in (True, False):
        for n in rest:
            if len(out) >= count:
                break
            game = n.rsplit("_", 1)[0]
            if wide and game in seen:
                continue
            if n not in out:
                out.append(n)
                seen.add(game)
    return out[:count]


def styled(prompt, style):
    """/*----------------------
     | styled
     | Description: One prompt with a different look swapped in, or unchanged.
     |     The look is one string in gen_room_prompts and appears verbatim in
     |     every prompt, so trying another is a substitution rather than a
     |     regeneration -- which matters because regenerating to try a look
     |     rewrites the sheet that is the record of what every plate was drawn
     |     from.
     | Author: suinevere
     | Dependencies: gen_room_prompts
     | Globals: N/A
     | Params: prompt -- the sheet's prompt; style -- the look, or None
     | Returns: the prompt to draw
     ----------------------*/"""
    if not style:
        return prompt
    return prompt.replace(gen_room_prompts.STYLE, style)


def descriptions(entries):
    """/*----------------------
     | descriptions
     | Description: The room's own words, by plate name -- its title and the
     |     description the story file carries, untouched.
     |
     |     The point of showing these beside the picture is that a plate can be
     |     wrong in two entirely different ways and they need different fixes.
     |     Either the room said something and the picture ignored it, which is a
     |     prompt bug, or the room said nothing at all and the picture is pure
     |     invention from a title, which no prompt rule can fix and which only
     |     an override or a shrug will. Ballyhoo's "Walking a Tightrope" has no
     |     description whatsoever, and that is not visible from the prompt.
     | Author: suinevere
     | Dependencies: json, pathlib
     | Globals: ROOT
     | Params: entries -- the sheet by name
     | Returns: {plate name: (title, description or "")}
     ----------------------*/"""
    rooms = ROOT / "tools" / "assets" / "rooms"
    want = {}
    for name, e in entries.items():
        want.setdefault(e["game"], {})[int(e["obj"])] = name
    out = {}
    for stem, objs in want.items():
        path = rooms / f"{stem}.json"
        if not path.is_file():
            continue
        for r in json.loads(path.read_text(encoding="utf-8"))["rooms"]:
            name = objs.get(int(r["obj"]))
            if name:
                out[name] = ((r.get("title") or "").strip(),
                             (r.get("description") or "").strip())
    return out


def page(names, entries):
    """/*----------------------
     | page
     | Description: The contact sheet -- old plate beside new drawing, the
     |     prompt underneath, one row per room.
     | Author: suinevere
     | Dependencies: html, pathlib
     | Globals: ART
     | Params: names -- the rooms drawn; entries -- the sheet by name
     | Returns: the page as a string
     ----------------------*/"""
    said = descriptions(entries)
    rows = []
    for n in names:
        e = entries[n]
        title, prose = said.get(n, ("", ""))
        if (ART / f"{n}.png").is_file():
            old = f'<img src="../{n}.png" alt="">'
        else:
            old = '<div class="none">no plate drawn yet</div>'
        if prose:
            words = (f'<p class="says"><b>the room says</b> '
                     f'{html.escape(prose)}</p>')
        else:
            words = ('<p class="says none-said"><b>the room says nothing</b> '
                     '&mdash; this room has no description at all, so the '
                     'picture is invented from the title. No prompt rule can '
                     'fix a picture here; only an override can.</p>')
        rows.append(
            f'<section><h2>{html.escape(n)} &mdash; '
            f'{html.escape(title or e.get("shows", ""))}</h2>'
            f'{words}'
            f'<div class="pair"><figure>{old}'
            f'<figcaption>before</figcaption></figure>'
            f'<figure><img src="{n}.png" alt="">'
            f'<figcaption>after</figcaption></figure></div>'
            f'<p class="prompt"><b>drawn from</b> '
            f'{html.escape(e["prompt"])}</p></section>')
    style = ("body{background:#111;color:#ddd;font:14px/1.5 system-ui;"
             "margin:2rem auto;max-width:1100px;padding:0 1rem}"
             "h1{font-size:18px}h2{font-size:15px;color:#9cf;margin:2rem 0 .5rem}"
             ".pair{display:flex;gap:1rem}figure{margin:0;flex:1}"
             "img{width:100%;display:block;border:1px solid #333}"
             "figcaption{color:#888;font-size:12px;padding-top:.25rem}"
             ".none{aspect-ratio:4/3;display:grid;place-items:center;"
             "border:1px dashed #444;color:#666}"
             ".says{color:#e8e0c8;background:#1e1c16;border-left:3px solid #7a6;"
             "padding:.6rem .8rem;margin:.25rem 0 .75rem;border-radius:0 4px 4px 0}"
             ".says b{color:#9c7;font-weight:600;margin-right:.4rem}"
             ".none-said{color:#e0b0b0;background:#221818;border-left-color:#a55}"
             ".none-said b{color:#d88}"
             ".prompt{color:#aaa;font-family:ui-monospace,monospace;"
             "font-size:12px;background:#1a1a1a;padding:.6rem;"
             "border-radius:4px;overflow-wrap:anywhere}"
             ".prompt b{color:#888;font-family:system-ui}")
    return ("<!doctype html><meta charset=utf-8>"
            "<title>prompt probe</title>"
            f"<style>{style}</style>"
            f"<h1>{len(names)} rooms &mdash; what the room says, the old "
            "plate, the new one, and the words it was drawn from</h1>"
            + "".join(rows))


def main(argv=None):
    """/*----------------------
     | main
     | Description: Draws the probe and writes the page to look at it in.
     | Author: suinevere
     | Dependencies: argparse, pathlib, forge_client
     | Globals: OUT, SHEET
     | Params: argv -- command line
     | Returns: 0
     ----------------------*/"""
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default=forge_client.HOST)
    ap.add_argument("--count", type=int, default=20,
                    help="how many rooms to draw (default 20)")
    ap.add_argument("--names",
                    help="comma-separated room names, instead of a spread")
    ap.add_argument("--out", default=str(OUT))
    ap.add_argument("--no-prose", action="store_true",
                    help="draw only rooms the story file describes with "
                         "nothing at all, which is 42%% of them and the hardest "
                         "class: their pictures are invented from a title")
    ap.add_argument("--cfg", type=float, default=forge_client.CFG,
                    help="guidance. The default 2.5 is what the Hyper merge "
                         "needs, and it is low enough that the negative prompt "
                         "barely applies -- which is why people keep appearing "
                         "in plates that forbid them. A non-distilled "
                         "checkpoint wants 6 to 7.")
    ap.add_argument("--steps", type=int, default=forge_client.STEPS)
    ap.add_argument("--checkpoint",
                    help="draw with another checkpoint the server has. The "
                         "installed one is a photorealism model, which is what "
                         "the painted look has been arguing with.")
    ap.add_argument("--compose-from", dest="compose_from",
                    help="an image to take the composition from, for the room "
                         "whose geometry a sentence keeps failing to pin down. "
                         "A path, or a filename in tools/assets/art/refs.")
    ap.add_argument("--denoise", type=float,
                    help="with a reference: how much of it to repaint. Under "
                         "0.4 is the photograph with a filter on it, over "
                         "0.75 loses the composition it was brought in for. "
                         "0.6 by default.")
    ap.add_argument("--style",
                    help="draw with this look instead of the sheet's, to try "
                         "one without regenerating and without committing to "
                         "it; the rest of every prompt is untouched")
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would be drawn and draw nothing")
    args = ap.parse_args(argv)

    entries = sheet()
    if args.names:
        names = [n.strip() for n in args.names.split(",") if n.strip()]
        for n in names:
            if n not in entries:
                raise SystemExit(f"probe_prompts: {n} is not in {SHEET.name}")
    else:
        names = pick(entries, args.count, args.no_prose)

    said = descriptions(entries)
    if args.dry_run:
        for n in names:
            title, prose = said.get(n, ("", ""))
            print(f"  {n} -- {title}")
            print(f"     room says: {prose or '(nothing: no description)'}")
            print(f"     drawn from: {styled(entries[n]['prompt'], args.style)}")
        print(f"would draw {len(names)} probes")
        return 0

    ckpt = forge_client.alive(args.host)
    if ckpt is None:
        raise SystemExit(f"probe_prompts: no Forge API at {args.host}. Start it "
                         "with --api; without that flag every endpoint is a 404.")
    if args.checkpoint:
        have = forge_client.models(args.host)
        if have and args.checkpoint not in have:
            raise SystemExit(
                f"probe_prompts: the server has no checkpoint "
                f"{args.checkpoint!r}. It has: {', '.join(have)}")
    print(f"Forge at {args.host}, checkpoint {args.checkpoint or ckpt}, "
          f"cfg {args.cfg}, steps {args.steps}")

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for i, n in enumerate(names, 1):
        e = entries[n]
        print(f"  [{i}/{len(names)}] {n} seed {e['seed']}", flush=True)
        prompt = styled(e["prompt"], args.style)
        neg = (forge_client.NEGATIVE + ", " + e["negative"]
               if e.get("negative") else forge_client.NEGATIVE)
        ckpt_of = args.checkpoint or e.get("checkpoint")
        ref = args.compose_from or e.get("compose_from")
        if ref:
            path = pathlib.Path(ref)
            if not path.is_file():
                path = REFS / ref
            if not path.is_file():
                raise SystemExit(f"probe_prompts: no reference image at {ref}")
            png = forge_client.img2img(
                prompt, int(e["seed"]), path.read_bytes(),
                denoise=args.denoise if args.denoise is not None
                else float(e.get("denoise", 0.6)),
                host=args.host, steps=args.steps, cfg=args.cfg,
                checkpoint=ckpt_of, negative=neg)
        else:
            png = forge_client.txt2img(
                prompt, int(e["seed"]), host=args.host, steps=args.steps,
                cfg=args.cfg, checkpoint=ckpt_of, negative=neg)
        (out / f"{n}.png").write_bytes(png)

    (out / "settings.txt").write_text(
        "\n".join([
            f"checkpoint {args.checkpoint or ckpt}",
            f"cfg {args.cfg}",
            f"steps {args.steps}",
            f"style {args.style or 'sheet default'}",
        ]) + "\n", encoding="utf-8")
    index = out / "index.html"
    shown = entries
    if args.style:
        shown = {n: {**entries[n],
                     "prompt": styled(entries[n]["prompt"], args.style)}
                 for n in names}
    index.write_text(page(names, shown), encoding="utf-8")
    print(f"\nDrew {len(names)} probes. Open {index}")
    print("The manifest was not touched -- delete the directory when done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
