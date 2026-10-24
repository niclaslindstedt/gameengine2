// The army's helicopters (army_heli.hpp).
//
// ge:summary step_army_helis' body: the call, the flight, the hunt, the landings, the men out
// ge:keywords army helicopter step_army_helis call arrive fly hunt_want home_want settle
// ge:keywords ask_landings work_landing step_heli_soldier army_helis_hash why station landing
// ge:skill sim
// ge:test tests/fall_line_army_heli_test.cpp
#include "fall_line/sim/army_heli.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "ai/choose.hpp"
#include "ai/why.hpp"
#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"

#include "fall_line/sim/army.hpp"
#include "fall_line/sim/army_door.hpp"
#include "fall_line/sim/army_heli_rules.hpp"
#include "fall_line/sim/army_land.hpp"
#include "fall_line/sim/army_rules.hpp"
#include "fall_line/sim/army_steal.hpp"
#include "fall_line/sim/army_troop.hpp"

namespace fall_line::sim {

namespace lm = ge::core::libm;
namespace num = ge::core::num;
using ge::core::angle_diff;
using ge::core::clamp;
using ge::core::hypot;
using ge::core::kPi;
using ge::core::smoothstep;
using namespace army_heli;

namespace {

constexpr int64_t kAskSteps = static_cast<int64_t>(kHz);

void say(GameState& state, ArmyHeliChange c, const ArmyHeli& h) {
  state.events.push_back(
      GameEvent{EventKind::army_heli, 0, static_cast<int32_t>(c), static_cast<double>(h.id)});
}

TroopWorld troop_world(const GameState& state) {
  return TroopWorld{state.level.get(), &state.trees, state.army.soldiers, state.t, kDt};
}

// The most the disc's lean accelerates it, m/s^2.
double turn_most() { return kGravity * lm::tan(kTilt); }

// An angle into (-pi, pi].
double wrap(double a) { return lm::atan2(lm::sin(a), lm::cos(a)); }

// How the hunted man moves, m/s: the free ride's helicopter he rides, or his skis.
Spot quarry_velocity(const GameState& state) {
  if (state.heli.present && state.heli.rider) {
    return Spot{state.heli.body.vx, state.heli.body.vy, state.heli.body.vz};
  }
  const ge::skier::SkierState& c = state.racers.front().skier;
  return Spot{c.vx, c.vy, c.vz};
}

// Whether he is on the snow, not up in a machine: what a landing waits for.
bool on_the_ground(const GameState& state, Spot q) {
  if (state.heli.present && state.heli.rider) return false;
  return q.y - state.level->ground_at(q.x, q.z) < kOnGroundM;
}

// A point on the map's edge `inset` m inside it (negative: past it), on the bearing `b` from the
// map's middle.
Spot edge_point(double size, double b, double inset) {
  const double sx = lm::sin(b);
  const double sz = lm::cos(b);
  const double r = (size / 2 - inset) / num::max(num::max(std::fabs(sx), std::fabs(sz)), 1e-6);
  return Spot{size / 2 + sx * r, 0, size / 2 + sz * r};
}

// The machine `id`, or null.
ArmyHeli* heli_of(ArmyState& a, int32_t id) {
  for (ArmyHeli& h : a.helis) {
    if (h.id == id) return &h;
  }
  return nullptr;
}

bool fit(const Soldier& s) { return s.mode != SoldierMode::gone; }

// ─── the call ────────────────────────────────────────────────────────────────────────────────

void turn_home(GameState& state, ArmyHeli& h) {
  if (h.mode != ArmyHeliMode::hunt) return;
  h.mode = ArmyHeliMode::leave;
  h.t = 0;
  say(state, ArmyHeliChange::leaving, h);
}

// CALL IN what the roster asks for; send home what it no longer does.
void call(GameState& state, int32_t want, Spot q) {
  ArmyState& a = state.army;
  int32_t live = 0;
  int32_t waiting = 0;
  for (ArmyHeli& h : a.helis) {
    if (h.mode != ArmyHeliMode::due && h.mode != ArmyHeliMode::hunt && !landing(h)) continue;
    if (live >= want) {
      if (h.mode == ArmyHeliMode::due) {
        h.mode = ArmyHeliMode::gone;
      } else if (h.tasked) {
        h.task.done = true;  // on the snow or on its way down: it lifts first
      } else {
        turn_home(state, h);
      }
      continue;
    }
    live++;
    waiting += h.mode == ArmyHeliMode::due ? 1 : 0;
  }
  const double half = state.level->size / 2;
  while (live < want && a.helis.size() < kMostHelis) {
    const int32_t k = a.helis_called;
    if (k == 0) a.from = lm::atan2(q.x - half, q.z - half);
    // 0, +1, -1, +2, -2 ... spreads either side of the army's bearing.
    const int32_t step = k == 0 ? 0 : (k % 2 == 1 ? 1 : -1) * ((k + 1) / 2);
    ArmyHeli h;
    h.id = a.next_heli++;
    h.due = state.t + kRespondS + kStaggerS * waiting;
    h.bearing = a.from + step * kSpread;
    h.dir = h.id % 2 == 0 ? 1 : -1;
    h.aboard = kSection;
    a.helis.push_back(h);
    a.helis_called++;
    waiting++;
    live++;
    say(state, ArmyHeliChange::called, a.helis.back());
  }
}

// IN AT THE EDGE: the called machine on the map, at its cruise, its nose on him.
void arrive(GameState& state, ArmyHeli& h, Spot q) {
  const Spot e = edge_point(state.level->size, h.bearing, kInsetM);
  h.x = e.x;
  h.z = e.z;
  h.y = num::max(state.level->ground_at(e.x, e.z) + kEnterM, army_floor(state, e.x, e.z, kFloorM));
  h.heading = lm::atan2(q.x - e.x, q.z - e.z);
  h.vx = lm::sin(h.heading) * kCruise;
  h.vz = lm::cos(h.heading) * kCruise;
  h.mode = ArmyHeliMode::hunt;
  h.t = 0;
  say(state, ArmyHeliChange::arrived, h);
}

// ─── the flight ──────────────────────────────────────────────────────────────────────────────

// WHAT THE MACHINE WANTS: the velocity over the snow, m/s; the height of its skid datum, m; the
// fastest it will go for it, m/s; and the heading, when it asks for one.
struct Want {
  double vx = 0, vz = 0, y = 0, most = kCruise;
  bool has_face = false;
  double face = 0;
};

// THE WAY ROUND A CIRCLE of `r` m about (cx, cz), itself moving at (cvx, cvz): the circle's own
// way plus `speed` along it (the way round `dir` says) and a closing on its radius, coming
// straight in from far off and taking up the way round as it nears.
void circle_way(const ArmyHeli& h, double cx, double cz, double cvx, double cvz, double r,
                double speed, double& vx, double& vz) {
  const double dx = h.x - cx;
  const double dz = h.z - cz;
  const double d = num::max(1.0, hypot(dx, dz));
  const double nx = dx / d;
  const double nz = dz / d;
  const double round = speed * (1 - smoothstep(1.5 * r, 3 * r, d));
  const double close = clamp(kClose * (r - d), -kCruise, kClose * r);
  vx = cvx + h.dir * nz * round + nx * close;
  vz = cvz - h.dir * nx * round + nz * close;
}

// WHAT IT WANTS while it hunts: round the pylon turn about him (or, out of sight, round where he
// was last seen), or in echelon off his tail when he runs. The choice is the `station` decision.
Want hunt_want(const GameState& state, const ArmyHeli& h, Spot q, ge::ai::Why* why) {
  const WantedState& w = state.wanted;
  const bool seen = w.seen;
  const Spot c = seen ? q : Spot{w.last.x, w.last.y, w.last.z};
  const Spot qv = seen ? quarry_velocity(state) : Spot{};
  const double r =
      seen ? kOrbitM : kSearchShare * kSearchRadiusM[static_cast<size_t>(clamp(w.stars, 0, 5))];
  const double speed = seen ? kOrbitSpeed : kSearchSpeed;
  const double up = seen ? kOrbitUpM : kSearchUpM;
  Want s;
  circle_way(h, c.x, c.z, qv.x, qv.z, r, speed, s.vx, s.vz);
  s.y = c.y + up;
  const double run = hypot(qv.x, qv.z);
  const double k = seen ? smoothstep(0.6 * kFast, kFast, run) : 0;
  const double off = hypot(h.x - c.x, h.z - c.z);
  if (why) {
    why->decide("station");
    why->fact("seen", seen ? 1 : 0);
    why->fact("his_speed", run);
    why->fact("off", off);
    why->fact("radius", r);
    why->option("orbit", 1 - k);
    if (!seen) why->bar("out of sight");
    why->option("echelon", k);
    if (!seen) why->bar("out of sight");
    why->option("search", 1);
    if (seen) why->bar("in sight");
    why->choose(!seen     ? "search"
                : k > 0.5 ? "echelon"
                          : "orbit",
                !seen     ? "out of sight"
                : k > 0.5 ? "he runs"
                          : "he is slow");
  }
  // The man square out of the inward door, while it circles him near.
  if (seen && k < 0.5 && off < 1.6 * r) {
    s.has_face = true;
    s.face = lm::atan2(q.x - h.x, q.z - h.z) - h.dir * (kPi / 2);
  }
  if (k <= 0) return s;
  // THE ECHELON: behind him and out to the side, flown at his way and closed on.
  const double ux = qv.x / run;
  const double uz = qv.z / run;
  const double ex = q.x - ux * kBackM - uz * kAsideM * h.dir;
  const double ez = q.z - uz * kBackM + ux * kAsideM * h.dir;
  const auto mix = [k](double o, double e) { return o + (e - o) * k; };
  s.vx = mix(s.vx, qv.x + kGain * (ex - h.x));
  s.vz = mix(s.vz, qv.z + kGain * (ez - h.z));
  s.y = mix(s.y, q.y + kAboveM);
  s.most = kCruise + (kDash - kCruise) * k;
  if (k > 0.5) {
    s.has_face = true;
    // Its doors manned, crabbed side-on to him so the inward door has the line.
    s.face = lm::atan2(ux, uz) - (doors_manned(state, h) ? h.dir * kCrab : 0);
  }
  return s;
}

// WHAT IT WANTS on the way out: past the map's edge on its own bearing, high, at its cruise.
Want home_want(const GameState& state, const ArmyHeli& h) {
  const double half = state.level->size / 2;
  const Spot e = edge_point(state.level->size, lm::atan2(h.x - half, h.z - half), -2 * kPastM);
  const double d = num::max(1.0, hypot(e.x - h.x, e.z - h.z));
  Want s;
  s.vx = (e.x - h.x) / d * kCruise;
  s.vz = (e.z - h.z) / d * kCruise;
  s.y = state.level->ground_at(h.x, h.z) + kLeaveUpM;
  return s;
}

// The nose turned toward `face` at no more than its yaw rate, the rate eased in.
void turn_to(ArmyHeli& h, double face) {
  const double turn = clamp(angle_diff(h.heading, face) * 1.2, -kYaw, kYaw);
  h.yaw_rate += (turn - h.yaw_rate) * num::min(1.0, kDt / 0.5);
  h.heading = wrap(h.heading + h.yaw_rate * kDt);
}

// The acceleration toward the velocity (wx, wz), inside the lean and smoothed; then the velocity.
void pull(ArmyHeli& h, double wx, double wz) {
  const double most = turn_most();
  double ax = (wx - h.vx) / 1.2;
  double az = (wz - h.vz) / 1.2;
  const double am = hypot(ax, az);
  if (am > most) {
    ax *= most / am;
    az *= most / am;
  }
  const double lag = num::min(1.0, kDt / kLagS);
  h.ax += (ax - h.ax) * lag;
  h.az += (az - h.az) * lag;
  h.vx += h.ax * kDt;
  h.vz += h.az * kDt;
}

// The attitude read off what it does: leant into its acceleration, the nose hung at speed.
void attitude(ArmyHeli& h) {
  const double fx = lm::sin(h.heading);
  const double fz = lm::cos(h.heading);
  const double af = h.ax * fx + h.az * fz;
  const double as = h.ax * fz - h.az * fx;
  const double along = h.vx * fx + h.vz * fz;
  h.pitch = -lm::atan2(af, kGravity) - kNose * along;
  h.roll = lm::atan2(as, kGravity);
}

// FLY as it wants: the acceleration the velocity asks, the height kept off the snow ahead and the
// floor never broken, the attitude read off it all.
void fly(GameState& state, ArmyHeli& h, Want s) {
  const ge::mountain::mapgen::Level& level = *state.level;
  double wx = s.vx;
  double wz = s.vz;
  // KEPT APART: off every other machine flying.
  for (const ArmyHeli& o : state.army.helis) {
    if (&o == &h || (o.mode != ArmyHeliMode::hunt && o.mode != ArmyHeliMode::leave)) continue;
    const double dx = h.x - o.x;
    const double dz = h.z - o.z;
    const double d = hypot(dx, dz);
    if (d < kApartM && d > 0.1) {
      const double push = (kApartM - d) * 0.4;
      wx += dx / d * push;
      wz += dz / d * push;
    }
  }
  const double wv = hypot(wx, wz);
  if (wv > s.most) {
    wx *= s.most / wv;
    wz *= s.most / wv;
  }
  pull(h, wx, wz);
  // THE HEIGHT: over the station's, and clear of the snow it will meet.
  const double flat = hypot(h.vx, h.vz);
  double ahead = level.ground_at(h.x, h.z);
  if (flat > 1) {
    const auto n = static_cast<int32_t>(std::floor(flat * kLookS / kLookStepM));
    for (int32_t i = 1; i <= n; i++) {
      const double d = kLookStepM * i;
      ahead = num::max(ahead, level.ground_at(h.x + h.vx / flat * d, h.z + h.vz / flat * d));
    }
  }
  const double want = num::max(s.y, ahead + kClearM);
  const double vy = clamp(0.5 * (want - h.y), -kSink, kClimb);
  h.vy += clamp((vy - h.vy) * 1.5, -kHeave, kHeave) * kDt;
  h.x += h.vx * kDt;
  h.y += h.vy * kDt;
  h.z += h.vz * kDt;
  // THE FLOOR: never into the mountain or the woods.
  const double low = army_floor(state, h.x, h.z, kFloorM);
  if (h.y < low) {
    h.y = low;
    h.vy = num::max(0.0, h.vy);
  }
  turn_to(h, s.has_face ? s.face : flat > 8 ? lm::atan2(h.vx, h.vz) : h.heading);
  attitude(h);
}

// ─── the landing ─────────────────────────────────────────────────────────────────────────────

// The attitude eased toward level, the nose hung `k` rad, the nose toward `face`.
void level_out(ArmyHeli& h, double face, double k) {
  turn_to(h, face);
  const double lag = num::min(1.0, kDt / kLagS);
  h.pitch += (-k - h.pitch) * lag;
  h.roll += (0 - h.roll) * lag;
}

void slide_doors(ArmyHeli& h, double open) {
  h.doors = clamp(h.doors + (open > h.doors ? 1 : open < h.doors ? -1 : 0) * 0.6 * kDt, 0, 1);
}

// A LANDING, flown: in to its spot, braked over the last of the way and held `kOverM` up until
// over it; then sunk onto the snow (`set`) or held at the rope's hover (`rope`) while the men go
// out or come in; then climbed straight back off it (`rise`) to the hunt. A landing given up
// climbs off at once.
void settle(GameState& state, ArmyHeli& h, bool on) {
  LandTask& task = h.task;
  if (h.tasked && !on) task.done = true;
  if (h.mode == ArmyHeliMode::land) {
    if (!h.tasked || task.done) {
      h.mode = ArmyHeliMode::rise;
      h.t = 0;
      return;
    }
    const double dx = task.x - h.x;
    const double dz = task.z - h.z;
    const double d = hypot(dx, dz);
    const double hover = task.rope
                             ? rope_hover(state, task.x, task.z, task.y)
                             : num::max(task.y + kOverM, army_floor(state, h.x, h.z, kFloorM));
    // In along the way, braked to a stop over it as a pilot flares.
    const double sp = clamp(std::sqrt(2 * kFlare * num::max(0.0, d - 1)), kSlow, kCruise);
    const double wx = d > 0.5 ? dx / d * num::min(sp, d) : 0;
    const double wz = d > 0.5 ? dz / d * num::min(sp, d) : 0;
    pull(h, wx, wz);
    h.x += h.vx * kDt;
    h.z += h.vz * kDt;
    const bool over = d < kWithinM && hypot(h.vx, h.vz) < kSettle;
    // Held up (clear of the snow and the woods) until over it; then down: to the snow, or to the
    // rope's hover.
    const double floor = army_floor(state, h.x, h.z, kFloorM);
    const double want = over ? (task.rope ? hover : task.y) : num::max(hover, floor);
    const double vy = clamp(0.6 * (want - h.y), over ? -kSettleSink : -kSink, kClimb);
    h.vy += clamp((vy - h.vy) * 1.5, -kHeave, kHeave) * kDt;
    h.y += h.vy * kDt;
    if (!over && h.y < floor) {
      h.y = floor;
      h.vy = num::max(0.0, h.vy);
    }
    attitude(h);
    turn_to(h, d > kFaceM ? lm::atan2(dx, dz) : task.heading);
    if (over) slide_doors(h, 1);
    const bool down = task.rope ? std::fabs(h.y - hover) < 1.2 : h.y <= task.y + 0.05;
    if (over && down) {
      h.mode = task.rope ? ArmyHeliMode::rope : ArmyHeliMode::set;
      h.t = 0;
      if (!task.rope) h.y = task.y;
      h.vx = h.vy = h.vz = h.ax = h.az = 0;
      say(state, ArmyHeliChange::landed, h);
    }
    return;
  }
  if (h.mode == ArmyHeliMode::set || h.mode == ArmyHeliMode::rope) {
    h.vx = h.vy = h.vz = h.ax = h.az = 0;
    if (h.tasked) {
      // Held on the spot: on the snow, or at the hover over it.
      h.x += (task.x - h.x) * num::min(1.0, kDt);
      h.z += (task.z - h.z) * num::min(1.0, kDt);
      if (h.mode == ArmyHeliMode::set) h.y = task.y;
    }
    level_out(h, h.tasked ? task.heading : h.heading, h.mode == ArmyHeliMode::set ? 0.02 : 0.03);
    slide_doors(h, 1);
    if (!h.tasked || task.done || h.t > kWaitS + 30) {
      h.mode = ArmyHeliMode::rise;
      h.t = 0;
    }
    return;
  }
  // RISE: straight up off it, then the hunt again.
  const double top =
      num::max(army_floor(state, h.x, h.z, kFloorM), state.level->ground_at(h.x, h.z) + kClearM);
  h.vx = h.vz = h.ax = h.az = 0;
  h.vy += clamp((kLiftClimb - h.vy) * 1.5, -kHeave, kHeave) * kDt;
  h.y += h.vy * kDt;
  level_out(h, h.heading, 0.04);
  slide_doors(h, 0);
  if (h.y >= top) {
    h.mode = ArmyHeliMode::hunt;
    h.t = 0;
    h.tasked = false;
    say(state, ArmyHeliChange::lifted, h);
  }
}

// Where a man comes out of `side`'s door (1 the right), or (`rope`) the rope's hook, world frame.
Spot door_of(const ArmyHeli& h, int32_t side, bool rope) {
  const double out = rope ? kHookOutM : kHalfWidthM + 0.3;
  const double rx = lm::cos(h.heading) * side;
  const double rz = -lm::sin(h.heading) * side;
  const double fx = lm::sin(h.heading);
  const double fz = lm::cos(h.heading);
  return Spot{h.x + rx * out + fx * kDoorM, h.y + (rope ? kHookUpM : 0),
              h.z + rz * out + fz * kDoorM};
}

// Whether a man out of a door is at his place and knelt there.
bool knelt(const Soldier& s) { return hypot(s.tx - s.x, s.tz - s.z) < 1 && s.speed < 0.1; }

// A MACHINE ON THE SNOW OR AT THE HOVER: its men put out (a drop) or taken in (a pick-up), and
// the task said done once they are.
void work_landing(GameState& state, ArmyHeli& h) {
  if (!h.tasked || h.task.done) return;
  ArmyState& a = state.army;
  LandTask& task = h.task;
  if (!task.drop) {
    bool coming = false;
    for (const Soldier& s : a.soldiers) {
      coming = coming ||
               (s.from == SoldierFrom::heli && s.squad == h.id && s.mode == SoldierMode::board);
    }
    if (!coming || h.t > kWaitS) task.done = true;
    return;
  }
  // One man out each `every` s: from each door in turn, or down the rope.
  const double every = task.rope ? kRopeGapS : kEveryS;
  const bool room = a.soldiers.size() < army::kMostSoldiers;
  if (task.put < task.count && h.aboard > 0 && room && h.t >= task.put * every) {
    const int32_t n = task.put;
    const int32_t side = n % 2 == 0 ? 1 : -1;
    const Spot door = door_of(h, side, task.rope);
    const double rx = lm::cos(h.heading) * side;
    const double rz = -lm::sin(h.heading) * side;
    const double fx = lm::sin(h.heading);
    const double fz = lm::cos(h.heading);
    // Out to his side of the machine, square to it, spread fore and aft.
    const int32_t pair = n / 2;  // the men go out in pairs, one from each door
    const int32_t pairs = (task.count + 1) / 2;
    const double along =
        (static_cast<double>(pair) - static_cast<double>(pairs - 1) / 2.0) * kAlongM;
    const double run = task.rope ? kRopeRunM : kRunM;
    Soldier& s = enlist_soldier(state, SoldierFrom::heli, h.id,
                                task.rope ? SoldierMode::rope : SoldierMode::out, door.x, door.z,
                                lm::atan2(rx, rz));
    if (task.rope) s.y = door.y;
    s.tx = door.x + rx * run + fx * along;
    s.tz = door.z + rz * run + fz * along;
    task.put++;
    h.aboard--;
    a.dropped++;
  }
  if (!room && task.put < task.count) task.count = task.put;
  // Done once every man is out and clear (on the snow and knelt), and `kLingerS` on.
  bool clear = true;
  for (const Soldier& s : a.soldiers) {
    if (s.from != SoldierFrom::heli || s.squad != h.id) continue;
    if (s.mode == SoldierMode::rope || (s.mode == SoldierMode::out && !knelt(s))) clear = false;
  }
  if ((task.put >= task.count || h.aboard <= 0) && clear) {
    if (task.clear < 0) task.clear = state.t;
    if (state.t - task.clear >= kLingerS) task.done = true;
  }
  if (h.t > kWaitS + 20) task.done = true;
}

// ASK FOR LANDINGS, once a second while he is on the snow: a pick-up for a squad left behind,
// else a drop near him for a machine with men aboard while the roster is short of soldiers. With
// more than one machine out, the first stays up as the OVERWATCH and never lands.
void ask_landings(GameState& state, int32_t want, Spot q) {
  ArmyState& a = state.army;
  const WantedState& w = state.wanted;
  if (!on_the_ground(state, q)) return;
  int32_t live = 0;
  for (const Soldier& s : a.soldiers) live += s.from == SoldierFrom::heli && fit(s) ? 1 : 0;
  std::array<LandAvoid, kMostHelis + 1> avoid{};
  size_t avoids = 0;
  for (const ArmyHeli& h : a.helis) {
    if (h.tasked) avoid[avoids++] = LandAvoid{h.task.x, h.task.z, kAvoidM};
  }
  if (state.heli.present) {
    avoid[avoids++] = LandAvoid{state.heli.body.x, state.heli.body.z, kAvoidHeliM};
  }
  // A drop he has skied far from before a man is out: given up, a nearer one asked for later.
  for (ArmyHeli& h : a.helis) {
    if (h.mode != ArmyHeliMode::land || !h.tasked || !h.task.drop || h.task.put > 0) continue;
    if (hypot(q.x - h.task.x, q.z - h.task.z) > kStaleM) h.task.done = true;
  }
  int32_t up = 0;
  const ArmyHeli* watch = nullptr;
  for (const ArmyHeli& h : a.helis) {
    if (!h.on || (h.mode != ArmyHeliMode::hunt && !landing(h))) continue;
    up++;
    if (watch == nullptr || (watch->tasked && !h.tasked)) watch = &h;
  }
  if (up < 2) watch = nullptr;
  const Spot last{w.last.x, w.last.y, w.last.z};
  for (ArmyHeli& h : a.helis) {
    if (h.mode != ArmyHeliMode::hunt || h.tasked || !h.on || &h == watch) continue;
    ge::ai::Why* why = state.why ? state.why->follow(kAgentBase + h.id, state.steps) : nullptr;
    // THE LEAPFROG: every man of its squad far from where he was last seen, long, unseeing.
    int32_t n = 0;
    bool behind = true;
    double cx = 0, cy = 0, cz = 0;
    for (const Soldier& s : a.soldiers) {
      if (s.from != SoldierFrom::heli || s.squad != h.id || !fit(s)) continue;
      n++;
      cx += s.x;
      cy += s.y;
      cz += s.z;
      const bool chasing = s.mode == SoldierMode::chase || s.mode == SoldierMode::hold;
      behind = behind && chasing && !s.sees && s.t > kAfterS &&
               hypot(s.x - last.x, s.z - last.z) > kRecallM;
    }
    behind = behind && n > 0 && h.aboard + n <= kSection;
    const double off = hypot(h.x - q.x, h.z - q.z);
    if (why) {
      why->decide("landing");
      why->fact("aboard", h.aboard);
      why->fact("squad", n);
      why->fact("soldiers", live);
      why->fact("want", want);
      why->fact("off", off);
    }
    if (behind) {
      cx /= n;
      cy /= n;
      cz /= n;
      const std::optional<LandSpot> spot =
          land_spot(state, cx, cy, cz, 0, 0, std::span(avoid.data(), avoids),
                    LandRing{kPickNearM, kPickFarM, kPickPreferM});
      if (spot && !spot->rope) {
        h.tasked = true;
        h.task = LandTask{false, spot->x, spot->y, spot->z, spot->heading, false, n, 0, -1, false};
        avoid[avoids++] = LandAvoid{spot->x, spot->z, kAvoidM};
        for (Soldier& s : a.soldiers) {
          if (s.from != SoldierFrom::heli || s.squad != h.id || !fit(s)) continue;
          // Not a record of its own: the machine's "pick" decision is open (ai/why.hpp).
          ge::ai::enter(s.mode, s.t, SoldierMode::board, nullptr, soldier_mode_name, "");
        }
        if (why) why->choose("pick", "squad left behind");
        continue;
      }
    }
    const char* none = h.aboard <= 0       ? "none aboard"
                       : live >= want      ? "roster has its men"
                       : !w.seen           ? "out of sight"
                       : off > kAskWithinM ? "too far"
                                           : nullptr;
    std::optional<LandSpot> spot;
    if (none == nullptr) {
      const Spot v = quarry_velocity(state);
      spot =
          land_spot(state, q.x, q.y, q.z, v.x, v.z, std::span(avoid.data(), avoids), drop_ring());
      if (!spot) none = "no place";
    }
    if (none != nullptr) {
      if (why) why->choose("stay up", none);
      continue;
    }
    const int32_t count = std::min(h.aboard, want - live);
    h.tasked = true;
    h.task =
        LandTask{true, spot->x, spot->y, spot->z, spot->heading, spot->rope, count, 0, -1, false};
    avoid[avoids++] = LandAvoid{spot->x, spot->z, kAvoidM};
    live += count;
    if (why)
      why->choose(spot->rope ? "rope" : "drop", spot->rope ? "no flat place" : "a flat place");
  }
}

// WRITE THE MACHINES into the wanted level's pursuers: every one hunting or about a landing.
void write_pursuers(GameState& state) {
  std::vector<Pursuer>& ps = state.wanted.pursuers;
  std::erase_if(ps, [](const Pursuer& p) {
    return p.id >= kPursuerBase && p.id < kPursuerBase + static_cast<int32_t>(kMostHelis);
  });
  for (const ArmyHeli& h : state.army.helis) {
    if (ps.size() >= ps.capacity()) break;
    if (h.mode != ArmyHeliMode::hunt && !landing(h)) continue;
    ps.push_back(Pursuer{kPursuerBase + h.id, PursuerKind::helicopter, h.x, h.y + kEyeM, h.z});
  }
}

}  // namespace

bool landing(const ArmyHeli& h) {
  return h.mode == ArmyHeliMode::land || h.mode == ArmyHeliMode::set ||
         h.mode == ArmyHeliMode::rope || h.mode == ArmyHeliMode::rise;
}

void step_army_helis(GameState& state, bool on, Spot q) {
  ArmyState& a = state.army;
  const int32_t want = on && a.send_helis ? roster_of(state.wanted.stars).helicopters : 0;
  const int32_t soldiers = on && a.send_helis ? roster_of(state.wanted.stars).soldiers : 0;
  if (want == 0 && a.helis.empty()) return;
  call(state, want, q);
  const double size = state.level->size;
  const WantedState& w = state.wanted;
  for (ArmyHeli& h : a.helis) {
    h.t += kDt;
    switch (h.mode) {
      case ArmyHeliMode::due:
        if (state.t >= h.due) arrive(state, h, q);
        break;
      case ArmyHeliMode::hunt: {
        if (h.tasked && !h.task.done) {
          h.mode = ArmyHeliMode::land;
          h.t = 0;
          settle(state, h, on);
          break;
        }
        ge::ai::Why* why = state.why ? state.why->follow(kAgentBase + h.id, state.steps) : nullptr;
        fly(state, h, hunt_want(state, h, q, why));
        const double reach =
            num::max(kSearchRadiusM[static_cast<size_t>(clamp(w.stars, 0, 5))], 2 * kOrbitM);
        if (hypot(h.x - w.last.x, h.z - w.last.z) < reach) h.on = true;
        break;
      }
      case ArmyHeliMode::land:
      case ArmyHeliMode::set:
      case ArmyHeliMode::rope:
      case ArmyHeliMode::rise:
        settle(state, h, on);
        if (h.mode == ArmyHeliMode::set || h.mode == ArmyHeliMode::rope) work_landing(state, h);
        break;
      case ArmyHeliMode::leave:
        fly(state, h, home_want(state, h));
        if (h.x < -kPastM || h.z < -kPastM || h.x > size + kPastM || h.z > size + kPastM) {
          h.mode = ArmyHeliMode::gone;
          say(state, ArmyHeliChange::gone, h);
        }
        break;
      case ArmyHeliMode::fall:
      case ArmyHeliMode::wreck:
        step_downed_heli(state, h);
        break;
      case ArmyHeliMode::gone:
        break;
    }
  }
  // His machine flown into one of theirs (army_steal.hpp).
  ram_army_helis(state);
  if (on && state.steps % kAskSteps == 0) ask_landings(state, soldiers, q);
  step_army_doors(state, q);
  std::erase_if(a.helis, [](const ArmyHeli& h) { return h.mode == ArmyHeliMode::gone; });
  // ON THEIR WAY: the search does not run out before the first machine has reached it.
  if (on && !w.seen) {
    bool coming = false;
    bool there = false;
    for (const ArmyHeli& h : a.helis) {
      coming = coming || h.mode == ArmyHeliMode::due || h.mode == ArmyHeliMode::hunt;
      there = there || h.on;
    }
    if (coming && !there) state.wanted.unseen = 0;
  }
  write_pursuers(state);
}

bool step_heli_soldier(GameState& state, Soldier& s) {
  const TroopWorld tw = troop_world(state);
  ArmyHeli* h = heli_of(state.army, s.squad);
  switch (s.mode) {
    case SoldierMode::rope: {
      // Down the rope under its hook, onto the snow.
      const double ground = state.level->ground_at(s.x, s.z);
      s.y = num::max(ground, s.y - kSlide * kDt);
      s.speed = 0;
      if (s.y <= ground) {
        enter_mode(state, s, SoldierMode::out, "on the snow");
      }
      return false;
    }
    case SoldierMode::out: {
      // Out to his place, knelt there facing out until his machine has lifted.
      const bool held =
          h != nullptr && (h->mode == ArmyHeliMode::set || h->mode == ArmyHeliMode::rope);
      if (!held && knelt(s)) {
        enter_mode(state, s, SoldierMode::chase, "his machine has lifted");
        return false;
      }
      if (knelt(s)) {
        stop_here(tw, s);
        return false;
      }
      const double heading = s.heading;
      go_toward(tw, s, s.tx, s.tz, army::kSkate * 1.5, 0);
      if (hypot(s.tx - s.x, s.tz - s.z) < 1) {
        stop_here(tw, s);
        s.heading = heading;
      }
      return true;
    }
    case SoldierMode::board: {
      if (h == nullptr || !h->tasked || h->task.drop || h->task.done) {
        enter_mode(state, s, SoldierMode::chase, "his machine is not coming");
        return false;
      }
      if (h->mode != ArmyHeliMode::set) {
        // Waiting off the spot for it to land.
        go_toward(tw, s, h->task.x, h->task.z, army::kPace, kStandByM);
        return hypot(h->task.x - s.x, h->task.z - s.z) > kStandByM + 2;
      }
      // To the nearer door, and in.
      const Spot r = door_of(*h, 1, false);
      const Spot l = door_of(*h, -1, false);
      const Spot d = hypot(r.x - s.x, r.z - s.z) < hypot(l.x - s.x, l.z - s.z) ? r : l;
      go_toward(tw, s, d.x, d.z, army::kSkate * 1.5, 0);
      if (hypot(d.x - s.x, d.z - s.z) < kBoardM) {
        s.mode = SoldierMode::gone;
        h->aboard++;
        state.army.picked++;
      }
      return true;
    }
    default:
      return false;
  }
}

uint64_t army_helis_hash(const ArmyState& a, uint64_t h) {
  const auto mix = [&h](double v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof u);
    for (int b = 0; b < 64; b += 8) {
      h ^= (u >> b) & 0xffu;
      h *= 0x100000001b3ull;
    }
  };
  if (a.helis_called == 0) return h;
  mix(static_cast<double>(a.helis.size()));
  for (const ArmyHeli& m : a.helis) {
    mix(m.id);
    mix(static_cast<double>(m.mode));
    mix(m.x);
    mix(m.y);
    mix(m.z);
    mix(m.vx);
    mix(m.vy);
    mix(m.vz);
    mix(m.heading);
    mix(m.doors);
    mix(m.aboard);
    mix(m.on ? 1 : 0);
    mix(m.tasked ? 1 : 0);
    mix(m.task.put);
    mix(m.task.done ? 1 : 0);
  }
  mix(a.next_heli);
  mix(a.helis_called);
  mix(a.from);
  mix(a.dropped);
  mix(a.picked);
  // Only once one was taken or brought down, so a hunt that never was keeps its digest.
  if (a.stolen > 0 || a.downed > 0) {
    mix(a.stolen);
    mix(a.downed);
    for (const ArmyHeli& m : a.helis) {
      mix(m.yaw_rate);
      mix(m.roll);
      mix(m.pitch);
    }
  }
  return h;
}

