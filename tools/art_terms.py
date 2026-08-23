#!/usr/bin/env python3
"""Hand-edited search terms: per scene, and per game.

Description: Owns tools/assets/search_terms.json, the layer that lets someone
    fix a scene's stock-photo queries without editing Python, and give a story
    words that go into every one of its searches.

    Two independent things live here. `scenes` replaces a scene's phrases
    outright -- the shipped vocabulary is a starting guess and the person
    looking at the results is better informed than it is. `games` is additive:
    the words are appended to every phrase that story searches, which is what
    a period or a setting is. "1930s" is not a place to photograph, so it can
    never be a phrase of its own; it is a filter on the places.

    Effective order for a scene's phrases is JSON override, then the genre
    rewording in art_nouns.GENRE_NOUNS, then scene_vocab.FETCH_NOUNS. Only the
    first layer is editable at runtime, because the other two are the shipped
    defaults it exists to correct.
Author: suinevere
Dependencies: json, pathlib, scene_vocab
Globals: TERMS_PATH
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import scene_vocab as vocab

TERMS_PATH = pathlib.Path("tools") / "assets" / "search_terms.json"


def empty():
    """The document a first run writes.

    Description: Both sections present and empty, so an editor never has to
        create a key before it can add one.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: N/A
    Returns: a fresh terms document
    """
    return {"scenes": {}, "games": {}}


def load(root):
    """Read the hand-edited terms.

    Description: A missing or unreadable file means "nothing overridden",
        which leaves every scene on its shipped phrases -- the same
        everything-degrades rule the rest of these tools follow.
    Author: suinevere
    Dependencies: pathlib, json
    Globals: TERMS_PATH
    Params: root -- repo root
    Returns: a terms document with both sections present
    """
    path = pathlib.Path(root) / TERMS_PATH
    if not path.exists():
        return empty()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return empty()
    return {"scenes": {k: list(v or ()) for k, v in (data.get("scenes") or {}).items()},
            "games": {k: list(v or ()) for k, v in (data.get("games") or {}).items()}}


def save(root, data):
    """Write the terms, sorted so its diffs stay readable.

    Description: Drops empty lists on the way out. An explicit "no terms" and
        an absent key mean the same thing, so keeping both spellings would
        only invite the question of which one is which.
    Author: suinevere
    Dependencies: pathlib, json
    Globals: TERMS_PATH
    Params: root -- repo root; data -- a terms document
    Returns: N/A
    """
    clean = {
        "scenes": {k: list(v) for k, v in sorted((data.get("scenes") or {}).items()) if v},
        "games": {k: list(v) for k, v in sorted((data.get("games") or {}).items()) if v},
    }
    path = pathlib.Path(root) / TERMS_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(clean, indent=1, sort_keys=True) + "\n",
                    encoding="utf-8")


def rename_scene(data, old, new):
    """Move a scene's override to a new name, in place.

    Description: Part of the rename cascade. A scene with no override is
        nothing to move, which is the common case and not an error.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: data -- a terms document, mutated; old, new -- scene names
    Returns: N/A
    """
    scenes = data.setdefault("scenes", {})
    if old in scenes:
        scenes[new] = scenes.pop(old)


def game_terms(data, game):
    """The words appended to every search this story runs.

    Description: A story with none is the ordinary case; the shipped
        vocabulary already names places, and a filter is only wanted where the
        places would otherwise come back in the wrong century.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: data -- a terms document; game -- a story stem
    Returns: a list of words, possibly empty
    """
    return list((data.get("games") or {}).get(game) or ())


def scene_override(data, scene):
    """The hand-edited phrases for one scene, or None.

    Description: None rather than an empty list, so a caller can tell "left
        alone" from "deliberately emptied" -- save() never writes the latter,
        but a document edited by hand might.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: data -- a terms document; scene -- a scene name
    Returns: a list of phrases, or None
    """
    found = (data.get("scenes") or {}).get(scene)
    return list(found) if found else None


def apply_terms(phrases, terms):
    """Append a story's filter words to each of its search phrases.

    Description: Appended to each phrase rather than searched separately,
        because both stock-photo APIs treat extra words as a narrowing: "cave"
        plus "1930s" asks for a 1930s cave, while a separate search for
        "1930s" asks for anything at all from that decade. A word already in
        the phrase is not repeated -- "spaceship corridor" filtered by
        "spaceship" would ask for it twice and match less, not more.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: phrases -- the scene's own search phrases; terms -- the story's
        filter words
    Returns: a tuple of phrases
    """
    if not terms:
        return tuple(phrases)
    out = []
    for phrase in phrases:
        have = set(phrase.lower().split())
        extra = [t for t in terms if t.lower() not in have]
        out.append(" ".join([phrase] + extra) if extra else phrase)
    return tuple(out)


def validate(data):
    """Every complaint the document deserves, as plain sentences.

    Description: Reports rather than raises, and reports all of them at once.
        An unknown scene name is the important one: its override is silently
        ignored and looks exactly like a scene nobody has corrected.
    Author: suinevere
    Dependencies: scene_vocab
    Globals: N/A
    Params: data -- a terms document
    Returns: a list of message strings, empty when the document is sound
    """
    problems = []
    for scene, phrases in sorted((data.get("scenes") or {}).items()):
        if scene not in vocab.SCENE_INDEX:
            problems.append(f"scenes/{scene} is not a scene")
            continue
        if not all(isinstance(p, str) and p.strip() for p in phrases):
            problems.append(f"scenes/{scene} has an empty phrase")
    for game, words in sorted((data.get("games") or {}).items()):
        if not all(isinstance(w, str) and w.strip() for w in words):
            problems.append(f"games/{game} has an empty term")
    return problems


def main(argv):
    """Print the overrides in effect.

    Description: A read-only view for the command line; the editing surface is
        the review server's search page.
    Author: suinevere
    Dependencies: pathlib
    Globals: N/A
    Params: argv -- unused
    Returns: 0, or 1 when the document does not validate
    """
    root = pathlib.Path(__file__).resolve().parent.parent
    data = load(root)
    problems = validate(data)
    for line in problems:
        print(f"  {line}")
    for scene, phrases in sorted(data["scenes"].items()):
        print(f"  {scene:<10} {', '.join(phrases)}")
    for game, words in sorted(data["games"].items()):
        print(f"  {game:<10} + {' '.join(words)}")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
