---
name: water
description: "Use for water and wind in gameengine2 or a game on it: engine/wind (the wind as a pure function: the mean, gusts and veer, the log-law profile, shelter, local sources such as a downwash, a whirl or a blast), engine/water as its rows land (bodies of water and the one sample query, waves off the wind and the fetch, floating, swimming and its per-game presets, the colour from what is in the water, flowing water), the water's render passes and sound, and their labs. Load it before adding water or wind to a game, or changing how either behaves."
---

# Water and wind

The design is [`docs/water.md`](../../../docs/water.md) and the roadmap is `docs/goals.md`
§ M24 (one status file a row, `docs/goals/M24<x>.md`). This skill is the procedure; read the
doc's section for the subject before the first edit.

**Read this skill's lessons first**: `make lessons ARGS=water`. Load `determinism` beside it
for anything the simulation reads, `render` for a pass, `audio` for a voice, `labs` for a lab.

## Where it lives

| Subject | Home | Lab | Test |
| --- | --- | --- | --- |
| The wind: the field, gusts, veer, the profile, shelter | `engine/wind/field.hpp` | `wind` | `tests/wind_field_test.cpp` (golden) |
| Local wind sources: jet, whirl, blast | `engine/wind/sources.hpp` | `wind` | `tests/wind_sources_test.cpp` |
| A world's wind as a field (the mountain's) | `plugins/mountain/air/air.hpp` (`field_of`) | `wind`, `snowfall` | `tests/mountain_air_test.cpp` (golden) |
| The sea a wind grows: growth laws, spectrum, depth, spread, dispersion | `engine/water/spectrum.hpp` | `waves` | `tests/water_spectrum_test.cpp` |
| The simulation tier's waves: the components laid, the surface, breaking | `engine/water/waves.hpp` | `waves` | `tests/water_waves_test.cpp` (golden) |
| The fetch over a body's outline | `engine/water/fetch.hpp` | `fetch` | `tests/water_fetch_test.cpp` |
| Bodies (lake, ocean, river, volume) and the one `sample` query | `engine/water/world.hpp` | `fetch` | `tests/water_world_test.cpp` (golden) |
| Floating: probes, buoyancy, drag, slam, added mass, planing, splashes | `engine/water/float.hpp` | `float` | `tests/water_float_test.cpp` (golden) |
| Swimming: immersion, the kinematic and body regimes, clothes, cold, breath, the water jump | `engine/water/swim.hpp` | `swim` | `tests/water_swim_test.cpp` (golden) |
| The dressed skier's swim preset | `plugins/skier/swim.hpp` | `skier-swim` | `tests/skier_swim_test.cpp` |
| The colour from what is in the water: spectral IOPs, the shallows, the veil, visibility, the looks | `engine/water/optics.hpp` | `water-colour` | `tests/water_optics_test.cpp` (golden) |
| Flowing water: the HLL scheme, the quanta, sources, drains, open edges, falls, the tiers | `engine/water/flow.hpp` | `flow`, `water-lod`, `water-cost` | `tests/water_flow_test.cpp` (golden) |
| The surface drawn: rings, the waves kept per vertex and pixel, the mirror, the glitter | `engine/render/water.hpp`, `water.wgsl` | `gpu-water`, `lake` | `tests/render_water_test.cpp` |
| The view under water: the fog, blur, Snell's window, the presets | `engine/render/underwater.hpp`, `underwater.wgsl` | `gpu-underwater`, `lake` | `tests/render_water_test.cpp` |
| Water in Fall Line's frame (`add_water`) | `games/fall_line/app/frame.hpp` | `lake` | `create_frame`'s pass-table check |
| Liquids: viscosity, yield, the wind's keep and drift, the swim they give, ripples (`docs/liquids.md`) | `engine/water/liquid.hpp` | `liquids` | `tests/water_liquid_test.cpp` (golden) |
| Harm: ticks, burns by thermal dose, radiant heat | `engine/water/harm.hpp` | `liquids` | `tests/water_harm_test.cpp` (golden) |
| A black body's glow | `engine/water/glow.hpp` | `gpu-lava` | `tests/water_glow_test.cpp` |
| The named liquids (lava, slime, ...) | `engine/water/liquids.hpp` | `liquids` | `tests/water_liquids_test.cpp` |
| Lava and slime drawn, the looks, the eye's key (`adapt_cd`) | `engine/render/liquid.hpp`, `liquid.wgsl` | `gpu-lava`, `gpu-slime` | `tests/render_liquid_test.cpp` |
| Drawn, sound | the rows M24g–M24l | the doc's § Labs | as each row lands |

