// A scripted session: the log's axes held on a simulated pad.
//
// ge:summary pad_session: an input log's axes as a simulated pad's event log, a frame a step
// ge:keywords fall-line script pad session
// ge:skill input
// ge:test tests/fall_line_controls_test.cpp
#include "fall_line/controls/script.hpp"

#include "input/sim.hpp"

namespace fall_line::controls {

namespace in = ge::input;

const char* const kExactPadBindings = R"(ge-bindings 1
context ride
bind steer pad:left_x deadzone=0,1
bind tuck pad:right_trigger deadzone=0,1
bind brake pad:left_trigger deadzone=0,1
bind jump pad:south
bind reset pad:north
)";

in::EventLog pad_session(const ge::play::InputLog& log) {
  in::SimInput sim;
  in::InputState state;
  in::EventLog out;
  const in::DeviceId pad = in::sim_plug(sim, in::sim_pad());
  bool jump = false, reset = false;
  float last[3] = {0, 0, 0};
  const in::PadAxis axes[3] = {in::PadAxis::left_x, in::PadAxis::right_trigger,
                               in::PadAxis::left_trigger};
  for (int64_t i = 0; i < log.steps(); ++i) {
    if (i > 0) in::sim_advance(sim, static_cast<double>(frame_time(i) - sim.t_ns) * 1e-9);
    const auto a = log.at(i);
    for (int k = 0; k < 3; ++k) {
      const auto v = static_cast<float>(a[static_cast<size_t>(k)]);
      if (v != last[k]) in::sim_axis(sim, pad, static_cast<int>(axes[k]), v);
      last[k] = v;
    }
    const bool j = a[3] != 0;
    if (j != jump) in::sim_button(sim, pad, static_cast<int>(in::PadButton::south), j);
    jump = j;
    const bool r = a[4] != 0;
    if (r && reset) in::sim_button(sim, pad, static_cast<int>(in::PadButton::north), false);
    if (r || reset) in::sim_button(sim, pad, static_cast<int>(in::PadButton::north), r);
    reset = r;
    in::Queue kept;
    in::sim_frame(sim, state, &kept);
    out.add(frame_time(i), kept);
  }
  return out;
}

}  // namespace fall_line::controls
