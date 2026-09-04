#!/usr/bin/env python3
"""The four games that are Zork I in another wrapper show the disc's own
pictures for the rooms they share with it, and are not drawn for twice."""
import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import zork1_reuse

ROOMS = ROOT / "tools" / "assets" / "rooms"
SHEET = ROOT / "tools" / "assets" / "art" / "room_prompts.json"


class TestNorm(unittest.TestCase):
    def test_drops_a_leading_article_so_troll_room_matches(self):
        self.assertEqual(zork1_reuse.norm("Troll Room"),
                         zork1_reuse.norm("The Troll Room"))

    def test_does_not_collapse_rooms_that_merely_share_a_word(self):
        self.assertNotEqual(zork1_reuse.norm("Windy Cave"),
                            zork1_reuse.norm("Cave"))

    def test_empty_title_normalises_to_nothing_and_never_matches(self):
        self.assertEqual(zork1_reuse.norm(""), "")
        self.assertEqual(zork1_reuse.norm(None), "")


class TestMatches(unittest.TestCase):
    def test_every_reused_room_names_a_real_object_of_its_game(self):
        for stem in zork1_reuse.DERIVED:
            data = json.loads((ROOMS / f"{stem}.json").read_text(encoding="utf-8"))
            objs = {int(r["obj"]) for r in data["rooms"]}
            for obj in zork1_reuse.matches(stem):
                self.assertIn(obj, objs, f"{stem} room {obj}")

    def test_a_game_that_is_not_a_derivative_reuses_nothing(self):
        # Mini-Zork II shares a title with Zork I by coincidence. Coincidence
        # is not the same room, and a wrong match is permanent.
        self.assertEqual(zork1_reuse.matches("MZORKII"), {})
        self.assertEqual(zork1_reuse.matches("ZORK2"), {})

    def test_the_derivatives_are_mostly_zork_and_the_samplers_mostly_not(self):
        got = {}
        for stem in zork1_reuse.DERIVED:
            data = json.loads((ROOMS / f"{stem}.json").read_text(encoding="utf-8"))
            got[stem] = len(zork1_reuse.matches(stem)) / len(data["rooms"])
        # A Mini-Zork is Zork I cut down, so nearly every room is one of its
        # own; a Sampler is four games in a trenchcoat and only its first act
        # is. If either drifts far from that the match rule has gone wrong.
        self.assertGreater(got["MZORKI"], 0.8)
        self.assertGreater(got["MZORKI2"], 0.8)
        self.assertLess(got["INFOSAM5"], 0.6)
        self.assertLess(got["INFOSAM7"], 0.8)


class TestSheet(unittest.TestCase):
    def test_the_prompt_sheet_does_not_name_a_room_that_reuses_a_picture(self):
        if not SHEET.is_file():
            self.skipTest("no room_prompts.json; run tools/gen_room_prompts.py")
        batch = json.loads(SHEET.read_text(encoding="utf-8"))["batch"]
        named = {(e["game"], int(e["obj"])) for e in batch if "game" in e}
        both = named & set(zork1_reuse.all_matches())
        self.assertEqual(both, set(),
                         "these rooms take Zork I's picture AND are drawn for: "
                         f"{sorted(both)[:10]}")


if __name__ == "__main__":
    unittest.main()
