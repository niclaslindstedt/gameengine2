// THE RUN PLAYED BY HAND: a person's devices skiing Fall Line through the play contract, the loop
// the `fall_line` window runs and a test drives with a scripted session. The run is the play
// contract's own (sim/play.hpp's `kGame`), so a person's run is stepped, hashed and written down
// exactly as an agent's is, and `ge replay` plays the input log it writes back to its digest.
//
// ONCE A FRAME, `hand_frame` reads the input state through the player's action map (bindings.hpp)
// and latches the presses (play/devices.hpp). FOR EACH STEP the run clock grants, `hand_step` takes
// one value per axis off the actions, snaps it onto its grid, steps the run on it, records it, and
// plays what the step's events give the hands (felt.hpp). With `pilot` set, the game's bot fills
// the axes instead (the window's --bot): the same loop, the same log.
//
// ge:summary The run played by hand: the actions to the play contract's axes, step, log, rumble
// ge:keywords fall-line by-hand person player human hand-run hand_frame hand_step hand_log
// ge:keywords input-log record play-contract devices rumble pilot window loop
// ge:skill input sim
// ge:test tests/fall_line_controls_test.cpp
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "input/actions.hpp"
#include "input/feedback.hpp"
#include "input/state.hpp"
#include "mountain/mapgen/types.hpp"
#include "play/devices.hpp"
#include "play/game.hpp"
#include "play/inputs.hpp"

namespace fall_line::sim {
struct GameState;
}

namespace fall_line::controls {

struct HandRun {
  std::unique_ptr<ge::play::Run> run;
  ge::play::InputRecorder recorder{{}};
  ge::input::ActionMap map;
  ge::input::ActionValues values;
  ge::play::DeviceAxes axes;
  ge::input::Feedback felt;
  std::vector<double> step_in;  // this step's axes, snapped
  int64_t steps = 0;
  int64_t most_steps = 0;  // the spec's longest run
  int pulses = 0;          // rumble pulses started
  bool pilot = false;      // the game's bot fills the axes
};

// A run of Fall Line on `seed` with `rivals` bots beside the player, read through `map`, on the
// seed's race map, or on `level` when one is given (the app's, under a picked sky). Null with
// `error` when the seed builds no map or an axis has no action of its name.
std::unique_ptr<HandRun> open_hand_run(
    double seed, int rivals, ge::input::ActionMap map, std::string& error,
    std::shared_ptr<const ge::mountain::mapgen::Level> level = nullptr);

// The run's game state (for the renderer, the camera and the sound).
const sim::GameState& hand_state(const HandRun& h);

// Once a frame, before its steps: the state through the map over `dt_s`.
void hand_frame(HandRun& h, const ge::input::InputState& s, double dt_s);
// Whether the run has more to play: not over, and short of the longest run.
bool hand_playing(const HandRun& h);
// One fixed step (nothing once the run is no longer playing); `now_s` is the felt's clock.
void hand_step(HandRun& h, const ge::input::InputState& s, double now_s);
// An action's press this frame (the app's camera and rebind keys).
bool hand_pressed(const HandRun& h, std::string_view action);

// The run as an input log, its digest after every step so far.
ge::play::InputLog hand_log(const HandRun& h);

}  // namespace fall_line::controls
