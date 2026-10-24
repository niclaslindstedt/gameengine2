// engine/water/wake.hpp: the rings keep their life and their count; a ring's waves lengthen
// toward its front at the deep-water group speed; a hull's trail opens at Kelvin's angle (19.47
// degrees, within 2) where the hull makes no waves much shorter than itself; the rings ride on a
// body's surface in `sample` and rock a float; a splash sets off a ring; and the sum keeps its
// bits in tests/golden/water_wake.txt.
//
// ge:summary Test: wakes: ring life and count, group speed, Kelvin's angle, sample, splash, golden
// ge:keywords test water wake ring kelvin group-speed trail sample splash golden
// ge:skill water testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "water/float.hpp"
#include "water/wake.hpp"
#include "water_golden.hpp"

namespace {

namespace gw = ge::water;
constexpr double kPi = 3.14159265358979323846;
constexpr double kDt = 1.0 / 120;

// A hull `length` m long run up +z at `speed` m/s for long enough to stand `reach` transverse
// wavelengths of wake behind it.
gw::Wakes trail(double length, double speed, double reach, double& t, double& z) {
  gw::Wakes w;
  w.life = 80;
  gw::Trail tr;
  const double lambda = 2 * kPi * speed * speed / gw::kGravity;
  const int steps = static_cast<int>((reach * lambda / speed + 10) / kDt);
  for (int i = 0; i <= steps; ++i) {
    t = i * kDt;
    gw::wake_trail(w, tr, 0, 0, speed * t, 0, speed, length, 1, 0.3, t);
  }
  z = speed * t;
  return w;
}

// The arm's angle: on each line across the wake `d` m behind the hull, where the waves are
// highest (their envelope over half a wavelength along the track); the angle of the straight
// line fitted through those points.
double arm_angle(const gw::Wakes& w, double speed, double t, double z) {
  const double lambda = 2 * kPi * speed * speed / gw::kGravity;
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  int n = 0;
  for (int m = 6; m <= 12; m += 2) {
    const double d = m * lambda;
    double best = 0, at = 0;
    for (int i = 0; 0.2 + i * lambda / 50 < 0.6 * d; ++i) {
      const double x = 0.2 + i * lambda / 50;
      double h = 0;
      for (int j = -4; j <= 4; ++j) {
        const double dz = j * lambda / 8;
        h = std::fmax(h, std::fabs(gw::wake_at(w, 0, x, z - d + dz, t).height));
      }
      if (h > best) {
        best = h;
        at = x;
      }
    }
    sx += d;
    sy += at;
    sxx += d * d;
    sxy += d * at;
    ++n;
  }
  const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
  return std::atan(slope) * 180 / kPi;
}

}  // namespace

TEST(rings_keep_their_life_and_their_count) {
  gw::Wakes w;
  w.life = 10;
  for (int i = 0; i < 40; ++i) gw::emit_ring(w, {.x = 0, .z = 0, .t0 = i * 1.0, .volume = 1});
  CHECK(w.rings.size() == 10);
  CHECK(w.rings.front().t0 == 30);
  w.life = 1e9;
  for (int i = 0; i < gw::kMaxRings + 50; ++i) gw::emit_ring(w, {.t0 = 100.0 + i, .volume = 1});
  CHECK(w.rings.size() == static_cast<size_t>(gw::kMaxRings));
  CHECK(w.rings.back().t0 == 100.0 + gw::kMaxRings + 49);
  // An old ring is faded out to nothing at the end of its life.
  gw::Wakes one;
  one.life = 10;
  const gw::Ring r{.volume = 1, .size = 0.1};
  CHECK(std::fabs(gw::ring_height(one, r, 20, 10.0)) < 1e-12);
  CHECK(gw::ring_height(one, r, 20, -1) == 0);
}

