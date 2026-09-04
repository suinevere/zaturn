#!/usr/bin/env python3
"""/*----------------------
 | forge_client.py
 | Description: Talks to a local Stable Diffusion Forge server's txt2img API,
 |     for generating the source plates tools/gen_art_archive.py then puts into
 |     the house style.
 |
 |     Nothing about this is part of a build. The server is a machine the
 |     project happens to have access to, not a dependency of the disc: every
 |     plate it produces is committed as a PNG, and a checkout without it can
 |     still build every archive. What it saves is regenerating art by hand.
 |
 |     Two things about that server, learned the hard way and recorded here
 |     because they are invisible from a stack trace. It must be launched with
 |     --api: without it every /sdapi/v1/* is a 404 while the web UI works
 |     perfectly, so the symptom is a server that is plainly running and an
 |     endpoint that is plainly not there. And urllib is used rather than curl
 |     because curl and wget are intercepted in this environment.
 |
 |     The sampler settings are the ones that were measured to work on this
 |     machine rather than the defaults, and all three moved together once it
 |     turned out they were the whole problem. The checkpoint was a photorealism
 |     model with a Hyper distillation, which requires CFG 2.5 -- and at 2.5 a
 |     hundred-token negative prompt barely applies, so people kept appearing in
 |     plates that forbade them nine ways and a painted illustration kept coming
 |     back a photograph. dreamshaper_8 is not distilled, so it takes the CFG
 |     that makes a prompt mean something. Eight steps was measured against the
 |     old model; 25 is what the new one wants and the downsample to 320x240
 |     still forgives the difference.
 | Author: suinevere
 | Dependencies: base64, io, json, urllib
 | Globals: HOST, CHECKPOINT, STEPS, CFG, SAMPLER, SCHEDULER, WIDTH, HEIGHT,
 |     NEGATIVE
 ----------------------*/"""
import base64
import io
import json
import urllib.error
import urllib.request

HOST = "http://127.0.0.1:7860"
CHECKPOINT = "dreamshaper_8"
STEPS = 25
CFG = 7.0
SAMPLER = "DPM++ SDE"
SCHEDULER = "SGM Uniform"
WIDTH, HEIGHT = 512, 384
NEGATIVE = ("person, people, man, woman, child, human, figure, silhouette, "
            "crowd, portrait, face, head, hands, arms, legs, body, standing, "
            "walking, statue, mannequin, "
            "text, watermark, signature, caption, letters, sign, "
            "still life, product photograph, studio lighting, macro, close-up, "
            "centred object, single object, food, dessert, cutlery, "
            "satellite, spaceship, vehicle, machine, hero shot, "
            "large object in the middle of the frame, floating object, "
            "photograph, photorealistic, stock photo, 3d render, cgi, "
            "aerial view, bird's eye view, drone shot, from above, "
            "establishing shot, wide shot, distant view, third person, "
            "isometric, orthographic, floor plan, diorama, miniature, "
            "modern, car, blurry, low quality, oversaturated, anime")
