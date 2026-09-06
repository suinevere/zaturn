#!/usr/bin/env python3
"""Hold the PCM sound-effect budget against the one game on the disc that has one.

LURKING.BLB is the only sound blorb shipped, and Lurking Horror is also the
largest story -- so the single game with sound effects is the one whose C heap
has least left behind its own image. That is not a coincidence to work around,
it is the whole shape of the problem:

  measured heap 192,800, LURKING.Z3 129,704, free behind it 63,096
  the fourteen samples run 7,728 to 59,990 bytes

At the heap this started from -- 135,616, before typeahead_extract's build
scratch came out of .bss -- none of the fourteen fit at all. That is no longer
the argument, and this file's own check said so by failing the moment the heap
moved. The argument now is the floor: test_hwram_budget requires 16,384 bytes
left behind the largest story and names a PCM slice as one of the things that
floor is for, so a sample drawn from the heap is drawn from the runway. The
largest would leave 3,106. And one sample is not the shape of the problem
anyway -- sound.cxx caches every slice for the game's lifetime so a re-trigger
never seeks, because a seek stops the CD-DA track, and the two largest together
are 118,004 against 63,096.

So the fix is not to free heap -- freeing heap is a separate piece of work that
happened alongside this one. It is to allocate from a different zone. sound.cxx's
load_slice takes its buffers from LowWorkRam, gated the way item_art_open and
load_area gate theirs, and room_art's load_area can ask for that space back when
an area archive needs it (sound_release_cache).

What this file holds:

  1. That the slices still come from Low Work RAM. A move back to the C heap is
     a silent regression -- the effect simply never sounds, with no message,
     which is exactly the state this started in.
  2. That the gate is checked before the allocation rather than after, so the
     floor survives a sample that would technically have fitted.
  3. That the read target is alignment-checked, which LoadBytes requires and the
     High Work RAM version never did.
  4. That the largest sample fits Low Work RAM beside everything else a session
     holds there. It does, by ~3 KB -- and only because Lurking has no item
     pane. That margin is the reason this check exists rather than being
     obvious.
  5. That a slice which loads but finds no free cache entry is owned by its slot
     rather than by nobody.
  6. That the archive can still reclaim from the cache.

The reserves are imported from test_lwram_budget rather than restated, because
two copies of a budget drift and only one of them is ever updated.

Run as a human-readable report: python saturn/tests/test_sound_slice_budget.py
Run as tests: pytest saturn/tests/test_sound_slice_budget.py
"""
import re
import sys
import pathlib
import struct

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import test_lwram_budget as lwram          # noqa: E402  (path set above)
import test_hwram_budget as hwram          # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "saturn" / "src"
BLB = ROOT / "saturn" / "cd" / "data" / "Z3" / "LURKING.BLB"
STORY = ROOT / "saturn" / "cd" / "data" / "Z3" / "LURKING.Z3"


def source(*parts):
    return SRC.joinpath(*parts).read_text(encoding="utf-8", errors="replace")


def body_of(text, name):
    """test_hwram_budget's function_body, made to see extern "C" definitions.

    Its regex allows only [\\w :*&] between the start of the line and the name,
    and the quotes in `extern "C"` are neither -- so every entry point in
    sound.cxx is invisible to it and a check on one reads as "not found" rather
    than as a failure. Dropping the linkage specifier is enough, and it moves no
    brace the walk depends on.
    """
    return hwram.function_body(text.replace('extern "C" ', ""), name)


def slice_keep_free():
    """load_slice's floor, read off the source rather than restated here."""
    m = re.search(r"(?m)^#define\s+SLICE_KEEP_FREE\s+\(([^)]*)\)",
                  source("sound", "sound.cxx"))
    if m is None:
        raise RuntimeError("SLICE_KEEP_FREE is not #defined in sound.cxx -- if it "
                           "was renamed, rename it here too rather than inlining "
                           "a number that will go stale")
    return eval(m.group(1), {"__builtins__": {}}, {})


