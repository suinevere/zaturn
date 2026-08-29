#!/usr/bin/env python3
"""Cut a v3 story down to the part the typeahead extractor reads.

Usage:
    python3 tools/trim_z3_vocab.py IN.z3 OUT.z3

The netbin embeds a story purely so input/typeahead_extract.c has a dictionary
and grammar to build a trie from -- no interpreter runs in that build, and the
bytes are never executed. The extractor reads the header, the object table, the
dictionary, the abbreviation table and the verb grammar; all of those live below
the header's base of high memory (offset 0x04). Everything from that address up
is Z-code and printed text, which the extractor never touches, and which for
Zork I is 76% of the file.

So the trim is a truncation at the high-memory base, and nothing has to be
relocated: every pointer the extractor follows already points below the cut.
saturn/tests/test_netbin_story_pin.py pins the generated blob to this rule, and
saturn/tests/test_netbin_typeahead.c proves full and trimmed produce the same
vocabulary, the same solution overlay and the same ranking.

The output is not a runnable story -- it has no code and no text. It is a
vocabulary donor, and only the extractor may be pointed at it.
"""
import sys


def high_memory_base(story):
    """Byte address where high memory starts, from the v3 header."""
    if len(story) < 0x40:
        raise ValueError("not a story file: shorter than a header")
    if story[0] != 3:
        raise ValueError(f"only v3 stories are supported, got v{story[0]}")
    base = (story[0x04] << 8) | story[0x05]
    if not 0x40 < base <= len(story):
        raise ValueError(f"high-memory base {base} outside the file")
    return base


def trim(story):
    return story[:high_memory_base(story)]


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    with open(argv[1], "rb") as f:
        story = f.read()
    out = trim(story)
    with open(argv[2], "wb") as f:
        f.write(out)
    sys.stderr.write(
        f"trim_z3_vocab: {len(story)} -> {len(out)} bytes "
        f"({100.0 * (len(story) - len(out)) / len(story):.1f}% dropped)\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
