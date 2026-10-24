// The run played by hand: open, the frame, the step, the log.
//
// ge:summary open_hand_run, hand_frame, hand_step, hand_log: a person's run on the play contract
// ge:keywords fall-line by-hand hand-run step log
// ge:skill input sim
// ge:test tests/fall_line_controls_test.cpp
#include "fall_line/controls/by_hand.hpp"

#include <map>

#include "core/num.hpp"
#include "fall_line/controls/felt.hpp"
#include "fall_line/sim/play.hpp"
#include "fall_line/sim/state.hpp"

namespace fall_line::controls {

namespace in = ge::input;
namespace pl = ge::play;

std::unique_ptr<HandRun> open_hand_run(double seed, int rivals, in::ActionMap map,
                                       std::string& error,
                                       std::shared_ptr<const ge::mountain::mapgen::Level> level) {
  const pl::GameSpec& spec = sim::kGame;
  pl::Setup setup;
  if (!pl::make_setup(spec, seed, {{"rivals", std::to_string(rivals)}}, setup, error)) {
    return nullptr;
  }
  auto h = std::make_unique<HandRun>();
  h->run = level ? sim::open_run_on(std::move(level), setup, error) : spec.open(setup, error);
  if (!h->run) return nullptr;
  std::vector<std::string> missing;
  h->axes = pl::bind_axes(map, spec.axes, &missing);
  if (!missing.empty()) {
    error = "the bindings have no action for the axis '" + missing.front() +
            "'; add one of that name (games/fall_line/controls/bindings.hpp)";
    return nullptr;
  }
  h->map = std::move(map);
  h->values = in::make_values(h->map);
  h->recorder = pl::InputRecorder(pl::new_log(spec, setup));
  h->step_in.assign(spec.axes.size(), 0);
  h->most_steps = static_cast<int64_t>(ge::core::num::round(spec.most_seconds * spec.hz));
  return h;
}

const sim::GameState& hand_state(const HandRun& h) { return *sim::state_of(*h.run); }

void hand_frame(HandRun& h, const in::InputState& s, double dt_s) {
  in::evaluate(h.map, s, dt_s, h.values);
  pl::axes_frame(h.axes, h.values);
}

bool hand_playing(const HandRun& h) { return !h.run->over() && h.steps < h.most_steps; }

void hand_step(HandRun& h, const in::InputState& s, double now_s) {
  if (!hand_playing(h)) return;
  const std::span<const pl::Axis> axes = sim::kGame.axes;
  // The press is taken off the actions even while the bot skis, so it is not held over.
  pl::axes_step(h.axes, h.values, h.step_in);
  if (h.pilot) h.run->pilot(h.step_in);
  for (size_t i = 0; i < axes.size(); ++i) h.step_in[i] = pl::snap_axis(h.step_in[i], axes[i].kind);
  h.run->step(h.step_in);
  h.recorder.record(h.step_in);
  ++h.steps;
  h.pulses += feel_events(h.felt, s, hand_state(h).events, 0, now_s);
}

bool hand_pressed(const HandRun& h, std::string_view action) {
  const int a = h.map.find_action(action);
  return a >= 0 && h.values[static_cast<size_t>(a)].pressed;
}

pl::InputLog hand_log(const HandRun& h) {
  pl::InputLog log = h.recorder.log();
  log.digest = h.run->digest();
  return log;
}

}  // namespace fall_line::controls
