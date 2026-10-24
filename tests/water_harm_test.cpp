// engine/water/harm.hpp: the ticks fall on going in and every interval after, by the immersion
// level, and a dip out and back in cannot reset the clock; a hot liquid's contact heat is dosed
// on the wetted share; the radiant flux over a disc falls with its view factor; the burn's
// grades follow the dose's thresholds; lava kills a body all in within a second, feet first in a
// few; and a run keeps its bits in tests/golden/water_harm.txt.
//
// ge:summary Test: what a liquid does to a body: ticks, dose, radiant flux, burn grades
// ge:keywords test harm tick interval dose radiant-flux burn-grade lava slime golden
// ge:skill water testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "water/harm.hpp"
#include "water/liquids.hpp"
#include "water_golden.hpp"

namespace {

namespace gw = ge::water;
constexpr double kDt = 1.0 / 120;

}  // namespace

TEST(the_ticks_fall_by_level_on_their_clock) {
  gw::Harm h;
  h.tick[0] = 4;
  h.tick[1] = 8;
  h.tick[2] = 12;
  h.interval = 1;
  gw::HarmState s;
  CHECK(gw::step_harm(h, 20, 2, s, kDt) == 8);  // on going in
  double dealt = 0;
  for (int i = 1; i < 120; ++i) dealt += gw::step_harm(h, 20, 2, s, kDt);
  CHECK(dealt == 0);  // nothing more within the second
  dealt = 0;
  for (int i = 0; i < 2; ++i) dealt += gw::step_harm(h, 20, 3, s, kDt);
  CHECK(dealt == 12);  // the next, at the level he is at then
  // Out and straight back in: the clock ran on, nothing early.
  CHECK(gw::step_harm(h, 20, 0, s, kDt) == 0);
  CHECK(gw::step_harm(h, 20, 1, s, kDt) == 0);
}

TEST(the_heat_is_dosed_on_the_wetted_share) {
  const gw::Stuff lava = gw::liquid("lava");
  const gw::Harm& h = lava.liquid.harm;
  // All in: dead (100 of the game's health) within a second.
  gw::HarmState all;
  double damage = 0;
  int steps = 0;
  while (damage < 100 && steps < 1200) {
    damage += gw::step_harm(h, lava.temperature, 3, all, kDt);
    ++steps;
  }
  CHECK(steps * kDt < 1.0);
  CHECK(gw::burn_grade(all.dose) == 5);
  // Feet only: the same, more slowly.
  gw::HarmState feet;
  damage = 0;
  int fsteps = 0;
  while (damage < 100 && fsteps < 12000) {
    damage += gw::step_harm(h, lava.temperature, 1, feet, kDt);
    ++fsteps;
  }
  CHECK(fsteps > 3 * steps);
  CHECK(fsteps * kDt < 10.0);
  // Water deals nothing.
  gw::HarmState w;
  CHECK(gw::step_harm(gw::liquid("water").liquid.harm, 15, 3, w, 1.0) == 0);
}

TEST(the_radiant_flux_falls_with_the_view_factor) {
  // A molten surface radiates about 200 kW/m^2; at its own radius off it, half.
  const double q0 = gw::radiant_flux(1150, 0.9, 10, 0);
  CHECK(q0 > 1.8e5 && q0 < 2.3e5);
  CHECK(std::fabs(gw::radiant_flux(1150, 0.9, 10, 10) - 0.5 * q0) < 1e-6 * q0);
  // A crust at 700 C radiates under a quarter as much.
  CHECK(gw::radiant_flux(700, 0.9, 10, 0) < q0 / 4);
}

TEST(the_burns_follow_the_dose) {
  CHECK(gw::burn_grade(50) == 0);
  CHECK(gw::burn_grade(95) == 1);
  CHECK(gw::burn_grade(200) == 2);
  CHECK(gw::burn_grade(500) == 3);
  CHECK(gw::burn_grade(1500) == 4);
  CHECK(gw::burn_grade(3000) == 5);
  // 10 kW/m^2 for 14 s is about a second-degree burn's dose.
  const double d = gw::dose_rate(1e4) * 14;
  CHECK(gw::burn_grade(d) == 3);
}

TEST(every_number_keeps_its_golden) {
  std::vector<std::vector<uint64_t>> got;
  for (const gw::NamedLiquid& l : gw::liquids()) {
    gw::HarmState s;
    double damage = 0;
    for (int i = 0; i < 240; ++i) {
      const int level = i < 80 ? 1 : (i < 160 ? 2 : 3);
      damage += gw::step_harm(l.stuff.liquid.harm, l.stuff.temperature, level, s, kDt, 2000);
    }
    got.push_back({ge::test::bits(damage), ge::test::bits(s.dose), ge::test::bits(s.flux)});
  }
  ge::test::hold_golden("tests/golden/water_harm.txt",
                        "per liquid, 2 s at levels 1, 2, 3 under 2 kW/m^2: damage dose flux "
                        "(water_harm_test)",
                        got);
}
