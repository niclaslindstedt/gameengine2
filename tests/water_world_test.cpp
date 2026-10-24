// engine/water/world.hpp: each kind of body answers the one query: a lake inside its shore and
// rougher at its lee, an ocean wherever its bed is under its level, a river with its current
// fastest mid-stream and carrying its discharge, a volume in its hulls; the point above the
// surface reads a negative depth; the batch is the single query many times; and every number keeps
// its bits in tests/golden/water_world.txt.
//
// ge:summary Test: bodies of water and the one sample query, every kind; golden
// ge:keywords test water world lake ocean river channel volume hull sample current depth golden
// ge:skill water testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "water/world.hpp"
#include "water_golden.hpp"

namespace {

namespace gw = ge::water;

ge::wind::Field breeze(double mean, double from) {
  ge::wind::Field f;
  f.mean = mean;
  f.from = from;
  return f;
}

std::vector<gw::Point> square(double half) {
  return {{-half, -half}, {half, -half}, {half, half}, {-half, half}};
}

double shelf(const void*, double x, double) { return -2 - 0.01 * x; }  // deepening toward +x

// The height's spread over time at a point.
double rough(const gw::World& world, double x, double z) {
  double sum = 0, sum2 = 0;
  const int n = 4000;
  for (int i = 0; i < n; ++i) {
    const double h = gw::sample(world, x, 0, z, i * 0.1).surface;
    sum += h;
    sum2 += h * h;
  }
  return std::sqrt(sum2 / n - (sum / n) * (sum / n));
}

gw::Hull box(double x0, double y0, double z0, double x1, double y1, double z1) {
  gw::Hull h;
  h.planes = {{1, 0, 0, x1},   {-1, 0, 0, -x0}, {0, 1, 0, y1},
              {0, -1, 0, -y0}, {0, 0, 1, z1},   {0, 0, -1, -z0}};
  return h;
}

}  // namespace

TEST(a_lake_is_water_inside_its_shore_and_rougher_at_its_lee) {
  // A wind from +z (heading 0) blows toward -z: the -z shore is the lee.
  gw::World world(breeze(12, 0));
  const auto shore = square(1500);
  CHECK(gw::add_lake(world, shore, 400) == 0);
  const gw::Sample in = gw::sample(world, 0, 398, 0, 3);
  CHECK(in.body == 0);
  CHECK(in.depth > 0 && std::fabs(in.surface - 400) < 2);
  CHECK(std::fabs(in.bed - 395) < 1e-12);
  CHECK(in.density == 1000);
  const gw::Sample above = gw::sample(world, 0, 410, 0, 3);
  CHECK(above.body == 0 && above.depth < 0);
  CHECK(gw::sample(world, 1600, 398, 0, 3).body == -1);
  CHECK(rough(world, 0, -1400) > 2 * rough(world, 0, 1400));
  // The normal is a unit vector leaning off the slopes.
  CHECK(std::fabs(in.nx * in.nx + in.ny * in.ny + in.nz * in.nz - 1) < 1e-12);
}

TEST(an_ocean_is_wherever_its_bed_is_under_its_level) {
  gw::World world(breeze(10, 1.0));
  gw::add_ocean(world, 0, {.look = "adriatic", .density = 1025}, {.height = shelf});
  // Bed at -2 - 0.01 x: wet where x > -200.
  const gw::Sample deep = gw::sample(world, 1000, -1, 50, 0);
  CHECK(deep.body == 0 && deep.density == 1025);
  CHECK(std::fabs(deep.bed + 12) < 1e-12);
  CHECK(gw::sample(world, -300, -1, 50, 0).body == -1);
  // In the shallows the waves are held under the depth.
  const gw::Sample near = gw::sample(world, -150, 0, 0, 0);
  CHECK(near.body == 0);
  double hi = 0;
  for (int i = 0; i < 2000; ++i)
    hi = std::fmax(hi, std::fabs(gw::sample(world, -195, 0, 0, i * 0.1).surface));
  CHECK(hi < 0.1);
}

