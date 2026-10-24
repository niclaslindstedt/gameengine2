// The village's streets (plugins/mountain/village): a ski area lays a loop of streets below its
// hub with a road out off the map's edge; every street keeps off the runs and no street climbs
// steeper than a street is built; the junctions join what they say they join; the ground under it
// reads road on a carriageway and street to the back of the sidewalks; the trees felled are the
// village's; a pad named keeps the streets off it; a map with no ski area has no village; the plan
// of each reference seed is golden.
//
// ge:summary Tests: the village's street plan, its ground mask and felled trees, and its golden
// ge:keywords test village streets loop road-out ground felled pads golden
// ge:skill mountain testing
// ge:test tests/mountain_village_test.cpp
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "check.hpp"
#include "core/math.hpp"
#include "golden.hpp"
#include "mountain/mapgen/generate.hpp"
#include "mountain/mapgen/query.hpp"
#include "mountain/village/ground.hpp"
#include "mountain/village/streets.hpp"

namespace mg = ge::mountain::mapgen;
namespace vg = ge::mountain::village;

namespace {

constexpr const char* kGolden = "tests/golden/mountain_village.txt";
constexpr int kVersion = 10;

bool env_set(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && *v != '\0' && std::string(v) != "0";
}

// The ski area each seed builds, built once.
const mg::Level& map_of(double seed) {
  static std::map<double, std::unique_ptr<mg::Level>> built;
  auto& slot = built[seed];
  if (!slot) {
    mg::GenerateOptions opts;
    opts.version = kVersion;
    slot = std::make_unique<mg::Level>(mg::generate_level(seed, opts));
  }
  return *slot;
}

const vg::Streets& plan_of(double seed) {
  static std::map<double, vg::Streets> planned;
  auto it = planned.find(seed);
  if (it == planned.end()) {
    const std::optional<vg::Streets> p = vg::plan_streets(map_of(seed));
    CHECK_MSG(p.has_value(), "seed " + std::to_string(seed) + " laid no village");
    it = planned.emplace(seed, p.value_or(vg::Streets{})).first;
  }
  return it->second;
}

std::string hex(uint64_t u) {
  char b[24];
  std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(u));
  return b;
}

}  // namespace

TEST(seed_38_lays_a_loop_with_a_road_out_off_the_map) {
  const mg::Level& level = map_of(38);
  const vg::Streets& plan = plan_of(38);
  CHECK(!plan.main.empty() && !plan.back.empty());
  CHECK(plan.end_cross[0] >= 0 && plan.end_cross[1] >= 0);
  int roads = 0;
  for (const vg::Street& st : plan.streets) {
    if (st.kind != vg::StreetKind::road) continue;
    roads++;
    CHECK(st.exit);
    const vg::StreetPoint& last = st.points.back();
    const double edge =
        std::min(std::min(last.x, level.size - last.x), std::min(last.z, level.size - last.z));
    CHECK_MSG(edge < 1, st.id + " ends " + std::to_string(edge) + " m in from the map's edge");
  }
  CHECK(roads >= 1);
  CHECK(plan.areas.size() >= 1 && plan.areas[0].kind == vg::AreaKind::square);
}

TEST(every_street_keeps_off_the_runs_and_climbs_like_a_street) {
  for (const double seed : {38.0, 7.0, 101.0}) {
    const mg::Level& level = map_of(seed);
    const vg::Streets& plan = plan_of(seed);
    std::vector<mg::Track> runs;
    CHECK(level.resort.has_value());
    if (!level.resort) continue;
    for (const mg::Run& r : level.resort->runs) runs.push_back({r.points, r.length, nullptr});
    for (const vg::Street& st : plan.streets) {
      const double reach = std::max(vg::side_reach(st.section, 0), vg::side_reach(st.section, 1));
      for (size_t i = 0; i < st.points.size(); i++) {
        const vg::StreetPoint& p = st.points[i];
        for (const mg::Track& t : runs) {
          if (t.points.size() < 2) continue;
          const mg::TrackHit hit = mg::nearest_track_point(t, p.x, p.z);
          const double gap = hit.distance - t.points[size_t(hit.index)].width / 2 - reach;
          CHECK_MSG(gap > vg::streets::kClearLine - 1,
                    st.id + " comes " + std::to_string(gap) + " m from a run's edge");
        }
        if (i > 0 && st.kind != vg::StreetKind::aisle) {
          const vg::StreetPoint& a = st.points[i - 1];
          CHECK(std::fabs(p.y - a.y) <= vg::streets::kGradeAlong * (p.s - a.s) + 1e-9);
        }
      }
    }
  }
}

TEST(every_junction_joins_the_streets_that_start_or_end_there) {
  const vg::Streets& plan = plan_of(38);
  for (size_t k = 0; k < plan.streets.size(); k++) {
    const vg::Street& st = plan.streets[k];
    const vg::Junction& a = plan.junctions[size_t(st.from)];
    const vg::Junction& b = plan.junctions[size_t(st.to)];
    CHECK(ge::core::hypot(a.x - st.points.front().x, a.z - st.points.front().z) < 0.01);
    CHECK(ge::core::hypot(b.x - st.points.back().x, b.z - st.points.back().z) < 0.01);
    CHECK(std::count(a.streets.begin(), a.streets.end(), int32_t(k)) == 1);
    CHECK(std::count(b.streets.begin(), b.streets.end(), int32_t(k)) == 1);
  }
}

