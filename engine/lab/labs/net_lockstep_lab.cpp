// LAB `net-lockstep`: the same step everywhere, over the simulated network, once under each
// condition preset. A STRATEGY game of four seats runs in lockstep turns (each seat's hand gives
// its sixteen units an order a few times a second) and a FIGHTING game of two seats runs with
// rollback (each pad changes every few steps, each peer predicting the other's). It answers what
// a link costs a session that sends only inputs: how often a peer waits, how long an order or a
// pad change takes to be felt, the bytes a peer sends, how often and how deep a rollback goes,
// and that every peer's state agrees; then it plants a desync on one step of one seat and shows
// the step the peers name. The session's progress is charted over the run and the latencies and
// rollback depths drawn as boxes. Everything runs on a virtual clock off one seed, so the lab is a
// determinism test too.
//
// ge:summary Lab: lockstep turns and rollback over each preset: waits, latency, rollbacks, desync
// ge:keywords lab net lockstep rollback turns input-delay desync digest strategy fighting
// ge:keywords latency stall bandwidth preset multiplayer
// ge:skill net labs
// ge:test tests/net_lockstep_test.cpp
#include <cstdio>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "lab/stats.hpp"
#include "net/lockstep_trial.hpp"

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

void run(gl::Lab& lab) {
  gn::LockstepTrialConfig s;
  s.game = gn::LockstepGameKind::strategy;
  s.seats = static_cast<int>(lab.arg_num("seats"));
  s.seconds = lab.arg_num("seconds");
  s.seed = static_cast<uint32_t>(lab.arg_num("seed"));
  s.lockstep.turn_steps = static_cast<int>(lab.arg_num("turn-steps"));
  s.lockstep.delay = static_cast<int>(lab.arg_num("delay"));
  s.lockstep.send_every = static_cast<int>(lab.arg_num("send-every"));
  gn::LockstepTrialConfig f = s;
  f.game = gn::LockstepGameKind::fighting;
  f.seats = static_cast<int>(lab.arg_num("fighters"));
  f.lockstep.delay = static_cast<int>(lab.arg_num("rollback-delay"));
  f.lockstep.max_rollback = static_cast<int>(lab.arg_num("max-rollback"));
  f.lockstep.send_every = 1;
  const int plant = static_cast<int>(lab.arg_num("plant"));
  if (s.seats < 2 || s.seats > gn::kMaxSeats || f.seats < 2 || f.seats > 8 || s.seconds < 2 ||
      s.lockstep.turn_steps < 1 || s.lockstep.delay < 0 || f.lockstep.max_rollback < 1 ||
      s.lockstep.send_every < 1) {
    lab.fail(
        "--seats, --fighters, --seconds, --turn-steps, --delay or --max-rollback is out of "
        "range",
        "Pass --seats=2 to 32, --fighters=2 to 8, --seconds=2 or more, --turn-steps=1 or "
        "more, --delay=0 or more, --max-rollback=1 or more and --send-every=1 or more; skill: "
        "net.");
    return;
  }
  const int ms_turn = (1000 * s.lockstep.turn_steps + s.tick_hz / 2) / s.tick_hz;
  gl::LabTable strat{std::to_string(s.seats) + " strategy seats in lockstep, turns of " +
                         std::to_string(s.lockstep.turn_steps) + " steps (" +
                         std::to_string(ms_turn) + " ms), " + std::to_string(s.lockstep.delay) +
                         " turns of delay",
                     {"preset", "mesh", "pace", "waits", "order ms p50", "p95", "max", "up kbit/s",
                      "inputs kbit/s", "agree", "log"},
                     {}};
  gl::LabTable fight{std::to_string(f.seats) + " fighting seats with rollback, " +
                         std::to_string(f.lockstep.delay) + " step of delay, at most " +
                         std::to_string(f.lockstep.max_rollback) + " steps predicted",
                     {"preset", "pace", "waits", "pad ms p50", "p95", "rollbacks/s", "depth mean",
                      "deepest", "mispredicted", "up kbit/s", "agree", "log"},
                     {}};
  gl::LabTable desync{"A desync planted on step " + std::to_string(plant) + " of seat 1",
                      {"preset", "game", "caught", "named step", "seen after ms"},
                      {}};
  gl::LineChart progress{
      {"STRATEGY SESSION PROGRESS, EACH PRESET", "seat 0's steps run", "t (s)", "steps"},
      {},
      {},
      {},
      {},
      false,
      false,
      false};
  std::vector<gl::Sample> orders, pads, depths;
  for (const std::string& name : split(lab.arg("presets"))) {
    gn::LinkConditions link;
    if (!gn::conditions_preset(name, link)) {
      lab.fail("unknown preset '" + name + "'",
               "Pick from lan, broadband, wifi, mobile, bad, awful (engine/net/conditions.cpp); "
               "skill: net.");
      continue;
    }
    s.link = f.link = link;
    s.plant_step = f.plant_step = -1;
    const gn::LockstepTrial a = gn::run_lockstep_trial(s);
    const gn::LockstepTrial b = gn::run_lockstep_trial(f);
    const gl::Summary la = gl::summarize(a.latency_ms), lb = gl::summarize(b.latency_ms),
                      db = gl::summarize(b.depths);
    strat.rows.push_back({name, std::to_string(a.connected) + "/" + std::to_string(s.seats),
                          fmt(a.pace, 3), fmt(a.stall_share * 100, 1) + "%", fmt(la.p50, 0),
                          fmt(la.p95, 0), fmt(la.max, 0), fmt(a.up_kbps, 1), fmt(a.payload_kbps, 1),
                          a.agree ? "yes" : "NO", hex(a.hash)});
    fight.rows.push_back({name, fmt(b.pace, 3), fmt(b.stall_share * 100, 1) + "%", fmt(lb.p50, 0),
                          fmt(lb.p95, 0), fmt(static_cast<double>(b.rollbacks) / f.seconds, 1),
                          fmt(db.mean, 2), std::to_string(b.deepest),
                          std::to_string(b.mispredicted), fmt(b.up_kbps, 1), b.agree ? "yes" : "NO",
                          hex(b.hash)});
    const std::string k = name + "-";
    lab.metric(k + "pace", a.pace, "ratio");
    lab.metric(k + "waits", a.stall_share, "ratio");
    lab.metric(k + "order-ms-p95", la.p95, "ms");
    lab.metric(k + "up-kbps", a.up_kbps, "kbit/s");
    lab.metric(k + "rollbacks-per-s", static_cast<double>(b.rollbacks) / f.seconds, "count");
    lab.metric(k + "rollback-depth-mean", db.mean, "count");
    lab.metric(k + "pad-ms-p95", lb.p95, "ms");
    progress.traces.push_back({name, a.t_s, a.steps});
    orders.push_back({name, a.latency_ms});
    pads.push_back({name, b.latency_ms});
    depths.push_back({name, b.depths});
    for (const auto& [game, o, cfg] :
         {std::tuple{"strategy", &a, &s}, std::tuple{"fighting", &b, &f}}) {
      if (!o->agree)
        lab.fail(
            name + ": the " + game + " peers' states differ at step " + std::to_string(o->common),
            "Every peer must step the same inputs: read run_step and on_message in "
            "engine/net/lockstep.cpp, then run tests/net_lockstep_test.cpp; skill: net.");
      if (o->caught)
        lab.fail(name + ": a " + game + " desync was seen with none planted",
                 "Read check_digests and the digests' acks in engine/net/lockstep.cpp; skill: "
                 "net.");
      if (o->connected != cfg->seats)
        lab.fail(name + ": the " + game + " mesh did not come up",
                 "Read the connect loop in engine/net/lockstep_trial.cpp; skill: net.");
    }
    if (b.deepest > f.lockstep.max_rollback)
      lab.fail(name + ": a rollback ran " + std::to_string(b.deepest) + " steps again, past " +
                   std::to_string(f.lockstep.max_rollback),
               "Read update_rollback's stall in engine/net/lockstep.cpp; skill: net.");
    if (plant >= 0) {
      s.plant_step = f.plant_step = plant;
      s.plant_seat = f.plant_seat = 1;
      for (const auto& [game, cfg] : {std::pair{"strategy", &s}, std::pair{"fighting", &f}}) {
        const gn::LockstepTrial p = gn::run_lockstep_trial(*cfg);
        desync.rows.push_back({name, game, p.caught ? "yes" : "NO",
                               p.caught ? std::to_string(p.caught_step) : "-",
                               p.caught ? fmt(p.caught_after_ms, 0) : "-"});
        if (!p.caught || p.caught_step != static_cast<uint32_t>(plant))
          lab.fail(name + ": the " + std::string(game) + " desync planted on step " +
                       std::to_string(plant) + " was " +
                       (p.caught ? "named step " + std::to_string(p.caught_step) : "not seen"),
                   "Read check_digests in engine/net/lockstep.cpp: the first step two seats' "
                   "digests differ on is the desync; skill: net.");
        lab.metric(k + game + "-desync-seen-ms", p.caught_after_ms, "ms");
      }
    }
  }
  lab.table(std::move(strat));
  lab.table(std::move(fight));
  if (plant >= 0) lab.table(std::move(desync));
  lab.note(
      "mesh: peers connected to every other before the first step. pace: steps run per update, "
      "the slowest peer (1: never waits). waits: updates that ran no step for want of an input, "
      "mean over peers. order ms: from an order given to the step that applies it, on its own "
      "peer (the input delay plus the rest of the turn it was given in, plus any wait). pad ms: "
      "the same for a pad change in the fighting game. up: every packet a peer sent, headers and "
      "keepalives too; inputs: the lockstep messages alone. rollbacks/s: loads of an earlier "
      "state, all peers; depth: steps run again each time. agree: every peer's digest equal at "
      "the last step all of them ran. log: a hash of every peer's end. named step: the first "
      "step two seats' digests differ on; seen after: from the planted step being run to the "
      "first peer naming it.");
  const auto shoot = [&](const std::string& id, const gl::Chart& chart, const std::string& caption,
                         const std::string& look_for) {
    const std::string svg = gl::to_svg(chart.plot);
    lab.attach(id + ".svg", std::vector<uint8_t>(svg.begin(), svg.end()));
    lab.shot({id,
              chart.title,
              caption,
              look_for,
              {{"presets", lab.arg("presets")},
               {"seats", lab.arg("seats")},
               {"turn-steps", lab.arg("turn-steps")},
               {"delay", lab.arg("delay")},
               {"seed", lab.arg("seed")}}},
             gl::to_picture(chart.plot));
  };
  if (progress.traces.empty()) return;
  shoot("progress", gl::line_chart(progress),
        "The steps the strategy session has run by each second, one line a preset.",
        "lines one atop another: a link that loses or delays inputs makes the peers wait, and its "
        "line falls behind the LAN's by the share of updates spent waiting.");
  shoot("orders",
        gl::box_chart({"ORDER TO FELT, STRATEGY, EACH PRESET", "every order on its own peer",
                       "latency (ms)", ""},
                      orders),
        "How long an order takes from the hand to the step that applies it, one box a preset: "
        "p25 to p75, the median, whiskers p5 to p95.",
        "every box from the input delay to a turn past it on a good link; a bad one adds its "
        "waits on top.");
  shoot("pads",
        gl::box_chart({"PAD TO FELT, FIGHTING WITH ROLLBACK, EACH PRESET",
                       "every pad change on its own peer", "latency (ms)", ""},
                      pads),
        "How long a pad change takes to be felt on its own peer, one box a preset.",
        "every box at the one step of delay: rollback keeps the local player's own input "
        "immediate whatever the link, until the peers run out of prediction.");
  shoot("rollbacks",
        gl::box_chart({"ROLLBACK DEPTH, EACH PRESET", "steps run again per rollback", "steps", ""},
                      depths),
        "How many steps each rollback ran again, one box a preset.",
        "boxes that grow with the link's round trip, all under the most steps a peer may predict.");
}

const gl::LabSpec kSpec{
    "net-lockstep",
    gl::LabKind::sim,
    "engine",
    "engine/net (lockstep, lockstep_trial)",
    "net",
    "On each network condition, does a session that sends only inputs stay in step, how long "
    "does an order take to be felt, how deep does rollback go, and is a desync caught on the "
    "step it happens?",
    {
        {"presets", "lan,broadband,wifi,mobile,bad,awful", "condition presets, comma-separated"},
        {"seats", "4", "strategy seats (2 to 32)"},
        {"fighters", "2", "fighting seats (2 to 8)"},
        {"seconds", "20", "how long each session runs"},
        {"turn-steps", "6", "strategy: steps a turn"},
        {"delay", "2", "strategy: turns of input delay"},
        {"send-every", "1", "strategy: updates between a peer's messages"},
        {"rollback-delay", "1", "fighting: steps of input delay"},
        {"max-rollback", "8", "fighting: steps a peer may predict"},
        {"plant", "600", "the step a desync is planted on (-1: none)"},
        {"seed", "1", "the network's and the hands' stream"},
    },
    "--seconds=4 --presets=lan,bad --plant=120",
    run,
};
GE_LAB(kSpec);

}  // namespace
