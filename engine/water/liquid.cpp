// A liquid's physics: definitions.
//
// ge:summary A liquid's physics: the wind's keep and drift, the stroke's speed, ripples
// ge:keywords liquid wind-keep drift stroke-speed swim-for ripples capillary dispersion bingham
// ge:skill water
// ge:test tests/water_liquid_test.cpp
#include "water/liquid.hpp"

#include <cmath>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"
#include "core/prng.hpp"
#include "water/spectrum.hpp"

namespace ge::water {

namespace {

namespace lm = core::libm;
constexpr double kPi = 3.141592653589793;
constexpr double kWater = 1000;  // kg/m^3: the density the wind's input over water is stated at
constexpr double kGrowth = 0.04;
// The default swimmer (swim.hpp's default_swimmer): his stroke, frontal area, drag coefficient
// and wetted skin, and his density with a half breath in.
constexpr double kThrust = 90;  // N
constexpr double kArea = 0.12;  // m^2
constexpr double kDrag = 1.0;
constexpr double kSkin = 1.8;       // m^2
constexpr double kBody = 1013;      // kg/m^3
constexpr double kShortest = 0.02;  // m: the ripples' shortest

// The wind's friction velocity, m/s.
double friction_velocity(double wind) { return std::sqrt(kDragCoefficient) * wind; }

}  // namespace

double wind_stress(double wind) { return kAirDensity * kDragCoefficient * wind * wind; }

double wind_keep(const Liquid& liquid, double density, double wind, double k, double w) {
  if (liquid.viscosity <= 0) return 1;
  if (k <= 0 || w <= 0 || wind <= 0) return 0;
  const double c = w / k;
  const double u = friction_velocity(wind) / c;
  const double input = kGrowth * u * u * w * kWater / density;
  const double loss = 4 * liquid.viscosity / density * k * k;
  return core::clamp(1 - loss / input, 0, 1);
}

void damp_waves(Waves& waves, const Liquid& liquid, double density, double wind) {
  if (liquid.viscosity <= 0) return;
  waves.m0 = 0;
  waves.m2 = 0;
  for (int i = 0; i < waves.count; ++i) {
    Component& c = waves.c[i];
    c.a *= std::sqrt(wind_keep(liquid, density, wind, c.k, c.w));
    waves.m0 += 0.5 * c.a * c.a;
    waves.m2 += 0.5 * c.a * c.a * c.w * c.w;
  }
}

double drift(const Liquid& liquid, double wind, double depth) {
  const double most = kDriftShare * wind;
  if (liquid.viscosity <= 0) return most;
  const double tau = wind_stress(wind) - liquid.yield;
  if (tau <= 0) return 0;
  return core::num::min(most, tau * core::num::max(0.0, depth) / liquid.viscosity);
}

double stroke_speed(double density, double viscosity, double yield) {
  const double push = kThrust - yield * kSkin;
  if (push <= 0) return 0;
  const double a = 0.5 * density * kDrag * kArea;
  const double b = 6 * kPi * viscosity * std::sqrt(kArea / kPi);
  return (-b + std::sqrt(b * b + 4 * a * push)) / (2 * a);
}

SwimScale swim_for(double density, double viscosity, double yield) {
  const double speed = stroke_speed(density, viscosity, yield) / stroke_speed(kWater, 0, 0);
  SwimScale m;
  m.speed = speed;
  m.friction = 1 / core::num::max(speed, 0.05);
  // His weight against the liquid's buoyancy moves him through its viscosity; the yield holds
  // a stroke, never the buoyancy of a body in something twice his density.
  m.sink = core::clamp((kBody - density) / (kBody - kWater), -1, 1) *
           stroke_speed(density, viscosity, 0) / stroke_speed(kWater, 0, 0);
  return m;
}

std::vector<Component> ripples(const Liquid& liquid, double density, double wind, double toward,
                               double fetch, int count, uint32_t seed) {
  std::vector<Component> out;
  const Sea sea = grow(wind, fetch, toward);
  if (sea.hs <= 0 || sea.tp <= 0 || count <= 0) return out;
  const double norm = normaliser(sea);
  const double wp = 2 * kPi / sea.tp;
  const double w0 = 0.5 * wp;
  // The shortest ripple's frequency, its capillary term with it (w^2 = g k + sigma / rho k^3).
  const double k1 = 2 * kPi / kShortest;
  const double w1 = std::sqrt(kGravity * k1 + liquid.tension / density * k1 * k1 * k1);
  if (w1 <= w0) return out;
  core::Rng rng = core::make_rng(seed);
  const double step = lm::log(w1 / w0) / count;
  for (int i = 0; i < count; ++i) {
    const double lo = w0 * lm::exp(step * i), hi = w0 * lm::exp(step * (i + 1));
    double e = 0;
    constexpr int kTaps = 8;
    for (int j = 0; j < kTaps; ++j) {
      const double w = lo + (hi - lo) * (j + 0.5) / kTaps;
      e += jonswap_at(sea, w, norm) * (hi - lo) / kTaps;
    }
    const double w = std::sqrt(lo * hi);
    // The wavenumber off the capillary-gravity dispersion, by Newton from the gravity guess.
    double k = w * w / kGravity;
    for (int it = 0; it < 12; ++it) {
      const double s = liquid.tension / density;
      const double f = kGravity * k + s * k * k * k - w * w;
      const double df = kGravity + 3 * s * k * k;
      k -= f / df;
    }
    const double off = (core::next(rng) * 2 - 1) * kPi / 3;
    Component c;
    c.w = w;
    c.k = k;
    c.kx = k * lm::sin(toward + off);
    c.kz = k * lm::cos(toward + off);
    c.a = std::sqrt(2 * e * wind_keep(liquid, density, wind, k, w));
    c.phase = core::next(rng) * 2 * kPi;
    out.push_back(c);
  }
  return out;
}

}  // namespace ge::water
