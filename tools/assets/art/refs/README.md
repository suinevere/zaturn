# Composition references

Images a plate is drawn *over* rather than described into existence, for the
handful of rooms whose geometry is the whole point.

Drop a photograph in here and name it from a room's entry in
`../room_prompt_overrides.json`:

```json
"holywood_105": {
  "compose_from": "crawlspace.jpg",
  "denoise": 0.6,
  ...
}
```

`denoise` is how much of the reference is repainted. Below about 0.4 the plate
is the photograph with a filter on it, which is not what the disc is for; above
about 0.75 the composition it was brought in for has gone again. 0.6 keeps the
geometry and repaints the surface.

Any size works -- the server rescales to 512x384 -- but a 4:3 reference crops
to nothing, so prefer one already about that shape.

A room that names a reference which is not here is a hard error rather than a
silent fall back to drawing from the words, because the silent fall back is the
exact failure this directory exists to end.

Why it exists: seven rewordings of one crawl space each produced precisely what
they asked for and none of it was a crawl space. "brick piers" drew pillars,
"a shaft of light" drew a skylight, "slivers of light between the boards" drew
a hole through them. A photograph of a crawl space has the geometry already.