def aiff_rate(raw):
    """The 80-bit IEEE extended sample rate in a COMM chunk, as an integer.

    The same arithmetic sound_blorb.c's ext_rate does, so a disagreement here
    means one of the two is wrong rather than that they measure different things.
    """
    exp = struct.unpack(">H", raw[:2])[0] & 0x7FFF
    hi = struct.unpack(">I", raw[2:6])[0]
    if exp == 0:
        return 0
    return int(hi * (2.0 ** (exp - 16383 - 31)) + 0.5)


def samples():
    """Every sound in LURKING.BLB as (number, bytes, rate), largest first.

    Mirrors sound_blorb.c's parse: the FORM/IFRS header, the RIdx table, and for
    each 'Snd ' entry the COMM and SSND chunks of the AIFF at its offset. Reading
    the shipped file rather than a table written here is the point -- a sample
    that grows has to move these numbers on its own.
    """
    b = BLB.read_bytes()
    if b[0:4] != b"FORM" or b[8:12] != b"IFRS":
        raise RuntimeError(f"{BLB.name} is not a Blorb")
    if b[12:16] != b"RIdx":
        raise RuntimeError(f"{BLB.name} has no resource index")
    nres = struct.unpack(">I", b[20:24])[0]
    out = []
    for i in range(nres):
        e = b[24 + i * 12: 36 + i * 12]
        if e[0:4] != b"Snd ":
            continue
        num = struct.unpack(">I", e[4:8])[0]
        start = struct.unpack(">I", e[8:12])[0]
        if b[start:start + 4] != b"FORM" or b[start + 8:start + 12] != b"AIFF":
            continue
        formlen = struct.unpack(">I", b[start + 4:start + 8])[0]
        p, rate, length = 12, 0, 0
        while p + 8 <= formlen + 8:
            c = start + p
            clen = struct.unpack(">I", b[c + 4:c + 8])[0]
            if b[c:c + 4] == b"COMM":
                rate = aiff_rate(b[c + 16:c + 26])
            elif b[c:c + 4] == b"SSND":
                length = clen - 8
            p += 8 + clen + (clen & 1)
            if rate and length:
                break
        if length:
            out.append((num, length, rate))
    if not out:
        raise RuntimeError(f"no sounds parsed out of {BLB.name}")
    return sorted(out, key=lambda s: -s[1])


def require_blorb():
    if not BLB.is_file():
        pytest.skip("cd/data/Z3/LURKING.BLB is not in this checkout, so the only "
                    "samples the disc ships cannot be measured here")


def load_slice_body():
    body = body_of(source("sound", "sound.cxx"), "load_slice")
    assert body is not None, (
        "load_slice is not where this file can find it in sound.cxx. If it moved "
        "or changed shape, move these checks with it rather than deleting them -- "
        "what they hold is that effect buffers do not go back to the C heap.")
    return body


def test_slices_come_from_low_work_ram():
    """The regression that would be silent.

    A sample allocated from the C heap comes out of the runway
    test_hwram_budget's floor reserves, and the two largest together do not fit
    behind LURKING.Z3 at all -- so a cache there fails partway through a session
    rather than at the start of one. When load_slice returns NULL,
    saturn_sound_effect returns without playing and the game is simply quiet: no
    message, nothing in a log, nothing a player could report beyond "I do not
    think this game has sound" -- which is what was believed about it.
    """
    text = source("sound", "sound.cxx")
    body = load_slice_body()
    assert "LowWorkRam::Malloc" in body, (
        "load_slice no longer allocates from Low Work RAM. The sample buffers are "
        "7,728 to 59,990 bytes and the C heap has 5,912 free behind the only "
        "story that has any, so this is the difference between a game with sound "
        "effects and a game without.")
    assert "HighWorkRam::Malloc" not in text, (
        "sound.cxx allocates from High Work RAM somewhere. That zone is the C "
        "heap the story image lives in; see this file's header for the sizes.")


