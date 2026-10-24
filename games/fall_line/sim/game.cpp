// The run's create and its step (game.hpp has the order).
//
// ge:summary create_game, step and state_hash's bodies
// ge:keywords game create_game step state_hash log channel course skier field log_step wanted
// ge:keywords army
// ge:skill sim
// ge:test tests/fall_line_sim_test.cpp
#include "fall_line/sim/game.hpp"

#include <cstring>
#include <string>

#include "core/num.hpp"
#include "core/num_text.hpp"
#include "core/output.hpp"

#include "fall_line/sim/army.hpp"
#include "fall_line/sim/army_heli.hpp"
#include "fall_line/sim/army_heli_rules.hpp"
#include "fall_line/sim/army_para.hpp"
#include "fall_line/sim/army_para_rules.hpp"
#include "fall_line/sim/army_rules.hpp"
#include "fall_line/sim/army_sled.hpp"
#include "fall_line/sim/army_sled_rules.hpp"
#include "fall_line/sim/course.hpp"
#include "fall_line/sim/crowd.hpp"
#include "fall_line/sim/field.hpp"
#include "fall_line/sim/free.hpp"
#include "fall_line/sim/gunfire.hpp"
#include "fall_line/sim/heli_ride.hpp"
#include "fall_line/sim/lift_ride.hpp"
#include "fall_line/sim/para_ride.hpp"
#include "fall_line/sim/sled_ride.hpp"
#include "fall_line/sim/wanted.hpp"
#include "mountain/mapgen/generate.hpp"
#include "prof/zones.hpp"
#include "skier/body.hpp"
#include "skier/skier.hpp"

