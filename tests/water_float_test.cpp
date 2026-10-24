// engine/water/float.hpp: a probe's submerged share; a box floats at its density's draught; its
// heave period meets 2 pi sqrt((m + ma) / (rho g A)) within 3 %; undamped it gains no energy and
// damped it loses some every swing; a body dropped in meets the slam and reports its splash; a
// planing sled skims at speed and slows into the water; a buoy rides the waves; and a floater's
// run keeps its bits in tests/golden/water_float.txt.
//
// ge:summary Test: floating: share, draught, heave period, energy, slam, splash, planing, golden
// ge:keywords test water float buoyancy draught heave period energy slam splash planing golden
// ge:skill water testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "water/float.hpp"
#include "water_golden.hpp"

namespace {

namespace gw = ge::water;
namespace gc = ge::core;
constexpr double kDt = 1.0 / 120;

gw::World still() {
  gw::World world;
  gw::add_ocean(world, 0, {});
  return world;
}

// A 2 x 1 x 2 m box as four slabs, one a quarter.
std::vector<gw::Probe> box(double drag, double linear, double added) {
  std::vector<gw::Probe> out;
  for (const double x : {-0.5, 0.5}) {
    for (const double z : {-0.5, 0.5}) {
      gw::Probe p;
      p.shape = gw::Shape::slab;
      p.x = x;
      p.z = z;
      p.radius = 0.5;
      p.volume = 1;
      p.area = 1;
      p.drag = drag;
      p.linear = linear;
      p.added = added;
      out.push_back(p);
    }
  }
  return out;
}

gw::Floater floater(double mass, double y) {
  gw::Floater f;
  f.mass = mass;
  f.inertia = {mass * 0.4, mass * 0.6, mass * 0.4};
  f.motion.p = {0, y, 0};
  return f;
}

}  // namespace

TEST(a_probes_share_follows_its_depth) {
  gw::Probe sphere;
  sphere.radius = 0.5;
  CHECK(gw::share_of(sphere, -0.5) == 0);
  CHECK(gw::share_of(sphere, -1) == 0);
  CHECK(std::fabs(gw::share_of(sphere, 0) - 0.5) < 1e-12);
  CHECK(gw::share_of(sphere, 0.5) == 1);
  CHECK(gw::share_of(sphere, -0.25) < 0.25);  // a cap fills slower than a slab
  gw::Probe slab = sphere;
  slab.shape = gw::Shape::slab;
  CHECK(std::fabs(gw::share_of(slab, -0.25) - 0.25) < 1e-12);
  CHECK(gw::share_of(slab, 2) == 1);
}

TEST(a_box_floats_at_its_densitys_draught) {
  const gw::World world = still();
  const auto probes = box(1, 400, 0.5);
  for (const double density : {250.0, 500.0, 750.0}) {
    gw::Floater f = floater(density * 4, 1);
    for (int i = 0; i < 120 * 60; ++i) gw::step_floater(world, probes, f, i * kDt, kDt);
    const double draught = 0.5 - f.motion.p.y;  // from the bottom face
    CHECK(std::fabs(draught - density / 1000) < 0.005);
    CHECK(std::fabs(f.motion.v.y) < 1e-3);
  }
  // Denser than the water: it sinks.
  gw::Floater stone = floater(1200 * 4, 0);
  for (int i = 0; i < 120 * 10; ++i) gw::step_floater(world, probes, stone, i * kDt, kDt);
  CHECK(stone.motion.p.y < -5);
}

