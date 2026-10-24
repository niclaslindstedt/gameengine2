// LAB `by-hand`: Fall Line played by hand (games/fall_line/controls). The bot skis one seed through
// the play contract; its input log is held on a simulated pad as a session's device events
// (script.hpp), and the session is played back into the hand loop the `fall_line` window runs
// (by_hand.hpp). Every step must read back the bot's own axes, so the hand run finishes on the
// bot's digest; its input log is written and replayed through the play contract (`ge replay`'s
// path) to the digest it records. The lab fails otherwise. Then it charts the pad against the
// axes the run was stepped on, and what the pad's motors were told (felt.hpp), and lists the
// default bindings.
//
// It writes three files beside its report: `session.events` (the pad's session, which
// `fall_line --events=<it>` plays in the window), `run.inputs` (the hand run's log, which
// `ge replay <it>` plays back) and `bindings.txt` (the defaults the player's file starts from).
//
// ge:summary Lab: the bot's run held on a pad and played by hand, its log replayed, charted, felt
// ge:keywords lab by-hand person pad session event-log input-log replay digest rumble felt
// ge:keywords bindings defaults fall-line window events
// ge:skill input labs
// ge:test tests/fall_line_controls_test.cpp
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/num_text.hpp"
#include "fall_line/controls/bindings.hpp"
#include "fall_line/controls/by_hand.hpp"
#include "fall_line/controls/felt.hpp"
#include "fall_line/controls/script.hpp"
#include "fall_line/sim/play.hpp"
#include "fall_line/sim/state.hpp"
#include "input/bindings_text.hpp"
#include "input/event_log.hpp"
#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "lab/series.hpp"
#include "lab/series_charts.hpp"
#include "play/inputs.hpp"
#include "play/session.hpp"