def test_the_gate_comes_before_the_allocation():
    """Checked free space, not a NULL afterwards.

    The difference matters because a successful malloc is not the same as an
    affordable one: TLSF will happily hand over the last 60 KB of the zone and
    leave the save scratch with nothing, and saturn_scratch_alloc is not gated at
    all -- it returns NULL and the story prints its own failure line.
    """
    body = load_slice_body()
    assert "GetFreeSpace" in body, (
        "load_slice allocates without checking free space first. Every other "
        "Low Work RAM claimant in this client gates before it allocates "
        "(item_art_open, load_area, song_bank_cd); this one takes the largest "
        "single blocks of the three.")
    assert body.index("GetFreeSpace") < body.index("LowWorkRam::Malloc"), (
        "load_slice checks free space after allocating, which measures the state "
        "its own allocation produced rather than the one that should have "
        "prevented it.")
    assert "SLICE_KEEP_FREE" in body, (
        "load_slice's gate no longer names SLICE_KEEP_FREE, so whatever it now "
        "holds back is not the floor this file and its header describe.")


def test_the_read_target_is_alignment_checked():
    """LoadBytes wants a long-aligned destination.

    load_area and item_art_open both check this and refuse rather than corrupt.
    The High Work RAM version of load_slice never did, which was survivable only
    for as long as that allocator happened to return aligned blocks -- not a
    property either allocator promises.
    """
    body = load_slice_body()
    assert "& 3" in body, (
        "load_slice hands LoadBytes a buffer without checking it is long-aligned. "
        "room_art.cxx and item_art.cxx both check; this one is reading into the "
        "same kind of buffer through the same call.")


def test_a_sample_would_spend_the_story_runway():
    """Why the zone is not the C heap, argued from the floor rather than from a
    number that moved.

    The first version of this check asserted that the heap could not hold even
    the smallest sample -- true when it was written, and false a few hours later,
    because freeing typeahead_extract's build scratch out of .bss returned 57 KB
    to the heap in the same session. It failed, which is what it was for; this is
    the re-derivation it asked for.

    Fitting was never the real argument. test_hwram_budget's STORY_HEADROOM says
    a build has to leave a floor behind the largest story, and names what that
    floor is for in the same breath: "no PCM slice off a .BLB, no item picture,
    no save or restore scratch". A sample taken from the heap is drawn from
    exactly that runway. The largest is 59,990 against the roughly 63,000 the
    heap has behind LURKING.Z3, so playing one sound effect would leave the
    running story under the floor a separate test is holding -- which is a
    contradiction, not a tight fit.
    """
    require_blorb()
    if not STORY.is_file():
        pytest.skip("cd/data/Z3/LURKING.Z3 is not in this checkout "
                    "(cd/data/Z3/*.z3 is gitignored)")
    heap = hwram.heap_bytes()
    if heap is None:
        pytest.skip(hwram.NO_HEAP)
    behind = heap - STORY.stat().st_size
    biggest = samples()[0][1]
    assert behind - biggest < hwram.STORY_HEADROOM, (
        f"the C heap leaves {behind} bytes behind LURKING.Z3, and the largest "
        f"sample ({biggest}) would still leave {behind - biggest} of them -- over "
        f"test_hwram_budget's {hwram.STORY_HEADROOM}-byte floor. The heap could "
        "host a sample now without breaking that floor, so the reason these "
        "buffers live in Low Work RAM has changed again. Re-derive it: the "
        "remaining arguments are that the cache holds several at once (see the "
        "next check) and that a session-long claim on the heap is a claim on "
        "what the next story image needs contiguously.")


def test_the_story_heap_could_never_hold_the_cache():
    """And the cache is the point, not one sample.

    sound.cxx keeps every slice it loads for the game's lifetime, because a
    re-trigger that re-read the disc would seek, and a seek stops the CD-DA track
    the room engine is playing. A zone that can hold one sample at a time is not
    somewhere a cache can live, whatever the floor says.
    """
    require_blorb()
    if not STORY.is_file():
        pytest.skip("cd/data/Z3/LURKING.Z3 is not in this checkout "
                    "(cd/data/Z3/*.z3 is gitignored)")
    heap = hwram.heap_bytes()
    if heap is None:
        pytest.skip(hwram.NO_HEAP)
    behind = heap - STORY.stat().st_size
    two = sum(s[1] for s in samples()[:2])
    assert two > behind, (
        f"the two largest samples together ({two}) now fit in the {behind} bytes "
        "the C heap has behind LURKING.Z3. The cache could live there, so say why "
        "it should not rather than leaving a check that has stopped saying "
        "anything.")


