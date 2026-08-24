#!/usr/bin/env python3
"""Walk one game from untagged rooms to pictures on the disc.

Description: The whole pipeline is six tools, two web pages and a build, and
    the order matters -- fetching art before the rooms are tagged fetches for
    scenes the game does not use, and building the disc before the generators
    run ships the previous table. This asks for a game, then stands at each
    step in turn: it starts what needs starting, opens the page you work in,
    waits, and then measures what you actually did before moving on.

    It does not build the disc. Every build in this project is the owner's to
    run and to watch; the last step prints the command and stops.

    Every path comes from this file's own location, and nothing here is
    Windows-specific -- process_game.bat is a four-line wrapper so the file can
    be double-clicked.
Author: suinevere
Dependencies: json, os, pathlib, subprocess, sys, webbrowser, scene_vocab,
    servers, art_review, art_status, fetch_art
Globals: ROOT, SCENE_URL, ART_URL
Run: tools/.venv/Scripts/python.exe tools/walkthrough.py [GAME]
"""
import json
import os
import pathlib
import subprocess
import sys
import webbrowser

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import art_review
import art_status
import fetch_art
import scene_vocab as vocab
import servers

ROOT = pathlib.Path(__file__).resolve().parent.parent

SCENE_URL = "http://127.0.0.1:8081"
ART_URL = "http://127.0.0.1:8080"
"""SCENE_URL / ART_URL

Description: Where the two review servers answer. Loopback rather than the
    hostname servers.py also prints, because this script opens a browser on
    the machine it is running on.
Author: suinevere
"""


def say(text=""):
    """Print one line of the script's own voice.

    Description: One place to write to, so the whole walkthrough can be
        indented consistently without every call site remembering to.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: text -- the line, or nothing for a blank one
    Returns: N/A
    """
    print(f"  {text}" if text else "")


def rule(title):
    """A step heading.

    Description: Steps are what the operator is counting, so each one gets a
        visible break rather than another indented line.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: title -- the heading text
    Returns: N/A
    """
    print()
    print("=" * 64)
    print(f"  {title}")
    print("=" * 64)


def anykey(prompt="Press any key to continue"):
    """Wait for one keypress, or a line when there is no terminal.

    Description: Reads a single character where the terminal allows it --
        msvcrt on Windows, raw mode elsewhere -- and falls back to a whole
        line when stdin is a pipe, so the script still works under a
        redirect instead of spinning on EOF. Returns the key so a caller can
        offer q to quit without a second prompt.
    Author: suinevere
    Dependencies: msvcrt (Windows) / termios, tty (POSIX)
    Globals: N/A
    Params: prompt -- what to show
    Returns: the character pressed, lowercased; "" at end of input
    """
    print(f"  {prompt}... ", end="", flush=True)
    key = ""
    if not sys.stdin.isatty():
        key = (sys.stdin.readline() or "").strip()[:1]
    elif os.name == "nt":
        import msvcrt
        key = msvcrt.getch().decode("latin-1", "ignore")
    else:
        import termios
        import tty
        fd = sys.stdin.fileno()
        saved = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            key = sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, saved)
    print()
    return key.lower()


def stop_if_quit(key):
    """End the walkthrough when the operator pressed q.

    Description: Leaves the servers running on the way out. Stopping them
        would undo the one step that is tedious to redo, and the operator
        pressing q is usually going to keep working in the browser.
    Author: suinevere
    Dependencies: sys
    Globals: N/A
    Params: key -- what anykey returned
    Returns: N/A; exits the process on q
    """
    if key == "q":
        say()
        say("Stopped. The servers are still up -- stop_asset_servers.bat ends them.")
        raise SystemExit(0)


def run(argv, why):
    """Run one of the project's own tools and show its output.

    Description: Streams rather than capturing, because these are the steps
        that take minutes and a silent script reads as a hung one. A non-zero
        exit is reported and not fatal: a fetch that failed on a missing API
        key should not throw away the tagging the operator just did.
    Author: suinevere
    Dependencies: subprocess
    Globals: ROOT
    Params: argv -- the command; why -- what to call it in the report
    Returns: True when it exited cleanly
    """
    say(f"$ {' '.join(str(a) for a in argv)}")
    print()
    sys.stdout.flush()
    code = subprocess.call([str(a) for a in argv], cwd=str(ROOT))
    print()
    if code != 0:
        say(f"{why} exited {code}. Nothing else has been changed.")
    return code == 0