TEST(a_river_runs_fastest_mid_stream_and_carries_its_discharge) {
  gw::World world(breeze(0, 0));
  const gw::ChannelPoint line[] = {{0, 0, 100, 10, 2}, {0, 200, 98, 10, 2}, {50, 300, 97, 10, 2}};
  gw::add_river(world, line, 20);
  const gw::Sample mid = gw::sample(world, 0, 99, 100, 0);
  CHECK(mid.body == 0);
  CHECK(std::fabs(mid.surface - 99) < 1e-9);  // still air: no ripple
  CHECK(mid.vz > 0 && std::fabs(mid.vx) < 1e-12);
  CHECK(mid.vy < 0);  // the surface falls downstream
  const gw::Sample bank = gw::sample(world, 4, 99, 100, 0);
  CHECK(bank.vz > 0 && bank.vz < 0.5 * mid.vz);
  CHECK(gw::sample(world, 6, 99, 100, 0).body == -1);
  // The section's flow: the speed times the depth across it, summed over the width, is Q.
  double q = 0;
  const int n = 2000;
  for (int i = 0; i < n; ++i) {
    const double x = -5 + (i + 0.5) * 10.0 / n;
    const gw::Sample s = gw::sample(world, x, 99, 100, 0);
    q += s.vz * (s.surface - s.bed) * 10.0 / n;
  }
  CHECK(std::fabs(q - 20) < 0.05);
  // Past the bend the current turns with the channel.
  const gw::Sample after = gw::sample(world, 25, 97.5, 250, 0);
  CHECK(after.body == 0 && after.vx > 0 && after.vz > 0);
}

TEST(a_volume_is_water_in_its_hulls) {
  gw::World world;
  const gw::Hull hulls[] = {box(0, -64, 0, 128, 0, 128)};
  gw::add_volume(world, hulls, {.look = "slime", .density = 1100});
  const gw::Sample in = gw::sample(world, 10, -20, 10, 0);
  CHECK(in.body == 0 && in.depth == 20 && in.surface == 0 && in.density == 1100);
  CHECK(in.bed == -64);
  const gw::Sample above = gw::sample(world, 10, 5, 10, 0);
  CHECK(above.body == 0 && above.depth == -5);
  CHECK(gw::sample(world, 10, -70, 10, 0).body == -1);
  CHECK(gw::sample(world, -1, -20, 10, 0).body == -1);
}

TEST(the_point_under_a_surface_wins_over_one_above) {
  gw::World world;
  const gw::Hull low[] = {box(0, -10, 0, 10, 0, 10)};
  const gw::Hull high[] = {box(0, 20, 0, 10, 30, 10)};
  gw::add_volume(world, low);
  gw::add_volume(world, high);
  CHECK(gw::sample(world, 5, 25, 5, 0).body == 1);
  CHECK(gw::sample(world, 5, -5, 5, 0).body == 0);
  // Between the two, the nearer surface under the point: the low one's.
  const gw::Sample gap = gw::sample(world, 5, 10, 5, 0);
  CHECK(gap.body == 0 && gap.depth == -10);
}

TEST(the_batch_is_the_single_query_many_times) {
  gw::World world(breeze(9, 2.0));
  gw::add_lake(world, square(300), 10);
  const std::vector<double> xyz = {0, 9, 0, 100, 9, -50, 400, 9, 0, -20, 11, 20};
  std::vector<gw::Sample> out(4);
  gw::sample(world, xyz, 7.5, out);
  for (size_t i = 0; i < 4; ++i) {
    const gw::Sample one = gw::sample(world, xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2], 7.5);
    CHECK(out[i].body == one.body);
    CHECK_BITS(out[i].surface, one.surface);
  }
}

TEST(every_number_keeps_its_golden) {
  gw::World world(breeze(11, 0.8));
  world.seed = 38;
  const gw::Point shore[] = {{-400, -300}, {500, -350}, {600, 400}, {-300, 500}, {-500, 100}};
  gw::add_lake(world, shore, 20);
  const gw::ChannelPoint line[] = {{1000, 0, 30, 12, 2.5}, {1000, 300, 25, 8, 1.5}};
  gw::add_river(world, line, 30);
  gw::add_ocean(world, -5);
  std::vector<std::vector<uint64_t>> got;
  const double points[][3] = {{0, 19, 0}, {450, 19, 300}, {1002, 27, 150}, {3000, -6, 0}};
  for (const auto& p : points) {
    for (const double t : {0.0, 12.5, 300.0}) {
      const gw::Sample s = gw::sample(world, p[0], p[1], p[2], t);
      got.push_back({static_cast<uint64_t>(s.body + 1), ge::test::bits(s.surface),
                     ge::test::bits(s.nx), ge::test::bits(s.nz), ge::test::bits(s.vx),
                     ge::test::bits(s.vy), ge::test::bits(s.vz), ge::test::bits(s.depth)});
    }
  }
  ge::test::hold_golden("tests/golden/water_world.txt",
                        "per point and moment: body+1 surface nx nz vx vy vz depth "
                        "(water_world_test)",
                        got);
}
