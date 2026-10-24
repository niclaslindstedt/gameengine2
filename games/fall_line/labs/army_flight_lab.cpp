// LAB `army-flight`: the army's helicopters on a real ski area. The hunted man is stood at a spot
// of the map (stood again every step, or moved along a line) while the hunt is pinned at its stars,
// and the army runs through the real `step` (`step_army_helis` after the men and their
// snowmobiles), its other arms on: THE HELICOPTERS table (per scenario, the machines called, when
// the first came in over the edge, how far round him it flew its circle, the landings and the fast
// ropes, the men put down and when the first was out, the men picked up again, the soldiers
// holding him at the end, when the stars were lost and the machines gone home, and the share of
// the time the first flew in echelon) and, per scenario, the map with every machine's path, where
// it set down and where the men went.
//
// Scenarios: `open` (open snow some 350 m up the mountain from the village, three stars: in, round
// him, a section set down near him that takes up its stand-off), `rope` (in the woods where nothing
// near him is flat and clear enough to set down, three stars: the section comes down a fast rope),
// `leapfrog` (the open spot, three stars, and once the section is out he is moved some 700 m off:
// the squad left behind is picked up and put down near him again), `hide` (stood under a spruce's
// crown deep in the woods, two stars: the machine searches round where he was last seen, the stars
// are lost and it flies home over the edge), `fast` (moved across the map at 35 m/s on open snow,
// one star: the machine flies in echelon off his tail, its `station` decision read off a trace).
//
// TAKEN AND DOWNED (army_steal.hpp): `steal` (the open spot, three stars: the first machine set
// down taken at its pilot's door, flown up 60 m and let go of) and `ram` (the free ride's machine
// flown into the first machine hunting him): the taken-and-downed table (when the machine was set
// down, taken or rammed, the stars after, when his machine and theirs came down, how long theirs
// fell, when the free ride's own was back on its pad), a picture each of the paths and the wrecks,
// and the rammed machine's fall as a chart.
//
// A scenario that never sees what it stages (no landing, no rope, no pick-up, no machine home, no
// echelon, no machine taken or rammed, nothing down on the snow) is a failure (exit 1).
//
// ge:summary Lab: the army's helicopters on seed 38: the circle, the drop, home, taken, rammed
// ge:keywords lab army helicopter army-heli pylon-turn orbit echelon search landing fast-rope
// ge:keywords leapfrog pick-up home paths scenario why station steal ram wreck fall
// ge:skill sim labs
// ge:test tests/fall_line_army_heli_test.cpp
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "ai/why.hpp"
#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num_text.hpp"
#include "fall_line/sim/army.hpp"
#include "fall_line/sim/army_heli.hpp"
#include "fall_line/sim/army_heli_rules.hpp"
#include "fall_line/sim/army_land.hpp"
#include "fall_line/sim/army_steal.hpp"
#include "fall_line/sim/army_steal_rules.hpp"
#include "fall_line/sim/free.hpp"
#include "fall_line/sim/game.hpp"
#include "fall_line/sim/heli_ride.hpp"
#include "fall_line/sim/wanted.hpp"
#include "helicopter/spec.hpp"
#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "mountain/labs/level_map.hpp"
#include "mountain/mapgen/generate.hpp"
#include "skier/body.hpp"
#include "skier/skier.hpp"

