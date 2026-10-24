// LAB `army-para`: the army's paramotor riflemen on a real ski area. The hunted man is stood on
// open snow up the mountain from the village (stood again and healed every step, so the hunt runs
// its length) while the hunt is pinned at its stars, and the army runs through the real `step`
// with only its paramotors sent (`step_army_paras` after the helicopters): THE PARAMOTORS table
// (per scenario, the rigs called, when the first came in, how far round him they flew their
// circle and how high over the snow, the least height any came down to, the rifles' rounds and
// the rounds into him, the rigs brought down and the rigs gone home) and, per scenario, the
// ground round him from above with every rig's path, and a chart of every rig's height over the
// snow through the hunt.
//
// Scenarios: `four` (four stars, the roster's two rigs, their rifles on), `five` (five stars, the
// roster's four), `home` (four stars, the hunt ended at `--stop` s: every rig flies out to the
// map's edge on its bearing and is gone).
//
// A rig brought down by the snow or a crown, a roster not filled, a rig never in, no round fired
// at four stars, or a rig still out at the end of `home` is a failure (exit 1).
//
// ge:summary Lab: the army's paramotors on seed 38: the call, the circle, the height, the rifle
// ge:keywords lab army paramotor army-para rifleman circle orbit height-over-snow paths home
// ge:keywords scenario lost
// ge:skill sim labs
// ge:test tests/fall_line_army_para_test.cpp
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num_text.hpp"
#include "fall_line/sim/army.hpp"
#include "fall_line/sim/army_para.hpp"
#include "fall_line/sim/army_para_rules.hpp"
#include "fall_line/sim/free.hpp"
#include "fall_line/sim/game.hpp"
#include "fall_line/sim/wanted.hpp"
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
namespace gl = ge::lab;
namespace ap = fall_line::sim::army_para;
namespace lm = ge::core::libm;
using ge::core::hypot;
using gl::Rgba;

std::string fixed(double v, int d) { return ge::core::num::to_fixed(v, d); }

enum class Scene : uint8_t { four, five, home };
constexpr Scene kScenes[] = {Scene::four, Scene::five, Scene::home};

const char* scene_name(Scene s) {
  switch (s) {
    case Scene::four:
      return "four";
    case Scene::five:
      return "five";
    case Scene::home:
      return "home";
  }
  return "?";
}

// He stands this far up the mountain from the army's pad, m. The ground round him drawn this wide,
// m (the whole map for `home`).
constexpr double kUpM = 400;
constexpr double kViewM = 1400;
// How long after the first rig is in before its circle is measured, s.
constexpr double kSettleS = 60;
// The colours the rigs' paths are drawn in, by id.
constexpr Rgba kPathColours[] = {{0, 200, 255}, {255, 140, 0}, {0, 230, 120}, {255, 230, 0}};

struct Rig {
  std::vector<fs::Spot> path;  // each second
  std::vector<double> t, agl;  // each second while flying
};

struct Hunt {
  fs::Spot q;
  int32_t stars = 0;
  int32_t called = 0;
  double arrived = -1;
  // The rigs' mean distance from him while hunting him in sight, from `kSettleS` after the first
  // came in, m, and their mean height over the snow then, m.
  double circle = -1;
  double over = -1;
  double low = -1;     // the least height over the snow any rig came down to while hunting, m
  int32_t rounds = 0;  // the paramotors' rifles' rounds
  int32_t hits = 0;    // rounds into him
  int32_t lost = 0;
  int32_t gone = 0;
  int32_t out = 0;  // rigs still out at the end
  std::vector<Rig> rigs;
};

