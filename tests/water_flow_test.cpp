// engine/water/flow.hpp: still water over a rough bed stays still; a dam break on a dry bed
// meets Ritter's solution; a drain empties by Torricelli's law and a breach by the weir's power
// of the head; the water is held to the quantum through sources, drains, open edges, waterfalls
// and every change of tier; a waterfall's sheet lands beyond its lip; the rows give the same bits
// on one thread and on eight; the tiers follow the actors with hysteresis; `sample` reads a
// region's depth and current; and a breach keeps its bits in tests/golden/water_flow.txt.
//
// ge:summary Test: flowing water: rest, Ritter, Torricelli, the weir, conservation, falls, tiers
// ge:keywords test water flow shallow-water dam-break ritter torricelli weir breach conservation
// ge:keywords waterfall threads tier lod sleep golden
// ge:skill water testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "core/libm/libm.hpp"
#include "water/flow.hpp"
#include "water/world.hpp"
#include "water_golden.hpp"

namespace {

namespace gw = ge::water;
constexpr double kDt = 1.0 / 60;
constexpr double kG = 9.80665;

double bumps(const void*, double x, double z) {
  return 0.3 * ge::core::libm::sin(0.7 * x) * ge::core::libm::cos(0.5 * z) + 0.002 * x;
}

// The water in cells [i0, i1) of every row, m^3.
double held(const gw::Flow& f, int i0, int i1) {
  const gw::FlowGrid& g = f.grids[f.tier];
  double v = 0;
  for (int k = 0; k < g.nz; ++k)
    for (int i = i0; i < i1; ++i) v += static_cast<double>(g.vol[k * g.nx + i]) * gw::kQuantum;
  return v;
}

// A basin 20 m across behind a wall with a gap `gap` m wide, the floor beyond open to the east.
gw::Flow breach(double gap, int workers = 1) {
  gw::Flow f =
      gw::make_flow({.nx = 64, .nz = 80, .cell = 0.5, .open = gw::kEast, .workers = workers});
  gw::set_bed(f, 10, 0, 10.5, 40, 3);
  gw::fill_to(f, 0, 0, 10, 40, 2);
  gw::set_bed(f, 10, 20 - 0.5 * gap, 10.5, 20 + 0.5 * gap, 0);
  return f;
}

}  // namespace

TEST(still_water_over_a_rough_bed_stays_still) {
  gw::Flow f = gw::make_flow({.nx = 32, .nz = 32});
  gw::set_bed(f, bumps, nullptr);
  gw::fill_to(f, 0, 0, 32, 32, 1);
  const int64_t v0 = gw::volume(f);
  for (int i = 0; i < 600; ++i) gw::step_flow(f, kDt);
  CHECK(gw::flow_speed(f) < 1e-6);
  CHECK(gw::volume(f) == v0);
  CHECK(std::fabs(gw::flow_at(f, 10.5, 20.5).surface - 1) < 1e-6);
}

TEST(a_dam_break_meets_ritters_solution) {
  gw::Flow f = gw::make_flow({.nx = 800, .nz = 4, .cell = 0.5, .manning = 0});
  gw::fill_to(f, 0, 0, 100, 2, 1);
  const double t = 20;
  for (int i = 0; i < static_cast<int>(t / kDt); ++i) gw::step_flow(f, kDt);
  const double c0 = std::sqrt(kG);
  double err = 0, front = 0;
  int n = 0;
  for (int i = 0; i < 800; ++i) {
    const double x = (i + 0.5) * 0.5, xi = (x - 100) / t;
    const gw::FlowSample s = gw::flow_at(f, x, 1);
    if (s.depth > 0.001) front = x;
    if (xi <= -c0 || xi >= 2 * c0) continue;
    const double h = (2 * c0 - xi) * (2 * c0 - xi) / (9 * kG);
    err += (s.depth - h) * (s.depth - h);
    ++n;
  }
  CHECK(std::sqrt(err / n) < 0.015);                                  // m, over the rarefaction
  CHECK(front > 100 + 0.8 * 2 * c0 * t && front < 100 + 2 * c0 * t);  // the thin tip lags
}

