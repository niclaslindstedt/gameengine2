// The free ride's runs, the run under the skis and the reset (free.hpp).
//
// ge:summary free_map, run_tracks, run_chain, create_free_ride, step_free and reset_free
// ge:keywords free-ride run-tracks run-chain step_free reset_free foot onto
// ge:skill sim
// ge:test tests/fall_line_free_test.cpp
#include "fall_line/sim/free.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/math.hpp"
#include "fall_line/sim/army.hpp"
#include "fall_line/sim/army_heli_rules.hpp"
#include "fall_line/sim/army_para_rules.hpp"
#include "fall_line/sim/army_rules.hpp"
#include "fall_line/sim/army_sled_rules.hpp"
#include "fall_line/sim/course.hpp"
#include "fall_line/sim/heli_ride.hpp"
#include "fall_line/sim/sled_ride.hpp"
#include "fall_line/sim/wanted.hpp"
#include "mountain/mapgen/generate.hpp"
#include "mountain/mapgen/query.hpp"
#include "skier/skier.hpp"

namespace fall_line::sim {

namespace mg = ge::mountain::mapgen;
namespace sk = ge::skier;
namespace num = ge::core::num;

namespace {

// Whether he is on `track` and where: the hit, and how far inside its edge (negative outside).
bool on_track(const mg::Track& track, double x, double z, mg::TrackHit& hit, double& inside) {
  hit.distance = INFINITY;
  mg::nearest_within(track, x, z, kRunLook, hit);
  if (!(hit.distance < INFINITY)) return false;
  const double half = track.points[static_cast<size_t>(hit.index)].width / 2;
  inside = half + kOnRunMargin - hit.distance;
  return inside >= 0;
}

// Whether a run ends at a foot: it flows into no other.
bool is_foot(const mg::Level& level, int32_t run) {
  if (!level.resort) return true;
  return !level.resort->runs[static_cast<size_t>(run)].into.has_value();
}

void stand_on(GameState& state, int32_t i, int32_t run, double s) {
  const mg::Track& track = (*state.runs)[static_cast<size_t>(run)];
  const mg::TrackPoint at = mg::track_point_at(track, s);
  Racer& r = state.racers[static_cast<size_t>(i)];
  r.skier = sk::stand_skier(state.world(), at.x, at.z, at.heading, 0);
  r.free.run = run;
  r.free.last = run;
  r.free.last_s = num::min(s, track.length);
}

}  // namespace

mg::Level free_map(double seed, std::optional<int> version) {
  mg::GenerateOptions opts;
  opts.version = version;
  return mg::generate_level(seed, opts);
}

std::shared_ptr<const std::vector<mg::Track>> run_tracks(const mg::Level& level) {
  auto tracks = std::make_shared<std::vector<mg::Track>>();
  if (level.resort) {
    for (const mg::Run& run : level.resort->runs) {
      mg::Track t;
      t.points = run.points;
      t.length = run.length;
      tracks->push_back(std::move(t));
    }
  } else {
    mg::Track t;
    t.points = level.track.points;
    t.length = level.track.length;
    tracks->push_back(std::move(t));
  }
  // The index is built on the first ask: ask once here, so a step never builds one.
  for (const mg::Track& t : *tracks) {
    if (t.points.size() > 1) mg::nearest_track_point(t, t.points[0].x, t.points[0].z);
  }
  return tracks;
}

int32_t run_index(const mg::Level& level, std::string_view id) {
  if (!level.resort) return -1;
  const auto& runs = level.resort->runs;
  for (size_t i = 0; i < runs.size(); i++) {
    if (runs[i].id == id) return static_cast<int32_t>(i);
  }
  return -1;
}

mg::Track run_chain(const mg::Level& level, int32_t run) {
  mg::Track t;
  const auto add = [&t](const mg::TrackPoint& p) {
    mg::TrackPoint q = p;
    if (t.points.empty()) {
      q.s = 0;
    } else {
      const mg::TrackPoint& b = t.points.back();
      const double d = ge::core::hypot(q.x - b.x, q.z - b.z);
      if (d < 0.5) return;
      q.s = b.s + d;
    }
    t.points.push_back(q);
  };
  if (!level.resort) {
    for (const mg::TrackPoint& p : level.track.points) add(p);
  } else {
    const auto& runs = level.resort->runs;
    double from = 0;
    // Every run is joined at most once, so a loop in the map cannot hold the chain.
    std::vector<bool> joined(runs.size(), false);
    for (int32_t k = run; k >= 0 && !joined[static_cast<size_t>(k)];) {
      joined[static_cast<size_t>(k)] = true;
      const mg::Run& r = runs[static_cast<size_t>(k)];
      for (const mg::TrackPoint& p : r.points) {
        if (p.s >= from) add(p);
      }
      if (!r.into) break;
      from = r.into->s;
      k = run_index(level, r.into->run);
    }
  }
  t.length = t.points.empty() ? 0 : t.points.back().s;
  return t;
}

GameState create_free_ride(std::shared_ptr<const mg::Level> level, int32_t start) {
  GameState state;
  state.level = std::move(level);
  state.mode = Mode::free;
  state.runs = run_tracks(*state.level);
  state.lifts = std::make_shared<const std::vector<ge::mountain::lifts::LiftPlan>>(
      ge::mountain::lifts::lift_plans(*state.level));
  state.trees = sk::build_tree_index(*state.level);
  state.spec = sk::kReferenceSkis;
  state.racers.resize(1);
  state.events.reserve(kEventsReserve);
  state.wanted.pursuers.reserve(wanted::kMaxPursuers);
  state.army.soldiers.reserve(army::kMostSoldiers);
  state.army.sleds.reserve(army_sled::kMostSleds);
  state.army.helis.reserve(army_heli::kMostHelis);
  state.army.paras.reserve(army_para::kMostParas);
  state.ways = army_ways(*state.level);
  const auto n = static_cast<int32_t>(state.runs->size());
  FreeRide& f = state.racers[0].free;
  f.start = start >= 0 && start < n ? start : 0;
  f.skied.reserve(state.runs->size());
  stand_on(state, 0, f.start, kHeadIn);
  f.run = -1;
  f.last = -1;
  place_sled(state);
  place_heli(state);
  return state;
}

void step_free(GameState& state, int32_t i, double x0, double z0) {
  Racer& r = state.racers[static_cast<size_t>(i)];
  FreeRide& f = r.free;
  const sk::SkierState& s = r.skier;
  if (s.on_snow && !s.down) f.distance += ge::core::hypot(s.x - x0, s.z - z0);
  if (!s.on_snow) return;
  const auto& runs = *state.runs;
  mg::TrackHit hit;
  double inside = 0;
  int32_t on = -1;
  double on_s = 0;
  if (f.run >= 0 && on_track(runs[static_cast<size_t>(f.run)], s.x, s.z, hit, inside)) {
    on = f.run;
    on_s = hit.s;
  } else {
    double best = -1;
    for (int32_t k = 0; k < static_cast<int32_t>(runs.size()); k++) {
      if (on_track(runs[static_cast<size_t>(k)], s.x, s.z, hit, inside) && inside > best) {
        best = inside;
        on = k;
        on_s = hit.s;
      }
    }
  }
  if (on != f.run && on >= 0) {
    state.events.push_back(GameEvent{EventKind::onto, i, on, 0});
    if (std::find(f.skied.begin(), f.skied.end(), on) == f.skied.end()) f.skied.push_back(on);
  }
  f.run = on;
  if (on < 0) return;
  f.last = on;
  f.last_s = on_s;
  const mg::Track& t = runs[static_cast<size_t>(on)];
  if (!f.arrived && is_foot(*state.level, on) && on_s >= t.length - kFootReach) {
    f.arrived = true;
    f.arrived_at = state.t;
    state.events.push_back(GameEvent{EventKind::arrived, i, on, state.t});
  }
}

void reset_free(GameState& state, int32_t i) {
  Racer& r = state.racers[static_cast<size_t>(i)];
  FreeRide& f = r.free;
  f.lift.phase = LiftPhase::none;
  f.lift.carrier = -1;
  if (f.last >= 0) {
    stand_on(state, i, f.last, f.last_s + kResetPast);
  } else {
    stand_on(state, i, f.start, kHeadIn);
  }
  r.progress.stuck = 0;
  r.progress.resets++;
  state.events.push_back(GameEvent{EventKind::reset, i, f.run, 0});
}

}  // namespace fall_line::sim