Hunt run_scene(const std::shared_ptr<const mg::Level>& level, Scene scene, double limit,
               double stop) {
  fs::GameState st = fs::create_free_ride(level, 0);
  st.army.send_helis = false;
  Hunt h;
  h.stars = scene == Scene::five ? 5 : 4;
  const double x = st.army.pad_x + lm::sin(st.army.pad_heading) * kUpM;
  const double z = st.army.pad_z + lm::cos(st.army.pad_heading) * kUpM;
  st.racers[0].skier = ge::skier::stand_skier(st.world(), x, z, 0, 0);
  fs::set_wanted(st, h.stars);
  h.q = fs::quarry_of(st);
  const fs::Controls none{};
  int32_t fired[ap::kMostParas] = {};
  double circled = 0;
  double over = 0;
  int32_t circling = 0;
  while (st.t < limit) {
    fs::WantedState& w = st.wanted;
    if (scene == Scene::home && st.t >= stop && w.ended == fs::WantedEnd::running) {
      w.ended = fs::WantedEnd::escaped;
    }
    if (w.ended == fs::WantedEnd::running && w.stars > 0) w.heat = h.stars;
    st.racers[0].skier = ge::skier::stand_skier(st.world(), x, z, 0, 0);
    st.racers[0].body = ge::skier::BodyState{};
    const int32_t hits = st.gunfire.hits;
    fs::step(st, std::span<const fs::Controls>(&none, 1));
    h.hits += st.gunfire.hits - hits;
    for (const fs::GameEvent& e : st.events) {
      if (e.kind != fs::EventKind::army_para) continue;
      const auto c = static_cast<fs::ArmyParaChange>(e.gate);
      if (c == fs::ArmyParaChange::arrived && h.arrived < 0) h.arrived = st.t;
      h.gone += c == fs::ArmyParaChange::gone ? 1 : 0;
    }
    for (const fs::Gun& g : st.gunfire.guns) {
      const int32_t k = g.id - ap::kGunBase;
      if (k >= 0 && k < static_cast<int32_t>(ap::kMostParas)) fired[k] = g.fired;
    }
    const bool second = st.steps % static_cast<int64_t>(fs::kHz) == 0;
    for (const fs::ArmyPara& p : st.army.paras) {
      if (p.mode == fs::ArmyParaMode::due) continue;
      const double agl = p.pilot.y - level->ground_at(p.pilot.x, p.pilot.z);
      if (p.mode == fs::ArmyParaMode::hunt) {
        h.low = h.low < 0 || agl < h.low ? agl : h.low;
        if (w.seen && h.arrived >= 0 && st.t > h.arrived + kSettleS) {
          circled += hypot(p.pilot.x - x, p.pilot.z - z);
          over += agl;
          circling++;
        }
      }
      if (!second) continue;
      const auto id = static_cast<size_t>(p.id);
      if (h.rigs.size() <= id) h.rigs.resize(id + 1);
      h.rigs[id].path.push_back({p.pilot.x, p.pilot.y, p.pilot.z});
      h.rigs[id].t.push_back(st.t);
      h.rigs[id].agl.push_back(agl);
    }
  }
  for (const int32_t f : fired) h.rounds += f;
  h.called = st.army.paras_called;
  h.lost = st.army.paras_lost;
  h.out = static_cast<int32_t>(st.army.paras.size());
  h.circle = circling > 0 ? circled / circling : -1;
  h.over = circling > 0 ? over / circling : -1;
  return h;
}

// The ground round him from above, `view` m wide, `px` square, with every rig's path.
gl::Picture ground_round(const mg::Level& level, const Hunt& h, double view, int px) {
  const double k = px / view;
  const int full = static_cast<int>(level.size * k);
  const double x0 = ge::core::clamp(h.q.x - view / 2, 0.0, level.size - view);
  const double z0 = ge::core::clamp(h.q.z - view / 2, 0.0, level.size - view);
  gl::Picture pic = gl::create_picture(px, px, Rgba{0, 0, 0});
  gl::blit(pic, ge::mountain::labs::draw_map(level, full), static_cast<int>(-x0 * k),
           static_cast<int>(-z0 * k));
  const auto sx = [&](double x) { return (x - x0) * k; };
  const auto sz = [&](double z) { return (z - z0) * k; };
  gl::draw_ring(pic, sx(h.q.x), sz(h.q.z), ap::kOrbitM * k, Rgba{255, 255, 255});
  for (size_t i = 0; i < h.rigs.size(); i++) {
    const auto& path = h.rigs[i].path;
    const Rgba c = kPathColours[i % std::size(kPathColours)];
    for (size_t j = 1; j < path.size(); j++) {
      gl::draw_line(pic, sx(path[j - 1].x), sz(path[j - 1].z), sx(path[j].x), sz(path[j].z), c, 2);
    }
    if (!path.empty()) gl::fill_disc(pic, sx(path.back().x), sz(path.back().z), 4, c);
  }
  gl::fill_disc(pic, sx(h.q.x), sz(h.q.z), 5, Rgba{255, 0, 255});
  return pic;
}