const char* army_heli_mode_name(ArmyHeliMode m) {
  switch (m) {
    case ArmyHeliMode::due:
      return "due";
    case ArmyHeliMode::hunt:
      return "hunt";
    case ArmyHeliMode::land:
      return "land";
    case ArmyHeliMode::set:
      return "set";
    case ArmyHeliMode::rope:
      return "rope";
    case ArmyHeliMode::rise:
      return "rise";
    case ArmyHeliMode::leave:
      return "leave";
    case ArmyHeliMode::gone:
      return "gone";
    case ArmyHeliMode::fall:
      return "fall";
    case ArmyHeliMode::wreck:
      return "wreck";
  }
  return "?";
}

const char* army_heli_change_name(ArmyHeliChange c) {
  switch (c) {
    case ArmyHeliChange::called:
      return "called";
    case ArmyHeliChange::arrived:
      return "arrived";
    case ArmyHeliChange::landed:
      return "landed";
    case ArmyHeliChange::lifted:
      return "lifted";
    case ArmyHeliChange::leaving:
      return "leaving";
    case ArmyHeliChange::gone:
      return "gone";
    case ArmyHeliChange::stolen:
      return "stolen";
    case ArmyHeliChange::rammed:
      return "rammed";
    case ArmyHeliChange::down:
      return "down";
  }
  return "?";
}

}  // namespace fall_line::sim
