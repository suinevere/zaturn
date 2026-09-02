#!/usr/bin/env python3
"""The exit-graph decode, the area grouping, and the per-genre track pools."""
import collections
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import game_genre as genre_vocab
import pres_store as store
import room_groups
import zexits


class ExitsTest(unittest.TestCase):
    def test_every_story_on_the_disc_decodes(self):
        for stem in store.games() + [store.ZORK1_STEM]:
            raw = zexits.story(stem)
            self.assertIsNotNone(raw, f"{stem}.Z3 is missing")
            self.assertTrue(zexits.graph(raw), f"{stem} decoded no rooms at all")

    def test_room_count_agrees_with_the_inventory(self):
        """The inventory is generated from the same story by a different
        script; the two disagreeing by more than a couple of pseudo-rooms
        means one of them is reading the object table wrong."""
        for stem in store.games():
            n = len(zexits.graph(zexits.story(stem)))
            inv = len(store.rooms(stem))
            if stem == "SPLBRKR":
                continue
            self.assertLess(abs(n - inv), 20,
                            f"{stem}: {n} rooms decoded against {inv} in the inventory")

    def test_a_destination_always_names_a_room(self):
        for stem in [store.ZORK1_STEM, "ZORK2", "PLNTFALL"]:
            g = zexits.graph(zexits.story(stem))
            for o, e in g.items():
                for dest in e.values():
                    self.assertIn(dest, g, f"{stem}: {o} exits to non-room {dest}")

    def test_the_graph_is_made_symmetric(self):
        g = zexits.graph(zexits.story("ZORK2"))
        adj = zexits.neighbours(g)
        for a, ks in adj.items():
            for k in ks:
                self.assertIn(a, adj[k], f"{a}-{k} is one-sided after neighbours()")


class GroupTest(unittest.TestCase):
    def test_every_room_lands_in_exactly_one_area(self):
        for stem in store.games():
            rooms = [r["obj"] for r in store.rooms(stem)]
            held = [o for a in room_groups.groups(stem) for o in a["rooms"]]
            self.assertEqual(sorted(held), sorted(rooms), f"{stem} lost or duplicated rooms")

    def test_no_area_is_empty(self):
        for stem in store.games():
            for a in room_groups.groups(stem):
                self.assertTrue(a["rooms"], f"{stem} produced an empty area")
                self.assertTrue(a["label"], f"{stem} produced an unnamed area")

    def test_an_area_never_suggests_a_reserved_track(self):
        for stem in store.games():
            for a in room_groups.groups(stem):
                self.assertIn(a["track"], store.NEUTRAL_POOL,
                              f"{stem} area {a['id']} suggests track {a['track']}")

    def test_grouping_recovers_zork1s_real_areas(self):
        """The only place the grouping can be checked at all. Zork I's true
        areas are known -- each room was drawn from one picture archive -- so
        this measures how many rooms land in an area whose majority archive is
        their own. It was 91% when the rule was settled on; a change that
        drops it well below that has made the grouping worse, whatever it did
        to the other thirty games where nothing can tell."""
        import gen_presentation as gp
        join = gp.build_join()
        truth = {o: s["area_archive"] for o, s in join.items()}
        pure = tot = 0
        for a in room_groups.groups(store.ZORK1_STEM):
            have = [truth[o] for o in a["rooms"] if o in truth]
            if not have:
                continue
            tot += len(have)
            pure += collections.Counter(have).most_common(1)[0][1]
        self.assertGreater(tot, 100, "Zork I's rooms did not join the ground truth")
        self.assertGreaterEqual((100 * pure) // tot, 85,
                                f"area purity fell to {(100 * pure) // tot}%")

    def test_zork1_comes_out_as_a_workable_number_of_areas(self):
        """A hundred areas is the per-room page again under another name, and
        one area is no grouping at all."""
        n = len(room_groups.groups(store.ZORK1_STEM))
        self.assertGreater(n, 10)
        self.assertLess(n, 60)


