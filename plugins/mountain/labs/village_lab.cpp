// LAB `village`: the village's streets laid on a seed's ski area, and the ground under them. Each
// seed's map is built at the free ride's generator version and its street plan laid
// (`plan_streets`); the picture is the village from above (the face, the woods, the runs and the
// lifts as every map lab draws them), the ground mask over it (the carriageways dark, the rest of
// each street to the back of its sidewalks pale, the felled margin tan, the gardens faint), every
// street's centreline in its kind's colour with its id, the junctions, the square and the car
// park, and every tree felled for it crossed out. The table lists every street; the metrics say
// whether a village was laid, its loop's length, its roads out, the trees it fells and how near
// any street's reach comes to a run's edge, a lift's line or a station (none may come inside its
// clearance). A seed whose village has no room says so and is drawn without one.
//
// ge:summary Lab: the village's streets laid on a seed's ski area and the ground under them, drawn
// ge:keywords lab village streets road loop square car-park ground felled clearance
// ge:skill mountain labs
// ge:test tests/lab_runner_test.cpp
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "mountain/labs/level_map.hpp"
#include "mountain/mapgen/hub.hpp"
#include "mountain/mapgen/query.hpp"
#include "mountain/mapgen/versions.hpp"
#include "mountain/village/ground.hpp"
#include "mountain/village/streets.hpp"

