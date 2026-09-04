#!/usr/bin/env python3
"""What is taken out of a room's prose and what is put in front of a room's
title, each held to the failure that caused it."""
import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import gen_room_prompts as G

SOURCE = ROOT / "tools" / "gen_room_prompts.py"


def sheet_batch():
    """The generated sheet, or a skip when no art has been prompted for."""
    path = ROOT / "tools" / "assets" / "art" / "room_prompts.json"
    if not path.is_file():
        raise unittest.SkipTest("no room_prompts.json")
    return json.loads(path.read_text(encoding="utf-8"))["batch"]


class TestQuoted(unittest.TestCase):
    def test_a_possessive_is_not_an_opening_quote(self):
        # These two paired their apostrophes with each other and deleted the
        # span between, inventing "Marinerss" and "Yous".
        for prose in ["You are in Mariners' Trust, the Island's bank.",
                      "You're standing in Aunt Hildegarde's much envied garden."]:
            self.assertEqual(G.QUOTED.sub("", prose), prose)

    def test_a_real_double_quote_is_still_stripped(self):
        got = G.QUOTED.sub("", 'A plaque reads "TREASURE VAULT" in gold.')
        self.assertNotIn("TREASURE", got)

    def test_a_real_single_quote_is_still_stripped(self):
        got = G.QUOTED.sub("", "A voice says 'step right up' from nowhere.")
        self.assertNotIn("step right up", got)

    def test_no_description_in_the_corpus_loses_a_possessive(self):
        bad = []
        for path in sorted((ROOT / "tools" / "assets" / "rooms").glob("*.json")):
            for r in json.loads(path.read_text(encoding="utf-8"))["rooms"]:
                prose = r.get("description")
                if not prose or '"' in prose:
                    continue
                if G.QUOTED.sub("", prose) != prose and "'" in prose:
                    # Only a quote with space or punctuation in front of it may
                    # legitimately strip anything here.
                    if " '" not in prose and prose[:1] != "'":
                        bad.append((r.get("title"), prose[:60]))
        self.assertEqual(bad, [], f"{len(bad)} descriptions lose a possessive")


class TestClean(unittest.TestCase):
    def test_a_sentence_that_opened_you_have_reached_loses_the_verb(self):
        # The word boundary in this pattern was a literal backspace, so the
        # strip never fired and rooms were drawn from "reached a dead end".
        self.assertEqual(G.clean("You have reached a dead end."), "a dead end")
        self.assertEqual(G.clean("You have entered a dusty cavern."),
                         "a dusty cavern")

    def test_prose_naming_a_role_or_a_pronoun_is_dropped(self):
        for prose in [
            "A tall and lanky clown in baggy pants trudges out of the tent.",
            "This is the garden where she would spend hours tending flowers.",
            "Comrade Thumb follows behind.",
        ]:
            self.assertEqual(G.clean(prose), "", prose)

    def test_a_compass_point_costs_the_word_and_not_the_sentence(self):
        # These three were thrown away whole for naming a direction: a tent,
        # a volcano and a limestone passage.
        got = G.clean("To the west stands a droopy tent, and the field "
                      "continues north and east.")
        self.assertIn("droopy tent", got)
        self.assertNotIn("west", got)
        self.assertNotIn("north", got)
        got = G.clean("Far below you is an active volcano, from which great "
                      "gouts of molten lava come surging out.")
        self.assertIn("volcano", got)
        got = G.clean("You are walking along a gently sloping north/south "
                      "passage lined with oddly shaped limestone formations.")
        self.assertIn("limestone", got)
        self.assertNotIn("/", got)

    def test_a_sentence_that_is_only_the_way_out_still_goes(self):
        # The verb is what makes it about the exit rather than the room.
        self.assertEqual(G.clean("A passage leads north."), "")
        self.assertEqual(G.clean("You can go west from here."), "")

    def test_removing_a_direction_leaves_no_dangling_grammar(self):
        for prose in ["To the west stands a droopy tent.",
                      "You are at the northeast end of an immense room.",
                      "To the north is a vertical cage."]:
            got = G.clean(prose)
            self.assertFalse(got.startswith("to the"), got)
            self.assertNotIn("  ", got)
            self.assertFalse(got.rstrip().endswith(" and"), got)

    def test_a_sculpted_figure_goes_the_way_a_statue_does(self):
        # Stationfall's Studio says a large scrap sculpture floats in the
        # middle of it, and got a giant human head for saying so. A carved
        # figure is a person in the picture, exactly as a statue is.
        got = G.clean("Most of the space is taken up by a large scrap "
                      "sculpture floating in the center.")
        self.assertEqual(got, "")
        # And what the room is stays.
        kept = G.clean("This living bubble has been set up as an artist's loft.")
        self.assertIn("artist", kept)

    def test_a_sentence_about_the_way_out_goes_whole(self):
        # It leaves "the way out is" behind otherwise, once the direction has
        # been taken out of it.
        self.assertEqual(G.clean("The way out is to the east."), "")
        self.assertEqual(G.clean("The only way back is up the ladder."), "")

    def test_prose_about_a_place_survives(self):
        got = G.clean("This is a large low room with a granite slab.")
        self.assertIn("granite", got)


