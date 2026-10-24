# Grass

Grass the engine grows blade by blade (`docs/goals.md` M34): short and long, thick and thin, on
a heightfield's ground or on any mesh's floors, swaying in the engine's wind (`engine/wind`,
`docs/water.md` § Wind). Any genre uses it: a summer meadow under a mountain, a lawn in a
level's courtyard, reeds round a lake, a field a player hides in.

| Part | Home | Lab | Test |
| --- | --- | --- | --- |
| Kinds and recipes | `engine/grass/kind.hpp` | `grass` | `tests/grass_kind_test.cpp` |
| Covers and the blade scatter | `engine/grass/cover.hpp` | `grass` | `tests/grass_cover_test.cpp` (golden) |
| A blade in the wind | `engine/grass/sway.hpp` | `grass` | `tests/grass_sway_test.cpp` (golden) |
| The grass drawn | `engine/render/grass.hpp`, `grass.wgsl`, `grass_scatter.wgsl`, `wgsl/grass_common.wgsl` | `gpu-grass` | `tests/render_grass_test.cpp` |

## Research: how real-time grass is drawn

What the current practice is, and what this engine takes from it.

- **Texture cards** (a quad with a picture of a tuft, alpha-tested) are the old answer: cheap,
  but they read as flat sheets up close, sway as rigid sheets, and alpha testing aliases and
  overdraws. Not taken except as the far ground's colour.