class GenreTrackTest(unittest.TestCase):
    def test_every_genre_has_a_track_pool(self):
        for name, _note in genre_vocab.GENRES:
            self.assertIn(name, genre_vocab.GENRE_TRACKS, f"{name} offers no tracks")
            self.assertTrue(genre_vocab.GENRE_TRACKS[name], f"{name}'s pool is empty")

    def test_no_genre_offers_a_reserved_track(self):
        for name, tracks in genre_vocab.GENRE_TRACKS.items():
            for t in tracks:
                self.assertIn(t, store.NEUTRAL_POOL,
                              f"{name} offers reserved track {t}")

    def test_silence_is_always_offerable(self):
        for stem in store.games():
            self.assertIn(0, genre_vocab.tracks_for(stem))

    def test_a_shortlist_narrows(self):
        """A pool that offers everything to everyone is not a shortlist."""
        self.assertLess(len(genre_vocab.tracks_for("LURKING")),
                        len(store.NEUTRAL_POOL))

    def test_a_stored_track_stays_representable(self):
        """A menu that cannot show the value it holds does not show it -- the
        browser falls back to the first option and the next unrelated edit
        writes that back."""
        self.assertIn(12, genre_vocab.tracks_for("LURKING", {12}))

    def test_an_unfiled_game_still_gets_a_pool(self):
        self.assertIn(0, genre_vocab.tracks_for("NOSUCHGAME"))
        self.assertGreater(len(genre_vocab.tracks_for("NOSUCHGAME")), 1)



class GuessTest(unittest.TestCase):
    """The layers that close the gap, and the flag that keeps them honest."""

    def setUp(self):
        import room_guess
        self.guess = room_guess
        self.pool = store.pool()

    def test_every_room_on_the_disc_gets_a_picture(self):
        for stem in store.games():
            for obj, s in self.guess.suggestions(stem, self.pool).items():
                self.assertTrue(s["image"], f"{stem} object {obj} still has none")

    def test_every_area_gets_a_track(self):
        for stem in store.games():
            for i, (t, _how) in self.guess.area_tracks(stem, self.pool).items():
                self.assertTrue(t, f"{stem} area {i} is still silent")
                self.assertIn(t, store.NEUTRAL_POOL)

    def test_a_guess_is_never_reported_as_evidence(self):
        """The whole bargain: the gap is closed with something visible and
        wrong, not with something that looks measured."""
        for stem in store.games():
            tags = store.scenes(stem)
            for r in store.rooms(stem):
                s = self.guess.suggestions(stem, self.pool)[r["obj"]]
                scene, _how = store.scene_of(r["obj"], r["title"] or "", tags)
                if scene is None:
                    self.assertEqual(s["confidence"], "guess",
                                     f"{stem} {r['obj']} claims {s['confidence']}")
            break

    def test_a_tagged_room_keeps_its_own_confidence(self):
        """Adding guessing layers must not restate evidence as a guess."""
        for stem in ("WITNESS", "ZORK2"):
            tags = store.scenes(stem)
            sug = self.guess.suggestions(stem, self.pool)
            for r in store.rooms(stem):
                scene, origin = store.scene_of(r["obj"], r["title"] or "", tags)
                if scene is None:
                    continue
                want = store.suggest(scene, self.pool["scene_defaults"], origin)
                self.assertEqual(sug[r["obj"]]["confidence"], want["confidence"])

    def test_a_description_naming_nothing_yields_nothing(self):
        self.assertIsNone(self.guess.text_scene(None))
        self.assertIsNone(self.guess.text_scene(""))
        self.assertIsNone(self.guess.text_scene("It is pitch black."))

    def test_a_description_naming_one_place_names_it(self):
        self.assertEqual(
            self.guess.text_scene("A dense forest. Tall forest trees crowd the "
                                  "forest floor on every side."), "FOREST")

    def test_the_description_layer_still_recovers_most_stored_tags(self):
        """Held out: hide the stored tag and ask the prose to recover it. It
        was 83% when the layer order was settled on."""
        ok = tot = 0
        for stem in store.games():
            tags = store.scenes(stem)
            if not tags:
                continue
            for r in store.rooms(stem):
                truth = tags.get(str(r["obj"]))
                if not truth:
                    continue
                got = self.guess.text_scene(r["description"])
                if got:
                    tot += 1
                    ok += (got == truth)
        self.assertGreater(tot, 200, "the layer answered for almost nothing")
        self.assertGreaterEqual((100 * ok) // tot, 75,
                                f"accuracy fell to {(100 * ok) // tot}%")

    def test_every_genre_has_a_fallback(self):
        for name, _note in genre_vocab.GENRES:
            scene, track = genre_vocab.GENRE_FALLBACK[name]
            self.assertIn(scene, self.pool["scene_defaults"], f"{name} names scene {scene}")
            self.assertIn(track, store.NEUTRAL_POOL, f"{name} names track {track}")



