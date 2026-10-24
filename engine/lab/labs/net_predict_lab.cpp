// LAB `net-predict`: how fresh and how smooth a predicting client is, under each condition preset
// and at each tick rate, in three modes: FIXED (a fixed interpolation delay, the client's clock
// its own, corrections decayed, the others drawn straight between snapshots and held when they run
// late), SYNC (M17n: the server's slack steering the client's clock, the delay following what the
// snapshots needed) and SMOOTH (M17n and M17o: corrections drawn out through a critically damped
// spring, the others on cubic curves, carried on for a capped time when late and blended back),
// with the clients' clocks true and skewed. It answers what a player feels: how long an input
// takes from the hand to the server's step, how far behind the others are drawn and how often
// they stand still, how often a prediction is corrected, how jerky the drawn paths are, and how
// far off the others are drawn when the snapshots run late. Everything runs on a virtual clock
// off one seed.
//
// ge:summary Lab: prediction's freshness and smoothness at each tick rate: fixed, sync, smooth
// ge:keywords lab net predict prediction time-sync dilation clock-skew input-latency queue
// ge:keywords interpolation-delay jitter view-delay corrections multiplayer smoothing spring
// ge:keywords hermite cubic extrapolation jerk tick-rate tick-hz
// ge:skill net labs
// ge:test tests/net_predict_test.cpp
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "lab/stats.hpp"
#include "net/replicate_trial.hpp"

namespace {

namespace gl = ge::lab;
namespace gn = ge::net;

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at <= s.size()) {
    const size_t comma = s.find(',', at);
    const std::string part =
        s.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    if (!part.empty()) out.push_back(part);
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
  return out;
}

