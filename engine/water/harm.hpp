// WHAT A LIQUID DOES TO A BODY IN IT (docs/liquids.md § Harm): the game's damage on a clock by
// how deep the body is in, and the heat a hot liquid (or the air over it) puts into skin, as a
// thermal dose. A liquid's `Harm` (liquid.hpp) holds the knobs; the game's armour, its resistance
// and what its health is are the game's: `step_harm` hands back the damage this step.
//
// - THE TICKS: `tick[level - 1]` at immersion level 1, 2 and 3 (swim.hpp's `immersion`), dealt
//   the step a body goes in and every `interval` s while it stays in; the clock runs on out of
//   the liquid, so stepping out and back in deals nothing early.
// - THE HEAT: a body in a hot liquid takes `contact (T - 35)` W/m^2 on its immersed share (0.15
//   of the skin at level 1, the feet and shins; 0.5 at 2; all of it at 3), plus any flux the game
//   hands in from outside on all of it (`radiant_flux` over lava). The skin's burn follows the
//   THERMAL DOSE, D = sum of q^(4/3) dt with q in kW/m^2, the measure the burn thresholds are
//   stated in: pain at about 92, a first-degree burn at 105, second at 290, third at 1000, and
//   half of those exposed whole-body killed at 2376 ((kW/m^2)^(4/3) s, the last from the
//   probit Y = -14.9 + 2.56 ln(t q^(4/3) / 10^4), q in W/m^2). `per_dose` turns it into the game's
//   health. A dose is counted on the share it reaches, so feet in lava burn at 0.15 of the rate.
// - THE RADIANT HEAT over a hot surface: a face looking down on a disc of radius R at temperature
//   T from h over its middle takes eps sigma T^4 R^2 / (R^2 + h^2) (the disc's view factor), the
//   T^4 being the surface's: a lava lake crusted over radiates far less than a molten one.
//
// ge:summary What a liquid does to a body: damage ticks by immersion, heat flux and thermal dose
// ge:keywords harm damage hurt burn heat lava slime acid tick interval thermal-dose tdu probit
// ge:keywords radiant-flux view-factor stefan-boltzmann contact-flux step_harm burn_grade
// ge:skill water
// ge:test tests/water_harm_test.cpp
#pragma once

#include "water/liquid.hpp"

namespace ge::water {

inline constexpr double kStefan = 5.670374419e-8;  // W/(m^2 K^4)
inline constexpr double kSkinC = 35;               // degrees C
// The thermal dose's thresholds, (kW/m^2)^(4/3) s.
inline constexpr double kDosePain = 92;
inline constexpr double kDoseFirst = 105;
inline constexpr double kDoseSecond = 290;
inline constexpr double kDoseThird = 1000;
inline constexpr double kDoseLethal = 2376;  // half of those exposed whole-body

struct HarmState {
  double wait = 0;  // s to the next tick
  double dose = 0;  // the thermal dose taken, (kW/m^2)^(4/3) s
  double flux = 0;  // this step's heat into the skin, W/m^2 (on the whole body's terms)
};

// The share of the skin a liquid wets at immersion `level` 0..3.
double wetted_share(int level);

// The heat flux into skin wetted by a liquid at `temperature` degrees C, W/m^2.
double contact_flux(const Harm& harm, double temperature);

// The radiant flux on a face `height` m over the middle of a disc of radius `radius` m at
// `temperature` degrees C with emissivity `emissivity`, W/m^2.
double radiant_flux(double temperature, double emissivity, double radius, double height);

// The dose's rate at a flux of `flux` W/m^2, (kW/m^2)^(4/3) per s.
double dose_rate(double flux);

// The burn a dose has come to: 0 none, 1 pain, 2 first degree, 3 second, 4 third, 5 lethal.
int burn_grade(double dose);

// One step of `dt` s at immersion `level` in a liquid of `harm` at `temperature` degrees C, with
// `outside` W/m^2 more on the whole body from outside (radiant heat); the damage dealt.
double step_harm(const Harm& harm, double temperature, int level, HarmState& state, double dt,
                 double outside = 0);

}  // namespace ge::water
