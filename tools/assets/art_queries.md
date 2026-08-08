# art_queries.json

This file is the only place search terms are written. `tools/art_queries.py`
turns it into the phrases the fetcher searches for; nothing else in the
pipeline invents a word. Changing an adjective, a donor, an extra noun, or an
exclusion means re-running the fetcher and reviewing the fresh candidates it
brings back -- the old images do not get relabeled, they get replaced.

Each of the twelve moods (`WILDER`, `UNDRGRND`, `WATER`, `NAUTICAL`, `TOWN`,
`DUNGN`, `DESERT`, `MAGIC`, `SCIFI`, `HORROR`, `MYSTERY`, `HOUSE`) has one
entry with five fields.

## adjectives

The mood's own look -- the words that make a picture read as *this* mood
rather than just a place. `art_queries.build` crosses every adjective with
every reachable noun, so this list is the fastest lever for raising or
lowering a mood's query count.

## donors

Which moods' place-nouns this mood is allowed to borrow, by folder name (see
`tools/art_nouns.py`'s `MOODS`). Most moods donate only to themselves --
`WILDER` searches `WILDER` nouns, `HOUSE` searches `HOUSE` nouns.

Three moods must borrow because their classifier keywords are qualities, not
places. `TC_HORROR`'s keywords are corpse, decay, eerie, rotting, shadow,
skeleton, stench -- nothing photographs ninety-nine distinct stenches, so
`HORROR` borrows nouns from `HOUSE`, `UNDRGRND`, and `DUNGN`, the moods whose
places it haunts. `TC_MAGIC` and `TC_MYSTERY` have the same shape: `MAGIC`
borrows from `HOUSE`, `UNDRGRND`, and `WILDER`; `MYSTERY` borrows from `HOUSE`
and `TOWN`. Without a donor, a mood built entirely from qualities reaches zero
nouns and `build()` raises rather than silently fetching nothing.

## extra_nouns

A search word the classifier does not know -- either because it is too
specific to belong in `room_class_data.c` (a "cavern lake" is still just a
cave to the classifier) or because a mood needs more raw material than its
donors supply. These nouns are tagged with donor `"EXTRA"` in the built
`Query`, so a reviewer can tell at a glance that the word came from this file
and not from the classifier's own vocabulary.

## exclude_nouns

A word that keeps returning junk -- a donor noun that photographs badly for
this mood's adjectives, or that collides with a different mood's identity. For
example `HORROR` excludes `kitchen` and `porch` even though it borrows from
`HOUSE`, because "abandoned kitchen" and "derelict porch" search results skew
toward real-estate listings rather than horror set-dressing.

## target

How many pictures this mood needs to fill its folder, capped at 99 (three
digits, `NN_adjective_noun.tga` naming). `art_queries.build` raises if a
mood's `adjectives x reachable nouns` doesn't clear its target -- fix it by
adding adjectives, adding a donor, or adding `extra_nouns`, in that order of
preference (a new donor changes an editorial claim about what a mood looks
like; a new extra_noun is closer to free).