TEST(the_heave_period_meets_theory) {
  const gw::World world = still();
  for (const double added : {0.0, 0.5}) {
    const auto probes = box(0, 0, added);
    const double mass = 2000;  // floats half under: the waterplane is 4 m^2
    gw::Floater f = floater(mass, 0.1);
    double last = f.motion.p.y;
    double first = -1, prev = -1;
    int crossings = 0;
    double hi = 0;
    for (int i = 1; i <= 120 * 60; ++i) {
      gw::step_floater(world, probes, f, i * kDt, kDt);
      const double y = f.motion.p.y;
      hi = std::fmax(hi, std::fabs(y));
      if (last > 0 && y <= 0) {
        const double tc = i * kDt;
        if (first < 0) first = tc;
        prev = tc;
        ++crossings;
      }
      last = y;
    }
    const double period = (prev - first) / (crossings - 1);
    // The added mass of the half-submerged box, as float_push reports it.
    const double ma = added * 1000 * 4 * 0.5;
    const double theory = 6.283185307179586 * std::sqrt((mass + ma) / (1000 * gw::kGravity * 4));
    CHECK(std::fabs(period / theory - 1) < 0.03);
    // No energy gained: the swing never grows past its start.
    CHECK(hi <= 0.1 * 1.005);
  }
}

TEST(drag_takes_energy_out_every_swing) {
  const gw::World world = still();
  const auto probes = box(1, 100, 0.5);
  gw::Floater f = floater(2000, 0.3);
  double last_peak = 1e9, prev = f.motion.p.y, prev2 = prev;
  int peaks = 0;
  for (int i = 1; i <= 120 * 20; ++i) {
    gw::step_floater(world, probes, f, i * kDt, kDt);
    const double y = f.motion.p.y;
    if (prev > prev2 && prev >= y && prev > 1e-4) {
      CHECK(prev < last_peak);
      last_peak = prev;
      ++peaks;
    }
    prev2 = prev;
    prev = y;
  }
  CHECK(peaks >= 2);
}

TEST(a_body_dropped_in_meets_the_slam_and_splashes) {
  const gw::World world = still();
  gw::Probe ball;
  ball.radius = 0.3;
  ball.drag = 0.5;
  const std::vector<gw::Probe> probes = {ball};
  gw::Motion m;
  m.p = {0, 0.31, 0};
  m.v = {0, -6, 0};
  gw::FloatState state;
  gw::Push p = gw::float_push(world, probes, m, 0, state);
  CHECK(p.wet == 0 && p.splashes == 0);
  m.p.y = 0.2;  // a tenth of a metre in
  p = gw::float_push(world, probes, m, kDt, state);
  CHECK(p.splashes == 1 && p.splash[0].entering);
  CHECK(std::fabs(p.splash[0].speed - 6) < 1e-9);
  CHECK(p.splash[0].size == 0.3);
  // The same depth at a crawl feels buoyancy and little else; at 6 m/s the slam and the drag
  // dwarf it.
  gw::FloatState calm;
  calm.share[0] = 0.01;
  gw::Motion crawl = m;
  crawl.v = {0, -0.01, 0};
  const double buoyant = gw::float_push(world, probes, crawl, kDt, calm).force.y;
  CHECK(p.force.y > 5 * buoyant);
  // Out again: a leaving splash.
  m.p.y = 1;
  m.v = {0, 3, 0};
  p = gw::float_push(world, probes, m, 2 * kDt, state);
  CHECK(p.splashes == 1 && !p.splash[0].entering);
}

