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
stalactites, liquids). M32e and M32f grow the format for classic maps and clone them into it
(§ Cloning a classic map); M32g runs their doors, buttons, platforms and triggers.

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

**Pictures.** A 29 or BSP2 map can carry its textures' pixels: `Texture::indexed` holds the
full-size picture as palette indices (the palette itself is in the game's files, not the map). The
`bsp` lab draws such a map textured: `labs bsp --pak=<pack> --map=maps/<a>.bsp,maps/<b>.bsp` reads
the maps and `gfx/palette.lmp` out of a pack archive (`engine/vfs/pak.hpp`, the first slice of
M18a), then draws each from its player start and three other starts. Each texel is lit by its
lightmap, so a luxel of 128 shows the picture as painted. The palette's last 32 colours glow
whatever the light, the sky is its two layers along the view, and trigger volumes are left out.
Content read this way stays outside the tree. `--textures-out=<dir>` (a directory outside the
tree) also writes each map's textures there as PNGs, `<dir>/<map>/<name>.png` with `liq_` for a
leading `*` and `anim_` for a leading `+`, and `textures.txt` naming each with its size: what the
procedural texture matcher reads to propose the cloner's texture map (§ Cloning a classic map).

Left to M18b: the clip hulls' trace (they are read and checked), the PHS, the BSPX lumps, animated
texture sequences, and `ge bsp`.

## Cloning a classic map

A classic map's STRUCTURE can be carried into the room format: where its solid space is, its
faces' planes and how each texture is laid on them, its moving parts and its entities. Its
TEXTURES cannot: they are someone else's pictures. The cloner (`rooms/clone.hpp`, M32f) therefore
takes every texture's place from a TEXTURE MAP an agent has approved, and never decides one
itself. The loop has an agent in the middle:

1. `labs bsp --pak=<pack> --map=<map> --textures-out=<dir>` writes the map's textures as PNGs
   to a directory outside the tree, with `textures.txt` naming each.
2. The texture matcher (`ge texture match <dir>`, M31) proposes a procedural recipe for each and
   writes `<dir>/textures.texmap` with its scores and comparison pictures.
3. The agent judges each match, turns the recipe's knobs, re-runs the matcher, and pins the lines
   it approves (`# pin`; the matcher never rewrites a pinned line).
4. `labs clone --pak=<pack> --map=<map> --texmap=<dir>` clones the map with that texture map.

A texture the map does not name stops the clone, naming it, unless placeholders are asked for
(drawn plain, and listed). The only thing read from a texture's pixels is its mean colour, the
flat colour a far face is drawn in. No pixel of a source map is ever written into a level, the
tree or a lab's output that is kept.

The texture map is plain text, one line a texture:

```
texture <name in the map> <recipe reference...>     # brick_wall seed=3 tint=#8a5a40
liquid <name> <water|slime|lava|teleport> [<recipe reference...>]
sky <name> [<recipe reference...>]
surface <name> <surface reference...>               # grass meadow height=0.6
```

A `#` starts a comment only at a line's start or after a space, so a recipe's `tint=#rrggbb`
stays whole; a line of any other kind is the matcher's own and is passed over.

### What the format grows (M32e)

Rooms describe empty space; a classic map describes solid space. The room format therefore
gains a second kind of geometry beside its rooms:

- **Brushes**: a convex solid as a list of planes. Each face carries a material and a texture
  projection (two axes and offsets in the face's plane, so a texture lies exactly as it did).
  Brushes can stand alone or within a room.
- **Contents**: what a brush is made of: solid, water, slime, lava, sky, clip (stops a body,
  never drawn) or trigger (neither drawn nor met: a volume an entity acts on). A liquid brush
  is a volume the water and liquid systems (M24, M30) take over.
- **Brush groups**: brushes that move or act together. A door, a button or a trigger volume is a
  group, named by the entity that owns it.
- **Entities**: a class and its key/value pairs, standing at a point or owning a brush group.
  The format keeps them as data. The classic gameplay classes run on M32g's step.
- **Surface replacements**: a material may name a procedural surface (tall grass, short grass,
  moss) drawn in place of its texture. The grass system fills the name. The cloner offers a
  table from texture names to replacements.

Brushes are built into faces at load: each brush's faces are cut by its own planes, and the
parts that lie inside another solid brush are removed (constructive solid geometry, the same
bits on every machine). The mover's world is built from the same faces.

### The clone (M32f)

- **Solid space** comes from the map's tree: each path to a solid leaf bounds a convex cell
  (its planes, clamped to the model's bounds a unit out), and the cell becomes a brush. Each face
  a drawn surface lies on takes that surface's texture and its projection, recovered from the
  surface's largest triangle; a face no surface lies on (one buried in the map's outside) is
  hidden. Merging cells into fewer, larger brushes is still to come.
- **Brush models** (doors, buttons, platforms, trains) become brush groups (`model_<n>`) the
  same way, each from its own tree; a `trigger_` entity's model becomes TRIGGER volumes, which
  are neither drawn nor met.
- **Liquids and sky**: water, slime and lava leaves become liquid brushes, and sky leaves sky.
- **Entities** come across with their keys. A brush entity's `model` becomes its group,
  `origin` is written in metres in the engine's frame, and `angle` becomes `heading` (degrees
  clockwise from +z). Other keys keep the map's units for now; M32g reads them.
- **Smoothing** (`--smooth=1`): every face is marked smooth, so neighbours less than about 53
  degrees apart are shaded as one curve (a faceted arch looks round) and right angles stay sharp.
  Replacing such a run of faces with curved geometry is still to come.
- **Units**: map units become metres through `brush::to_engine` (z up becomes y up).

The `clone` lab clones each map given (`--pak`, `--map`, `--texmap`) and draws it beside the
original from the same eyes, then stands a body at every start in the clone's mover world. It
reports the brushes, faces, hidden faces, groups, entities, bytes and times. `--write=<dir>`
(outside the tree) keeps the clone as `.gelv` and `.gelv.txt`. Without a pack it clones the
brush plugin's fixture, which is what `make labs-smoke` runs.