namespace {

namespace fs = fall_line::sim;
namespace mg = ge::mountain::mapgen;
namespace ah = fall_line::sim::army_heli;
namespace lm = ge::core::libm;
using ge::core::hypot;
using ge::lab::Rgba;

std::string fixed(double v, int d) { return ge::core::num::to_fixed(v, d); }

enum class Scene : uint8_t { open, rope, leapfrog, hide, fast };
constexpr Scene kScenes[] = {Scene::open, Scene::rope, Scene::leapfrog, Scene::hide, Scene::fast};

const char* scene_name(Scene s) {
  switch (s) {
    case Scene::open:
      return "open";
    case Scene::rope:
      return "rope";
    case Scene::leapfrog:
      return "leapfrog";
    case Scene::hide:
      return "hide";
    case Scene::fast:
      return "fast";
  }
  return "?";
}

// The open spot: this far up the mountain from the village at the least, m, clear of every trunk
// by this much, m. The leapfrog's second spot: this far from the first at the least, m.
constexpr double kOpenM = 350;
constexpr double kClearM = 15;
constexpr double kAwayM = 650;
// The fast man's speed, m/s, and how far in from the map's edge his line runs, its share.
constexpr double kFastSpeed = 35;
constexpr double kFastIn = 0.25;
// The colours the machines' paths are drawn in, by id; the men's.
constexpr Rgba kPathColours[] = {{0, 200, 255}, {255, 140, 0}, {0, 230, 120}, {255, 230, 0}};
constexpr Rgba kMen{255, 255, 255};

struct Hunt {
  fs::Spot q;
  fs::Spot q2;  // the leapfrog's second spot
  int32_t stars = 0;
  int32_t called = 0;
  double arrived = -1;
  double circle = -1;  // the first machine's mean distance from him while it circled him, m
  int32_t landed = 0;
  int32_t ropes = 0;
  int32_t dropped = 0;
  double first_out = -1;
  int32_t picked = 0;
  int32_t holding = 0;
  double lost = -1;
  int32_t gone = 0;
  double echelon = 0;  // the share of the hunt's station decisions that chose the echelon
  std::vector<std::vector<fs::Spot>> paths;
  std::vector<fs::Spot> sets;  // where the machines set down or hovered for the rope
  std::vector<fs::Spot> men;   // where the men were at the end
};

bool clear_of_trees(const fs::GameState& st, double x, double z, double r) {
  for (const mg::TreeDef& t : st.level->trees) {
    if (hypot(t.x - x, t.z - z) < r + t.radius) return false;
  }
  return true;
}

int32_t trees_within(const fs::GameState& st, double x, double z, double r) {
  int32_t n = 0;
  for (const mg::TreeDef& t : st.level->trees) n += hypot(t.x - x, t.z - z) < r ? 1 : 0;
  return n;
}

// Up the mountain from the village's machines' spot, the first open snow.
fs::Spot open_spot(const fs::GameState& st) {
  const fs::ArmyState& a = st.army;
  const double fx = lm::sin(a.pad_heading);
  const double fz = lm::cos(a.pad_heading);
  for (int32_t i = 0; i < 40; ++i) {
    const double r = kOpenM + 10.0 * i;
    for (const double side : {0.0, 60.0, -60.0, 120.0, -120.0}) {
      const double x = a.pad_x + fx * r + fz * side;
      const double z = a.pad_z + fz * r - fx * side;
      if (clear_of_trees(st, x, z, kClearM)) return {x, st.level->ground_at(x, z), z};
    }
  }
  return {a.pad_x + fx * kOpenM, 0, a.pad_z + fz * kOpenM};
}

// The leapfrog's second spot: the open point of a run nearest `kAwayM` from the first.
fs::Spot away_spot(const fs::GameState& st, fs::Spot from) {
  fs::Spot best = from;
  double off = 1e9;
  for (const mg::Track& t : *st.runs) {
    for (size_t i = 0; i < t.points.size(); i += 10) {
      const mg::TrackPoint& p = t.points[i];
      const double d = hypot(p.x - from.x, p.z - from.z);
      if (d < kAwayM || d - kAwayM >= off || !clear_of_trees(st, p.x, p.z, kClearM)) continue;
      off = d - kAwayM;
      best = {p.x, p.y, p.z};
    }
  }
  return best;
}

// In the woods, where the nearest landing is a fast rope's: the first of the densest stands of
// trees (by the trees within 25 m) whose landing search finds only a rope.
fs::Spot rope_spot(const fs::GameState& st) {
  const double size = st.level->size;
  std::vector<std::pair<int32_t, fs::Spot>> stands;
  const auto cells = static_cast<int32_t>(size * 0.7 / 60);
  for (int32_t j = 0; j <= cells; j++) {
    const double z = size * 0.15 + 60 * j;
    if (z >= size * 0.85) break;
    for (int32_t i = 0; i <= cells; i++) {
      const double x = size * 0.15 + 60 * i;
      if (x >= size * 0.85) break;
      const int32_t n = trees_within(st, x, z, 25);
      if (n >= 8 && clear_of_trees(st, x, z, 2))
        stands.push_back({n, {x, st.level->ground_at(x, z), z}});
    }
  }
  std::stable_sort(stands.begin(), stands.end(),
                   [](const auto& a, const auto& b) { return a.first > b.first; });
  for (size_t i = 0; i < stands.size() && i < 60; i++) {
    const fs::Spot q = stands[i].second;
    const auto spot = fs::land_spot(st, q.x, q.y, q.z, 0, 0, {}, fs::drop_ring());
    if (spot && spot->rope) return q;
  }
  return stands.empty() ? open_spot(st) : stands.front().second;
}

// Under the crown of a spruce deep in the woods (the most trees within 40 m): beside its trunk.
fs::Spot hide_spot(const fs::GameState& st) {
  int32_t most = -1;
  fs::Spot best{};
  for (size_t i = 0; i < st.level->trees.size(); i += 7) {
    const mg::TreeDef& t = st.level->trees[i];
    if (t.crown < 2) continue;
    const int32_t n = trees_within(st, t.x, t.z, 40);
    if (n > most) {
      most = n;
      best = {t.x + t.radius + 0.5, t.y, t.z};
    }
  }
  return best;
}

Hunt run_scene(const std::shared_ptr<const mg::Level>& level, Scene scene, double limit) {
  fs::GameState st = fs::create_free_ride(level, 0);
  // Staged: the machines and their men without the gunfire (the `gunfire` lab's).
  st.army.fire = false;
  Hunt h;
  h.stars = scene == Scene::hide ? 2 : scene == Scene::fast ? 1 : 3;
  fs::Spot at = scene == Scene::rope   ? rope_spot(st)
                : scene == Scene::hide ? hide_spot(st)
                                       : open_spot(st);
  const double size = level->size;
  if (scene == Scene::fast) at = {size * kFastIn, 0, size / 2};
  if (scene == Scene::leapfrog) h.q2 = away_spot(st, at);
  ge::ai::Why why(ah::kAgentBase);
  if (scene == Scene::fast) st.why = &why;
  st.racers[0].skier = ge::skier::stand_skier(st.world(), at.x, at.z, 0, 0);
  st.wanted.lethal = true;
  fs::set_wanted(st, h.stars);
  h.q = fs::quarry_of(st);
  const fs::Controls none{};
  bool moved = false;
  double way = 1;
  int32_t circling = 0;
  double circled = 0;
  int32_t stations = 0;
  int32_t echelons = 0;
  while (st.t < limit) {
    fs::WantedState& w = st.wanted;
    if (scene != Scene::hide && w.ended == fs::WantedEnd::running && w.stars > 0) w.heat = h.stars;
    // He stands where he was put; the fast man runs his line back and forth.
    if (scene == Scene::fast) {
      at.x += way * kFastSpeed * fs::kDt;
      if (at.x > size * (1 - kFastIn) || at.x < size * kFastIn) way = -way;
      st.racers[0].skier = ge::skier::stand_skier(
          st.world(), at.x, at.z, way > 0 ? ge::core::kPi / 2 : -ge::core::kPi / 2, kFastSpeed);
    } else {
      st.racers[0].skier = ge::skier::stand_skier(st.world(), at.x, at.z, 0, 0);
    }
    // THE LEAPFROG: once the section is out and the machine off the snow, he is somewhere else.
    if (scene == Scene::leapfrog && !moved && st.army.dropped > 0 && !st.army.helis.empty() &&
        st.army.helis.front().mode == fs::ArmyHeliMode::hunt) {
      at = h.q2;
      moved = true;
    }
    why.clear();
    fs::step(st, std::span<const fs::Controls>(&none, 1));
    for (const ge::ai::Decision& d : why.decisions()) {
      if (std::string(d.name) != "station") continue;
      stations++;
      echelons += std::string(d.chosen) == "echelon" ? 1 : 0;
    }
    for (const fs::GameEvent& e : st.events) {
      if (e.kind == fs::EventKind::wanted && fs::wanted_change(e) == fs::WantedChange::lost) {
        h.lost = st.t;
      }
      if (e.kind != fs::EventKind::army_heli) continue;
      const auto c = static_cast<fs::ArmyHeliChange>(e.gate);
      h.called += c == fs::ArmyHeliChange::called ? 1 : 0;
      h.gone += c == fs::ArmyHeliChange::gone ? 1 : 0;
      if (c == fs::ArmyHeliChange::arrived && h.arrived < 0) h.arrived = st.t;
      if (c == fs::ArmyHeliChange::landed) {
        for (const fs::ArmyHeli& m : st.army.helis) {
          if (m.id != static_cast<int32_t>(e.value)) continue;
          h.landed++;
          h.ropes += m.task.rope ? 1 : 0;
          h.sets.push_back({m.task.x, m.task.y, m.task.z});
        }
      }
    }
    if (h.first_out < 0 && st.army.dropped > 0) h.first_out = st.t;
    const fs::Spot q = fs::quarry_of(st);
    for (const fs::ArmyHeli& m : st.army.helis) {
      if (m.id == 0 && m.mode == fs::ArmyHeliMode::hunt && m.on && w.seen && scene != Scene::fast) {
        circled += hypot(m.x - q.x, m.z - q.z);
        circling++;
      }
    }
    if (st.steps % static_cast<int64_t>(fs::kHz) == 0) {
      for (const fs::ArmyHeli& m : st.army.helis) {
        if (m.mode == fs::ArmyHeliMode::due) continue;
        const auto id = static_cast<size_t>(m.id);
        if (h.paths.size() <= id) h.paths.resize(id + 1);
        h.paths[id].push_back({m.x, m.y, m.z});
      }
    }
    if (scene == Scene::hide && h.lost >= 0 && st.army.helis.empty()) break;
  }
  h.dropped = st.army.dropped;
  h.picked = st.army.picked;
  h.circle = circling > 0 ? circled / circling : -1;
  h.echelon = stations > 0 ? static_cast<double>(echelons) / stations : 0;
  for (const fs::Soldier& s : st.army.soldiers) {
    if (s.from != fs::SoldierFrom::heli) continue;
    h.men.push_back({s.x, s.y, s.z});
    h.holding += s.mode == fs::SoldierMode::hold ? 1 : 0;
  }
  return h;
}

// TAKEN AND BROUGHT DOWN (army_steal.hpp), on the open spot at three stars with the army's guns
// silent: `steal` takes the first machine set down at its pilot's door, climbs 60 m in it and lets
// go; `ram` boards the free ride's machine on its pad and flies it level with the first machine
// hunting him, its disc at theirs, closing at 10 m/s (the hands held by the lab), then waits for
// the wreck.
enum class Taking : uint8_t { steal, ram };

struct Taken {
  fs::Spot q;
  double set = -1;                           // the first machine set down, s
  double took = -1;                          // taken, or rammed, s
  int32_t stars = 0;                         // the stars just after
  double crashed = -1;                       // his machine down, s
  double down = -1;                          // theirs on the snow, s
  double back = -1;                          // the free ride's own on its pad again, s
  std::vector<std::vector<fs::Spot>> paths;  // the army's machines', by id
  std::vector<fs::Spot> his;                 // his machine's, while he or nobody flew it
  std::vector<fs::Spot> wrecks;              // his machine's and theirs, where each came down
  std::vector<double> fall_t, fall_h;        // the rammed machine's height over the snow, by time
};

Taken run_taking(const std::shared_ptr<const mg::Level>& level, Taking how, double limit) {
  fs::GameState st = fs::create_free_ride(level, 0);
  st.army.fire = false;
  Taken k;
  fs::Spot at = open_spot(st);
  st.racers[0].skier = ge::skier::stand_skier(st.world(), at.x, at.z, 0, 0);
  fs::set_wanted(st, 3);
  k.q = fs::quarry_of(st);
  fs::Controls c{};
  int32_t target = -1;
  bool stand = true;
  bool placed = false;
  double let_go = -1;
  while (st.t < limit) {
    fs::WantedState& w = st.wanted;
    if (w.ended == fs::WantedEnd::running && w.heat < 3) w.heat = 3;
    if (stand) st.racers[0].skier = ge::skier::stand_skier(st.world(), at.x, at.z, 0, 0);
    st.racers[0].body = ge::skier::BodyState{};
    c = fs::Controls{};
    ge::helicopter::HeliState& b = st.heli.body;
    if (how == Taking::steal && k.took < 0) {
      for (const fs::ArmyHeli& m : st.army.helis) {
        if (m.mode != fs::ArmyHeliMode::set || k.set >= 0) continue;
        k.set = st.t;
        const fs::Spot d = fs::pilot_door(m);
        at = {d.x, d.y, d.z};
      }
      c.machine = k.set >= 0;
    } else if (how == Taking::steal && let_go < 0) {
      stand = false;
      c.lever = 0.85;
      if (b.y - level->ground_at(b.x, b.z) >= 60) {
        c.machine = true;
        let_go = st.t;
      }
    }
    if (how == Taking::ram && target < 0) {
      for (const fs::ArmyHeli& m : st.army.helis) {
        if (m.mode != fs::ArmyHeliMode::hunt || !m.on || target >= 0) continue;
        target = m.id;
        const ge::core::Vec3 s = fs::heli_seat(b);
        at = {s.x, s.y, s.z};
        st.racers[0].skier = ge::skier::stand_skier(st.world(), at.x, at.z, 0, 0);
        c.machine = true;
        stand = false;
      }
    } else if (how == Taking::ram && k.took < 0) {
      for (const fs::ArmyHeli& m : st.army.helis) {
        if (m.id != target) continue;
        if (!placed) {
          // Boarded on its pad last step: put 20 m off their side, level with their disc.
          placed = true;
          b.x = m.x + lm::cos(m.heading) * 20;
          b.z = m.z - lm::sin(m.heading) * 20;
          b.heading = m.heading;
          b.spool = 1;
          b.grounded = false;
        }
        const double d = hypot(m.x - b.x, m.z - b.z);
        b.y = m.y + fs::army_steal::kHubM - ge::helicopter::spec::rotor::kHub;
        b.vy = 0;
        b.vx = m.vx + (m.x - b.x) / d * 10;
        b.vz = m.vz + (m.z - b.z) / d * 10;
      }
      c.lever = 0.6;
    }
    fs::step(st, std::span<const fs::Controls>(&c, 1));
    for (const fs::GameEvent& e : st.events) {
      if (e.kind == fs::EventKind::heli) {
        const auto ph = static_cast<fs::HeliPhase>(e.gate);
        if (ph == fs::HeliPhase::crash && k.crashed < 0) {
          k.crashed = st.t;
          k.wrecks.push_back({b.x, b.y, b.z});
        }
        if (ph == fs::HeliPhase::restart && k.crashed >= 0 && k.back < 0) k.back = st.t;
      }
      if (e.kind != fs::EventKind::army_heli) continue;
      const auto ch = static_cast<fs::ArmyHeliChange>(e.gate);
      if ((ch == fs::ArmyHeliChange::stolen || ch == fs::ArmyHeliChange::rammed) && k.took < 0) {
        k.took = st.t;
        k.stars = st.wanted.stars;
      }
      if (ch == fs::ArmyHeliChange::down && k.down < 0) k.down = st.t;
    }
    for (const fs::ArmyHeli& m : st.army.helis) {
      if (m.mode == fs::ArmyHeliMode::fall || (m.mode == fs::ArmyHeliMode::wreck && m.t < 3)) {
        k.fall_t.push_back(st.t);
        k.fall_h.push_back(m.y - level->ground_at(m.x, m.z));
      }
      if (m.mode == fs::ArmyHeliMode::wreck && m.t <= fs::kDt) k.wrecks.push_back({m.x, m.y, m.z});
    }
    if (st.steps % static_cast<int64_t>(fs::kHz / 2) == 0) {
      for (const fs::ArmyHeli& m : st.army.helis) {
        if (m.mode == fs::ArmyHeliMode::due) continue;
        const auto id = static_cast<size_t>(m.id);
        if (k.paths.size() <= id) k.paths.resize(id + 1);
        k.paths[id].push_back({m.x, m.y, m.z});
      }
      if (st.heli.army || (how == Taking::ram && k.crashed < 0 && target >= 0)) {
        k.his.push_back({b.x, b.y, b.z});
      }
    }
    if (k.down >= 0 && st.t > k.down + 5 && (how == Taking::ram || k.back >= 0)) break;
  }
  return k;
}

void run_takings(ge::lab::Lab& lab, const std::shared_ptr<const mg::Level>& level, double limit,
                 int px, bool pictures, double seed) {
  ge::lab::LabTable table{"taken and downed",
                          {"scenario", "set s", "taken s", "stars", "his down s", "theirs down s",
                           "fell s", "pad again s"},
                          {}};
  const auto opt = [](double v, int d) { return v >= 0 ? fixed(v, d) : std::string("-"); };
  const double view = 1600;
  for (const Taking how : {Taking::steal, Taking::ram}) {
    const std::string id = how == Taking::steal ? "steal" : "ram";
    const Taken k = run_taking(level, how, limit);
    const double fell = how == Taking::ram && k.down >= 0 ? k.down - k.took : -1;
    table.rows.push_back({id, opt(k.set, 2), opt(k.took, 2), std::to_string(k.stars),
                          opt(k.crashed, 2), opt(k.down, 2), opt(fell, 2), opt(k.back, 2)});
    lab.metric(id + ".taken", k.took, "s");
    lab.metric(id + ".stars", k.stars, "stars");
    lab.metric(id + ".down", k.down, "s");
    const std::string look = "Look at previews/army-flight/" + id + ".png; read ";
    if (k.took < 0 || k.stars < (how == Taking::steal ? 4 : 3)) {
      lab.fail(id + ": no machine " + (how == Taking::steal ? "taken" : "rammed") + " within " +
                   fixed(limit, 0) + " s, or the stars not raised",
               look +
                   "steal_army_heli and ram_army_helis in games/fall_line/sim/army_steal.cpp "
                   "(skill: sim).");
    }
    if ((how == Taking::steal ? k.crashed : k.down) < 0) {
      lab.fail(id + ": " + (how == Taking::steal ? "the machine let go of" : "the rammed machine") +
                   " never came down on the snow",
               look +
                   "step_downed_heli in games/fall_line/sim/army_steal.cpp and the taken "
                   "machine's home in heli_ride.cpp (skill: sim).");
    }
    if (!pictures) continue;
    const double k_px = px / view;
    const double x0 = ge::core::clamp(k.q.x - view / 2, 0.0, level->size - view);
    const double z0 = ge::core::clamp(k.q.z - view / 2, 0.0, level->size - view);
    ge::lab::Picture pic = ge::lab::create_picture(px, px, Rgba{0, 0, 0});
    ge::lab::blit(pic, ge::mountain::labs::draw_map(*level, static_cast<int>(level->size * k_px)),
                  static_cast<int>(-x0 * k_px), static_cast<int>(-z0 * k_px));
    const auto sx = [&](double x) { return (x - x0) * k_px; };
    const auto sz = [&](double z) { return (z - z0) * k_px; };
    for (size_t i = 0; i < k.paths.size(); i++) {
      const Rgba c = kPathColours[i % std::size(kPathColours)];
      for (size_t j = 1; j < k.paths[i].size(); j++) {
        ge::lab::draw_line(pic, sx(k.paths[i][j - 1].x), sz(k.paths[i][j - 1].z),
                           sx(k.paths[i][j].x), sz(k.paths[i][j].z), c, 2);
      }
    }
    for (size_t j = 1; j < k.his.size(); j++) {
      ge::lab::draw_line(pic, sx(k.his[j - 1].x), sz(k.his[j - 1].z), sx(k.his[j].x),
                         sz(k.his[j].z), kMen, 3);
    }
    for (const fs::Spot& w : k.wrecks) {
      ge::lab::draw_ring(pic, sx(w.x), sz(w.z), 7, Rgba{255, 40, 40});
      ge::lab::fill_disc(pic, sx(w.x), sz(w.z), 4, Rgba{255, 40, 40});
    }
    ge::lab::fill_disc(pic, sx(k.q.x), sz(k.q.z), 5, Rgba{255, 0, 255});
    lab.shot({id,
              "SEED " + fixed(seed, 0) + " ARMY HELICOPTER " +
                  (how == Taking::steal ? "TAKEN" : "RAMMED"),
              "The ground round the man hunted (magenta dot), " + fixed(view, 0) +
                  " m across from above, at three stars with the army's guns silent: every army "
                  "machine's path (a colour a machine), his machine's in white, and every wreck "
                  "on the snow, his or theirs (red). " +
                  (how == Taking::steal
                       ? "He takes the first machine set down at its pilot's door, climbs 60 m in "
                         "it and lets go: with no pilot it comes down."
                       : "He flies the free ride's machine into the first machine hunting him: "
                         "theirs falls spinning, his crashes."),
              how == Taking::steal
                  ? "a machine's path ending where it set down near the magenta dot, the white "
                    "path starting there and ending at a red wreck"
                  : "the white path meeting a coloured one, and a red wreck under where they met",
              {{"seed", fixed(seed, 0)}, {"scenario", id}, {"stars", "3"}}},
             pic);
    if (how != Taking::ram || k.fall_t.empty()) continue;
    ge::lab::LineChart lc;
    lc.frame = {"the rammed machine's height over the snow", "seed " + fixed(seed, 0), "t (s)",
                "height over the snow (m)"};
    lc.spikes = false;
    lc.traces.push_back({"rammed machine", k.fall_t, k.fall_h});
    if (k.down >= 0) lc.marks.push_back({k.down, "on the snow"});
    const ge::lab::Chart ch = ge::lab::line_chart(lc);
    lab.note(ge::lab::chart_text(ch, 2));
    lab.shot({"ram-fall",
              "SEED " + fixed(seed, 0) + " THE RAMMED MACHINE'S FALL",
              "The rammed army machine's skid datum's height over the snow under it from the "
              "ram until it lies on the snow, its tail rotor gone, spinning.",
              "a curve that steepens as it falls (gravity's share, the way bled off) and ends "
              "at 0 at the mark",
              {{"seed", fixed(seed, 0)}}},
             ge::lab::to_picture(ch.plot));
  }
  lab.table(std::move(table));
}

void run(ge::lab::Lab& lab) {
  const double seed = lab.arg_num("seed");
  const auto version = static_cast<int>(lab.arg_num("version"));
  const double limit = lab.arg_num("limit");
  const int px = static_cast<int>(lab.arg_num("px"));
  const bool pictures = !lab.arg_bool("no-pictures");
  std::shared_ptr<const mg::Level> level;
  try {
    level = std::make_shared<const mg::Level>(fs::free_map(seed, version));
  } catch (const mg::GenerateError& e) {
    lab.fail("seed " + fixed(seed, 0) + " built no map: " + e.what(),
             "Run `make lab LAB=analyze ARGS=--seeds=" + fixed(seed, 0) +
                 " --refusals` (skill: mountain).");
    return;
  }
  const auto& resort = level->resort;
  if (!resort || resort->lifts.empty()) {
    lab.fail("seed " + fixed(seed, 0) + " v" + fixed(version, 0) + " has no ski area",
             "Pass a --version that builds a ski area (10 or later; skill: mountain).");
    return;
  }
  ge::lab::LabTable table{
      "helicopters",
      {"scenario", "stars", "called", "in s", "circle m", "landed", "ropes", "men out",
       "first out s", "picked", "holding", "lost s", "gone", "echelon"},
      {}};
  const double k = px / level->size;
  const auto opt = [](double v, int d) { return v >= 0 ? fixed(v, d) : std::string("-"); };
  for (const Scene scene : kScenes) {
    const std::string id = scene_name(scene);
    const Hunt h = run_scene(level, scene, limit);
    table.rows.push_back({id, std::to_string(h.stars), std::to_string(h.called), opt(h.arrived, 2),
                          opt(h.circle, 0), std::to_string(h.landed), std::to_string(h.ropes),
                          std::to_string(h.dropped), opt(h.first_out, 2), std::to_string(h.picked),
                          std::to_string(h.holding), opt(h.lost, 2), std::to_string(h.gone),
                          fixed(h.echelon, 2)});
    lab.metric(id + ".called", h.called, "machines");
    lab.metric(id + ".in", h.arrived, "s");
    lab.metric(id + ".circle", h.circle, "m");
    lab.metric(id + ".landed", h.landed, "landings");
    lab.metric(id + ".ropes", h.ropes, "ropes");
    lab.metric(id + ".men-out", h.dropped, "men");
    lab.metric(id + ".picked", h.picked, "men");
    lab.metric(id + ".holding", h.holding, "men");
    lab.metric(id + ".gone", h.gone, "machines");
    lab.metric(id + ".echelon", h.echelon, "share");
    const std::string look = "Look at previews/army-flight/" + id + ".png; read ";
    if ((scene == Scene::open || scene == Scene::leapfrog) && (h.landed == 0 || h.dropped == 0)) {
      lab.fail(id + ": no section set down within " + fixed(limit, 0) + " s",
               look +
                   "ask_landings and settle in games/fall_line/sim/army_heli.cpp and land_spot "
                   "in army_land.cpp (skill: sim).");
    }
    if (scene == Scene::rope && h.ropes == 0) {
      lab.fail("rope: no fast rope in the woods within " + fixed(limit, 0) + " s",
               look + "land_spot in games/fall_line/sim/army_land.cpp (skill: sim).");
    }
    if (scene == Scene::leapfrog && h.picked == 0) {
      lab.fail("leapfrog: the squad left behind was never picked up",
               look +
                   "the leapfrog in ask_landings, games/fall_line/sim/army_heli.cpp (skill: "
                   "sim).");
    }
    if (scene == Scene::hide && (h.lost < 0 || h.gone == 0)) {
      lab.fail("hide: the stars were not lost, or no machine flew home over the edge",
               look +
                   "hunt_want's search and home_want in games/fall_line/sim/army_heli.cpp "
                   "(skill: sim).");
    }
    if (scene == Scene::fast && h.echelon < 0.5) {
      lab.fail("fast: the machine chose the echelon on only " + fixed(h.echelon * 100, 0) +
                   "% of its station decisions",
               look + "hunt_want in games/fall_line/sim/army_heli.cpp (skill: sim).");
    }
    if (!pictures) continue;
    ge::lab::Picture pic = ge::mountain::labs::draw_map(*level, px);
    for (size_t i = 0; i < h.paths.size(); i++) {
      const auto& path = h.paths[i];
      const Rgba c = kPathColours[i % std::size(kPathColours)];
      for (size_t j = 1; j < path.size(); j++) {
        ge::lab::draw_line(pic, path[j - 1].x * k, path[j - 1].z * k, path[j].x * k, path[j].z * k,
                           c, 2);
      }
    }
    for (const fs::Spot& s : h.sets) ge::lab::fill_disc(pic, s.x * k, s.z * k, 4, Rgba{0, 0, 0});
    for (const fs::Spot& s : h.men) ge::lab::fill_disc(pic, s.x * k, s.z * k, 2, kMen);
    ge::lab::fill_disc(pic, h.q.x * k, h.q.z * k, 5, Rgba{255, 0, 255});
    if (scene == Scene::leapfrog)
      ge::lab::fill_disc(pic, h.q2.x * k, h.q2.z * k, 5, Rgba{255, 0, 255});
    lab.shot(
        {id,
         "SEED " + fixed(seed, 0) + " ARMY HELICOPTERS: " + id,
         "The map from above (+x right, +z down; the runs red, trees dark green) with the man "
         "hunted (magenta dot; the leapfrog's second spot a second one) and every army "
         "helicopter's path, a colour a machine, from the map's edge. Black dots where a "
         "machine set down or hovered for the fast rope; white dots the soldiers it put down, "
         "where they were at the end. " +
             std::to_string(h.called) + " called, " + std::to_string(h.landed) + " landed, " +
             std::to_string(h.dropped) + " men out.",
         scene == Scene::hide
             ? "a path in from the edge to a wide circle round the magenta dot, then out over "
               "the edge again; no black dots"
         : scene == Scene::fast
             ? "a path that follows the magenta dot's line back and forth, off to one side of "
               "it; no black dots"
             : "a path in from the edge to a ring round the magenta dot, a black dot beside it "
               "and white dots between it and him",
         {{"seed", fixed(seed, 0)},
          {"version", lab.arg("version")},
          {"scenario", id},
          {"stars", std::to_string(h.stars)},
          {"limit s", fixed(limit, 0)}}},
        pic);
  }
  lab.table(std::move(table));
  run_takings(lab, level, limit, px, pictures, seed);
}

const ge::lab::LabSpec kSpec{
    "army-flight",
    ge::lab::LabKind::scenario,
    "game:fall_line",
    "games/fall_line/sim/army_heli (the army's helicopters: the call, the pylon turn, the echelon, "
    "the search, the landings and the fast rope, the leapfrog, home)",
    "sim",
    "On a real ski area, do the army's helicopters come in, circle a man where he is seen, put a "
    "section down near him (on the snow or down a fast rope), pick a squad left behind up and put "
    "it down again, search where he was last seen, and fly home when the hunt is over? Can he "
    "take one set down, and does one rammed by his machine fall to the snow?",
    {
        {"seed", "38", "the map's seed"},
        {"version", "10", "the generator's version (a ski area: 10 or later)"},
        {"limit", "300", "each scenario's time limit, s"},
        {"px", "768", "the pictures' size, px"},
        {"no-pictures", "", "skip the pictures", true},
    },
    "--px=256 --limit=240",
    run,
};
GE_LAB(kSpec);

}  // namespace