class TestWeight(unittest.TestCase):
    """The first sentence of a Z-machine description is what the room IS."""

    def test_the_first_sentence_is_weighted_and_leads(self):
        import game_genre
        for e in sheet_batch():
            if "(" not in e["prompt"]:
                continue
            self.assertIn(f":{G.FIRST_WEIGHT})", e["prompt"], e["name"])
            # Nothing comes before it but the camera and the world: at most
            # the confined point of view, which is three clauses, and an era,
            # which is two. Five is the ceiling and it is reached.
            before = [b for b in e["prompt"].split("(", 1)[0].split(", ") if b]
            self.assertLessEqual(len(before), 5, e["name"])
            self.assertTrue(before[0].startswith("first person view"),
                            e["name"])

    def test_a_title_the_description_already_says_is_not_repeated(self):
        # Mariners' Trust opens "in Mariners' Trust, the Island's bank".
        by_name = {e["name"]: e for e in sheet_batch()}
        if "cuthroat_101" in by_name:
            p = by_name["cuthroat_101"]["prompt"]
            self.assertEqual(p.count("mariners' trust"), 1)

    def test_the_prose_is_cut_on_a_word(self):
        # The old cut landed mid-syllable and left "the narro".
        long = ". ".join(["This is a chamber of dressed stone with a vaulted "
                          "roof and a floor of worn flagstones"] * 6) + "."
        for part in G.clean_parts(long):
            self.assertFalse(part.endswith("-"), part)
            self.assertTrue(part == part.strip(), part)
        joined = " ".join(G.clean_parts(long))
        self.assertNotIn("  ", joined)


class TestTitle(unittest.TestCase):
    def test_map_jargon_is_said_as_what_it_means(self):
        self.assertEqual(G.title_words("Dead End"),
                         "narrow passage ending at a blank wall")
        self.assertIn("divides", G.title_words("Fork, Of Sorts"))

    def test_an_ordinary_title_is_left_alone(self):
        self.assertEqual(G.title_words("Vogon Hold"), "vogon hold")
        self.assertEqual(G.title_words("Back Yard"), "back yard")

    def test_jargon_is_anchored_so_a_real_fork_is_still_a_fork(self):
        self.assertEqual(G.title_words("Bent Fork"), "bent fork")


