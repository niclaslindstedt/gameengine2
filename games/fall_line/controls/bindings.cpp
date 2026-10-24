// Fall Line's actions, its default bindings file and the player's file.
//
// ge:summary Fall Line's action defs, the default bindings text, the player's file read and written
// ge:keywords fall-line controls bindings defaults player-file
// ge:skill input
// ge:test tests/fall_line_controls_test.cpp
#include "fall_line/controls/bindings.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "input/bindings_text.hpp"

namespace fall_line::controls {

namespace in = ge::input;

// The keys' ramps (attack, release: the rates, 1/s, a key's push eases toward its target at): the
// steer eases in with a time constant of a sixth of a second and lets go a little faster, so a tap
// is a small correction; the tuck folds in over a quarter second and opens fast; the brake is
// near instant either way.
const char* const kDefaultBindings = R"(ge-bindings 1
context ride
bind steer key:a value=-1 ramp=6,9
bind steer key:d ramp=6,9
bind steer key:left value=-1 ramp=6,9
bind steer key:right ramp=6,9
bind steer pad:left_x deadzone=0.12,0.97
bind steer role:steer
bind tuck key:w ramp=4,12
bind tuck key:up ramp=4,12
bind tuck pad:right_trigger
bind tuck role:throttle
bind brake key:s ramp=20,30
bind brake key:down ramp=20,30
bind brake pad:left_trigger
bind brake role:brake
bind jump key:space
bind jump pad:south
bind reset key:r
bind reset pad:north
context app
bind camera key:c
bind camera pad:back
bind rebind key:f5
)";

in::ActionMap default_map() {
  in::ActionMap map;
  map.defs = {
      {"steer", in::ActionKind::axis, false, "turn: left -1 .. right 1"},
      {"tuck", in::ActionKind::axis, true, "fold into the tuck: less drag"},
      {"brake", in::ActionKind::axis, true, "skid to scrub speed"},
      {"jump", in::ActionKind::button, false, "hold to load the jump, let go to pop"},
      {"reset", in::ActionKind::button, false, "stand back on the piste past the last gate"},
      {"camera", in::ActionKind::button, false, "the next camera"},
      {"rebind", in::ActionKind::button, false, "rebind the keys, one action at a time"},
  };
  std::string error;
  // The defaults are the game's own text: one that does not read is a bug the tests catch.
  in::read_bindings(kDefaultBindings, map, error);
  return map;
}

bool load_player_map(const std::string& path, in::ActionMap& map, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return true;
  std::ostringstream text;
  text << file.rdbuf();
  in::ActionMap read = map;
  if (!in::read_bindings(text.str(), read, error)) {
    error = path + ": " + error + "; fix that line, or delete the file to go back to the defaults";
    return false;
  }
  map = std::move(read);
  return true;
}

bool save_player_map(const std::string& path, const in::ActionMap& map, std::string& error) {
  const std::filesystem::path p(path);
  std::error_code ec;
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "cannot write the bindings to " + path + "; pass --bindings=<a writable path>";
    return false;
  }
  file << in::write_bindings(map);
  return static_cast<bool>(file);
}

}  // namespace fall_line::controls
