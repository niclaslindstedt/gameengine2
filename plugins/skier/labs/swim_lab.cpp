// LAB `skier-swim`: the dressed skier through the ice (plugins/skier/swim.hpp on the engine's
// swimmer): his eyes against the surface over eight minutes of 1 degree water, lying still and
// swimming up, beside a swimsuit's swimmer doing the same; and his buoyancy's budget as his
// clothes let their air go and soak (the trapped air, the water carried, and what he weighs in
// the water with his lungs half full).
//
// ge:summary Lab: the dressed skier in icy water: floating on his jacket's air, soaking, sinking
// ge:keywords lab skier swim dressed soak trapped-air sink ice cold buoyancy budget
// ge:skill skier water labs
// ge:test tests/skier_swim_test.cpp
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "skier/swim.hpp"

namespace {

namespace gl = ge::lab;
namespace gw = ge::water;

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
  lab.shot({id, chart.title, caption, look_for, {{"minutes", lab.arg("minutes")}}},
           gl::to_picture(chart.plot));
}

void sinking(gl::Lab& lab) {
  const double minutes = lab.arg_num("minutes");
  gw::World world;
  gw::add_ocean(world, 0, {.look = "glacial", .density = 1000, .temperature = 1});
  gl::LineChart line;
  line.frame = {"THROUGH THE ICE: A SKIER AND A SWIMMER IN 1 DEGREE WATER",
                "the eyes against the surface (above 0: out)", "t (s)",
                "eyes over the surface (m)"};
  line.spikes = false;
  gl::LabTable table{
      "Through the ice",
      {"who", "does", "eyes under for good s", "out of breath s", "carried kg", "depth at end m"},
      {}};
  const struct {
    const char* who;
    const char* does;
    gw::SwimSpec spec;
    double up;
  } cases[] = {{"skier", "lies still", ge::skier::dressed_skier(), 0},
               {"skier", "swims up", ge::skier::dressed_skier(), 1},
               {"swimmer", "lies still", gw::default_swimmer(), 0},
               {"swimmer", "swims up", gw::default_swimmer(), 1}};
  for (const auto& c : cases) {
    gw::SwimState st;
    st.p = {0, -0.8, 0};
    gl::Trace tr{std::string(c.who) + " " + c.does, {}, {}};
    double under = -1, out = -1;
    double last_out = 0;  // the last moment the eyes were out
    for (int i = 0; i < static_cast<int>(minutes * 60 * 120); ++i) {
      gw::step_swimmer(world, c.spec, {0, c.up, 0}, st, i * kDt, kDt);
      if (st.level < 3) last_out = i * kDt;
      if (out < 0 && st.drowning) out = i * kDt;
      if (i % 60 == 0) {
        tr.x.push_back(i * kDt);
        tr.y.push_back(std::fmax(-8.0, st.p.y + c.spec.eyes));
      }
    }
    if (st.level == 3) under = last_out;
    line.traces.push_back(tr);
    table.rows.push_back({c.who, c.does, under < 0 ? "never" : fixed(under, 0),
                          out < 0 ? "never" : fixed(out, 0), fixed(st.carried, 1),
                          fixed(st.p.y, 1)});
    lab.metric(std::string("eyes-under.") + c.who + "." + (c.up > 0 ? "swims" : "still"), under,
               "s");
  }
  line.rules.push_back(0);
  chart_shot(
      lab, "through-the-ice", gl::line_chart(line),
      "A dressed skier and a swimmer in a swimsuit dropped into 1 degree water, lying still "
      "and swimming up flat out. The skier floats at first on the air in his jacket, then "
      "his clothes let it go and soak, his eyes go under, and once his breath is gone he "
      "sinks slowly (the chart stops at 8 m down). Swimming in the soaked suit only buys "
      "time. The swimmer has no air in his clothes to float on: lying still he is gone at "
      "once, and swimming, the cold shock halves his stroke and his eyes keep dipping under.",
      "the skier's lines near 0 for the first minutes then falling, the swimming one later; "
      "the still swimmer falling at once, the swimming one hovering about 0");
  lab.table(table);
}

void budget(gl::Lab& lab) {
  const gw::SwimSpec s = ge::skier::dressed_skier();
  gl::LineChart line;
  line.frame = {"THE SKIER'S BUOYANCY AS HIS CLOTHES SOAK",
                "kilograms; what he weighs in the water: under 0 he floats, over 0 he sinks",
                "t in the water (s)", "kg"};
  line.spikes = false;
  gl::Trace air{"trapped air (kg it lifts)", {}, {}}, carried{"water soaked up (kg)", {}, {}},
      net{"what he weighs in the water, lungs half full (kg)", {}, {}};
  for (int step = 0; step <= 480; step += 4) {
    const gw::Budget b = gw::budget(s, step);
    const double lungs = 0.5 * (s.lungs + s.residual);
    air.x.push_back(step);
    air.y.push_back(b.air * 1000);
    carried.x.push_back(step);
    carried.y.push_back(b.carried);
    net.x.push_back(step);
    net.y.push_back(b.mass - (b.volume + lungs) * 1000);
  }
  line.traces = {air, carried, net};
  line.rules.push_back(0);
  lab.metric("net-at-0s", net.y.front(), "kg");
  lab.metric("net-at-480s", net.y.back(), "kg");
  chart_shot(
      lab, "budget", gl::line_chart(line),
      "The dressed skier's budget against his time in the water: the air his clothes trap "
      "(and the kilograms it lifts) leaks away, the water they soak climbs (weighing nothing "
      "in the water and everything out of it), and what he weighs in the water crosses from "
      "floating to sinking.",
      "the air falling from about 23 kg, the soak rising to about 9 kg, the net climbing "
      "from about -22 kg and crossing 0 after about six minutes");
}

void run(gl::Lab& lab) {
  sinking(lab);
  budget(lab);
}

const gl::LabSpec kSpec{
    "skier-swim",
    gl::LabKind::picture,
    "plugin:skier",
    "plugins/skier/swim.hpp",
    "skier",
    "Does a skier through the ice float on his clothes' air, soak, and sink slowly unless he "
    "swims, and how does he compare with a swimmer?",
    {{"minutes", "8", "how long the skier is in the water"}},
    "--minutes=2",
    run};
GE_LAB(kSpec);

}  // namespace