TEST(the_ground_reads_road_on_the_carriageway_and_street_to_the_back_of_the_walk) {
  const vg::Streets& plan = plan_of(38);
  const vg::GroundMask mask = vg::bake_ground(plan);
  const vg::Street& main = plan.streets[size_t(plan.main.front())];
  const vg::StreetPoint mid = vg::street_at(main, main.length / 2);
  CHECK(vg::ground_at(mask, mid.x, mid.z) == vg::Ground::road);
  const double reach = vg::side_reach(main.section, 1);
  const mg::PlanPoint walk = vg::beside_point(mid, reach - 0.6);
  CHECK(vg::ground_at(mask, walk.x, walk.z) == vg::Ground::street);
  const mg::PlanPoint margin = vg::beside_point(mid, reach + 1.5);
  const vg::Ground g = vg::ground_at(mask, margin.x, margin.z);
  CHECK(g == vg::Ground::felled || g == vg::Ground::street);
  CHECK(vg::ground_at(mask, -1000, -1000) == vg::Ground::none);
  CHECK(vg::on_road(mask, mid.x, mid.z) && vg::on_street(mask, walk.x, walk.z));
}

TEST(the_trees_felled_stand_on_the_villages_ground) {
  const mg::Level& level = map_of(38);
  const vg::GroundMask mask = vg::bake_ground(plan_of(38));
  const std::vector<uint8_t> felled = vg::felled_trees(level, mask);
  CHECK(felled.size() == level.trees.size());
  int n = 0;
  for (size_t i = 0; i < felled.size(); i++) {
    if (!felled[i]) continue;
    n++;
    CHECK(vg::ground_at(mask, level.trees[i].x, level.trees[i].z) != vg::Ground::none);
  }
  CHECK(n > 0);
}

TEST(a_pad_named_keeps_every_street_off_it) {
  const mg::Level& level = map_of(38);
  const vg::Streets& free = plan_of(38);
  const vg::Street& main = free.streets[size_t(free.main.front())];
  const vg::StreetPoint at = vg::street_at(main, main.length / 2);
  const mg::PlanPoint pad{at.x, at.z};
  const std::optional<vg::Streets> plan = vg::plan_streets(level, std::span(&pad, 1));
  CHECK(plan.has_value());
  if (!plan) return;
  for (const vg::Street& st : plan->streets) {
    for (const vg::StreetPoint& p : st.points) {
      CHECK(ge::core::hypot(p.x - pad.x, p.z - pad.z) >
            vg::streets::kClearPad -
                std::max(vg::side_reach(st.section, 0), vg::side_reach(st.section, 1)) - 1e-6);
    }
  }
}

TEST(a_map_with_no_ski_area_has_no_village) {
  mg::GenerateOptions opts;
  opts.version = 1;
  CHECK(!vg::plan_streets(mg::generate_level(38, opts)).has_value());
}

TEST(the_plan_is_a_pure_function_of_the_map) {
  const std::optional<vg::Streets> again = vg::plan_streets(map_of(38));
  CHECK(again.has_value());
  if (again) {
    CHECK(vg::streets_hash(*again, 0xcbf29ce484222325ull) ==
          vg::streets_hash(plan_of(38), 0xcbf29ce484222325ull));
  }
}

TEST(every_seed_keeps_its_golden) {
  std::vector<std::string> got;
  for (const double seed : {38.0, 7.0, 101.0}) {
    const vg::Streets& plan = plan_of(seed);
    const vg::GroundMask mask = vg::bake_ground(plan);
    int felled = 0;
    for (const uint8_t f : vg::felled_trees(map_of(seed), mask)) felled += f;
    got.push_back(std::to_string(static_cast<long long>(seed)) + " " +
                  hex(vg::streets_hash(plan, 0xcbf29ce484222325ull)) + " " +
                  std::to_string(plan.streets.size()) + " " + std::to_string(felled));
  }
  if (env_set("GE_GOLDEN_WRITE")) {
    std::ofstream out(ge::test::root_path(kGolden));
    out << "# each seed's village at generator v10: the seed, the plan's hash, its streets and "
           "the trees it fells (mountain_village_test)\n";
    for (const std::string& row : got) out << row << "\n";
    return;
  }
  const auto want = ge::test::golden_words(kGolden);
  CHECK(want.size() == got.size());
  for (size_t r = 0; r < got.size() && r < want.size(); r++) {
    std::string line;
    for (const auto& w : want[r]) line += (line.empty() ? "" : " ") + w;
    CHECK_MSG(line == got[r], "a village moved:\n  got  " + got[r] + "\n  want " + line +
                                  "\nIf the move is meant, rewrite " + kGolden +
                                  " with GE_GOLDEN_WRITE=1 and put the village lab's pictures "
                                  "before and after in the PR (skill: mountain).");
  }
}
