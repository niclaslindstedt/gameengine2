// THE HUNT'S SETTINGS ON A RUN: the server's hunt cvars (cfg/cvars.hpp: `sv_lethal`,
// `sv_armyfire`, `sv_helis`, `sv_paras`) handed to a run's wanted level and army once, when the
// run is made and before its first step. Every field it writes is one a lab or a test also stages
// by hand (`WantedState::lethal`, `ArmyState::fire`, `send_helis`, `send_paras`), so a run made
// with the defaults is the run every golden holds.
//
// ge:summary The hunt's server cvars handed to a run: lethal, the army's fire, helicopters, paras
// ge:keywords hunt cvars apply_hunt sv_lethal sv_armyfire sv_helis sv_paras settings army wanted
// ge:skill sim console
// ge:test tests/fall_line_cvars_test.cpp
#pragma once

#include "fall_line/cfg/cvars.hpp"
#include "fall_line/sim/state.hpp"

namespace fall_line::sim {

// Hand `s`'s hunt settings to `state`'s wanted level and army.
void apply_hunt(const cfg::Settings& s, GameState& state);

}  // namespace fall_line::sim
