// The rebinding walk: the prompts in turn, each captured and bound.
//
// ge:summary begin_rebinding and poll_rebinding: the prompts captured and bound in turn
// ge:keywords fall-line rebind walk capture
// ge:skill input
// ge:test tests/fall_line_controls_test.cpp
#include "fall_line/controls/rebinding.hpp"

#include "input/bindings_text.hpp"

namespace fall_line::controls {

namespace in = ge::input;

namespace {

void ask(Rebinding& r, const in::InputState& s, const in::ActionMap& map, std::string& said) {
  const Prompt& p = kPrompts[r.at];
  const int a = map.find_action(p.action);
  const in::ActionKind kind =
      a < 0 ? in::ActionKind::button : map.defs[static_cast<size_t>(a)].kind;
  in::begin_capture(r.capture, s, kind);
  said = "rebind " + std::to_string(r.at + 1) + "/" + std::to_string(kPromptCount) +
         ": press the control for " + std::string(p.label) + " (escape: keep the rest)";
}

}  // namespace

void begin_rebinding(Rebinding& r, const in::InputState& s, const in::ActionMap& map,
                     std::string& said) {
  r.active = true;
  r.at = 0;
  ask(r, s, map, said);
}

RebindStep poll_rebinding(Rebinding& r, const in::InputState& s, in::ActionMap& map,
                          std::string& said) {
  if (!r.active) return RebindStep::stopped;
  in::Source source;
  std::string device;
  switch (in::poll_capture(r.capture, s, source, device)) {
    case in::CaptureResult::waiting:
      return RebindStep::waiting;
    case in::CaptureResult::cancelled:
      r.active = false;
      said = "rebind: stopped; the rest keep their controls";
      return RebindStep::stopped;
    case in::CaptureResult::captured:
      break;
  }
  const Prompt& p = kPrompts[r.at];
  std::string also;
  for (const std::string& other : in::conflicts(map, p.context, source)) {
    if (other != p.action) also += (also.empty() ? "" : ", ") + other;
  }
  in::rebind(map, p.context, p.action, p.slot, source, device);
  said = "rebind: " + std::string(p.label) + " is " + in::source_text(source) +
         (also.empty() ? "" : " (also on " + also + ")");
  if (++r.at == kPromptCount) {
    r.active = false;
    return RebindStep::done;
  }
  std::string next;
  ask(r, s, map, next);
  said += "; " + next;
  return RebindStep::bound;
}

}  // namespace fall_line::controls
