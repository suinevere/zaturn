"""Derive photographable place-nouns from the scene tagging vocabulary.

Description: Reads scene_vocab.FETCH_NOUNS, the stock-photo query nouns
    already authored for each scene. Deriving rather than duplicating is the
    point: a scene's fetch vocabulary can never drift from its tagging
    vocabulary, because both now come from scene_vocab.
Author: suinevere
Dependencies: scene_vocab
Globals: N/A
"""
import scene_vocab as vocab


def nouns_for_scene(scene):
    """The stock-photo query nouns fetch_art should use for one scene.

    Description: Looks scene up in scene_vocab.FETCH_NOUNS directly, so a
        noun added there gains art coverage with no edit here. An unknown
        scene name gets an empty tuple rather than a KeyError -- a caller
        iterating scene_vocab.SCENES never needs to guard the lookup, and a
        caller passing a typo sees "no nouns" instead of a crash.
    Author: suinevere
    Dependencies: scene_vocab
    Globals: N/A
    Params: scene -- an SC_* scene name, e.g. "FOREST"
    Returns: a tuple of query nouns, possibly empty
    """
    return vocab.FETCH_NOUNS.get(scene, ())
