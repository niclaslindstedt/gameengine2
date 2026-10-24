// Fall Line played by hand (games/fall_line/controls): the default bindings bind every axis, the
// player's file is saved and read back, the felt's pulse table, the rebinding walk, and the row's
// proof: the bot's run held on a simulated pad, written as a session's device events and played
// back into the hand loop, finishes the race on the bot's digest, and the input log the hand loop
// writes replays through the play contract (`ge replay`'s path) to that digest.
//
// ge:summary Tests for Fall Line's controls: bindings, player's file, rumble, rebinding, by hand
// ge:keywords test fall-line controls bindings player-file rumble felt rebind by-hand session
// ge:keywords event-log replay digest pad
// ge:skill input sim testing
#include <filesystem>
#include <string>

#include "check.hpp"
#include "fall_line/controls/bindings.hpp"
#include "fall_line/controls/by_hand.hpp"
#include "fall_line/controls/felt.hpp"
#include "fall_line/controls/rebinding.hpp"
#include "fall_line/controls/script.hpp"
#include "fall_line/sim/play.hpp"
#include "input/bindings_text.hpp"
#include "input/names.hpp"
#include "input/sim.hpp"
#include "play/devices.hpp"
#include "play/session.hpp"

namespace fc = fall_line::controls;
namespace fs = fall_line::sim;
namespace in = ge::input;
namespace pl = ge::play;

namespace {

constexpr double kSeed = 38;

}  // namespace

TEST(the_default_bindings_read_and_bind_every_axis_of_the_play_contract) {
  in::ActionMap map = fc::default_map();
  std::string error;
  CHECK_MSG(in::read_bindings(fc::kDefaultBindings, map, error), error.c_str());
  std::vector<std::string> missing;
  pl::bind_axes(map, fs::kGame.axes, &missing);
  CHECK_MSG(missing.empty(), "every axis has an action of its name");
  CHECK(map.find_context(fc::kRide) != nullptr && map.find_context(fc::kApp) != nullptr);
  // The file the game writes reads back to itself.
  in::ActionMap again = fc::default_map();
  CHECK(in::read_bindings(in::write_bindings(map), again, error));
  CHECK(in::write_bindings(again) == in::write_bindings(map));
}

TEST(the_keys_ramp_the_steer_in_and_a_pad_and_a_key_both_play) {
  in::SimInput sim;
  in::InputState s;
  const in::DeviceId kb = in::sim_plug(sim, in::sim_keyboard());
  const in::DeviceId pad = in::sim_plug(sim, in::sim_pad());
  in::ActionMap map = fc::default_map();
  in::ActionValues v = in::make_values(map);
  const auto steer = static_cast<size_t>(map.find_action("steer"));
  const auto tuck = static_cast<size_t>(map.find_action("tuck"));
  in::sim_frame(sim, s);
  in::sim_key(sim, kb, in::key::d, true);
  in::sim_frame(sim, s);
  in::evaluate(map, s, 1.0 / 60, v);
  CHECK_MSG(v[steer].x > 0 && v[steer].x < 0.2f, "a tap of D is a small turn right");
  for (int i = 0; i < 30; ++i) {
    in::sim_frame(sim, s);
    in::evaluate(map, s, 1.0 / 60, v);
  }
  CHECK_MSG(v[steer].x > 0.95f, "held, D eases in to a full turn");
  in::sim_axis(sim, pad, static_cast<int>(in::PadAxis::right_trigger), 0.6f);
  in::sim_frame(sim, s);
  in::evaluate(map, s, 1.0 / 60, v);
  CHECK_MSG(v[tuck].x > 0.5f, "the pad's right trigger tucks while the keys steer");
}

TEST(the_players_file_is_saved_and_read_back_and_a_broken_one_keeps_the_defaults) {
  const std::string dir =
      (std::filesystem::temp_directory_path() / "fall_line_controls_test").string();
  const std::string path = dir + "/sub/bindings.txt";
  std::filesystem::remove_all(dir);
  in::ActionMap map = fc::default_map();
  std::string error;
  CHECK_MSG(fc::load_player_map(path, map, error), "no file is no error: the defaults stand");
  const auto j = in::parse_source("key:j");
  CHECK(j.has_value());
  if (!j) return;
  CHECK(in::rebind(map, "ride", "jump", 0, *j, ""));
  CHECK_MSG(fc::save_player_map(path, map, error), error.c_str());
  in::ActionMap read = fc::default_map();
  CHECK(fc::load_player_map(path, read, error));
  CHECK(in::write_bindings(read) == in::write_bindings(map));
  {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    std::fputs("ge-bindings 1\ncontext ride\nbind jump key:nonsense\n", f);
    std::fclose(f);
  }
  in::ActionMap kept = fc::default_map();
  CHECK(!fc::load_player_map(path, kept, error));
  CHECK_MSG(error.find(path) != std::string::npos, "the error names the file");
  CHECK_MSG(in::write_bindings(kept) == in::write_bindings(fc::default_map()),
            "a file that does not read leaves the defaults");
  std::filesystem::remove_all(dir);
}