TEST(a_rings_waves_run_at_the_group_speed) {
  // Where a ring's local wave is lambda long, it has come out at the group speed of that wave,
  // r = 1/2 sqrt(g / k) tau: read off the zero crossings round that radius.
  gw::Wakes w;
  w.shortest = 0.01;
  const gw::Ring r{.volume = 1, .size = 0.01};
  const double tau = 20, lambda = 4, k = 2 * kPi / lambda;
  const double at = 0.5 * std::sqrt(gw::kGravity / k) * tau;
  std::vector<double> zeros;
  double prev = gw::ring_height(w, r, at - 2 * lambda, tau);
  for (int j = 0; j < static_cast<int>(4 * lambda / 0.001); ++j) {
    const double x = at - 2 * lambda + j * 0.001;
    const double h = gw::ring_height(w, r, x, tau);
    if ((h > 0) != (prev > 0)) zeros.push_back(x);
    prev = h;
  }
  CHECK(zeros.size() >= 6);
  // A whole wavelength centred on the radius: the crossings either side of the one nearest it.
  size_t i = 1;
  for (size_t j = 1; j + 1 < zeros.size(); ++j)
    if (std::fabs(zeros[j] - at) < std::fabs(zeros[i] - at)) i = j;
  const double measured = zeros[i + 1] - zeros[i - 1];
  CHECK_MSG(std::fabs(measured - lambda) < 0.05 * lambda,
            "the ring's wave at its group-speed radius is " + std::to_string(measured) +
                " m long, not 4 m (water/wake.cpp's phase)");
}

TEST(a_hulls_trail_opens_at_kelvins_angle) {
  double t = 0, z = 0;
  const gw::Wakes w = trail(4, 3, 14, t, z);
  const double angle = arm_angle(w, 3, t, z);
  CHECK_MSG(std::fabs(angle - 19.47) < 2,
            "a 4 m hull at 3 m/s leaves an arm at " + std::to_string(angle) +
                " degrees, not Kelvin's 19.47 (the float lab's wake table; skill: water)");
}

TEST(the_rings_ride_on_the_surface_and_rock_a_float) {
  gw::World world;
  gw::add_ocean(world, 0, {});
  const gw::Sample calm = gw::sample(world, 30, 0, 0, 12);
  gw::emit_ring(world.wakes, {.x = 0, .z = 0, .t0 = 0, .volume = 2, .size = 0.5, .body = 0});
  const gw::Sample s = gw::sample(world, 30, 0, 0, 12);
  const gw::WakeAt at = gw::wake_at(world.wakes, 0, 30, 0, 12);
  CHECK(at.height != 0);
  CHECK_BITS(s.surface, calm.surface + at.height);
  CHECK(s.vx != calm.vx);
  // A ring on another body is not on this one.
  gw::World other;
  gw::add_ocean(other, 0, {});
  gw::emit_ring(other.wakes, {.volume = 2, .body = 1});
  CHECK_BITS(gw::sample(other, 30, 0, 0, 12).surface, calm.surface);

  // A buoy 20 m off heaves as a dropped crate's ring goes by.
  gw::World still;
  gw::add_ocean(still, 0, {});
  gw::Probe p;
  p.radius = 0.4;
  gw::Floater buoy;
  buoy.mass = 0.5 * 1000 * p.radius * p.radius * p.radius * 4 / 3 * kPi;
  buoy.motion.p = {20, 0, 0};
  const gw::Splash crate{.x = 0, .y = 0, .z = 0, .speed = 6, .size = 0.8};
  gw::splash_ring(still.wakes, 0, crate, 0);
  CHECK(still.wakes.rings.size() == 1);
  double lo = 1e9, hi = -1e9;
  for (int i = 0; i < 120 * 30; ++i) {
    gw::step_floater(still, {&p, 1}, buoy, i * kDt, kDt);
    if (i > 120 * 2) {
      lo = std::fmin(lo, buoy.motion.p.y);
      hi = std::fmax(hi, buoy.motion.p.y);
    }
  }
  CHECK_MSG(hi - lo > 0.01, "the buoy heaved " + std::to_string(hi - lo) + " m in the ring");
}

TEST(every_number_keeps_its_golden) {
  double t = 0, z = 0;
  const gw::Wakes w = trail(4, 3, 4, t, z);
  std::vector<std::vector<uint64_t>> got;
  for (int i = 0; i < 5; ++i) {
    const double d = 5 + 10.0 * i;
    for (int j = 0; j < 4; ++j) {
      const double x = 4.0 * j;
      const gw::WakeAt a = gw::wake_at(w, 0, x, z - d, t);
      got.push_back({ge::test::bits(a.height), ge::test::bits(a.dx), ge::test::bits(a.dz),
                     ge::test::bits(a.u), ge::test::bits(a.v), ge::test::bits(a.w)});
    }
  }
  ge::test::hold_golden("tests/golden/water_wake.txt",
                        "a 4 m hull's wake at 3 m/s, across it 5 to 45 m behind: height dx dz "
                        "u v w (water_wake_test)",
                        got);
}