class ImageLooksTest(unittest.TestCase):
    """What the pictures actually show, and the spread that came out of it."""

    def setUp(self):
        import image_looks
        import room_guess
        self.looks = image_looks
        self.guess = room_guess
        self.pool = store.pool()

    def test_every_picture_has_been_looked_at(self):
        for i in self.pool["images"]:
            self.assertTrue(self.looks.looks(i["index"]).strip(),
                            f"nobody has said what picture {i['index']} shows")

    def test_every_scene_names_pictures_that_exist(self):
        valid = {i["index"] for i in self.pool["images"]}
        for scene, imgs in self.looks.SCENE_IMAGES.items():
            self.assertTrue(imgs, f"{scene} names no picture")
            for i in imgs:
                self.assertIn(i, valid, f"{scene} names picture {i}")

    def test_every_scene_in_the_vocabulary_has_pictures(self):
        import scene_vocab as vocab
        for scene in vocab.SCENES:
            self.assertIn(scene, self.looks.SCENE_IMAGES, f"{scene} has no pictures")

    def test_no_picture_is_left_out_of_every_list(self):
        """45 of the 74 had never been used once, which is what one picture per
        scene buys you."""
        listed = {i for v in self.looks.SCENE_IMAGES.values() for i in v}
        missing = sorted({i["index"] for i in self.pool["images"]} - listed)
        self.assertEqual(missing, [], f"pictures nothing will ever pick: {missing}")

    def test_no_one_picture_carries_the_disc(self):
        """Picture 37 was on 22% of every room on the disc, because CORRIDOR
        was pointed at it by its name and it is a rock face."""
        used = collections.Counter()
        for stem in store.games():
            for _o, s in self.guess.suggestions(stem, self.pool).items():
                used[s["image"]] += 1
        total = sum(used.values())
        worst, n = used.most_common(1)[0]
        self.assertLess((100 * n) // total, 15,
                        f"picture {worst} is on {(100 * n) // total}% of the disc")

    def test_the_disc_uses_most_of_its_pictures(self):
        used = set()
        for stem in store.games():
            for _o, s in self.guess.suggestions(stem, self.pool).items():
                used.add(s["image"])
        self.assertGreaterEqual(len(used), 55, f"only {len(used)} of 74 pictures used")

    def test_a_strongly_measured_scene_is_never_spread(self):
        """Four of four Zork I FOREST rooms took picture 3; that is a count,
        not a taste, and no judgement in image_looks may overrule it."""
        d = self.pool["scene_defaults"]
        for rank in range(6):
            self.assertEqual(self.guess.picture("FOREST", d, rank),
                             d["FOREST"]["image"])

    def test_a_weak_scene_is_spread(self):
        d = self.pool["scene_defaults"]
        got = {self.guess.picture("CORRIDOR", d, r) for r in range(6)}
        self.assertGreater(len(got), 1, "CORRIDOR still takes one picture everywhere")

    def test_a_scene_measured_on_one_room_is_not_trusted_outright(self):
        """KITCHEN, PARLOR and ROAD are one Zork I room each, so each agreed
        with itself 100% and outranked everything -- which put a dim Victorian
        kitchen into the Bridge of the Heart of Gold."""
        d = self.pool["scene_defaults"]
        for scene in ("KITCHEN", "PARLOR", "ROAD"):
            self.assertLess(d[scene]["n"], self.guess.MIN_ROOMS, f"{scene} grew")
            got = {self.guess.picture(scene, d, r) for r in range(4)}
            self.assertGreater(len(got), 1, f"{scene} is still trusted on one room")

    def test_the_genre_narrows_a_guess(self):
        d = self.pool["scene_defaults"]
        scifi = set(genre_vocab.images_for("HITCHHKR"))
        self.assertTrue(scifi)
        for r in range(4):
            self.assertIn(self.guess.picture("KITCHEN", d, r, scifi), scifi)

    def test_the_genre_never_overrules_a_scene_it_disagrees_with(self):
        """A forest in a science fiction story is still a forest."""
        d = self.pool["scene_defaults"]
        scifi = set(genre_vocab.images_for("HITCHHKR"))
        got = self.guess.picture("FOREST", d, 0, scifi)
        self.assertEqual(got, d["FOREST"]["image"])

    def test_every_genre_picture_pool_names_real_pictures(self):
        valid = {i["index"] for i in self.pool["images"]}
        for name, imgs in genre_vocab.GENRE_IMAGES.items():
            for i in imgs:
                self.assertIn(i, valid, f"{name} names picture {i}")

    def test_a_tagged_room_keeps_the_picture_its_scene_argues_for(self):
        """The genre corrects a guess and nothing else."""
        sug = self.guess.suggestions("HITCHHKR", self.pool)
        tags = store.scenes("HITCHHKR")
        scifi = set(genre_vocab.images_for("HITCHHKR"))
        off = [o for o, s in sug.items()
               if s["confidence"] != "guess" and s["image"] not in scifi]
        self.assertTrue(off, "evidence should sometimes disagree with the genre")


if __name__ == "__main__":
    unittest.main(verbosity=2)
