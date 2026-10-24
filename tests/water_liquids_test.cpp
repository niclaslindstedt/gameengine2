// engine/water/liquids.hpp: every row is found by its name, carries a look the optics knows and
// sane numbers; an unknown name lays water; and laying the liquids into a world does what their
// numbers say: a lava volume grows no waves in a gale while a water volume beside it does, a
// thin slime drifts in the gale while a gel does not, a swimmer floats high on lava and is held
// fast in tar, and water's bodies keep their bits (every older water golden is untouched).
//
// ge:summary Test: the named liquids: rows, laid in a world: waves, drift, floating, held fast
// ge:keywords test liquids lava slime tar gel lay_waves volume drift float held-fast
// ge:skill water testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "water/liquids.hpp"
#include "water/swim.hpp"

namespace {

namespace gw = ge::water;
constexpr double kDt = 1.0 / 120;

// A box 20 x 20 m, 3 m deep, its top at y = 0.
std::vector<gw::Hull> pit(double x0) {
  gw::Hull h;
  h.planes = {{0, 1, 0, 0},    {0, -1, 0, 3}, {1, 0, 0, x0 + 20},
              {-1, 0, 0, -x0}, {0, 0, 1, 20}, {0, 0, -1, 0}};
  return {h};
}

gw::World gale(double wind) {
  ge::wind::Field f;
  f.mean = wind;
  f.from = 0;
  return gw::World(f);
}

}  // namespace

TEST(every_row_is_named_and_sane) {
  CHECK(gw::liquids().size() >= 9);
  for (const gw::NamedLiquid& l : gw::liquids()) {
    CHECK(gw::find_liquid(l.name) == &l);
    CHECK(l.stuff.water || gw::find_look(l.stuff.look) != nullptr);
    CHECK(l.stuff.density > 900 && l.stuff.density < 3200);
    CHECK(l.stuff.liquid.viscosity > 0);
    CHECK(l.stuff.liquid.yield >= 0);
    CHECK(!l.what.empty());
  }
  CHECK(gw::find_liquid("mercury") == nullptr);
  CHECK(gw::liquid("mercury").density == 1000);
}

TEST(lava_grows_no_waves_where_water_does) {
  gw::World world = gale(20);
  const int water = gw::add_volume(world, pit(0), gw::liquid("water"));
  const int lava = gw::add_volume(world, pit(100), gw::liquid("lava"));
  gw::lay_waves(world, water, 200);
  gw::lay_waves(world, lava, 200);
  CHECK(world.bodies[water].waves.m0 > 0);
  CHECK(world.bodies[lava].waves.m0 == 0);
  // The water's surface moves under the gale; the lava's stays at its level.
  double spread_w = 0, spread_l = 0;
  for (int i = 0; i < 50; ++i) {
    const double t = i * 0.37;
    spread_w = std::fmax(spread_w, std::fabs(gw::sample(world, 10, -1, 10, t).surface));
    spread_l = std::fmax(spread_l, std::fabs(gw::sample(world, 110, -1, 10, t).surface));
  }
  CHECK(spread_w > 0.05);
  CHECK(spread_l == 0);
  // The sample carries the liquid's numbers.
  const gw::Sample s = gw::sample(world, 110, -1, 10, 0);
  CHECK(s.viscosity == 400);
  CHECK(s.density == 2600);
}

TEST(a_thin_slime_drifts_in_a_gale_and_a_gel_does_not) {
  gw::World world = gale(18);
  const int slime = gw::add_volume(world, pit(0), gw::liquid("slime"));
  const int gel = gw::add_volume(world, pit(100), gw::liquid("slime-gel"));
  gw::lay_waves(world, slime, 20);
  gw::lay_waves(world, gel, 20);
  const gw::Body& b = world.bodies[slime];
  const double d = std::hypot(b.drift[0], b.drift[1]);
  CHECK(d > 0.01 && d < 0.03 * 18);  // under water's, the viscosity holding it back
  CHECK(world.bodies[gel].drift[0] == 0 && world.bodies[gel].drift[1] == 0);
}

TEST(a_swimmer_floats_high_on_lava_and_is_held_fast_in_tar) {
  gw::World world;
  gw::add_volume(world, pit(0), gw::liquid("lava"));
  gw::add_volume(world, pit(100), gw::liquid("slime-tar"));
  const gw::SwimSpec spec = gw::default_swimmer();
  // Dropped into the lava up to his chest, he rises until about two fifths of him is in.
  gw::SwimState lava;
  lava.p = {10, -1.2, 10};
  for (int i = 0; i < 120 * 20; ++i) gw::step_swimmer(world, spec, {}, lava, i * kDt, kDt);
  CHECK(lava.p.y > -0.9 && lava.p.y < -0.4);
  // In tar, swimming flat out for ten seconds gets him almost nowhere.
  gw::SwimState tar;
  tar.p = {110, -1.0, 10};
  for (int i = 0; i < 120 * 10; ++i) gw::step_swimmer(world, spec, {1, 0, 0}, tar, i * kDt, kDt);
  CHECK(std::fabs(tar.p.x - 110) < 0.1);
}
