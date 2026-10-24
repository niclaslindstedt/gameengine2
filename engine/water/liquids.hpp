// THE NAMED LIQUIDS (docs/liquids.md § The liquids): water, lava and slime as rows a game lays by
// name and tunes field by field, each a whole `Stuff` (world.hpp): its look under the surface,
// its density and temperature, and its liquid (liquid.hpp: the viscosity and yield, the swim's
// motion, the harm). The drawn look of the lava and the slimes is the renderer's row of the same
// name (render/liquid.hpp).
//
//   water::Stuff lava = water::liquid("lava");
//   lava.liquid.harm.per_dose *= 0.5;              // a tougher player
//   water::add_volume(world, hulls, lava);
//
// The rows' numbers are measured where a measurement exists (a basalt's viscosity at its
// temperature, a mucus's, a bitumen's) and named for the look where the liquid is invented (a
// toxic slime): docs/liquids.md gives each its source. The harm's defaults are the engine's
// mid-way between real and playable; a game states its own (Shooter's Edge's classic ticks are
// plugins/shooter's).
//
// | Row            | Density | Viscosity Pa s | Yield Pa | Temperature C | Harm | | water | 1000 |
// 0.00114 (thin) | 0        | 15            | none                     | | sea            | 1025 |
// 0.00122        | 0        | 15            | none                     | | lava           | 2600 |
// 400            | 100      | 1150          | heat: a burn in a second | | lava-crusted   | 2450 |
// 20000          | 2000     | 1100          | heat                     | | slime          | 1150 |
// 2              | 0.2      | 25            | chemical ticks           | | slime-gel      | 1030 |
// 30             | 20       | 15            | chemical ticks, slow     | | slime-mucus    | 1010 |
// 0.5            | 0.05     | 20            | none                     | | slime-sludge   | 1250 |
// 5              | 3        | 20            | chemical ticks           | | slime-tar      | 1100 |
// 3000           | 50       | 30            | none: it holds           |
//
// ge:summary The named liquids: water, sea, lava, slimes; each a whole Stuff a game tunes
// ge:keywords liquids liquid lava slime toxic gel mucus sludge tar water sea catalog preset
// ge:keywords find_liquid liquid named-liquid tweak
// ge:skill water
// ge:test tests/water_liquids_test.cpp
#pragma once

#include <span>
#include <string_view>

#include "water/world.hpp"

namespace ge::water {

struct NamedLiquid {
  std::string_view name;
  std::string_view what;  // one line: what it is
  Stuff stuff;
};

std::span<const NamedLiquid> liquids();
// nullptr for a name not among them.
const NamedLiquid* find_liquid(std::string_view name);
// The row's stuff; an unknown name warns and gives water's.
Stuff liquid(std::string_view name);

}  // namespace ge::water
