---
name: snow
description: "Use for engine/snow in gameengine2: a layer of snow over any ground that remembers what crossed it. The column model (depth and mass over a ground), the cover, the sparse fixed-point tiles, sample, press and the imprint record, the reference contacts, the snapshot and `ge snow`; the snow-press and snow-field labs; and the M29 plan in docs/snow.md (contact laws, time, tiers, drawing, sound, Fall Line's move onto it)."
---

# Snow

`engine/snow/` (`ge_snow`, namespace `ge::snow`) is snow as an engine feature: any game lays a
layer over any ground and presses whatever crosses it into the snow. `docs/snow.md` is the design
and the plan (rows M29a–M29l); this skill is how to work on it.

| File | What |
| --- | --- |
| `snow/kinds.hpp` | The named snows, their loose, packed and cap densities, the packed share |
| `snow/layer.hpp/.cpp` | The layer: region, ground, cover, tiles of fixed-point cells, `sample`, `cell_for`, `mass_units` |
| `snow/contact.hpp/.cpp` | The contact laws: a `ContactRow` a kind, Bekker's sink and its cap (`column_sink`), compaction and bulldozing, Mohr–Coulomb and Janosi traction, glide and tyre friction, planing |
| `snow/press.hpp/.cpp` | `press`: targets from a sink or a pressure, mass kept, the berm, the imprint record |
| `snow/reference.hpp` | The reference contacts (a carving and a skidding ski, a boot, a tyre, a tread, a body), `place` |
| `snow/snapshot.hpp/.cpp` | `save`, `load`, `read_info`, `load_bare` (the format in the header) |
| `engine/cli/snow.hpp/.cpp` | `ge snow show` and `ge snow column` over a saved layer |

## Rules

- **Mass to the unit.** A cell keeps its mass in units of 0.05 kg/m2 and every press moves whole
  units: what the tiles hold plus `spilled` is always `mass_in`. `snow-press` and
  `tests/snow_press_test.cpp` hold it; a change that breaks it is a bug, never a tolerance.
- **An untouched layer is its cover, bit for bit.** `sample` reads the cover itself where no
  sample round the point was pressed. That is what lets a game (Fall Line, M29j) move its
  physics onto a layer without moving a digest. Never return a quantized cell for an untouched
  sample.
- **Fixed point in the tiles, doubles between.** Cells are integers (the same bits everywhere,
  uploaded to the GPU as they are); the arithmetic is doubles with `core/libm` only
  (`make libm-check` scans `engine/snow`).
- **The game's order is the snow's order.** Presses change state; a game presses in its step
  order. Nothing in `engine/snow` reads a clock, a camera or `Math`-style randomness.
- **The deeper of two passes.** Targets are measured from the untouched cover, so pressing the
  same imprint twice changes nothing; keep it so (the stationary contact must not dig forever).
- **A narrow outline reaches a sample.** Outlines are read at least three quarters of a sample
  wide and berms reach a sample and a half out; the true width is the picture's (M29e's marks).
- **Golden**: `tests/golden/snow_press.txt` (`GE_GOLDEN_WRITE=1 build/<preset>/tests/snow_press_test`
  rewrites it). A move is a behaviour change: say why in the PR with the `snow-press` table
  before and after.
- **Labs**: `snow-press` (every contact on three snows: cross-sections, plans, the table),
  `snow-contact` (the laws charted for every kind: sink, traction, glide, drag) and
  `snow-field` (a patch crossed by everything: relief, density, tiles, the press's cost). Run
  them with `--tag=before` and `--compare=before` around any change here.
- **A row is a fit, not a fact.** Settled snow's contact row is the published set; the rest are
  fitted to one stated sink each (its comment). Moving a row moves `tests/golden/snow_contact.txt`
  and the press golden: say what it was fitted to and put the `snow-contact` table before and
  after in the PR.
- **What is next** is `docs/snow.md`'s rows: M29c adds time, M29d folds stale tiles into a
  coarse layer under a budget.
