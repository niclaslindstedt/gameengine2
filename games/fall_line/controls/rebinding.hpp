// THE REBINDING WALK: the player rebinds the keys by pressing them, one action at a time, until
// the menu's keys page (M22d) draws it. F5 (the `rebind` action) starts it; each prompt captures
// the next deliberate input (engine/input/rebind.hpp) and puts it in that action's slot, keeping
// the slot's push and ramp, so a key captured for STEER LEFT pushes left as the A key did. The walk
// says what it wants and what it got as one line each (the window prints them), and names any
// other action the source is also on. ESCAPE stops the walk and keeps what was bound so far. The
// app saves the map to the player's file (bindings.hpp) when the walk ends.
//
//   steer left, steer right, tuck, brake, jump, reset, camera
//
// Each prompt is the action's slot in the `ride` (or `app`) context, in the default file's order:
// the keys come first, so the walk rebinds the keyboard; a pad button pressed instead replaces the
// key with it.
//
// ge:summary The rebinding walk: each action's key captured by pressing, in turn, with conflicts
// ge:keywords fall-line rebind rebinding walk capture prompt keys conflicts player-file f5
// ge:keywords begin_rebinding poll_rebinding
// ge:skill input
// ge:test tests/fall_line_controls_test.cpp
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "input/actions.hpp"
#include "input/rebind.hpp"
#include "input/state.hpp"

namespace fall_line::controls {

struct Prompt {
  std::string_view action;
  std::string_view context;
  size_t slot;  // the action's binding in the context, counted from 0
  std::string_view label;
};

inline constexpr Prompt kPrompts[] = {
    {"steer", "ride", 0, "STEER LEFT"}, {"steer", "ride", 1, "STEER RIGHT"},
    {"tuck", "ride", 0, "TUCK"},        {"brake", "ride", 0, "BRAKE"},
    {"jump", "ride", 0, "JUMP"},        {"reset", "ride", 0, "RESET"},
    {"camera", "app", 0, "CAMERA"},
};
inline constexpr size_t kPromptCount = sizeof(kPrompts) / sizeof(kPrompts[0]);

struct Rebinding {
  bool active = false;
  size_t at = 0;  // the prompt being asked
  ge::input::Capture capture;
};

enum class RebindStep : unsigned char {
  waiting,  // nothing pressed yet
  bound,    // a prompt took its source; the next one is asked
  done,     // the last prompt took its source: save the map
  stopped,  // escape: the walk ended early (save what was bound)
};

// Start the walk on the first prompt; `said` is its line.
void begin_rebinding(Rebinding& r, const ge::input::InputState& s, const ge::input::ActionMap& map,
                     std::string& said);
// Once a frame while active; `said` is the frame's line when something changed.
RebindStep poll_rebinding(Rebinding& r, const ge::input::InputState& s, ge::input::ActionMap& map,
                          std::string& said);

}  // namespace fall_line::controls
