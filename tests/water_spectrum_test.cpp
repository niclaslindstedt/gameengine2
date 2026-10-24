// engine/water/spectrum.hpp: the growth laws give their textbook heights and periods and cap at the
// fully developed sea; the normalised spectrum carries the asked height; the depth factor is one
// in deep water and falls in the shallows; the spread peaks at the mean and is narrowest at the
// peak frequency; and the wavenumber solves the dispersion relation in any depth.
//
// ge:summary Test: the sea a wind grows: growth laws, JONSWAP normalisation, depth limit, spread
// ge:keywords test water spectrum jonswap fetch growth tma spread dispersion wavenumber
// ge:skill water testing
#include <cmath>

#include "check.hpp"
#include "core/libm/libm.hpp"
#include "water/spectrum.hpp"

namespace {

namespace gw = ge::water;
constexpr double kTau = 6.283185307179586;

double close(double got, double want) { return std::fabs(got - want) / std::fabs(want); }

}  // namespace

TEST(the_growth_laws_give_their_heights_and_periods) {
  // 10 m/s over 10 km: g F / U^2 = 981, so Hs = 0.0016 sqrt(981) U^2 / g and
  // Tp = 0.286 981^(1/3) U / g.
  const gw::Sea s = gw::grow(10, 1e4, 0.5);
  const double x = gw::kGravity * 1e4 / 100;
  CHECK(close(s.hs, 0.0016 * std::sqrt(x) * 100 / gw::kGravity) < 1e-12);
  CHECK(close(s.tp, 0.286 * std::cbrt(x) * 10 / gw::kGravity) < 1e-12);
  CHECK(s.hs > 0.5 && s.hs < 0.52);
  CHECK(s.tp > 2.85 && s.tp < 2.95);
  CHECK(s.toward == 0.5);
  // A longer fetch grows a bigger, longer sea, up to the developed one.
  const gw::Sea longer = gw::grow(10, 1e5, 0);
  CHECK(longer.hs > s.hs && longer.tp > s.tp);
  const gw::Sea full = gw::grow(10, 1e9, 0);
  CHECK(close(full.hs, 0.243 * 100 / gw::kGravity) < 1e-12);
  CHECK(close(full.tp, 8.13 * 10 / gw::kGravity) < 1e-12);
  CHECK(gw::developed_fetch() > 2.2e4 && gw::developed_fetch() < 2.4e4);
  // Still air and no fetch grow nothing.
  CHECK(gw::grow(0.2, 1e4, 0).hs == 0);
  CHECK(gw::grow(10, 0, 0).hs == 0);
}

TEST(the_spectrum_carries_the_asked_height) {
  for (const double depth : {1e9, 30.0, 4.0}) {
    gw::Sea sea = gw::grow(12, 5e4, 0);
    sea.depth = depth;
    const double norm = gw::normaliser(sea);
    const double wp = kTau / sea.tp;
    double m0 = 0;
    const int n = 20000;
    const double lo = 0.2 * wp, hi = 8 * wp, dw = (hi - lo) / n;
    for (int i = 0; i < n; ++i) m0 += gw::jonswap_at(sea, lo + (i + 0.5) * dw, norm) * dw;
    CHECK(close(4 * std::sqrt(m0), sea.hs) < 0.002);
    // The peak is where it is asked to be (in deep water).
    if (depth > 1e6) {
      double best = 0, at = 0;
      for (int i = 0; i < n; ++i) {
        const double w = lo + (i + 0.5) * dw;
        const double f = gw::jonswap_at(sea, w, norm);
        if (f > best) {
          best = f;
          at = w;
        }
      }
      CHECK(close(at, wp) < 0.01);
    }
  }
  // A peakier sea (gamma) puts more of its energy at the peak.
  const double wp = 1.0;
  CHECK(gw::jonswap_shape(wp, wp, 7) > gw::jonswap_shape(wp, wp, 3.3));
  CHECK(gw::jonswap_shape(wp, wp, 1) < gw::jonswap_shape(wp, wp, 3.3));
}

TEST(the_depth_factor_is_one_deep_and_falls_in_the_shallows) {
  CHECK(gw::tma(1.0, 1000) == 1);
  CHECK(gw::tma(1.0, 0.5) < 0.2);
  double last = 0;
  for (int step = 0; step < 20; ++step) {
    const double f = gw::tma(1.0, 0.1 * std::pow(1.3, step));
    CHECK(f >= last && f <= 1);
    last = f;
  }
}

TEST(the_spread_is_round_the_mean_and_narrowest_at_the_peak) {
  CHECK(gw::spread(0, 10) == 1);
  CHECK(gw::spread(0.5, 10) < 1 && gw::spread(1.0, 10) < gw::spread(0.5, 10));
  CHECK(gw::spread(0.5, 10) == gw::spread(-0.5, 10));
  const double peak = gw::spread_power(1, 1);
  CHECK(close(peak, gw::kSpreadPeak) < 1e-12);
  CHECK(gw::spread_power(0.7, 1) < peak && gw::spread_power(2, 1) < peak);
  CHECK(gw::spread_power(0.1, 1) >= 1 && gw::spread_power(20, 1) >= 1);
}

TEST(the_wavenumber_solves_the_dispersion_relation) {
  for (const double w : {0.3, 0.8, 1.5, 4.0, 10.0}) {
    // Deep water: k = w^2 / g exactly.
    CHECK(gw::wavenumber(w, 1e9) == w * w / gw::kGravity);
    for (const double d : {0.2, 1.0, 5.0, 40.0}) {
      const double k = gw::wavenumber(w, d);
      const double lhs = gw::kGravity * k * ge::core::libm::tanh(k * d);
      CHECK(close(lhs, w * w) < 1e-9);
      // Shallow water slows a wave: k is never less than the deep water's.
      CHECK(k >= w * w / gw::kGravity * (1 - 1e-12));
    }
  }
}
