// The sea a wind grows: definitions.
//
// ge:summary The sea a wind grows: definitions
// ge:keywords water spectrum jonswap fetch tma spread dispersion newton
// ge:skill water determinism
// ge:test tests/water_spectrum_test.cpp
#include "water/spectrum.hpp"

#include <cmath>

#include "core/libm/libm.hpp"
#include "core/num.hpp"

namespace ge::water {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kTau = 2 * kPi;

constexpr double kHsLaw = 0.0016;   // g Hs / U^2 per sqrt(g F / U^2)
constexpr double kTpLaw = 0.286;    // g Tp / U per (g F / U^2)^(1/3)
constexpr double kHsFull = 0.243;   // g Hs / U^2, fully developed
constexpr double kTpFull = 8.13;    // g Tp / U, fully developed
constexpr double kStill = 0.3;      // m/s: no sea under this
constexpr int kNormSteps = 2400;    // the normaliser's trapezoid over 0.2 .. 8 wp
constexpr double kSpreadFloor = 1;  // s never under this far from the peak

}  // namespace

double developed_fetch() {
  const double r = kHsFull / kHsLaw;
  return r * r;
}

Sea grow(double wind, double fetch, double toward) {
  Sea sea;
  sea.toward = toward;
  if (wind < kStill || fetch <= 0) return sea;
  const double u2 = wind * wind;
  const double x = core::num::min(kGravity * fetch / u2, developed_fetch());
  sea.hs = kHsLaw * std::sqrt(x) * u2 / kGravity;
  sea.tp = core::num::min(kTpLaw * core::libm::cbrt(x), kTpFull) * wind / kGravity;
  return sea;
}

double jonswap_shape(double w, double wp, double gamma) {
  if (w <= 0) return 0;
  const double sigma = w <= wp ? 0.07 : 0.09;
  const double d = (w - wp) / (sigma * wp);
  const double r = core::libm::exp(-0.5 * d * d);
  const double q = wp / w;
  const double q4 = q * q * q * q;
  const double w5 = w * w * w * w * w;
  return kGravity * kGravity / w5 * core::libm::exp(-1.25 * q4) * core::libm::pow(gamma, r);
}

double tma(double w, double depth) {
  const double wh = w * std::sqrt(depth / kGravity);
  if (wh >= 2) return 1;
  if (wh <= 1) return 0.5 * wh * wh;
  const double e = 2 - wh;
  return 1 - 0.5 * e * e;
}

double normaliser(const Sea& sea) {
  if (sea.hs <= 0 || sea.tp <= 0) return 0;
  const double wp = kTau / sea.tp;
  const double lo = 0.2 * wp, hi = 8 * wp, dw = (hi - lo) / kNormSteps;
  double m0 = 0;
  for (int i = 0; i <= kNormSteps; ++i) {
    const double w = lo + i * dw;
    const double f = jonswap_shape(w, wp, sea.gamma) * tma(w, sea.depth);
    m0 += (i == 0 || i == kNormSteps) ? 0.5 * f : f;
  }
  m0 *= dw;
  const double q = sea.hs / 4;
  return m0 > 0 ? q * q / m0 : 0;
}

double jonswap_at(const Sea& sea, double w, double norm) {
  if (sea.tp <= 0) return 0;
  return norm * jonswap_shape(w, kTau / sea.tp, sea.gamma) * tma(w, sea.depth);
}

double spread_power(double w, double wp) {
  const double q = w / wp;
  const double s = q < 1 ? kSpreadPeak * q * q * q * q * q : kSpreadPeak * core::libm::pow(q, -2.5);
  return core::num::max(s, kSpreadFloor);
}

double spread(double off, double s) {
  const double c = core::libm::cos(0.5 * off);
  return core::libm::pow(c * c, s);
}

double wavenumber(double w, double depth) {
  const double deep = w * w / kGravity;
  if (deep * depth > 20) return deep;
  // Eckart's estimate, then Newton on g k tanh(k d) - w^2, a fixed four steps.
  double k = deep / std::sqrt(core::libm::tanh(deep * depth));
  for (int i = 0; i < 4; ++i) {
    const double t = core::libm::tanh(k * depth);
    const double f = kGravity * k * t - w * w;
    const double df = kGravity * (t + k * depth * (1 - t * t));
    k -= f / df;
  }
  return k;
}

}  // namespace ge::water
