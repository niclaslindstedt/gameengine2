// The free ride's helicopter (heli_ride.hpp).
//
// ge:summary heli_pad, place_heli, step_heli_ride: board, fly, drop, home, crash, restart
// ge:keywords heli helicopter pad board drop home crash wreck restart hold seat parked take_heli
// ge:keywords crash_heli army
// ge:skill sim helicopter
// ge:test tests/fall_line_heli_test.cpp
#include "fall_line/sim/heli_ride.hpp"

#include <cmath>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"
#include "core/quat.hpp"
#include "fall_line/sim/army_steal.hpp"
#include "fall_line/sim/sled_ride.hpp"
#include "helicopter/pilot.hpp"
#include "helicopter/scenarios.hpp"
#include "skier/skier.hpp"

namespace fall_line::sim {

namespace {

namespace mg = ge::mountain::mapgen;
namespace ml = ge::mountain::lifts;
namespace sk = ge::skier;
namespace hc = ge::helicopter;
namespace core = ge::core;
namespace num = ge::core::num;
namespace lm = ge::core::libm;
using namespace heli_ride;

// THE HEADING FROM (x, z) TO THE SUMMIT: a skier climbing on looks up at the mountain he is about
// to fly. A map with no summit published is faced up its fall line (+z is down it).
double summitward(const mg::Level& level, double x, double z) {
  const mg::Vec3& top = level.mountain.summit;
  if (core::hypot(top.x - x, top.z - z) < 1) return core::kPi;
  return lm::atan2(top.x - x, top.z - z);
}

hc::HeliLoad load_of(const GameState& state) {
  return state.heli.rider ? hc::HeliLoad{state.spec.mass, hc::kSeatX} : hc::HeliLoad{};
}

void say(GameState& state, int32_t i, HeliPhase phase, double speed) {
  state.events.push_back(GameEvent{EventKind::heli, i, static_cast<int32_t>(phase), speed});
}

// A machine on its pad, its rotor still, nobody on it.
void fresh(GameState& state) {
  FreeHeli& m = state.heli;
  m.body = hc::stand_heli(*state.level, m.pad_x, m.pad_z, m.pad_heading);
  m.mode = HeliMode::parked;
  m.rider = false;
  m.aboard = false;
  m.t = 0;
  m.flown = {};
  m.army = false;
}

// THE SKIER SAT ON THE SKID: where the seat has him, facing out over it, moving with it. His skis
// are off the snow: nothing of theirs is edged, tucked or sunk.
void hold(GameState& state, int32_t i) {
  sk::SkierState& c = state.racers[static_cast<size_t>(i)].skier;
  const hc::HeliState& b = state.heli.body;
  const core::Vec3 at = heli_seat(b);
  c.x = at.x;
  c.y = at.y;
  c.z = at.z;
  c.vx = b.vx;
  c.vy = b.vy;
  c.vz = b.vz;
  c.heading = num::mod(b.heading + core::kPi / 2, core::kTau);
  c.yaw_rate = b.yaw_rate;
  c.speed = core::hypot3(b.vx, b.vy, b.vz);
  c.on_snow = false;
  c.air_time = 0;
  c.air_peak = 0;
  c.edge = 0;
  c.tuck = 0;
  c.lean = 0;
  c.sink = 0;
  c.g_lat = 0;
  c.jump_held = 0;
  c.down = false;
  c.down_time = 0;
  c.cause = sk::Wipeout::none;
  c.thrown.active = false;
}

// Sat on the skid: the machine his, the rotor spooling up.
void seat(GameState& state, int32_t i) {
  FreeHeli& m = state.heli;
  Racer& r = state.racers[static_cast<size_t>(i)];
  m.rider = true;
  m.mode = HeliMode::flown;
  m.t = 0;
  r.free.lift.phase = LiftPhase::none;
  r.free.lift.carrier = -1;
  r.progress.stuck = 0;
  hold(state, i);
}

// Off the skid at the seat with the machine's way and a push `out` over the skid and `up`, facing
// the way it was flying (or out over the skid, at the hover). A new descent: his foot is to be
// reached again.
void release(GameState& state, int32_t i, double out, double up) {
  FreeHeli& m = state.heli;
  const hc::HeliState& b = m.body;
  Racer& r = state.racers[static_cast<size_t>(i)];
  const core::Vec3 s = heli_seat(b);
  const core::Vec3 side = core::rotate(hc::heli_quat(b), core::Vec3{1, 0, 0});
  const double flat = core::hypot(b.vx, b.vz);
  const double heading =
      flat > 4 ? lm::atan2(b.vx, b.vz) : num::mod(b.heading + core::kPi / 2, core::kTau);
  const double over = num::max(0.0, s.y - state.level->ground_at(s.x, s.z));
  r.skier = sk::stand_skier(state.world(), s.x, s.z, heading, 0, b.grounded ? 0.0 : over);
  r.skier.vx = b.vx + side.x * out;
  r.skier.vy = b.vy + up;
  r.skier.vz = b.vz + side.z * out;
  r.skier.speed = core::hypot3(r.skier.vx, r.skier.vy, r.skier.vz);
  r.free.arrived = false;
  r.progress.stuck = 0;
  m.rider = false;
}

void board(GameState& state, int32_t i) {
  state.heli.taken++;
  seat(state, i);
  say(state, i, HeliPhase::board, 0);
}

// THE DROP: pushed off the skid (landed, stepped off onto the snow). In the air the pilot flies
// it home; landed, he shuts down where it stands.
void drop(GameState& state, int32_t i) {
  FreeHeli& m = state.heli;
  const hc::HeliState& b = m.body;
  const double speed = core::hypot3(b.vx, b.vy, b.vz);
  release(state, i, b.grounded ? 0.0 : kDropOut, b.grounded ? 0.0 : kDropUp);
  m.mode = b.grounded ? HeliMode::parked : HeliMode::home;
  m.t = 0;
  say(state, i, HeliPhase::drop, speed);
}

// THE CRASH: the machine down where it struck, and the skier on its skid thrown off by the blast.
void wreck(GameState& state, int32_t i, const hc::HeliEvents& ev) {
  FreeHeli& m = state.heli;
  m.mode = HeliMode::wreck;
  m.t = 0;
  m.aboard = m.rider;
  say(state, i, HeliPhase::crash, ev.speed);
  if (!m.rider) return;
  Racer& r = state.racers[static_cast<size_t>(i)];
  release(state, i, kBlastOut, kBlastUp);
  sk::put_down(r.skier, sk::Wipeout::landing);
  r.progress.wipeouts++;
  state.events.push_back(
      GameEvent{EventKind::wipeout, i, static_cast<int32_t>(r.skier.cause), r.skier.speed});
}

// THE RIDE STARTED AGAIN FROM THE BOTTOM: a fresh machine on the pad, its rotor at speed, the
// skier sat on its skid.
void start_again(GameState& state, int32_t i) {
  fresh(state);
  state.heli.body.spool = 1;
  sk::SkierState& c = state.racers[static_cast<size_t>(i)].skier;
  const core::Vec3 s = heli_seat(state.heli.body);
  c = sk::stand_skier(state.world(), s.x, s.z, state.heli.body.heading, 0);
  seat(state, i);
  say(state, i, HeliPhase::restart, 0);
}

// THE CONTROLS THIS STEP: his, flying it; the pilot's flying it home after a beat with the
// controls where he left them (the lurch as his weight leaves the skid); otherwise let go.
hc::HeliControls controls_for(const GameState& state, const Controls& c) {
  const FreeHeli& m = state.heli;
  const hc::HeliWorld world = heli_world(state);
  if (m.mode == HeliMode::flown && m.rider) {
    return hc::heli_turned_back(m.body, world, load_of(state), heli_controls(c));
  }
  if (m.mode == HeliMode::home) {
    // A machine taken from the army has no pilot of its own to fly it home: let go.
    if (m.army) return {};
    if (m.t < kHomeBeat) return m.flown;
    hc::HeliAim aim;
    aim.x = m.pad_x;
    aim.z = m.pad_z;
    aim.height = kHomeClear;
    aim.land = true;
    aim.has_face = true;
    aim.face = m.pad_heading;
    return hc::heli_pilot(m.body, world, hc::HeliLoad{}, aim);
  }
  return {};
}

// One step of the machine's own flight, and what it did.
void fly(GameState& state, int32_t i, const Controls& c) {
  FreeHeli& m = state.heli;
  const hc::HeliControls in = controls_for(state, c);
  const double spool_to = m.rider || m.mode == HeliMode::home ? 1 : 0;
  hc::HeliEvents ev;
  hc::step_heli(m.body, in, load_of(state), spool_to, heli_world(state), kDt, ev);
  m.flown = in;
  if (ev.crash != hc::HeliCrash::none) {
    wreck(state, i, ev);
    return;
  }
  if (ev.landed && m.mode == HeliMode::home &&
      core::hypot(m.body.x - m.pad_x, m.body.z - m.pad_z) < kHomeNear) {
    m.mode = HeliMode::parked;
    m.t = 0;
    say(state, i, HeliPhase::home, 0);
  }
}

}  // namespace

double pad_lean(const mg::Level& level, double x, double z, double r) {
  const double g = level.ground_at(x, z);
  double most = 0;
  for (int i = 0; i < 8; i++) {
    const double a = static_cast<double>(i) / 8 * 2 * core::kPi;
    most =
        num::max(most, std::fabs(level.ground_at(x + lm::sin(a) * r, z + lm::cos(a) * r) - g) / r);
  }
  return most;
}

bool pad_clear(const mg::Level& level, const std::vector<ml::LiftPlan>& lifts,
               const sk::TreeIndex& trees, double x, double z, double room) {
  bool tree = false;
  sk::for_each_tree_near(trees, x, z, [&](int32_t i) {
    const mg::TreeDef& t = level.trees[static_cast<size_t>(i)];
    if (core::hypot(t.x - x, t.z - z) < room + t.radius) tree = true;
  });
  if (tree) return false;
  for (const ml::LiftPlan& p : lifts) {
    const double rx = x - p.bottom.x;
    const double rz = z - p.bottom.z;
    if (core::hypot(rx, rz) < kStationGap + room) return false;
    const double u = rx * p.dx + rz * p.dz;
    const double v = rx * p.dz - rz * p.dx;
    if (u > -room && u < p.length + room && std::fabs(v) < kLineGap + room) return false;
  }
  if (level.resort) {
    for (const mg::WindTunnel& t : level.resort->tunnels) {
      for (const mg::WindTunnel::Point& q : t.points) {
        if (core::hypot(q.x - x, q.z - z) < kTunnelGap + room + t.width / 2) return false;
      }
    }
  }
  return true;
}

HeliPad heli_pad(const mg::Level& level, const std::vector<ml::LiftPlan>& lifts,
                 const sk::TreeIndex& trees, double avoid_x, double avoid_z, double avoid_r) {
  const mg::TrackPoint& finish = level.track.points.back();
  const double cx = level.resort ? level.resort->village.x : finish.x;
  const double cz = level.resort ? level.resort->village.z : finish.z;
  const double half = level.size / 2;
  const double edge = kPadRadius + 30;
  bool found = false;
  double bx = cx, bz = cz, best = 0;
  const auto n = static_cast<int>(kSearch / kStep);
  for (int iz = -n; iz <= n; iz++) {
    for (int ix = -n; ix <= n; ix++) {
      const double dx = ix * kStep;
      const double dz = iz * kStep;
      const double x = cx + dx;
      const double z = cz + dz;
      if (std::fabs(x - half) > half - edge || std::fabs(z - half) > half - edge) continue;
      const double d = core::hypot(dx, dz);
      if (found && d >= best) continue;
      const double lean = pad_lean(level, x, z, kPadRadius);
      if (lean > kPadSlope) continue;
      if (core::hypot(x - avoid_x, z - avoid_z) < avoid_r) continue;
      const double cost = d + lean * 600;
      if (found && cost >= best) continue;
      if (!pad_clear(level, lifts, trees, x, z, kPadRadius)) continue;
      found = true;
      bx = x;
      bz = z;
      best = cost;
    }
  }
  return HeliPad{bx, bz, summitward(level, bx, bz)};
}

void place_heli(GameState& state) {
  FreeHeli& m = state.heli;
  const FreeSled& sled = state.sled;
  const double avoid = sled.present ? kPadRadius + sled_ride::kPadRadius + 4 : 0;
  const HeliPad pad =
      heli_pad(*state.level, *state.lifts, state.trees, sled.body.x, sled.body.z, avoid);
  m = FreeHeli{};
  m.present = true;
  m.pad_x = pad.x;
  m.pad_z = pad.z;
  m.pad_heading = pad.heading;
  fresh(state);
}

hc::HeliControls heli_controls(const Controls& c) {
  return hc::HeliControls{core::clamp(c.lever, 0, 1), core::clamp(-c.lean, -1, 1),
                          core::clamp(c.ski.steer, -1, 1), core::clamp(c.pedal, -1, 1)};
}

hc::HeliWorld heli_world(const GameState& state) {
  return hc::HeliWorld{state.level.get(), &state.trees, 0, 0};
}

core::Vec3 heli_seat(const hc::HeliState& h) {
  return hc::heli_point(h, core::Vec3{hc::kSeatX, kSeatUp, 0});
}

bool heli_within(const GameState& state, int32_t i) {
  const FreeHeli& m = state.heli;
  if (i != 0 || !m.present || m.rider || m.mode != HeliMode::parked) return false;
  if (state.sled.present && (state.sled.rider || state.sled.thrown)) return false;
  const Racer& r = state.racers[static_cast<size_t>(i)];
  const sk::SkierState& c = r.skier;
  if (c.down || r.free.lift.phase != LiftPhase::none) return false;
  const core::Vec3 s = heli_seat(m.body);
  return core::hypot(c.x - s.x, c.z - s.z) <= kBoardReach && c.speed <= kBoardFastest;
}

bool step_heli_ride(GameState& state, int32_t i, Controls& c) {
  FreeHeli& m = state.heli;
  if (!m.present || i != 0) return false;
  m.t += kDt;
  if (m.mode == HeliMode::wreck) {
    if (m.t < kWreck) return false;
    // Burned out: he is put back on a fresh machine, or one waits on the pad for him.
    if (m.aboard) {
      start_again(state, i);
      return true;
    }
    fresh(state);
    say(state, i, HeliPhase::restart, 0);
    return false;
  }
  if (!m.rider) {
    // Sat on the skid: the press that does it is spent, never read again as the drop. Within
    // reach of a set-down army machine's pilot's door instead, he takes that (army_steal.hpp).
    if (c.machine && heli_within(state, i)) {
      c.machine = false;
      board(state, i);
    } else if (c.machine && steal_army_heli(state, i)) {
      c.machine = false;
      return true;
    } else if (m.mode == HeliMode::parked && m.body.grounded && m.body.spool == 0) {
      // Parked with its rotor still: not stepped.
      return false;
    }
  }
  fly(state, i, c);
  if (!m.rider) return false;
  if (m.mode == HeliMode::wreck) return false;
  if (c.machine) {
    c.machine = false;
    drop(state, i);
    return false;
  }
  hold(state, i);
  return true;
}

void take_heli(GameState& state, int32_t i, double x, double z, double heading, double spool) {
  FreeHeli& m = state.heli;
  m.body = hc::stand_heli(*state.level, x, z, heading);
  m.body.spool = spool;
  m.mode = HeliMode::parked;
  m.aboard = false;
  m.t = 0;
  m.flown = {};
  m.army = true;
  board(state, i);
}

void crash_heli(GameState& state, int32_t i) {
  FreeHeli& m = state.heli;
  if (!m.present || m.mode == HeliMode::wreck) return;
  hc::HeliEvents ev;
  ev.speed = core::hypot3(m.body.vx, m.body.vy, m.body.vz);
  wreck(state, i, ev);
}

bool restart_heli(GameState& state, int32_t i) {
  FreeHeli& m = state.heli;
  if (i != 0 || !m.present) return false;
  if (!m.rider && !(m.mode == HeliMode::wreck && m.aboard)) return false;
  start_again(state, i);
  return true;
}

const char* heli_phase_name(HeliPhase p) {
  switch (p) {
    case HeliPhase::board:
      return "board";
    case HeliPhase::drop:
      return "drop";
    case HeliPhase::crash:
      return "crash";
    case HeliPhase::home:
      return "home";
    case HeliPhase::restart:
      return "restart";
  }
  return "?";
}

}  // namespace fall_line::sim
