// LAB `move-trace`: what each movement scenario does, drawn (engine/move/scenarios.hpp over
// step.hpp). One picture a scenario: the path of the feet and the top of the head from the side
// (along the run against height, the world's solids in grey) or from above when the path turns,
// the take-offs and landings marked; one picture of every row's running jump; and one table of
// every run's numbers and digest. `--preset` runs a scenario on another row and `--set` retunes
// one, so a knob's change is a before-and-after of these pictures.
//
// ge:summary Lab: every movement scenario's path drawn from the side or above, with its numbers
// ge:keywords lab move movement trace trajectory side-view path jump arc stairs ramp crouch
// ge:keywords ledge coyote slide air-control scenario presets
// ge:skill move labs
// ge:test tests/move_scenarios_test.cpp
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "core/num_text.hpp"
#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "move/cvars.hpp"
#include "move/knobs.hpp"
#include "move/scenarios.hpp"

namespace {

namespace gl = ge::lab;
namespace mv = ge::move;
using ge::core::num::to_fixed;

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

struct Track {
  double unit = 1;
  const mv::MoveSpec* spec = nullptr;
  std::vector<double> x, y, z, head;
  std::vector<std::pair<double, std::string>> events;  // (z, what)
  static void on(void* ctx, const mv::MoveState& s, const mv::MoveCmd&, const mv::MoveEvents& ev) {
    auto* k = static_cast<Track*>(ctx);
    const double u = k->unit;
    k->x.push_back(s.pos.x / u);
    k->y.push_back(s.pos.y / u);
    k->z.push_back(s.pos.z / u);
    k->head.push_back((s.pos.y + (s.ducked ? k->spec->crouch_height : k->spec->height)) / u);
    const char* what = ev.has(mv::kJumped)       ? "JUMP"
                       : ev.has(mv::kAirJumped)  ? "AIR JUMP"
                       : ev.has(mv::kLanded)     ? "LAND"
                       : ev.has(mv::kSlideStart) ? "SLIDE"
                       : ev.has(mv::kLeftGround) ? "OFF"
                                                 : nullptr;
    if (what != nullptr) k->events.emplace_back(s.pos.z / u, what);
  }
};

// A brush's outline seen from the side (z across, y up), clamped to a window of z: the convex
// hull of its corners, closed, then a gap (NaN) before the next.
void outline(const mv::BoxWorld& w, double u, double z0, double z1, gl::Trace& t) {
  for (const mv::Brush& b : w.brushes) {
    std::vector<std::pair<double, double>> p;
    for (uint32_t i = 0; i < b.corners; i++) {
      const mv::Vec3& c = w.corners[b.first_corner + i];
      p.emplace_back(std::clamp(c.z / u, z0, z1), c.y / u);
    }
    std::sort(p.begin(), p.end());
    p.erase(std::unique(p.begin(), p.end()), p.end());
    if (p.size() < 2) continue;
    auto turn = [](const auto& o, const auto& a, const auto& b2) {
      return (a.first - o.first) * (b2.second - o.second) -
             (a.second - o.second) * (b2.first - o.first);
    };
    std::vector<std::pair<double, double>> h;
    for (int pass = 0; pass < 2; pass++) {
      const size_t base = h.size();
      for (size_t k = 0; k < p.size(); k++) {
        const auto& q = pass == 0 ? p[k] : p[p.size() - 1 - k];
        while (h.size() >= base + 2 && turn(h[h.size() - 2], h.back(), q) <= 0) h.pop_back();
        h.push_back(q);
      }
      h.pop_back();
    }
    for (const auto& q : h) {
      t.x.push_back(q.first);
      t.y.push_back(q.second);
    }
    t.x.push_back(h.front().first);
    t.y.push_back(h.front().second);
    t.x.push_back(h.front().first);
    t.y.push_back(kNan);
  }
}

// The row retuned: the server's cfg (`--cfg`, its `sv_` knobs), then `--set` over it.
void tune(gl::Lab& lab, mv::MoveSpec& spec, bool report) {
  std::string error;
  const std::string& cfg = lab.arg("cfg");
  if (!cfg.empty() && !mv::apply_cfg(spec, cfg, error) && report)
    lab.fail("--cfg=" + cfg + ": " + error, "next: fix the cfg's lines named above and run again");
  if (!mv::apply_knobs(spec, lab.arg("set"), error) && report)
    lab.fail(error, "fix --set and run again");
}

mv::MoveSpec spec_for(gl::Lab& lab, const mv::MoveScenario& sc) {
  const std::string& preset = lab.arg("preset");
  const mv::MoveSpec* base = mv::find_preset(preset.empty() ? sc.preset : preset.c_str());
  if (base == nullptr) {
    lab.fail("move-trace: no preset '" + preset + "'",
             "next: run again with --preset= one of classic_arena, strafe_arena, "
             "modern_shooter, platformer, walker");
    base = mv::find_preset(sc.preset);
  }
  mv::MoveSpec spec = *base;
  tune(lab, spec, true);
  return spec;
}

void shoot(gl::Lab& lab, const std::string& id, const gl::Chart& chart, const std::string& caption,
           const std::string& look_for, const std::string& preset) {
  const std::string svg = gl::to_svg(chart.plot);
  lab.attach(id + ".svg", std::vector<uint8_t>(svg.begin(), svg.end()));
  lab.shot({id,
            chart.title,
            caption,
            look_for,
            {{"preset", preset},
             {"hz", lab.arg("hz")},
             {"cfg", lab.arg("cfg")},
             {"set", lab.arg("set")}}},
           gl::to_picture(chart.plot));
}

void draw(gl::Lab& lab, const mv::MoveScenario& sc, const mv::MoveSpec& spec, const Track& k,
          const char* preset) {
  double xs = 0;
  for (const double v : k.x) xs = std::max(xs, std::fabs(v));
  const bool top = xs > 0.5;
  gl::LineChart c;
  c.spikes = false;
  std::string title = sc.id;
  std::transform(title.begin(), title.end(), title.begin(),
                 [](char ch) { return ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 32) : ch; });
  if (top) {
    c.frame = {title + " FROM ABOVE", sc.what, "along the run, z (m)", "across, x (m)"};
    c.traces.push_back({"feet", k.z, k.x});
  } else {
    c.frame = {title + " FROM THE SIDE", sc.what, "along the run, z (m)", "height (m)"};
    double z0 = 0, z1 = 0;
    for (const double v : k.z) {
      z0 = std::min(z0, v);
      z1 = std::max(z1, v);
    }
    gl::Trace world{"world", {}, {}};
    outline(mv::scenario_world(sc, spec), spec.unit, z0 - 1, z1 + 1, world);
    c.traces.push_back(world);  // first, so the paths are drawn over it
    c.traces.push_back({"top of the head", k.z, k.head});
    c.traces.push_back({"feet", k.z, k.y});
  }
  for (const auto& e : k.events) c.marks.push_back({e.first, e.second});
  const std::string look = std::string(sc.what);
  shoot(lab, sc.id, gl::line_chart(c),
        std::string(top ? "From above: " : "From the side: ") + "the path of the feet" +
            (top ? ""
                 : " and the top of the head (the hull's height: it "
                   "drops while crouched) over the world's solids, "
                   "each drawn as its outline") +
            ". Grey lines mark take-offs, landings, slides and walking off. Row: " + preset + ".",
        look, preset);
}

