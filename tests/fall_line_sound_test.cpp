// Fall Line's sound (games/fall_line/sound): every bank sound has a sentence and a unique id, and
// every id the route plays is in the bank; the faders fold the master and the switch into the
// buses; the listener's chase seat is the tuned one; and the route plays the player's events
// (a gate, a landing in powder, a fall) and not a rival's, on the effects bus; and the play
// contract's sound (play_sound.hpp) plays a run of the game into an export's synth and nothing
// for a run of another game.
//
// ge:summary Tests for Fall Line's bank, faders, listener and event route
// ge:keywords test sound bank route listener faders mix_of events
// ge:skill audio sim testing
#include <set>
#include <string>
#include <vector>

#include "audio/meter.hpp"
#include "capture/sync_fixture.hpp"
#include "check.hpp"
#include "fall_line/sim/game.hpp"
#include "fall_line/sim/play.hpp"
#include "fall_line/sound/bank.hpp"
#include "fall_line/sound/mix.hpp"
#include "fall_line/sound/play_sound.hpp"
#include "fall_line/sound/route.hpp"
#include "play/game.hpp"

namespace {

namespace snd = fall_line::sound;
namespace sim = fall_line::sim;

float loudest_after(sim::GameState& state, std::vector<sim::GameEvent> events) {
  ge::audio::Synth synth;
  snd::RunSound sound = snd::make_run_sound();
  state.events = std::move(events);
  snd::run_sound_step(sound, synth, state);
  std::vector<float> out(size_t{2} * 24000);
  synth.render(out.data(), 24000);
  return ge::audio::measure(out).loudest_db;
}

}  // namespace

TEST(every_bank_sound_has_a_sentence_and_its_own_id) {
  std::set<std::string> ids;
  for (const auto& def : snd::kBank) {
    CHECK(!def.description.empty());
    CHECK(!def.voices.empty());
    ids.insert(std::string(def.id));
  }
  CHECK(ids.size() == snd::kBank.size());
  for (auto id : {snd::kLandSoft, snd::kLandHard, snd::kLandPowder, snd::kHitTree, snd::kBrushTree,
                  snd::kBump, snd::kGateTaken, snd::kGateMissed, snd::kReset, snd::kFinish,
                  snd::kWipeoutTree, snd::kWipeoutNose, snd::kWipeoutCatch}) {
    CHECK(ge::audio::find_sound(snd::kBank, id) != nullptr);
  }
}

TEST(the_faders_fold_the_master_and_the_switch_into_the_buses) {
  const snd::BusGains g = snd::mix_of({.master = 0.5f, .wind = 0.4f, .effects = 1});
  CHECK(g.wind == 0.2f && g.effects == 0.5f);
  const snd::BusGains off = snd::mix_of({.master = 1, .on = false});
  CHECK(off.wind == 0 && off.effects == 0);
  CHECK(snd::mix_of({.master = 3, .wind = -1}).wind == 0);
}

TEST(the_chase_seat_is_the_tuned_one_and_the_crane_is_quieter) {
  const snd::Listener& chase = snd::listener_for(snd::Seat::chase);
  const snd::Listener& high = snd::listener_for(snd::Seat::high);
  CHECK(chase.events == 1 && chase.muffle == 1);
  CHECK(high.wind < chase.wind && high.snow < chase.snow && high.muffle < 1);
}

TEST(the_route_plays_the_players_events_and_not_a_rivals) {
  sim::GameState state = sim::create_game({.seed = 38, .racers = 2, .version = {}});
  CHECK(loudest_after(state, {}) < -100);
  const float gate = loudest_after(state, {{sim::EventKind::gate, 0, 3, 0}});
  CHECK(gate > -45);
  CHECK(loudest_after(state, {{sim::EventKind::gate, 1, 3, 0}}) < -100);
  // The start gate is the same slap, quieter.
  CHECK(loudest_after(state, {{sim::EventKind::gate, 0, 0, 0}}) < gate - 3);
  // A shoulder is heard when he is one of the two.
  CHECK(loudest_after(state, {{sim::EventKind::shoulder, 1, 0, 6}}) > -50);
  // A fall into a trunk is the loudest thing a run makes.
  const float fall = loudest_after(
      state, {{sim::EventKind::wipeout, 0, static_cast<int32_t>(ge::skier::Wipeout::tree), 0}});
  CHECK(fall > gate);
}

TEST(a_landing_in_powder_is_the_whumpf_and_a_harsh_one_is_harder) {
  sim::GameState state = sim::create_game({.seed = 38, .racers = 1, .version = {}});
  state.racers[0].skier.packed = 1;
  const float soft = loudest_after(state, {{sim::EventKind::landing, 0, 0, 2}});
  const float hard = loudest_after(state, {{sim::EventKind::landing, 0, 0, 10}});
  CHECK(hard > soft);
  state.racers[0].skier.packed = 0;
  CHECK(loudest_after(state, {{sim::EventKind::landing, 0, 0, 2}}) > -45);
}

TEST(the_play_contracts_sound_plays_a_run_and_nothing_for_another_game) {
  namespace gp = ge::play;
  gp::Setup setup;
  std::string error;
  CHECK(gp::make_setup(sim::kGame, 38, {}, setup, error));
  auto run = sim::kGame.open(setup, error);
  CHECK_MSG(run != nullptr, error.c_str());
  if (!run) return;
  ge::audio::Synth synth;
  auto sound = snd::kSound.open(synth, error);
  CHECK(sound != nullptr);
  if (!sound) return;
  // Ten seconds down the race on the bot's hands (the start lights first), steered once a
  // frame at 60 fps.
  std::vector<double> input(sim::kGame.axes.size(), 0.0);
  std::vector<float> out;
  std::vector<float> chunk(size_t{2} * 800);
  for (int frame = 0; frame < 600; frame++) {
    for (int k = 0; k < 2; k++) {
      run->pilot(input);
      run->step(input);
      sound->step(*run);
    }
    sound->frame(*run, 1.0 / 60);
    synth.render(chunk.data(), 800);
    out.insert(out.end(), chunk.begin(), chunk.end());
  }
  // The wind of the ride bed rising off the start (about -40 dBFS at its loudest by 10 s).
  CHECK(ge::audio::measure(out).loudest_db > -60);
  // A run of another game is not a Fall Line run: nothing is routed.
  ge::audio::Synth quiet;
  auto other = snd::kSound.open(quiet, error);
  gp::Setup none;
  CHECK(gp::make_setup(ge::capture::kSyncGame, 0, {}, none, error));
  auto fixture = ge::capture::kSyncGame.open(none, error);
  if (!other || !fixture) return;
  other->step(*fixture);
  other->frame(*fixture, 1.0 / 60);
  quiet.render(chunk.data(), 800);
  CHECK(ge::audio::measure(chunk).loudest_db < -90);
}
