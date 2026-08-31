#!/usr/bin/env python3
"""Tests for the per-room presentation store, its suggestions, and the review API."""
import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import pres_store as store


class PoolTest(unittest.TestCase):
    def setUp(self):
        self.pool = store.pool()

    def test_pool_has_every_picture_and_track(self):
        self.assertEqual(len(self.pool["images"]), 74)
        self.assertTrue(any(t["track"] == 0 for t in self.pool["tracks"]),
                        "silence must be offerable as a choice, not an absence")

    def test_image_indices_are_dense_and_one_based(self):
        idx = sorted(i["index"] for i in self.pool["images"])
        self.assertEqual(idx, list(range(1, 75)))

    def test_every_scene_has_a_default(self):
        import scene_vocab as vocab
        for scene in vocab.SCENES:
            self.assertIn(scene, self.pool["scene_defaults"],
                          f"{scene} has neither measured evidence nor an analogue")

    def test_defaults_name_real_pictures(self):
        valid = {i["index"] for i in self.pool["images"]}
        for scene, d in self.pool["scene_defaults"].items():
            self.assertIn(d["image"], valid, f"{scene} names picture {d['image']}")

    def test_measured_evidence_totals_one_row_per_room(self):
        """A title-based rejoin would cross-join repeated titles; this pins that
        the counts are one per tagged room, not the product of a bad join."""
        n = sum(d["n"] for d in self.pool["scene_defaults"].values()
                if d["source"] == "measured")
        self.assertLessEqual(n, 110)
        self.assertGreater(n, 50)


class SuggestTest(unittest.TestCase):
    def setUp(self):
        self.defaults = store.pool()["scene_defaults"]

    def test_no_scene_yields_no_suggestion(self):
        s = store.suggest(None, self.defaults)
        self.assertEqual(s["confidence"], "none")
        self.assertEqual(s["image"], 0)

    def test_unanimous_scene_is_strong(self):
        s = store.suggest("FOREST", self.defaults, "stored")
        self.assertEqual(s["confidence"], "strong")

    def test_scattered_scene_is_weak(self):
        """CAVE's thirteen Zork I rooms took ten different pictures, so its
        suggestion must not present itself as well founded."""
        s = store.suggest("CAVE", self.defaults, "stored")
        self.assertEqual(s["confidence"], "weak")

    def test_analogue_is_never_strong(self):
        for scene, d in self.defaults.items():
            if d["source"] == "analogue":
                self.assertEqual(store.suggest(scene, self.defaults)["confidence"],
                                 "analogue")

    def test_title_derived_scene_is_never_strong(self):
        """Two inferences are stacked -- title names scene, scene implies
        picture -- and only the second has evidence behind it."""
        s = store.suggest("FOREST", self.defaults, "title")
        self.assertEqual(s["confidence"], "weak")

    def test_scene_of_prefers_a_stored_tag(self):
        scene, origin = store.scene_of(5, "Forest", {"5": "TEMPLE"})
        self.assertEqual((scene, origin), ("TEMPLE", "stored"))

    def test_scene_of_falls_back_to_the_title(self):
        scene, origin = store.scene_of(5, "Dark Forest", {})
        self.assertEqual(origin, "title")
        self.assertIsNotNone(scene)


class StoreTest(unittest.TestCase):
    """Round-trips against a real game file, restoring whatever was there."""

    GAME = "WITNESS"

    def setUp(self):
        self.path = store.path(self.GAME)
        self.backup = self.path.read_bytes() if self.path.is_file() else None
        self.obj = store.rooms(self.GAME)[0]["obj"]

    def tearDown(self):
        if self.backup is None:
            if self.path.is_file():
                self.path.unlink()
        else:
            self.path.write_bytes(self.backup)

    def test_assign_then_undo_restores_absence(self):
        store.assign(self.GAME, self.obj, 12, 4)
        self.assertEqual(store.load(self.GAME)["rooms"][str(self.obj)],
                         {"image": 12, "track": 4})
        store.undo(self.GAME)
        self.assertNotIn(str(self.obj), store.load(self.GAME)["rooms"])

    def test_undo_restores_the_previous_value_not_just_absence(self):
        store.assign(self.GAME, self.obj, 12, 4)
        store.assign(self.GAME, self.obj, 30, 8)
        store.undo(self.GAME)
        self.assertEqual(store.load(self.GAME)["rooms"][str(self.obj)],
                         {"image": 12, "track": 4})

    def test_undo_on_an_empty_stack_is_harmless(self):
        while store.undo(self.GAME) is not None:
            pass
        self.assertIsNone(store.undo(self.GAME))

    def test_zork1_is_not_assignable(self):
        self.assertNotIn("ZORK1", store.games())


class ApiTest(unittest.TestCase):
    """The review API's refusals. Each of these would otherwise reach the
    generated table as a row nothing reads and nothing reports."""

    GAME = "WITNESS"

    @classmethod
    def setUpClass(cls):
        try:
            import pres_server
        except ImportError as exc:
            raise unittest.SkipTest(f"flask not installed: {exc}")
        pres_server.app.config["TESTING"] = True
        cls.client = pres_server.app.test_client()

    def setUp(self):
        self.path = store.path(self.GAME)
        self.backup = self.path.read_bytes() if self.path.is_file() else None
        self.obj = store.rooms(self.GAME)[0]["obj"]

    def tearDown(self):
        if self.backup is None:
            if self.path.is_file():
                self.path.unlink()
        else:
            self.path.write_bytes(self.backup)

    def post(self, url, body):
        return self.client.post(url, json=body)

    def test_valid_assignment_is_accepted(self):
        r = self.post("/api/assign",
                      {"game": self.GAME, "obj": self.obj, "image": 3, "track": 11})
        self.assertEqual(r.status_code, 200)
        self.assertGreaterEqual(r.get_json()["done"], 1)

    def test_object_that_is_not_a_room_is_refused(self):
        objs = {r["obj"] for r in store.rooms(self.GAME)}
        phantom = next(i for i in range(256) if i not in objs)
        r = self.post("/api/assign",
                      {"game": self.GAME, "obj": phantom, "image": 3, "track": 11})
        self.assertEqual(r.status_code, 400)

    def test_picture_outside_the_pool_is_refused(self):
        r = self.post("/api/assign",
                      {"game": self.GAME, "obj": self.obj, "image": 999, "track": 11})
        self.assertEqual(r.status_code, 400)

    def test_track_that_is_not_on_the_disc_is_refused(self):
        r = self.post("/api/assign",
                      {"game": self.GAME, "obj": self.obj, "image": 3, "track": 99})
        self.assertEqual(r.status_code, 400)

    def test_zork1_is_refused(self):
        r = self.post("/api/assign",
                      {"game": "ZORK1", "obj": 1, "image": 3, "track": 11})
        self.assertEqual(r.status_code, 400)

    def test_accept_strong_takes_only_strong_suggestions(self):
        before = self.client.get(f"/g/{self.GAME}").status_code
        self.assertEqual(before, 200)
        r = self.post("/api/accept_strong", {"game": self.GAME})
        self.assertEqual(r.status_code, 200)
        j = r.get_json()
        self.assertEqual(j["strong"], 0, "every strong suggestion should be taken")
        self.assertEqual(j["accepted"], j["done"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
