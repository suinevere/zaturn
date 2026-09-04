#!/usr/bin/env python3
"""/*----------------------
 | gen_scene_prompts.py
 | Description: GENERATES tools/assets/art/prompts.json -- three plates for
 |     every scene tag rooms are classified into, so the thirty games that are
 |     not Zork I can be given pictures of their own places instead of Zork I's.
 |
 |     The words are authored here and the numbers are measured here, which is
 |     the whole point of generating the sheet rather than writing it. A plate's
 |     reference rotates through its scene's own measured list, so three
 |     corridors are graded against three different corridors and do not come
 |     back as one colour; and its lift is computed from that reference rather
 |     than guessed, as the gain that lands the finished plate at the
 |     brightness room_art_style aims for.
 |     Setting lift by eye is what produced a first plate at mean 8.6 against a
 |     disc whose own average is 18.7, three times over.
 |
 |     Seeds are assigned by position and never reused: a plate is committed and
 |     a regenerated one has to be the same picture, or the offsets already
 |     written down for it point into the middle of a record.
 | Author: suinevere
 | Dependencies: json, pathlib, sys, image_looks, room_art_style, scene_vocab
 | Globals: ROOT, OUT, SEED0, HELD, SCENE_PROMPTS
 ----------------------*/"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import image_looks
import room_art_style
import scene_vocab

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "tools" / "assets" / "art" / "prompts.json"
SEED0 = 1000
HELD = {"bridge", "corridor_stone", "corridor_timber", "corridor_rock",
        "corridor_metal", "maze_brick", "maze_stone", "forest_birch",
        "forest_pine", "mine_shaft", "road_track", "road_paved", "cave_teal"}
"""ROOT / OUT / SEED0 / HELD

Description: Where the sheet goes, the seed the numbering starts from, and the names already drawn and packed
    at indices 75 to 87. HELD is not skipped -- gen_art_source skips anything
    already in the manifest -- but its names are avoided here so a new plate can
    never collide with one whose index is already in a room record.