- **Geometry blades** are what current open-world games draw near the camera: every blade its
  own few triangles, generated on the GPU so nothing per blade is stored. Two published designs
  shaped this one:
  - A per-blade physical model (Jahrmann and Wimmer, "Responsive Real-Time Grass Rendering for
    General 3D Scenes", I3D 2017): a blade is a quadratic Bézier from its root through a guide
    point to its tip; forces (gravity, wind, a recovery toward rest, collisions) move the tip;
    the guide is placed from the tip's height, and then both are scaled so the curve's length
    stays the blade's length. Blades may stand on any mesh, not only a heightfield. Culling by
    orientation, frustum, distance and occlusion keeps only what matters.
  - GPU-generated fields with clumps (a 2021 talk on procedural grass in a large open-world
    game): blades generated per tile every frame by a compute pass, each with its own look
    hashed from its position; blades grouped into clumps (a Voronoi of jittered points) that
    share height and lean so the field reads as grass and not fur; a blade seen edge-on widened
    toward the camera so it never thins to a thread; a low-detail blade at distance; wind as a
    noise blown across the field.
- **Wind** in shipping games is usually a scrolling noise. Here it is the engine's own wind, so
  a rotor's downwash or a blast flattens the grass where the simulation says it blows.

## Kinds

A kind (`grass::Kind`) is a handful of numbers: the blade's height and how much it varies, its
width at the root, the blades a square metre (`density`), the rest lean, the clumps (`clump`:
their size; `pull`: how far a blade leans toward its clump's middle), the wind's `give` (the
speed at the blade's height that bends it halfway) and its own `sway` frequency, and the colours
(root, tip, a `dry` share turned to `dry_tip`). Tall or short is `height`, thick or thin is
`density` and `width`.

| Preset | Height | Width | Blades/m² | Give | Sway | What it is |
| --- | --- | --- | --- | --- | --- | --- |
| `lawn` | 0.07 m | 3 mm | 900 | 11 m/s | 2.8 Hz | short, dense, stiff |
| `meadow` | 0.35 m | 5 mm | 400 | 6 m/s | 1.6 Hz | shin high, some dry |
| `long` | 0.85 m | 7 mm | 220 | 3.5 m/s | 0.9 Hz | knee to thigh high, soft |
| `tussock` | 0.5 m | 4 mm | 450 | 5 m/s | 1.3 Hz | tufts, strongly clumped |
| `steppe` | 0.45 m | 2.5 mm | 300 | 7 m/s | 1.4 Hz | thin, wiry, mostly dry |
| `reeds` | 1.6 m | 12 mm | 90 | 9 m/s | 0.6 Hz | tall, stiff, sparse |

A **recipe** is a preset's name and overrides, the form a level file and the command line
write: `meadow height=0.6 density=300 width=0.004 dry=0.2`. `parse_kind` reads one and names
every fault (an unknown preset or field, a value that is not a number); `print_kind` writes the
canonical recipe back.

## Cover

A cover (`grass::Cover`) is a list of triangles, each with its kind and the share of the kind's
density at each corner. It comes from:

- **a heightfield** (`cover_heightfield`): a region at a cell of its own, each corner's kind and
  share asked of the game (nothing on a path, long grass in a hollow);
- **any mesh** (`add_mesh`): every triangle whose normal is within `acos(min_up)` of vertical,
  either winding. This is the hook for a level's floors: the room format's grass material and a
  classic map's grass-textured faces (`docs/levels.md`).

A triangle whose blades would pass half of `kMaxBladesPerTri` is halved across its longest edge
until they fit, so a large floor is never capped.

**The scatter** is a pure function of the triangle: its seed is its corners at a centimetre,
hashed (so the same ground grows the same blades in any order); it holds `floor(A × D × S)`
blades and one more by its seed's chance; each is a uniform point (the square-root fold of two
hashed numbers), kept by the share at that point; its clump is the nearest jittered centre of
the clump grid, which scales its height and pulls its lean. Each blade has a **rank** in [0, 1),
which is what the picture thins it by: the lowest ranks are kept, so thinning never reshuffles.
No transcendental is called and every float is in the GPU shader's order, so the CPU knows every
blade the GPU draws.

## Sway

A blade's shape is a pure function of its kind, its numbers, the air at its root and the clock
(`blade_shape`, and its WGSL twin):

- **The air**: the engine's wind (`wind::wind_at` at the blades' height, with the field's
  shelter and every local source) sampled on a grid round the eye (`fill_wind_grid`). The gusts
  are **carried downwind**: a node reads the field's air at `t − (its distance along the mean
  bearing) / (the mean speed)`, so a gust crosses a field as a front. The grid is read
  bilinearly and ruffled by a 6 m value noise blown with the wind: the cat's-paws.
- **The bend**: `b = w² / (w² + give²)` (the drag grows with the square of the speed, the stiffness
  pushes back). The rest lean gives way to the wind's bearing as `b` grows, to at most 1.35 rad.
- **The sway**: a sine at the kind's frequency (±15 %, each blade's phase its own), largest in a
  middling wind and least in a calm or a gale that holds the blade flat, with a smaller sway
  across the wind.
- **The curve**: the tip from the bend, the guide straight up from the root to `h × max(1 − sin θ,
  0.05)`, then both scaled so the Bézier's length (between its chord and its guide polygon) is
  the blade's length.

## Drawn

`engine/render/grass.hpp`, two passes:

1. **`grass.scatter`** (compute): one workgroup a cover triangle, culled by the frustum (its
   sphere raised by its kind's tallest blade) and the reach; each thread takes every 64th slot,
   scatters the blade, keeps it by rank against the distance's share (all within `full_m`,
   `(full_m / d)²` past it, none past `reach_m`), culls it by its own sphere, shrinks it to
   nothing near its rank's threshold and near the reach (no popping) and widens the fewer far
   blades. It is appended to the fine list (within `near_m`, 5 segments) or the coarse list (2
   segments), each an indirect draw held to its capacity.
2. **`grass`** (draw, after the ground's colour and before the sky, depth written): one triangle
   strip a blade; each vertex bends its blade in the air at its root and steps out by the width,
   tapering to the tip, never under about 0.7 px, turned toward the eye when edge-on. Lit
   two-sided: the sun past the ground's horizon and the shadow maps on the face toward the eye,
   the sun through the blade from behind (translucency, strongest at the tip), the sky dimmed
   toward the root (a dense field shades its own floor), a faint sheen; hazed.

**Far away** there are no blades: the game paints the ground's albedo under a cover with each
kind's `far_colour` (the blades' mean as seen from afar, darkened by the field's own shade), so
the field thins into a ground of its own colour with no ring.

On the software adapter the `gpu-grass` lab's eight views (about 130,000 blades each at 480 ×
270) take about 11 s together.

## Rows

| Row | What | Status |
| --- | --- | --- |
| M34a | Kinds, covers, the scatter and the sway on the CPU; the `grass` lab | done |
| M34b | The grass drawn: the scatter pass, thinning, the blades, the light; the `gpu-grass` lab | done |
| M34c | The rest of the look: blades pressed down by what walks or drives through them and springing back (a trample map), blades casting into the sun's shadow maps, flowers and seed heads, the sway's own motion vectors, a stop on the picture's quality ladder priced | open |
| M34d | A level's grass: a room format material's `surface <material> grass <recipe>` line and a cloned classic map's grass-textured faces (the cloner's texture map, `*grass*` to a meadow by default) grown into covers by the builders (`plugins/rooms`, `plugins/brush`, with M32) | open |
| M34e | Fall Line's summer grass: the summer ground's meadows, pastures and runs as covers, the rotor's downwash flattening them | open |
