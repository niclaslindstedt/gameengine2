// LAB `move-strafe`: how much speed each movement row lets a player gain by strafe jumping, and
// why (engine/move/step.hpp's acceleration rule, strafe.hpp's best angle). Five charts: the
// classic row's speed over a run for a plain run, jumps straight on, and the strafe jumper on
// forward-and-strafe and on the strafe alone; every preset's strafe jumper as a share of its own
// run speed; the gain one step of air gives against the wish's angle off the velocity at four
// speeds, the best angle marked; the strafe jumper's path from above beside the straight hops;
// and the speed the strafe jumper reaches by the step rate (the gain is taken a step at a time,
// so it depends on the rate). Every number comes off the real step, so the digest holds the model.
//
// ge:summary Lab: strafe-jump speed gain charted by row, by wish angle, by step rate; the path
// ge:keywords lab move movement strafe strafe-jump bunny-hop bhop speed gain angle rate chart
// ge:keywords air-accelerate air-cap presets path tuning
// ge:skill move labs
// ge:test tests/move_step_test.cpp
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num_text.hpp"
#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "move/cvars.hpp"
#include "move/knobs.hpp"
#include "move/scenarios.hpp"
#include "move/strafe.hpp"

namespace {

namespace gl = ge::lab;
namespace mv = ge::move;
using ge::core::num::to_fixed;

struct Track {
  std::vector<double> t, speed, x, z;
  std::vector<double> jumps;  // s
  double unit = 1;
  static void on(void* ctx, const mv::MoveState& s, const mv::MoveCmd&, const mv::MoveEvents& ev) {
    auto* k = static_cast<Track*>(ctx);
    k->t.push_back(s.t);
    k->speed.push_back(mv::flat_length(s.vel) / k->unit);
    k->x.push_back(s.pos.x / k->unit);
    k->z.push_back(s.pos.z / k->unit);
    if (ev.has(mv::kJumped)) k->jumps.push_back(s.t);
  }
};

// The row retuned: the server's cfg (`--cfg`, its `sv_` knobs), then `--set` over it.
void tune(gl::Lab& lab, mv::MoveSpec& spec, bool report) {
  std::string error;
  const std::string& cfg = lab.arg("cfg");
  if (!cfg.empty() && !mv::apply_cfg(spec, cfg, error) && report)
    lab.fail("--cfg=" + cfg + ": " + error, "next: fix the cfg's lines named above and run again");
  if (!mv::apply_knobs(spec, lab.arg("set"), error) && report)
    lab.fail(error, "fix --set and run again");
}

mv::MoveSpec spec_for(gl::Lab& lab, const char* preset) {
  mv::MoveSpec spec = *mv::find_preset(preset);
  tune(lab, spec, true);
  return spec;
}

Track run(const char* id, const mv::MoveSpec& spec, double hz, double seconds, mv::MoveRun* out) {
  mv::MoveScenario sc = *mv::find_move_scenario(id);
  sc.seconds = seconds;
  Track k;
  k.unit = spec.unit;
  const mv::MoveRun r = mv::run_move_scenario(sc, spec, hz, Track::on, &k);
  if (out != nullptr) *out = r;
  return k;
}

void shoot(gl::Lab& lab, const std::string& id, const gl::Chart& chart, const std::string& caption,
           const std::string& look_for) {
  const std::string svg = gl::to_svg(chart.plot);
  lab.attach(id + ".svg", std::vector<uint8_t>(svg.begin(), svg.end()));
  lab.shot({id,
            chart.title,
            caption,
            look_for,
            {{"hz", lab.arg("hz")},
             {"seconds", lab.arg("seconds")},
             {"cfg", lab.arg("cfg")},
             {"set", lab.arg("set")}}},
           gl::to_picture(chart.plot));
  lab.note(gl::chart_text(chart, 0));
}

void run(gl::Lab& lab) {
  const double hz = lab.arg_num("hz"), seconds = lab.arg_num("seconds");
  const mv::MoveSpec classic = spec_for(lab, "classic_arena");

  // 1. The classic row: a run, hops straight on, the strafe jumper both ways.
  gl::LineChart speed;
  speed.frame = {"THE ARENA ROWS: SPEED OVER A RUN", "plan speed, m/s (32 units a metre)", "t (s)",
                 "speed (m/s)"};
  speed.spikes = false;
  struct Line {
    const char* label;
    const char* scenario;
    const char* preset;
  };
  const Line lines[] = {{"run, forward held", "run-stop", "classic_arena"},
                        {"jumps straight on", "bhop-straight", "classic_arena"},
                        {"classic: forward+strafe", "strafe-jump", "classic_arena"},
                        {"strafe_arena: forward+strafe", "strafe-jump", "strafe_arena"},
                        {"strafe_arena: strafe only", "strafe-jump-side", "strafe_arena"}};
  Track straight, strafer;
  for (const Line& l : lines) {
    mv::MoveRun r;
    const Track k = run(l.scenario, spec_for(lab, l.preset), hz, seconds, &r);
    if (std::string(l.scenario) == "run-stop") {
      // The run-stop scenario lets go at 1.5 s: keep only the run-up.
      gl::Trace t{l.label, {}, {}};
      for (size_t i = 0; i < k.t.size() && k.t[i] <= 1.5; i++) {
        t.x.push_back(k.t[i]);
        t.y.push_back(k.speed[i]);
      }
      speed.traces.push_back(t);
    } else {
      speed.traces.push_back({l.label, k.t, k.speed});
    }
    const bool classic_row = std::string(l.preset) == "classic_arena";
    if (std::string(l.scenario) == "bhop-straight") straight = k;
    if (std::string(l.scenario) == "strafe-jump" && classic_row) strafer = k;
    lab.metric(std::string(l.preset) + "." + l.scenario + ".end-speed", r.end_speed / classic.unit,
               "m/s");
  }
  speed.rules = {classic.max_speed / classic.unit};
  shoot(
      lab, "classic-speed", gl::line_chart(speed),
      "The arena rows' plan speed over a run: forward alone and forward with jump pressed on "
      "every landing, the view held straight (classic row); the strafe jumper turning the view "
      "to the best angle on the classic row, and on the strafe_arena row with forward and a "
      "strafe and with the strafe alone. The run is drawn to 1.5 s, where it lets go.",
      "the run and the straight hops level off at the run speed (10 m/s, under the hops' line); "
      "every strafe jumper climbs past it, the classic one slowest and strafe_arena's forward and "
      "strafe fastest; no line drops (each "
      "jump is taken on the first step on the ground, before friction)");

  // 2. Every preset's strafe jumper, as a share of its own run speed.
  gl::LineChart rows;
  rows.frame = {"EVERY ROW STRAFE JUMPING", "speed as a share of the row's own run speed", "t (s)",
                "speed / max speed"};
  rows.spikes = false;
  gl::LabTable table{"each row's strafe jumper",
                     {"row", "run m/s", "end m/s", "end / run", "jumps", "gain per jump m/s"},
                     {}};
  for (const char* const* p = mv::preset_names(); *p != nullptr; p++) {
    const mv::MoveSpec spec = spec_for(lab, *p);
    mv::MoveRun r;
    const Track k = run("strafe-jump", spec, hz, seconds, &r);
    gl::Trace t{*p, k.t, {}};
    for (const double v : k.speed) t.y.push_back(v * spec.unit / spec.max_speed);
    rows.traces.push_back(t);
    const double run_ms = spec.max_speed / spec.unit, end_ms = r.end_speed / spec.unit;
    const double per = r.jumps > 0 ? (end_ms - run_ms) / r.jumps : 0;
    table.rows.push_back({*p, to_fixed(run_ms, 2), to_fixed(end_ms, 2),
                          to_fixed(end_ms / run_ms, 2), std::to_string(r.jumps), to_fixed(per, 3)});
    lab.metric(std::string(*p) + ".strafe.end-share", end_ms / run_ms, "share");
  }
  rows.rules = {1};
  lab.table(table);
  shoot(lab, "rows", gl::line_chart(rows),
        "The same strafe jumper on every preset row, its speed over its own run speed (1 is "
        "running flat out).",
        "the classic and strafe_arena rows climb well past 1; modern_shooter levels off at its "
        "sprint (1.45, its air speed limit); platformer and walker stay at 1 (theirs is the run)");

  // 3. The gain of one step of air against the wish's angle off the velocity.
  gl::LineChart gain;
  gain.frame = {"ONE STEP OF AIR: GAIN AGAINST WISH ANGLE",
                "classic row, wish at run speed, the air's cap and factor; one 1/" + lab.arg("hz") +
                    " s step",
                "wish angle off the velocity (deg)", "speed gained a second if held (m/s per s)"};
  gain.spikes = false;
  const double dt = 1 / hz;
  const double cap = classic.air_speed_cap > 0 ? classic.air_speed_cap : classic.max_speed;
  for (const double ms : {10.0, 15.0, 20.0, 30.0}) {
    const double v0 = ms * classic.unit;
    gl::Trace t{to_fixed(ms, 0) + " m/s", {}, {}};
    for (int tenth = 700; tenth <= 950; tenth++) {
      const double deg = tenth / 10.0, th = deg * ge::core::kPi / 180;
      mv::Vec3 v{0, 0, v0};
      mv::accelerate(v, {ge::core::libm::sin(th), 0, ge::core::libm::cos(th)}, classic.max_speed,
                     cap, classic.air_accelerate, dt);
      t.x.push_back(deg);
      t.y.push_back((mv::flat_length(v) - v0) / classic.unit * hz);
    }
    gain.traces.push_back(t);
    const double best = mv::best_wish_angle(v0, classic.max_speed, cap, classic.air_accelerate, dt);
    gain.marks.push_back({best * 180 / ge::core::kPi, ""});
    lab.metric("classic.best-angle." + to_fixed(ms, 0) + "ms", best * 180 / ge::core::kPi, "deg");
  }
  gain.rules = {0};
  shoot(lab, "gain-angle", gl::line_chart(gain),
        "How much plan speed one step of air adds for each angle between the velocity and the "
        "wish direction, at four speeds, as a rate (the step's gain times the steps a second). "
        "Grey lines mark the best angle (strafe.hpp) at each speed.",
        "each line is zero where the wish is too near the velocity (its speed along the wish is "
        "past the cap), rises steeply to a peak just short of 90 degrees, then falls below zero "
        "(the wish pulls back on the velocity); the faster, the nearer 90 and the lower the peak; "
        "every peak sits on a grey line");

  // 4. The paths from above.
  gl::LineChart path;
  path.frame = {"THE PATHS FROM ABOVE", "classic row; the axes are not to the same scale", "z (m)",
                "x (m)"};
  path.spikes = false;
  path.traces.push_back({"jumps straight on", straight.z, straight.x});
  path.traces.push_back({"strafe jumper", strafer.z, strafer.x});
  for (const double t : strafer.jumps) {
    for (size_t i = 0; i < strafer.t.size(); i++) {
      if (strafer.t[i] >= t) {
        path.marks.push_back({strafer.z[i], ""});
        break;
      }
    }
  }
  shoot(lab, "path", gl::line_chart(path),
        "Where each runner went, seen from above: along the run (z) and across it (x). The grey "
        "lines mark the strafe jumper's take-offs.",
        "the straight hops run along x = 0; the strafe jumper weaves, one swing a jump, drifting "
        "to the side of its first swing (each swing turns as far, from straight on), and gets "
        "farther along in the same time");

  // 5. The speed reached by the step rate.
  gl::LineChart rates;
  rates.frame = {"STRAFE JUMPING BY STEP RATE",
                 "speed gained in " + lab.arg("seconds") + " s, as a share of the gain at " +
                     lab.arg("hz") + " steps a second",
                 "steps a second", "gain / gain at the lab's rate"};
  rates.spikes = false;
  rates.points = true;
  rates.rules = {1};
  for (const char* preset : {"classic_arena", "strafe_arena"}) {
    const mv::MoveSpec spec = spec_for(lab, preset);
    mv::MoveRun ref;
    run("strafe-jump", spec, hz, seconds, &ref);
    const double ref_gain = ref.end_speed - spec.max_speed;
    gl::Trace t{preset, {}, {}};
    for (const double r : lab.arg_nums("rates")) {
      mv::MoveRun res;
      run("strafe-jump", spec, r, seconds, &res);
      t.x.push_back(r);
      t.y.push_back(ref_gain > 0 ? (res.end_speed - spec.max_speed) / ref_gain : 0);
      lab.metric(std::string(preset) + ".rate-" + to_fixed(r, 0) + ".end-speed",
                 res.end_speed / spec.unit, "m/s");
    }
    rates.traces.push_back(t);
  }
  shoot(lab, "rates", gl::line_chart(rates),
        "The speed the strafe jumper gains over the run when the same row is stepped at each "
        "rate, against the gain at the lab's rate (the dashed line): the gain is taken a step at "
        "a time, so a game's step rate is part of its feel.",
        "both rows gain a little more the faster they are stepped (each step's addition costs "
        "less of the cap); a game that wants the same feel on every machine fixes its rate");
}

const gl::LabSpec kSpec{
    "move-strafe",
    gl::LabKind::sim,
    "engine",
    "engine/move/step.hpp (the acceleration rule), engine/move/strafe.hpp",
    "move",
    "How much speed does each movement row let a strafe jumper gain, at which angle, at which "
    "step rate?",
    {
        {"hz", "120", "steps a second"},
        {"seconds", "8", "the length of each run"},
        {"rates", "30,60,77,120,125,250", "the step rates of the rate chart"},
        {"cfg", "",
         "a server cfg whose sv_ knobs retune the row before --set (engine/move/cvars.hpp)"},
        {"set", "", "knobs set on every row, name=value,... (engine/move/knobs.cpp)"},
    },
    "--seconds=3 --rates=60,120",
    run,
};
GE_LAB(kSpec);

}  // namespace