Author: suinevere
"""

TAIL = "empty, abandoned, uninhabited, photographic, deep shadow"

SCENE_PROMPTS = {
    "BATHROOM": [
        ("tiled", "a tiled washroom, basin and mirror",
         "a small tiled washroom, white basin, clouded mirror, chrome tap, damp"),
        ("bathhouse", "a stone bathhouse pool, steam over water",
         "an old stone bathhouse, sunken pool, steam over still water, columns"),
        ("cramped", "a cramped ship's washroom in metal",
         "a cramped metal washroom aboard a ship, riveted panels, small round mirror"),
    ],
    "BEDROOM": [
        ("fourposter", "a four-poster bed under heavy hangings",
         "an old bedroom, four poster bed with heavy hangings, night table, shuttered window"),
        ("bunk", "a narrow bunk in a panelled cabin",
         "a narrow bunk built into a panelled cabin wall, blanket, brass lamp, low ceiling"),
        ("attic", "an attic bed under a sloping roof",
         "an attic bedroom under a steeply sloping roof, iron bedstead, bare boards, small dormer"),
    ],
    "CASTLE": [
        ("hall", "a great hall with a vaulted roof",
         "a great stone hall, high vaulted roof, banners, long empty table, cold light from tall windows"),
        ("rampart", "a battlement walk between merlons",
         "a castle battlement walk, square merlons, worn flagstones, mist below the wall"),
        ("gate", "a portcullis arch into a courtyard",
         "a castle gatehouse arch, raised portcullis, cobbled courtyard beyond, massive masonry"),
    ],
    "CAVE": [
        ("dry", "a dry chamber of pale rock",
         "a dry limestone cave chamber, pale rock walls, dust, low ceiling, faint light from a crack"),
        ("crystal", "a chamber lined with crystal growth",
         "a cave chamber lined with crystal growth catching the light, rough rock behind, still air"),
        ("column", "a cavern where a column meets the floor",
         "a large cavern, a thick stalagmite column meeting the floor, flowstone, distant dark"),
    ],
    "CELL": [
        ("barred", "a cell behind heavy iron bars",
         "a prison cell seen past heavy iron bars, straw on the stone floor, high slit window"),
        ("oubliette", "a bare stone pit with a grate above",
         "a bare stone oubliette, iron grate high overhead, damp walls, nothing on the floor"),
        ("brig", "a metal holding cell aboard a ship",
         "a metal holding cell aboard a ship, riveted bulkhead, bench, barred door, cold light"),
    ],
    "CORRIDOR": [
        ("panelled", "a panelled hallway with closed doors",
         "a long panelled hallway, closed doors down one side, worn carpet runner, dim wall lamps"),
        ("vault", "a low barrel vault of set brick",
         "a low brick barrel vaulted passage receding, set brick, damp floor, one lamp far ahead"),
        ("gallery", "a colonnaded gallery running away",
         "a stone colonnaded gallery running away from the viewer, columns on one side, flagstones"),
    ],
    "CRYPT": [
        ("niches", "a crypt wall of stacked burial niches",
         "a crypt, stacked burial niches cut into the stone wall, dust, low arched ceiling"),
        ("sarcophagus", "a stone sarcophagus in a vault",
         "a carved stone sarcophagus in a low vault, chiselled lid, cobwebs, single shaft of light"),
        ("ossuary", "an ossuary of stacked bone and stone",
         "an ossuary chamber, stacked stone burial caskets behind a screen, arched recess, very dim"),
    ],
    "DARKROOM": [
        ("void", "a room the light does not reach",
         "a lightless room, only the nearest edge of a stone floor visible, everything beyond black"),
        ("doorway", "a black room with one lit doorway",
         "a pitch dark room with a single dimly lit doorway far off, floor barely visible"),
        ("lamp", "a failing lamp in an unlit space",
         "an unlit interior with a single failing oil lamp, small circle of light, black beyond"),
    ],
    "DESERT": [
        ("dune", "wind-rippled dunes to the horizon",
         "wind rippled sand dunes running to a low horizon, hard shadow, no vegetation"),
        ("hardpan", "cracked hardpan under a pale sky",
         "cracked dry hardpan desert floor, distant flat mesa, pale hazy sky, no vegetation"),
        ("rockdesert", "boulders on red desert sand",
         "scattered boulders on red desert sand, low scrub, long shadows, empty horizon"),
    ],
    "DOCK": [
        ("wharf", "a timber wharf over dark water",
         "an old timber wharf over dark water, bollards, coiled rope, moored hull just visible"),
        ("harbour", "a stone harbour wall and steps",
         "a stone harbour wall with weed covered steps down to the water, iron ring, still water"),
        ("jetty", "a narrow jetty running out into mist",
         "a narrow wooden jetty running out into mist over flat water, posts, no boat"),
    ],
    "FOREST": [
        ("clearing", "a small clearing walled by trees",
         "a small clearing walled in by trees, long grass, light falling into the middle, no path"),
        ("mossy", "moss-covered trunks and deep undergrowth",
         "moss covered tree trunks, deep green undergrowth, fallen log, heavy shade, no path"),
        ("winter", "bare branches over frozen ground",
         "bare winter branches over hard frozen ground, thin trunks, cold flat light, no path"),
    ],
    "GARDEN": [
        ("formal", "clipped hedges along a gravel walk",
         "a formal garden, clipped hedges either side of a gravel walk, stone urn, overcast"),
        ("overgrown", "a walled garden gone to seed",
         "an overgrown walled garden, brick wall, weeds through paving, rusted gate, nobody"),
        ("greenhouse", "a glasshouse of leaves and iron",
         "the inside of an old iron framed glasshouse, dense leaves against the glass, brick floor"),
    ],
    "HOUSE_EXT": [
        ("cottage", "a shuttered cottage front",
         "the front of a small stone cottage, shuttered windows, low door, overgrown path, overcast"),
        ("manor", "a manor facade behind a lawn",
         "the facade of an old manor house across an unkempt lawn, tall windows, grey sky"),
        ("porch", "a wooden porch and a closed door",
         "a wooden porch with peeling paint and a closed front door, boards, empty chair"),
    ],
    "KITCHEN": [
        ("range", "a range and hanging pans",
         "an old kitchen, black iron range, pans hanging above, scrubbed table, dim window"),
        ("scullery", "a stone scullery with a deep sink",
         "a stone floored scullery, deep stone sink, shelves of crockery, damp wall, small window"),
        ("galley", "a ship's galley in steel",
         "a ship's galley, steel counters and lockers, small round port, everything stowed"),
    ],
    "LAB": [
        ("glassware", "benches of glassware and burners",
         "a laboratory bench crowded with glassware, retorts and burners, shelves of jars behind"),
        ("machine", "a control bank of dials and switches",
         "a bank of scientific instruments, dials and toggle switches, cabling, indicator lamps"),
        ("operating", "a clinical room with a steel table",
         "a clinical room, steel examination table, instrument tray, tiled wall, hard overhead light"),
    ],
    "LIBRARY": [
        ("stacks", "shelves of books running away",
         "library stacks running away from the viewer, shelves floor to ceiling, ladder, dim aisle"),
        ("readingroom", "a reading room with a long table",
         "a library reading room, long table, green shaded lamps, galleries of books above"),
        ("study", "a private study lined with books",
         "a private study lined with books, desk, globe, leather chair, fire out, one lamp"),
    ],
    "MAZE": [
        ("hedge", "identical hedge walls and a turn",
         "a hedge maze, tall identical clipped walls, a turning that shows only more hedge"),
        ("mirror", "a passage that repeats itself",
         "a confusing passage of identical stone arches repeating away in both directions"),
        ("tunnels", "three identical mouths in rough rock",
         "three identical dark tunnel openings in a rough rock wall, nothing to tell them apart"),
    ],
    "MINE": [
        ("gallery", "a propped gallery with a rail",
         "an underground mine gallery, timber props, narrow rail track, spoil, dim lamp"),
        ("facewall", "a worked rock face and tools",
         "a mine working wall, drill marks in the rock, abandoned pick and bucket, dust"),
        ("cage", "a shaft head with a lift cage",
         "a mine shaft top underground, iron lift cage, chains, heavy timber, black shaft"),
    ],
    "OFFICE": [
        ("desk", "a desk under a shaded lamp",
         "an office, wooden desk under a shaded lamp, papers, filing cabinet, blinds drawn"),
        ("clerks", "rows of clerks' desks, all empty",
         "rows of empty clerks desks in a large office, typewriters, hard overhead light"),
        ("control", "an office of monitors and consoles",
         "a control office, consoles and screens dark, swivel chair, cabling, low light"),
    ],
    "PARLOR": [
        ("sitting", "an armchair by a cold hearth",
         "a sitting room, armchair beside a cold hearth, patterned wallpaper, heavy curtains"),
        ("drawing", "a drawing room with a piano",
         "a drawing room, upright piano, rug, portrait over the mantel, shuttered light"),
        ("lounge", "a panelled lounge and a low table",
         "a panelled lounge, low table, decanter, deep chairs, lamps unlit, dusk through the window"),
    ],
    "PIT": [
        ("shaftpit", "a black shaft dropping away",
         "the edge of a black shaft dropping away, rough rock rim, nothing visible below"),
        ("chasm", "a chasm crossed by a plank",
         "a chasm in a cave floor crossed by a single plank, dark below, rough walls"),
        ("well", "a stone well mouth in a floor",
         "a stone well opening set into a flagged floor, rope over the rim, blackness inside"),
    ],
    "RIVER": [
        ("underground", "a black river under rock",
         "an underground river running through a rock channel, black water, wet ledges"),
        ("bank", "a slow river between reeds",
         "a slow river between reed banks, flat water, low mist, grey sky, no boat"),
        ("rapids", "white water over stones",
         "a fast river over stones, white water, wet boulders, steep wooded bank"),
    ],
    "ROAD": [
        ("crossroads", "a signless crossroads on a moor",
         "an empty crossroads on open moorland, rutted track, heather and gorse, low horizon, wide sky"),
        ("avenue", "a road under an avenue of trees",
         "a straight road running away under an avenue of tall trees, long shadows, no traffic"),
        ("causeway", "a raised causeway over flats",
         "a raised stone causeway running across tidal flats, water either side, flat grey light"),
    ],
    "ROCKY": [
        ("scree", "a slope of loose scree",
         "a steep slope of loose grey scree, broken rock, thin cloud, nothing growing"),
        ("ledge", "a narrow ledge along a cliff",
         "a narrow rock ledge running along a cliff wall, drop on one side, bare stone"),
        ("boulders", "a field of split boulders",
         "a field of huge split boulders, moss in the cracks, overcast, no path through"),
    ],
    "SHIP_EXT": [
        ("hull", "a hull at anchor seen from the water",
         "a large ship hull at anchor seen low from the water, rivets, anchor chain, mist"),
        ("wreck", "a wreck aground and listing",
         "a rusted shipwreck aground and listing on a shore, broken plating, still water"),
        ("deck", "an open deck of rail and rigging",
         "an open ship deck, rail and rigging, hatch cover, wet planking, grey sea beyond"),
    ],
    "SHIP_INT": [
        ("engine", "an engine room of pipes and gauges",
         "a ship engine room, heavy machinery, pipes and gauges, steel walkway, low light"),
        ("cabin", "a panelled cabin with a port",
         "a small panelled ship cabin, bunk, desk, round brass port, oil lamp"),
        ("hold", "a cargo hold of crates and ribs",
         "a ship cargo hold, stacked crates, curved steel frames, chain, one hanging bulb"),
    ],
    "SHORE": [
        ("shingle", "a shingle beach under cliffs",
         "a shingle beach under low cliffs, wet stones, flat sea, overcast, driftwood"),
        ("sandbeach", "wet sand and a far tideline",
         "a wide flat wet sand beach, distant tideline, reflected sky, nobody"),
        ("cove", "a rocky cove with a small strand",
         "a small rocky cove, dark headland either side, narrow strand, breaking swell"),
    ],
    "SPACE": [
        ("viewport", "stars through a heavy viewport",
         "stars and a distant planet seen through a heavy round spacecraft viewport, dark interior"),
        ("surface", "an airless plain under black sky",
         "an airless grey planetary surface, craters, sharp shadows, black sky, no atmosphere"),
        ("dock", "a station bay open to the dark",
         "a space station docking bay open to the dark, gantries, floodlights, no crew"),
    ],
    "STORAGE": [
        ("crates", "stacked crates in a store room",
         "a store room of stacked wooden crates and barrels, dust, single high window"),
        ("cellarstore", "shelved jars in a cool cellar",
         "a cool cellar of shelves and stoneware jars, brick vault, cobwebs, lantern light"),
        ("lockup", "a lockup of tools and shelving",
         "a lockup of metal shelving, tools and boxes, concrete floor, bare bulb"),
    ],
    "TEMPLE": [
        ("nave", "a nave between heavy columns",
         "a temple nave between heavy stone columns, altar far off, shafts of light, empty"),
        ("shrine", "a carved shrine in an alcove",
         "a carved stone shrine set in an alcove, worn geometric relief carving, "
         "offering bowl, dim"),
        ("cloister", "a cloister walk around a court",
         "a stone cloister walk around an open court, arcade of arches, worn paving, still"),
    ],
    "THEATER": [
        ("stage", "a stage seen from the empty stalls",
         "an empty theatre stage seen from the stalls, heavy curtain half drawn, footlights off"),
        ("boxes", "gilded boxes above dark seats",
         "gilded theatre boxes above rows of dark empty seats, chandelier unlit, dust"),
        ("backstage", "ropes and flats behind the scenes",
         "backstage of an old theatre, hanging ropes, painted flats stacked, bare working light"),
    ],
    "VILLAGE": [
        ("lane", "a village lane of stone cottages",
         "a village lane of low stone cottages, walled gardens, overcast, nobody about"),
        ("square", "a village square and a well",
         "a small village square with a stone well, shuttered houses around, wet cobbles"),
        ("bridgeway", "a stone bridge into a hamlet",
         "an old stone bridge over a stream into a hamlet, low roofs beyond, grey light"),
    ],
}
"""SCENE_PROMPTS

