#!/usr/bin/env python3
"""Tests for the per-room presentation store, its suggestions, and the review API."""
import json
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import art_frames
import game_genre as genre_vocab
import pres_store as store


class PoolTest(unittest.TestCase):
    def setUp(self):
        self.pool = store.pool()

    def test_pool_has_every_picture(self):
        """The 74 measured ones and every picture generated since -- the pool is
        the whole supply, and a picture missing from it is one no game can be
        given however well it is on the disc."""
        self.assertEqual(len(self.pool["images"]), 74 + len(art_frames.frames()))

    def test_only_neutral_pool_tracks_are_offerable(self):
        """The cues, the fanfares and the ending are the runtime's to issue --
        a room naming one records a decision the engine will not honour."""
        offered = [t["track"] for t in store.tracks(self.pool)]
        self.assertEqual(offered, sorted(store.NEUTRAL_POOL))
        self.assertIn(0, offered, "silence is a choice, not an absence")
        for reserved in (13, 14, 15, 16, 17, 19, 21, 25, 30, 32):
            self.assertNotIn(reserved, offered)

    def test_offered_tracks_match_the_runtime_pool(self):
        """P_NEUTRAL in music_data.c is the list the engine actually draws
        from; this app offering a different one would put tracks in the table
        that no build ever plays."""
        src = (ROOT / "saturn" / "src" / "sound" / "music_data.c").read_text(encoding="utf-8")
        m = re.search(r"P_NEUTRAL\[\] = \{([0-9,\s]+)\}", src)
        self.assertIsNotNone(m, "P_NEUTRAL not found in music_data.c")
        runtime = sorted(int(t) for t in m.group(1).split(","))
        self.assertEqual([t for t in sorted(store.NEUTRAL_POOL) if t], runtime)

    def test_image_indices_are_dense_and_one_based(self):
        """A gap would mean a room record pointing at a picture the pool cannot
        show, which the review app renders as a blank rather than an error."""
        idx = sorted(i["index"] for i in self.pool["images"])
        self.assertEqual(idx, list(range(1, len(idx) + 1)))

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


