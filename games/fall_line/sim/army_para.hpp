// THE ARMY'S PARAMOTORS: the riflemen the wanted level's roster asks for (`WantedRoster::
// paramotors`, from four stars), flown in on the free ride's own paramotor, holding him in sight
// from a circle over him and firing on him through gunfire. Its numbers are army_para_rules.hpp.
//
// - THE CALL: when the roster asks for more rigs than are out, each is called; after `kRespondS`
//   (the next `kStaggerS` after) it comes in AIRBORNE at the map's edge on the army's bearing
//   (each spread either side), drawn in to no further than `kNearM` from him, already flying
//   (`paramotor::aloft_para`). Rigs past what is asked go home.
// - THE FLIGHT is the free ride's paramotor in every respect (`paramotor::step_para`, the pilot a
//   skier's state hung under the wing, `skier::step_skier` keeping his fall and his landing), flown
//   by the bot's hands (`paramotor::para_pilot`) to an AIM: a point `kLead` ahead round its circle
//   at a height over the snow that puts it `kOrbitUpM` over him, pushed off the other rigs and the
//   army's helicopters. The wing does what the physics lets it: in a strong wind it crabs and
//   drifts, under a ridge it is lifted, a fold is pumped out.
// - THE HUNT: round him where he is seen, round where he was last seen (wider and higher) where he
//   is not.
// - THE RIFLE: a burst whenever he is in sight inside the rifle's reach and the last is
//   `kEveryS` behind, under the hunt's rules of fire (gunfire.hpp).
// - HOME: the hunt over, out to the map's edge on its bearing, climbing, gone in its band.
// - BROUGHT DOWN: a wing flown onto the snow or into a crown is let go (`paramotor::drop_para`);
// the
//   soldier falls, lies `kLieS` and is gone, and the rig is counted lost (a fault).
// - Every rig hunting is written into the wanted level's pursuers (`kPursuerBase` + its id), so
//   its rifleman's eyes keep the stars up.
//
// `step_army` steps the rigs after the helicopters. Nothing here draws a random number or
// allocates in a step, and `ArmyState::paras` stays empty, hashing nothing, on every run whose hunt
// never reached four stars.
//
// ge:summary The army's paramotors: the call, the circle over him, the search, the rifle, home
// ge:keywords army paramotor army-para step_army_paras rifleman circle orbit search call
// ge:keywords aloft para_pilot pursuers army_paras_hash brought-down lost
// ge:skill sim
// ge:test tests/fall_line_army_para_test.cpp
#pragma once

#include <cstdint>

#include "fall_line/sim/army_state.hpp"
#include "fall_line/sim/state.hpp"
#include "fall_line/sim/wanted.hpp"

namespace fall_line::sim {

// THE PARAMOTORS' STEP: the call for the roster's rigs, every rig flown and its rifle fired, and
// the riflemen written into the pursuers. `on` is whether a hunt is running with a star; `q` where
// he is.
void step_army_paras(GameState& state, bool on, Spot q);

// FNV-1a 64 over every rig, folded into `h` (nothing while none was ever called).
uint64_t army_paras_hash(const ArmyState& a, uint64_t h);

const char* army_para_mode_name(ArmyParaMode m);
const char* army_para_change_name(ArmyParaChange c);

}  // namespace fall_line::sim
