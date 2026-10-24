// LAB `flow`: flowing water (engine/water/flow.hpp) against the laws it must meet and as it
// looks: a dam break's profile against Ritter's solution at three moments; a reservoir on a
// hillside with its dam broken, from above as the flood runs down the valley; a basin emptying
// through a breach, its outflow against the weir's power of the head; a drain against
// Torricelli's law; a river over a cliff, its sheets' paths from the side; and the books: every
// run's water to the quantum, and the same bits on one thread and on eight.
//
// ge:summary Lab: flowing water: Ritter, a flood from above, the weir, Torricelli, a waterfall
// ge:keywords lab water flow dam-break ritter flood breach weir drain torricelli waterfall
// ge:keywords conservation threads
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
constexpr double kG = 9.80665;

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

void chart_shot(gl::Lab& lab, const std::string& id, const gl::Chart& chart,
                const std::string& caption, const std::string& look_for) {
  const std::string svg = gl::to_svg(chart.plot);
  lab.attach(id + ".svg", std::vector<uint8_t>(svg.begin(), svg.end()));
  lab.shot({id, chart.title, caption, look_for, {}}, gl::to_picture(chart.plot));
}

// A region from above: the bed shaded by its height and lit from the north-west, the water in
// the colour its depth gives (a clear mountain lake's), white where it runs fast.
void draw_region(gl::Picture& p, const gw::Flow& f, int ox, int oy, int px) {
  const gw::FlowGrid& g = f.grids[f.tier];
  const gw::Constituents& look = *gw::find_look("nordic-clear");
  const gw::Iops in = gw::iops(look);
  double lo = 1e300, hi = -1e300;
  for (const double b : g.bed) {
    lo = b < lo ? b : lo;
    hi = b > hi ? b : hi;
  }
  const int scale = px * (1 << f.tier);
  for (int k = 0; k < g.nz; ++k) {
    for (int i = 0; i < g.nx; ++i) {
      const int c = k * g.nx + i;
      const double cx = f.spec.x0 + (i + 0.5) * g.cell, cz = f.spec.z0 + (k + 0.5) * g.cell;
      const gw::FlowSample s = gw::flow_at(f, cx, cz);
      const double up = i > 0 && k > 0 ? g.bed[c] - g.bed[c - g.nx - 1] : 0;
      const double shade = 0.35 + 0.35 * (hi > lo ? (g.bed[c] - lo) / (hi - lo) : 0) + 0.8 * up;
      gl::Rgba colour{encode(shade * 0.55), encode(shade * 0.5), encode(shade * 0.42)};
      if (s.depth > 0.002) {
        const gw::Rgb w = gw::colour_of(gw::reflectance(in, look, s.depth));
        const double foam = std::sqrt(s.vx * s.vx + s.vz * s.vz) > 2.5 ? 0.35 : 0;
        colour = {encode(w.r * 4 + foam), encode(w.g * 4 + foam), encode(w.b * 4 + foam)};
      }
      // North up: z grows up the picture.
      gl::fill_rect(p, ox + i * scale, oy + (g.nz - 1 - k) * scale, scale, scale, colour);
    }
  }
}

void ritter(gl::Lab& lab) {
  gl::LineChart line;
  line.frame = {"A DAM BREAK AGAINST RITTER'S SOLUTION",
                "1 m of still water behind a dam at 100 m, a dry bed beyond", "x (m)", "depth (m)"};
  line.spikes = false;
  gw::Flow f = gw::make_flow({.nx = 800, .nz = 4, .cell = 0.5, .manning = 0});
  gw::fill_to(f, 0, 0, 100, 2, 1);
  const double c0 = std::sqrt(kG);
  gl::LabTable table{"The dam break", {"t s", "rms error m", "front m", "Ritter's front m"}, {}};
  double t = 0;
  for (const double at : {5.0, 10.0, 20.0}) {
    if (at > 5 && at > 20 * lab.arg_num("time")) break;  // the smoke run checks the first
    while (t < at - 1e-9) {
      gw::step_flow(f, kDt);
      t += kDt;
    }
    gl::Trace sim{"t " + fixed(at, 0) + " s", {}, {}},
        exact{"Ritter " + fixed(at, 0) + " s", {}, {}};
    double err = 0, front = 0;
    int n = 0;
    for (int i = 100; i < 600; ++i) {
      const double x = (i + 0.5) * 0.5, xi = (x - 100) / at;
      const double h = xi <= -c0 ? 1 : xi >= 2 * c0 ? 0 : (2 * c0 - xi) * (2 * c0 - xi) / (9 * kG);
      const double d = gw::flow_at(f, x, 1).depth;
      if (d > 0.001) front = x;
      if (xi > -c0 && xi < 2 * c0) {
        err += (d - h) * (d - h);
        ++n;
      }
      if (i % 2 == 0) {
        sim.x.push_back(x);
        sim.y.push_back(d);
        exact.x.push_back(x);
        exact.y.push_back(h);
      }
    }
    line.traces.push_back(sim);
    line.traces.push_back(exact);
    const double rms = std::sqrt(err / n);
    table.rows.push_back(
        {fixed(at, 0), fixed(rms, 4), fixed(front, 1), fixed(100 + 2 * c0 * at, 1)});
    lab.metric("ritter.rms." + fixed(at, 0) + "s", rms, "m");
    if (rms > 0.02)
      lab.fail("the dam break is " + fixed(rms, 4) + " m rms off Ritter's solution at " +
                   fixed(at, 0) +
                   " s, over 0.02 m (a first-order scheme smears the early, steepest profile most)",
               "Read the HLL flux in engine/water/flow.cpp (skill: water).");
  }
  chart_shot(lab, "ritter", gl::line_chart(line),
             "A dam break on a dry bed: the simulated depth against Ritter's analytic solution "
             "at 5, 10 and 20 s. The rarefaction runs back into the reservoir at sqrt(g h) and "
             "the front runs out at twice it.",
             "each simulated line on its Ritter line; only the front's thin tip falling short");
  lab.table(table);
}

