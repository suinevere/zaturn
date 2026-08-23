"""Turn the scene vocabulary into search phrases for stock-photo fetching.

Description: Every scene is a place a photographer can point a camera at --
    FOREST, KITCHEN, CAVE, DOCK -- so each of scene_vocab.FETCH_NOUNS's own
    entries is already a complete, photographer-typed search phrase (see that
    module's own docstring on why "misty" beats "torchlit"). One query per
    noun, no cross product needed.

    The old mood vocabulary needed more machinery because several of its
    moods (HORROR, MAGIC, MYSTERY) were atmospheres, not places -- HORROR's
    keywords were corpse/decay/eerie/rotting/shadow/skeleton/stench,
    qualities, and nothing photographs ninety-nine distinct stenches. Those
    moods "donated" nouns from the moods whose places they haunted, then
    crossed an adjective against the borrowed noun to make it searchable.
    Every scene in this vocabulary names a place directly, so there is
    nothing left to donate, nothing to borrow, and no adjective needed to
    make a bare noun searchable. The donor concept is deleted rather than
    ported for exactly that reason.
Author: suinevere
Dependencies: collections, scene_vocab
Globals: N/A
"""
from collections import namedtuple

import scene_vocab as vocab

Query = namedtuple("Query", "game scene noun phrase")


def validate(nouns):
    """Reject a nouns mapping that names an unknown scene or an empty one.

    Description: Raises rather than warning. A typo'd scene key would
        silently fetch nothing for a real scene, which is not worth
        discovering after an afternoon of review.
    Author: suinevere
    Dependencies: scene_vocab
    Globals: N/A
    Params: nouns -- dict mapping scene name to an iterable of query nouns
    Returns: nouns, unchanged
    """
    for scene, words in nouns.items():
        if scene not in vocab.SCENE_INDEX:
            raise ValueError(f"{scene} is not a scene; expected one of {vocab.SCENES}")
        if not words:
            raise ValueError(f"{scene} has no nouns to search")
    return nouns


def build(nouns, game):
    """Turn one game's nouns mapping into one Query per phrase, grouped by scene.

    Description: Each phrase is already a complete search, so this is a
        straight one-to-one lift rather than a cross product -- the shape the
        old mood/donor/adjective build() had, kept only because harvest() and
        the manifest still key on a scene->[Query] plan. The game rides along
        on every Query because art now ships per game: the same phrase run for
        two stories must produce two independently curated pools, and the
        record harvest() writes needs to say which.
    Author: suinevere
    Dependencies: validate, scene_vocab
    Globals: N/A
    Params: nouns -- dict mapping scene to an iterable of query phrases;
        game -- the story stem this run is fetching for
    Returns: dict mapping scene to a list of Query, in nouns' own order
    """
    validate(nouns)
    return {scene: [Query(game, scene, noun, noun) for noun in words]
            for scene, words in nouns.items()}
