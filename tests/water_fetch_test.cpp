// engine/water/fetch.hpp: the outline's inside, the run to its shore, the effective fetch (longer
// at the lee shore than the windward one, shorter in a narrow body than its length) and the baked
// grid reading back the measure at its nodes.
//
// ge:summary Test: the fetch over a body's outline: inside, to the shore, effective fetch, the grid
// ge:keywords test water fetch outline inside shore effective-fetch grid lee windward
// ge:skill water testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "water/fetch.hpp"

namespace {

namespace gw = ge::water;
constexpr double kPi = 3.141592653589793;

// A rectangle 2000 m along z and `wide` m along x, centred on the origin.
std::vector<gw::Point> long_lake(double wide) {
  return {{-wide / 2, -1000}, {wide / 2, -1000}, {wide / 2, 1000}, {-wide / 2, 1000}};
}

}  // namespace

TEST(the_inside_and_the_run_to_the_shore) {
  const auto lake = long_lake(400);
  CHECK(gw::inside(lake, 0, 0));
  CHECK(gw::inside(lake, 199, 999));
  CHECK(!gw::inside(lake, 201, 0));
  CHECK(!gw::inside(lake, 0, -1001));
  // Heading +z (0) from the middle: 1000 m to the far shore; +x (pi / 2): 200 m.
  CHECK(std::fabs(gw::to_shore(lake, 0, 0, 0) - 1000) < 1e-9);
  CHECK(std::fabs(gw::to_shore(lake, 0, 0, kPi / 2) - 200) < 1e-9);
  CHECK(std::fabs(gw::to_shore(lake, 0, 500, kPi) - 1500) < 1e-9);
  CHECK(gw::to_shore(lake, 500, 0, 0) == 0);
}

TEST(the_lee_shore_has_the_longer_fetch) {
  const auto lake = long_lake(2000);
  // A wind blowing toward +z: the fetch grows toward the +z shore.
  const double windward = gw::effective_fetch(lake, 0, -900, 0);
  const double mid = gw::effective_fetch(lake, 0, 0, 0);
  const double lee = gw::effective_fetch(lake, 0, 900, 0);
  CHECK(windward < mid && mid < lee);
  CHECK(windward < 150);
  CHECK(lee > 1300 && lee < 1900);
}

TEST(a_narrow_body_gives_the_wind_a_shorter_run) {
  // Straight up the middle both have 1900 m; the narrow one's rays reach its sides first.
  const double wide = gw::effective_fetch(long_lake(2000), 0, 900, 0);
  const double narrow = gw::effective_fetch(long_lake(200), 0, 900, 0);
  CHECK(narrow < 0.5 * wide);
}

TEST(the_grid_reads_back_the_fetch_at_its_nodes) {
  const auto lake = long_lake(600);
  const gw::FetchGrid g = gw::bake_fetch(lake, kPi, 32);
  CHECK(g.nx > 2 && g.nz > 30);
  double longest = 0;
  for (int k = 0; k < g.nz; k += 3) {
    for (int i = 0; i < g.nx; i += 2) {
      const double x = g.x0 + i * g.cell, z = g.z0 + k * g.cell;
      const double want = gw::effective_fetch(lake, x, z, kPi);
      CHECK(std::fabs(gw::fetch_at(g, x, z) - want) < 1e-3 * (1 + want));
      longest = std::fmax(longest, want);
    }
  }
  CHECK(g.longest >= longest);
  // A wind toward -z: the -z end is the lee.
  CHECK(gw::fetch_at(g, 0, -900) > gw::fetch_at(g, 0, 900));
}