class TestSheet(unittest.TestCase):
    """The sheet on disk, which is the record of what each plate was drawn
    from and so the only thing a redraw list can be diffed against."""

    @classmethod
    def setUpClass(cls):
        path = ROOT / "tools" / "assets" / "art" / "room_prompts.json"
        if not path.is_file():
            raise unittest.SkipTest("no room_prompts.json")
        cls.batch = sheet_batch()

    def test_every_prompt_is_from_where_the_player_stands(self):
        # A room background is what the player is looking at, not a picture OF
        # the room taken from outside it. It also does the job the old wide
        # shot did: there is no way to be standing inside a fork.
        for e in self.batch:
            self.assertTrue(e["prompt"].startswith("first person view"),
                            e["name"])

    def test_no_prompt_says_no_people(self):
        # Diffusion has no negation; the phrase puts the token in the
        # conditioning being sampled towards.
        for e in self.batch:
            self.assertNotIn("no people", e["prompt"])
            self.assertNotIn("without people", e["prompt"])

    def test_no_prompt_carries_an_invented_word_from_a_deleted_possessive(self):
        for e in self.batch:
            for word in ("marinerss", "yous "):
                self.assertNotIn(word, e["prompt"], e["name"])


class TestSourceHygiene(unittest.TestCase):
    def test_the_source_holds_no_mangled_escape(self):
        # A backslash-b written through a bash heredoc becomes a literal
        # backspace, which compiles, never matches, and reads correctly.
        text = SOURCE.read_text(encoding="utf-8")
        for ch in ("\x07", "\x08", "\x0b", "\x0c"):
            self.assertNotIn(ch, text,
                             f"control character {ch!r} in {SOURCE.name}")


class TestEra(unittest.TestCase):
    """The century a game is lit in. A bare genre word was already the last
    token of every Planetfall prompt while the model drew a 1990s desktop."""

    def test_the_world_is_named_before_the_room_and_not_after_it(self):
        # It used to trail 240 characters of prose, which for Stationfall's
        # Rec Shop and Mayor's Office was two words of "sci-fi" against a page
        # of civic vocabulary. Context goes in front of its subject.
        import game_genre
        hand = G.overrides()
        for e in sheet_batch():
            # Unless the room named a light of its own, which replaces it.
            if "setting" in hand.get(e["name"], {}):
                continue
            era = game_genre.setting_for(e["game"])
            if not era:
                continue
            head = e["prompt"].split(", ")
            first = era.split(", ")[0]
            self.assertIn(first, head, e["name"])
            self.assertLess(head.index(first), 4, e["name"])

    def test_a_room_is_not_asked_for_its_own_kind_twice(self):
        # A room titled Kitchen and tagged KITCHEN said "kitchen, kitchen".
        for e in sheet_batch():
            clauses = [c for c in e["prompt"].split(", ") if c]
            self.assertEqual(len(clauses), len(set(clauses)), e["name"])

    def test_a_game_may_name_its_own_place_over_its_genre(self):
        import game_genre
        self.assertEqual(game_genre.setting_for("BALLYHOO"), "circus, 1930s")
        # And a game with no entry of its own still gets its genre's.
        self.assertEqual(game_genre.setting_for("DEADLINE"),
                         game_genre.GENRE_SETTING["MYSTERY"])

    def test_every_genre_but_sampler_names_a_century(self):
        import game_genre
        used = set(game_genre.GAME_GENRE.values())
        for genre in used - {"SAMPLER"}:
            self.assertIn(genre, game_genre.GENRE_SETTING, genre)
        # An anthology is several worlds and has no one century.
        self.assertNotIn("SAMPLER", game_genre.GENRE_SETTING)

    def test_the_era_says_nothing_about_what_kind_of_place_a_room_is(self):
        # Hitchhiker's is SCIFI and has an untagged pub and country lane in it.
        # An era composes with those; "worn painted metal" would not.
        import game_genre
        for phrase in game_genre.GENRE_SETTING.values():
            for material in ("metal", "bulkhead", "starship", "corridor",
                             "room", "interior"):
                self.assertNotIn(material, phrase.lower(), phrase)

    def test_every_prompt_carries_an_era_or_its_genre(self):
        import game_genre
        # Unless a hand-written override gave the room a light of its own: the
        # Scimitar is a dry cockpit and its genre's light said underwater.
        hand = G.overrides()
        for e in sheet_batch():
            if "setting" in hand.get(e["name"], {}):
                continue
            genre = game_genre.GAME_GENRE.get(e["game"], "")
            # setting_for, not the genre table: four games name their own
            # place because their genre does not say where they happen --
            # Ballyhoo is a MYSTERY the way a circus is a mystery.
            era = game_genre.setting_for(e["game"])
            self.assertIn((era or genre).lower(), e["prompt"], e["name"])

    def test_an_override_may_give_a_room_a_look_of_its_own(self):
        # Noir is a look full of doorways and columns, and a crawl space is
        # not a room. A genre's look is a default, not a sentence.
        hand = G.overrides()
        by_name = {e["name"]: e for e in sheet_batch()}
        for name, rec in hand.items():
            if "style" in rec:
                self.assertTrue(
                    by_name[name]["prompt"].endswith(
                        f"empty, {rec['style']}, deep shadow"), name)

    def test_an_override_may_give_a_room_a_light_of_its_own(self):
        hand = G.overrides()
        by_name = {e["name"]: e for e in sheet_batch()}
        overridden = [(n, r) for n, r in hand.items() if "setting" in r]
        if not overridden:
            self.skipTest("no override sets a light")
        for name, rec in overridden:
            self.assertIn(rec["setting"].lower(), by_name[name]["prompt"], name)


