// FALL LINE'S ARMY PARAMOTORS, HELD (games/fall_line/sim/army_para.hpp): none called under four
// stars; at four on seed 38 the roster's two rigs come in aloft, find him and hold him in sight
// from their circle, none of them flown into the snow or a crown; their riflemen fire under the
// rules of fire; the rigs go home over the edge once the hunt is over; and their flight on seed
// 38, held bit for bit by tests/golden/fall_line_army_para.txt.
//
// A deliberate change to the paramotors moves the golden: rewrite it with
// `GE_GOLDEN_WRITE=1 build/release/tests/fall_line_army_para_test`, and put the `army-flight`
// lab's paramotor tables before and after in the PR.
//
// ge:summary Test: the army's paramotors: the call, holding him in sight, the rifle, home; golden
// ge:keywords test army-para paramotor rifleman circle hold-in-sight lost golden
// ge:skill sim testing
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "check.hpp"
#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "fall_line/sim/army.hpp"
#include "fall_line/sim/army_para.hpp"
#include "fall_line/sim/army_para_rules.hpp"
#include "fall_line/sim/free.hpp"
#include "fall_line/sim/game.hpp"
#include "fall_line/sim/gunfire.hpp"
#include "fall_line/sim/wanted.hpp"
#include "golden.hpp"
#include "skier/body.hpp"
#include "skier/skier.hpp"

namespace {

namespace fs = fall_line::sim;
namespace mg = ge::mountain::mapgen;
namespace sk = ge::skier;
namespace ap = fall_line::sim::army_para;
namespace lm = ge::core::libm;
constexpr const char* kGolden = "tests/golden/fall_line_army_para.txt";

bool env_set(const char* name) {
#ifdef _MSC_VER
  char* v = nullptr;
  size_t n = 0;
  const bool set = _dupenv_s(&v, &n, name) == 0 && v != nullptr;
  std::free(v);
  return set;
#else
  return std::getenv(name) != nullptr;
#endif
}

std::string hex(uint64_t u) {
  char b[24];
  std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(u));
  return b;
}

const std::shared_ptr<const mg::Level>& seed38() {
  static const auto level = std::make_shared<const mg::Level>(fs::free_map(38, 10));
  return level;
}

// A HUNT ON SEED 38 at `stars` for `limit` s: he stands on open snow up the mountain from the
// village (healed each step, so the hunt runs its length), only the paramotors sent unless
// `others`, their rifles off unless `fire`. The hunt is ended at `stop` s (never if negative).
struct Hunt {
  int32_t called = 0;
  int32_t lost = 0;
  double arrived = -1;            // the first rig in, s
  double near = 0;                // s of the last `hold` s with a rig within 1.5 circles of him
  double low = 1e9;               // the least height of any rig's pilot over the snow, m
  int32_t rounds = 0;             // the paramotors' rifles' rounds
  bool home = false;              // every rig gone once the hunt was over
  std::vector<std::string> rows;  // each 10 s: the digest and every rig's mode and place
};

Hunt hunt(int32_t stars, double limit, bool others, bool fire, double stop, double hold) {
  fs::GameState st = fs::create_free_ride(seed38(), 0);
  st.army.fire = fire;
  if (!others) {
    st.army.send_helis = false;
  }
  const double x = st.army.pad_x + lm::sin(st.army.pad_heading) * 400;
  const double z = st.army.pad_z + lm::cos(st.army.pad_heading) * 400;
  st.racers[0].skier = sk::stand_skier(st.world(), x, z, 0, 0);
  fs::set_wanted(st, stars);
  const fs::Controls none{};
  Hunt h;
  int32_t fired[ap::kMostParas] = {};
  for (int64_t i = 0; i < std::llround(limit * fs::kHz); i++) {
    fs::WantedState& w = st.wanted;
    if (stop >= 0 && st.t >= stop && w.ended == fs::WantedEnd::running) {
      w.ended = fs::WantedEnd::escaped;
    }
    if (w.ended == fs::WantedEnd::running && w.stars > 0) w.heat = stars;
    st.racers[0].skier = sk::stand_skier(st.world(), x, z, 0, 0);
    st.racers[0].body = sk::BodyState{};
    fs::step(st, std::span<const fs::Controls>(&none, 1));
    bool close = false;
    for (const fs::ArmyPara& p : st.army.paras) {
      if (p.mode != fs::ArmyParaMode::hunt) continue;
      if (h.arrived < 0) h.arrived = st.t;
      const double off = ge::core::hypot(p.pilot.x - x, p.pilot.z - z);
      close = close || off < 1.5 * ap::kOrbitM;
      const double agl = p.pilot.y - st.level->ground_at(p.pilot.x, p.pilot.z);
      h.low = agl < h.low ? agl : h.low;
    }
    if (close && st.t > limit - hold) h.near += fs::kDt;
    for (const fs::Gun& g : st.gunfire.guns) {
      const int32_t k = g.id - ap::kGunBase;
      if (k < 0 || k >= static_cast<int32_t>(ap::kMostParas)) continue;
      fired[static_cast<size_t>(k)] = g.fired;
    }
    h.rounds = fired[0] + fired[1] + fired[2] + fired[3];
    if (stop >= 0 && st.t > stop && st.army.paras.empty()) h.home = true;
    if (st.steps % static_cast<int64_t>(10 * fs::kHz) == 0) {
      std::string row =
          hex(fs::army_paras_hash(st.army, 0xcbf29ce484222325ull)) + " " + std::to_string(h.rounds);
      for (const fs::ArmyPara& p : st.army.paras) {
        row += " " + std::string(fs::army_para_mode_name(p.mode)) + ":" +
               std::to_string(std::llround(p.pilot.x)) + "," +
               std::to_string(std::llround(p.pilot.y)) + "," +
               std::to_string(std::llround(p.pilot.z));
      }
      h.rows.push_back(row);
    }
  }
  h.called = st.army.paras_called;
  h.lost = st.army.paras_lost;
  return h;
}

