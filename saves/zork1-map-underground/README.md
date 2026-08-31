# Zork I (Japan) - underground map walk, 2026-08-30

The capture behind `docs/ZORK1_MAP_RECON.md`'s conclusion that the map stores no
room coordinates. Kept in a subdirectory because the tracked boot states in
`saves/` carry the same disc hash and would otherwise collide.

Every state was saved **with the map screen open**, which is the point: the
original builds its geometry on the stack when the map opens and discards it on
close, so a map-closed capture cannot show the layout at all.

| Slot | Move | Room | Id | Cursor |
|---|---|---|---|---|
| mc0 | - | Living Room | 15 | `0x0204` |
| mc1 | D | Cellar | 16 | `0x0011` |
| mc2 | N | Troll Room | 20 | `0x0010` |
| mc3 | S | Cellar | 16 | `0x0011` |
| mc4 | S | East of Chasm | 17 | `0x0005` |
| mc5 | E | Gallery | 18 | `0x0041` |
| mc6 | N | Studio | 19 | `0x0110` |
| mc7 | S | Gallery | 18 | `0x0041` |
| mc8 | N | Studio | 19 | `0x0110` |
| mc9 | U | Kitchen | 14 | `0x0144` |

Slots 3, 7, 8 and 9 introduce no new room, which is what makes them useful: a
per-room coordinate assigned on first visit would be unchanged across those four
transitions and changed across the other five. Scanning all of HWRAM for that
signature returns nothing but scratch, which is the negative result the design
rests on.

The companion above-ground walk is **lost**. It was saved into the same ten
emulator slots earlier the same evening and overwritten by this one; its room
ids and cursors survive only in `ZORK1_MAP_RECON.md`. If a future capture needs
to keep two walks, save the first set out of `mcs/` before starting the second.

Read them with `analysis/zork_savestate.py`; see `saves/README.md` for the
byte-swap and the variable layout.
