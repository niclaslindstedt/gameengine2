// The named liquids: the rows.
//
// ge:summary The named liquids: the rows, their swim scales derived once
// ge:keywords liquids rows lava slime gel mucus sludge tar water sea find_liquid
// ge:skill water
// ge:test tests/water_liquids_test.cpp
#include "water/liquids.hpp"

#include <string>
#include <vector>

#include "core/output.hpp"
#include "water/harm.hpp"

namespace ge::water {

namespace {

// A person's 100 health spent at the dose that kills half of those exposed.
constexpr double kPerDose = 100 / kDoseLethal;

Harm heat(double contact) {
  Harm h;
  h.kind = HarmKind::heat;
  h.contact = contact;
  h.per_dose = kPerDose;
  return h;
}

Harm chemical(double feet, double waist, double under, double interval) {
  Harm h;
  h.kind = HarmKind::chemical;
  h.tick[0] = feet;
  h.tick[1] = waist;
  h.tick[2] = under;
  h.interval = interval;
  return h;
}

Stuff row(std::string look, double density, double temperature, double viscosity, double yield,
          double tension, Harm harm, bool derived = true) {
  Stuff s;
  s.look = std::move(look);
  s.density = density;
  s.temperature = temperature;
  s.liquid.viscosity = viscosity;
  s.liquid.yield = yield;
  s.liquid.tension = tension;
  s.liquid.harm = harm;
  if (derived) s.liquid.swim = swim_for(density, viscosity, yield);
  return s;
}

Stuff own(Stuff s, Constituents c) {
  s.water = c;
  return s;
}

std::vector<NamedLiquid> build() {
  std::vector<NamedLiquid> out;
  // Water at 15 degrees: the reference the swim's scales are 1 in.
  out.push_back({"water", "fresh water at 15 C: the reference",
                 row("nordic-clear", 1000, 15, 0.00114, 0, 0.0735, {}, false)});
  out.push_back(
      {"sea", "sea water at 15 C", row("adriatic", 1025, 15, 0.00122, 0, 0.0735, {}, false)});
  // A basalt's pahoehoe at 1150 C: about 400 Pa s measured in the field, a crystal-bearing
  // melt's yield of about 100 Pa, 2600 kg/m^3 with its bubbles.
  out.push_back({"lava", "basalt lava, molten and flowing, 1150 C",
                 row("lava", 2600, 1150, 400, 100, 0.35, heat(500))});
  // The same cooled toward its solidus and crystallising: twenty thousand Pa s and a yield of
  // kilopascals, the crust thick over it.
  out.push_back({"lava-crusted", "basalt lava near its solidus, crusted over, 1100 C",
                 row("lava", 2450, 1100, 20000, 2000, 0.35, heat(400))});
  // The slimes: their viscosity and yield those of the real stuff they look like.
  out.push_back({"slime", "toxic slime: a glowing green ooze, as thin as syrup",
                 row("slime", 1150, 25, 2, 0.2, 0.05, chemical(4, 8, 12, 1))});
  out.push_back({"slime-gel", "a clear jelly ooze: set like a soft gel, digesting slowly",
                 own(row("slime", 1030, 15, 30, 20, 0.05, chemical(2, 4, 6, 1)),
                     {.cdom = 0.4,
                      .chlorophyll = 25,
                      .sediment = 0.6,
                      .bed = {0.1, 0.1, 0.1},
                      .emission = {0.0, 0.01, 0.005}})});
  out.push_back({"slime-mucus", "a pale runny mucus, like egg white",
                 own(row("slime", 1010, 20, 0.05, 0.01, 0.045, {}), {.cdom = 1.5,
                                                                     .chlorophyll = 40,
                                                                     .sediment = 60,
                                                                     .sediment_scatter = 1.0,
                                                                     .bed = {0.1, 0.1, 0.1}})});
  out.push_back(
      {"slime-sludge", "a sewer's sludge: olive-brown, scummed and bubbling",
       own(row("slime", 1250, 20, 5, 3, 0.05, chemical(3, 6, 9, 1)), {.cdom = 12,
                                                                      .chlorophyll = 120,
                                                                      .sediment = 400,
                                                                      .sediment_scatter = 0.3,
                                                                      .bed = {0.08, 0.07, 0.05}})});
  out.push_back(
      {"slime-tar", "warm tar: black, glossy, and it does not let go",
       own(row("slime", 1100, 30, 3000, 50, 0.03, {}),
           {.cdom = 400, .sediment = 3000, .sediment_scatter = 0.02, .bed = {0.03, 0.03, 0.03}})});
  return out;
}

const std::vector<NamedLiquid>& rows() {
  static const std::vector<NamedLiquid> table = build();
  return table;
}

}  // namespace

std::span<const NamedLiquid> liquids() { return rows(); }

const NamedLiquid* find_liquid(std::string_view name) {
  for (const NamedLiquid& l : rows())
    if (l.name == name) return &l;
  return nullptr;
}

Stuff liquid(std::string_view name) {
  if (const NamedLiquid* l = find_liquid(name)) return l->stuff;
  core::output::warn("water: \"" + std::string(name) +
                     "\" is not a named liquid, so it is laid as water. next: name one of "
                     "water::liquids() or state a Stuff of the game's own (skill: water)");
  return find_liquid("water")->stuff;
}

}  // namespace ge::water