class GenreTest(unittest.TestCase):
    """The per-game genre vocabulary the track sweeps are grouped by."""

    def test_every_game_on_the_disc_is_filed(self):
        for stem in store.games() + [store.ZORK1_STEM]:
            self.assertIn(stem, genre_vocab.GAME_GENRE,
                          f"{stem} has no genre -- nobody has said what it sounds like")

    def test_no_genre_is_filed_for_a_game_that_is_not_there(self):
        known = set(store.games()) | {store.ZORK1_STEM}
        for stem in genre_vocab.GAME_GENRE:
            self.assertIn(stem, known, f"{stem} is filed but is not on the disc")

    def test_every_genre_used_is_in_the_vocabulary(self):
        named = dict(genre_vocab.GENRES)
        for stem, g in genre_vocab.GAME_GENRE.items():
            self.assertIn(g, named, f"{stem} is filed under an unlisted genre {g}")

    def test_every_genre_carries_a_note(self):
        for name, note in genre_vocab.GENRES:
            self.assertTrue(note.strip(), f"{name} says nothing about what it means")

    def test_grouping_loses_no_game(self):
        stems = store.games()
        grouped = [s for _g, held in genre_vocab.by_genre(stems) for s in held]
        self.assertEqual(sorted(grouped), sorted(stems))

    def test_a_game_outside_the_table_still_appears(self):
        """A list of every game that quietly drops one is not a list of every
        game, and an unfiled story is exactly the one worth seeing."""
        grouped = genre_vocab.by_genre(["ZORK2", "NOSUCHGAME"])
        self.assertEqual(grouped[-1], (None, ["NOSUCHGAME"]))


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

    def test_every_analogue_scene_names_a_track(self):
        """An analogue room opening silent is the blank it was meant to avoid,
        and a room holding no track can never be accepted."""
        for scene, d in self.defaults.items():
            if d["source"] == "analogue":
                self.assertTrue(d["track"], f"{scene} stands in silence")

    def test_no_suggestion_names_a_reserved_track(self):
        for scene in self.defaults:
            s = store.suggest(scene, self.defaults)
            self.assertIn(s["track"], store.NEUTRAL_POOL,
                          f"{scene} suggests track {s['track']}")

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
        """Set up the absence rather than assume it: every room on the disc
        now holds a record, so a room with none is a state this test has to
        make for itself."""
        d = store.load(self.GAME)
        d["rooms"].pop(str(self.obj), None)
        store.save(self.GAME, d)
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

    def test_accepted_needs_both_halves(self):
        self.assertTrue(store.accepted({"image": 3, "track": 11}))
        self.assertFalse(store.accepted({"image": 3, "track": 0}))
        self.assertFalse(store.accepted({"image": 0, "track": 11}))
        self.assertFalse(store.accepted(None))

    def test_basis_reports_an_untouched_room_as_its_suggestion(self):
        sug = {"image": 3, "track": 11, "confidence": "weak"}
        self.assertEqual(store.basis(None, sug), "weak")
        self.assertEqual(store.basis({"image": 3, "track": 11}, sug), "weak")

    def test_basis_reports_an_overruled_room_as_chosen(self):
        sug = {"image": 3, "track": 11, "confidence": "strong"}
        self.assertEqual(store.basis({"image": 4, "track": 11}, sug), "chosen")
        self.assertEqual(store.basis({"image": 3, "track": 12}, sug), "chosen")

    def test_bless_fills_every_room_it_has_a_picture_for(self):
        while store.undo(self.GAME) is not None:
            pass
        n = store.bless(self.GAME)
        self.assertGreater(n, 0)
        saved = store.load(self.GAME)["rooms"]
        self.assertEqual(len(saved), n)
        for rec in saved.values():
            self.assertTrue(rec["image"], "a blank record hides that a room needs a human")

    def test_bless_never_overwrites_a_stored_verdict(self):
        while store.undo(self.GAME) is not None:
            pass
        store.assign(self.GAME, self.obj, 7, 0)
        store.bless(self.GAME)
        self.assertEqual(store.load(self.GAME)["rooms"][str(self.obj)],
                         {"image": 7, "track": 0})

    def test_bless_is_idempotent(self):
        while store.undo(self.GAME) is not None:
            pass
        store.bless(self.GAME)
        self.assertEqual(store.bless(self.GAME), 0)

    def test_set_all_tracks_keeps_each_picture(self):
        store.bless(self.GAME)
        before = {k: v["image"] for k, v in store.load(self.GAME)["rooms"].items()}
        store.set_all_tracks(self.GAME, 12)
        after = store.load(self.GAME)["rooms"]
        for k, image in before.items():
            self.assertEqual(after[k]["image"], image, f"{k} lost its picture")
            self.assertEqual(after[k]["track"], 12)

    def test_set_all_tracks_is_idempotent(self):
        store.bless(self.GAME)
        store.set_all_tracks(self.GAME, 12)
        self.assertEqual(store.set_all_tracks(self.GAME, 12), 0)

    def test_set_all_tracks_writes_no_blank_record(self):
        """A room with neither a picture nor a track is a row nothing reads;
        sweeping silence over an untagged room must leave it alone."""
        store.set_all_tracks(self.GAME, 0)
        for k, rec in store.load(self.GAME)["rooms"].items():
            self.assertTrue(rec["image"] or rec["track"], f"{k} is a blank record")


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
        # Counted off the pool rather than named: 999 was outside it while the
        # supply was 74 pictures and inside it at 1,942, so the assertion went
        # on reading the same while it had stopped testing anything.
        beyond = len(store.pool()["images"]) + 1
        r = self.post("/api/assign",
                      {"game": self.GAME, "obj": self.obj, "image": beyond,
                       "track": 11})
        self.assertEqual(r.status_code, 400)

    def test_track_that_is_not_on_the_disc_is_refused(self):
        r = self.post("/api/assign",
                      {"game": self.GAME, "obj": self.obj, "image": 3, "track": 99})
        self.assertEqual(r.status_code, 400)

    def test_zork1_is_refused(self):
        r = self.post("/api/assign",
                      {"game": "ZORK1", "obj": 1, "image": 3, "track": 11})
        self.assertEqual(r.status_code, 400)

    def test_reserved_track_is_refused(self):
        """Track 19 is the death cue. A room may not claim it."""
        r = self.post("/api/assign",
                      {"game": self.GAME, "obj": self.obj, "image": 3, "track": 19})
        self.assertEqual(r.status_code, 400)

    def test_assign_returns_the_rows_new_pills(self):
        """The game page repaints one row rather than reloading, so the state
        and basis it shows have to come from the same code the page did."""
        r = self.post("/api/assign",
                      {"game": self.GAME, "obj": self.obj, "image": 3, "track": 11})
        self.assertEqual(r.status_code, 200)
        room = r.get_json()["room"]
        self.assertIn("accepted", room["state"])
        self.assertIn("c-", room["basis"])

    def test_track_all_sets_every_room(self):
        r = self.post("/api/track_all", {"game": self.GAME, "track": 12})
        self.assertEqual(r.status_code, 200)
        tracks = {rec["track"] for rec in store.load(self.GAME)["rooms"].values()}
        self.assertEqual(tracks, {12})

    def test_area_track_sets_only_that_area(self):
        import room_groups
        areas = room_groups.groups(self.GAME)
        a, b = areas[0], areas[-1]
        self.assertNotEqual(a["id"], b["id"])
        r = self.post("/api/area_track",
                      {"game": self.GAME, "area": a["id"], "track": 8})
        self.assertEqual(r.status_code, 200)
        saved = store.load(self.GAME)["rooms"]
        for obj in a["rooms"]:
            self.assertEqual(saved[str(obj)]["track"], 8)
        outside = [saved[str(o)]["track"] for o in b["rooms"] if str(o) in saved]
        self.assertTrue(all(t != 8 for t in outside) or not outside,
                        "a neighbouring area was swept too")

    def test_area_track_refuses_an_area_that_is_not_there(self):
        r = self.post("/api/area_track",
                      {"game": self.GAME, "area": 9999, "track": 8})
        self.assertEqual(r.status_code, 400)

    def test_area_track_refuses_a_reserved_track(self):
        import room_groups
        a = room_groups.groups(self.GAME)[0]
        r = self.post("/api/area_track",
                      {"game": self.GAME, "area": a["id"], "track": 19})
        self.assertEqual(r.status_code, 400)

    def test_track_all_refuses_a_reserved_track(self):
        r = self.post("/api/track_all", {"game": self.GAME, "track": 19})
        self.assertEqual(r.status_code, 400)

    def test_genre_track_sweeps_every_game_in_the_genre(self):
        """WITNESS is a mystery; a MYSTERY sweep must reach it and its five
        siblings, so this backs up all of them rather than only its own."""
        stems = [s for s in store.games()
                 if genre_vocab.genre_of(s) == "MYSTERY" and s != self.GAME]
        kept = {s: (store.path(s).read_bytes() if store.path(s).is_file() else None)
                for s in stems}
        try:
            r = self.post("/api/genre_track", {"genre": "MYSTERY", "track": 12})
            self.assertEqual(r.status_code, 200)
            self.assertGreater(r.get_json()["games"], 1)
            for s in stems + [self.GAME]:
                tracks = {rec["track"] for rec in store.load(s)["rooms"].values()}
                self.assertEqual(tracks, {12}, f"{s} was not swept")
        finally:
            for s, blob in kept.items():
                if blob is None:
                    if store.path(s).is_file():
                        store.path(s).unlink()
                else:
                    store.path(s).write_bytes(blob)

    def test_genre_track_refuses_an_unknown_genre(self):
        r = self.post("/api/genre_track", {"genre": "SPOOOKY", "track": 12})
        self.assertEqual(r.status_code, 400)

    def test_genre_track_refuses_a_reserved_track(self):
        r = self.post("/api/genre_track", {"genre": "MYSTERY", "track": 19})
        self.assertEqual(r.status_code, 400)

    def test_bless_leaves_nothing_untouched_that_has_a_basis(self):
        self.assertEqual(self.client.get(f"/g/{self.GAME}").status_code, 200)
        r = self.post("/api/bless", {"game": self.GAME})
        self.assertEqual(r.status_code, 200)
        j = r.get_json()
        self.assertEqual(j["done"] + j["part"] + j["none"], j["total"],
                         "every room with a basis should now hold a pairing")


if __name__ == "__main__":
    unittest.main(verbosity=2)