namespace {

namespace fc = fall_line::controls;
namespace fs = fall_line::sim;
namespace gl = ge::lab;
namespace in = ge::input;
namespace pl = ge::play;

std::string fixed(double v, int d) { return ge::core::num::to_fixed(v, d); }

std::vector<uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

// The game's bot down `setup`'s map for at most `most` steps, as the session would step it: each
// step's axes snapped, recorded and stepped.
struct BotRun {
  pl::InputLog log;
  uint64_t digest = 0;
  bool over = false;
};

std::optional<BotRun> bot_run(const pl::Setup& setup, int64_t most, std::string& error) {
  auto run = fs::kGame.open(setup, error);
  if (!run) return std::nullopt;
  pl::InputRecorder rec(pl::new_log(fs::kGame, setup));
  std::vector<double> in(fs::kGame.axes.size(), 0);
  for (int64_t i = 0; i < most && !run->over(); ++i) {
    std::fill(in.begin(), in.end(), 0);
    run->pilot(in);
    for (size_t a = 0; a < in.size(); ++a) in[a] = pl::snap_axis(in[a], fs::kGame.axes[a].kind);
    run->step(in);
    rec.record(in);
  }
  return BotRun{rec.log(), run->digest(), run->over()};
}

void run(gl::Lab& lab) {
  const double seed = lab.arg_num("seed");
  const auto most = static_cast<int64_t>(lab.arg_num("limit") * fs::kGame.hz);
  const std::string k = "seed-" + fixed(seed, 0);
  pl::Setup setup;
  std::string error;
  if (!pl::make_setup(fs::kGame, seed, {}, setup, error)) {
    lab.fail("the setup did not open: " + error, "next: pass --seed=<a number>");
    return;
  }
  const auto bot = bot_run(setup, most, error);
  if (!bot) {
    lab.fail("the bot's run on seed " + fixed(seed, 0) + " did not open: " + error,
             "Run `make sim SEEDS=" + fixed(seed, 0) + "` (skill: sim).");
    return;
  }
  const pl::InputLog& script_log = bot->log;
  const in::EventLog session = fc::pad_session(script_log);
  in::EventLog read;
  if (!in::read_events(in::write_events(session), read, error)) {
    lab.fail("the pad's session does not read back: " + error,
             "Fix engine/input/event_log.cpp (skill: input).");
    return;
  }

  in::ActionMap map = fc::default_map();
  in::read_bindings(fc::kExactPadBindings, map, error);
  auto hand = fc::open_hand_run(seed, 0, std::move(map), error);
  if (!hand) {
    lab.fail("the hand run did not open: " + error, "See games/fall_line/controls/by_hand.hpp.");
    return;
  }
  gl::Series s = gl::make_series({"t_s", "pad_x", "steer", "trigger", "tuck", "low", "high"});
  s.set("title", "seed " + fixed(seed, 0) + " by hand: the bot's run held on a simulated pad");
  s.set("x", "t_s");
  s.set("unit.t_s", "s");
  std::vector<gl::Mark> marks;
  in::InputState held;
  int64_t differ = 0;
  for (const in::LoggedFrame& f : read.frames) {
    in::update(held, f.q);
    fc::hand_frame(*hand, held, 1.0 / 120);
    const int64_t at = hand->steps;
    const int before = hand->pulses;
    const double now = static_cast<double>(f.t_ns) * 1e-9;
    fc::hand_step(*hand, held, now);
    const auto want = script_log.at(at);
    for (size_t i = 0; i < want.size(); ++i) {
      if (hand->step_in[i] != want[i]) {
        ++differ;
        break;
      }
    }
    const in::Slot* pad = nullptr;
    for (const in::Slot& slot : held.slots) {
      if (slot.active && slot.info.kind == in::DeviceKind::gamepad) pad = &slot;
    }
    float low = 0, high = 0;
    if (pad) {
      if (const in::DeviceFeedback* d = hand->felt.find(pad->info.id)) {
        in::rumble_at(*d, now, low, high);
      }
    }
    const double t = fs::state_of(*hand->run)->t;
    gl::add_row(
        s, {t, pad ? pad->axes[static_cast<int>(in::PadAxis::left_x)] : 0.0, hand->step_in[0],
            pad ? pad->axes[static_cast<int>(in::PadAxis::right_trigger)] : 0.0, hand->step_in[1],
            low, high});
    if (hand->pulses > before) {
      for (const fs::GameEvent& e : fs::state_of(*hand->run)->events) {
        if (e.racer == 0 && fc::pulse_for(e, 0).seconds > 0) {
          marks.push_back({t, fs::event_name(e.kind)});
          break;
        }
      }
    }
  }
  const pl::InputLog log = fc::hand_log(*hand);
  const auto replayed = pl::replay(fs::kGame, log, error);
  lab.metric(k + ".steps", static_cast<double>(hand->steps), "steps");
  lab.metric(k + ".frames", static_cast<double>(read.frames.size()), "count");
  lab.metric(k + ".steps-off-the-bots", static_cast<double>(differ), "count");
  lab.metric(k + ".pulses", static_cast<double>(hand->pulses), "count");
  lab.metric(k + ".finished", hand->run->over() ? 1 : 0, "count");
  if (differ != 0) {
    lab.fail(
        std::to_string(differ) + " steps read back other axes than the bot's",
        "See games/fall_line/controls/script.hpp (the pad's shape) and engine/play/devices.cpp.");
  }
  if (hand->run->digest() != bot->digest) {
    lab.fail("the hand run's digest " + pl::hex64(hand->run->digest()) + " is not the bot's " +
                 pl::hex64(bot->digest),
             "Run tests/fall_line_controls_test and diff the two logs with `ge diverge`.");
  }
  if (!replayed || replayed->digest != log.digest.value_or(0)) {
    lab.fail(
        "the hand run's log does not replay to its digest" + (error.empty() ? "" : ": " + error),
        "Run `ge replay previews/by-hand/run.inputs` (skill: agent-interface).");
  }
  lab.attach("session.events", bytes(in::write_events(session)));
  lab.attach("run.inputs", bytes(pl::to_text(log)));
  lab.attach("bindings.txt", bytes(fc::kDefaultBindings));

  gl::LabTable binds{"FALL LINE'S DEFAULT BINDINGS", {"context", "action", "source"}, {}};
  const in::ActionMap defaults = fc::default_map();
  for (const in::Context& c : defaults.contexts) {
    for (const in::Binding& b : c.bindings) {
      binds.rows.push_back({c.name, defaults.defs[b.action].name, in::source_text(b.source)});
    }
  }
  lab.table(binds);

  const std::string sub = "seed " + fixed(seed, 0) + ", " + std::to_string(hand->steps) +
                          " steps by hand, " + std::to_string(hand->pulses) + " pulses felt" +
                          (hand->run->over() ? ", finished" : "");
  const gl::SeriesCharts out{"",
                             k,
                             sub,
                             {{"seed", fixed(seed, 0)}, {"pilot", "the bot, on a pad"}},
                             marks,
                             {},
                             "Grey lines are the moments a pulse started."};
  gl::chart_series(lab, s, out,
                   {{"steer",
                     "THE PAD'S STICK AND THE STEER, SEED " + fixed(seed, 0),
                     "-1 left .. 1 right",
                     {"pad_x", "steer"},
                     "The pad's left stick, x, and the steer axis the run was stepped on, every "
                     "step: the bot's run held on a pad and read back through the bindings.",
                     "the two lines lie on each other: the device path adds and loses nothing"},
                    {"tuck",
                     "THE RIGHT TRIGGER AND THE TUCK, SEED " + fixed(seed, 0),
                     "0 .. 1",
                     {"trigger", "tuck"},
                     "The pad's right trigger and the tuck axis, every step.",
                     "the two lines lie on each other"},
                    {"felt",
                     "WHAT THE PAD FELT, SEED " + fixed(seed, 0),
                     "motor 0 .. 1",
                     {"low", "high"},
                     "The pad's two rumble motors, every step: the low (heavy) one and the high "
                     "(buzz), each pulse picked off the run's events (controls/felt.hpp).",
                     "a pulse at each hard landing and on any wipeout or trunk; quiet between"}});
}

const gl::LabSpec kSpec{
    "by-hand",
    gl::LabKind::drive,
    "game:fall_line",
    "games/fall_line/controls (the bindings, the hand loop, the felt) over engine/input and "
    "engine/play",
    "input",
    "Does a person's pad, read through Fall Line's bindings, ski exactly the run its axes say, "
    "and is the run's log the run again?",
    {
        {"seed", "38", "the map's seed"},
        {"limit", "600", "the longest stretch held on the pad, s"},
    },
    "--seed=38 --limit=20",
    run,
};
GE_LAB(kSpec);

}  // namespace