class TestStyle(unittest.TestCase):
    """One string decides what every plate is made of."""

    def test_every_prompt_carries_its_genres_style(self):
        import game_genre
        hand = G.overrides()
        for e in sheet_batch():
            # Unless the room named its own, which is a default overridden
            # and not a rule broken.
            style = (hand.get(e["name"], {}).get("style")
                     or game_genre.look_for(e["game"]).get("style", G.STYLE))
            self.assertIn(style, e["prompt"], e["name"])

    def test_the_looks_are_few_and_each_covers_more_than_one_game(self):
        import game_genre, collections
        per = collections.Counter()
        for stem, genre in game_genre.GAME_GENRE.items():
            look = game_genre.GENRE_LOOK.get(genre, {}).get("style")
            if look:
                per[look] += 1
        # A look with one game behind it is a look nobody can judge.
        self.assertLessEqual(len(per), 4, dict(per))
        for style, n in per.items():
            self.assertGreater(n, 1, f"{style} covers only {n} game")

    def test_a_genre_may_name_a_checkpoint_and_the_sheet_carries_it(self):
        import game_genre
        named = {g: v["checkpoint"] for g, v in game_genre.GENRE_LOOK.items()
                 if v.get("checkpoint")}
        for e in sheet_batch():
            want = named.get(game_genre.GAME_GENRE.get(e["game"]))
            self.assertEqual(e.get("checkpoint"), want, e["name"])

    def test_no_prompt_asks_for_a_photograph(self):
        # The plates are painted. A photograph is the format that fails worst
        # when a detail is wrong.
        for e in sheet_batch():
            for word in ("photographic", "photograph", "photography"):
                self.assertNotIn(word, e["prompt"], e["name"])

    def test_the_negative_prompt_agrees_with_the_style(self):
        import forge_client
        neg = forge_client.NEGATIVE
        # Whatever the plates are made of must not also be refused.
        self.assertIn("photograph", neg)
        self.assertNotIn("illustration", neg)


