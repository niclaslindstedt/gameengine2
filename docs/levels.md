# Levels

The level formats the engine reads (`docs/goals.md` M32): its own **room format** (`plugins/rooms`),
where a level is a few plain shapes and every room carries the seed its surfaces grow from, and
the **classic BSP families** that existing content comes in (`plugins/brush`, M32b).

## The room format

### Why

A hand-built level stores every triangle; a generated one stores nothing and cannot be authored.
The room format sits between: what a designer (or an agent) decides is stored, small and exact
(where the rooms are, how high, where the doors are, what each surface is made of), and what
nobody needs to decide is grown at load from a seed stored with the room (the stones of a wall,
the rock of a cave). A brick wall is stored and drawn as the flat wall it is, since its bricks are
the texture's; a fieldstone wall or a cave is the same few numbers plus a seed, and grows into
tens of thousands of triangles in about a millisecond, the same bits on every machine.

The sample level (`plugins/rooms/levels/halls.gelv.txt`) is ten rooms in about 1.7 KB on disk that
grow into about 360,000 triangles in some 45 ms on one thread (the `rooms` lab measures both).

### The model

Stated once in `plugins/rooms/level.hpp`:

- **Units**: a stored coordinate is a whole number of 1/128 m. A room's origin is absolute (i32);
  everything in it is relative to it (i16, up to 256 m from the origin). x/z is the plan, y is up.