double hillside(const void*, double x, double z) {
  // A valley falling to the east, a reservoir's bowl at its head.
  const double valley = 0.004 * (z - 32) * (z - 32);
  const double fall = x < 20 ? 0 : -0.12 * (x - 20);
  return 8 + valley + fall + 0.25 * lm::sin(0.6 * x) * lm::cos(0.45 * z);
}

void flood(gl::Lab& lab) {
  gw::Flow f = gw::make_flow({.nx = 128, .nz = 64, .cell = 0.5, .open = gw::kEast});
  gw::set_bed(f, hillside, nullptr);
  gw::set_bed(f, 18, 0, 19, 64, 14);  // the dam
  gw::fill_to(f, 0, 0, 18, 64, 11);
  gw::set_bed(f, 18, 26, 19, 38, 7.5);  // broken: 12 m of it down to the valley floor
  constexpr int kPx = 2, kW = 128 * kPx, kH = 64 * kPx;
  gl::Picture p = gl::create_picture(2 * kW + 30, 3 * (kH + 22) + 10, {250, 250, 248});
  // `--time` shortens every run but the laws' first checks for the smoke run.
  const double scale = lab.arg_num("time");
  const double frames[6] = {0, 2 * scale, 5 * scale, 10 * scale, 20 * scale, 40 * scale};
  double t = 0;
  for (int n = 0; n < 6; ++n) {
    while (t < frames[n] - 1e-9) {
      gw::step_flow(f, kDt);
      t += kDt;
    }
    const int ox = 10 + (n % 2) * (kW + 10), oy = 6 + (n / 2) * (kH + 22);
    gl::draw_text(p, ox, oy, "T " + fixed(frames[n], 1) + " S", {24, 26, 32});
    draw_region(p, f, ox, oy + 10, kPx);
  }
  lab.metric("flood.drained", static_cast<double>(f.drained) * gw::kQuantum, "m^3");
  lab.shot({"flood",
            "A DAM BROKEN AT THE HEAD OF A VALLEY",
            "A reservoir behind a dam across a valley's head, 12 m of the dam broken to the "
            "valley floor; from above (north up) at 0, 2, 5, 10, 20 and 40 s. The water's colour "
            "is its depth's, white where it runs faster than 2.5 m/s; the valley's lower end is "
            "open.",
            "the flood bursting through the gap, spreading down the valley and finding its "
            "lowest line; the reservoir falling; nothing leaking through the standing dam",
            {}},
           p);
}