class TestFraming(unittest.TestCase):
    """A wide establishing shot of a closet is not a thing."""

    def test_a_room_you_could_not_stand_up_in_is_shot_from_inside(self):
        for title in ("Crawl Space, South", "Wardrobe Closet", "Wet Tunnel",
                      "Inside Cage", "Prison Cell"):
            self.assertTrue(G.CONFINED.search(title), title)

    def test_an_ordinary_room_is_not_confined(self):
        for title in ("Living Room", "Great Hall", "Country Lane",
                      "Observation Deck", "Garden, South"):
            self.assertIsNone(G.CONFINED.search(title), title)

    def test_the_two_points_of_view_are_both_used_and_do_not_overlap(self):
        # The standing one is a prefix of the confined one, so "starts with
        # POV" is true of both and only the longer match distinguishes them.
        self.assertTrue(G.POV_CONFINED.startswith(G.POV))
        close = [e for e in sheet_batch()
                 if e["prompt"].startswith(G.POV_CONFINED)]
        stood = [e for e in sheet_batch()
                 if e["prompt"].startswith(G.POV)
                 and not e["prompt"].startswith(G.POV_CONFINED)]
        self.assertGreater(len(close), 20)
        self.assertGreater(len(stood), 400)
        self.assertEqual(set(e["name"] for e in close)
                         & set(e["name"] for e in stood), set())

    def test_the_point_of_view_stays_short_enough_to_be_read(self):
        # It sits identically at the front of every prompt, so every word of it
        # is a word the room's own description does not get. The confined one
        # is allowed three more: said only as a low ceiling the model drew a
        # tall room seen from the floor, and where the ceiling sits in the
        # FRAME is the whole difference. It reaches 51 rooms, not all 1,759.
        self.assertLessEqual(len(G.POV.split()), 8, G.POV)
        self.assertLessEqual(len(G.POV_CONFINED.split()), 11, G.POV_CONFINED)

    def test_the_negative_prompt_refuses_every_other_camera(self):
        import forge_client
        for shot in ("aerial view", "bird's eye view", "establishing shot",
                     "wide shot", "from above", "isometric"):
            self.assertIn(shot, forge_client.NEGATIVE, shot)

    def test_a_crawl_space_is_said_as_one(self):
        # It was tagged CAVE and drawn as a cave under a Hollywood mansion.
        # It is the filthy gap under the floor where the rats live, not a
        # room and not a cave. The dirt, the cobwebs and the joists are what
        # say so. Two words are deliberately absent: "crawl space", which the
        # model kept rendering as an ordinary room, and "house", which pulls
        # a house interior in behind it.
        said = G.title_words("Crawl Space, South")
        for word in ("floorboards", "joists", "pipes", "wires", "dust",
                     "cobwebs", "dirt"):
            self.assertIn(word, said)
        # Four words are deliberately absent. "crawl space" and "house" were
        # each rendered as an ordinary room; "pier" IS a pillar and drew one;
        # and "room" would invite the thing this is not.
        for word in ("crawl space", "house", "pier", "room"):
            self.assertNotIn(word, said)

    def test_a_confined_room_is_told_where_the_ceiling_sits_in_frame(self):
        # "Low ceiling" got a tall room seen from the floor, which is a
        # correct description of a wrong picture.
        self.assertIn("upper half", G.POV_CONFINED)
        got = [e for e in sheet_batch()
               if e["prompt"].startswith(G.POV_CONFINED)]
        self.assertGreater(len(got), 20)


