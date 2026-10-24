// WHAT IS FELT: the player's run in the hands, as rumble on every pad that has motors. A step's
// events (sim/state.hpp) for the player's racer pick a pulse off the table below; the low motor is
// the heavy one (a body hitting the snow), the high one the buzz (an edge chattering, a pole
// clipped). A pulse never cuts a stronger one short: while one plays, a weaker one is dropped.
//
//   landing   by its load: nothing under 1.5 g, then the low motor up to full at 5.5 g, 0.12 s
//   wipeout   both motors hard, 0.45 s
//   glance    a trunk brushed: mostly the high motor, 0.15 s
//   missed    a gate skied past: a short buzz, 0.1 s
//   shoulder  another racer met (or one of the crowd, `bump`): a knock, 0.2 s
//   hurt      an injury taken, 0.3 s; `died` both motors full, 0.8 s
//   finish    a soft double motor, 0.25 s
//
// The motors are a pure function of the run's events and the clock handed in, so a recorded
// session's feel replays with it.
//
// ge:summary What is felt: rumble by the run's events on every pad with motors
// ge:keywords fall-line rumble haptics vibration felt pulse landing wipeout glance gate finish
// ge:keywords feel_events pulse_for
// ge:skill input
// ge:test tests/fall_line_controls_test.cpp
#pragma once

#include <span>

#include "fall_line/sim/state.hpp"
#include "input/feedback.hpp"
#include "input/state.hpp"

namespace fall_line::controls {

struct Pulse {
  float low = 0, high = 0;  // the motors, 0..1
  double seconds = 0;       // 0: nothing to feel
};

// The pulse one event gives the player's racer `racer` (seconds 0 when it gives none).
Pulse pulse_for(const sim::GameEvent& e, int racer);

// Every pulse of `events` played on every pad of `state` that has rumble, at `now_s`. Returns how
// many pulses started.
int feel_events(ge::input::Feedback& felt, const ge::input::InputState& state,
                std::span<const sim::GameEvent> events, int racer, double now_s);

}  // namespace fall_line::controls