void weir(gl::Lab& lab) {
  gw::Flow f = gw::make_flow({.nx = 64, .nz = 80, .cell = 0.5, .open = gw::kEast});
  gw::set_bed(f, 10, 0, 10.5, 40, 3);
  gw::fill_to(f, 0, 0, 10, 40, 2);
  gw::set_bed(f, 10, 19, 10.5, 21, 0);
  gl::LineChart pts;
  pts.frame = {"A BASIN EMPTYING THROUGH A BREACH", "a 2 m gap in the wall of a 20 x 40 m basin",
               "head over the gap's sill (m)", "outflow (m^3/s)"};
  pts.spikes = false;
  pts.log_y = true;
  gl::Trace sim{"simulated", {}, {}}, law{"weir law 1.705 b H^1.5", {}, {}};
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  int n = 0;
  const auto basin = [&] {
    double v = 0;
    for (int k = 0; k < 80; ++k)
      for (int i = 0; i < 20; ++i) v += static_cast<double>(f.grids[0].vol[k * 64 + i]);
    return v * gw::kQuantum;
  };
  const int rounds = 40;  // whole even in the smoke run: the fit needs the head well down
  for (int s = 0; s < rounds; ++s) {
    const double before = basin();
    for (int i = 0; i < 30; ++i) gw::step_flow(f, kDt);
    const double q = (before - basin()) / 0.5, h = gw::flow_at(f, 2, 20).depth;
    if (s < 4) continue;
    sim.x.push_back(h);
    sim.y.push_back(q);
    law.x.push_back(h);
    law.y.push_back(1.705 * 2 * lm::pow(h, 1.5));
    const double x = lm::log(h), y = lm::log(q);
    sx += x;
    sy += y;
    sxx += x * x;
    sxy += x * y;
    ++n;
  }
  const double p = (n * sxy - sx * sy) / (n * sxx - sx * sx);
  const double a = lm::exp((sy - p * sx) / n);
  lab.metric("weir.power", p, "");
  lab.metric("weir.coefficient", a / (1.705 * 2), "");
  if (p < 1.3 || p > 1.7)
    lab.fail("the breach's outflow goes as the head to the " + fixed(p, 2) +
                 ", out of the weir's 1.3 to 1.7",
             "Read the hydrostatic reconstruction in engine/water/flow.cpp (skill: water).");
  pts.traces = {sim, law};
  chart_shot(lab, "weir", gl::line_chart(pts),
             "The outflow through a breach against the head behind it. Over a sill water leaves "
             "as over a weir, Q = Cd 1.705 b H^1.5; the fit gives the power " +
                 fixed(p, 2) + " and the coefficient " + fixed(a / (1.705 * 2), 2) + ".",
             "the simulated points on a line parallel to the weir law's, a little under it");
}

void drain(gl::Lab& lab) {
  gw::Flow f = gw::make_flow({.nx = 8, .nz = 8});
  gw::fill_to(f, 0, 0, 8, 8, 2);
  f.drains.push_back({.x = 4, .z = 4, .area = 0.05, .cd = 0.6});
  gl::LineChart line;
  line.frame = {"A TANK EMPTYING THROUGH A DRAIN", "an 8 x 8 m tank, 2 m deep, a 0.05 m^2 hole",
                "t (s)", "outflow (m^3/s)"};
  line.spikes = false;
  gl::Trace sim{"simulated", {}, {}}, law{"Torricelli Cd A sqrt(2 g h)", {}, {}};
  const int samples = static_cast<int>(120 * lab.arg_num("time"));
  for (int s = 0; s < samples; ++s) {
    const int64_t before = gw::volume(f);
    for (int i = 0; i < 300; ++i) gw::step_flow(f, kDt);
    const double q = static_cast<double>(before - gw::volume(f)) * gw::kQuantum / 5;
    const double h = gw::flow_at(f, 4, 4).depth;
    sim.x.push_back(5.0 * (s + 1));
    sim.y.push_back(q);
    law.x.push_back(5.0 * (s + 1));
    law.y.push_back(0.6 * 0.05 * std::sqrt(2 * kG * h));
  }
  line.traces = {sim, law};
  chart_shot(lab, "drain", gl::line_chart(line),
             "A tank emptying through a hole: the outflow over ten minutes against Torricelli's "
             "law at the depth the tank has left. The outflow falls with the square root of the "
             "head, so the tank's last half takes longer than its first.",
             "the two lines one, falling ever more slowly");
}

double cliff(const void*, double x, double) { return x < 12 ? 6 + 0.02 * (12 - x) : 0; }