def games():
    """Every story with a blessed scenes file, sorted.

    Description: The same list gen_scene_tables walks, rediscovered here so a
        checkout with a story added yesterday needs no edit.
    Author: suinevere
    Dependencies: pathlib
    Globals: ROOT
    Params: N/A
    Returns: a sorted list of story stems
    """
    scenes = ROOT / "tools" / "assets" / "scenes"
    rooms = ROOT / "tools" / "assets" / "rooms"
    if not scenes.is_dir():
        return []
    return [p.stem for p in sorted(scenes.glob("*.json"))
            if not p.stem.endswith(".review") and (rooms / (p.stem + ".json")).exists()]


def progress(game):
    """Where one game stands, measured rather than remembered.

    Description: Reads the four files the pipeline writes, so the numbers are
        what the next tool will actually see. Every lookup degrades to zero
        rather than raising: this runs before most of them exist.
    Author: suinevere
    Dependencies: json, pathlib, art_review, art_status
    Globals: ROOT
    Params: game -- a story stem
    Returns: dict of counts
    """
    scenes_dir = ROOT / "tools" / "assets" / "scenes"
    blessed, review = {}, []
    try:
        blessed = json.loads((scenes_dir / f"{game}.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        pass
    try:
        review = json.loads(
            (scenes_dir / f"{game}.review.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        pass

    manifest = fetch_art.load_manifest(ROOT / "tools" / "assets" / "art_manifest.json")
    mine = [r for r in manifest.values() if r.get("game") == game]
    wanted = {s for s in blessed.values() if s in vocab.SCENE_INDEX}
    accepted = {}
    for record in mine:
        if record.get("status") == art_status.ACCEPTED:
            scene = art_review.scene_of(record)
            accepted[scene] = accepted.get(scene, 0) + 1

    tga_dir = ROOT / "saturn" / "cd" / "data" / "TGA" / game
    return {
        "tagged": len(blessed),
        "left": len(review),
        "scenes": sorted(wanted),
        "accepted": accepted,
        "undecided": sum(1 for r in mine
                         if r.get("status") == art_status.CANDIDATE),
        "empty": sorted(s for s in wanted if not accepted.get(s)),
        "tga": len(list(tga_dir.glob("*.TGA"))) if tga_dir.is_dir() else 0,
    }


def choose(argv):
    """Ask which game to work on, unless one was named on the command line.

    Description: Lists what there is to do per story rather than bare names --
        the reason to pick one is usually that it is the least finished, and
        that is not something to have to look up first.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: argv -- command-line arguments
    Returns: a story stem, or None when the operator gave up
    """
    known = games()
    if not known:
        say("No stories have blessed scene files yet. Run tools/room_scenes.py.")
        return None
    if argv:
        want = argv[0].strip().upper()
        if want in known:
            return want
        say(f"{argv[0]} is not one of the {len(known)} known stories.")

    rule("Which game?")
    for i, stem in enumerate(known, start=1):
        p = progress(stem)
        note = []
        if p["left"]:
            note.append(f"{p['left']} rooms to tag")
        if p["empty"]:
            note.append(f"{len(p['empty'])} scenes with no art")
        if p["tga"]:
            note.append(f"{p['tga']} on disc")
        print(f"  {i:>2}. {stem:<10} {'; '.join(note) or 'nothing left to do'}")
    say()
    answer = input("  Number or stem (blank to give up): ").strip()
    if not answer:
        return None
    if answer.isdigit() and 1 <= int(answer) <= len(known):
        return known[int(answer) - 1]
    if answer.upper() in known:
        return answer.upper()
    say(f"{answer} is not one of them.")
    return None


def step_servers():
    """Start both review servers and confirm they answer.

    Description: Always starts rather than checking first: servers.start
        stops whatever was running, and a stale server from an older checkout
        serving the page you are about to work in is the failure this avoids.
    Author: suinevere
    Dependencies: servers
    Globals: SCENE_URL, ART_URL
    Params: N/A
    Returns: True when both answer
    """
    rule("Step 1 of 7 -- start the review servers")
    servers.start()
    up = all(servers.port_held(port) for _n, _s, port in servers.SERVERS)
    if not up:
        say()
        say("A server did not come up; see scene_server.err.log / art_server.err.log.")
    return up


def step_tag(game):
    """Tag the rooms the title rules refused.

    Description: The first step because everything downstream is keyed on it:
        the scenes a game needs, the art it fetches, and the music it plays
        all come from these verdicts.
    Author: suinevere
    Dependencies: webbrowser
    Globals: SCENE_URL
    Params: game -- a story stem
    Returns: N/A
    """
    rule(f"Step 2 of 7 -- tag {game}'s rooms")
    before = progress(game)
    say(f"{before['tagged']} rooms already tagged, {before['left']} groups left.")
    say()
    say("Tag what is left. Space skips a room to the back, Backspace undoes.")
    say("The tagged list and any room's own page can change a verdict later.")
    say()
    url = f"{SCENE_URL}/game/{game}"
    say(url)
    webbrowser.open(url)
    say()
    stop_if_quit(anykey("Work through the queue, then press any key"))

    after = progress(game)
    say()
    say(f"Now {after['tagged']} tagged ({after['tagged'] - before['tagged']} this "
        f"pass), {after['left']} left across {len(after['scenes'])} scenes.")
    if after["left"]:
        say("Untagged rooms draw no picture and fall back to the neutral music.")


def step_terms(game):
    """Check what the fetcher will search for.

    Description: Between tagging and fetching because it is the last moment
        the queries can be corrected for free -- afterwards the corrections
        cost another fetch.
    Author: suinevere
    Dependencies: webbrowser
    Globals: SCENE_URL
    Params: game -- a story stem
    Returns: N/A
    """
    rule(f"Step 3 of 7 -- check {game}'s search terms")
    say("Optional, but cheap now and expensive later: this is what the fetcher")
    say("will type into the stock-photo search for each scene.")
    say()
    say("Global keywords go here too -- a period or a setting, appended to every")
    say("search. Skip if the shipped phrases look right.")
    say()
    url = f"{SCENE_URL}/game/{game}/search"
    say(url)
    webbrowser.open(url)
    say()
    stop_if_quit(anykey("Adjust anything you want, then press any key"))


def step_fetch(game):
    """Download candidate pictures for the game's scenes.

    Description: Offers rather than assumes. A fetch costs API quota and
        minutes, and a second pass over a game whose pool is already deep is
        usually not what was wanted.
    Author: suinevere
    Dependencies: subprocess, sys
    Globals: ROOT
    Params: game -- a story stem
    Returns: N/A
    """
    rule(f"Step 4 of 7 -- fetch pictures for {game}")
    p = progress(game)
    say(f"{len(p['scenes'])} scenes to cover; {len(p['empty'])} have nothing yet.")
    if p["empty"]:
        say(f"empty: {', '.join(p['empty'])}")
    say()
    say("This calls the stock-photo APIs and needs a key in tools/.env.")
    say("Press f to fetch, or any other key to skip and curate what is there.")
    say()
    key = anykey("f to fetch, anything else to skip")
    stop_if_quit(key)
    if key != "f":
        say("Skipped.")
        return
    run([sys.executable, ROOT / "tools" / "fetch_art.py", "--game", game],
        "the fetch")


def step_curate(game):
    """Accept or reject what the fetch brought back.

    Description: The count that matters afterwards is scenes with no accepted
        picture, not pictures accepted: one scene left empty is a room that
        draws nothing, however deep the rest of the pool is.
    Author: suinevere
    Dependencies: webbrowser
    Globals: ART_URL
    Params: game -- a story stem
    Returns: N/A
    """
    rule(f"Step 5 of 7 -- curate {game}'s pictures")
    before = progress(game)
    say(f"{before['undecided']} undecided, "
        f"{sum(before['accepted'].values())} accepted so far.")
    say()
    say("a accepts, r rejects, u puts one back, arrows move between pictures.")
    say("Nothing is final -- the accepted and rejected filters are where you")
    say("change your mind.")
    say()
    url = f"{ART_URL}/game/{game}"
    say(url)
    webbrowser.open(url)
    say()
    stop_if_quit(anykey("Curate, then press any key"))

    after = progress(game)
    total = sum(after["accepted"].values())
    say()
    say(f"{total} accepted across {len(after['accepted'])} scenes "
        f"(the disc holds 99 per game).")
    if after["empty"]:
        say(f"Still no picture for: {', '.join(after['empty'])}")
        say("Those rooms will draw a blank background.")


def step_music(game):
    """Assign CD-DA tracks to the scenes this game uses.

    Description: After the art rather than before it, only because the art is
        the long pole; the two are independent and the page is shared by every
        story.
    Author: suinevere
    Dependencies: webbrowser
    Globals: SCENE_URL
    Params: game -- a story stem
    Returns: N/A
    """
    rule(f"Step 6 of 7 -- music for {game}'s scenes")
    say("Optional. One row per CD-DA track; name the scenes it should play")
    say("under. A scene named by exactly one track always sounds the same.")
    say("A scene named by none falls back to the neutral pool.")
    say()
    url = f"{SCENE_URL}/game/{game}/tracks"
    say(url)
    webbrowser.open(url)
    say()
    stop_if_quit(anykey("Assign what you want, then press any key"))


def step_generate(game):
    """Compile the tables and convert the pictures the disc will carry.

    Description: Both generators, in order: gen_scene_tables writes the room
        maps and the music masks, make_tga converts every accepted picture into
        the game's own 1..99 TGA range and writes the index table that names
        them. Running one without the other ships a table that describes
        pictures that are not there.
    Author: suinevere
    Dependencies: subprocess, sys
    Globals: ROOT
    Params: game -- a story stem
    Returns: N/A
    """
    rule(f"Step 7 of 7 -- generate the tables and the TGAs")
    say("Two generators. The first writes the room maps and the music masks,")
    say("the second converts every accepted picture into this game's TGA range.")
    say()
    stop_if_quit(anykey("Press any key to run them"))
    ok = run([sys.executable, ROOT / "tools" / "gen_scene_tables.py"],
             "the table generator")
    if ok:
        run([sys.executable, ROOT / "tools" / "make_tga.py"], "the TGA converter")
    p = progress(game)
    say(f"{game} now has {p['tga']} TGA(s) on the disc tree.")


def step_build(game):
    """Hand the disc build back to the operator.

    Description: Deliberately not run. Every build in this project is the
        owner's to start and to watch -- the toolchain prints warnings worth
        reading and the emulator needs a person in front of it -- so this
        prints the command and stops.
    Author: suinevere
    Dependencies: N/A
    Globals: ROOT
    Params: game -- a story stem
    Returns: N/A
    """
    rule("Done here -- the disc build is yours to run")
    p = progress(game)
    say(f"{game}: {p['tagged']} rooms tagged, "
        f"{sum(p['accepted'].values())} pictures accepted, {p['tga']} on the disc.")
    if p["empty"]:
        say(f"Scenes still without art: {', '.join(p['empty'])}")
    say()
    say("Build the CD image with:")
    say()
    print("      cd saturn && compile-cd.bat")
    say()
    say("then run it however you normally do. This script does not build or")
    say("launch anything -- that has always been yours to watch.")
    say()
    say("The servers are still up. stop_asset_servers.bat ends them.")


def main(argv):
    """Walk one game from untagged rooms to pictures on the disc.

    Description: Seven steps, each measuring what the last one actually
        achieved rather than assuming it. q at any prompt stops and leaves the
        servers running.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: argv -- optionally the story stem, to skip the menu
    Returns: 0
    """
    rule("Scene and picture walkthrough")
    say("One game, start to finish. q at any prompt stops.")
    game = choose(argv)
    if game is None:
        say("Nothing chosen.")
        return 0
    if not step_servers():
        return 1
    step_tag(game)
    step_terms(game)
    step_fetch(game)
    step_curate(game)
    step_music(game)
    step_generate(game)
    step_build(game)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
