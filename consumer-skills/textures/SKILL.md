---
name: textures
description: "Use to add or change one of this game's PROCEDURAL TEXTURES: a .tex recipe (a few text lines: the texture's name, size, seed, surface, scale, tags, the params a level turns, and its layers bottom first), ge texture kinds for every layer kind and key, ge texture check for the recipe's mistakes, ge texture bake for one variant into the cache, a level naming a variant by a texture reference (`brick_wall seed=12 wear=0.8`), and looking at it with the engine's texgen labs."
---

# Procedural textures

A texture here is a RECIPE, not a picture: a short text file the engine bakes into the maps a
renderer samples (albedo, normal, occlusion-roughness-metal, glow, height), deterministically,
from a seed. A level names a VARIANT of it with a texture reference, and the engine bakes each
variant once into a cache directory and loads it from there after that.

## A recipe

```
# textures/brick_wall.tex
texture  brick_wall
size     512                 # a power of two
seed     7
surface  stone               # what it is made of: footsteps, decals, impacts read it
scale    2                   # metres one tile covers
tags     wall brick exterior
param    wear 0.3 0 1        # a knob a level turns: name, default, range
layer    bricks rows=10 cols=4 color=#8c4a32 vary=0.18 mortar_color=#a49c8e
layer    noise kind=perlin period=24 octaves=5 amount=0.35 blend=overlay height=0.06
layer    grime amount=0.55
layer    wear amount=$wear
```

- Layers paint bottom first. Start with a BASE (`fill`, `noise`, `stripes`) or a PATTERN
  (`bricks`, `tiles`, `planks`, `panels`), which cut joints and edges into the height; then the
  LIFE layers (`cracks`, `scratches`, `dirt`, `dust`, `grime`, `wear`, `rust`, `streaks`, `moss`,
  `snow`, `glow`), which read that height: grime pools in the joints, wear rubs the edges, dust
  and snow lie on the ledges, a `glow pattern=cavity` lights the low places.
- `ge texture kinds` prints every kind and key with its default and range. Never guess a key.
- Every period and stretch is a whole number, so every texture tiles.
- Two layers with the same `salt=` share one pattern (a glow in the cracks a cracks layer cut).
- A value may name a param: `amount=$wear` or `amount=$wear*0.5`.

## The loop

```sh
ge texture check textures/*.tex                       # every mistake, at its line
ge texture bake textures/brick_wall.tex --ref="seed=12 wear=0.8"   # into build/textures/
make lab LAB=texgen-specimens ARGS="--library=textures"   # lit, tiled, every map, a seam score
make lab LAB=texgen-layers ARGS="--library=textures --recipes=brick_wall"   # layer by layer
make lab LAB=texgen-variants ARGS="--library=textures --recipes=brick_wall" # seeds and params
```

Look at the pictures before calling a texture done: does it read as its material lit, is there a
line through the middle of LIT 2X2 (a seam), does each layer in the layer strip add what its
name says. Change one layer at a time and rerun.

## In a level

A level stores, per surface or room, a texture reference: `<name> [seed=<n>] [<param>=<v> ...]`.
The room's seed gives each room its own stable variant of the same recipe; the params age it.
`texgen::bake_to_disk` writes `<cache>/<name>-<key>_<map>.png` and a `.meta`, where the key
hashes everything the bytes depend on: the first load bakes, every later one reads the files.