## The rules

- **The wind is a pure function.** Everything the air does at a point is computed from a
  `wind::Field` (data, built once), the point and the time: nothing is stepped, nothing draws a
  random number, and only `core::libm` is called. A local source comes from the game's state
  (a rotor's thrust, a blast's moment), so it is as deterministic as the state.
- **`open_at` keeps its order of operations.** The mountain's wind was moved onto it with its
  golden unchanged; a change to the sum, the veer or the fold's place moves
  `tests/golden/mountain_air.txt` and every Fall Line digest that reads the wind. Change it only
  on purpose, rewrite both goldens and say so in the PR.
- **A world's rule stays in the world.** The engine knows no slope: the mountain's "never up
  the hill" is a `Fold` the mountain passes in. Do the same for any world rule (a valley's
  channel, a coast's sea breeze): a field the world fills, never a branch in the engine.
- **The simulation's level of detail is measured from the game's actors, never the camera**
  (docs/water.md § Level of detail): a camera is one machine's, an actor is every machine's.
- **Per-game numbers live in the game or its plugin.** The engine gives a mechanism and a
  default; a preset (the classic swim, the clothed skier) is the game's.
- **Adding water or wind to a game is one call.** If it takes more, the API is what to fix.
- **Everything asks `water::sample`.** The physics, a swimmer, the sound and the renderer read
  the one query (or the GPU's restatement of `surface_at`, held to it by a lab), so they cannot
  disagree about where the surface is. A second walk over the bodies is a bug.
- **The laid band must carry the sea.** Each component carries its own slice's energy, so the
  laid Hs is the asked one less only the band's tails; a change to `lay` keeps the `waves` lab's
  "Hs laid" column within a few per cent of "Hs asked" and its series within 8 %.
- **A body that floats integrates the added mass's change too.** The added mass grows as a body
  wets; taking it into the acceleration without its Lagrangian term (1/2 v d(ma)/dt, as
  `step_floater` does) makes a heaving box gain a few per cent of energy a minute. The `float`
  lab's energy chart is the check.
- **A look is constituents, never colours.** Retune a look by what is in its water (the humus,
  the plankton, the sediment, the bed), and keep its derived visibility inside the field band
  its row carries; the `water-colour` lab fails one that leaves it. A colour picked by hand is
  the renderer's art override, never a look.
- **A swimmer's test or lab skips the plunge.** Dropped in, any swimmer goes under for a
  second and bobs; "when his eyes go under" means after that (the `skier-swim` lab measures
  when they go under for good), or every number reads the plunge.
- **Flowing water is counted in quanta.** A pass may move water only as whole quanta that leave
  one cell and enter another (or the books' `added` and `drained`); a double added to a depth
  anywhere is water made or lost. The `flow` lab's books and `water-lod`'s tier changes fail
  on one quantum missing.
- **A coarse tier keeps the walls.** A thin wall averaged into a coarse cell's bed lets a lake
  through it while no one is near; the coarse faces' crests (`FlowGrid::sx`, `sz`) hold it. A
  change to `coarsen` keeps `water-lod`'s three floods the same water in the same shape.
- **A waterfall's first trickle lands at the foot.** The sheet's reach grows with the
  discharge; check the landing on a sheet worth the name, never on the first quanta over the lip.
- **A plan view drops the components shorter than a few pixels** (the picture's own tier):
  drawn, they alias into stripes that are not on the water.

## Checking a change

```sh
make test T='wind_|mountain_air|water_'     # the wind's and the water's tests, the mountain's golden
make lab LAB=wind ARGS=--tag=before          # before the change
make lab LAB=wind ARGS=--compare=before      # after: the gusts' intensity, the profile, the plan
make lab LAB=waves ARGS=--tag=before         # the same for the waves (and LAB=fetch for a lake)
```

Read the `wind` lab's table: the measured intensity sits within a few per cent of the asked
(1 / ln(10 / z0) for the ground), every profile crosses 1 at 10 m, and the plan's wood and its
lee are calm while the arrows bend at their edges. Read the `waves` lab's tables: the band laid
keeps nearly all of the asked Hs, the series measures it within a few per cent, and the
surfaces show short-crested bands across the wind with no single stripe. The `fetch` lab's lee
must be rougher than its windward shore.