void run(gl::Lab& lab) {
  const double hz = lab.arg_num("hz");
  const std::string& only = lab.arg("scenario");
  gl::LabTable table{"every scenario",
                     {"scenario", "row", "top m/s", "distance m", "apex m", "end height m", "jumps",
                      "landings", "steps up", "impact m/s", "digest"},
                     {}};
  for (const mv::MoveScenario& sc : mv::move_scenarios()) {
    if (only != "all" && only != sc.id) continue;
    const mv::MoveSpec spec = spec_for(lab, sc);
    const std::string preset = lab.arg("preset").empty() ? sc.preset : lab.arg("preset");
    Track k;
    k.unit = spec.unit;
    k.spec = &spec;
    const mv::MoveRun r = mv::run_move_scenario(sc, spec, hz, Track::on, &k);
    const double u = spec.unit;
    char digest[24];
    std::snprintf(digest, sizeof digest, "%016llx", static_cast<unsigned long long>(r.digest));
    table.rows.push_back({sc.id, preset, to_fixed(r.top_speed / u, 2), to_fixed(r.distance / u, 2),
                          to_fixed(r.apex / u, 3), to_fixed(r.end_height / u, 3),
                          std::to_string(r.jumps + r.air_jumps), std::to_string(r.landings),
                          std::to_string(r.steps_up), to_fixed(r.max_impact / u, 2), digest});
    const std::string key = std::string(sc.id) + ".";
    lab.metric(key + "top-speed", r.top_speed / u, "m/s");
    lab.metric(key + "apex", r.apex / u, "m");
    lab.metric(key + "end-height", r.end_height / u, "m");
    draw(lab, sc, spec, k, preset.c_str());
  }
  lab.table(table);

  if (only != "all") return;
  // Every row's running jump, side by side.
  gl::LineChart arcs;
  arcs.frame = {"EVERY ROW'S RUNNING JUMP", "a jump at full run, from take-off", "distance (m)",
                "height (m)"};
  arcs.spikes = false;
  for (const char* const* p = mv::preset_names(); *p != nullptr; p++) {
    // The strafe arena's ground and jump are the classic row's: its arc would hide that one.
    if (std::string(*p) == "strafe_arena") continue;
    mv::MoveSpec spec = *mv::find_preset(*p);
    tune(lab, spec, false);
    Track k;
    k.unit = spec.unit;
    k.spec = &spec;
    mv::run_move_scenario(*mv::find_move_scenario("run-jump"), spec, hz, Track::on, &k);
    // From the take-off to the first landing after it.
    double z0 = -1, z1 = -1;
    for (const auto& e : k.events) {
      if (e.second == "JUMP" && z0 < 0) z0 = e.first;
      if (e.second == "LAND" && z0 >= 0 && e.first > z0 && z1 < 0) z1 = e.first;
    }
    gl::Trace t{*p, {}, {}};
    for (size_t i = 0; i < k.z.size(); i++) {
      if (z0 < 0 || k.z[i] < z0) continue;
      if (z1 >= 0 && k.z[i] > z1) break;
      t.x.push_back(k.z[i] - z0);
      t.y.push_back(k.y[i] - k.y.front());
    }
    arcs.traces.push_back(t);
  }
  shoot(lab, "arcs", gl::line_chart(arcs),
        "Each preset row's running jump from the side, from its take-off to its landing, in metres "
        "(the strafe arena's is the classic row's).",
        "every arc is a smooth parabola (the platformer's falls steeper than it rises); the "
        "classic row's goes about 1.4 m up and 6.7 m along; the walker's is the shortest",
        "all");
}

const gl::LabSpec kSpec{
    "move-trace",
    gl::LabKind::scenario,
    "engine",
    "engine/move/scenarios.hpp over engine/move/step.hpp",
    "move",
    "What does each movement scenario do: where do the feet and the head go, and what happens on "
    "the way?",
    {
        {"scenario", "all", "one scenario's id, or all"},
        {"preset", "", "run on this row instead of each scenario's own"},
        {"cfg", "",
         "a server cfg whose sv_ knobs retune the row before --set (engine/move/cvars.hpp)"},
        {"set", "", "knobs set on the row, name=value,... (engine/move/knobs.cpp)"},
        {"hz", "120", "steps a second"},
    },
    "--scenario=stairs",
    run,
};
GE_LAB(kSpec);

}  // namespace
