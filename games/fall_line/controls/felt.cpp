// What is felt: the pulse table and its play on the pads.
//
// ge:summary pulse_for and feel_events: the run's events as rumble pulses on the pads
// ge:keywords fall-line rumble felt pulse
// ge:skill input
// ge:test tests/fall_line_controls_test.cpp
#include "fall_line/controls/felt.hpp"

namespace fall_line::controls {

namespace in = ge::input;

Pulse pulse_for(const sim::GameEvent& e, int racer) {
  using K = sim::EventKind;
  const bool mine = e.racer == racer || (e.kind == K::shoulder && e.gate == racer);
  if (!mine) return {};
  switch (e.kind) {
    case K::landing: {
      const double g = e.value;
      if (!(g > 1.5)) return {};
      const float s = static_cast<float>(g >= 5.5 ? 1.0 : (g - 1.5) / 4.0);
      return {s, 0.3f * s, 0.12};
    }
    case K::wipeout:
      return {1.0f, 0.6f, 0.45};
    case K::glance:
      return {0.35f, 0.8f, 0.15};
    case K::missed:
      return {0.0f, 0.35f, 0.1};
    case K::shoulder:
    case K::bump:
      return {0.6f, 0.4f, 0.2};
    case K::hurt:
      return {0.8f, 0.5f, 0.3};
    case K::died:
      return {1.0f, 1.0f, 0.8};
    case K::finish:
      return {0.3f, 0.3f, 0.25};
    default:
      return {};
  }
}

int feel_events(in::Feedback& felt, const in::InputState& state,
                std::span<const sim::GameEvent> events, int racer, double now_s) {
  int started = 0;
  for (const sim::GameEvent& e : events) {
    const Pulse p = pulse_for(e, racer);
    if (p.seconds <= 0) continue;
    bool any = false;
    for (const in::Slot& slot : state.slots) {
      if (!slot.active || !in::has(slot.info, in::kRumble)) continue;
      float low = 0, high = 0;
      if (const in::DeviceFeedback* d = felt.find(slot.info.id))
        in::rumble_at(*d, now_s, low, high);
      if (low + high > p.low + p.high) continue;  // a stronger pulse is still playing
      in::rumble(felt, slot.info.id, p.low, p.high, p.seconds, now_s);
      any = true;
    }
    if (any) ++started;
  }
  return started;
}

}  // namespace fall_line::controls