"""HOST / STEPS / CFG / SAMPLER / SCHEDULER / WIDTH / HEIGHT / NEGATIVE

Description: Where the server is, which checkpoint draws, and the settings that
    were measured to work on it. 512x384 is generated rather than 320x240 because the styler downsamples
    to the screen itself and a larger source survives that better; the aspect is
    4:3 either way. The negative prompt carries what a room background must not
    have: a figure in it makes it a scene rather than a place, and anything
    written on it is text the game is already drawing over.

    Every way a person can be named is listed, and the list leads, because this
    is the one thing the negative prompt has to win and it is working against a
    handicap: CFG is 2.5, which the Hyper merge requires and which is low
    enough that negative conditioning barely bites. The other half of that fix
    is not here -- it is that no positive prompt may contain the words "no
    people", because diffusion has no negation and that phrase puts the token
    "people" straight into the conditioning that is being sampled towards.

    The still-life run is there for a different failure with the same shape. A
    third of the rooms have no prose, so their prompt is a title and a genre,
    and a bare noun phrase is read as the SUBJECT of a photograph: "fork, of
    sorts" came back as a studio-lit fork lying on a road and "martian dessert"
    as a cupcake on a dune. Both are competently drawn photographs of an object,
    which is the one composition a room background can never be. The other half
    of this fix is also not here -- it is gen_room_prompts.POV, which puts the
    camera where the player is standing rather than where a photographer of the
    room would be.

    A room background is an ENVIRONMENT and never a thing seen whole, which is
    the same failure in its third costume: a cupcake, then a fork, then a giant
    satellite filling the frame for a room called "Outside Ship". The other
    half of that one is also in gen_room_prompts, where every genre-sense
    replacement now has to name a place instead of an object -- a hull rather
    than a spacecraft. This list can only refuse the composition; it cannot
    stop a positive prompt naming something photogenic.

    The camera run is the other half of gen_room_prompts.POV. Saying "first
    person point of view" in the positive was not enough on its own: a prompt
    that also described what the place looks like from outside pulled the
    camera back out to show it, and a tightrope asked for from on the wire came
    back as a picture of a circus seen from across the tent. Every way of
    naming a shot from somewhere the player is not has to be refused here.

    Photograph is in the negative and cartoon is no longer, which is a reversal.
    The plates are painted now: the disc's own frames are photographs but they
    are posterised to a duotone at 320x240, and a photograph is the format that
    fails worst when a detail is wrong -- a competently photographed beige
    1990s desktop in a Miniaturization Booth reads as a mistake in a way a
    painted console does not. Anime stays out because it is a specific look
    that is not this one; illustration had to come out or it fights STYLE.
Author: suinevere
"""


def alive(host=HOST, timeout=5):
    """/*----------------------
     | alive
     | Description: Whether the API is up and answering, as opposed to the web
     |     UI being up -- which it can be while every endpoint here 404s.
     | Author: suinevere
     | Dependencies: urllib
     | Globals: HOST
     | Params: host -- the server; timeout -- seconds
     | Returns: the loaded checkpoint's name, or None
     ----------------------*/"""
    try:
        with urllib.request.urlopen(host + "/sdapi/v1/options", timeout=timeout) as r:
            return json.loads(r.read()).get("sd_model_checkpoint")
    except Exception:
        return None


def models(host=HOST):
    """/*----------------------
     | models
     | Description: The checkpoints the server has, by name.
     | Author: suinevere
     | Dependencies: json, urllib
     | Globals: HOST
     | Params: host -- the server
     | Returns: a list of checkpoint names, empty when the server is not up
     ----------------------*/"""
    try:
        with urllib.request.urlopen(host + "/sdapi/v1/sd-models",
                                    timeout=15) as r:
            return [m.get("model_name") for m in json.loads(r.read())]
    except Exception:
        return []


def set_checkpoint(name, host=HOST):
    """/*----------------------
     | set_checkpoint
     | Description: Loads a checkpoint on the server and leaves it loaded.
     |
     |     Once, at the start of a run, rather than per plate. A checkpoint sent
     |     with every request and restored afterwards makes the server unload
     |     and reload a two gigabyte model between pictures, which turns a ten
     |     hour run into a week.
     | Author: suinevere
     | Dependencies: json, urllib
     | Globals: HOST
     | Params: name -- the checkpoint; host -- the server
     | Returns: N/A
     ----------------------*/"""
    body = json.dumps({"sd_model_checkpoint": name}).encode("utf-8")
    req = urllib.request.Request(host + "/sdapi/v1/options", data=body,
                                 headers={"Content-Type": "application/json"})
    try:
        urllib.request.urlopen(req, timeout=600).read()
    except urllib.error.HTTPError as e:
        raise SystemExit(f"forge_client: could not load {name!r}: {e.code}")


