---
name: textures
description: "Use for engine/texgen in gameengine2 and anything about PROCEDURAL TEXTURES: the .tex recipe format (texture, size, seed, surface, scale, bump, tags, param, layer lines), the layer kinds table (fill, noise, stripes, bricks, tiles, planks, panels, cracks, scratches, dirt, dust, grime, wear, rust, streaks, moss, snow, glow), the tileable noise kinds (value, perlin, cells, edges, voronoi, ridged, turbulence, white, warp), texture references a level stores (`brick_wall seed=12 wear=0.8`), the bake to albedo, normal, ORM, glow and height maps, the on-disk cache and its key, the specimen library, the CPU lit preview, ge texture (kinds, check, bake) and the texgen-specimens, texgen-layers, texgen-variants, texgen-noise and texgen-cost labs; the M31 rows."
---

# Procedural textures

`engine/texgen` turns a RECIPE (a few lines of text an agent writes) into the maps a renderer
samples. `docs/textures.md` is the design and the format's guide; this skill is the procedure.
Read the lessons first: `make lessons ARGS=textures`. Load `write-code` beside it, and `labs`
before touching a lab.

## Where things are

| What | Where |
| --- | --- |
| The format, its parser, texture references, params, the layer seeds | `engine/texgen/recipe.hpp` |
| Every layer kind and key, with defaults and ranges: THE table | `engine/texgen/layers.cpp` (`ge texture kinds` prints it) |
| The painters | `layers.cpp` (base), `patterns.cpp`, `life.cpp` |
| The surface planes and the height readings (blur, relief, up-facing) | `surface.hpp` |
| Tileable noise | `noise.hpp` |
| The bake, the derived maps, the key, the disk cache, the canonical text | `bake.hpp` |
| The CPU light for pictures | `preview.hpp` |
| The specimen recipes | `engine/texgen/library/*.tex` |

## Rules

- **Everything tiles.** A periodic coordinate is a whole number of cells across the tile, a
  stretch is a whole number, a drawn mark (a scratch, a streak) wraps its texels. The seam score
  in `texgen-specimens` and `every_library_recipe_tiles` hold it.
- **A bake is a pure function of (recipe, reference).** Draw from the layer's `Args::seed` and
  `hash_u32`/`hash01` only, never a clock, `std::rand` or a shared stream; a painter writes only
  its own texel inside `for_texels`, or works serially. No `<cmath>` transcendental: `core::libm`
  (`make libm-check` scans the directory).
- **A key is added in the table, not in a painter.** A new key gets its type, default, range
  and help in its kind's `KeyDef` array; the parser, `ge texture kinds`, the canonical text and
  the docs follow from it. A painter reads it by name (`a.n("amount")`).
- **Life layers read the structure.** A new aging effect takes its placement from the height
  (`relief`: pits positive; `up_facing`: ledges) and a noise breakup, and its strength from an
  `amount` 0..1 a level can drive through a param.
- **Colours are sRGB as written**, blended in that space; the glow is sRGB times brightness.

## Change a look

1. `make lab LAB=texgen-specimens ARGS="--only=<names> --tag=before"` (and `texgen-layers` for
   the recipe whose layer you touch).
2. Change one thing: a painter, a default, or a library recipe.
3. Rerun with `--compare=before`; look at LIT, LIT 2X2 and the layer strip. A painter change
   moves every library recipe that uses the kind: look at each.
4. `GE_GOLDEN_WRITE=1 build/debug/tests/texgen_bake_test` (and `texgen_noise_test` for a noise
   change), `make ge ARGS="digest texgen-specimens --write"` for each lab whose digest moved, and
   say in the PR which textures moved and why, with the before-and-after sheets.

## Add a layer kind

1. Its `KeyDef` array and its row in `kKindsTable` (`layers.cpp`): name, group, help, keys,
   painter. Keep `amount` as the strength key wherever it fits.
2. The painter in `patterns.cpp` (structure) or `life.cpp` (aging), declared in `layers.hpp`.
3. Use it in a library recipe so the labs and the golden show it; add the recipe if none fits.
4. `ge texture kinds` shows it; update `docs/textures.md`'s kinds summary if a group changed.

## Measure

`make lab LAB=texgen-cost PRESET=release` times every library recipe at 256, 512 and 1024 px.
A speed claim carries its before and after (the `perf` skill).