TEST(no_paramotor_is_called_under_four_stars) {
  const Hunt h = hunt(3, 60, true, false, -1, 0);
  CHECK_MSG(h.called == 0, std::to_string(h.called) + " paramotors called at three stars");
}

TEST(at_four_stars_the_rigs_find_him_and_hold_him_in_sight) {
  const Hunt h = hunt(4, 240, false, false, -1, 120);
  CHECK_MSG(h.called == 2, std::to_string(h.called) + " paramotors called at four stars, want 2");
  CHECK_MSG(h.arrived > 0, "no paramotor reached the hunt in 240 s");
  CHECK_MSG(h.near > 100, "a rig was within 1.5 circles of him only " + std::to_string(h.near) +
                              " s of the last 120 s");
  CHECK_MSG(h.lost == 0, std::to_string(h.lost) + " paramotors flown into the snow or a crown");
  CHECK_MSG(h.low > 20, "a rig's pilot came down to " + std::to_string(h.low) +
                            " m over the snow while hunting");
}

TEST(their_riflemen_fire_on_him_under_the_rules_of_fire) {
  const Hunt h = hunt(4, 180, false, true, -1, 0);
  CHECK_MSG(h.rounds > 0, "the riflemen fired no round in 180 s at four stars");
  CHECK_MSG(h.lost == 0, std::to_string(h.lost) + " paramotors lost with their rifles on");
  const Hunt quiet = hunt(4, 180, false, false, -1, 0);
  CHECK_MSG(quiet.rounds == 0, "a rifle fired with the army's fire off");
}

TEST(once_the_hunt_is_over_the_rigs_go_home) {
  const Hunt h = hunt(4, 420, false, false, 150, 0);
  CHECK_MSG(h.called == 2, std::to_string(h.called) + " paramotors called, want 2");
  CHECK_MSG(h.home, "a rig was still out 270 s after the hunt ended");
  CHECK_MSG(h.lost == 0, std::to_string(h.lost) + " paramotors lost on the way home");
}

TEST(the_rigs_on_seed_38_keep_their_golden) {
  // Four stars for 240 s, the rifles on, only the paramotors sent; then the five-star roster's
  // four rigs for 180 s. Each 10 s: the rigs' digest, the rounds so far, every rig's mode and
  // place.
  const Hunt four = hunt(4, 240, false, true, -1, 0);
  const Hunt five = hunt(5, 180, false, true, -1, 0);
  CHECK_MSG(five.called == 4, std::to_string(five.called) + " paramotors called at five stars");
  CHECK_MSG(five.lost == 0, std::to_string(five.lost) + " paramotors lost at five stars");
  std::vector<std::string> got;
  got.reserve(four.rows.size() + five.rows.size());
  for (const auto& r : four.rows) got.push_back("4 " + r);
  for (const auto& r : five.rows) got.push_back("5 " + r);
  if (env_set("GE_GOLDEN_WRITE")) {
    std::ofstream out(ge::test::root_path(kGolden));
    out << "# each 10 s: stars, the rigs' digest, rifle rounds so far, every rig mode:x,y,z "
           "(fall_line_army_para_test, seed 38)\n";
    for (const std::string& row : got) out << row << "\n";
    return;
  }
  const auto want = ge::test::golden_words(kGolden);
  CHECK(want.size() == got.size());
  for (size_t r = 0; r < got.size() && r < want.size(); r++) {
    std::string line;
    for (const auto& w : want[r]) line += (line.empty() ? "" : " ") + w;
    CHECK_MSG(line == got[r], "the army's paramotors on seed 38 moved at row " + std::to_string(r) +
                                  ":\n  got  " + got[r] + "\n  want " + line +
                                  "\nIf the move is meant, rewrite " + kGolden +
                                  " with GE_GOLDEN_WRITE=1 and put the army-flight lab's "
                                  "paramotor tables before and after in the PR.");
  }
}

}  // namespace