TEST(the_pulse_table_feels_the_players_own_events_and_never_cuts_a_stronger_one) {
  using K = fs::EventKind;
  CHECK(fc::pulse_for({K::landing, 0, 0, 1.2}, 0).seconds == 0);  // a soft landing: nothing
  const fc::Pulse hard = fc::pulse_for({K::landing, 0, 0, 5.5}, 0);
  CHECK(hard.low == 1.0f && hard.seconds > 0);
  CHECK(fc::pulse_for({K::wipeout, 1, 0, 0}, 0).seconds == 0);  // a rival's wipeout
  CHECK(fc::pulse_for({K::shoulder, 2, 0, 3}, 0).seconds > 0);  // met by a rival
  CHECK(fc::pulse_for({K::gate, 0, 3, 0}, 0).seconds == 0);     // a gate taken: nothing

  in::SimInput sim;
  in::InputState s;
  const in::DeviceId pad = in::sim_plug(sim, in::sim_pad());
  in::sim_plug(sim, in::sim_keyboard());
  in::sim_frame(sim, s);
  in::Feedback felt;
  const fs::GameEvent wipe{K::wipeout, 0, 0, 0};
  CHECK(fc::feel_events(felt, s, {&wipe, 1}, 0, 10.0) == 1);
  float low = 0, high = 0;
  in::rumble_at(*felt.find(pad), 10.1, low, high);
  CHECK(low == 1.0f);
  const fs::GameEvent missed{K::missed, 0, 2, 0};
  CHECK_MSG(fc::feel_events(felt, s, {&missed, 1}, 0, 10.2) == 0,
            "a buzz does not cut the wipeout short");
  in::rumble_at(*felt.find(pad), 10.6, low, high);
  CHECK_MSG(low == 0 && high == 0, "the pulse ends on its time");
}

TEST(the_rebinding_walk_binds_each_prompt_keeps_its_push_and_stops_on_escape) {
  in::SimInput sim;
  in::InputState s;
  const in::DeviceId kb = in::sim_plug(sim, in::sim_keyboard());
  in::sim_frame(sim, s);
  in::ActionMap map = fc::default_map();
  fc::Rebinding r;
  std::string said;
  fc::begin_rebinding(r, s, map, said);
  CHECK(said.find("STEER LEFT") != std::string::npos);
  in::sim_frame(sim, s);
  CHECK(fc::poll_rebinding(r, s, map, said) == fc::RebindStep::waiting);
  in::sim_key(sim, kb, in::key::q, true);
  in::sim_frame(sim, s);
  said.clear();
  CHECK(fc::poll_rebinding(r, s, map, said) == fc::RebindStep::bound);
  CHECK_MSG(said.find("STEER LEFT is key:q") != std::string::npos, said.c_str());
  CHECK(said.find("STEER RIGHT") != std::string::npos);
  const in::Context* ride = map.find_context("ride");
  const in::Binding& first = ride->bindings.front();
  CHECK(in::source_text(first.source) == "key:q" && first.value == -1.0f);
  in::sim_key(sim, kb, in::key::q, false);
  in::sim_frame(sim, s);
  fc::poll_rebinding(r, s, map, said);
  in::sim_key(sim, kb, in::key::escape, true);
  in::sim_frame(sim, s);
  CHECK(fc::poll_rebinding(r, s, map, said) == fc::RebindStep::stopped);
  CHECK(!r.active);
}

TEST(a_scripted_frame_is_granted_the_steps_due_by_its_time) {
  CHECK(fc::steps_due(0) == 1);  // the first frame takes the first step
  for (int64_t i = 1; i < 400000; i += 997) {
    CHECK(fc::steps_due(fc::frame_time(i)) == i + 1);
    CHECK_MSG(fc::steps_due(fc::frame_time(i) - 1) == i, "a nanosecond early, the step is not due");
  }
}

TEST(the_bots_run_held_on_a_pad_finishes_by_hand_and_its_log_replays_to_its_digest) {
  pl::Setup setup;
  std::string error;
  CHECK(pl::make_setup(fs::kGame, kSeed, {}, setup, error));
  const auto bot = pl::pilot_run(fs::kGame, setup, error);
  CHECK_MSG(bot.has_value(), error.c_str());
  if (!bot) return;
  CHECK_MSG(bot->over, "the bot finishes the seed");

  // The session as its text, read back: what the window's --events reads.
  in::EventLog written = fc::pad_session(bot->log);
  in::EventLog session;
  CHECK(in::read_events(in::write_events(written), session, error));
  CHECK(session.frames.size() == written.frames.size());

  in::ActionMap map = fc::default_map();
  CHECK(in::read_bindings(fc::kExactPadBindings, map, error));
  auto hand = fc::open_hand_run(kSeed, 0, std::move(map), error);
  CHECK_MSG(hand != nullptr, error.c_str());
  if (!hand) return;
  in::InputState held;
  for (const in::LoggedFrame& f : session.frames) {
    in::update(held, f.q);
    fc::hand_frame(*hand, held, 1.0 / 120);
    fc::hand_step(*hand, held, static_cast<double>(f.t_ns) * 1e-9);
  }
  CHECK_MSG(hand->run->over(), "the session finishes the race");
  CHECK(hand->steps == bot->steps);
  CHECK_MSG(hand->run->digest() == bot->digest, "the pad gave back the bot's every axis");
  CHECK_MSG(hand->pulses > 0, "the run was felt on the pad");

  const pl::InputLog log = fc::hand_log(*hand);
  pl::InputLog read;
  {
    const auto parsed = pl::parse_inputs(pl::to_text(log), error);
    CHECK_MSG(parsed.has_value(), error.c_str());
    if (!parsed) return;
    read = *parsed;
  }
  const auto replayed = pl::replay(fs::kGame, read, error);
  CHECK_MSG(replayed.has_value(), error.c_str());
  if (!replayed) return;
  CHECK_MSG(read.digest.has_value() && replayed->digest == read.digest.value_or(0),
            "the written log replays to the digest it records");
  CHECK(replayed->over);
}