void run(gl::Lab& lab) {
  const double seed = lab.arg_num("seed");
  const auto version = static_cast<int>(lab.arg_num("version"));
  const double limit = lab.arg_num("limit");
  const double stop = lab.arg_num("stop");
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
  gl::LabTable table{"paramotors",
                     {"scenario", "stars", "called", "in s", "circle m", "over m", "lowest m",
                      "rounds", "hits", "lost", "gone", "out"},
                     {}};
  const auto opt = [](double v, int d) { return v >= 0 ? fixed(v, d) : std::string("-"); };
  for (const Scene scene : kScenes) {
    const std::string id = scene_name(scene);
    const double until = scene == Scene::home ? limit + stop : limit;
    const Hunt h = run_scene(level, scene, until, stop);
    table.rows.push_back({id, std::to_string(h.stars), std::to_string(h.called), opt(h.arrived, 2),
                          opt(h.circle, 0), opt(h.over, 0), opt(h.low, 0), std::to_string(h.rounds),
                          std::to_string(h.hits), std::to_string(h.lost), std::to_string(h.gone),
                          std::to_string(h.out)});
    lab.metric(id + ".called", h.called, "rigs");
    lab.metric(id + ".in", h.arrived, "s");
    lab.metric(id + ".circle", h.circle, "m");
    lab.metric(id + ".over", h.over, "m");
    lab.metric(id + ".lowest", h.low, "m");
    lab.metric(id + ".rounds", h.rounds, "rounds");
    lab.metric(id + ".hits", h.hits, "rounds");
    lab.metric(id + ".lost", h.lost, "rigs");
    lab.metric(id + ".gone", h.gone, "rigs");
    std::string look = "Look at previews/army-para/" + id;
    look += ".png and " + id;
    look += "-height.png; read ";
    const int32_t want = scene == Scene::five ? 4 : 2;
    if (h.called != want || h.arrived < 0) {
      lab.fail(id + ": " + std::to_string(h.called) + " rigs called (want " + std::to_string(want) +
                   "), the first in at " + opt(h.arrived, 1) + " s",
               look + "call and arrive in games/fall_line/sim/army_para.cpp (skill: sim).");
    }
    if (h.lost > 0) {
      lab.fail(id + ": " + std::to_string(h.lost) + " rigs flown into the snow or a crown",
               look +
                   "aim_round and the floor in games/fall_line/sim/army_para.cpp and "
                   "army_para_rules.hpp (skill: sim).");
    }
    if (scene == Scene::four && h.rounds == 0) {
      lab.fail("four: the riflemen fired no round in " + fixed(limit, 0) + " s",
               look + "fire_rifle in games/fall_line/sim/army_para.cpp (skill: sim).");
    }
    if (scene == Scene::home && h.out > 0) {
      lab.fail("home: " + std::to_string(h.out) + " rigs still out " + fixed(limit, 0) +
                   " s after the hunt ended",
               look +
                   "the leave mode in step_army_paras, games/fall_line/sim/army_para.cpp "
                   "(skill: sim).");
    }
    if (!pictures) continue;
    const double view = scene == Scene::home ? level->size : kViewM;
    lab.shot({id,
              "SEED " + fixed(seed, 0) + " ARMY PARAMOTORS: " + id,
              "The ground round the man hunted (magenta dot), " + fixed(view, 0) +
                  " m across from above (+x right, +z down; the runs red, trees dark green), the "
                  "circle the rigs are asked to fly round him (white ring, " +
                  fixed(ap::kOrbitM, 0) +
                  " m) and every rig's path, a colour a rig, its last "
                  "place a dot. " +
                  std::to_string(h.called) + " called, " + std::to_string(h.rounds) +
                  " rounds fired, " + std::to_string(h.lost) + " lost.",
              scene == Scene::home
                  ? "paths that come in to loops round the magenta dot, then run out to the "
                    "map's edge and stop there"
                  : "paths that come in from the edge of the picture and loop round the magenta "
                    "dot near the white ring, never crossing each other at one place for long",
              {{"seed", fixed(seed, 0)},
               {"version", lab.arg("version")},
               {"scenario", id},
               {"stars", std::to_string(h.stars)},
               {"limit s", fixed(until, 0)}}},
             ground_round(*level, h, view, px));
    gl::LineChart lc;
    lc.frame = {"each rig's height over the snow, " + id,
                "seed " + fixed(seed, 0) + ", " + std::to_string(h.stars) + " stars", "t (s)",
                "height over the snow (m)"};
    lc.rules = {ap::kHoldM, ap::kFloorM, ap::kLowM};
    lc.spikes = false;
    for (size_t i = 0; i < h.rigs.size(); i++) {
      lc.traces.push_back({"rig " + std::to_string(i), h.rigs[i].t, h.rigs[i].agl});
    }
    if (scene == Scene::home) lc.marks.push_back({stop, "hunt over"});
    const gl::Chart ch = gl::line_chart(lc);
    lab.note(gl::chart_text(ch, 4));
    lab.shot({id + "-height",
              "SEED " + fixed(seed, 0) + " ARMY PARAMOTORS' HEIGHT: " + id,
              "Each rig's pilot's height over the snow under him, every second from when it "
              "came in; dashed rules at the least height its aim asks for (" +
                  fixed(ap::kHoldM, 0) + " m), the floor under which it turns only gently (" +
                  fixed(ap::kFloorM, 0) +
                  " m) and the height under which it leaves the circle "
                  "for the fall line (" +
                  fixed(ap::kLowM, 0) + " m).",
              "every line starting high and settling near the top rule, dipping under it in "
              "sinking air but never to 0",
              {{"seed", fixed(seed, 0)}, {"scenario", id}, {"stars", std::to_string(h.stars)}}},
             gl::to_picture(ch.plot));
  }
  lab.table(std::move(table));
}

const gl::LabSpec kSpec{
    "army-para",
    gl::LabKind::scenario,
    "game:fall_line",
    "games/fall_line/sim/army_para (the army's paramotor riflemen: the call, the circle, the "
    "height, the rifle, home)",
    "sim",
    "On a real ski area, do the army's paramotors come in at four and five stars, circle a man "
    "they see without flying into the snow or a crown, fire on him, and fly home when the hunt "
    "is over?",
    {
        {"seed", "38", "the map's seed"},
        {"version", "10", "the generator's version (a ski area: 10 or later)"},
        {"limit", "240", "each scenario's time limit (home: after the hunt ends), s"},
        {"stop", "150", "when `home`'s hunt ends, s"},
        {"px", "768", "the pictures' size, px"},
        {"no-pictures", "", "skip the pictures", true},
    },
    "--px=256",
    run,
};
GE_LAB(kSpec);

}  // namespace