namespace fall_line::sim {

namespace mg = ge::mountain::mapgen;
namespace sk = ge::skier;
namespace out = ge::core::output;

namespace {

// THE RUN'S LOG (docs/debugging.md): this step's events on four channels (`course`: gates,
// misses, the finish, resets; `skier`: flights, landings, wipeouts, glances; `field`: racers
// meeting; `wanted`: the stars, seen or searching, the arrest) at debug, and every racer's state
// each step on `skier` at trace. Off by default: a dropped line costs a table read and formats
// nothing.
void log_step(const GameState& state) {
  static const out::Channel kCourse = out::channel("course");
  static const out::Channel kSkier = out::channel("skier");
  static const out::Channel kField = out::channel("field");
  static const out::Channel kWanted = out::channel("wanted");
  static const out::Channel kArmy = out::channel("army");
  const auto f = [](double v, int d) { return ge::core::num::to_fixed(v, d); };
  for (const GameEvent& e : state.events) {
    const bool course =
        e.kind == EventKind::gate || e.kind == EventKind::missed || e.kind == EventKind::finish ||
        e.kind == EventKind::reset || e.kind == EventKind::onto || e.kind == EventKind::arrived ||
        e.kind == EventKind::boarded || e.kind == EventKind::unloaded ||
        e.kind == EventKind::sled || e.kind == EventKind::heli || e.kind == EventKind::para;
    const out::Channel ch = e.kind == EventKind::shoulder || e.kind == EventKind::bump ? kField
                            : e.kind == EventKind::wanted                              ? kWanted
                            : e.kind == EventKind::army || e.kind == EventKind::army_sled ||
                                    e.kind == EventKind::army_heli || e.kind == EventKind::gunfire
                                ? kArmy
                            : course ? kCourse
                                     : kSkier;
    if (!out::enabled(ch, out::Level::debug)) continue;
    const sk::SkierState& s = state.racers[static_cast<size_t>(e.racer)].skier;
    std::string line = "racer " + std::to_string(e.racer) + " ";
    line += e.kind == EventKind::gate ? "took" : event_name(e.kind);
    switch (e.kind) {
      case EventKind::gate:
      case EventKind::missed:
        line += " gate " + std::to_string(e.gate);
        break;
      case EventKind::reset:
        line += ", owing gate " + std::to_string(e.gate);
        break;
      case EventKind::finish:
        line += " in " + f(e.value, 3) + " s";
        break;
      case EventKind::landing:
        line += " at " + f(e.value, 2) + " g, owing gate " + std::to_string(e.gate);
        break;
      case EventKind::wipeout:
        line += " (cause " + std::to_string(e.gate) + ") at " + f(e.value * 3.6, 1) + " km/h";
        break;
      case EventKind::glance:
        line += " tree " + std::to_string(e.gate);
        break;
      case EventKind::shoulder:
        line += " with racer " + std::to_string(e.gate) + " closing at " + f(e.value, 2) + " m/s";
        break;
      case EventKind::takeoff:
        line += " owing gate " + std::to_string(e.gate);
        break;
      case EventKind::bump:
        line += " into amateur " + std::to_string(e.gate) + " closing at " + f(e.value, 2) + " m/s";
        break;
      case EventKind::wanted:
        line += " " + std::string(wanted_change_name(wanted_change(e))) + ", " +
                std::to_string(e.gate) + " stars";
        break;
      case EventKind::onto:
        line += " run " + std::to_string(e.gate);
        break;
      case EventKind::arrived:
        line += " at the foot of run " + std::to_string(e.gate) + " in " + f(e.value, 3) + " s";
        break;
      case EventKind::boarded:
        line += " lift " + std::to_string(e.gate);
        break;
      case EventKind::unloaded:
        line += " at the top of lift " + std::to_string(e.gate) + " after " + f(e.value, 1) + " s";
        break;
      case EventKind::sled:
        line += " " + std::string(sled_phase_name(static_cast<SledPhase>(e.gate)));
        break;
      case EventKind::heli:
        line += " " + std::string(heli_phase_name(static_cast<HeliPhase>(e.gate)));
        break;
      case EventKind::para:
        line += " " + std::string(para_phase_name(static_cast<ParaPhase>(e.gate)));
        break;
      case EventKind::army:
        line += " " + std::string(army_change_name(army_change(e))) + ", " + f(e.value, 0) +
                " men, " + std::to_string(state.army.soldiers.size()) + " out";
        break;
      case EventKind::army_sled:
        line += " snowmobile " + f(e.value, 0) + " " +
                army_sled_change_name(static_cast<ArmySledChange>(e.gate)) + ", " +
                std::to_string(state.army.sleds.size()) + " out";
        break;
      case EventKind::army_heli:
        line += " helicopter " + f(e.value, 0) + " " +
                army_heli_change_name(static_cast<ArmyHeliChange>(e.gate)) + ", " +
                std::to_string(state.army.helis.size()) + " out";
        break;
      case EventKind::gunfire: {
        const GunfirePhase ph = gunfire_phase(e);
        line += std::string(" gunfire ") + gunfire_phase_name(ph);
        if (ph == GunfirePhase::hit) {
          line += std::string(" ") + ge::skier::part_name(gunfire_part(e)) + " " + f(e.value, 0) +
                  " J, ISS " + std::to_string(ge::skier::severity_of(state.racers.front().body));
        }
        break;
      }
      case EventKind::hurt:
        line += " " + std::to_string(e.gate) + " injuries, severity " + f(e.value, 0);
        break;
      case EventKind::died:
        line += " at severity " + f(e.value, 0);
        break;
      case EventKind::army_para:
        line += " paramotor " + f(e.value, 0) + " " +
                army_para_change_name(static_cast<ArmyParaChange>(e.gate)) + ", " +
                std::to_string(state.army.paras.size()) + " out";
        break;
    }
    line += " at x " + f(s.x, 1) + " z " + f(s.z, 1) + ", " + f(s.speed * 3.6, 1) + " km/h";
    out::log_on(ch, out::Level::debug, line);
  }
  if (!out::enabled(kSkier, out::Level::trace)) return;
  for (size_t i = 0; i < state.racers.size(); i++) {
    const sk::SkierState& s = state.racers[i].skier;
    out::log_on(kSkier, out::Level::trace,
                "racer " + std::to_string(i) + " x " + f(s.x, 2) + " z " + f(s.z, 2) + " speed " +
                    f(s.speed * 3.6, 2) + " km/h edge " + f(s.edge, 3) + " tuck " + f(s.tuck, 2) +
                    " lean " + f(s.lean, 3) + " g_lat " + f(s.g_lat, 2) +
                    (s.on_snow ? "" : " air") + (s.down ? " down" : ""));
  }
}

// The seed a racer's body draws its risks off (body.hpp hashes it with the step): the map's and
// his place in the field, so two racers met by the same blow are hurt apart.
uint32_t body_seed(const GameState& state, int32_t i) {
  return ge::core::num::to_uint32(state.level->seed * 1000) ^
         (0x9e3779b9u * static_cast<uint32_t>(i + 1));
}

// The reset the run's mode makes: on the free ride, the paramotor's rig on him begun again on the
// summit (para_ride.hpp), the helicopter he is on or crashed in started
// again from its pad with him on it (heli_ride.hpp), the snowmobile he rides or was thrown off
// stood back on its belt with him on it (sled_ride.hpp), else his run's.
void reset(GameState& state, int32_t i) {
  if (state.injuries) sk::mend_body(state.racers[static_cast<size_t>(i)].body);
  if (state.mode == Mode::free) {
    if (!restart_para(state, i) && !restart_heli(state, i) && !remount_sled(state, i)) {
      reset_free(state, i);
    }
  } else {
    reset_racer(state, i);
  }
}

}  // namespace

const char* event_name(EventKind kind) {
  switch (kind) {
    case EventKind::gate:
      return "gate";
    case EventKind::missed:
      return "missed";
    case EventKind::finish:
      return "finish";
    case EventKind::takeoff:
      return "takeoff";
    case EventKind::landing:
      return "landing";
    case EventKind::wipeout:
      return "wipeout";
    case EventKind::glance:
      return "glance";
    case EventKind::reset:
      return "reset";
    case EventKind::shoulder:
      return "shoulder";
    case EventKind::wanted:
      return "wanted";
    case EventKind::onto:
      return "onto";
    case EventKind::arrived:
      return "arrived";
    case EventKind::boarded:
      return "boarded";
    case EventKind::unloaded:
      return "unloaded";
    case EventKind::sled:
      return "sled";
    case EventKind::army:
      return "army";
    case EventKind::heli:
      return "heli";
    case EventKind::para:
      return "para";
    case EventKind::bump:
      return "bump";
    case EventKind::army_sled:
      return "army-sled";
    case EventKind::army_heli:
      return "army-heli";
    case EventKind::gunfire:
      return "gunfire";
    case EventKind::hurt:
      return "hurt";
    case EventKind::died:
      return "died";
    case EventKind::army_para:
      return "army-para";
  }
  return "?";
}

mg::Level race_map(double seed, std::optional<int> version) {
  mg::GenerateOptions opts;
  opts.version = version.value_or(1);
  return mg::generate_level(seed, opts);
}

GameState create_game(const GameOptions& opts) {
  return create_game_on(std::make_shared<const mg::Level>(race_map(opts.seed, opts.version)),
                        opts.racers);
}

GameState create_game_on(std::shared_ptr<const mg::Level> level, int32_t racers) {
  GameState state;
  state.level = std::move(level);
  state.trees = sk::build_tree_index(*state.level);
  state.spec = sk::kReferenceSkis;
  const auto slots = static_cast<int32_t>(state.level->grid.size());
  const int32_t most = slots < 1 ? 1 : slots;
  state.racers.resize(static_cast<size_t>(racers > most ? most : racers < 1 ? 1 : racers));
  state.events.reserve(kEventsReserve);
  state.wanted.pursuers.reserve(wanted::kMaxPursuers);
  state.army.soldiers.reserve(army::kMostSoldiers);
  state.army.sleds.reserve(army_sled::kMostSleds);
  state.army.helis.reserve(army_heli::kMostHelis);
  state.army.paras.reserve(army_para::kMostParas);
  state.ways = army_ways(*state.level);
  const std::vector<Rider> field =
      deal_field(*state.level, static_cast<int32_t>(state.racers.size()));
  for (int32_t i = 0; i < static_cast<int32_t>(state.racers.size()); i++) {
    reset_racer(state, i);
    state.racers[static_cast<size_t>(i)].progress = Progress{};
    state.racers[static_cast<size_t>(i)].rider = field[static_cast<size_t>(i)];
  }
  state.events.clear();
  return state;
}

void step(GameState& state, std::span<const Controls> controls) {
  out::set_step(state.steps);
  state.events.clear();
  const sk::World world = state.world();
  for (int32_t i = 0; i < static_cast<int32_t>(state.racers.size()); i++) {
    Racer& r = state.racers[static_cast<size_t>(i)];
    Progress& p = r.progress;
    Controls c =
        static_cast<size_t>(i) < controls.size() ? controls[static_cast<size_t>(i)] : Controls{};
    // Mortally hurt (by a blow under the INJURIES switch, or by the hunt's rounds) he is not stood
    // back up, and dead his run is over: he lies where he fell.
    const bool doomed = r.body.mortal >= 0;
    if (doomed) c = Controls{};
    if (c.reset && !p.finished) reset(state, i);
    // A lift that has him carries him: his skis, his run and the run's resets wait.
    if (state.mode == Mode::free && on_lift(state, i)) {
      step_lift(state, i);
      continue;
    }
    // Under the paramotor's wing the machine press lets the rig go, and is not read again.
    if (state.mode == Mode::free) para_press(state, i, c);
    // On the helicopter's skid or the snowmobile's boards the step is the machine's: his skis,
    // his run and the run's resets wait. A press the helicopter spends is not read again.
    if (state.mode == Mode::free) {
      GE_ZONE("heli");
      if (step_heli_ride(state, i, c)) continue;
    }
    if (state.mode == Mode::free) {
      GE_ZONE("sled");
      if (step_sled_ride(state, i, c)) continue;
    }
    sk::SkierInput in = para_held(state, i, c.ski);
    if (p.finished) in = sk::SkierInput{0, 0, 1, false};
    const double x0 = r.skier.x;
    const double z0 = r.skier.z;
    sk::SkierEvents ev;
    {
      GE_ZONE("skier");
      sk::step_skier(r.skier, in, world, state.spec, kDt, ev);
    }
    // THE BODY after the skis: every racer's under the INJURIES switch, his skis' blows judged on
    // it; off, only a body the hunt's rounds have hurt mortally, to run death's course.
    if (state.injuries || doomed) {
      GE_ZONE("body");
      sk::BodyEvents bev;
      const sk::BodyClock clock{state.t + kDt, kDt, state.steps, body_seed(state, i)};
      sk::step_body(r.body, r.skier, state.injuries ? ev : sk::SkierEvents{}, world, state.spec,
                    clock, bev);
      const auto iss = static_cast<double>(sk::severity_of(r.body));
      if (bev.taken > 0) state.events.push_back(GameEvent{EventKind::hurt, i, bev.taken, iss});
      if (bev.died) state.events.push_back(GameEvent{EventKind::died, i, 0, iss});
    }
    if (state.mode == Mode::free) {
      GE_ZONE("para");
      step_para_ride(state, i, c);
    }
    if (state.mode == Mode::free) {
      GE_ZONE("free");
      step_free(state, i, x0, z0);
      board_lift(state, i);
    } else {
      GE_ZONE("course");
      step_course(state, i, x0, z0);
    }
    if (ev.took_off) state.events.push_back(GameEvent{EventKind::takeoff, i, p.next, 0});
    if (ev.landed) state.events.push_back(GameEvent{EventKind::landing, i, p.next, ev.landing_g});
    if (ev.glanced) state.events.push_back(GameEvent{EventKind::glance, i, ev.tree, 0});
    if (ev.wiped_out) {
      p.wipeouts++;
      state.events.push_back(
          GameEvent{EventKind::wipeout, i, static_cast<int32_t>(ev.cause), r.skier.speed});
    }
    if (p.finished || r.body.mortal >= 0) continue;
    // Going nowhere while asking to go. A race's racer always is; on the free ride a skier
    // standing still to look at the view has his poles still, and is left there.
    const bool asks = state.mode != Mode::free || in.tuck > 0;
    p.stuck = !r.skier.down && asks && r.skier.speed < kStuckSpeed ? p.stuck + kDt : 0;
    if ((r.skier.down && r.skier.down_time > kDownReset) || p.stuck > kStuckReset) {
      reset(state, i);
    }
  }
  {
    GE_ZONE("meet");
    meet_racers(state);
  }
  if (state.crowd.active) {
    GE_ZONE("crowd");
    step_crowd(state);
    meet_crowd(state);
  }
  {
    GE_ZONE("army");
    step_army(state);
  }
  {
    GE_ZONE("gunfire");
    step_gunfire(state);
  }
  {
    GE_ZONE("wanted");
    step_wanted(state);
  }
  log_step(state);
  state.steps++;
  state.t = static_cast<double>(state.steps) * kDt;
}

uint64_t state_hash(const GameState& state, uint64_t h) {
  const auto mix = [&h](double v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof u);
    for (int b = 0; b < 64; b += 8) {
      h ^= (u >> b) & 0xffu;
      h *= 0x100000001b3ull;
    }
  };
  mix(state.t);
  for (const Racer& r : state.racers) {
    h = sk::skier_hash(r.skier, h);
    if (state.injuries) h = sk::body_hash(r.body, h);
    const Progress& p = r.progress;
    mix(p.next);
    mix(p.missed ? 1 : 0);
    mix(p.finished ? 1 : 0);
    mix(p.finish_time);
    mix(p.resets);
    mix(p.wipeouts);
    mix(p.missed_gates);
    mix(p.stuck);
    if (state.mode != Mode::free) continue;
    const FreeRide& f = r.free;
    mix(f.run);
    mix(f.last);
    mix(f.last_s);
    mix(static_cast<double>(f.skied.size()));
    for (const int32_t k : f.skied) mix(k);
    mix(f.distance);
    mix(f.arrived ? 1 : 0);
    mix(f.arrived_at);
    mix(static_cast<double>(f.lift.phase));
    mix(f.lift.lift);
    mix(f.lift.carrier);
    mix(f.lift.since);
    mix(f.lift.ridden);
  }
  if (state.sled.present) {
    const FreeSled& m = state.sled;
    h = ge::snowmobile::sled_hash(m.body, h);
    mix(static_cast<double>(m.mode));
    mix(m.rider ? 1 : 0);
    mix(m.thrown ? 1 : 0);
    mix(m.t);
    mix(m.taken);
    mix(m.still);
    mix(m.asleep ? 1 : 0);
  }
  if (state.heli.present) {
    const FreeHeli& m = state.heli;
    h = ge::helicopter::heli_hash(m.body, h);
    mix(static_cast<double>(m.mode));
    mix(m.rider ? 1 : 0);
    mix(m.aboard ? 1 : 0);
    mix(m.t);
    mix(m.taken);
  }
  if (state.para.present) {
    h = ge::paramotor::para_hash(state.para.body, h);
    mix(state.para.starts);
  }
  if (state.crowd.active) h = crowd_hash(state.crowd, h);
  return gunfire_hash(state.gunfire, army_hash(state.army, wanted_hash(state.wanted, h)));
}

}  // namespace fall_line::sim
