// LAB `float`: floating (engine/water/float.hpp) read the way its readers read it: boxes of ten
// densities settled and drawn from the side at the draught each found; three boxes heaving after a
// push with the period measured against theory; an undamped box's energy over a minute (it must
// never grow); balls dropped from three heights with their depth, the force they meet and the
// splash they report; a planing sled run onto a pond at four speeds; buoys riding a sea beside
// the surface at their own points; and the wakes (engine/water/wake.hpp): a hull's trail seen from
// above with Kelvin's lines over it, and the angle its arms open at against its speed.
//
// ge:summary Lab: floating: draughts, heave, energy, drops, a skimming sled, buoys, wakes
// ge:keywords lab water float buoyancy draught heave period energy slam splash planing skim buoy
// ge:keywords wake kelvin ring trail froude
// ge:skill water labs
// ge:test tests/water_float_test.cpp
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "water/float.hpp"
#include "water/wake.hpp"

namespace {

namespace gl = ge::lab;
namespace gw = ge::water;
namespace gc = ge::core;

constexpr double kPi = 3.141592653589793;
constexpr double kDt = 1.0 / 120;

std::string fixed(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

void chart_shot(gl::Lab& lab, const std::string& id, const gl::Chart& chart,
                const std::string& caption, const std::string& look_for) {
  const std::string svg = gl::to_svg(chart.plot);
  lab.attach(id + ".svg", std::vector<uint8_t>(svg.begin(), svg.end()));
  lab.shot({id, chart.title, caption, look_for, {{"seconds", lab.arg("seconds")}}},
           gl::to_picture(chart.plot));
}

gw::World still() {
  gw::World world;
  gw::add_ocean(world, 0, {});
  return world;
}

// A 2 x 1 x 2 m box as four slabs.
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

void draughts(gl::Lab& lab) {
  const gw::World world = still();
  const auto probes = box(1, 400, 0.5);
  constexpr int kBox = 60, kGap = 24, kW = 10 * (kBox + kGap) + kGap, kH = 260;
  constexpr int kWater = 150;  // the waterline's row
  gl::Picture p = gl::create_picture(kW, kH, {236, 242, 248});
  gl::fill_rect(p, 0, kWater, kW, kH - kWater, {120, 165, 200});
  gl::LabTable table{"Settled after 60 s: the draught against the density's share",
                     {"density kg/m3", "draught m", "asked m", "error mm"},
                     {}};
  double worst = 0;
  for (int k = 0; k < 10; ++k) {
    const double density = k < 9 ? 100.0 * (k + 1) : 950.0;
    gw::Floater f = floater(density * 4, 1);
    for (int i = 0; i < 120 * 60; ++i) gw::step_floater(world, probes, f, i * kDt, kDt);
    const double draught = 0.5 - f.motion.p.y;
    const double err = (draught - density / 1000) * 1000;
    worst = std::fmax(worst, std::fabs(err));
    table.rows.push_back(
        {fixed(density, 0), fixed(draught, 4), fixed(density / 1000, 4), fixed(err, 2)});
    // The box drawn at its pose, a metre to kBox pixels.
    const int x = kGap + k * (kBox + kGap);
    const double top = kWater - (f.motion.p.y + 0.5) * kBox;
    const auto shade = static_cast<uint8_t>(200 - 14 * k);
    gl::fill_rect(p, x, top, kBox, kBox, {shade, static_cast<uint8_t>(shade * 0.75), 90});
    gl::draw_line(p, x, kWater, x + kBox, kWater, {20, 60, 110});
    gl::draw_text(p, x + 6, kH - 30, fixed(density, 0), {255, 255, 255}, 2);
  }
  lab.metric("draught.worst-error", worst, "mm");
  if (worst > 5) {
    lab.fail("a box settles " + fixed(worst, 1) + " mm off its density's draught",
             "Read float_push's buoyancy and share_of in engine/water/float.cpp, then rerun `make "
             "lab LAB=float` (skill: water).");
  }
  lab.shot({"draughts",
            "TEN BOXES SETTLED AT THEIR DRAUGHTS",
            "2 x 1 x 2 m boxes of 100 to 950 kg/m3 (the number under each) in fresh water, "
            "settled for 60 s and drawn from the side at the pose each found. A box floats with "
            "the share of its height under that its density is of the water's.",
            "a staircase: the lightest riding high, each next one lower by a tenth of its height, "
            "the 950 kg/m3 box nearly awash; none tilted",
            {}},
           p);
  lab.table(table);
}

void heave(gl::Lab& lab) {
  const gw::World world = still();
  const double seconds = lab.arg_num("seconds");
  gl::LineChart line;
  line.frame = {"THREE BOXES HEAVING AFTER A PUSH", "lifted 0.2 m and let go, with drag", "t (s)",
                "heave (m)"};
  line.spikes = false;
  gl::LabTable table{"Heave: the period measured undamped against 2 pi sqrt((m + ma) / (rho g A))",
                     {"box", "mass kg", "added kg", "theory s", "measured s", "error %"},
                     {}};
  for (const double mass : {800.0, 2000.0, 3200.0}) {
    const auto damped = box(1, 150, 0.5);
    gw::Floater f = floater(mass, 0);
    // Settle first, then lift.
    for (int i = 0; i < 120 * 30; ++i) gw::step_floater(world, damped, f, i * kDt, kDt);
    const double rest = f.motion.p.y;
    f.motion.p.y += 0.2;
    gl::Trace tr{fixed(mass, 0) + " kg", {}, {}};
    for (int i = 0; i < static_cast<int>(seconds * 120); ++i) {
      gw::step_floater(world, damped, f, i * kDt, kDt);
      if (i % 4 == 0) {
        tr.x.push_back(i * kDt);
        tr.y.push_back(f.motion.p.y - rest);
      }
    }
    line.traces.push_back(tr);
    // The period without drag.
    const auto free = box(0, 0, 0.5);
    gw::Floater u = floater(mass, rest + 0.05);
    double last = 0.05, first = -1, prev = -1;
    int crossings = 0;
    for (int i = 1; i <= 120 * 40; ++i) {
      gw::step_floater(world, free, u, i * kDt, kDt);
      const double y = u.motion.p.y - rest;
      if (last > 0 && y <= 0) {
        if (first < 0) first = i * kDt;
        prev = i * kDt;
        ++crossings;
      }
      last = y;
    }
    const double measured = crossings > 1 ? (prev - first) / (crossings - 1) : 0;
    const double ma = 0.5 * 1000 * 4 * (mass / 4000);
    const double theory = 2 * kPi * std::sqrt((mass + ma) / (1000 * gw::kGravity * 4));
    const double err = (measured / theory - 1) * 100;
    table.rows.push_back({"2 x 1 x 2 m", fixed(mass, 0), fixed(ma, 0), fixed(theory, 3),
                          fixed(measured, 3), fixed(err, 2)});
    lab.metric("heave-error." + fixed(mass, 0), err, "%");
    if (std::fabs(err) > 3) {
      lab.fail("the " + fixed(mass, 0) + " kg box heaves " + fixed(err, 1) + " % off theory",
               "Read step_floater and the added mass in engine/water/float.cpp (skill: water).");
    }
  }
  chart_shot(lab, "heave", gl::line_chart(line),
             "Three boxes settled, lifted 0.2 m and let go: each heaves at its own period (the "
             "heavier, the slower) and dies away with the drag.",
             "smooth decaying sines, the heavier box slower; no swing larger than the one before");
  lab.table(table);
}

void energy(gl::Lab& lab) {
  const gw::World world = still();
  const auto free = box(0, 0, 0);
  const double mass = 2000;
  gw::Floater f = floater(mass, 0.1);
  gl::LineChart line;
  line.frame = {"AN UNDAMPED BOX'S ENERGY OVER A MINUTE",
                "kinetic plus the buoyancy's and gravity's potential about the rest pose", "t (s)",
                "energy (J)"};
  line.spikes = false;
  gl::Trace tr{"energy", {}, {}};
  const double k = 1000 * gw::kGravity * 4;  // the waterplane's stiffness, N/m
  double start = 0, most = 0;
  for (int i = 0; i <= 120 * 60; ++i) {
    if (i > 0) gw::step_floater(world, free, f, i * kDt, kDt);
    const double y = f.motion.p.y;  // the rest pose is 0 for 2000 kg
    const double e = 0.5 * mass * f.motion.v.y * f.motion.v.y + 0.5 * k * y * y;
    if (i == 0) start = e;
    most = std::fmax(most, e);
    if (i % 6 == 0) {
      tr.x.push_back(i * kDt);
      tr.y.push_back(e);
    }
  }
  line.traces.push_back(tr);
  line.rules.push_back(start);
  lab.metric("energy.most-over-start", most / start, "share");
  if (most > start * 1.01) {
    lab.fail("the undamped box gained " + fixed((most / start - 1) * 100, 2) + " % energy",
             "Read step_floater's integration order in engine/water/float.cpp (skill: water).");
  }
  chart_shot(lab, "energy", gl::line_chart(line),
             "An undamped box released 0.1 m high: its energy (kinetic plus the waterplane's "
             "spring) over a minute. The dashed line is where it started.",
             "a flat band at or under the dashed line, wobbling by a few per cent within each "
             "swing (the step's own error) and never climbing");
}

void drops(gl::Lab& lab) {
  const gw::World world = still();
  gl::LineChart line;
  line.frame = {"BALLS DROPPED IN", "a 0.3 m ball of 400 kg/m3 dropped from three heights", "t (s)",
                "centre's height (m)"};
  line.spikes = false;
  gl::LabTable table{"Drops: the splash on entry and the deepest the ball went",
                     {"from m", "splash speed m/s", "free fall m/s", "peak force kN", "deepest m"},
                     {}};
  gw::Probe ball;
  ball.radius = 0.3;
  ball.drag = 0.5;
  const std::vector<gw::Probe> probes = {ball};
  for (const double from : {0.5, 2.0, 6.0}) {
    gw::Floater f;
    f.mass = 400 * 4.0 / 3 * kPi * 0.027;
    f.inertia = {1, 1, 1};
    f.motion.p = {0, from + 0.3, 0};
    gl::Trace tr{"from " + fixed(from, 1) + " m", {}, {}};
    double splash = 0, peak = 0, deepest = 0;
    for (int i = 0; i < 120 * 6; ++i) {
      const gw::Push p = gw::step_floater(world, probes, f, i * kDt, kDt);
      for (int s = 0; s < p.splashes; ++s)
        if (p.splash[s].entering && splash == 0) splash = p.splash[s].speed;
      peak = std::fmax(peak, p.force.y);
      deepest = std::fmin(deepest, f.motion.p.y);
      if (i % 2 == 0) {
        tr.x.push_back(i * kDt);
        tr.y.push_back(f.motion.p.y);
      }
    }
    line.traces.push_back(tr);
    table.rows.push_back({fixed(from, 1), fixed(splash, 2),
                          fixed(std::sqrt(2 * gw::kGravity * from), 2), fixed(peak / 1000, 2),
                          fixed(deepest, 2)});
    lab.metric("splash." + fixed(from, 1), splash, "m/s");
  }
  line.rules.push_back(0);
  chart_shot(lab, "drops", gl::line_chart(line),
             "A light ball dropped from 0.5, 2 and 6 m: it falls, meets the slam at the surface "
             "(the dashed line), plunges, bobs back up and settles riding high.",
             "a fall, a sharp turn at the surface, a plunge deeper for the higher drop, then a few "
             "bobs that die away; nothing jumps or goes flat");
  lab.table(table);
}

std::vector<gw::Probe> sled() {
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
  return probes;
}

void skim(gl::Lab& lab) {
  const gw::World world = still();
  const auto probes = sled();
  gl::LineChart speed;
  speed.frame = {"A SLED RUN ONTO A POND", "300 kg on two 0.4 m2 plates, trimmed 5 degrees up",
                 "distance (m)", "speed (m/s)"};
  speed.spikes = false;
  gl::LineChart height = speed;
  height.frame.title = "THE SLED'S HEIGHT OVER THE WATER";
  height.frame.y_label = "centre's height (m)";
  gl::LabTable table{"Skimming: how far each sled got and when it went under",
                     {"entry m/s", "on the plates s", "under at m", "under at s", "at 30 s m"},
                     {}};
  for (const double v0 : {22.0, 15.0, 10.0, 5.0}) {
    gw::Floater f;
    f.mass = 300;
    f.inertia = {1e9, 1e9, 1e9};
    f.motion.q = gc::from_axis_angle(1, 0, 0, -5 * kPi / 180);
    f.motion.p = {0, 0.3, 0};
    f.motion.v = {0, 0, v0};
    gl::Trace ts{fixed(v0, 0) + " m/s", {}, {}}, th = ts;
    double planing = 0, under_z = -1, under_t = -1;
    for (int i = 0; i < 120 * 30; ++i) {
      gw::step_floater(world, probes, f, i * kDt, kDt);
      const double share = 0.5 * (f.state.share[0] + f.state.share[1]);
      if (under_t < 0 && share > 0 && share < 0.6 && f.motion.v.z > 2) planing += kDt;
      if (under_t < 0 && f.motion.p.y < -0.6) {
        under_t = i * kDt;
        under_z = f.motion.p.z;
      }
      if (i % 3 == 0 && f.motion.p.y > -3) {
        ts.x.push_back(f.motion.p.z);
        ts.y.push_back(f.motion.v.z);
        th.x.push_back(f.motion.p.z);
        th.y.push_back(f.motion.p.y);
      }
    }
    speed.traces.push_back(ts);
    height.traces.push_back(th);
    table.rows.push_back({fixed(v0, 0), fixed(planing, 2), fixed(under_z, 1), fixed(under_t, 2),
                          fixed(f.motion.p.z, 1)});
    lab.metric("skim-distance." + fixed(v0, 0), f.motion.p.z, "m");
  }
  height.rules.push_back(0);
  chart_shot(lab, "skim-speed", gl::line_chart(speed),
             "Each sled's speed against the distance it has come over the water: the plates hold "
             "it up while it is fast and bleed its speed; once too slow it ploughs in and stops "
             "short.",
             "the fast sleds slowing steadily for tens of metres on their plates, then faster once "
             "they plough in; the slowest stopping within about ten metres");
  chart_shot(lab, "skim-height", gl::line_chart(height),
             "The same runs' height over the water (the dashed line): riding high on the plates, "
             "then sinking.",
             "the fast sleds level near the line, then each one going down where its speed fell");
  lab.table(table);
}

void buoys(gl::Lab& lab) {
  ge::wind::Field wind;
  wind.mean = 11;
  gw::World world(wind);
  world.seed = 38;
  gw::add_ocean(world, 0, {}, {}, 3e4);
  gw::Probe ball;
  ball.radius = 0.25;
  ball.drag = 0.6;
  ball.linear = 5;
  const std::vector<gw::Probe> probes = {ball};
  constexpr int kBuoys = 9;
  gw::Floater f[kBuoys];
  for (int b = 0; b < kBuoys; ++b) {
    f[b].mass = 300 * 4.0 / 3 * kPi * 0.25 * 0.25 * 0.25;
    f[b].inertia = {1, 1, 1};
    f[b].motion.p = {0, 0, b * 6.0};
  }
  gl::LineChart line;
  line.frame = {"A BUOY AND THE SEA UNDER IT", "the middle buoy's height and the surface at it",
                "t (s)", "height (m)"};
  line.spikes = false;
  gl::Trace tb{"buoy", {}, {}}, tw{"surface", {}, {}};
  const int steps = 120 * 60;
  for (int i = 0; i < steps; ++i) {
    for (auto& fl : f) gw::step_floater(world, probes, fl, i * kDt, kDt);
    if (i >= 120 * 20 && i % 3 == 0) {
      const gw::Floater& mid = f[kBuoys / 2];
      tb.x.push_back(i * kDt);
      tb.y.push_back(mid.motion.p.y);
      tw.x.push_back(i * kDt);
      tw.y.push_back(gw::sample(world, mid.motion.p.x, 0, mid.motion.p.z, (i + 1) * kDt).surface);
    }
  }
  line.traces = {tw, tb};
  chart_shot(lab, "buoy", gl::line_chart(line),
             "The middle of nine buoys in an 11 m/s sea over 40 s: its centre's height beside the "
             "surface's at its own point. A light ball rides the surface, its centre a little "
             "under it.",
             "the two lines moving together, the buoy a touch under and a touch late, never "
             "flying off");
  // The row of buoys from the side over the surface along their line.
  constexpr int kW = 760, kH = 240;
  constexpr double kPerM = 14;  // px a metre
  gl::Picture p = gl::create_picture(kW, kH, {236, 242, 248});
  const double t = steps * kDt;
  for (int px = 0; px < kW; ++px) {
    const double z = (px - 20) / kPerM;
    const double s = gw::sample(world, 0, 0, z, t).surface;
    const double row = 0.5 * kH - s * kPerM;
    gl::fill_rect(p, px, row, 1, kH - row, {120, 165, 200});
  }
  for (const auto& fl : f) {
    gl::fill_disc(p, 20 + fl.motion.p.z * kPerM, 0.5 * kH - fl.motion.p.y * kPerM, 0.25 * kPerM,
                  {230, 120, 30});
  }
  lab.shot({"buoys",
            "NINE BUOYS ON THE SEA, FROM THE SIDE",
            "Nine 0.5 m buoys laid 6 m apart along the z axis, after a minute in an 11 m/s sea, "
            "drawn over the surface along their line. Each rides its own bit of the wave.",
            "every buoy riding the surface (a 300 kg/m3 ball floats mostly above it), on crests "
            "and in troughs alike; none in the air or sunk",
            {{"seed", "38"}, {"wind", "11"}}},
           p);
}

// A hull `length` m long run up +z at `speed` m/s until `reach` transverse wavelengths of wake
// stand behind it; `t` and `z` where it ends.
gw::Wakes hull_trail(double length, double speed, double reach, double& t, double& z) {
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

// The arm's angle (water_wake_test's measure): on lines across the wake 6 to 12 transverse
// wavelengths behind, where the waves are highest; the straight line fitted through them. Also
// the highest wave met.
double arm_angle(const gw::Wakes& w, double speed, double t, double z, double& top) {
  const double lambda = 2 * kPi * speed * speed / gw::kGravity;
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  int n = 0;
  top = 0;
  for (int m = 6; m <= 12; m += 2) {
    const double d = m * lambda;
    double best = 0, at = 0;
    for (int i = 0; 0.2 + i * lambda / 40 < 0.6 * d; ++i) {
      const double x = 0.2 + i * lambda / 40;
      double h = 0;
      for (int j = -3; j <= 3; ++j) {
        const double dz = j * lambda / 6;
        h = std::fmax(h, std::fabs(gw::wake_at(w, 0, x, z - d + dz, t).height));
      }
      if (h > best) {
        best = h;
        at = x;
      }
    }
    top = std::fmax(top, best);
    sx += d;
    sy += at;
    sxx += d * d;
    sxy += d * at;
    ++n;
  }
  return gc::libm::atan((n * sxy - sx * sy) / (n * sxx - sx * sx)) * 180 / kPi;
}

std::vector<double> speeds(const std::string& list) {
  std::vector<double> out;
  size_t at = 0;
  while (at < list.size()) {
    size_t end = list.find(',', at);
    if (end == std::string::npos) end = list.size();
    out.push_back(std::strtod(list.substr(at, end - at).c_str(), nullptr));
    at = end + 1;
  }
  return out;
}

void wakes(gl::Lab& lab) {
  // The plan: a 4 m hull at 3 m/s from above, the waves' height shaded, Kelvin's lines over it.
  {
    double t = 0, z = 0;
    const gw::Wakes w = hull_trail(4, 3, 14, t, z);
    // Worked out at half the size and drawn at twice it.
    constexpr int kW = 380, kH = 210;
    constexpr double kPerM = 1.75;  // px a metre
    gl::Picture p = gl::create_picture(kW, kH, {236, 242, 248});
    const double cx = kW / 2.0, top = 10;
    for (int py = 0; py < kH; ++py) {
      for (int px = 0; px < kW; ++px) {
        const double x = (px - cx) / kPerM, back = (py - top) / kPerM;
        const double h = gw::wake_at(w, 0, x, z - back, t).height;
        const double v = gc::clamp(h / 0.12, -1, 1);
        const auto ch = [&](double a, double b) {
          return static_cast<uint8_t>(gc::clamp(a + (b - a) * (0.5 + 0.5 * v), 0, 255));
        };
        gl::set_pixel(p, px, py, {ch(20, 235), ch(60, 245), ch(110, 255), 255});
      }
    }
    p = gl::resized(p, 2 * kW, 2 * kH);
    const double arm = gc::libm::tan(19.47 * kPi / 180);
    for (const double side : {-1.0, 1.0}) {
      gl::draw_line(p, 2 * cx, 2 * top, 2 * (cx + side * arm * (kH - top)), 2 * kH, {230, 120, 30},
                    1);
    }
    gl::fill_disc(p, 2 * cx, 2 * top, 2 * kPerM, {230, 120, 30});
    gl::draw_text(p, 8, 2 * kH - 14, "ORANGE: 19.47 DEGREES EITHER SIDE (KELVIN)", {24, 26, 32});
    lab.shot({"wake",
              "A HULL'S WAKE FROM ABOVE",
              "A 4 m hull at 3 m/s on still water, from above after 37 s, the hull at the top: "
              "every ring it set off summed (engine/water/wake.hpp), the waves' height shaded "
              "dark (troughs) to light (crests), 115 m of its trail.",
              "the transverse waves curving across the track behind the hull and the diverging "
              "waves along two arms, the arms' outer edge on the orange lines; no ridges along "
              "the track",
              {{"length", "4 m"}, {"speed", "3 m/s"}}},
             p);
  }
  // The angle against the speed.
  gl::LabTable table{"The arms' angle against the hull's speed (a 4 m hull)",
                     {"speed m/s", "Froude", "transverse wave m", "arm angle deg", "top wave m"},
                     {}};
  for (const double speed : speeds(lab.arg("wake-speeds"))) {
    double t = 0, z = 0, top = 0;
    const gw::Wakes w = hull_trail(4, speed, 12, t, z);
    const double angle = arm_angle(w, speed, t, z, top);
    const double froude = speed / std::sqrt(gw::kGravity * 4);
    table.rows.push_back({fixed(speed, 1), fixed(froude, 2),
                          fixed(2 * kPi * speed * speed / gw::kGravity, 1), fixed(angle, 2),
                          fixed(top, 3)});
    lab.metric("wake.angle." + fixed(speed, 1), angle, "deg");
    if (speed == 3.0 && std::fabs(angle - 19.47) > 2) {
      lab.fail(
          "a 4 m hull at 3 m/s leaves its arms at " + fixed(angle, 2) +
              " degrees, not Kelvin's 19.47",
          "Check the ring's phase and its shortest wave (engine/water/wake.cpp). Skill: water.");
    }
  }
  lab.table(table);
  lab.note(
      "Past a Froude number of about 0.6 a hull is too short to damp its own short "
      "diverging waves, and the arms' highest waves move in toward the track: the wake "
      "looks narrower than Kelvin's angle, as measured on real hulls.");
}

void run(gl::Lab& lab) {
  draughts(lab);
  heave(lab);
  energy(lab);
  drops(lab);
  skim(lab);
  buoys(lab);
  wakes(lab);
}

const gl::LabSpec kSpec{
    "float",
    gl::LabKind::picture,
    "engine",
    "engine/water/float.hpp",
    "water",
    "Does a body float at its density's draught, heave at the period theory gives without gaining "
    "energy, meet the water hard when dropped, skim when fast, ride the waves, and leave a "
    "wake at Kelvin's angle?",
    {{"seconds", "20", "how long the heave chart runs"},
     {"wake-speeds", "3,4,5,6", "the hull speeds of the wake's table, m/s"}},
    "--seconds=8 --wake-speeds=3",
    run};
GE_LAB(kSpec);

}  // namespace
