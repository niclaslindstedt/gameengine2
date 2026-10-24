// The army's paramotors (army_para.hpp): the call, the free ride's rig flown by the bot's hands to
// a point round a circle over him, the rifle, home, and the rig brought down.
//
// ge:summary The army's paramotors' step: called in aloft, circling him, the rifle, home
// ge:keywords army paramotor army-para step_army_paras circle search rifle aloft para_pilot
// ge:keywords army_paras_hash pursuers lost
// ge:skill sim
// ge:test tests/fall_line_army_para_test.cpp
#include "fall_line/sim/army_para.hpp"

#include <cmath>
#include <cstring>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"
#include "fall_line/sim/army_heli_rules.hpp"
#include "fall_line/sim/army_para_rules.hpp"
#include "fall_line/sim/gunfire.hpp"
#include "fall_line/sim/weapons_rules.hpp"
#include "paramotor/pilot.hpp"
#include "skier/skis.hpp"

namespace fall_line::sim {

namespace lm = ge::core::libm;
namespace num = ge::core::num;
namespace pm = ge::paramotor;
namespace sk = ge::skier;
using ge::core::clamp;
using ge::core::hypot;
using namespace army_para;

namespace {

// The soldier in the harness, with his kit: the reference pair's man.
constexpr const sk::SkiSpec& kSoldier = sk::kReferenceSkis;

void say(GameState& state, ArmyParaChange c, const ArmyPara& p) {
  state.events.push_back(
      GameEvent{EventKind::army_para, 0, static_cast<int32_t>(c), static_cast<double>(p.id)});
}

int32_t gun_of(const ArmyPara& p) { return kGunBase + p.id; }

pm::ParaWorld para_world(const GameState& state) {
  return pm::ParaWorld{state.level.get(), &state.trees, &state.army.para_field,
                       pm::level_wind(*state.level), state.t};
}

void turn_home(GameState& state, ArmyPara& p) {
  if (p.mode != ArmyParaMode::hunt) return;
  p.mode = ArmyParaMode::leave;
  p.t = 0;
  disarm_gun(state, gun_of(p));
  say(state, ArmyParaChange::leaving, p);
}

// CALL IN what the roster asks for; send home what it no longer does.
void call(GameState& state, int32_t want, Spot q) {
  ArmyState& a = state.army;
  int32_t live = 0;
  int32_t waiting = 0;
  for (ArmyPara& p : a.paras) {
    if (p.mode != ArmyParaMode::due && p.mode != ArmyParaMode::hunt) continue;
    if (live >= want) {
      if (p.mode == ArmyParaMode::due) {
        p.mode = ArmyParaMode::gone;
      } else {
        turn_home(state, p);
      }
      continue;
    }
    live++;
    waiting += p.mode == ArmyParaMode::due ? 1 : 0;
  }
  const double half = state.level->size / 2;
  // The army's bearing: the helicopters' where they were called, else toward him now.
  const double from = a.helis_called > 0 ? a.from : lm::atan2(q.x - half, q.z - half);
  while (live < want && a.paras.size() < kMostParas) {
    if (!a.para_air) {
      a.para_field = pm::make_air_field(state.level->seed);
      a.para_air = true;
    }
    const int32_t k = a.paras_called;
    // 1, -1, 2, -2 ... spreads either side of the army's bearing, off the helicopters' own line.
    const int32_t step = (k % 2 == 0 ? 1 : -1) * (k / 2 + 1);
    ArmyPara p;
    p.id = a.next_para++;
    p.due = state.t + kRespondS + kStaggerS * waiting;
    p.bearing = from + step * kSpread;
    p.dir = p.id % 2 == 0 ? 1 : -1;
    a.paras.push_back(p);
    a.paras_called++;
    waiting++;
    live++;
    say(state, ArmyParaChange::called, a.paras.back());
  }
}

// ON THE MAP, AIRBORNE: at the edge on its bearing, drawn in to `kNearM` from him, over the snow
// and over him, flying at him.
void arrive(GameState& state, ArmyPara& p, Spot q) {
  const auto& level = *state.level;
  const double size = level.size;
  const double sx = lm::sin(p.bearing);
  const double sz = lm::cos(p.bearing);
  const double r = (size / 2 - kPastM) / num::max(num::max(std::fabs(sx), std::fabs(sz)), 1e-6);
  double x = size / 2 + sx * r;
  double z = size / 2 + sz * r;
  const double far = hypot(x - q.x, z - q.z);
  if (far > kNearM) {
    x = q.x + (x - q.x) / far * kNearM;
    z = q.z + (z - q.z) / far * kNearM;
  }
  const double h = lm::atan2(q.x - x, q.z - z);
  p.pilot = sk::stand_skier(state.world(), x, z, h, 0);
  p.pilot.y = num::max(level.ground_at(x, z) + kEnterM, q.y + kOrbitUpM);
  p.wing = pm::aloft_para(p.pilot, h, kTrimMs, pm::pilot::kCruise);
  p.mode = ArmyParaMode::hunt;
  p.t = 0;
  say(state, ArmyParaChange::arrived, p);
}

// ONE STEP OF THE RIG on `in`: the soldier hung under it (his skis given nothing), then the wing.
// A wing brought down onto the snow or into a crown is the rig lost.
void fly(GameState& state, ArmyPara& p, const pm::ParaControls& in) {
  sk::SkierEvents sev;
  sk::step_skier(p.pilot, sk::SkierInput{}, state.world(), kSoldier, kDt, sev);
  pm::ParaEvents pev;
  pm::step_para(p.wing, p.pilot, in, para_world(state), kSoldier, kDt, pev);
  if (pev.cut != pm::ParaCut::none && p.mode != ArmyParaMode::down) {
    p.mode = ArmyParaMode::down;
    p.t = 0;
    disarm_gun(state, gun_of(p));
    state.army.paras_lost++;
    say(state, ArmyParaChange::down, p);
  }
}

// THE AIM ROUND THE CIRCLE at (cx, cz), radius `R`: a point `kLead` ahead of the rig round it, or
// its middle from further out; pushed off the other rigs and the army's helicopters.
pm::ParaAim aim_round(const GameState& state, ArmyPara& p, double cx, double cy, double cz,
                      double R, double up) {
  const sk::SkierState& s = p.pilot;
  const double d = hypot(s.x - cx, s.z - cz);
  double ax = cx;
  double az = cz;
  if (d < kFarShare * R) {
    const double a = lm::atan2(s.x - cx, s.z - cz) + p.dir * kLead;
    ax = cx + lm::sin(a) * R;
    az = cz + lm::cos(a) * R;
  }
  const auto push = [&](double ox, double oy, double oz, double keep) {
    const double dx = s.x - ox;
    const double dz = s.z - oz;
    const double dd = hypot(dx, dz);
    if (dd >= keep || dd < 0.1 || std::fabs(s.y - oy) > kLevelM) return;
    const double k = (1 - dd / keep) * keep;
    ax += dx / dd * k;
    az += dz / dd * k;
  };
  for (const ArmyPara& o : state.army.paras) {
    if (&o == &p || o.mode != ArmyParaMode::hunt) continue;
    push(o.pilot.x, o.pilot.y, o.pilot.z, kApartM);
  }
  for (const ArmyHeli& h : state.army.helis) {
    if (h.mode == ArmyHeliMode::due || h.mode == ArmyHeliMode::gone) continue;
    push(h.x, h.y, h.z, kHeliKeepM);
  }
  const double ground = state.level->ground_at(s.x, s.z);
  // LOW, in sinking air or over snow rising faster than it climbs: off the circle and down the
  // fall line, where the snow drops away under it, until it has its height back.
  if (p.wing.agl < kLowM) p.low = true;
  if (p.wing.agl > kFloorM) p.low = false;
  if (p.low) return pm::ParaAim{false, 0, 0, kHoldM, false};
  return pm::ParaAim{true, ax, az, num::max(kHoldM, cy + up - ground), false};
}

// THE RIFLE: a burst at him whenever he is in sight inside its reach and the last is far enough
// behind.
void fire_rifle(GameState& state, ArmyPara& p, Spot q) {
  const WantedState& w = state.wanted;
  if (!state.army.fire || w.ended != WantedEnd::running || !w.seen) return;
  const sk::SkierState& s = p.pilot;
  const double range = hypot(hypot(q.x - s.x, q.z - s.z), q.y - s.y);
  if (range > weapons::gun_def(WeaponKind::rifle).range || state.t < p.next) return;
  const Pursuer eye{kPursuerBase + p.id, PursuerKind::paramotor, s.x, s.y + kEyeM, s.z};
  if (!sees_quarry(*state.level, state.trees, eye, q, under_cover(state))) return;
  const double mx = s.x;
  const double my = s.y + kMuzzleUpM;
  const double mz = s.z;
  Gun* g = arm_gun(state, gun_of(p), WeaponKind::rifle, mx, my, mz);
  if (g == nullptr) return;
  g->x = mx;
  g->y = my;
  g->z = mz;
  g->vx = s.vx;
  g->vy = s.vy;
  g->vz = s.vz;
  if (!gun_ready(*g)) return;
  if (fire_at(state, *g, AimTarget{}, roster_of(w.stars).fire)) {
    // Each man's rhythm his own, off his id.
    p.next = state.t + kEveryS * (1 + kEverySpread * num::mod(p.id * 0.618, 1.0));
  }
}

// WRITE THE RIFLEMEN into the wanted level's pursuers: every rig hunting.
void write_pursuers(GameState& state) {
  std::vector<Pursuer>& ps = state.wanted.pursuers;
  std::erase_if(ps, [](const Pursuer& p) {
    return p.id >= kPursuerBase && p.id < kPursuerBase + static_cast<int32_t>(kMostParas);
  });
  for (const ArmyPara& p : state.army.paras) {
    if (ps.size() >= ps.capacity()) break;
    if (p.mode != ArmyParaMode::hunt) continue;
    ps.push_back(Pursuer{kPursuerBase + p.id, PursuerKind::paramotor, p.pilot.x, p.pilot.y + kEyeM,
                         p.pilot.z});
  }
}

}  // namespace

void step_army_paras(GameState& state, bool on, Spot q) {
  ArmyState& a = state.army;
  const int32_t want = on && a.send_paras ? roster_of(state.wanted.stars).paramotors : 0;
  if (want == 0 && a.paras.empty()) return;
  call(state, want, q);
  const double size = state.level->size;
  const WantedState& w = state.wanted;
  for (ArmyPara& p : a.paras) {
    p.t += kDt;
    switch (p.mode) {
      case ArmyParaMode::due:
        if (state.t >= p.due) arrive(state, p, q);
        break;
      case ArmyParaMode::hunt: {
        const Spot c = w.seen ? q : Spot{w.last.x, w.last.y, w.last.z};
        const double R = kOrbitM * (w.seen ? 1 : kSearchWider);
        const double cx = clamp(c.x, R, size - R);
        const double cz = clamp(c.z, R, size - R);
        const pm::ParaAim aim =
            aim_round(state, p, cx, c.y, cz, R, w.seen ? kOrbitUpM : kSearchUpM);
        pm::ParaControls in = pm::para_pilot(p.wing, p.pilot, para_world(state), aim);
        // Under the floor the turns are gentle: a hard turn sheds the height it is short of.
        if (p.wing.agl < kFloorM) in.steer = clamp(in.steer, -kLowSteer, kLowSteer);
        fly(state, p, in);
        if (p.mode == ArmyParaMode::hunt) fire_rifle(state, p, q);
        break;
      }
      case ArmyParaMode::leave: {
        const double half = size / 2;
        const pm::ParaAim aim{true, half + lm::sin(p.bearing) * size,
                              half + lm::cos(p.bearing) * size, kHomeUpM, false};
        fly(state, p, pm::para_pilot(p.wing, p.pilot, para_world(state), aim));
        const sk::SkierState& s = p.pilot;
        if (s.x < kGoneM || s.z < kGoneM || s.x > size - kGoneM || s.z > size - kGoneM) {
          p.mode = ArmyParaMode::gone;
          say(state, ArmyParaChange::gone, p);
        }
        break;
      }
      case ArmyParaMode::down:
        // The rig let go: the soldier falls and lies, the pieces fall where they fall.
        fly(state, p, pm::ParaControls{});
        if (p.pilot.on_snow && p.t > kLieS) p.mode = ArmyParaMode::gone;
        break;
      case ArmyParaMode::gone:
        break;
    }
  }
  for (const ArmyPara& p : a.paras) {
    if (p.mode == ArmyParaMode::gone) disarm_gun(state, gun_of(p));
  }
  std::erase_if(a.paras, [](const ArmyPara& p) { return p.mode == ArmyParaMode::gone; });
  write_pursuers(state);
}

uint64_t army_paras_hash(const ArmyState& a, uint64_t h) {
  if (a.paras_called == 0) return h;
  const auto mix = [&h](double v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof u);
    for (int b = 0; b < 64; b += 8) {
      h ^= (u >> b) & 0xffu;
      h *= 0x100000001b3ull;
    }
  };
  mix(static_cast<double>(a.paras.size()));
  for (const ArmyPara& p : a.paras) {
    mix(p.id);
    mix(static_cast<double>(p.mode));
    mix(p.due);
    mix(p.t);
    mix(p.bearing);
    mix(p.next);
    mix(p.low ? 1 : 0);
    h = sk::skier_hash(p.pilot, h);
    h = pm::para_hash(p.wing, h);
  }
  mix(a.next_para);
  mix(a.paras_called);
  mix(a.paras_lost);
  return h;
}

const char* army_para_mode_name(ArmyParaMode m) {
  switch (m) {
    case ArmyParaMode::due:
      return "due";
    case ArmyParaMode::hunt:
      return "hunt";
    case ArmyParaMode::leave:
      return "leave";
    case ArmyParaMode::down:
      return "down";
    case ArmyParaMode::gone:
      return "gone";
  }
  return "?";
}

const char* army_para_change_name(ArmyParaChange c) {
  switch (c) {
    case ArmyParaChange::called:
      return "called";
    case ArmyParaChange::arrived:
      return "arrived";
    case ArmyParaChange::leaving:
      return "leaving";
    case ArmyParaChange::gone:
      return "gone";
    case ArmyParaChange::down:
      return "down";
  }
  return "?";
}

}  // namespace fall_line::sim