namespace {

namespace mg = ge::mountain::mapgen;
namespace vg = ge::mountain::village;
using ge::core::hypot;
using ge::lab::Rgba;

std::string f1(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.1f", v);
  return b;
}

Rgba kind_colour(vg::StreetKind k) {
  switch (k) {
    case vg::StreetKind::main:
      return {230, 60, 40};
    case vg::StreetKind::back:
      return {240, 170, 30};
    case vg::StreetKind::cross:
      return {250, 230, 80};
    case vg::StreetKind::road:
      return {60, 160, 255};
    case vg::StreetKind::aisle:
      break;
  }
  return {200, 120, 220};
}

Rgba ground_colour(vg::Ground g) {
  switch (g) {
    case vg::Ground::road:
      return {55, 55, 60, 230};
    case vg::Ground::street:
      return {190, 190, 185, 220};
    case vg::Ground::felled:
      return {190, 150, 100, 150};
    case vg::Ground::garden:
      return {120, 200, 120, 60};
    case vg::Ground::none:
      break;
  }
  return {0, 0, 0, 0};
}

// How near any street's reach comes to a run's edge, a lift's line and a station, m (negative:
// inside it).
struct Nearest {
  double run = 1e9;
  double station = 1e9;
};

Nearest nearest_of(const mg::Level& level, const vg::Streets& plan) {
  Nearest out;
  std::vector<mg::Track> runs;
  for (const mg::Run& r : level.resort->runs) runs.push_back({r.points, r.length, nullptr});
  for (const vg::Street& st : plan.streets) {
    const double reach = std::max(vg::side_reach(st.section, 0), vg::side_reach(st.section, 1));
    for (const vg::StreetPoint& p : st.points) {
      for (const mg::Track& t : runs) {
        if (t.points.size() < 2) continue;
        const mg::TrackHit hit = mg::nearest_track_point(t, p.x, p.z);
        out.run = std::min(out.run, hit.distance - t.points[size_t(hit.index)].width / 2 - reach);
      }
      for (const mg::Lift& l : level.resort->lifts) {
        for (const mg::Vec3& e : {l.bottom, l.top}) {
          out.station = std::min(out.station, hypot(e.x - p.x, e.z - p.z) - reach);
        }
      }
    }
  }
  return out;
}

ge::lab::Picture draw_village(const mg::Level& level, const vg::Streets& plan,
                              const vg::GroundMask& mask, const std::vector<uint8_t>& felled,
                              int px, double metres) {
  const ge::mountain::picture::MapWindow win{plan.centre_x, plan.centre_z + plan.valley * 40,
                                             metres};
  ge::lab::Picture pic = ge::mountain::labs::draw_map(level, px, win);
  const double k = px / metres;
  const double x0 = win.x - metres / 2;
  const double z0 = win.z - metres / 2;
  const auto sx = [&](double x) { return (x - x0) * k; };
  const auto sz = [&](double z) { return (z - z0) * k; };
  // The ground, a pixel at a time.
  for (int j = 0; j < px; j++) {
    for (int i = 0; i < px; i++) {
      const vg::Ground g = vg::ground_at(mask, x0 + (i + 0.5) / k, z0 + (j + 0.5) / k);
      if (g != vg::Ground::none) ge::lab::set_pixel(pic, i, j, ground_colour(g));
    }
  }
  for (size_t i = 0; i < level.trees.size(); i++) {
    if (!felled[i]) continue;
    const double x = sx(level.trees[i].x);
    const double z = sz(level.trees[i].z);
    if (x < 0 || z < 0 || x >= px || z >= px) continue;
    ge::lab::draw_line(pic, x - 2, z - 2, x + 2, z + 2, {200, 30, 30}, 1);
    ge::lab::draw_line(pic, x - 2, z + 2, x + 2, z - 2, {200, 30, 30}, 1);
  }
  for (const vg::VillageArea& a : plan.areas) {
    const double fx = ge::core::libm::sin(a.heading);
    const double fz = ge::core::libm::cos(a.heading);
    const auto corner = [&](double lx, double lz) {
      return std::pair{sx(a.x + lx * fz + lz * fx), sz(a.z - lx * fx + lz * fz)};
    };
    const std::pair<double, double> c[4] = {corner(-a.half, -a.depth), corner(a.half, -a.depth),
                                            corner(a.half, a.depth), corner(-a.half, a.depth)};
    const Rgba col = a.kind == vg::AreaKind::square ? Rgba{255, 255, 255} : Rgba{200, 120, 220};
    for (int n = 0; n < 4; n++) {
      ge::lab::draw_line(pic, c[n].first, c[n].second, c[(n + 1) % 4].first, c[(n + 1) % 4].second,
                         col, 1);
    }
    const std::string word = a.kind == vg::AreaKind::square ? "SQUARE" : "CAR PARK";
    ge::lab::draw_text(pic, sx(a.x) - ge::lab::text_width(word) / 2.0, sz(a.z) - 3, word, col);
  }
  for (const vg::Street& st : plan.streets) {
    const Rgba c = kind_colour(st.kind);
    for (size_t n = 1; n < st.points.size(); n++) {
      ge::lab::draw_line(pic, sx(st.points[n - 1].x), sz(st.points[n - 1].z), sx(st.points[n].x),
                         sz(st.points[n].z), c, 2);
    }
  }
  for (const vg::Junction& j : plan.junctions) {
    ge::lab::fill_disc(pic, sx(j.x), sz(j.z), 3, j.exit ? Rgba{60, 160, 255} : Rgba{20, 20, 20});
  }
  for (const vg::Street& st : plan.streets) {
    if (st.kind == vg::StreetKind::aisle) continue;
    const vg::StreetPoint m = vg::street_at(st, st.length / 2);
    const double w = ge::lab::text_width(st.id);
    ge::lab::fill_rect(pic, sx(m.x) - w / 2 - 2, sz(m.z) - 5, w + 4, ge::lab::kGlyphH + 3,
                       {0, 0, 0, 170});
    ge::lab::draw_text(pic, sx(m.x) - w / 2, sz(m.z) - 3, st.id, kind_colour(st.kind));
  }
  return pic;
}

void run(ge::lab::Lab& lab) {
  const int px = static_cast<int>(lab.arg_num("px"));
  const double metres = lab.arg_num("metres");
  const auto version = static_cast<int>(lab.arg_num("version"));
  int laid = 0;
  int seeds = 0;
  ge::lab::LabTable all{"every seed: the village laid or not",
                        {"seed", "streets", "loop m", "roads out", "crosses", "car park", "felled",
                         "nearest run m", "nearest station m"},
                        {}};
  for (const double seed : lab.arg_nums("seeds")) {
    seeds++;
    const std::string seed_word = std::to_string(static_cast<long long>(seed));
    const std::string id = "seed-" + seed_word;
    const auto level = ge::mountain::labs::build_map(lab, seed, version);
    if (!level) continue;
    const auto t0 = std::chrono::steady_clock::now();
    const std::optional<vg::Streets> plan = vg::plan_streets(*level);
    const auto t1 = std::chrono::steady_clock::now();
    lab.metric(id + ".plan-ms", std::chrono::duration<double, std::milli>(t1 - t0).count(), "ms",
               ge::lab::Det::measured);
    lab.metric(id + ".laid", plan ? 1 : 0, "bool");
    if (!plan) {
      lab.note(id + ": no loop of streets fits near the village (it has no streets)");
      all.rows.push_back({seed_word, "0", "-", "-", "-", "-", "-", "-", "-"});
      continue;
    }
    laid++;
    const vg::GroundMask mask = vg::bake_ground(*plan);
    const std::vector<uint8_t> felled = vg::felled_trees(*level, mask);
    int n_felled = 0;
    for (const uint8_t f : felled) n_felled += f;
    double loop = 0;
    int roads = 0;
    int crosses = 0;
    ge::lab::LabTable table{
        id + ": the streets", {"id", "kind", "length m", "from", "to", "steepest %", "exit"}, {}};
    for (const vg::Street& st : plan->streets) {
      if (st.kind == vg::StreetKind::main || st.kind == vg::StreetKind::back) loop += st.length;
      if (st.kind == vg::StreetKind::road) roads++;
      if (st.kind == vg::StreetKind::cross) crosses++;
      double steep = 0;
      for (size_t n = 1; n < st.points.size(); n++) {
        const double run = st.points[n].s - st.points[n - 1].s;
        if (run > 0) steep = std::max(steep, std::fabs(st.points[n].y - st.points[n - 1].y) / run);
      }
      table.rows.push_back({st.id, vg::street_kind_name(st.kind), f1(st.length),
                            std::to_string(st.from), std::to_string(st.to), f1(100 * steep),
                            st.exit ? "yes" : ""});
    }
    lab.table(std::move(table));
    const Nearest near = nearest_of(*level, *plan);
    lab.metric(id + ".streets", double(plan->streets.size()), "count");
    lab.metric(id + ".loop", loop, "m");
    lab.metric(id + ".roads-out", roads, "count");
    lab.metric(id + ".crosses", crosses, "count");
    lab.metric(id + ".car-park", plan->carpark_side != 0 ? 1 : 0, "bool");
    lab.metric(id + ".felled", n_felled, "count");
    lab.metric(id + ".nearest-run", near.run, "m");
    lab.metric(id + ".nearest-station", near.station, "m");
    lab.metric(id + ".hash", double(vg::streets_hash(*plan, 0xcbf29ce484222325ull) % 1000000007ull),
               "hash");
    if (near.run < vg::streets::kClearLine - 1e-6) {
      lab.fail(
          id + ": a street's reach comes " + f1(near.run) + " m from a run's edge, inside its " +
              f1(vg::streets::kClearLine) + " m clearance",
          "next: check clearance_of in plugins/mountain/village/streets.cpp. Skill: mountain.");
    }
    all.rows.push_back({seed_word, std::to_string(plan->streets.size()), f1(loop),
                        std::to_string(roads), std::to_string(crosses),
                        plan->carpark_side != 0 ? "yes" : "no", std::to_string(n_felled),
                        f1(near.run), f1(near.station)});
    lab.shot({id + "-village", "SEED " + seed_word + " THE VILLAGE'S STREETS",
              "The village from above, " + f1(metres) +
                  " m a side: the map as every map lab draws it, the carriageways dark, the rest "
                  "of each street to the back of its sidewalks pale, the felled margin tan and "
                  "the gardens faint green; the main street red, the back street orange, the "
                  "cross streets yellow, the roads out blue (off the map's edge), the car park's "
                  "aisles violet; the junctions dots, the trees felled crossed out in red.",
              "a loop of four streets below the hub with a road leaving it; no street across a "
              "run, a lift's line or a station; the square below a lift",
              ge::mountain::labs::map_inputs(lab, *level)},
             draw_village(*level, *plan, mask, felled, px, metres));
  }
  lab.table(std::move(all));
  lab.metric("laid", laid, "count");
  lab.metric("seeds", seeds, "count");
}

const ge::lab::LabSpec kSpec{
    "village",
    ge::lab::LabKind::picture,
    "plugin:mountain",
    "plugins/mountain/village/streets.cpp",
    "mountain",
    "Where does a ski area's village lay its streets, and what ground do they take?",
    {
        {"seeds", "38,7,101", "seeds, comma-separated"},
        {"version", "10", "the generator version the map is built at (a ski area: 8 or later)"},
        {"px", "640", "the picture's side, px"},
        {"metres", "640", "the ground the picture covers, m a side"},
        ge::mountain::labs::kRegionFlag,
    },
    "--seeds=38 --px=320",
    run,
};
GE_LAB(kSpec);

}  // namespace