TEST(a_drain_empties_by_torricellis_law) {
  gw::Flow f = gw::make_flow({.nx = 8, .nz = 8});
  gw::fill_to(f, 0, 0, 8, 8, 2);
  f.drains.push_back({.x = 4, .z = 4, .area = 0.05, .cd = 0.6});
  for (int s = 0; s < 5; ++s) {
    const int64_t before = gw::volume(f);
    for (int i = 0; i < 60; ++i) gw::step_flow(f, kDt);
    const double q = static_cast<double>(before - gw::volume(f)) * gw::kQuantum;
    const double h = gw::flow_at(f, 4, 4).depth;
    CHECK(std::fabs(q / (0.6 * 0.05 * std::sqrt(2 * kG * h)) - 1) < 0.01);
  }
}

TEST(a_breach_empties_by_the_weirs_power_of_the_head) {
  gw::Flow f = breach(2);
  // Q against the head over the basin, fitted as Q = a H^p on a log scale.
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  int n = 0;
  for (int s = 0; s < 40; ++s) {
    const double before = held(f, 0, 20);
    for (int i = 0; i < 30; ++i) gw::step_flow(f, kDt);
    const double q = (before - held(f, 0, 20)) / 0.5, h = gw::flow_at(f, 2, 20).depth;
    if (s < 4) continue;  // the first surge
    const double x = ge::core::libm::log(h), y = ge::core::libm::log(q);
    sx += x;
    sy += y;
    sxx += x * x;
    sxy += x * y;
    ++n;
  }
  const double p = (n * sxy - sx * sy) / (n * sxx - sx * sx);
  const double a = ge::core::libm::exp((sy - p * sx) / n);
  CHECK(p > 1.3 && p < 1.7);
  CHECK(a / (1.705 * 2) > 0.6 && a / (1.705 * 2) < 1.0);  // a weir's coefficient over a gap
}

TEST(the_water_is_held_to_the_quantum) {
  gw::Flow f = breach(2);
  f.sources.push_back({.x = 3, .z = 30, .rate = 0.37});
  f.drains.push_back({.x = 4, .z = 10, .area = 0.02});
  const int64_t start = gw::volume(f) - f.added;  // fill_to counts as added
  for (int i = 0; i < 1200; ++i) gw::step_flow(f, kDt);
  CHECK(f.drained > 0 && f.added > 0);
  CHECK(start + f.added - f.drained == gw::volume(f));
}

TEST(a_waterfall_lands_beyond_its_lip_and_loses_nothing) {
  gw::Flow f = gw::make_flow({.nx = 64, .nz = 8, .cell = 0.5});
  gw::set_bed(f, 0, 0, 12, 4, 6);
  gw::fill_to(f, 0, 0, 8, 4, 7);
  const int64_t v0 = gw::volume(f);
  bool fell = false;
  for (int i = 0; i < 300; ++i) {
    gw::step_flow(f, kDt);
    for (const gw::Fall& fall : f.falls) {
      fell = true;
      CHECK(std::fabs(fall.x - 12) < 1e-9 && fall.land_x > 12 && fall.time > 0.5);
      if (fall.discharge > 0.05) CHECK(fall.land_x > 14);  // a sheet, not the first trickle
      CHECK(fall.vx > 0 && fall.discharge > 0);
    }
    CHECK(gw::volume(f) == v0);
  }
  CHECK(fell);
  CHECK(gw::flow_at(f, 20, 2).depth > 0.005);  // the floor below is wet
}

TEST(the_rows_give_the_same_bits_on_any_number_of_threads) {
  gw::Flow one = breach(3, 1), eight = breach(3, 8);
  for (int i = 0; i < 240; ++i) {
    gw::step_flow(one, kDt);
    gw::step_flow(eight, kDt);
  }
  CHECK(one.grids[0].vol == eight.grids[0].vol);
  CHECK(one.grids[0].qx == eight.grids[0].qx && one.grids[0].qz == eight.grids[0].qz);
}

