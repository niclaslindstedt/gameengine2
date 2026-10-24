// engine/water/optics.hpp: white under daylight is white; pure water is blue and absorbs the red;
// each constituent tints the way it should (the humus amber, the plankton green, the flour
// turquoise, the mud brown); the shallows show the bed and the deep the water, monotonically
// between; daylight dims with depth, the red first; a target fades into the veil with distance;
// every look is found by name and seen within its field band; a body takes its look's optics;
// and every look's optics keep their bits in tests/golden/water_optics.txt.
//
// ge:summary Test: water colour: the observer, pure water, constituents, shallows, veil, looks
// ge:keywords test water optics colour absorption reflectance shallow bed veil visibility look
// ge:keywords golden
// ge:skill water testing
#include <cmath>
#include <string>
#include <vector>

#include "check.hpp"
#include "water/optics.hpp"
#include "water/world.hpp"
#include "water_golden.hpp"

namespace {

namespace gw = ge::water;

gw::Rgb deep(const gw::Constituents& c) {
  return gw::colour_of(gw::reflectance(gw::iops(c), c, INFINITY));
}

}  // namespace

TEST(white_under_daylight_is_white) {
  gw::Spectrum w;
  w.fill(1);
  const gw::Rgb c = gw::colour_of(w);
  CHECK(std::fabs(c.r - 1) < 0.01 && std::fabs(c.g - 1) < 0.01 && std::fabs(c.b - 1) < 0.01);
}

TEST(pure_water_absorbs_the_red_and_is_blue) {
  CHECK(gw::water_absorption(680) > 30 * gw::water_absorption(440));
  CHECK(std::fabs(gw::water_absorption(445) - 0.5 * (0.00635 + 0.00922)) < 1e-12);
  const gw::Optics o = gw::optics({});
  CHECK(o.a.r > o.a.g && o.a.g > o.a.b);
  CHECK(o.deep.b > 10 * std::fabs(o.deep.g) && o.deep.b > 10 * std::fabs(o.deep.r));  // deep blue
  CHECK(o.c.r == o.a.r + o.b.r || std::fabs(o.c.r - (o.a.r + o.b.r)) < 1e-12);
}

TEST(each_constituent_tints_the_water_its_way) {
  const gw::Rgb clear = deep({});
  const gw::Rgb humus = deep({.cdom = 5});
  CHECK(humus.r > humus.b && humus.g > humus.b);  // amber
  const gw::Rgb green = deep({.chlorophyll = 10});
  CHECK(green.g > green.b && green.g / green.b > clear.g / clear.b);
  const gw::Rgb flour = deep({.sediment = 3, .sediment_slope = 1.2});
  CHECK(flour.g > 10 * flour.r && flour.b > 10 * flour.r);  // turquoise
  CHECK(flour.g > 10 * std::fabs(clear.g) && flour.g + flour.b > 1.5 * (clear.g + clear.b));
  const gw::Rgb mud = deep({.cdom = 2, .sediment = 120, .sediment_scatter = 0.3});
  CHECK(mud.r > mud.g && mud.g > mud.b);  // brown
}

TEST(the_shallows_show_the_bed_and_the_deep_the_water) {
  const gw::Constituents& c = *gw::find_look("tropical-shallows");
  const gw::Iops in = gw::iops(c);
  double last = 1e9;
  for (const double depth : {0.01, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0}) {
    const gw::Rgb s = gw::colour_of(gw::reflectance(in, c, depth));
    CHECK(s.g < last);  // the white sand fades as it deepens
    last = s.g;
  }
  const gw::Rgb at_bed = gw::colour_of(gw::reflectance(in, c, 1e-6));
  // The bed through the surface: pi Rrs = 0.5 rho / (1 - 1.5 rho / pi) at the bed's albedo.
  CHECK(std::fabs(at_bed.g - 0.5 * 0.52 / (1 - 1.5 * 0.52 / 3.14159265358979)) < 0.05);
  const gw::Rgb far = gw::colour_of(gw::reflectance(in, c, 200));
  const gw::Rgb inf = deep(c);
  CHECK(std::fabs(far.b - inf.b) < 1e-4);
}

TEST(daylight_dims_with_depth_red_first) {
  const gw::Iops in = gw::iops(*gw::find_look("adriatic"));
  const gw::Spectrum e = gw::light_at(in, 10);
  CHECK(e[gw::kBands - 1] < 0.05 && e[5] > 0.5);  // 700 nm gone, 450 nm through
  const gw::Spectrum near = gw::under_view(in, 2, 1), far = gw::under_view(in, 2, 50),
                     veil = gw::under_view(in, 2, INFINITY);
  CHECK(near[10] > far[10] && std::fabs(far[10] - veil[10]) < 0.02 * near[10]);
}

TEST(every_look_is_found_and_seen_within_its_band) {
  CHECK(gw::find_look("no-such-water") == nullptr);
  CHECK(gw::looks().size() == 11);
  for (const gw::NamedLook& l : gw::looks()) {
    CHECK(gw::find_look(l.name) == &l.water);
    const gw::Optics o = gw::optics(l.water);
    CHECK(o.visibility >= l.seen[0] && o.visibility <= l.seen[1]);
  }
  CHECK(gw::optics(*gw::find_look("lava")).emission.r > 1);
}

TEST(a_body_takes_its_looks_optics) {
  gw::World world;
  const int sea = gw::add_ocean(world, 0);  // the adriatic by default
  const int pond = gw::add_ocean(world, -50, {.look = "nordic-brown"});
  gw::Stuff own{.look = "mine"};
  own.water = gw::Constituents{.chlorophyll = 20};
  const int mine = gw::add_ocean(world, -100, own);
  CHECK(world.bodies[sea].optics.visibility == gw::optics(*gw::find_look("adriatic")).visibility);
  CHECK(world.bodies[pond].optics.deep.r > world.bodies[pond].optics.deep.b);
  CHECK(world.bodies[mine].optics.visibility ==
        gw::optics(gw::Constituents{.chlorophyll = 20}).visibility);
}

TEST(every_look_keeps_its_golden) {
  std::vector<std::vector<uint64_t>> got;
  for (const gw::NamedLook& l : gw::looks()) {
    const gw::Optics o = gw::optics(l.water);
    got.push_back({ge::test::bits(o.c.r), ge::test::bits(o.c.g), ge::test::bits(o.c.b),
                   ge::test::bits(o.kd.g), ge::test::bits(o.deep.r), ge::test::bits(o.deep.g),
                   ge::test::bits(o.deep.b), ge::test::bits(o.veil.g),
                   ge::test::bits(o.visibility)});
  }
  ge::test::hold_golden("tests/golden/water_optics.txt",
                        "per look: c r g b, kd g, deep r g b, veil g, visibility "
                        "(water_optics_test)",
                        got);
}