class TestSense(unittest.TestCase):
    """A word whose meaning is decided by the kind of game it is in."""

    def test_a_ship_in_a_space_game_is_a_spacecraft(self):
        # Starcross's "Outside Ship" came back as a beached ocean liner, and
        # then, once it was a spacecraft, as a giant satellite in frame. A hull
        # is somewhere to stand; a spacecraft is something to photograph.
        self.assertEqual(G.title_words("Outside Ship", "SCIFI"),
                         "outside spacecraft hull")
        self.assertEqual(G.title_words("Bridge", "SCIFI"), "command bridge")
        self.assertEqual(G.title_words("Vogon Hold", "SCIFI"),
                         "vogon cargo bay")

    def test_a_ship_in_every_other_kind_of_game_is_a_ship(self):
        self.assertEqual(G.title_words("Sunken Ship", "UNDERWATER"),
                         "sunken ship")
        self.assertEqual(G.title_words("Outside Ship", "PIRATE"),
                         "outside ship")
        # A fantasy bridge crosses water and must not become a command bridge.
        self.assertEqual(G.title_words("Stone Bridge", "FANTASY"),
                         "stone bridge")

    def test_every_replacement_names_a_place_and_not_an_object(self):
        # "Outside Ship" became "outside spacecraft" and came back as a giant
        # satellite hanging in frame. Name a thing and the model draws a
        # picture OF the thing, whatever the camera was told.
        objects = {"spacecraft", "ship", "satellite", "vehicle", "machine",
                   "robot", "car", "boat", "submarine", "rocket"}
        for table in (G.GENRE_SENSE, G.GENRE_SENSE_ANY):
            for genre, rules in table.items():
                for _pat, word in rules:
                    self.assertNotIn(word.strip().lower(), objects,
                                     f"{genre}: {word!r} names a thing, not a "
                                     "place a player could stand in")

    def test_the_negative_prompt_refuses_the_hero_composition(self):
        import forge_client
        for shot in ("satellite", "hero shot", "floating object",
                     "large object in the middle of the frame"):
            self.assertIn(shot, forge_client.NEGATIVE, shot)

    def test_the_sense_respects_word_boundaries(self):
        self.assertEqual(G.title_words("Airship", "SCIFI"), "airship")

    def test_the_sense_is_never_applied_to_prose(self):
        # Two Planetfall rooms use "hold" as a verb: planters hold dead plants,
        # and a room cannot hold a few days' worth. Neither is a cargo bay.
        got = G.clean("The planters hold dry, dead plants.")
        self.assertIn("hold", got)
        self.assertNotIn("cargo bay", got)

    def test_hardware_nouns_are_rewritten_in_prose_too(self):
        # Planetfall's Miniaturization Booth says a keyboard with numeric keys
        # and got a beige 1990s desktop for it twice. Unlike "hold" these are
        # never verbs, so prose is safe.
        got = G.clean("Mounted on the wall is a keyboard with numeric keys.",
                      "SCIFI")
        self.assertIn("numbered buttons", got)
        self.assertNotIn("keyboard", got)
        # And only in the genre that disagrees with the ordinary reading.
        plain = G.clean("Mounted on the wall is a keyboard with numeric keys.",
                        "MYSTERY")
        self.assertIn("keyboard", plain)

    def test_a_darkroom_is_a_room_with_no_light_in_it(self):
        # Not a place where film is developed under a red safelight, which is
        # what 29 rooms were being asked for.
        self.assertEqual(G.scene_words("DARKROOM"), "unlit room")

    def test_an_abbreviated_scene_tag_is_said_as_english(self):
        self.assertEqual(G.scene_words("SHIP_EXT", "PIRATE"), "outside a ship")
        self.assertEqual(G.scene_words("SHIP_EXT", "SCIFI"),
                         "outside a spacecraft hull")
        self.assertEqual(G.scene_words("HOUSE_EXT"), "outside a house")
        self.assertEqual(G.scene_words("SHIP_INT", "SCIFI"),
                         "inside a spacecraft hull")

    def test_an_ordinary_scene_tag_is_used_as_it_stands(self):
        self.assertEqual(G.scene_words("CAVE"), "cave")
        self.assertEqual(G.scene_words("GARDEN"), "garden")

    def test_no_prompt_carries_a_raw_tag_or_an_abbreviation(self):
        for e in sheet_batch():
            self.assertNotIn("_", e["prompt"], e["name"])
            for abbrev in (" ext,", " int,", "darkroom"):
                self.assertNotIn(abbrev, e["prompt"], e["name"])


