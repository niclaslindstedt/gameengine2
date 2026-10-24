---
name: snow
description: "Use when a game has snow on its ground: laying a layer of snow over any ground (ge::snow), pressing footprints, skis, tyres, treads and bodies into it, asking it how deep, dense and packed it is, saving it, and reading a saved layer with `ge snow`."
---

# Snow

`ge::snow` (headers `snow/*.hpp`, namespace `ge::snow`, target `ge::snow`) is a layer of snow
over any ground that remembers what crossed it. Every point is a column with a depth and a
density over the ground; a contact pressed into it compacts the snow with its mass kept and
throws part of it aside as a berm. The physics, the picture and the sound all read the same
columns.

| Header | What | Start with |
| --- | --- | --- |
| `snow/layer.hpp` | A layer: its region, its ground (any height function baked by `bake_ground`), its cover (what lies there untouched), the tiles of what changed, and `sample` | `ge explain snow/layer.hpp` |
| `snow/press.hpp` | `press`: an imprint (capsule, disc, box, ellipse; a load or a sink; how much is thrown aside) and what it left | `ge explain press` |
| `snow/reference.hpp` | A typical ski, boot, tyre, tread and body, with the numbers they are pressed with | `ge explain reference_contact` |
| `snow/contact.hpp` | The contact laws for any machine: sink (Bekker), compaction and bulldozing drag, traction against slip, glide and tyre friction, planing; one row of numbers a kind | `ge explain column_sink` |
| `snow/kinds.hpp` | The named snows (powder, settled, wind-packed, groomed, spring, crust, machine, ice) | `ge explain kind_row` |
| `snow/snapshot.hpp` | `save` and `load`: a layer's tiles as bytes, for a save game or a late joiner | `ge explain save` |

## Use it

- **One layer a patch of ground.** `add_layer(world, spec)` with the region, the ground and the
  cover. An untouched layer costs no memory and `sample` returns the cover exactly, so a game
  can read its physics off a layer before anything is pressed and nothing moves.
- **One `press` a contact a step**, in the game's step order (that order is part of the
  simulation's bits). A moving contact presses a capsule from where it was a step ago, so its
  track is continuous at any speed. Give `sink` when your physics already knows how deep the
  contact stands (a ski planing at speed); give `load` and let the layer pack the snow by the
  pressure when it does not (a boot, a tyre).
- **Read the imprint records** (`layer.records`, cleared with `clear_records` once a frame) for
  marks, spray and sound: they say where each press was and what it threw.
- **Keep the budget in mind**: `LayerSpec::max_tiles` (32 KB a 6.4 m tile at the default 10 cm
  cell). A press past it is refused and counted (`Pressed::refused`, `Layer::refused`).
- **Save it with `snow::save`** and look at the file with `ge snow show <file.snow>` (stats and
  depth, density and state maps) and `ge snow column <file.snow> --at=x,z`.
- Find the rest with `ge find snow <word>`; every header's leading comment is its manual.
