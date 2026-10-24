// The hunt's server cvars handed to a run (hunt_cfg.hpp).
//
// ge:summary The hunt's server cvars handed to a run's wanted level and army
// ge:keywords hunt cvars apply_hunt settings
// ge:skill sim console
// ge:test tests/fall_line_cvars_test.cpp
#include "fall_line/sim/hunt_cfg.hpp"

namespace fall_line::sim {

void apply_hunt(const cfg::Settings& s, GameState& state) {
  state.wanted.lethal = s.lethal;
  state.army.fire = s.army_fire;
  state.army.send_helis = s.helis;
  state.army.send_paras = s.paras;
}

}  // namespace fall_line::sim
