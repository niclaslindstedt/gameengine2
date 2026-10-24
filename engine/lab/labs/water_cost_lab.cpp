// LAB `water-cost`: what the water costs a step. The flow region's step timed at three sizes,
// each tier and one or eight threads, on a flood that keeps every cell busy; the waves' sample
// timed a probe. Times are measured, never in the digest.
//
// ge:summary Bench: the flow step's cost by size, tier and threads, and the waves' sample's
// ge:keywords lab bench water flow cost step time tier threads sample perf
// ge:skill water perf
// ge:test tests/water_flow_test.cpp
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "lab/lab.hpp"
#include "water/flow.hpp"
#include "water/world.hpp"

namespace {

namespace gl = ge::lab;
namespace gw = ge::water;

constexpr double kDt = 1.0 / 60;

std::string fixed(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

double bumps(const void*, double x, double z) {
  return 0.3 * ge::core::libm::sin(0.31 * x) * ge::core::libm::cos(0.23 * z);
}

void flow(gl::Lab& lab) {
  const std::vector<double> sizes = lab.arg_nums("sizes");
  const int steps = static_cast<int>(lab.arg_num("steps"));
  gl::LabTable table{"The flow step, ms a 1/60 s step (the mean over the run)",
                     {"cells", "tier", "threads", "ms", "sub-steps a step"},
                     {}};
  for (const double size : sizes) {
    const int n = static_cast<int>(size);
    for (int tier = 0; tier < gw::kTiers; ++tier) {
      for (const int workers : {1, 8}) {
        gw::Flow f = gw::make_flow({.nx = n, .nz = n, .cell = 0.5, .workers = workers});
        gw::set_bed(f, bumps, nullptr);
        gw::fill_to(f, 0, 0, 0.25 * n, 0.5 * n, 2);  // a dam break across a quarter
        gw::set_tier(f, tier);
        for (int i = 0; i < 8; ++i) gw::step_flow(f, kDt);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < steps; ++i) gw::step_flow(f, kDt);
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / steps;
        const double fastest = gw::flow_speed(f) + std::sqrt(9.80665 * 2);
        const double cell = 0.5 * (1 << tier);
        const double subs = std::ceil(kDt * (1 << tier) * fastest / (0.4 * cell));
        table.rows.push_back({std::to_string(n) + "^2", std::to_string(tier),
                              std::to_string(workers), fixed(ms, 3), fixed(subs, 0)});
        lab.metric("flow." + std::to_string(n) + ".tier" + std::to_string(tier) + ".w" +
                       std::to_string(workers),
                   ms, "ms", gl::Det::measured);
      }
    }
  }
  lab.table(table, gl::Det::measured);
}

void waves(gl::Lab& lab) {
  gw::World world;
  gw::add_ocean(world, 0);
  const int n = 100000;
  double sink = 0;
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < n; ++i) sink += gw::sample(world, i * 0.37, 0, i * 0.11, 1.0).surface;
  const auto t1 = std::chrono::steady_clock::now();
  const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / n;
  lab.metric("sample.ocean", us, "us", gl::Det::measured);
  lab.note("one sample of a developed sea: " + fixed(us, 3) + " us (checksum " + fixed(sink, 1) +
           ")");
}

void run(gl::Lab& lab) {
  flow(lab);
  waves(lab);
}

const gl::LabSpec kSpec{
    "water-cost",
    gl::LabKind::bench,
    "engine",
    "engine/water/flow.hpp",
    "water",
    "What does a flow region's step cost by size, tier and threads, and a sample of the sea?",
    {{"sizes", "128,256,512", "the regions' cells a side"},
     {"steps", "60", "the steps timed a case"}},
    "--sizes=64 --steps=4",
    run};
GE_LAB(kSpec);

}  // namespace
