// engine/water/liquid.hpp: a thin liquid keeps every wave and drifts at water's 3 %; a viscous
// one loses its short waves before its long ones, lava keeps none; a yield above the wind's
// stress holds the surface still; the stroke's speed falls with the viscosity and stops at the
// yield; the swim scales are water's in water; the ripples are cut by the keep; and the numbers
// keep their bits in tests/golden/water_liquid.txt.
//
// ge:summary Test: a liquid's physics: the wind's keep and drift, the stroke, the ripples
// ge:keywords test liquid viscosity yield wind-keep drift stroke-speed swim-for ripples golden
// ge:skill water testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "water/liquid.hpp"
#include "water/liquids.hpp"
#include "water/spectrum.hpp"
#include "water_golden.hpp"

namespace {

namespace gw = ge::water;

gw::Liquid of(const char* name) { return gw::liquid(name).liquid; }
double density(const char* name) { return gw::liquid(name).density; }

double keep_at(const char* name, double wind, double length) {
  const double k = 2 * 3.141592653589793 / length;
  const double w = std::sqrt(gw::kGravity * k);
  return gw::wind_keep(of(name), density(name), wind, k, w);
}

}  // namespace

TEST(a_thin_liquid_keeps_every_wave_and_drifts_at_three_per_cent) {
  const gw::Liquid thin;
  CHECK(gw::wind_keep(thin, 1000, 10, 4, 6) == 1);
  CHECK(std::fabs(gw::drift(thin, 10, 2) - 0.3) < 1e-12);
  // Water's own viscosity costs a metre wave nothing a game can see.
  CHECK(keep_at("water", 10, 1.0) > 0.99);
}

TEST(a_viscous_liquid_loses_its_short_waves_first) {
  // The runny mucus keeps its long swell and loses its ripples; the gel keeps nothing short.
  const double mucus_long = keep_at("slime-mucus", 15, 20),
               mucus_short = keep_at("slime-mucus", 15, 0.5);
  CHECK(mucus_long > mucus_short);
  CHECK(keep_at("slime-gel", 15, 0.5) == 0);
  // Lava grows no waves at any length.
  for (const double len : {0.1, 1.0, 10.0, 100.0}) CHECK(keep_at("lava", 25, len) == 0);
}

TEST(a_yield_above_the_winds_stress_holds_the_surface_still) {
  // 10 m/s is 0.16 Pa: the toxic slime's 0.2 Pa holds; a gale's 0.6 Pa moves it, slowly.
  CHECK(gw::wind_stress(10) < 0.2);
  CHECK(gw::drift(of("slime"), 10, 2) == 0);
  const double gale = gw::drift(of("slime"), 22, 2);
  CHECK(gale > 0 && gale < 0.03 * 22);
  CHECK(gw::drift(of("lava"), 30, 3) == 0);
  CHECK(gw::drift(of("slime-gel"), 30, 3) == 0);
}

TEST(the_stroke_slows_with_viscosity_and_stops_at_the_yield) {
  const double water = gw::stroke_speed(1000, 0, 0);
  CHECK(water > 1.1 && water < 1.4);
  double last = water;
  for (const double mu : {0.1, 1.0, 10.0, 100.0, 1000.0}) {
    const double v = gw::stroke_speed(1000, mu, 0);
    CHECK(v < last);
    last = v;
  }
  CHECK(gw::stroke_speed(1000, 1, 60) == 0);  // 60 Pa over 1.8 m^2 outpulls a 90 N stroke
  const gw::SwimScale w = gw::swim_for(1000, 0, 0);
  CHECK(w.speed == 1 && w.friction == 1 && w.sink == 1);
  // Lava floats him (its sink turned to a rise), and barely lets him move.
  const gw::SwimScale lava = of("lava").swim;
  CHECK(lava.sink < 0);
  CHECK(lava.speed < 0.1);
  CHECK(of("slime-tar").swim.speed == 0);
}

TEST(the_ripples_are_cut_by_the_keep) {
  const std::vector<gw::Component> water = gw::ripples(of("water"), 1000, 8, 0, 20, 16, 3);
  const std::vector<gw::Component> gel = gw::ripples(of("slime-gel"), 1030, 8, 0, 20, 16, 3);
  CHECK(water.size() == 16 && gel.size() == 16);
  double ew = 0, eg = 0;
  for (size_t i = 0; i < water.size(); ++i) {
    ew += water[i].a * water[i].a;
    eg += gel[i].a * gel[i].a;
  }
  CHECK(ew > 0);
  CHECK(eg < 0.01 * ew);
  // The shortest ripple runs faster than gravity alone would carry it (the capillary term).
  const gw::Component& c = water.back();
  CHECK(c.w > std::sqrt(gw::kGravity * c.k) * 1.05);
}

TEST(every_number_keeps_its_golden) {
  std::vector<std::vector<uint64_t>> got;
  for (const gw::NamedLiquid& l : gw::liquids()) {
    const gw::Liquid& q = l.stuff.liquid;
    got.push_back({ge::test::bits(keep_at(l.name.data(), 12, 0.3)),
                   ge::test::bits(keep_at(l.name.data(), 12, 30)),
                   ge::test::bits(gw::drift(q, 12, 1)), ge::test::bits(gw::drift(q, 25, 1)),
                   ge::test::bits(q.swim.speed), ge::test::bits(q.swim.friction),
                   ge::test::bits(q.swim.sink)});
  }
  ge::test::hold_golden("tests/golden/water_liquid.txt",
                        "per liquid: keep 0.3 m, keep 30 m, drift 12 and 25 m/s, swim speed "
                        "friction sink (water_liquid_test)",
                        got);
}