- **Room**: a simple polygon of corners, counter-clockwise seen from above (+x right, +z up the
  page), between a floor height and a ceiling height; a material for the floor, the ceiling and
  each wall; a seed; and a shape: `prism` (straight walls, flat floor and ceiling, each surface
  grown with its material's relief) or `cave` (the prism hollowed into rock by a seeded field).
- **Material**: a name, the texture reference the texture generator reads
  (`<name> [seed=<u32>] [<param>=<n>...]`, the room's seed mixed in when it bakes), a fallback
  colour for pictures drawn without textures, and a relief: its kind, its depth (cm) and its
  largest feature (dm).
- **Portal**: an opening in a wall (along it, its width, its sill and head above the floor), into
  another room or onto nothing (a window). The wall it pierces on the far side is found, never
  stored, so the two sides cannot disagree. A `trim` material puts a frame round it; an opening
  into a cave must have one, since it covers the rock's cell-sized edge.
- **Solid**: a box standing in a room, one material (pillars, steps, crates).

`validate` names every problem in the diagnostics' shape (what, where, what to write instead):
limits, indices, a polygon that is clockwise or crosses itself, an opening that does not fit its
wall or room, a far side that is not a wall of the room it names lying on the same line and facing
back, a cave's mouth with no frame, a box that is inside out.

### On disk and as text

`.gelv` (`plugins/rooms/codec.hpp` states every byte): a 16-byte header, the materials, rooms,
portals and solids as packed records with inline strings, and an FNV-1a trailer. A six-walled room
is about 60 bytes. `decode` refuses a wrong magic or version, a short file, trailing bytes, a bad
checksum and an unsound level, each with its byte offset; `encode(decode(b)) == b`.

`.gelv.txt` (`plugins/rooms/text.hpp`) is the same model a line at a time in metres, the form an
agent writes and reads; `parse(print(level))` is the level again and `print` is canonical:

```
material fieldstone rgb=132,124,110 relief=stones,8,4 tex fieldstone moss=0.3
room cellar at=0,0,0 seed=10 floor=0:cobbles ceiling=2.8:fieldstone
corner 0 -14 fieldstone
corner 8 -14 fieldstone
corner 8 -8 fieldstone
corner 0 -8 fieldstone
portal workshop 0 cellar at=1 width=1.6 sill=0 head=2.2 trim=timber
solid crypt flags -12 0 -7.2 -9 0.34 -6
```

### What grows

`plugins/rooms/build.hpp` states the builder; the short of it:

| Surface | How it grows |
| --- | --- |
| A flat wall | As few quads as its openings allow |
| A wall with a relief | A grid fine enough for the relief's smallest feature (at most 6,000 cells a surface, never under 5 cm), every vertex pushed into the room by the relief's height times its depth, faded to nothing at the wall's edges and round every opening, so walls, floors and neighbours still meet on straight lines |
| A floor or ceiling with a relief | The same grid over the footprint, laid over the flat cap so its edge never opens a hole |
| A cave | A field over a grid at the room's cell: the prism's distance with rounded edges, plus seeded noise of the cave's depth (eased to a third on the floor so it stays walkable, faded round a mouth), tunnels cut at the openings; its surface by surface nets, its normals by the field's gradient, the cap across each mouth cut away. The rock grows only inward, so a cave never leaves its footprint |
| A frame | Jambs, a head and (above the floor) a sill round a trimmed opening, half its depth into each room |
| A solid | A box, faces out, its foot left off on a prism's floor |

Every triangle faces into its room, so a view from outside with back faces culled is a cutaway.

The reliefs (`plugins/rooms/relief.hpp`):

| Relief | What grows | Its scale is |
| --- | --- | --- |
| `rock` | rough stone, value noise in five octaves, laid out in the world so it runs on across a corner | the largest bump |
| `crag` | broken cliff: ridged octaves, sharp crests and creases (also a cave's grain when its wall is crag) | the largest crest |
| `stones` | a fieldstone or cobbled surface: rounded stones of uneven size in mortar | a stone |
| `ashlar` | dressed blocks in running bond, bevelled, each its own proud | a block's height |
| `tiles` | square tiles in grout, each leaning a little | a tile |
| `planks` | boards with grooves, staggered ends, a little grain | a board's width |
| `corrugated` | sheet metal pressed into arches | the pitch |
| `dents` | hammered or battered plate, dimples round scattered points | the spacing |

Detail finer than the grid is the texture's (the baker's normal map), not the geometry's.

### Determinism and speed

Growing is pure: float arithmetic with exact operations only (no transcendental, no fused
multiply-add; the build's FP flags), noise from an integer lattice hash (`noise3.hpp`), every
room independent, so `build_level` runs rooms side by side on the worker pool and gives the same
bits for any number of workers (`tests/rooms_build_test.cpp` holds each room's digest golden).
A cave evaluates its noise only where the rock can reach the surface.

### In a world: what a body meets

`plugins/rooms/clip.hpp` (M32c) hands the mover (engine/move) the level as a mesh world
(`move/mesh_world.hpp`), so a character walks the level the renderer draws with no collision of
the level's own. A **prism is solid where it is stored**: its flat floor, ceiling and walls with
the openings cut out, the frames round them and its solids, built with every relief left off, so
a rock floor is walked as the flat floor it grows from and a body never catches on a relief a few
centimetres deep. A **cave is solid where it is drawn**: its rock is its shape, so the body meets
the grown surface itself. The sample level's world is about 37,000 triangles, built in some 40 ms.

`plugins/rooms/walk.hpp` is the proof and the lab's picture: a **tour** of every room linked to
the start room, depth first by its doorways, into each room's middle and out through each
doorway and back, routed inside a room on a quarter-metre grid by the grid A* (`ai/path.hpp`)
where every cell and every step between two is decided by the mover's own sweeps (a probe dropped
onto walkable ground, the standing hull fitting, a step no higher than the step height and no
deeper than a stair), then string-pulled; and the **walk**, the walker preset driven down it,
view toward the next point and stick forward. On the sample level the tour is 58 points and the
walker sprints its 147 m in 3,499 steps at under 10 µs a step, through all ten rooms, down the
crypt's steps and over both caves' rock, never under a floor (`tests/rooms_clip_test.cpp` holds
the walk's bits golden; the `rooms` lab draws it).

### Still to come

The rest of M32c: textures from the texture generator (M31) drawn by the GPU over a room's mesh,
and `ge level info/build/shot`. M32d grows more (sloped floors, stairs, arches, scattered rubble,
stalactites, liquids).

## The classic BSP families

`plugins/brush` (M32b) reads maps of the classic BSP families into **one view** (`map.hpp`), so
whatever draws, traces or reasons about a map asks the same questions of any of them:

| Family | Told by | What differs |
| --- | --- | --- |
| 29 | the first word is 29 | fifteen lumps; 16-bit indices; one byte a luxel; miptex with names and sizes; leaf `i > 0` is cluster `i − 1`; three clip hulls |
| BSP2 | the magic `BSP2` | the 29 lumps with node, leaf, face, clipnode, marksurface and edge indices widened to 32 bits and node and leaf bounds as floats (the early `2PSB` form is refused, naming the fix) |
| 38 | `IBSP`, version 38 | nineteen lumps; a texinfo names its texture (its pixels are in other files); RGB luxels; leaves name their cluster and area; the visibility starts with per-cluster offsets |
| 46 | `IBSP`, version 46 | seventeen lumps; vertices carry their own texture and lightmap coordinates; faces are polygons, meshes, curved patches or billboards; shaders by name; 128 × 128 lightmap pages; uncompressed cluster rows |

The view, stated once in `plugins/brush/map.hpp`:

- **Surfaces**: triangles over the map's vertices, counter-clockwise seen from the side they
  face (each family's own winding is checked against its plane or normals and turned when it
  disagrees), with a texture, a lightmap and a kind (planar, patch, mesh, liquid, sky).
- **Coordinates** are the file's (map units, z up); `to_engine` turns them into the engine's frame
  in metres at 1/40 m a unit.
- **Lightmaps**: three bytes a luxel in one buffer; a 29 map's grey luxels are repeated three
  times there, so a `.lit` file (`read_lit`) replaces them in place. Each vertex's `lm` is 0..1
  across its surface's lightmap: a face's own in 29 and 38 (one luxel every 16 texels, its size
  `ceil(max / 16) − floor(min / 16) + 1` each way), a page in 46.
- **The tree**: planes, nodes (`child >= 0` a node, `< 0` the leaf `−(c + 1)`), leaves with their
  contents, cluster and bounds; `leaf_at` walks it. A 46 leaf stores no contents: solid there is a
  leaf with no cluster.
- **Visibility**: one row a cluster, decompressed (a non-zero byte is eight bits; a zero and a
  count stand for that many zero bytes); `cluster_sees` asks it.
- **Entities**: the lump parsed into `{ "key" "value" ... }` blocks.

**Refused, never overrun.** A reader checks every lump against the file and its record size
before it reads it, and every index it keeps against what it points into (planes, nodes, leaves,
edges, vertices, textures, lightmaps, visibility offsets, a patch's grid, a model's surfaces); a
damaged file is refused naming the lump or record and what to do next. A patch's tessellation is
bounded, so a hostile file cannot ask for unbounded memory. `tests/brush_fuzz_test.cpp` mutates
the fixtures thousands of ways on every run and holds every mutant to that.

**Fixtures of our own.** No map anyone else made is in the tree: `write.hpp` compiles a few
axis-aligned rooms and point lights into a map of any family (every room's inside faces less its
doorways, an axis-aligned tree, visibility two doorways deep, a luxel per 16 texels lit by the
room's own lights, and in 46 a curved patch), the same bytes on every platform. The `bsp` lab draws
each (or any map given with `--map`).

Left to M18b: the clip hulls' trace (they are read and checked), the PHS, the BSPX lumps, animated
texture sequences, and `ge bsp`.
