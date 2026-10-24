---
name: grass
description: "Use for grass in gameengine2 or a game on it: engine/grass (kinds and recipes, covers from a heightfield or a mesh's floors, the blade scatter, a blade's sway in the engine's wind) and engine/render/grass (the GPU scatter, thinning, the blades drawn), and the grass and gpu-grass labs. Load it before adding grass to a game or a level, or changing how grass grows, moves or looks."
---

# Grass

The design and the research are [`docs/grass.md`](../../../docs/grass.md); the roadmap is
`docs/goals.md` § M34 (one status file a row, `docs/goals/M34<x>.md`). Read the doc's section
for the subject before the first edit.

**Read this skill's lessons first**: `make lessons ARGS=grass`. Load `render` beside it for a
pass, `labs` for a lab, and `water` for anything about the wind itself.

## Where it lives

| Subject | Home | Lab | Test |
| --- | --- | --- | --- |
| Kinds, presets, recipes, the far colour | `engine/grass/kind.hpp` | `grass` | `tests/grass_kind_test.cpp` |
| Covers (heightfield, mesh), the scatter, the hash | `engine/grass/cover.hpp` | `grass` | `tests/grass_cover_test.cpp` (golden) |
| The wind grid, the bend, the sway, the Bézier | `engine/grass/sway.hpp` | `grass` | `tests/grass_sway_test.cpp` (golden) |
| The GPU scatter, the lists, the draw, the light | `engine/render/grass.hpp`, `grass_scatter.wgsl`, `grass.wgsl`, `wgsl/grass_common.wgsl` | `gpu-grass` | `tests/render_grass_test.cpp` |

## Adding grass to a game

1. Pick or write the kinds: a preset, or a recipe (`meadow height=0.6 density=300`), parsed
   with `grass::parse_kind`. A level file stores the recipe, never the numbers.
2. Build a `grass::Cover`: `cover_heightfield` over the region round the play area (a cell of
   about 2 m, the share 0 where nothing grows), or `add_mesh` with a level's floors. Put the
   kinds in the cover BEFORE adding a mesh (the split reads their density).
3. Paint the ground's albedo under the cover with `far_colour(kind)`, so the blades thin into a
   ground of their own colour.
4. In the frame: `create_grass` at load (its pipelines in the manifest), `set_grass_cover`
   whenever the cover changes, `prepare_grass` with the frame's view and the game's
   `grass::GrassAir` (its `wind::Field`, shelter, sources and ground), then
   `encode_grass_scatter` after the frame's uniforms and `encode_grass` after the ground's colour
   and before the sky. Name both passes in the game's pass table.

## The rules

- **Two halves, one function.** `scatter_one`, `wind_at_blade` and `blade_shape` exist twice: in
  C++ (`engine/grass`) and in WGSL (`wgsl/grass_common.wgsl`), float for float in the same
  order. Change one, change the other in the same commit; `tests/render_grass_test.cpp` holds
  them together (blades, air and shapes) on the software adapter.
- **A blade is a pure function of its triangle.** Its seed is the triangle's corners hashed, so
  nothing about a blade is stored or stepped. Never key a blade on a triangle's index or on the
  camera.
- **The wind is the engine's.** The grass reads `wind::wind_at` through `fill_wind_grid`; a
  game's gusts, shelter and local sources (a downwash, a blast) reach the blades by being in
  its `GrassAir`, never by a grass-only wind.
- **Thin by rank, fade, never pop.** The picture keeps the lowest ranks and shrinks a blade near
  its threshold; a change to the distance's share keeps that.
- **Simulation code.** `engine/grass` is in `make libm-check`'s list: `core::libm` for every
  transcendental, since its blades feed goldens and lab digests.

## Before and after

A change to how grass grows or moves runs `make lab LAB=grass` and `make lab LAB=gpu-grass`
with `--tag=before` and then `--compare=before`, and puts both sheets in the PR. A moved golden
(`tests/golden/grass_*.txt`) is rewritten with `GE_GOLDEN_WRITE=1` only for a deliberate change.
