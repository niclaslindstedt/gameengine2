// WHAT A LIQUID IS BESIDE ITS LOOK (docs/liquids.md): the numbers that make water, lava and slime
// behave differently under the same wind, the same swimmer and the same body, every one a knob a
// game tunes on its body's `Stuff::liquid` (world.hpp). The named rows are liquids.hpp's.
//
// - THE FLOW: the dynamic viscosity (Pa s) and the yield stress (Pa) of a Bingham liquid, which
//   does not flow at all under a shear stress below its yield (a gel, a sludge, a lava's crust
//   holding it) and flows with its viscosity above it. The density is `Stuff::density`. A
//   viscosity of 0 is a THIN liquid, whose viscous terms are dropped (water's 0.001 Pa s
//   changes nothing a game can see): every water body laid before liquids existed is one, and
//   keeps its bits.
// - THE WIND ON IT (`wind_keep`, `drift`): a wave of wavenumber k is fed by the wind at
//   beta = 0.04 (u* / c)^2 w (1000 / rho) (the wind's input to a wave's energy over water, its
//   share falling as the liquid is denser than water) and loses its energy to viscosity at
//   4 nu k^2 (twice the amplitude's decay, 2 nu k^2); it keeps the share 1 - loss / input of the
//   wind's input, nothing where the loss wins, so a viscous liquid loses its short ripples first,
//   then its swell, and lava grows no waves at all. The surface drifts downwind at the stress's
//   Couette speed over the layer, (tau - tau_y) h / mu, capped at water's 3 % of the wind: water
//   always at the cap, a thin slime slowly in a gale, a yield above the wind's stress not at all.
//   tau = rho_air Cd U^2 with Cd = 1.3e-3 and u* = sqrt(Cd) U.
// - A SWIMMER IN IT (`SwimScale`, swim.hpp): the kinematic regime's speed, friction and idle sink
//   are scaled by the liquid's own `SwimScale` (1, 1, 1 in water). `swim_for` derives the scales
//   from the physics: the default swimmer's stroke against quadratic drag, Stokes drag on his
//   equivalent sphere (6 pi mu r v) and the yield's grip on his wetted skin (tau_y A) gives his
//   top speed here over his top speed in water; the friction is its inverse; the sink is his
//   body's density against the liquid's, times the speed with no yield (so he floats up out of
//   anything denser than himself, slowly in anything thick). A game that wants the classic feel in
//   every liquid sets `SwimScale` to {1, 1, 1}. The body regime reads the viscosity and the yield
//   directly as drag (swim.cpp).
// - WHAT IT DOES TO A BODY (`Harm`, harm.hpp): the game's damage at each immersion level on a
//   clock, and the heat a hot liquid puts into skin as a thermal dose.
//
// ge:summary A liquid's physics beside its look: viscosity, yield, wind keep and drift, swim scales
// ge:keywords liquid lava slime water viscosity yield-stress bingham density surface-tension
// ge:keywords wind_keep drift swim_for swim-scale harm wind-waves viscous-damping couette ripples
// ge:keywords liquid-type tweakable knobs
// ge:skill water
// ge:test tests/water_liquid_test.cpp
#pragma once

#include <cstdint>
#include <vector>

#include "water/waves.hpp"

namespace ge::water {

// How the kinematic swim is scaled in a liquid (swim.hpp): 1, 1, 1 is water's feel.
struct SwimScale {
  double speed = 1;     // the wish speed's share
  double friction = 1;  // the friction's scale
  double sink = 1;      // the idle sink's scale (negative: he rises)
};

enum class HarmKind : uint8_t { none, heat, chemical };

// What a liquid does to a body in it (harm.hpp steps it). TICKS: `tick[level - 1]` of the
// game's health at immersion level 1, 2 and 3, dealt on entering and every `interval` s while
// in it (a clock that keeps running out of it, so a dip cannot reset it). HEAT: the liquid's
// heat into the skin it wets, `contact` W/m^2 per kelvin over the skin's 35 degrees C on the
// immersed share of the body, and any flux the game adds from outside (the radiant heat over
// lava, `radiant_flux`), accumulated as a thermal dose; `per_dose` of the game's health per
// unit of it.
struct Harm {
  HarmKind kind = HarmKind::none;
  double tick[3] = {0, 0, 0};
  double interval = 1;  // s
  double contact = 0;   // W/(m^2 K)
  double per_dose = 0;  // health per (kW/m^2)^(4/3) s
};

struct Liquid {
  double viscosity = 0;    // Pa s, dynamic (0: thin, the viscous terms dropped)
  double yield = 0;        // Pa, the Bingham yield stress
  double tension = 0.072;  // N/m, the surface tension (the ripples' capillary end)
  SwimScale swim{};
  Harm harm{};
};

inline constexpr double kAirDensity = 1.225;  // kg/m^3
inline constexpr double kDragCoefficient = 1.3e-3;
inline constexpr double kDriftShare = 0.03;  // water's surface drift, a share of the 10 m wind

// The wind's stress on a surface under `wind` m/s at 10 m, Pa.
double wind_stress(double wind);

// The share of the wind's input a wave of wavenumber `k` (rad/m, angular frequency `w`) keeps
// on `liquid` of `density` under `wind` m/s at 10 m: 0..1, 1 on a thin liquid.
double wind_keep(const Liquid& liquid, double density, double wind, double k, double w);

// The waves laid on a body of `liquid` cut by `wind_keep`: each component's energy by its share
// (its amplitude by the root of it), the moments taken again. A thin liquid's are untouched.
void damp_waves(Waves& waves, const Liquid& liquid, double density, double wind);

// The surface's drift downwind under `wind` m/s over a layer `depth` m deep, m/s.
double drift(const Liquid& liquid, double wind, double depth);

// The default swimmer's top speed in a liquid of `density`, `viscosity` and `yield`, m/s (0:
// held fast by the yield), and the `SwimScale` the scales above derive from it.
double stroke_speed(double density, double viscosity, double yield);
SwimScale swim_for(double density, double viscosity, double yield);

// THE PICTURE'S RIPPLES: `count` components of the sea `wind` m/s grows over `fetch` m of a
// liquid (from half the peak's frequency to waves 2 cm long, even in the log of the frequency,
// spread up to 60 degrees either side of downwind, `toward` the heading they run to), each cut
// by `wind_keep`. For the drawing alone: the simulation's waves are `damp_waves`'.
std::vector<Component> ripples(const Liquid& liquid, double density, double wind, double toward,
                               double fetch, int count, uint32_t seed);

}  // namespace ge::water