def img2img(prompt, seed, reference, denoise=0.6, host=HOST, steps=STEPS,
            cfg=CFG, negative=NEGATIVE, checkpoint=None):
    """/*----------------------
     | img2img
     | Description: One image drawn over the bones of another, as PNG bytes.
     |
     |     For the rooms whose composition is the whole point and which a
     |     sentence cannot pin down. Seven rewordings of one crawl space each
     |     produced exactly what was asked for and none of it was a crawl
     |     space: "brick piers" drew pillars, "a shaft of light" drew a
     |     skylight, "slivers between the boards" drew a hole through them. A
     |     photograph of a crawl space has the geometry already and does not
     |     have to be described into existence.
     |
     |     denoise is how much of the reference survives. Below about 0.4 the
     |     plate is the photograph with a filter on it, which is not what the
     |     disc is for; above about 0.75 the composition it was brought in for
     |     has gone again. 0.6 keeps the geometry and repaints the surface.
     | Author: suinevere
     | Dependencies: base64, json, urllib
     | Globals: SAMPLER, SCHEDULER, WIDTH, HEIGHT
     | Params: prompt -- what to draw; seed -- the seed; reference -- PNG or
     |     JPEG bytes to compose from; denoise -- how much to repaint;
     |     host, steps, cfg, negative, checkpoint -- overrides
     | Returns: PNG bytes
     ----------------------*/"""
    body = json.dumps({
        "init_images": [base64.b64encode(reference).decode("ascii")],
        "denoising_strength": float(denoise),
        "resize_mode": 1,
        "prompt": prompt,
        "negative_prompt": negative,
        "seed": int(seed),
        "steps": int(steps),
        "cfg_scale": float(cfg),
        "sampler_name": SAMPLER,
        "scheduler": SCHEDULER,
        "width": WIDTH,
        "height": HEIGHT,
        "batch_size": 1,
        "n_iter": 1,
        **({"override_settings": {"sd_model_checkpoint": checkpoint},
            "override_settings_restore_afterwards": False}
           if checkpoint else {}),
    }).encode("utf-8")
    req = urllib.request.Request(host + "/sdapi/v1/img2img", data=body,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=300) as r:
            out = json.loads(r.read())
    except urllib.error.HTTPError as e:
        raise SystemExit(f"forge_client: img2img returned {e.code}")
    if not out.get("images"):
        raise SystemExit("forge_client: the server returned no image")
    return base64.b64decode(out["images"][0])


def txt2img(prompt, seed, host=HOST, steps=STEPS, cfg=CFG, negative=NEGATIVE,
            checkpoint=None):
    """/*----------------------
     | txt2img
     | Description: One image, as PNG bytes.
     |
     |     The seed is required rather than defaulted to -1: these plates are
     |     committed and a regenerated one has to be the same picture, or the
     |     archive it is packed into stops matching the offsets already written
     |     down for it.
     | Author: suinevere
     | Dependencies: base64, json, urllib
     | Globals: SAMPLER, SCHEDULER, WIDTH, HEIGHT
     | Params: prompt -- what to draw; seed -- the seed; host, steps, cfg,
     |     negative, checkpoint -- overrides
     | Returns: PNG bytes
     ----------------------*/"""
    body = json.dumps({
        "prompt": prompt,
        "negative_prompt": negative,
        "seed": int(seed),
        "steps": int(steps),
        "cfg_scale": float(cfg),
        "sampler_name": SAMPLER,
        "scheduler": SCHEDULER,
        "width": WIDTH,
        "height": HEIGHT,
        "batch_size": 1,
        "n_iter": 1,
        **({"override_settings": {"sd_model_checkpoint": checkpoint},
            "override_settings_restore_afterwards": False}
           if checkpoint else {}),
    }).encode("utf-8")
    req = urllib.request.Request(host + "/sdapi/v1/txt2img", data=body,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=300) as r:
            out = json.loads(r.read())
    except urllib.error.HTTPError as e:
        raise SystemExit(f"forge_client: txt2img returned {e.code} -- if this is "
                         "a 404 the server was started without --api, which "
                         "leaves the web UI working and every endpoint gone")
    if not out.get("images"):
        raise SystemExit("forge_client: the server returned no image")
    return base64.b64decode(out["images"][0])
