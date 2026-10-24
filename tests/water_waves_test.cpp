// engine/water/waves.hpp: a laid band carries the sea's height and its components are capped in
// steepness and fan round the wind; a long series at one point measures the laid Hs and mean
// period; the slopes are the height's derivatives and the vertical velocity its rise; the
// breaking shares wake where they should; and every number keeps its bits in
// tests/golden/water_waves.txt.
//
// ge:summary Test: the simulation tier's waves: the laid band, a measured series, slopes, breaking
// ge:keywords test water waves lay components series significant-height period slope breaking
// ge:keywords golden
// ge:skill water testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "water/waves.hpp"
#include "water_golden.hpp"

namespace {

namespace gw = ge::water;
constexpr double kTau = 6.283185307179586;

gw::Waves sea_of(double wind, double fetch, double toward, uint32_t seed, double depth = 1e9) {
  gw::Sea sea = gw::grow(wind, fetch, toward);
  sea.depth = depth;
  gw::Lay how;
  how.seed = seed;
  return gw::lay(sea, how);
}

}  // namespace

TEST(the_laid_band_carries_the_seas_height) {
  for (const double wind : {6.0, 10.0, 16.0}) {
    const gw::Waves w = sea_of(wind, 2e5, 1.0, 38);
    CHECK(w.count == 24);
    const double hs = 4 * std::sqrt(w.m0);
    // The band laid misses only the tails: under a tenth of the height.
    CHECK(hs <= w.sea.hs * 1.001 && hs > 0.9 * w.sea.hs);
    double cx = 0, cz = 0;
    for (int i = 0; i < w.count; ++i) {
      const gw::Component& c = w.c[i];
      CHECK(c.k * c.a <= 0.3 + 1e-12);
      CHECK(c.w >= w.w_lo && c.w <= w.w_hi);
      CHECK(std::fabs(c.kx * c.kx + c.kz * c.kz - c.k * c.k) < 1e-9 * c.k * c.k);
      cx += c.a * c.a * c.kx / c.k;
      cz += c.a * c.a * c.kz / c.k;
    }
    // The energy runs with the wind.
    CHECK(std::fabs(std::atan2(cx, cz) - 1.0) < 0.2);
  }
  // Nothing to lay on a still day.
  CHECK(sea_of(0.1, 1e5, 0, 1).count == 0);
}

TEST(a_long_series_measures_the_laid_height_and_period) {
  const gw::Waves w = sea_of(10, 5e4, 0.3, 7);
  const double dt = 0.05;
  const int n = 200000;  // 10000 s
  double sum = 0, sum2 = 0;
  int ups = 0;
  double last = gw::surface_at(w, 12, -5, 0).height;
  for (int i = 1; i <= n; ++i) {
    const double h = gw::surface_at(w, 12, -5, i * dt).height;
    sum += h;
    sum2 += h * h;
    if (last < 0 && h >= 0) ++ups;
    last = h;
  }
  const double mean = sum / n;
  const double sd = std::sqrt(sum2 / n - mean * mean);
  CHECK(std::fabs(4 * sd / (4 * std::sqrt(w.m0)) - 1) < 0.05);
  // The second-order crest lifts the crests and flattens the troughs: the mean sits a little low
  // (the bound wave's set-down), never far from still water.
  CHECK(std::fabs(mean) < 0.05 * sd);
  const double tz = n * dt / ups;
  const double tz_laid = kTau * std::sqrt(w.m0 / w.m2);
  CHECK(std::fabs(tz / tz_laid - 1) < 0.12);
}

TEST(the_slopes_and_the_rise_are_the_heights_derivatives) {
  const gw::Waves w = sea_of(12, 1e5, 2.0, 3, 20);
  const double e = 1e-5;
  for (const double t : {0.0, 3.7, 41.2}) {
    for (const double x : {-30.0, 0.0, 17.5}) {
      const gw::Surface s = gw::surface_at(w, x, 4, t);
      const double dx =
          (gw::surface_at(w, x + e, 4, t).height - gw::surface_at(w, x - e, 4, t).height) / (2 * e);
      const double dz =
          (gw::surface_at(w, x, 4 + e, t).height - gw::surface_at(w, x, 4 - e, t).height) / (2 * e);
      CHECK(std::fabs(dx - s.dx) < 1e-6);
      CHECK(std::fabs(dz - s.dz) < 1e-6);
      // The vertical speed is the linear surface's rise (the second-order part is left out).
      const double rise =
          (gw::surface_at(w, x, 4, t + e).height - gw::surface_at(w, x, 4, t - e).height) / (2 * e);
      CHECK(std::fabs(rise - s.v) < 0.35 * (std::fabs(rise) + 0.1));
    }
  }
}

TEST(a_scale_of_zero_is_flat_water) {
  const gw::Waves w = sea_of(12, 1e5, 0, 3);
  const gw::Surface s = gw::surface_at(w, 5, 5, 9, 0);
  CHECK(s.height == 0 && s.dx == 0 && s.dz == 0 && s.u == 0 && s.v == 0 && s.w == 0);
}

TEST(the_breaking_shares_wake_where_they_should) {
  gw::Surface flat;
  gw::Breaking b = gw::breaking_at(flat, 10, 0);
  CHECK(b.crest == 0 && b.shoal == 0 && b.wind == 0);
  gw::Surface steep;
  steep.dx = 0.6;
  CHECK(gw::breaking_at(steep, 10, 0).crest == 1);
  steep.dx = 0.3;
  CHECK(gw::breaking_at(steep, 10, 0).crest == 0);
  gw::Surface high;
  high.height = 0.5;
  CHECK(gw::breaking_at(high, 0.7, 0).shoal == 1);
  CHECK(gw::breaking_at(high, 30, 0).shoal == 0);
  CHECK(gw::breaking_at(flat, 0, 0).shoal == 1);
  // The whitecap cover: about 1% at 10 m/s, rising steeply with the wind.
  const double w10 = gw::breaking_at(flat, 10, 10).wind;
  CHECK(w10 > 0.009 && w10 < 0.011);
  CHECK(gw::breaking_at(flat, 10, 20).wind > 10 * w10);
}

TEST(every_number_keeps_its_golden) {
  std::vector<std::vector<uint64_t>> got;
  const gw::Waves seas[] = {sea_of(8, 3e3, 0.4, 38), sea_of(14, 1e9, 3.0, 7, 12),
                            sea_of(5, 400, 5.5, 1234)};
  for (const gw::Waves& w : seas) {
    got.push_back({ge::test::bits(w.m0), ge::test::bits(w.m2), ge::test::bits(w.c[0].kx),
                   ge::test::bits(w.c[5].phase)});
    for (const double t : {0.0, 9.25, 61.5}) {
      const gw::Surface s = gw::surface_at(w, 3.5, -8, t, 0.8);
      got.push_back({ge::test::bits(s.height), ge::test::bits(s.dx), ge::test::bits(s.dz),
                     ge::test::bits(s.u), ge::test::bits(s.v), ge::test::bits(s.w)});
    }
  }
  ge::test::hold_golden("tests/golden/water_waves.txt",
                        "per sea: m0 m2 c0.kx c5.phase; then per moment: height dx dz u v w "
                        "(water_waves_test)",
                        got);
}
