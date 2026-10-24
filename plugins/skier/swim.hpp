// THE DRESSED SKIER IN THE WATER: the engine's swimmer (engine/water/swim.hpp, the body regime)
// filled with what a skier wears. Each garment's numbers are a typical piece's, by its kind:
// - a shell jacket over insulation: about a kilogram of polyester (1300 kg/m^3), soaking three
//   litres over a few minutes, its loft trapping about twelve litres of air that leaks out over
//   two and a half;
// - shell trousers: the same, two litres and six;
// - a pair of ski boots: four kilograms of shell plastic (about 1150 kg/m^3), their liners
//   flooding in half a minute (a litre and a half a boot) with the air in them;
// - a helmet: a shell over foam, 1.5 litres of it for 450 g, so it floats the head;
// - gloves and the base layers: little mass, a little air, slow to soak.
// So a skier who goes through the ice floats on his jacket's air at first, soaks, and sinks slowly
// unless he swims, and swimming is weak in the soaked suit (the drag area more than doubles).
//
// ge:summary The dressed skier as the engine's swimmer: what each piece of his gear does in water
// ge:keywords skier swim dressed clothes jacket boots helmet soak trapped-air sink water preset
// ge:skill skier water
// ge:test tests/skier_swim_test.cpp
#pragma once

#include "water/swim.hpp"

namespace ge::skier {

inline water::SwimSpec dressed_skier() {
  water::SwimSpec s = water::default_swimmer();
  s.mass = 78;
  s.volume = 0.0735;
  s.area = 0.14;
  s.thrust = 85;
  s.gear[0] = {.mass = 1.0,
               .density = 1300,
               .capacity = 3,
               .soak = 1.0 / 120,
               .air = 0.012,
               .escape = 1.0 / 150,
               .drag = 0.05};  // the jacket
  s.gear[1] = {.mass = 0.8,
               .density = 1300,
               .capacity = 2,
               .soak = 1.0 / 100,
               .air = 0.006,
               .escape = 1.0 / 120,
               .drag = 0.04};  // the trousers
  s.gear[2] = {.mass = 4.0,
               .density = 1150,
               .capacity = 3,
               .soak = 1.0 / 30,
               .air = 0.003,
               .escape = 1.0 / 30,
               .drag = 0.03};                  // the boots
  s.gear[3] = {.mass = 0.45, .density = 300};  // the helmet
  s.gear[4] = {.mass = 0.2,
               .density = 1200,
               .capacity = 0.3,
               .soak = 1.0 / 60,
               .air = 0.0005,
               .escape = 1.0 / 60};  // the gloves
  s.gear[5] = {.mass = 0.5,
               .density = 1300,
               .capacity = 1,
               .soak = 1.0 / 200,
               .air = 0.002,
               .escape = 1.0 / 200,
               .drag = 0.01};  // the base layers
  s.garments = 6;
  return s;
}

}  // namespace ge::skier
