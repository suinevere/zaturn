"""Tests for tools/gen_cues.py -- the situational-cue table generator.

The generator's whole job is to turn names into the object numbers of one
specific story file, so these check it against that file rather than against a
fixture: a stub story would only prove the arithmetic, and the arithmetic was
never the risk.
"""
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import gen_cues  # noqa: E402


@pytest.fixture(scope="module")
def story():
    if not gen_cues.STORY.exists():
        pytest.skip(f"{gen_cues.STORY} not present")
    return gen_cues.load_story()


def test_story_is_the_release_the_table_was_measured_from(story):
    assert story.release == gen_cues.RELEASE
    assert story.serial == gen_cues.SERIAL


def test_invisible_solves_to_exactly_one_bit(story):
    """The thief is in a room long before he is in it visibly; without this bit
    the thief cue would fire on his wanderings. It has to come out unique."""
    if not gen_cues.ZIL.exists():
        pytest.skip("ZIL source not present")
    assert gen_cues.solve_flag(story, "INVISIBLE") == 7


def test_solved_bit_is_set_on_the_thief_and_not_on_the_others(story):
    if not gen_cues.ZIL.exists():
        pytest.skip("ZIL source not present")
    inv = gen_cues.solve_flag(story, "INVISIBLE")
    thief = gen_cues.obj_by_name(story, "thief")
    assert inv in gen_cues.attr_bits(story, thief)
    for name in ("troll", "cyclops", "sword"):
        obj = gen_cues.obj_by_name(story, name)
        assert inv not in gen_cues.attr_bits(story, obj)


def test_villains_start_where_the_zil_puts_them(story):
    """Each villain's starting parent is the room its rule names, which is what
    makes the object numbers checkable at all."""
    for villain, room in (("troll", "The Troll Room"),
                          ("cyclops", "Cyclops Room")):
        v = gen_cues.obj_by_name(story, villain)
        assert story.by_num[v].parent == gen_cues.obj_by_name(story, room)
    assert story.by_num[gen_cues.obj_by_name(story, "sword")].parent == \
        gen_cues.obj_by_name(story, "Living Room")


def test_an_unresolvable_name_raises_rather_than_writing_a_zero(story):
    with pytest.raises(SystemExit):
        gen_cues.obj_by_name(story, "no such object")


def test_an_unsolvable_flag_raises(story):
    if not gen_cues.ZIL.exists():
        pytest.skip("ZIL source not present")
    with pytest.raises(SystemExit):
        gen_cues.solve_flag(story, "NOSUCHBIT")


def test_build_emits_one_rule_per_declared_rule(story):
    if not gen_cues.ZIL.exists():
        pytest.skip("ZIL source not present")
    rules, fields = gen_cues.build()
    assert len(rules) == len(gen_cues.RULES)
    assert [r[2] for r in rules] == [r[2] for r in gen_cues.RULES]
    # A rule that names no room must emit 0, which the runtime reads as "any".
    for row, src in zip(rules, gen_cues.RULES):
        assert (row[1] == 0) == (src[1] is None)
        assert row[3] == (1 if src[3] else 0)
    assert fields["danger"] == gen_cues.DANGER
    assert fields["take"] == gen_cues.TAKE
    assert fields["death"] == gen_cues.DEATH
    assert fields["win"] == gen_cues.WIN


def test_emitted_table_matches_the_checked_in_inc(story):
    """The .inc is generated but tracked, so a drift between the two would ship
    a table nobody can reproduce."""
    if not gen_cues.ZIL.exists() or not gen_cues.OUT.exists():
        pytest.skip("inputs not present")
    rules, fields = gen_cues.build()
    on_disk = gen_cues.OUT.read_text(encoding="utf-8")
    for villain, room, track, unseen in rules:
        assert f"{{ {villain:3d}, {room:3d}, {track:2d}, {unseen} }}" in on_disk
    assert f'{{ {gen_cues.RELEASE}, "{gen_cues.SERIAL}"' in on_disk
    assert f'{fields["invisible"]}, {fields["sword"]},' in on_disk