TEST(a_planing_sled_skims_at_speed_and_slows_into_the_water) {
  const gw::World world = still();
  // A sled and its rider, 300 kg, trimmed 5 degrees nose up, two plates under it.
  std::vector<gw::Probe> probes;
  for (const double z : {-0.8, 0.8}) {
    gw::Probe p;
    p.shape = gw::Shape::slab;
    p.z = z;
    p.y = -0.15;
    p.radius = 0.15;
    p.volume = 0.12;
    p.area = 0.2;
    p.drag = 0.3;
    p.plate = 0.4;
    probes.push_back(p);
  }
  const auto run = [&](double speed, double& skim_share, double& sunk_at) {
    gw::Floater f;
    f.mass = 300;
    f.inertia = {1e9, 1e9, 1e9};  // held at its trim
    f.motion.q = gc::from_axis_angle(1, 0, 0, -5 * 3.141592653589793 / 180);
    f.motion.p = {0, 0.3, 0};
    f.motion.v = {0, 0, speed};
    skim_share = 0;
    sunk_at = -1;
    double distance = 0;
    for (int i = 1; i <= 120 * 30; ++i) {
      gw::step_floater(world, probes, f, i * kDt, kDt);
      if (i == 120) skim_share = f.state.share[0] + f.state.share[1];
      if (sunk_at < 0 && f.motion.p.y < -1) sunk_at = i * kDt;
      distance = f.motion.p.z;
    }
    return distance;
  };
  double fast_share = 0, fast_sunk = 0, slow_share = 0, slow_sunk = 0;
  const double fast = run(22, fast_share, fast_sunk);
  const double slow = run(5, slow_share, slow_sunk);
  CHECK(fast > 3 * slow);
  CHECK(fast_share < 0.6);  // a second in, the fast sled rides high on its plates
  CHECK(slow_sunk > 0 && slow_sunk < 8);
  CHECK(fast_sunk < 0 || fast_sunk > slow_sunk + 1);
}

TEST(a_buoy_rides_the_waves) {
  ge::wind::Field wind;
  wind.mean = 10;
  gw::World world(wind);
  gw::add_ocean(world, 0, {}, {}, 2e4);
  gw::Probe ball;
  ball.radius = 0.25;
  ball.drag = 0.6;
  ball.linear = 5;
  const std::vector<gw::Probe> probes = {ball};
  gw::Floater f;
  f.mass = 300 * 4.0 / 3 * 3.141592653589793 * 0.25 * 0.25 * 0.25;
  f.inertia = {1, 1, 1};
  double sum2 = 0, surf2 = 0, sum = 0, surf = 0;
  const int n = 120 * 300;
  for (int i = 0; i < n; ++i) {
    gw::step_floater(world, probes, f, i * kDt, kDt);
    if (i < 120 * 30) continue;
    const double s = gw::sample(world, f.motion.p.x, 0, f.motion.p.z, (i + 1) * kDt).surface;
    sum += f.motion.p.y;
    sum2 += f.motion.p.y * f.motion.p.y;
    surf += s;
    surf2 += s * s;
  }
  const double m = n - 120 * 30;
  const double sd = std::sqrt(sum2 / m - (sum / m) * (sum / m));
  const double ssd = std::sqrt(surf2 / m - (surf / m) * (surf / m));
  CHECK(std::fabs(sd / ssd - 1) < 0.2);
  // It drifts a little with the waves and stays near where it was put.
  CHECK(std::fabs(f.motion.p.x) < 30 && std::fabs(f.motion.p.z) < 30);
}

TEST(every_number_keeps_its_golden) {
  ge::wind::Field wind;
  wind.mean = 8;
  wind.from = 1.2;
  gw::World world(wind);
  world.seed = 38;
  gw::add_ocean(world, 0, {}, {}, 5e3);
  const auto probes = box(1, 50, 0.5);
  gw::Floater f = floater(1500, 0.4);
  f.motion.w = {0.2, 0, 0.1};
  std::vector<std::vector<uint64_t>> got;
  for (int i = 0; i < 120 * 20; ++i) {
    const gw::Push p = gw::step_floater(world, probes, f, i * kDt, kDt);
    if (i % 240 == 0) {
      got.push_back({ge::test::bits(f.motion.p.x), ge::test::bits(f.motion.p.y),
                     ge::test::bits(f.motion.p.z), ge::test::bits(f.motion.q.x),
                     ge::test::bits(f.motion.q.w), ge::test::bits(p.force.y),
                     ge::test::bits(p.torque.x), ge::test::bits(p.submerged)});
    }
  }
  ge::test::hold_golden("tests/golden/water_float.txt",
                        "every 2 s of a box afloat: p.x p.y p.z q.x q.w force.y torque.x "
                        "submerged (water_float_test)",
                        got);
}
