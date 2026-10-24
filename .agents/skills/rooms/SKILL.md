---
name: rooms
description: "Use for plugins/rooms in gameengine2: the engine's own level format. Rooms, portals, solids and materials (a texture reference and a relief), the .gelv bytes and .gelv.txt text form, growing rooms from their seeds (reliefs, caves by surface nets), and the rooms lab."
---

# The rooms plugin

`plugins/rooms/` (`ge_rooms`, namespace `ge::rooms`) is the engine's own level format: a level is
a few plain shapes and every room carries the seed its surfaces grow from. It depends on the
engine's core, on `engine/move` and `engine/ai` for a body walking it, and on the `brush` plugin
for the classic maps it clones. The design is [`docs/levels.md`](../../../docs/levels.md); each file's header
states its part once.

| File | What |
| --- | --- |
| `level.hpp/.cpp` | The model (units, rooms, materials, portals, solids; brushes, faces, projections, groups, entities), `validate`, `link` |
| `brushes.cpp` | The brush half's lookups (`group_index`, `projection_index`, `Entity::get`) and `validate_brushes` |
| `brush_build.hpp/.cpp` | Brushes built into the faces that can be seen (CSG): `brush_polygons`, `build_brushes` |
| `codec.hpp/.cpp` | The `.gelv` bytes: `encode`, `decode` with its refusals |
| `text.hpp/.cpp` | The `.gelv.txt` text form: `parse`, `print` |
| `relief.hpp/.cpp` | The reliefs a surface grows: heights and grid sizes |
| `noise3.hpp` | Seeded 3D value noise and fbm on an integer hash |
| `build.hpp/.cpp`, `build_parts.hpp`, `cave.cpp` | Growing a room: walls, caps, frames, solids; caves |
| `clip.hpp/.cpp` | The level as the mover's world: flat prisms, grown caves (`clip_world`), `room_at` |
| `walk.hpp/.cpp` | The scripted walk: a tour of every room by its doorways, routed by sweeps; the drive |
| `levels/halls.gelv.txt` | The sample level: every relief, two caves |
| `levels/vault.gelv.txt` | The brush sample: slabs, a pillar, a pool, a door group and its entity, clip, sky, a grass surface |
| `clone.hpp/.cpp` | A classic map cloned into a room level: the texture map, tree cells to brushes, entities |
| `labs/rooms_lab.cpp` | The `rooms` lab |
| `labs/clone_lab.cpp` | The `clone` lab: each map beside its clone, a body at every start |

## Writing a level

Write the text form (`text.hpp` has the syntax) and check it with the lab:
`make lab LAB=rooms ARGS=--level=<file>`; a parse error names the line and what to write instead.
Corners run counter-clockwise seen from above (+x right, +z up the page). Two rooms that share a
doorway share a wall line, facing back; the portal is written once, in either room. An opening
into a cave needs `trim=`.

## Changing what grows

1. Run the lab first: `make lab LAB=rooms ARGS=--tag=before` (release build: `PRESET=release`).
2. Change the builder or a relief; a new relief is a row in `Relief` (`level.hpp`), its name and
   height and grid in `relief.cpp`, a row in `docs/levels.md`, and a material in the sample level.
3. `make lab LAB=rooms ARGS=--compare=before`; look at `reliefs.png`, `cutaway.png`, `inside.png`.
4. Rewrite the golden: `GE_GOLDEN_WRITE=1 build/<preset>/tests/rooms_build_test`, then
   `make labs-digests`, and say in the PR why the bits moved.

## Changing what a body meets

The clip world and the walk are held by `tests/rooms_clip_test.cpp` (the walk's bits golden) and
pictured by the lab's `walk` and `walk-eyes` shots. A tour that stops short names the room and the
two points it found no way between; a walk that stalls names where it stuck. Drive the body into
the spot step by step before changing anything: a stall at a low sill or riser is usually the
world's skin against the step-up's creep (the `move` skill's lesson). A builder change that moves
a prism's flat hull or a cave's rock moves the walk's golden too: rewrite it with
`GE_GOLDEN_WRITE=1 build/<preset>/tests/rooms_clip_test` and say why.

## Rules

- **Grown, never stored.** Nothing a seed grows goes in the file; a format change bumps
  `kFormatVersion` and keeps the reader of the old one.
- **Pure and exact.** No transcendental, no `Rng`, no state between rooms: rooms build side by side
  on the pool and give the same bits (`tests/rooms_build_test.cpp`).
- **Faces into the room**, every triangle, with its vertex normals agreeing (a prism's always; a
  crag cave's slivers at most a few in a hundred).
- **Budgets**: a relief grows at most `kMaxReliefCells` cells a surface, never finer than
  `kMinReliefCell`; finer detail is the texture's.
