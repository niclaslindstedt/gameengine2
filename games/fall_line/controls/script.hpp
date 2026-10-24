// A SCRIPTED SESSION: a run's input log held on a simulated pad, frame by frame, as the device
// event log (engine/input/event_log.hpp) a person's hands on that pad would have written. Played
// back into the hand loop (by_hand.hpp, or the window's --events) through `kExactPadBindings`,
// whose pad sources have no dead zone and no curve, every step reads back the log's own axes to
// the bit, so the session skis the logged run again: the bot's run, held on a pad, finishes the
// race and replays to the bot's digest. That is the proof the whole device path (the events, the
// state, the bindings, the latch, the snap, the log) loses and adds nothing.
//
// THE FRAMES' TIMES carry the steps: a scripted frame at `t` is granted every step due by then
// (`steps_due`: the step at t = 0 and one each 1/120 s after, counted exactly in whole
// nanoseconds), so a session steps the same run on every machine and at every frame rate the
// window draws at. The pad's session has one frame a step, frame i at `frame_time(i)`, the first
// nanosecond its step is due. The pad's left stick's x is the steer, the right trigger the
// tuck, the left the brake, south the jump (held), north the reset (a press: down on its step, up
// on the next; two presses in a row are an up and a down in one frame).
//
// ge:summary A scripted session: an input log's axes held on a simulated pad, as a device event log
// ge:keywords fall-line script scripted session event-log pad simulated exact-bindings pad_session
// ge:keywords exact-pad-bindings bot-on-a-pad steps_due frame_time frame-times
// ge:skill input
// ge:test tests/fall_line_controls_test.cpp
#pragma once

#include <cstdint>

#include "input/event_log.hpp"
#include "play/inputs.hpp"

namespace fall_line::controls {

extern const char* const kExactPadBindings;

// The steps due by a scripted frame at `t_ns`: the step at 0 and one each 1/120 s after.
inline int64_t steps_due(uint64_t t_ns) {
  return static_cast<int64_t>(t_ns / 1000000000u * 120u + t_ns % 1000000000u * 120u / 1000000000u) +
         1;
}
// When step `i` is due: the first whole nanosecond at or after i/120 s.
inline uint64_t frame_time(int64_t i) {
  const auto u = static_cast<uint64_t>(i);
  return u / 120u * 1000000000u + (u % 120u * 1000000000u + 119u) / 120u;
}

// `log`'s steps (Fall Line's axes: steer, tuck, brake, jump, reset) as a pad's session.
ge::input::EventLog pad_session(const ge::play::InputLog& log);

}  // namespace fall_line::controls
