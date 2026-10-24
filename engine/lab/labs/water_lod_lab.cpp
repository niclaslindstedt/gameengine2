// LAB `water-lod`: the simulation's level of detail (docs/water.md § Level of detail), measured
// from the game's actors: one flood run at each tier from the start, side by side, and how far
// the coarse tiers' water stands from the fine one's; the same flood with its tier changed every
// few seconds, the water held to the quantum and the surface's jump at each change; and an actor
// walking past a region, the tier it is given against the distance, with the hysteresis band.
// (The picture's tiers, measured from the camera, join this lab with the drawn surface, M24g.)
//
// ge:summary Lab: water level of detail: the flood at three tiers, tier changes, the actors' tier
// ge:keywords lab water lod tier coarsen refine hysteresis actor flood conservation
// ge:skill water labs
// ge:test tests/water_flow_test.cpp
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "water/flow.hpp"
#include "water/optics.hpp"

namespace {

namespace gl = ge::lab;
namespace gw = ge::water;
namespace lm = ge::core::libm;

constexpr double kDt = 1.0 / 60;

std::string fixed(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

uint8_t encode(double linear) {
  const double x = linear <= 0 ? 0 : linear >= 1 ? 1 : linear;
  const double s = x <= 0.0031308 ? 12.92 * x : 1.055 * lm::pow(x, 1 / 2.4) - 0.055;
  return static_cast<uint8_t>(std::lround(s * 255));
}

double hillside(const void*, double x, double z) {
  const double valley = 0.004 * (z - 32) * (z - 32);
  const double fall = x < 20 ? 0 : -0.12 * (x - 20);
  return 8 + valley + fall + 0.25 * lm::sin(0.6 * x) * lm::cos(0.45 * z);
}

gw::Flow flood() {
  gw::Flow f = gw::make_flow({.nx = 128, .nz = 64, .cell = 0.5, .open = gw::kEast});
  gw::set_bed(f, hillside, nullptr);
  gw::set_bed(f, 18, 0, 19, 64, 14);
  gw::fill_to(f, 0, 0, 18, 64, 11);
  gw::set_bed(f, 18, 26, 19, 38, 7.5);
  return f;
}

// The water over the fine grid's cells, read through whatever tier the region is at.
std::vector<gw::FlowSample> samples(const gw::Flow& f) {
  std::vector<gw::FlowSample> s(static_cast<size_t>(f.spec.nx) * f.spec.nz);
  for (int k = 0; k < f.spec.nz; ++k)
    for (int i = 0; i < f.spec.nx; ++i)
      s[static_cast<size_t>(k) * f.spec.nx + i] =
          gw::flow_at(f, f.spec.x0 + (i + 0.5) * f.spec.cell, f.spec.z0 + (k + 0.5) * f.spec.cell);
  return s;
}

std::vector<double> depths(const gw::Flow& f) {
  std::vector<double> d;
  for (const gw::FlowSample& s : samples(f)) d.push_back(s.depth);
  return d;
}

void draw_depths(gl::Picture& p, const gw::Flow& f, const std::vector<double>& d, int ox, int oy,
                 int px) {
  const gw::Constituents& look = *gw::find_look("nordic-clear");
  const gw::Iops in = gw::iops(look);
  for (int k = 0; k < f.spec.nz; ++k) {
    for (int i = 0; i < f.spec.nx; ++i) {
      const double h = d[static_cast<size_t>(k) * f.spec.nx + i];
      gl::Rgba c{150, 140, 120};
      if (h > 0.002) {
        const gw::Rgb w = gw::colour_of(gw::reflectance(in, look, h));
        c = {encode(w.r * 4), encode(w.g * 4), encode(w.b * 4)};
      }
      gl::fill_rect(p, ox + i * px, oy + (f.spec.nz - 1 - k) * px, px, px, c);
    }
  }
}

void tiers(gl::Lab& lab) {
  constexpr int kPx = 2, kW = 128 * kPx, kH = 64 * kPx;
  gl::Picture p = gl::create_picture(3 * kW + 40, kH + 30, {250, 250, 248});
  gl::LabTable table{
      "The flood after 10 s at each tier",
      {"tier", "cells", "steps", "water m^3", "depth rms off tier 0 m", "front m (east-most wet)"},
      {}};
  std::vector<double> fine;
  for (int tier = 0; tier < gw::kTiers; ++tier) {
    gw::Flow f = flood();
    gw::set_tier(f, tier);
    int steps = 0;
    for (int i = 0; i < 600; ++i) {
      gw::step_flow(f, kDt);
      if (f.phase == 0) ++steps;
    }
    const std::vector<double> d = depths(f);
    if (tier == 0) fine = d;
    double err = 0, front = 0;
    for (size_t c = 0; c < d.size(); ++c) {
      err += (d[c] - fine[c]) * (d[c] - fine[c]);
      if (d[c] > 0.01) {
        const double x = (static_cast<double>(c % 128) + 0.5) * 0.5;
        front = x > front ? x : front;
      }
    }
    const int ox = 10 + tier * (kW + 10);
    gl::draw_text(p, ox, 6,
                  "TIER " + std::to_string(tier) +
                      (tier == 0   ? ", THE CELL"
                       : tier == 1 ? ", 2X2 HALF RATE"
                                   : ", 4X4 QUARTER RATE"),
                  {24, 26, 32});
    draw_depths(p, f, d, ox, 20, kPx);
    const int cells = (128 >> tier) * (64 >> tier);
    table.rows.push_back({std::to_string(tier), std::to_string(cells), std::to_string(steps),
                          fixed(static_cast<double>(gw::volume(f) + f.drained) * gw::kQuantum, 3),
                          fixed(std::sqrt(err / static_cast<double>(d.size())), 4),
                          fixed(front, 1)});
    lab.metric("tier" + std::to_string(tier) + ".rms",
               std::sqrt(err / static_cast<double>(d.size())), "m");
  }
  lab.shot({"tiers",
            "ONE FLOOD AT THREE TIERS",
            "The same broken dam after 10 s, run from the start at the cell (0.5 m, every step), "
            "its cells merged 2x2 (1 m, every other step) and 4x4 (2 m, every fourth). The "
            "coarse tiers cost a quarter and a sixteenth of the cells at a half and a quarter of "
            "the rate, and keep the flood's shape.",
            "three floods of one shape, the coarse ones blockier; the same water in each (the "
            "table's water column)",
            {}},
           p);
  lab.table(table);
}

void changes(gl::Lab& lab) {
  gw::Flow f = flood();
  const int64_t start = gw::volume(f) - f.added;
  gl::LabTable table{"Tier changes during the flood",
                     {"t s", "tier", "to", "water held", "surface jump m: median", "95th",
                      "largest", "the coarse cell's bed relief m"},
                     {}};
  const int plan[][2] = {{120, 1}, {240, 2}, {360, 1}, {480, 0}, {600, 2}, {720, 0}};
  int step = 0;
  for (const auto& change : plan) {
    while (step < change[0]) {
      gw::step_flow(f, kDt);
      ++step;
    }
    const std::vector<gw::FlowSample> before = samples(f);
    const int from = f.tier;
    gw::set_tier(f, change[1]);
    const std::vector<gw::FlowSample> after = samples(f);
    // The surface's jump where the cell is wet on both sides of the change.
    std::vector<double> jumps;
    for (size_t c = 0; c < before.size(); ++c)
      if (before[c].depth > 0.01 && after[c].depth > 0.01)
        jumps.push_back(std::fabs(after[c].surface - before[c].surface));
    std::sort(jumps.begin(), jumps.end());
    const auto at = [&](double q) {
      return jumps.empty() ? 0.0
                           : jumps[static_cast<size_t>(q * static_cast<double>(jumps.size() - 1))];
    };
    // The relief inside a 4x4 block of fine cells: what a coarse cell cannot see.
    double relief = 0;
    const gw::FlowGrid& g = f.grids[0];
    for (int k = 0; k + 4 <= g.nz; k += 4) {
      for (int i = 0; i + 4 <= g.nx; i += 4) {
        double lo = 1e300, hi = -1e300;
        for (int dk = 0; dk < 4; ++dk)
          for (int di = 0; di < 4; ++di) {
            const double b = g.bed[(k + dk) * g.nx + i + di];
            lo = b < lo ? b : lo;
            hi = b > hi ? b : hi;
          }
        relief = hi - lo > relief ? hi - lo : relief;
      }
    }
    const bool held = start + f.added - f.drained == gw::volume(f);
    table.rows.push_back({fixed(step * kDt, 0), std::to_string(from), std::to_string(change[1]),
                          held ? "to the quantum" : "LOST", fixed(at(0.5), 3), fixed(at(0.95), 3),
                          fixed(at(1), 3), fixed(relief, 3)});
    if (!held)
      lab.fail("a change of tier from " + std::to_string(from) + " to " +
                   std::to_string(change[1]) + " lost water",
               "Read coarsen and refine in engine/water/flow.cpp (skill: water).");
  }
  lab.table(table);
}

void actors(gl::Lab& lab) {
  gw::Flow f = gw::make_flow({.nx = 64, .nz = 64, .cell = 1});
  gl::LineChart line;
  line.frame = {"THE TIER AN ACTOR GIVES A REGION",
                "walking out to 600 m from a 64 m region and back", "time (s)", "tier"};
  line.spikes = false;
  gl::Trace tier{"tier", {}, {}}, dist{"distance / 200 m", {}, {}};
  for (int s = 0; s <= 1200; ++s) {
    // Out at 1 m/s and back, with a dither of 15 m about each edge at the turn points.
    const double d = s <= 600 ? s : 1200 - s;
    const double wobble = 15 * lm::sin(0.2 * s);
    const double actor[2] = {32, -(d + wobble)};
    gw::set_tier(f, gw::tier_for(f, actor, 60, 400));
    tier.x.push_back(s);
    tier.y.push_back(f.tier);
    dist.x.push_back(s);
    dist.y.push_back((d + wobble) / 200);
  }
  line.traces = {tier, dist};
  line.rules = {0.3, 2.0};
  const gl::Chart chart = gl::line_chart(line);
  const std::string svg = gl::to_svg(chart.plot);
  lab.attach("actors.svg", std::vector<uint8_t>(svg.begin(), svg.end()));
  lab.shot({"actors",
            chart.title,
            "An actor walks away from a region and back, wobbling 15 m to and fro; the tier is "
            "near under 60 m, mid under 400 m, far beyond, each edge moved out 10 m while the "
            "region is inside it. The dashed lines are the edges (60 and 400 m, over 200).",
            "the tier stepping up once at each edge on the way out and down once on the way "
            "back, never flickering while the actor wobbles across an edge",
            {}},
           gl::to_picture(chart.plot));
}

void run(gl::Lab& lab) {
  tiers(lab);
  changes(lab);
  actors(lab);
}

const gl::LabSpec kSpec{
    "water-lod",
    gl::LabKind::picture,
    "engine",
    "engine/water/flow.hpp",
    "water",
    "Do the flow's coarse tiers keep a flood's shape and every drop, change without a jump the "
    "bed does not explain, and follow the actors without flickering?",
    {},
    "",
    run};
GE_LAB(kSpec);

}  // namespace
