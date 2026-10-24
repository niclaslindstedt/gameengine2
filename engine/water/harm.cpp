// What a liquid does to a body: definitions.
//
// ge:summary What a liquid does to a body: the ticks' clock, the heat and the dose: definitions
// ge:keywords harm tick clock dose flux wetted-share radiant contact burn
// ge:skill water
// ge:test tests/water_harm_test.cpp
#include "water/harm.hpp"

#include "core/libm/libm.hpp"
#include "core/num.hpp"

namespace ge::water {

namespace {

constexpr double kKelvin = 273.15;

}  // namespace

double wetted_share(int level) {
  switch (level) {
    case 1:
      return 0.15;
    case 2:
      return 0.5;
    case 3:
      return 1;
    default:
      return 0;
  }
}

double contact_flux(const Harm& harm, double temperature) {
  return harm.contact * core::num::max(0.0, temperature - kSkinC);
}

double radiant_flux(double temperature, double emissivity, double radius, double height) {
  const double t = temperature + kKelvin;
  const double r2 = radius * radius;
  const double h = core::num::max(0.0, height);
  return emissivity * kStefan * t * t * t * t * r2 / (r2 + h * h);
}

double dose_rate(double flux) {
  if (flux <= 0) return 0;
  return core::libm::pow(flux / 1000, 4.0 / 3.0);
}

int burn_grade(double dose) {
  if (dose >= kDoseLethal) return 5;
  if (dose >= kDoseThird) return 4;
  if (dose >= kDoseSecond) return 3;
  if (dose >= kDoseFirst) return 2;
  if (dose >= kDosePain) return 1;
  return 0;
}

double step_harm(const Harm& harm, double temperature, int level, HarmState& state, double dt,
                 double outside) {
  double damage = 0;
  // The ticks: on going in, then every interval; the clock runs on out of the liquid.
  state.wait = core::num::max(0.0, state.wait - dt);
  if (level >= 1 && level <= 3 && state.wait <= 0) {
    const double tick = harm.tick[level - 1];
    if (tick > 0) {
      damage += tick;
      state.wait = harm.interval;
    }
  }
  // The heat: the wetted share's contact flux and the outside's on the whole skin. The dose is
  // counted on the share it reaches (the rate at the share's own flux, times the share).
  const double share = wetted_share(level);
  const double contact = contact_flux(harm, temperature);
  state.flux = share * contact + core::num::max(0.0, outside);
  const double dose = (share * dose_rate(contact) + dose_rate(outside)) * dt;
  state.dose += dose;
  damage += dose * harm.per_dose;
  return damage;
}

}  // namespace ge::water