def test_the_largest_sample_fits_low_work_ram_beside_the_session():
    """The check the whole change turns on, and it is close.

    What a Lurking session holds in Low Work RAM: its typeahead trie (charged at
    the library's worst case, Wishbringer's, which is larger than Lurking's own
    773-word vocabulary needs), one area archive plus its decode target and
    load_area's 4 KB, the map parchment, and the song-bank slot. The save scratch
    is not added because SLICE_KEEP_FREE already holds it back.

    The item pane is deliberately absent, and that absence is load-bearing rather
    than an oversight: scene/game_items.inc declares ITEM_GAME_N 1, so Zork I is
    the only story with an item-picture table and item_art_open refuses before
    touching the disc for every other one -- Lurking included. Put its 50,056
    bytes back and the largest sample stops fitting, which is why the exclusion
    is asserted here rather than assumed.
    """
    require_blorb()
    b = lwram.compute_budget()
    resident = (b["resident"] + lwram.TRIE_RESERVE + b["parchment"] + b["song_bank"])
    free = lwram.LWRAM_TOTAL - resident
    biggest = samples()[0]
    need = biggest[1] + slice_keep_free()
    assert need <= free, (
        f"the largest sample (number {biggest[0]}, {biggest[1]} bytes) plus "
        f"load_slice's {slice_keep_free()}-byte floor is {need}, against the "
        f"{free} bytes Low Work RAM has left once a session's trie "
        f"({lwram.TRIE_RESERVE}), area archive and decode target "
        f"({b['resident']}), parchment ({b['parchment']}) and song-bank slot "
        f"({b['song_bank']}) are resident. It is {need - free} bytes over. The "
        "effect would not play, and nothing would say so.")


def test_the_item_pane_is_what_makes_the_margin():
    """Records how thin the check above is, so a change that eats it is visible.

    Not a requirement that the margin stay small -- a requirement that if it ever
    grows past the item pane's own size, somebody notices, because at that point
    the pane could come back and the exclusion above would stop being the reason
    anything fits.
    """
    require_blorb()
    b = lwram.compute_budget()
    resident = (b["resident"] + lwram.TRIE_RESERVE + b["parchment"] + b["song_bank"])
    margin = lwram.LWRAM_TOTAL - resident - samples()[0][1] - slice_keep_free()
    assert margin < lwram.ITEM_ART_RESERVE, (
        f"the largest sample now clears its budget by {margin} bytes, more than "
        f"the item pane's own {lwram.ITEM_ART_RESERVE}. Low Work RAM has more "
        "room than this file's reasoning assumes -- re-derive the reserves "
        "rather than leaving a check that no longer describes the zone.")


def test_an_uncached_slice_is_owned_by_its_slot():
    """The leak the old heap was too small to reach.

    cached_slice used to record a loaded buffer in the first free cache entry and
    return it either way. With all NCACHE entries taken it returned a buffer that
    was in no cache entry, and the calling slot set its own buf to nullptr, so
    free_slot freed nothing and sound_stop_all knew nothing about it -- one
    sample's worth of Low Work RAM gone per play, for the rest of the session.
    Unreachable while the heap fitted at most two slices; reachable now.
    """
    text = source("sound", "sound.cxx")
    body = body_of(text, "cached_slice")
    assert body is not None, "cached_slice is not where this file can find it"
    assert "cached_out" in body, (
        "cached_slice no longer reports whether the cache took ownership of the "
        "buffer it returns, so a slice that finds no free entry is freed by "
        "nobody.")
    play = body_of(text, "saturn_sound_effect")
    assert play is not None, "saturn_sound_effect is not where this file can find it"
    assert "cached ? nullptr : buf" in re.sub(r"\s+", " ", play), (
        "saturn_sound_effect does not take ownership of an uncached slice, so the "
        "buffer cached_slice handed it is leaked.")


