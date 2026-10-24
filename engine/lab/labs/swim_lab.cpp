// LAB `swim`: the engine's swimmer (engine/water/swim.hpp) read the way a game reads it: a
// swimsuit's swimmer swimming up, lying still, and lying still in cold water, his eyes against
// the surface over three minutes; the kinematic regime's speed from rest flat out at two levels
// and its slow sink, for a first-person preset's numbers; the water jump's arc out over a lip;
// and the cold by the water's temperature (the shock, the breath held, the swim-failure time).
//
// ge:summary Lab: swimming: treading and sinking, the kinematic speeds, the water jump, the cold
// ge:keywords lab water swim tread sink kinematic idle-sink water-jump cold-shock swim-failure
// ge:keywords breath
// ge:skill water labs
// ge:test tests/water_swim_test.cpp
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "water/swim.hpp"

namespace {

namespace gl = ge::lab;
namespace gw = ge::water;

constexpr double kDt = 1.0 / 120;
constexpr double kUnits = 32;  // the preset's units a metre

std::string fixed(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

void chart_shot(gl::Lab& lab, const std::string& id, const gl::Chart& chart,
                const std::string& caption, const std::string& look_for) {
  const std::string svg = gl::to_svg(chart.plot);
  lab.attach(id + ".svg", std::vector<uint8_t>(svg.begin(), svg.end()));
  lab.shot({id, chart.title, caption, look_for, {{"minutes", lab.arg("minutes")}}},
           gl::to_picture(chart.plot));
}

gw::World pool(double temperature) {
  gw::World world;
  gw::add_ocean(world, 0, {.look = "pool", .density = 1000, .temperature = temperature});
  return world;
}

// A first-person preset's numbers, in its own units.
gw::SwimSpec first_person() {
  gw::SwimSpec s;
  s.regime = gw::Regime::kinematic;
  s.scale = kUnits;
  s.waist = 0.75;
  s.eyes = 1.4;
  s.run_speed = 320;
  s.wish_scale = 0.7;
  s.accelerate = 10;
  s.friction = 4;
  s.idle_sink = 60;
  s.jump_speed = 225;
  s.jump_push = 50;
  s.jump_time = 2;
  s.gravity = 800;
  return s;
}

bool lip(const void*, const ge::core::Vec3&, double, double) { return true; }

void tread(gl::Lab& lab) {
  const double minutes = lab.arg_num("minutes");
  gl::LineChart line;
  line.frame = {"A SWIMMER IN A SWIMSUIT", "his eyes against the surface (above 0: out)", "t (s)",
                "eyes over the surface (m)"};
  line.spikes = false;
  gl::LabTable table{"Treading and sinking",
                     {"case", "water C", "eyes first under s", "out of breath s", "depth at end m"},
                     {}};
  const struct {
    const char* name;
    double temperature;
    double up;
  } cases[] = {{"swims up, 22 C", 22, 1}, {"lies still, 22 C", 22, 0}, {"lies still, 2 C", 2, 0}};
  const gw::SwimSpec spec = gw::default_swimmer();
  for (const auto& c : cases) {
    const gw::World world = pool(c.temperature);
    gw::SwimState st;
    st.p = {0, -0.8, 0};
    gl::Trace tr{c.name, {}, {}};
    double under = -1, out = -1;
    for (int i = 0; i < static_cast<int>(minutes * 60 * 120); ++i) {
      gw::step_swimmer(world, spec, {0, c.up, 0}, st, i * kDt, kDt);
      if (under < 0 && st.level == 3) under = i * kDt;
      if (out < 0 && st.drowning) out = i * kDt;
      if (i % 24 == 0) {
        tr.x.push_back(i * kDt);
        tr.y.push_back(std::fmax(-6.0, st.p.y + spec.eyes));
      }
    }
    line.traces.push_back(tr);
    table.rows.push_back({c.name, fixed(c.temperature, 0), under < 0 ? "never" : fixed(under, 1),
                          out < 0 ? "never" : fixed(out, 1), fixed(st.p.y, 1)});
    lab.metric(std::string("out-of-breath.") + c.name, out, "s");
  }
  line.rules.push_back(0);
  chart_shot(
      lab, "tread", gl::line_chart(line),
      "A swimmer in a swimsuit dropped in feet first. Swimming up, he holds his eyes over "
      "the surface. Lying still, his eyes go under, he holds his breath, and once it is gone "
      "his lungs empty and he sinks (the chart stops at 6 m down). In 2 degree water the "
      "cold shock takes his breath three times faster.",
      "the swimming line above 0 throughout; both still lines just under 0 for a while, "
      "then falling, the cold one far sooner");
  lab.table(table);
}

void kinematic(gl::Lab& lab) {
  gl::LineChart line;
  line.frame = {"THE KINEMATIC REGIME FROM REST", "a first-person preset's numbers, its units/s",
                "t (s)", "speed (units/s)"};
  line.spikes = false;
  gl::LabTable table{"Kinematic speeds: settled against the formula",
                     {"case", "level", "settled units/s", "formula units/s"},
                     {}};
  const gw::World world = pool(20);
  const gw::SwimSpec s = first_person();
  const struct {
    const char* name;
    double depth;  // the feet, m
    gw::SwimInput in;
    double wish;
  } cases[] = {{"flat out, under", 5, {0, 0, 1}, 320 * 0.7},
               {"flat out, at the surface", 1.0, {0, 0, 1}, 320 * 0.7},
               {"no input, under", 5, {}, 60}};
  for (const auto& c : cases) {
    gw::SwimState st;
    st.p = {0, -c.depth * kUnits, 0};
    gl::Trace tr{c.name, {}, {}};
    for (int i = 0; i < 240; ++i) {
      gw::step_swimmer(world, s, c.in, st, i * kDt, kDt);
      tr.x.push_back(i * kDt);
      tr.y.push_back(std::sqrt(st.v.x * st.v.x + st.v.y * st.v.y + st.v.z * st.v.z));
    }
    line.traces.push_back(tr);
    const double formula = c.wish * std::fmin(1.0, s.accelerate / (s.friction * st.level));
    table.rows.push_back(
        {c.name, std::to_string(st.level), fixed(tr.y.back(), 2), fixed(formula, 2)});
    if (std::fabs(tr.y.back() - formula) > 1) {
      lab.fail(std::string("the kinematic regime settles ") + c.name + " at " +
                   fixed(tr.y.back(), 2) + ", the formula says " + fixed(formula, 2),
               "Read kinematic() in engine/water/swim.cpp (skill: water).");
    }
  }
  chart_shot(lab, "kinematic", gl::line_chart(line),
             "The kinematic regime's speed from rest over two seconds: flat out with the eyes "
             "under (level 3) and at the surface (level 2), and the slow sink with no input. Each "
             "settles at the wish where the friction allows it, else at accelerate / (friction "
             "level) of it.",
             "three curves rising fast and flattening inside a second; the surface one fastest, "
             "the sink slowest");
  lab.table(table);
  // The water jump.
  gl::LineChart arc;
  arc.frame = {"THE WATER JUMP", "from the waist, a lip reported in front", "forward (units)",
               "height over the surface (units)"};
  arc.spikes = false;
  gw::SwimState st;
  st.p = {0, -1.0 * kUnits, 0};
  gl::Trace feet{"feet", {}, {}}, eyes{"eyes", {}, {}};
  for (int i = 0; i < 70; ++i) {
    gw::step_swimmer(world, s, {0, 0, 1, true}, st, i * kDt, kDt, {lip, nullptr});
    feet.x.push_back(st.p.z);
    feet.y.push_back(st.p.y);
    eyes.x.push_back(st.p.z);
    eyes.y.push_back(st.p.y + s.eyes * kUnits);
  }
  arc.traces = {feet, eyes};
  arc.rules.push_back(0);
  chart_shot(lab, "water-jump", gl::line_chart(arc),
             "The water jump's arc: thrown up at 225 and forward at 50 units/s from the waist, the "
             "game's gravity acting. His feet come up to the lip's height at the surface, where "
             "the game's own step-up puts him on it.",
             "two smooth parallel arcs; the feet peaking just under 0, the eyes well over it");
}

void cold(gl::Lab& lab) {
  gl::LabTable table{"The cold by the water's temperature",
                     {"water C", "shock", "breath held still s", "swim failure min"},
                     {}};
  for (const double temp : {0.0, 2.0, 5.0, 10.0, 15.0, 20.0}) {
    const gw::World world = pool(temp);
    gw::SwimState st;
    st.p = {0, -3, 0};
    int i = 0;
    while (!st.drowning && i < 120 * 300) {
      gw::step_swimmer(world, gw::default_swimmer(), {}, st, i * kDt, kDt);
      ++i;
    }
    const bool shocked = temp < gw::default_swimmer().cold_from;
    table.rows.push_back({fixed(temp, 0), shocked ? "yes" : "no", fixed(i * kDt, 1),
                          shocked ? fixed(gw::failure_time(temp) / 60, 1) : "none"});
  }
  lab.table(table);
}

void run(gl::Lab& lab) {
  tread(lab);
  kinematic(lab);
  cold(lab);
}

const gl::LabSpec kSpec{
    "swim",
    gl::LabKind::picture,
    "engine",
    "engine/water/swim.hpp",
    "water",
    "Does a swimmer keep his head up when he swims and sink once his breath is gone, does the "
    "kinematic regime meet its numbers, and does the cold bite as it should?",
    {{"minutes", "3", "how long the treading chart runs"}},
    "--minutes=1",
    run};
GE_LAB(kSpec);

}  // namespace