std::string fmt(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

std::string hex(uint64_t h) {
  char b[20];
  std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
  return b;
}

struct Mode {
  const char* name;
  bool sync, smooth;
};
constexpr Mode kModes[] = {{"fixed", false, false}, {"sync", true, false}, {"smooth", true, true}};

void set_mode(gn::ReplicateConfig& r, const Mode& m) {
  r.sync = m.sync;
  r.spring = m.smooth;
  r.cubic = m.smooth;
  if (!m.smooth) r.extrapolate_s = 0;
}

std::vector<int> rates(const std::string& s) {
  std::vector<int> out;
  for (const std::string& part : split(s)) out.push_back(std::atoi(part.c_str()));
  return out;
}

// THE PREDICTED WORLD (M17p): pairs of clients charging each other, with each other predicted or
// only interpolated.
void run_joust(gl::Lab& lab, const gn::ReplicateTrialConfig& base, int hz) {
  const double radius = lab.arg_num("predict-radius");
  gl::LabTable t{
      "The joust at " + std::to_string(hz) + " Hz: " + std::to_string(base.clients) +
          " clients in pairs charging each other, the partner predicted or not",
      {"preset", "partner", "contacts", "corr/min", "corr/contact", "corr p95 m", "worst m",
       "partner off m p95", "events ok", "undone", "missed", "heard", "replay steps", "max", "log"},
      {}};
  std::vector<gl::Sample> gaps;
  for (const std::string& name : split(lab.arg("presets"))) {
    gn::LinkConditions link;
    if (!gn::conditions_preset(name, link)) continue;
    for (const bool predict : {false, true}) {
      gn::ReplicateTrialConfig k = base;
      k.link = link;
      k.replicate.tick_hz = hz;
      k.collide = true;
      k.joust = true;
      k.replicate.predict_radius = predict ? radius : 0;
      k.replicate.predict_max_s = lab.arg_num("predict-max");
      const gn::ReplicateTrial o = gn::run_replicate_trial(k);
      const gl::Summary corr = gl::summarize(o.correction_m);
      const gl::Summary gap = gl::summarize(o.partner_gap);
      const uint64_t judged = o.confirmed + o.undone;
      t.rows.push_back(
          {name, predict ? "predicted" : "interpolated", std::to_string(o.contacts),
           fmt(o.corrections_per_min, 1),
           o.contacts ? fmt(o.corrections_per_min * k.clients * k.seconds / 60 /
                                static_cast<double>(o.contacts),
                            2)
                      : "-",
           fmt(corr.p95, 3), fmt(o.worst_correction, 2), fmt(gap.p95, 2),
           predict && judged
               ? fmt(100.0 * static_cast<double>(o.confirmed) / static_cast<double>(judged), 1) +
                     "%"
               : "-",
           predict ? std::to_string(o.undone) : "-", predict ? std::to_string(o.unpredicted) : "-",
           predict ? std::to_string(o.heard) : "-", predict ? fmt(o.resim_mean, 1) : "-",
           predict ? fmt(o.resim_max, 0) : "-", hex(o.hash)});
      const std::string key = "joust-" + name + (predict ? "-predicted-" : "-interpolated-");
      lab.metric(key + "corr-p95", corr.p95, "m");
      lab.metric(key + "partner-p95", gap.p95, "m");
      if (predict) {
        lab.metric(key + "undone", static_cast<double>(o.undone), "count");
        lab.metric(key + "unpredicted", static_cast<double>(o.unpredicted), "count");
      }
      gaps.push_back({name + (predict ? " pred" : " interp"), o.partner_gap});
    }
  }
  lab.table(std::move(t));
  const gl::Chart c =
      gl::box_chart({"THE JOUST: THE PARTNER AS DRAWN AGAINST WHERE THE SERVER HAS IT",
                     "client 1's partner, each tick, " + std::to_string(hz) + " Hz", "metres", ""},
                    gaps);
  const std::string svg = gl::to_svg(c.plot);
  lab.attach("joust.svg", std::vector<uint8_t>(svg.begin(), svg.end()));
  lab.shot({"joust",
            c.title,
            "How far from the server's place one client draws the partner it charges, one box a "
            "preset, interpolated (drawn in the past) against predicted (drawn in the present, "
            "stepped by its last input and pushed by the contacts).",
            "predicted boxes far lower than interpolated on every preset: the partner is where it "
            "is when they meet, not a round trip behind.",
            {{"presets", lab.arg("presets")},
             {"predict-radius", lab.arg("predict-radius")},
             {"predict-max", lab.arg("predict-max")},
             {"clients", lab.arg("clients")},
             {"seed", lab.arg("seed")}}},
           gl::to_picture(c.plot));
}

void run(gl::Lab& lab) {
  gn::ReplicateTrialConfig base;
  base.clients = static_cast<int>(lab.arg_num("clients"));
  base.npcs = static_cast<int>(lab.arg_num("npcs"));
  base.seconds = lab.arg_num("seconds");
  base.seed = static_cast<uint32_t>(lab.arg_num("seed"));
  const double skew = lab.arg_num("skew");
  const std::vector<int> hzs = rates(lab.arg("hz"));
  bool hz_ok = !hzs.empty();
  for (const int hz : hzs) hz_ok = hz_ok && hz >= 10 && hz <= 240;
  if (base.clients < 1 || base.clients > 32 || base.npcs < 0 || base.seconds < 2 || skew < 0 ||
      skew > 0.2 || !hz_ok) {
    lab.fail("--clients, --npcs, --seconds, --skew or --hz is out of range",
             "Pass --clients=1 to 32, --npcs=0 or more, --seconds=2 or more, --skew=0 to 0.2 "
             "and --hz=10 to 240 (comma-separated); skill: net.");
    return;
  }
  const std::string chart_preset = lab.arg("chart-preset");
  const std::string hz0 = std::to_string(hzs.front());
  gl::LabTable table{
      std::to_string(base.clients) + " clients predicting their own mover among " +
          std::to_string(base.npcs) + " others: fixed, time sync, smoothing",
      {"preset", "hz", "clocks", "mode", "input ms p95", "repeated", "view ms", "held", "corr/min",
       "snaps", "own jerk p99", "others p99", "late err m", "log"},
      {}};
  const auto line = [](const std::string& title, const std::string& sub, const std::string& x,
                       const std::string& y) {
    return gl::LineChart{{title, sub, x, y}, {}, {}, {}, {}, false, false, false};
  };
  gl::LineChart queue = line(
      "ONE CLIENT'S INPUTS WAITING ON THE SERVER",
      "preset " + chart_preset + " at " + hz0 + " Hz, clocks skewed " + fmt(skew * 100, 1) + "%",
      "t (s)", "inputs queued");
  gl::LineChart delay =
      line("THE VIEW DELAY WITH TIME SYNC, EACH PRESET",
           "how far behind the server one client draws the others, " + hz0 + " Hz", "t (s)",
           "view delay (ms)");
  gl::LineChart late =
      line("HOW FAR OFF THE OTHERS ARE DRAWN WHEN SNAPSHOTS RUN LATE",
           "preset " + chart_preset + " at " + hz0 + " Hz: held (sync) against carried on (smooth)",
           "past the newest snapshot (ms)", "mean error (m)");
  std::vector<gl::Sample> inputs, jerk;
  for (const std::string& name : split(lab.arg("presets"))) {
    gn::LinkConditions link;
    if (!gn::conditions_preset(name, link)) {
      lab.fail("unknown preset '" + name + "'",
               "Pick from lan, broadband, wifi, mobile, bad, awful (engine/net/conditions.cpp); "
               "skill: net.");
      continue;
    }
    for (const int hz : hzs) {
      for (const double sk : {0.0, skew}) {
        for (const Mode& m : kModes) {
          gn::ReplicateTrialConfig k = base;
          k.link = link;
          k.skew = sk;
          k.replicate.tick_hz = hz;
          set_mode(k.replicate, m);
          const gn::ReplicateTrial o = gn::run_replicate_trial(k);
          const gl::Summary in = gl::summarize(o.input_ms);
          const gl::Summary own = gl::summarize(o.own_jerk);
          const gl::Summary others = gl::summarize(o.other_jerk);
          const gl::Summary off = gl::summarize(o.late_gap);
          const double repeated =
              o.inputs + o.repeated
                  ? static_cast<double>(o.repeated) / static_cast<double>(o.inputs + o.repeated)
                  : 0;
          table.rows.push_back(
              {name, std::to_string(hz), sk > 0 ? "+-" + fmt(sk * 100, 1) + "%" : "true", m.name,
               fmt(in.p95, 0), fmt(repeated * 100, 1) + "%", fmt(o.view_delay_ms, 0),
               fmt(o.underrun * 100, 1) + "%", fmt(o.corrections_per_min, 1),
               std::to_string(o.snaps), fmt(own.p99, 0), fmt(others.p99, 0),
               off.n ? fmt(off.mean, 2) : "-", hex(o.hash)});
          const std::string key =
              name + "-" + std::to_string(hz) + (sk > 0 ? "-skew-" : "-") + m.name + "-";
          lab.metric(key + "input-ms-p95", in.p95, "ms");
          lab.metric(key + "view-ms", o.view_delay_ms, "ms");
          lab.metric(key + "held", o.underrun, "ratio");
          lab.metric(key + "others-jerk-p99", others.p99, "m/s3");
          const bool first = hz == hzs.front();
          if (first && sk == 0 && m.smooth) {
            inputs.push_back({name, o.input_ms});
            delay.traces.push_back({name, o.t_q, o.delay_ms});
          }
          if (first && sk == 0 && m.sync)
            jerk.push_back({name + (m.smooth ? " smooth" : " sync"), o.other_jerk});
          if (first && name == chart_preset && sk == skew && m.smooth == m.sync)
            queue.traces.push_back({m.sync ? "time sync" : "fixed", o.t_q, o.queue});
          if (first && name == chart_preset && sk == 0 && m.sync) {
            // The error binned by how late, a bin a tick.
            const double bin = 1000.0 / hz;
            std::vector<double> sum(24, 0), cnt(24, 0);
            for (size_t i = 0; i < o.late_ms.size(); i++) {
              const auto b = static_cast<size_t>(o.late_ms[i] / bin);
              if (b < sum.size()) sum[b] += o.late_gap[i], cnt[b]++;
            }
            gl::Trace tr{m.smooth ? "smooth: carried on" : "sync: held", {}, {}};
            for (size_t b = 0; b < sum.size(); b++) {
              if (cnt[b] < 5) continue;
              tr.x.push_back((static_cast<double>(b) + 0.5) * bin);
              tr.y.push_back(sum[b] / cnt[b]);
            }
            late.traces.push_back(tr);
          }
          if (m.sync && o.trimmed > 0)
            lab.fail(name + " at " + std::to_string(hz) +
                         " Hz: with time sync the server still "
                         "dropped " +
                         std::to_string(o.trimmed) + " inputs for a full queue",
                     "The clock should have slowed before the queue filled: read sync_clock in "
                     "engine/net/replicate_client.cpp and the slack window in "
                     "replicate_server.cpp; skill: net.");
          if (m.smooth && o.snaps > 0)
            lab.fail(name + ": " + std::to_string(o.snaps) +
                         " corrections snapped with snap_distance off",
                     "Nothing should snap below the distance: read reconcile in "
                     "engine/net/replicate_client.cpp; skill: net.");
        }
        if (skew == 0) break;
      }
    }
  }
  lab.table(std::move(table));
  run_joust(lab, base, hzs.front());
  lab.note(
      "clocks: true, or each client's clock run fast (even clients) or slow (odd) by the skew. "
      "fixed: the client's clock is its own, the others drawn 6 ticks behind the newest "
      "snapshot, straight between snapshots and held past the newest, a correction's error "
      "scaled by 0.85 a tick. sync: the server reports each client's slack (the fewest inputs "
      "left waiting over half a second) and the client speeds or slows its clock up to 3% to "
      "bring it to zero (one more for five seconds after it ran dry), and draws the others "
      "behind its reckoning of the server's tick by the most any of the last 64 snapshots "
      "needed, easing up fast and down slowly. smooth: sync, with a correction drawn out through "
      "a critically damped spring (half-life 1/15 s), the others on a cubic curve through the "
      "snapshots and carried on at their last velocity for up to 0.2 s past the newest, blended "
      "back over 0.13 s. Every duration is in seconds at any tick rate. input ms p95: from an "
      "input made to the server stepping it. repeated: server ticks that stepped a client's last "
      "input again. view: how far behind the server's clock the others are drawn. held: drawn "
      "ticks past the newest snapshot. corr/min: predictions moved, a client a minute. snaps: "
      "corrections shown at once. jerk p99 (m/s3): the drawn paths' third difference: own, client "
      "1's own path as drawn, less the four ticks after each change of its input (the game's own "
      "steps); others, the first eight of the crowd. late err: the mean distance from the server's "
      "truth of the others drawn "
      "past "
      "the newest snapshot.");
  const auto shoot = [&](const std::string& id, const gl::Chart& chart, const std::string& caption,
                         const std::string& look_for) {
    const std::string svg = gl::to_svg(chart.plot);
    lab.attach(id + ".svg", std::vector<uint8_t>(svg.begin(), svg.end()));
    lab.shot({id,
              chart.title,
              caption,
              look_for,
              {{"presets", lab.arg("presets")},
               {"hz", lab.arg("hz")},
               {"skew", lab.arg("skew")},
               {"clients", lab.arg("clients")},
               {"seed", lab.arg("seed")}}},
             gl::to_picture(chart.plot));
  };
  if (!queue.traces.empty())
    shoot("queue", gl::line_chart(queue),
          "How many of one client's inputs wait on the server, ten times a second, with its "
          "clock skewed: fixed against time sync.",
          "fixed: a fast clock's queue climbing (or a slow one's pinned at zero, repeating); "
          "time sync: the queue held near zero whatever the clock.");
  if (!delay.traces.empty())
    shoot(
        "delay", gl::line_chart(delay),
        "How far behind the server one client draws the others with time sync, one line a "
        "preset.",
        "each line settling to what the latest snapshots needed: low on a clean link, higher on "
        "a jittery one, rising quickly after a late snapshot and sinking slowly, never stepping.");
  if (!inputs.empty())
    shoot("inputs",
          gl::box_chart({"INPUT TO SERVER STEP, TIME SYNC ON, EACH PRESET",
                         "every input of every client, " + hz0 + " Hz", "latency (ms)", ""},
                        inputs),
          "How long an input takes from the hand to the server's step, one box a preset.",
          "boxes at the one-way latency plus a tick, wider as the link jitters.");
  if (!jerk.empty())
    shoot("jerk",
          gl::box_chart({"THE OTHERS' DRAWN PATHS: JERK",
                         "the first eight of the crowd as client "
                         "1 draws them, " +
                             hz0 + " Hz",
                         "jerk (m/s3)", ""},
                        jerk),
          "The drawn paths' jerk, one box a preset and mode: sync (straight between snapshots, "
          "held when late) against smooth (cubic, carried on).",
          "smooth's boxes lower than sync's on every preset, most on the jittery ones: the curve "
          "has no corner at each snapshot and a late one no stop and start.");
  if (!late.traces.empty())
    shoot("late", gl::line_chart(late),
          "The others drawn past the newest snapshot, binned by how far past (a bin a tick): their "
          "mean distance from where the server had them.",
          "held: the error growing with lateness at the movers' speed; carried on: far lower up "
          "to the cap (0.2 s), then growing at the same rate as held.");
}

const gl::LabSpec kSpec{
    "net-predict",
    gl::LabKind::sim,
    "engine",
    "engine/net (replicate, replicate_trial)",
    "net",
    "Under each condition and tick rate, how fresh is a predicting client (input to server "
    "step, view delay) and how smooth (held frames, corrections, the drawn paths' jerk, the error "
    "when snapshots run late), fixed against time sync against smoothing, clocks true and skewed?",
    {
        {"presets", "lan,broadband,wifi,mobile,bad,awful", "condition presets, comma-separated"},
        {"clients", "16", "clients, each predicting its own mover (1 to 32)"},
        {"npcs", "32", "movers the server moves"},
        {"seconds", "20", "how long each trial runs"},
        {"skew", "0.02", "clients' clocks this much fast or slow in the skewed rows (0: none)"},
        {"hz", "72,120",
         "tick rates, comma-separated (10 to 240): 72 a shooter's, 120 Fall Line's"},
        {"chart-preset", "bad", "the preset whose queue and late error are charted"},
        {"predict-radius", "6", "the joust's predicted world, metres (M17p)"},
        {"predict-max", "0.1", "the joust's horizon: past it the others are drawn interpolated, s"},
        {"seed", "1", "the network's, the hands' and the crowd's stream"},
    },
    "--seconds=4 --presets=lan,bad --clients=4 --npcs=8 --hz=72",
    run,
};
GE_LAB(kSpec);

}  // namespace