def test_the_archive_can_reclaim_from_the_cache():
    """The other half of the trade, and it only runs one way.

    Area archives are 16 KB to 418 KB and load_area refuses silently when one
    will not fit -- rooms with no picture and no message. A cached slice is a CD
    read already paid for, and load_area is seeking anyway, so it yields. The
    archive never yields to a sample: a missing background is silent, a missing
    sound effect is silent too, and only one of them is what the player is
    looking at.
    """
    body = body_of(source("video", "room_art.cxx"), "load_area")
    assert body is not None, "load_area is not where this file can find it"
    assert "sound_release_cache" in body, (
        "load_area no longer asks the slice cache for its space back before "
        "refusing an archive. Without that, a session that has played a few "
        "effects can walk into an area whose archive will not fit and lose its "
        "backgrounds for good, silently.")


def _print_report():
    print("  LWRAM                    %8d" % lwram.LWRAM_TOTAL)
    if not BLB.is_file():
        print("  LURKING.BLB              missing from this checkout")
        return
    rows = samples()
    b = lwram.compute_budget()
    resident = (b["resident"] + lwram.TRIE_RESERVE + b["parchment"] + b["song_bank"])
    free = lwram.LWRAM_TOTAL - resident
    keep = slice_keep_free()

    print("  samples in LURKING.BLB   %8d" % len(rows))
    for num, size, rate in rows[:3]:
        print("    sound %-3d              %8d  %d Hz" % (num, size, rate))
    print("    ...")
    for num, size, rate in rows[-2:]:
        print("    sound %-3d              %8d  %d Hz" % (num, size, rate))
    print("  ------------------------------------")
    print("  trie reserve             %8d" % lwram.TRIE_RESERVE)
    print("  archive + frame + 4K     %8d" % b["resident"])
    print("  parchment                %8d" % b["parchment"])
    print("  song bank slot           %8d" % b["song_bank"])
    print("  item pane (NOT Lurking)  %8d" % lwram.ITEM_ART_RESERVE)
    print("  ------------------------------------")
    print("  free for slices          %8d" % free)
    print("  load_slice floor         %8d" % keep)
    print("  largest sample + floor   %8d  (%s)"
          % (rows[0][1] + keep, "OVER" if rows[0][1] + keep > free else "fits"))
    fits = sum(1 for _, size, _ in rows if size + keep <= free)
    print("  samples that fit alone   %8d of %d" % (fits, len(rows)))

    heap = hwram.heap_bytes()
    if heap is not None and STORY.is_file():
        behind = heap - STORY.stat().st_size
        two = sum(size for _, size, _ in rows[:2])
        print("  ------------------------------------")
        print("  C heap behind LURKING    %8d" % behind)
        print("  largest sample leaves    %8d  of the %d-byte story floor"
              % (behind - rows[0][1], hwram.STORY_HEADROOM))
        print("  two largest at once      %8d  (%s -- why the cache is not here)"
              % (two, "OVER" if two > behind else "fits"))


def main():
    try:
        _print_report()
    except RuntimeError as e:
        print(str(e), file=sys.stderr)
        sys.exit(1)

    checks = [
        (test_slices_come_from_low_work_ram, "slices come from Low Work RAM"),
        (test_the_gate_comes_before_the_allocation, "the gate precedes the allocation"),
        (test_the_read_target_is_alignment_checked, "the read target is aligned"),
        (test_a_sample_would_spend_the_story_runway, "a sample spends the story runway"),
        (test_the_story_heap_could_never_hold_the_cache, "the heap could never hold the cache"),
        (test_the_largest_sample_fits_low_work_ram_beside_the_session,
         "the largest sample fits beside the session"),
        (test_the_item_pane_is_what_makes_the_margin, "the item pane makes the margin"),
        (test_an_uncached_slice_is_owned_by_its_slot, "an uncached slice has an owner"),
        (test_the_archive_can_reclaim_from_the_cache, "the archive can reclaim"),
    ]
    fails = 0
    for fn, label in checks:
        try:
            fn()
        except AssertionError as e:
            print(f"\ntest_sound_slice_budget: FAILED -- {label}\n{e}", file=sys.stderr)
            fails += 1
        except Exception as e:                      # a skip outside pytest
            print(f"\ntest_sound_slice_budget: SKIPPED -- {label} ({e})")

    if fails:
        sys.exit(1)
    print("\ntest_sound_slice_budget: OK")


if __name__ == "__main__":
    main()
