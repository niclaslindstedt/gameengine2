// The simulation tier's waves: definitions.
//
// ge:summary The simulation tier's waves: definitions
// ge:keywords water waves lay surface breaking quantile shuffle
// ge:skill water determinism
// ge:test tests/water_waves_test.cpp
#include "water/waves.hpp"

#include <cmath>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"
#include "core/prng.hpp"

namespace ge::water {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kTau = 2 * kPi;
constexpr int kBandSteps = 1024;       // the band's energy table
constexpr int kSpreadSteps = 512;      // the spread's table over (-pi, pi]
constexpr double kSteepest = 0.3;      // a k: no component laid steeper
constexpr double kLimitSlope = 0.577;  // the limiting wave's 30 degree face
constexpr double kSpillFrom = 0.35;    // a face this steep starts to spill
constexpr double kBreakDepth = 0.78;   // a wave breaks past this share of the depth
constexpr double kDeep = 1e6;          // m: deep enough that the bed is nowhere

double smooth(double x) {
  const double c = core::clamp(x, 0, 1);
  return c * c * (3 - 2 * c);
}

// The angle off the mean at quantile `q` of the spread with power `s`.
double spread_quantile(double q, double s) {
  double cdf[kSpreadSteps + 1];
  cdf[0] = 0;
  const double d = kTau / kSpreadSteps;
  for (int i = 0; i < kSpreadSteps; ++i) {
    const double mid = -kPi + (i + 0.5) * d;
    cdf[i + 1] = cdf[i] + spread(mid, s);
  }
  const double want = q * cdf[kSpreadSteps];
  int i = 0;
  while (i < kSpreadSteps - 1 && cdf[i + 1] < want) ++i;
  const double span = cdf[i + 1] - cdf[i];
  const double f = span > 0 ? (want - cdf[i]) / span : 0.5;
  return -kPi + (i + f) * d;
}

}  // namespace

Waves lay(const Sea& sea, const Lay& how) {
  Waves out;
  out.sea = sea;
  if (sea.hs <= 0 || sea.tp <= 0) return out;
  const double depth = sea.depth > 0 ? sea.depth : kDeep;
  const double wp = kTau / sea.tp;
  const double k_short = kTau / how.shortest;
  const double w_short = std::sqrt(kGravity * k_short * core::libm::tanh(k_short * depth));
  out.w_lo = 0.5 * wp;
  out.w_hi = core::num::min(8 * wp, w_short);
  if (out.w_hi <= out.w_lo) return out;
  const int count = how.count < 1 ? 1 : how.count > kMaxComponents ? kMaxComponents : how.count;
  const double norm = normaliser(sea);
  // The band cumulated over a fine table twice: by energy, and by the slicing weight (the energy
  // times 1 + (w / wp)^2, half energy and half slope, so the tail gets enough components that no
  // single short wave carries a slice of the tail's slope alone).
  double cum[kBandSteps + 1], energy[kBandSteps + 1];
  cum[0] = energy[0] = 0;
  const double dw = (out.w_hi - out.w_lo) / kBandSteps;
  for (int i = 0; i < kBandSteps; ++i) {
    const double w = out.w_lo + (i + 0.5) * dw;
    const double e = jonswap_at(sea, w, norm) * dw;
    const double q = w / wp;
    energy[i + 1] = energy[i] + e;
    cum[i + 1] = cum[i] + e * (1 + q * q);
  }
  const double total = cum[kBandSteps];
  if (total <= 0) return out;
  core::Rng rng = core::make_rng(how.seed);
  // The quantiles of the spread, shuffled across the slices.
  int order[kMaxComponents];
  for (int i = 0; i < count; ++i) order[i] = i;
  for (int i = count - 1; i > 0; --i) {
    const int j = core::int_in(rng, 0, i);
    const int tmp = order[i];
    order[i] = order[j];
    order[j] = tmp;
  }
  // Where the weight reaches `want` on the table, as a fractional step, and the energy there.
  int at = 0;
  const auto find = [&](double want) {
    while (at < kBandSteps - 1 && cum[at + 1] < want) ++at;
    const double span = cum[at + 1] - cum[at];
    return at + (span > 0 ? (want - cum[at]) / span : 0.5);
  };
  const auto energy_at = [&](double step) {
    const int i = step >= kBandSteps ? kBandSteps - 1 : static_cast<int>(step);
    return energy[i] + (energy[i + 1] - energy[i]) * (step - i);
  };
  const double share = total / count;
  double lo_step = 0;
  for (int i = 0; i < count; ++i) {
    const double mid_step = find((i + 0.5) * share);
    const double hi_step = i + 1 == count ? kBandSteps : find((i + 1) * share);
    const double e = energy_at(hi_step) - energy_at(lo_step);
    lo_step = hi_step;
    Component& c = out.c[i];
    c.w = out.w_lo + mid_step * dw;
    c.k = wavenumber(c.w, depth);
    c.a = core::num::min(std::sqrt(2 * e), kSteepest / c.k);
    const double off = spread_quantile((order[i] + 0.5) / count, spread_power(c.w, wp));
    const double toward = sea.toward + off;
    c.kx = c.k * core::libm::sin(toward);
    c.kz = c.k * core::libm::cos(toward);
    c.phase = core::next(rng) * kTau;
    const double kd = c.k * depth;
    c.coth = kd > 20 ? 1 : 1 / core::libm::tanh(kd);
    out.m0 += 0.5 * c.a * c.a;
    out.m2 += 0.5 * c.a * c.a * c.w * c.w;
  }
  out.count = count;
  return out;
}

Surface surface_at(const Waves& waves, double x, double z, double t, double scale) {
  Surface s;
  for (int i = 0; i < waves.count; ++i) {
    const Component& c = waves.c[i];
    const double a = c.a * scale;
    const double phi = c.kx * x + c.kz * z - c.w * t + c.phase;
    const double sn = core::libm::sin(phi), cs = core::libm::cos(phi);
    const double s2 = 2 * sn * cs, c2 = cs * cs - sn * sn;
    s.height += a * sn - 0.5 * c.k * a * a * c2;
    const double slope = a * cs + c.k * a * a * s2;
    s.dx += slope * c.kx;
    s.dz += slope * c.kz;
    // Linear orbital motion at the surface: along the wave with the crest, up as it rises.
    const double along = a * c.w * sn * c.coth / c.k;
    s.u += along * c.kx;
    s.v += -a * c.w * cs;
    s.w += along * c.kz;
  }
  return s;
}

Breaking breaking_at(const Surface& s, double depth, double wind) {
  Breaking b;
  const double slope = std::sqrt(s.dx * s.dx + s.dz * s.dz);
  b.crest = smooth((slope - kSpillFrom) / (kLimitSlope - kSpillFrom));
  if (depth > 0 && s.height > 0) b.shoal = smooth(2 * s.height / (kBreakDepth * depth) - 0.6);
  if (depth <= 0) b.shoal = 1;
  b.wind = wind > 0 ? core::num::min(1.0, 3.84e-6 * core::libm::pow(wind, 3.41)) : 0;
  return b;
}

}  // namespace ge::water