class TestOverrides(unittest.TestCase):
    def test_an_override_replaces_only_the_rooms_own_words(self):
        hand = G.overrides()
        if not hand:
            self.skipTest("no overrides written")
        by_name = {e["name"]: e for e in sheet_batch()}
        for name, rec in hand.items():
            self.assertIn(name, by_name, name)
            prompt = by_name[name]["prompt"]
            # The point of view leads even an override: all of them are POV.
            self.assertIn(rec["text"].lower(), prompt, name)
            self.assertTrue(prompt.startswith("first person view"), name)
            # The tail is still appended: an override cannot opt a room out.
            # It is the genre's tail now, not one tail for the whole disc.
            import game_genre
            style = (rec.get("style")
                     or game_genre.look_for(by_name[name]["game"]).get(
                         "style", G.STYLE))
            self.assertTrue(prompt.endswith(f"empty, {style}, deep shadow"),
                            name)

    def test_an_override_may_refuse_what_only_that_room_must(self):
        # A crawl space must refuse furniture and a midway must refuse the
        # crowd it would obviously have; neither is safe disc-wide.
        hand = G.overrides()
        by_name = {e["name"]: e for e in sheet_batch()}
        for name, rec in hand.items():
            if "negative" in rec:
                # Contained rather than equal: a confined room's own refusal
                # rides on top of the one its whole class gets.
                self.assertIn(rec["negative"],
                              by_name[name].get("negative", ""), name)
        # And a room with nothing to refuse carries no extra negative at all.
        plain = [e for e in sheet_batch()
                 if e["name"] not in hand
                 and not e["prompt"].startswith(G.POV_CONFINED)]
        self.assertTrue(all("negative" not in e for e in plain))

    def test_every_confined_room_refuses_a_rooms_architecture(self):
        # A door and a pillar are what a "room" brings with it, and a pillar
        # is also literally what the word "pier" asked for.
        for word in ("door", "pillar", "column", "arch", "architecture"):
            self.assertIn(word, G.CONFINED_REFUSE, word)

    def test_every_confined_room_refuses_the_height(self):
        # Saying the ceiling is low in the positive got a tall room seen from
        # the floor. All 51 refuse the height, not just the two looked at.
        got = [e for e in sheet_batch()
               if e["prompt"].startswith(G.POV_CONFINED)]
        self.assertGreater(len(got), 20)
        for e in got:
            self.assertIn("high ceiling", e.get("negative", ""), e["name"])

    def test_the_exit_is_only_the_way_out_when_it_is_the_subject(self):
        # "Near the exit is a game booth lined with prizes" is a game booth,
        # and it is Sorcerer's midway.
        got = G.clean("Near the exit is a game booth lined with prizes.")
        self.assertIn("game booth", got)
        self.assertIn("prizes", got)
        # A sentence that really is about the way out still goes.
        self.assertEqual(G.clean("The only exit is the way you came in."), "")

    def test_a_named_reference_image_must_actually_be_there(self):
        # A missing reference would fall back to drawing from the words alone,
        # which is the exact failure the mechanism exists to end.
        import json as _json
        import tempfile
        original = G.OVERRIDES
        try:
            with tempfile.TemporaryDirectory() as tmp:
                path = pathlib.Path(tmp) / "o.json"
                path.write_text(_json.dumps({"rooms": {"x_1": {
                    "text": "a hall", "compose_from": "not_here.jpg"}}}),
                    encoding="utf-8")
                G.OVERRIDES = path
                with self.assertRaises(SystemExit):
                    G.overrides()
        finally:
            G.OVERRIDES = original

    def test_a_room_composing_from_an_image_carries_it_into_the_sheet(self):
        hand = G.overrides()
        by_name = {e["name"]: e for e in sheet_batch()}
        for name, rec in hand.items():
            if rec.get("compose_from"):
                self.assertEqual(by_name[name].get("compose_from"),
                                 rec["compose_from"], name)
                self.assertIsInstance(by_name[name].get("denoise"), float)
            else:
                self.assertNotIn("compose_from", by_name[name], name)

    def test_an_override_may_not_say_no_people(self):
        import json as _json
        import tempfile
        original = G.OVERRIDES
        try:
            with tempfile.TemporaryDirectory() as tmp:
                path = pathlib.Path(tmp) / "o.json"
                path.write_text(_json.dumps(
                    {"rooms": {"x_1": {"text": "a hall with no people"}}}),
                    encoding="utf-8")
                G.OVERRIDES = path
                with self.assertRaises(SystemExit):
                    G.overrides()
        finally:
            G.OVERRIDES = original


if __name__ == "__main__":
    unittest.main()
