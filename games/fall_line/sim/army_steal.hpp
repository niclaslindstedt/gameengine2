// THE ARMY'S HELICOPTERS TAKEN OFF THE SNOW OR BROUGHT DOWN (its numbers army_steal_rules.hpp).
//
// - TAKEN (`steal_army_heli`): the machine press, the skier on his skis within reach of a set-down
//   machine's pilot's door and slow, takes it. The machine leaves the hunt (gone, its landing
//   over, its men left to the ground, its door guns put away) and the free ride's helicopter
//   becomes it where it stands, its rotor turning, with him on it (`heli_ride::take_heli`). A
//   taken machine has no pilot of its own: dropped off in the air it falls, and once it is wrecked
//   or he is set back on the pad, the free ride's own machine waits on its pad again. Flying off
//   in it is an offence (`WantedCause::stole_army_heli`).
// - BROUGHT DOWN (`ram_army_helis`, each step after the machines fly): the free ride's helicopter,
//   with him on it, flown into one of theirs (the two discs meeting at a height, or the two
//   airframes) brings theirs down and crashes his. Theirs FALLS spinning, its tail rotor gone,
//   until the snow stops it, and lies there as a WRECK, then is gone. Bringing one down is an
//   offence (`WantedCause::downed_helicopter`).
//
// Both are counted on the army (`ArmyState::stolen`, `downed`). Nothing here draws a random
// number or allocates.
//
// ge:summary An army helicopter taken off the snow, or rammed out of the sky: the fall, the wreck
// ge:keywords army helicopter steal stolen steal_army_heli ram rammed ram_army_helis downed fall
// ge:keywords wreck pilot-door offence stole_army_heli downed_helicopter
// ge:skill sim
// ge:test tests/fall_line_army_steal_test.cpp
#pragma once

#include <cstdint>

#include "fall_line/sim/army_state.hpp"
#include "fall_line/sim/state.hpp"
#include "fall_line/sim/wanted.hpp"

namespace fall_line::sim {

// Where a machine's pilot's door is, world frame (its height the skid datum's).
Spot pilot_door(const ArmyHeli& h);

// THE MACHINE PRESS AT A SET-DOWN MACHINE: racer `i` takes the one whose pilot's door he is
// within reach of. False when there is none to take (and the press is his to spend elsewhere).
bool steal_army_heli(GameState& state, int32_t i);

// THE RAM: the free ride's helicopter, flown, into one of the army's.
void ram_army_helis(GameState& state);

// A machine brought down: falling, then a wreck, then gone. One step of it.
void step_downed_heli(GameState& state, ArmyHeli& h);

}  // namespace fall_line::sim