TEST(the_tiers_hold_the_water_and_follow_the_actors) {
  gw::Flow f = gw::make_flow({.nx = 64, .nz = 64, .cell = 0.5});
  gw::set_bed(f, bumps, nullptr);
  gw::fill_to(f, 0, 0, 12, 32, 1.5);
  const int64_t v0 = gw::volume(f);
  for (int i = 0; i < 400; ++i) {
    if (i == 60) gw::set_tier(f, 1);
    if (i == 120) gw::set_tier(f, 2);
    if (i == 200) gw::set_tier(f, 1);
    if (i == 260) gw::set_tier(f, 0);
    gw::step_flow(f, kDt);
    CHECK(gw::volume(f) == v0);
  }
  // Still water through every tier comes back level within the beds' own relief.
  gw::Flow still = gw::make_flow({.nx = 32, .nz = 32});
  gw::set_bed(still, bumps, nullptr);
  gw::fill_to(still, 0, 0, 32, 32, 1);
  gw::set_tier(still, 2);
  gw::set_tier(still, 0);
  CHECK(std::fabs(gw::flow_at(still, 10.5, 20.5).surface - 1) < 0.05);
  // The tiers by distance, with a band of hysteresis.
  const double near[2] = {10, -5}, mid[2] = {10, -105}, far[2] = {10, -500};
  CHECK(gw::tier_for(f, near, 60, 400) == 0);
  CHECK(gw::tier_for(f, mid, 60, 400) == 1);
  CHECK(gw::tier_for(f, far, 60, 400) == 2);
  gw::set_tier(f, 0);
  const double edge[2] = {10, -65};
  CHECK(gw::tier_for(f, edge, 60, 400) == 0);  // still near: inside the band
  gw::set_tier(f, 1);
  CHECK(gw::tier_for(f, edge, 60, 400) == 1);
}

TEST(a_far_settled_region_sleeps_until_an_actor_or_a_source_wakes_it) {
  gw::Flow f = gw::make_flow({.nx = 32, .nz = 32});
  gw::set_bed(f, bumps, nullptr);
  gw::fill_to(f, 0, 0, 32, 32, 1);
  gw::set_tier(f, gw::kTiers - 1);
  for (int i = 0; i < 600 && !f.asleep; ++i) gw::step_flow(f, kDt);
  CHECK(f.asleep);
  const int64_t v0 = gw::volume(f);
  const double t0 = f.t;
  for (int i = 0; i < 60; ++i) gw::step_flow(f, kDt);
  CHECK(f.t == t0 && gw::volume(f) == v0);  // asleep: nothing stepped
  f.sources.push_back({.x = 4, .z = 4, .rate = 0.5});
  gw::step_flow(f, kDt);
  CHECK(!f.asleep && f.t > t0);
  f.sources.clear();
  for (int i = 0; i < 1200 && !f.asleep; ++i) gw::step_flow(f, kDt);
  CHECK(f.asleep);
  gw::set_tier(f, 0);
  gw::step_flow(f, kDt);
  CHECK(!f.asleep);
}

TEST(sample_reads_a_regions_depth_and_current) {
  gw::World world;
  gw::Flow f = gw::make_flow({.nx = 64, .nz = 8, .cell = 0.5, .x0 = 100, .z0 = 50});
  gw::fill_to(f, 100, 50, 110, 54, 1);
  const int body = gw::add_flow(world, f, {.look = "muddy-river"});
  for (int i = 0; i < 120; ++i) gw::step_flows(world, kDt);
  const gw::Sample s = gw::sample(world, 112, 0.05, 52, 2);
  CHECK(s.body == body && s.depth > 0 && s.vx > 0.5);
  CHECK(gw::sample(world, 130, 5, 52, 2).body < 0 || gw::sample(world, 130, 5, 52, 2).depth < 0);
  CHECK(world.bodies[body].optics.visibility < 1);
}

TEST(a_breach_keeps_its_golden) {
  gw::Flow f = breach(2);
  std::vector<std::vector<uint64_t>> got;
  for (int s = 0; s < 8; ++s) {
    for (int i = 0; i < 60; ++i) gw::step_flow(f, kDt);
    const gw::FlowSample a = gw::flow_at(f, 9.75, 20), b = gw::flow_at(f, 15, 20);
    got.push_back({static_cast<uint64_t>(gw::volume(f)), static_cast<uint64_t>(f.drained),
                   ge::test::bits(a.depth), ge::test::bits(a.vx), ge::test::bits(b.depth),
                   ge::test::bits(b.vx)});
  }
  ge::test::hold_golden("tests/golden/water_flow.txt",
                        "every second of a breach: volume, drained, depth and vx at the gap and "
                        "past it (water_flow_test)",
                        got);
}