Description: Three treatments per scene, chosen to be different PLACES rather
    than three renders of one, because the spreading picks one per area and two
    areas that look alike defeat it. Every prompt names an empty place: a figure
    in a room background makes it a scene rather than a place, and the game
    draws its own text over it.
Author: suinevere
"""


def lift_for(index):
    """Where the plate is aimed, which room_art_style owns: a stored copy
    of that answer is what left every already-drawn plate at the old
    brightness when the target moved."""
    return room_art_style.lift_for(index)


def main():
    """/*----------------------
     | main
     | Description: Writes the sheet.
     | Author: suinevere
     | Dependencies: json, image_looks, scene_vocab
     | Globals: OUT, ROOT, SCENE_PROMPTS, SEED0, HELD
     | Params: N/A
     | Returns: 0
     ----------------------*/"""
    missing = sorted(set(scene_vocab.SCENES) - set(SCENE_PROMPTS))
    if missing:
        raise SystemExit("gen_scene_prompts: no prompts for " + ", ".join(missing))

    batch, seed = [], SEED0
    for scene in sorted(SCENE_PROMPTS):
        refs = [i for i in image_looks.images_for(scene) if i <= 74]
        if not refs:
            raise SystemExit(f"gen_scene_prompts: {scene} has no measured picture "
                             "to grade against")
        for n, (suffix, shows, prompt) in enumerate(SCENE_PROMPTS[scene]):
            name = f"{scene.lower()}_{suffix}"
            if name in HELD:
                raise SystemExit(f"gen_scene_prompts: {name} collides with a plate "
                                 "already packed at a fixed index")
            ref = refs[n % len(refs)]
            batch.append({
                "name": name,
                "scenes": [scene],
                "reference": ref,
                "seed": seed,
                "lift": lift_for(ref),
                "shows": shows,
                "prompt": f"{prompt}, {TAIL}",
            })
            seed += 1

    OUT.write_text(json.dumps({
        "_comment": "GENERATED by tools/gen_scene_prompts.py -- three plates for "
                    "every scene tag, so the thirty games that are not Zork I can "
                    "be shown their own places rather than Zork I's. reference "
                    "rotates through the scene's own measured list so three "
                    "corridors are not one colour; lift is computed from that "
                    "reference to land the finished plate near a mean of "
                    f"{room_art_style.TARGET_MEAN:.0f}, against the disc's own average of 18.7, "
                    "rather than set by eye.",
        "batch": batch,
    }, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Wrote {OUT.relative_to(ROOT)}: {len(batch)} plates across "
          f"{len(SCENE_PROMPTS)} scenes, seeds {SEED0}..{seed - 1}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