void waterfall(gl::Lab& lab) {
  gw::Flow f = gw::make_flow({.nx = 80, .nz = 8, .cell = 0.5, .open = gw::kEast});
  gw::set_bed(f, cliff, nullptr);
  f.sources.push_back({.x = 1, .z = 2, .rate = 2});
  double t = 0;
  while (t < 30) {
    gw::step_flow(f, kDt);
    t += kDt;
  }
  gl::LineChart line;
  line.frame = {"A RIVER OVER A CLIFF", "2 m^3/s over a 6 m drop, from the side", "x (m)",
                "height (m)"};
  line.spikes = false;
  gl::Trace bed{"bed", {}, {}}, water{"surface", {}, {}}, sheet{"the sheet", {}, {}};
  for (int i = 0; i < 80; ++i) {
    const double x = (i + 0.5) * 0.5;
    const gw::FlowSample s = gw::flow_at(f, x, 2);
    bed.x.push_back(x);
    bed.y.push_back(s.bed);
    if (s.depth > 0.001) {
      water.x.push_back(x);
      water.y.push_back(s.surface);
    }
  }
  double land = 0, tf = 0, q = 0;
  for (const gw::Fall& fall : f.falls) {
    if (std::fabs(fall.z - 2.25) > 0.3) continue;
    for (int j = 0; j <= 20; ++j) {
      const double s = fall.time * j / 20;
      sheet.x.push_back(fall.x + fall.vx * s);
      sheet.y.push_back(fall.y - 0.5 * kG * s * s);
    }
    land = fall.land_x;
    tf = fall.time;
  }
  for (const gw::Fall& fall : f.falls) q += fall.discharge;
  line.traces = {bed, water, sheet};
  lab.metric("waterfall.discharge", q, "m^3/s");
  lab.metric("waterfall.landing", land, "m");
  chart_shot(lab, "waterfall", gl::line_chart(line),
             "A river of 2 m^3/s running off a 6 m cliff, after 30 s. At the lip its water leaves "
             "the grid as a sheet on a ballistic path, " +
                 fixed(tf, 2) + " s in the air, and lands at " + fixed(land, 1) +
                 " m with its volume intact; "
                 "the sheets carry " +
                 fixed(q, 2) + " m^3/s.",
             "the sheet's parabola from the lip down to the pool below; the river shallow and "
             "fast at the lip");
}

void books(gl::Lab& lab) {
  gl::LabTable table{"The books: every run's water, in quanta of 1e-9 m^3",
                     {"run", "start", "added", "drained", "now", "missing"},
                     {}};
  const auto row = [&](const std::string& name, gw::Flow f, int steps) {
    const int64_t start = gw::volume(f) - f.added;
    for (int i = 0; i < steps; ++i) gw::step_flow(f, kDt);
    const int64_t missing = start + f.added - f.drained - gw::volume(f);
    table.rows.push_back({name, std::to_string(start), std::to_string(f.added),
                          std::to_string(f.drained), std::to_string(gw::volume(f)),
                          std::to_string(missing)});
    if (missing != 0)
      lab.fail("the run " + name + " lost " + std::to_string(missing) + " quanta of water",
               "Read the quanta passes in engine/water/flow.cpp (skill: water).");
  };
  gw::Flow a = gw::make_flow({.nx = 64, .nz = 64, .cell = 0.5, .open = gw::kEast});
  gw::set_bed(a, hillside, nullptr);
  gw::fill_to(a, 0, 0, 18, 32, 11);
  a.sources.push_back({.x = 5, .z = 16, .rate = 0.7});
  a.drains.push_back({.x = 8, .z = 8, .area = 0.03});
  const int steps = static_cast<int>(1200 * lab.arg_num("time"));
  row("a flood with a spring and a drain", a, steps);
  gw::Flow b = gw::make_flow({.nx = 80, .nz = 8, .cell = 0.5, .open = gw::kEast});
  gw::set_bed(b, cliff, nullptr);
  b.sources.push_back({.x = 1, .z = 2, .rate = 2});
  row("a waterfall", b, steps);
  // The same flood on one thread and on eight.
  gw::Flow one = a, eight = a;
  eight.spec.workers = 8;
  for (int i = 0; i < steps / 5; ++i) {
    gw::step_flow(one, kDt);
    gw::step_flow(eight, kDt);
  }
  const bool same = one.grids[0].vol == eight.grids[0].vol && one.grids[0].qx == eight.grids[0].qx;
  table.rows.push_back(
      {"one thread against eight", same ? "same bits" : "DIFFERENT", "", "", "", ""});
  if (!same)
    lab.fail("the flow's rows give different bits on eight threads than on one",
             "Find the pass that reads a row another band writes in engine/water/flow.cpp "
             "(skill: water).");
  lab.table(table);
}

void run(gl::Lab& lab) {
  ritter(lab);
  flood(lab);
  weir(lab);
  drain(lab);
  waterfall(lab);
  books(lab);
}

const gl::LabSpec kSpec{
    "flow",
    gl::LabKind::picture,
    "engine",
    "engine/water/flow.hpp",
    "water",
    "Does flowing water meet Ritter's dam break, the weir's and Torricelli's laws, fall over a "
    "lip as a sheet, and hold every drop on any number of threads?",
    {{"time", "1", "every run's length, a share of the full one (the smoke run's is shorter)"}},
    "--time=0.25",
    run};
GE_LAB(kSpec);

}  // namespace
