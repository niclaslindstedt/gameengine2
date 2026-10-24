// The army on foot (army.hpp).
//
// ge:summary army_ways, lift_for, step_army and army_hash's bodies: the squads, the waves, chase
// ge:keywords army step_army send_lift_squads send_waves climb_with_wave chase try_arrest
// ge:keywords write_pursuers way_at army_hash enlist_soldier
// ge:skill sim
// ge:test tests/fall_line_army_test.cpp
#include "fall_line/sim/army.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ai/choose.hpp"
#include "ai/think.hpp"
#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"

#include "fall_line/sim/army_heli.hpp"
#include "fall_line/sim/army_para.hpp"
#include "fall_line/sim/army_rules.hpp"
#include "fall_line/sim/army_sled.hpp"
#include "fall_line/sim/army_sled_rules.hpp"
#include "fall_line/sim/army_troop.hpp"
#include "fall_line/sim/free.hpp"
#include "fall_line/sim/gunfire.hpp"
#include "fall_line/sim/lift_ride.hpp"
#include "mountain/lifts/lift_loop.hpp"
#include "mountain/mapgen/lift_line.hpp"
#include "mountain/mapgen/query.hpp"

namespace fall_line::sim {

namespace mg = ge::mountain::mapgen;
namespace ml = ge::mountain::lifts;
namespace lm = ge::core::libm;
namespace num = ge::core::num;
using ge::core::clamp;
using ge::core::hypot;
using ge::core::kPi;

namespace {

// Steps between a soldier's looks (each man on his own step of them, by his id).
constexpr int64_t kLookSteps = static_cast<int64_t>(wanted::kLookEveryS * kHz);

void push(GameState& state, ArmyChange c, double men) {
  state.events.push_back(GameEvent{EventKind::army, 0, static_cast<int32_t>(c), men});
}

TroopWorld troop_world(const GameState& state) {
  return TroopWorld{state.level.get(), &state.trees, state.army.soldiers,
                    state.t,           kDt,          &state.army.near};
}

// Whether a man still counts: out on the map and not sent home.
bool fit(const Soldier& s) { return s.mode != SoldierMode::gone; }

// How many more men the army has room for.
int32_t room(const ArmyState& a) {
  return static_cast<int32_t>(army::kMostSoldiers) - static_cast<int32_t>(a.soldiers.size());
}

// ─── the lifts' squads ───────────────────────────────────────────────────────────────────────

// A lift's queue spot `back` m behind its load point, facing up the line.
ml::Place queue_spot(const ml::LiftPlan& plan, double back) {
  ml::Place at = ml::load_spot(plan);
  at.x -= plan.dx * back;
  at.z -= plan.dz * back;
  return at;
}

int32_t seats_of(const ml::LiftPlan& plan) { return army::kSeats[static_cast<size_t>(plan.kind)]; }

// SEND A SQUAD UP while the roster is short of lifted men: after the call's delay, then one every
// `kEveryS`.
void send_lift_squads(GameState& state, int32_t want, Spot q) {
  ArmyState& a = state.army;
  int32_t live = 0;
  for (const Soldier& s : a.soldiers) live += s.from == SoldierFrom::lift && fit(s) ? 1 : 0;
  if (live >= want) {
    a.lift_called = false;
    return;
  }
  if (!a.lift_called) {
    a.lift_called = true;
    a.lift_due = state.t + army::kRespondS;
  }
  if (state.t < a.lift_due) return;
  a.lift_due = state.t + army::kEveryS;
  ge::ai::Why* why = state.why ? state.why->follow(army::kCallAgent, state.steps) : nullptr;
  const int32_t lift = lift_for(state, q, why);
  if (lift < 0) return;
  const ml::LiftPlan& plan = (*state.lifts)[static_cast<size_t>(lift)];
  const int32_t squad = a.lift_squads++;
  const int32_t n = std::min({army::kSquad, want - live, room(a)});
  for (int32_t i = 0; i < n; i++) {
    // Put down at the lane's far end, a man's room apart; each goes to his place in the line.
    const ml::Place at = queue_spot(plan, army::kLaneM + i * army::kQueueM);
    Soldier& s =
        enlist_soldier(state, SoldierFrom::lift, squad, SoldierMode::walk, at.x, at.z, at.heading);
    const ml::Place to = queue_spot(plan, i * army::kQueueM);
    s.lift = lift;
    s.tx = to.x;
    s.tz = to.z;
  }
  if (n > 0) push(state, ArmyChange::squad, n);
}

// ON HIS CARRIER up the line; let go at the top where a rider is, after him.
void ride(GameState& state, Soldier& s) {
  const ml::LiftPlan& plan = (*state.lifts)[static_cast<size_t>(s.lift)];
  const double t = state.t + kDt;
  const ml::LoopSpot spot = ml::carrier_at(plan, s.carrier, t);
  if (spot.side == 0 && spot.turn < 0 && spot.u < ml::off_at(plan)) {
    const ml::Place at = ml::carrier_place(plan, spot);
    s.x = at.x;
    s.z = at.z;
    s.y = state.level->ground_at(s.x, s.z);
    s.heading = at.heading;
    s.speed = ml::carrier_speed_at(plan, spot.u, spot.side);
    return;
  }
  // AT THE TOP: stood where a rider is let go, side by side by his seat, and after him.
  const mg::LetGo off = mg::let_go_of(plan.kind, plan.bottom, plan.top);
  const double across = (s.seat - (seats_of(plan) - 1) / 2.0) * army::kApartM;
  s.x = off.x + lm::cos(off.heading) * across;
  s.z = off.z - lm::sin(off.heading) * across;
  s.y = state.level->ground_at(s.x, s.z);
  s.heading = off.heading;
  s.speed = 0;
  s.lift = -1;
  s.carrier = -1;
  enter_mode(state, s, SoldierMode::chase, "let off at the top");
  s.progress.x = s.x;
  s.progress.z = s.z;
  s.progress.t = state.t;
  push(state, ArmyChange::off, 1);
}

// WAITING at the load line: taken by the carrier that comes round while it has a seat free.
void wait_at_lift(GameState& state, Soldier& s) {
  const ml::LiftPlan& plan = (*state.lifts)[static_cast<size_t>(s.lift)];
  stop_here(troop_world(state), s);
  s.heading = plan.heading;
  const int32_t k = ml::carrier_passing(plan, ml::load_at(plan), state.t, kDt);
  if (k < 0) return;
  int32_t taken = 0;
  for (const Soldier& o : state.army.soldiers) {
    taken += o.mode == SoldierMode::ride && o.lift == s.lift && o.carrier == k ? 1 : 0;
  }
  if (taken >= seats_of(plan)) return;
  enter_mode(state, s, SoldierMode::ride, "a seat free");
  s.carrier = k;
  s.seat = taken;
}

// ─── the ground waves ────────────────────────────────────────────────────────────────────────

// Whether he, at `q`, could see a column formed up at (x, z).
bool in_his_sight(const GameState& state, double x, double z, Spot q) {
  const Pursuer p{0, PursuerKind::soldier, x, state.level->ground_at(x, z) + army::kEyeM, z};
  return sees_quarry(*state.level, state.trees, p, q, false);
}

// A WAVE sent up the mountain: its men on foot formed up in a column out of his sight down the
// way from him and set climbing it.
void send_wave(GameState& state, int32_t stars) {
  ArmyState& a = state.army;
  const std::optional<WaveMix> mix = ground_wave(stars, a.waves);
  const int32_t wave = a.waves++;
  // The snowmobiles it keeps out on top of the roster's (army_sled.hpp).
  if (mix) a.wave_sleds = std::min(army_sled::kWaveSleds, mix->snowmobiles);
  if (!mix || !state.ways || state.ways->empty()) return;
  int32_t live = 0;
  for (const Soldier& s : a.soldiers) live += s.from == SoldierFrom::wave && fit(s) ? 1 : 0;
  const int32_t men = std::max(
      0, std::min({mix->foot, army::kWaveMost[static_cast<size_t>(stars)] - live, room(a)}));
  if (men == 0) return;
  // THE WAY: the one with a point nearest him, from that point down.
  const Spot q = quarry_of(state);
  int32_t way = -1;
  mg::TrackHit hit{};
  for (size_t k = 0; k < state.ways->size(); k++) {
    const mg::Track& t = (*state.ways)[k];
    if (t.points.size() < 2) continue;
    const mg::TrackHit h = mg::nearest_track_point(t, q.x, q.z);
    if (way < 0 || h.distance < hit.distance) {
      way = static_cast<int32_t>(k);
      hit = h;
    }
  }
  if (way < 0) return;
  const mg::Track& track = (*state.ways)[static_cast<size_t>(way)];
  const double s_hit = track.points[static_cast<size_t>(hit.index)].s;
  // WHERE IT FORMS UP: `kDismountM` down the way, and on while it is near him or he could see it.
  double s0 = s_hit + army::kDismountM;
  for (;;) {
    if (s0 - s_hit >= army::kDismountM + army::kHiddenM) break;
    const mg::TrackPoint p = army_way_at(track, s0);
    if (hypot(p.x - q.x, p.z - q.z) >= army::kAwayM && !in_his_sight(state, p.x, p.z, q)) break;
    s0 += army::kSampleM;
  }
  // THE COLUMN: rank behind rank down the way from its head, each man a little off his file and
  // his rank (his id's own), so it reads as men, not a grid.
  int32_t made = 0;
  for (int32_t r = 0; made < men; r++) {
    const double s = s0 + r * army::kRankM;
    const mg::TrackPoint p = army_way_at(track, s);
    const auto across = static_cast<int32_t>(
        clamp(std::floor((p.width - 2 * army::kMarginM) / army::kFileM) + 1, 1, army::kAbreast));
    const int32_t m = std::min(across, men - made);
    const double up = p.heading + kPi;
    const double rx = lm::cos(up);
    const double rz = -lm::sin(up);
    for (int32_t c = 0; c < m; c++) {
      const double j = a.next_id;
      const double lane = (c - (m - 1) / 2.0) * army::kFileM + (num::mod(j * 0.618, 1) - 0.5) * 1.2;
      const double back = (num::mod(j * 0.382, 1) - 0.5) * 1.5;
      const double x = p.x + rx * lane - lm::sin(up) * back;
      const double z = p.z + rz * lane - lm::cos(up) * back;
      Soldier& man = enlist_soldier(state, SoldierFrom::wave, wave, SoldierMode::climb, x, z, up);
      man.way = way;
      man.s = s;
      man.s_end = s_hit;
      man.lane = lane;
      made++;
    }
  }
  a.marched += made;
  push(state, ArmyChange::wave, made);
}

// SEND THE WAVES the hunt has come to, counted at the star he carries from when he reached it.
void send_waves(GameState& state) {
  ArmyState& a = state.army;
  const WantedState& w = state.wanted;
  if (a.wave_star != w.stars) {
    a.wave_star = w.stars;
    a.wave_at = 0;
  }
  const int32_t due = waves_sent(w, state.t);
  while (a.wave_at < due) {
    a.wave_at++;
    send_wave(state, w.stars);
  }
}

// ONE STEP OF A MAN CLIMBING WITH HIS WAVE: on up the way on his own file; true once he is to
// leave the column for the fight.
bool climb_with_wave(GameState& state, Soldier& s, Spot to) {
  const mg::Track& way = (*state.ways)[static_cast<size_t>(s.way)];
  const double d = hypot(to.x - s.x, to.z - s.z);
  if ((s.sees && d < army::kStandOffM + army::kHoldBandM) || d < army::kEngageM) return true;
  // His mark: a point on his file a little up the way ahead of him (ai/steer.hpp), the way walked
  // back down its arc to where it leaves the column.
  ge::ai::LinePoint mark;
  const auto on_file = [&](double at) {
    const mg::TrackPoint p = army_way_at(way, at);
    const double up = p.heading + kPi;
    const double half = num::max(0.0, p.width / 2 - army::kMarginM);
    const double lane = clamp(s.lane, -half, half);
    return ge::ai::LinePoint{p.x + lm::cos(up) * lane, p.z - lm::sin(up) * lane, lm::sin(up),
                             lm::cos(up)};
  };
  if (!ge::ai::pursue_along(s.s, s.s_end, -army::kSampleM, army::kReachM, s.x, s.z, on_file,
                            mark)) {
    return true;
  }
  const double tx = mark.x;
  const double tz = mark.z;
  // Caught on something (a side-step running out): a point further on.
  if (s.progress.slow >= 2 && s.progress.until > state.t && s.progress.until - state.t < kDt * 1.5)
    s.s -= army::kSampleM;
  go_toward(troop_world(state), s, tx, tz, army::kPace, -1);
  return false;
}

// ─── the chase ───────────────────────────────────────────────────────────────────────────────

// Whether the man is slow or down.
bool slow_or_down(const GameState& state) {
  const ge::skier::SkierState& c = state.racers[0].skier;
  return c.down || c.speed < army::kArrestSpeed;
}

// THE ARREST where the army does not shoot to kill: a man within reach of him while he is slow
// or down takes him.
bool try_arrest(GameState& state, Soldier& s, Spot q) {
  if (on_lift(state, 0) || hypot(q.x - s.x, q.z - s.z) > wanted::kArrestReachM) return false;
  if (!slow_or_down(state) || !arrest_quarry(state)) return false;
  enter_mode(state, s, SoldierMode::arrest, "within reach, slow or down");
  state.army.arrested_by = s.id;
  push(state, ArmyChange::arrest, 1);
  return true;
}

// THE CHASE: after him (or to where he was last seen), holding with him in sight and in range
// where they shoot to kill. Returns whether he means to be moving.
bool chase(GameState& state, Soldier& s, Spot q, FireRule fire) {
  const WantedState& w = state.wanted;
  const Spot to = w.seen ? q : Spot{w.last.x, w.last.y, w.last.z};
  const double d = hypot(to.x - s.x, to.z - s.z);
  const TroopWorld tw = troop_world(state);
  const double off = hypot(q.x - s.x, q.z - s.z);
  const bool in_range =
      off < army::kStandOffM || (off < army::kStandOffM + army::kHoldBandM && s.progress.slow >= 1);
  if (w.lethal && fire != FireRule::hold && s.sees && in_range && state.t >= s.rest) {
    enter_mode(state, s, SoldierMode::hold, "in sight and in range");
    stop_here(tw, s);
    return false;
  }
  if (!w.seen && d < army::kSearchedM) {
    stop_here(tw, s);
    return false;
  }
  // Close on a man who is down or slow: pulled up beside him.
  const bool beside = slow_or_down(state) && s.sees;
  go_toward(tw, s, to.x, to.z, army::kPace, beside ? 0.8 : -1);
  return !(beside && d < 1.5);
}

// HOLDING with him in sight: stood, his rifle on him, settled and firing a few bursts
// (gunfire.hpp); back to the chase once he is out of sight or out of range, or his bursts are
// fired.
void hold(GameState& state, Soldier& s, Spot q, FireRule fire) {
  stop_here(troop_world(state), s);
  const int32_t gun = army::kGunBase + s.id;
  if (!s.sees || hypot(q.x - s.x, q.z - s.z) > army::kStandOffM + army::kHoldBandM * 1.5) {
    enter_mode(state, s, SoldierMode::chase, "out of sight or range");
    s.bursts = 0;
    disarm_gun(state, gun);
    return;
  }
  s.heading = lm::atan2(q.x - s.x, q.z - s.z);
  if (!state.army.fire) return;
  if (s.t > army::kFireS || s.bursts >= army::kBursts) {
    enter_mode(state, s, SoldierMode::chase, "his bursts fired");
    s.bursts = 0;
    s.rest = state.t + army::kMoveS + army::kMoveSpreadS * num::mod(s.id * 0.618, 1);
    disarm_gun(state, gun);
    return;
  }
  if (s.t < army::kSettleS) return;
  const double fx = lm::sin(s.heading);
  const double fz = lm::cos(s.heading);
  Gun* g = arm_gun(state, gun, WeaponKind::rifle, s.x, s.y, s.z);
  if (g == nullptr) return;
  g->x = s.x + fx * army::kMuzzleOutM;
  g->y = s.y + army::kMuzzleM;
  g->z = s.z + fz * army::kMuzzleOutM;
  g->vx = g->vy = g->vz = 0;
  if (fire_at(state, *g, AimTarget{}, fire)) s.bursts++;
}

// WRITE THE SOLDIERS into the wanted level's pursuers: every man on the map who can look (not
// one in a gondola's cabin). The reserve is never outgrown.
void write_pursuers(GameState& state) {
  std::vector<Pursuer>& ps = state.wanted.pursuers;
  std::erase_if(ps, [](const Pursuer& p) { return p.id >= army::kPursuerBase; });
  for (const Soldier& s : state.army.soldiers) {
    if (ps.size() >= ps.capacity()) break;
    if (!fit(s)) continue;
    if (s.mode == SoldierMode::ride &&
        (*state.lifts)[static_cast<size_t>(s.lift)].kind == mg::LiftKind::gondola) {
      continue;
    }
    ps.push_back(
        Pursuer{army::kPursuerBase + s.id, PursuerKind::soldier, s.x, s.y + army::kEyeM, s.z});
  }
}

}  // namespace

mg::TrackPoint army_way_at(const mg::Track& way, double s) {
  if (s <= way.length) return mg::track_point_at(way, s);
  mg::TrackPoint p = way.points.back();
  const double on = s - way.length;
  p.x += lm::sin(p.heading) * on;
  p.z += lm::cos(p.heading) * on;
  p.s = s;
  return p;
}

Soldier& enlist_soldier(GameState& state, SoldierFrom from, int32_t squad, SoldierMode mode,
                        double x, double z, double heading) {
  ArmyState& a = state.army;
  Soldier s;
  s.id = a.next_id++;
  s.from = from;
  s.mode = mode;
  s.squad = squad;
  s.x = x;
  s.z = z;
  s.y = state.level->ground_at(x, z);
  s.heading = heading;
  s.tx = x;
  s.tz = z;
  s.progress.x = x;
  s.progress.z = z;
  s.progress.t = state.t;
  a.soldiers.push_back(s);
  return a.soldiers.back();
}

void enter_mode(GameState& state, Soldier& s, SoldierMode next, const char* because) {
  ge::ai::Why* why =
      state.why ? state.why->follow(army::kPursuerBase + s.id, state.steps) : nullptr;
  ge::ai::enter(s.mode, s.t, next, why, soldier_mode_name, because);
}

std::shared_ptr<const std::vector<mg::Track>> army_ways(const mg::Level& level) {
  auto ways = std::make_shared<std::vector<mg::Track>>();
  if (level.resort && !level.resort->runs.empty()) {
    const auto& runs = level.resort->runs;
    for (size_t k = 0; k < runs.size(); k++) {
      // A link lane ends at a lift's foot, not in the valley.
      if (runs[k].to) continue;
      ways->push_back(run_chain(level, static_cast<int32_t>(k)));
    }
  } else if (level.track.points.size() >= 2) {
    ways->push_back(level.track);
  }
  for (const mg::Track& t : *ways) {
    if (!t.points.empty()) mg::nearest_track_point(t, t.points[0].x, t.points[0].z);
  }
  return ways;
}

int32_t lift_for(const GameState& state, Spot q, ge::ai::Why* why) {
  if (!state.lifts) return -1;
  if (why) {
    why->decide("lift");
    why->fact("his_x", q.x);
    why->fact("his_z", q.z);
  }
  // Each lift's cost: how far its top is from him, and far dearer when it does not stand over him.
  const auto score = [&](int32_t i) {
    const mg::PlanPoint top = (*state.lifts)[static_cast<size_t>(i)].top;
    const double ty = state.level->ground_at(top.x, top.z);
    return -(hypot(top.x - q.x, top.z - q.z) + (ty < q.y + army::kAboveM ? 5000 - ty : 0));
  };
  return ge::ai::pick_best(
      static_cast<int32_t>(state.lifts->size()), score, why, "lift", [](int32_t) { return "lift"; },
      "its top nearest over him");
}

void step_army(GameState& state) {
  ArmyState& a = state.army;
  const WantedState& w = state.wanted;
  const bool on = w.active && w.ended == WantedEnd::running && w.stars > 0;
  if (!a.active && !on) return;
  a.active = true;
  const Spot q = quarry_of(state);
  const WantedRoster& roster = roster_of(on ? w.stars : 0);
  if (on) send_waves(state);
  if (on && roster.lifted > 0) {
    send_lift_squads(state, roster.lifted, q);
  } else {
    a.lift_called = false;
  }
  const bool arrested = w.ended == WantedEnd::arrested;
  const Spot last{w.last.x, w.last.y, w.last.z};
  const int64_t steps = state.steps;
  int32_t home = 0;
  build_near(a.near, a.soldiers);
  for (Soldier& s : a.soldiers) {
    s.t += kDt;
    // THE LOOK, every `kLookEveryS`, each man on his own step of it (ai/think.hpp).
    if (s.mode != SoldierMode::ride && ge::ai::thinks(steps, s.id, kLookSteps)) {
      const Pursuer p{0, PursuerKind::soldier, s.x, s.y + army::kEyeM, s.z};
      s.sees = on && sees_quarry(*state.level, state.trees, p, q, false);
    }
    const TroopWorld tw = troop_world(state);
    bool moving = false;
    switch (s.mode) {
      case SoldierMode::walk:
        go_toward(tw, s, s.tx, s.tz, army::kSkate * 1.3, 0.2);
        moving = hypot(s.tx - s.x, s.tz - s.z) > 2;
        if (hypot(s.tx - s.x, s.tz - s.z) < 0.6) {
          enter_mode(state, s, SoldierMode::wait, "at the load line");
        }
        break;
      case SoldierMode::wait:
        wait_at_lift(state, s);
        break;
      case SoldierMode::ride:
        ride(state, s);
        break;
      case SoldierMode::climb:
        if (!on) {
          stop_here(tw, s);
        } else if (climb_with_wave(state, s, w.seen ? q : last)) {
          enter_mode(state, s, SoldierMode::chase, "left the column");
        } else {
          moving = true;
        }
        break;
      case SoldierMode::chase:
        if (!on) {
          stop_here(tw, s);
        } else if (!(!w.lethal && try_arrest(state, s, q))) {
          moving = chase(state, s, q, roster.fire);
        }
        break;
      case SoldierMode::hold:
        if (on) {
          hold(state, s, q, roster.fire);
        } else {
          stop_here(tw, s);
        }
        break;
      case SoldierMode::arrest:
        stop_here(tw, s);
        s.heading = lm::atan2(q.x - s.x, q.z - s.z);
        break;
      case SoldierMode::out:
      case SoldierMode::rope:
      case SoldierMode::board:
        moving = step_heli_soldier(state, s);
        break;
      case SoldierMode::gone:
        break;
    }
    watch_progress(s, state.t, moving);
    // SENT HOME: a wave's man left far behind while the hunt runs; once it is over (not in his
    // arrest), every man far from him.
    if (s.mode == SoldierMode::gone || s.mode == SoldierMode::ride || arrested) continue;
    const bool behind =
        on && s.from == SoldierFrom::wave && hypot(s.x - last.x, s.z - last.z) > army::kBehindM;
    const bool done = !on && hypot(s.x - q.x, s.z - q.z) > army::kHomeM;
    if (behind || done) {
      s.mode = SoldierMode::gone;
      home++;
    }
  }
  // Gone home, or aboard a helicopter: their guns put away.
  for (const Soldier& s : a.soldiers) {
    if (s.mode == SoldierMode::gone) disarm_gun(state, army::kGunBase + s.id);
  }
  std::erase_if(a.soldiers, [](const Soldier& s) { return s.mode == SoldierMode::gone; });
  if (home > 0) push(state, ArmyChange::home, home);
  step_army_sleds(state, on, q, roster.fire);
  step_army_helis(state, on, q);
  step_army_paras(state, on, q);
  write_pursuers(state);
}

uint64_t army_hash(const ArmyState& a, uint64_t h) {
  if (!a.active) return h;
  const auto mix = [&h](double v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof u);
    for (int b = 0; b < 64; b += 8) {
      h ^= (u >> b) & 0xffu;
      h *= 0x100000001b3ull;
    }
  };
  mix(static_cast<double>(a.soldiers.size()));
  for (const Soldier& s : a.soldiers) {
    mix(s.id);
    mix(static_cast<double>(s.mode));
    mix(s.x);
    mix(s.y);
    mix(s.z);
    mix(s.heading);
    mix(s.speed);
    mix(s.sees ? 1 : 0);
    mix(s.carrier);
    mix(s.s);
    mix(s.progress.slow);
  }
  mix(a.next_id);
  mix(a.lift_called ? 1 : 0);
  mix(a.lift_due);
  mix(a.lift_squads);
  mix(a.waves);
  mix(a.wave_star);
  mix(a.wave_at);
  mix(a.marched);
  mix(a.arrested_by);
  return army_paras_hash(a, army_helis_hash(a, army_sleds_hash(a, h)));
}

const char* soldier_mode_name(SoldierMode m) {
  switch (m) {
    case SoldierMode::walk:
      return "walk";
    case SoldierMode::wait:
      return "wait";
    case SoldierMode::ride:
      return "ride";
    case SoldierMode::climb:
      return "climb";
    case SoldierMode::chase:
      return "chase";
    case SoldierMode::hold:
      return "hold";
    case SoldierMode::arrest:
      return "arrest";
    case SoldierMode::out:
      return "out";
    case SoldierMode::rope:
      return "rope";
    case SoldierMode::board:
      return "board";
    case SoldierMode::gone:
      return "gone";
  }
  return "?";
}

ArmyChange army_change(const GameEvent& e) { return static_cast<ArmyChange>(e.gate); }

const char* army_change_name(ArmyChange c) {
  switch (c) {
    case ArmyChange::squad:
      return "squad";
    case ArmyChange::wave:
      return "wave";
    case ArmyChange::off:
      return "off";
    case ArmyChange::arrest:
      return "arrest";
    case ArmyChange::home:
      return "home";
  }
  return "?";
}

}  // namespace fall_line::sim
