// An army helicopter taken off the snow, or rammed out of the sky (army_steal.hpp).
//
// ge:summary steal_army_heli, ram_army_helis, step_downed_heli: taken, rammed, the fall, the wreck
// ge:keywords army helicopter steal ram fall wreck pilot-door downed stolen
// ge:skill sim
// ge:test tests/fall_line_army_steal_test.cpp
#include "fall_line/sim/army_steal.hpp"

#include <cmath>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"
#include "fall_line/sim/army_heli.hpp"
#include "fall_line/sim/army_heli_rules.hpp"
#include "fall_line/sim/army_steal_rules.hpp"
#include "fall_line/sim/gunfire.hpp"
#include "fall_line/sim/heli_ride.hpp"
#include "helicopter/spec.hpp"

namespace fall_line::sim {

namespace {

namespace hc = ge::helicopter;
namespace lm = ge::core::libm;
namespace num = ge::core::num;
using ge::core::hypot;
using ge::core::hypot3;
using namespace army_steal;

constexpr double kG = 9.81;

void say(GameState& state, ArmyHeliChange c, const ArmyHeli& h) {
  state.events.push_back(
      GameEvent{EventKind::army_heli, 0, static_cast<int32_t>(c), static_cast<double>(h.id)});
}

// Its door guns put away (their rounds in flight fly on).
void disarm_doors(GameState& state, const ArmyHeli& h) {
  disarm_gun(state, army_heli::kGunBase + 2 * h.id);
  disarm_gun(state, army_heli::kGunBase + 2 * h.id + 1);
}

// Out of the hunt: its landing over, the men it carried left to the ground.
void out_of_hunt(GameState& state, ArmyHeli& h) {
  h.task.done = true;
  h.tasked = false;
  h.aboard = 0;
  h.doors = 0;
  h.t = 0;
  disarm_doors(state, h);
}

}  // namespace

Spot pilot_door(const ArmyHeli& h) {
  const double fx = lm::sin(h.heading);
  const double fz = lm::cos(h.heading);
  // The right, square to the heading (heading 0 = +z, clockwise from above: right is +x).
  const double rx = fz;
  const double rz = -fx;
  return Spot{h.x + fx * kPilotAheadM + rx * kPilotOutM, h.y,
              h.z + fz * kPilotAheadM + rz * kPilotOutM};
}

bool steal_army_heli(GameState& state, int32_t i) {
  FreeHeli& m = state.heli;
  if (i != 0 || !m.present || m.rider || state.mode != Mode::free) return false;
  if (state.sled.present && (state.sled.rider || state.sled.thrown)) return false;
  const Racer& r = state.racers[static_cast<size_t>(i)];
  const ge::skier::SkierState& c = r.skier;
  if (c.down || r.free.lift.phase != LiftPhase::none || c.speed > kFastestMs) return false;
  for (ArmyHeli& h : state.army.helis) {
    if (h.mode != ArmyHeliMode::set) continue;
    const Spot d = pilot_door(h);
    if (hypot(c.x - d.x, c.z - d.z) > kReachM) continue;
    out_of_hunt(state, h);
    h.mode = ArmyHeliMode::gone;
    state.army.stolen++;
    say(state, ArmyHeliChange::stolen, h);
    // The rotor turning as its crew left it.
    take_heli(state, i, h.x, h.z, h.heading, 1);
    raise_heat(state, WantedCause::stole_army_heli);
    return true;
  }
  return false;
}

void ram_army_helis(GameState& state) {
  FreeHeli& m = state.heli;
  if (!m.present || !m.rider || m.mode == HeliMode::wreck) return;
  const hc::HeliState& p = m.body;
  const double hub = p.y + hc::spec::rotor::kHub;
  const double reach = hc::spec::rotor::kRadius + army_heli::kRotorM;
  for (ArmyHeli& h : state.army.helis) {
    if (h.mode != ArmyHeliMode::hunt && h.mode != ArmyHeliMode::leave && !landing(h)) continue;
    const double flat = hypot(p.x - h.x, p.z - h.z);
    const bool disc = flat < reach && std::fabs(hub - (h.y + kHubM)) < kDiscM;
    const bool hull = hypot3(p.x - h.x, p.y - h.y, p.z - h.z) < kHullM;
    if (!disc && !hull) continue;
    out_of_hunt(state, h);
    h.mode = ArmyHeliMode::fall;
    h.vx = kKeepShare * h.vx + (1 - kKeepShare) * p.vx;
    h.vz = kKeepShare * h.vz + (1 - kKeepShare) * p.vz;
    state.army.downed++;
    say(state, ArmyHeliChange::rammed, h);
    crash_heli(state, 0);
    raise_heat(state, WantedCause::downed_helicopter);
    return;
  }
}

void step_downed_heli(GameState& state, ArmyHeli& h) {
  if (h.mode == ArmyHeliMode::wreck) {
    if (h.t >= kWreckS) {
      h.mode = ArmyHeliMode::gone;
      say(state, ArmyHeliChange::gone, h);
    }
    return;
  }
  // FALLING: spinning, its tail rotor gone, until the snow stops it.
  const double ease = num::min(1.0, kDt);
  h.vy -= kG * kFallShare * kDt;
  h.vx *= 1 - kDragPerS * kDt;
  h.vz *= 1 - kDragPerS * kDt;
  h.yaw_rate += (kSpin - h.yaw_rate) * ease;
  h.heading = num::mod(h.heading + h.yaw_rate * kDt, ge::core::kTau);
  h.roll += (kRoll - h.roll) * ease * 0.8;
  h.pitch += (kPitch - h.pitch) * ease * 0.8;
  h.x += h.vx * kDt;
  h.y += h.vy * kDt;
  h.z += h.vz * kDt;
  const double ground = state.level->ground_at(h.x, h.z);
  if (h.y > ground) return;
  h.y = ground;
  h.vx = h.vy = h.vz = 0;
  h.yaw_rate = 0;
  h.roll = kWreckRoll;
  h.pitch = kWreckPitch;
  h.mode = ArmyHeliMode::wreck;
  h.t = 0;
  say(state, ArmyHeliChange::down, h);
}

}  // namespace fall_line::sim
