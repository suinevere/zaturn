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
 |     machine rather than the defaults: eight steps because everything past it
 |     dies in the downsample from 512x384 to the 320x240 the Saturn shows, and
 |     CFG 2.5 because the checkpoint is a Hyper merge and burns at the usual
 |     7.
 | Author: suinevere
 | Dependencies: base64, io, json, urllib
 | Globals: HOST, STEPS, CFG, SAMPLER, SCHEDULER, WIDTH, HEIGHT, NEGATIVE
 ----------------------*/"""
import base64
import io
import json
import urllib.error
import urllib.request

HOST = "http://127.0.0.1:7860"
STEPS = 8
CFG = 2.5
SAMPLER = "DPM++ SDE"
SCHEDULER = "SGM Uniform"
WIDTH, HEIGHT = 512, 384
NEGATIVE = ("person, people, man, woman, child, human, figure, silhouette, "
            "crowd, portrait, face, head, hands, arms, legs, body, standing, "
            "walking, statue, mannequin, "
            "text, watermark, signature, caption, letters, sign, "
            "modern, car, blurry, low quality, oversaturated, cartoon, anime, "
            "illustration")
"""HOST / STEPS / CFG / SAMPLER / SCHEDULER / WIDTH / HEIGHT / NEGATIVE

Description: Where the server is and the settings that were measured to work on
    it. 512x384 is generated rather than 320x240 because the styler downsamples
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


def txt2img(prompt, seed, host=HOST, steps=STEPS, cfg=CFG, negative=NEGATIVE):
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
     |     negative -- overrides
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
