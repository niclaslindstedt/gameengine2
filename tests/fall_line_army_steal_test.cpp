// FALL LINE'S ARMY HELICOPTERS TAKEN AND BROUGHT DOWN, HELD (games/fall_line/sim/army_steal.hpp):
// on seed 38 a machine set down near him is his to take at its pilot's door, the free ride's
// helicopter becomes it with him aboard, the machine leaves the hunt and the theft raises him to
// four stars; dropped off in the air the taken machine has no pilot and comes down, and the free
// ride's own waits on its pad again; his machine flown into a hunting one brings that one down
// spinning onto the snow as a wreck, crashes his, and raises the heat. Both held bit for bit by
// tests/golden/fall_line_army_steal.txt.
//
// A deliberate change moves the golden: rewrite it with
// `GE_GOLDEN_WRITE=1 build/release/tests/fall_line_army_steal_test`, and put the `army-flight`
// lab's `steal` and `ram` rows before and after in the PR.
//
// ge:summary Test: an army helicopter taken off the snow, and one rammed down; golden
// ge:keywords test army-steal steal stolen ram rammed downed wreck pilot-door golden
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
#include "fall_line/sim/army_heli.hpp"
#include "fall_line/sim/army_steal.hpp"
#include "fall_line/sim/army_steal_rules.hpp"
#include "fall_line/sim/free.hpp"
#include "fall_line/sim/game.hpp"
#include "fall_line/sim/heli_ride.hpp"
#include "fall_line/sim/wanted.hpp"
#include "golden.hpp"
#include "helicopter/spec.hpp"
#include "skier/body.hpp"
#include "skier/skier.hpp"

namespace {

namespace fs = fall_line::sim;
namespace mg = ge::mountain::mapgen;
namespace sk = ge::skier;
namespace lm = ge::core::libm;
constexpr const char* kGolden = "tests/golden/fall_line_army_steal.txt";
// The free ride's helicopter's modes, by `HeliMode`.
const std::string kHeliModes[] = {"parked", "flown", "home", "wreck"};

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

// A hunt on seed 38 at `stars`, the army's guns silent, he stood on open snow up the mountain
// from the village, healed each step.
struct Hunt {
  fs::GameState st;
  double x = 0, z = 0;
  int32_t stars = 0;
  std::vector<std::string> rows;  // each 5 s: the digest, the stars, every machine's mode
};

Hunt start(int32_t stars) {
  Hunt h;
  h.st = fs::create_free_ride(seed38(), 0);
  h.st.army.fire = false;
  h.stars = stars;
  h.x = h.st.army.pad_x + lm::sin(h.st.army.pad_heading) * 400;
  h.z = h.st.army.pad_z + lm::cos(h.st.army.pad_heading) * 400;
  h.st.racers[0].skier = sk::stand_skier(h.st.world(), h.x, h.z, 0, 0);
  fs::set_wanted(h.st, stars);
  return h;
}

// One step: the heat pinned at least at the stars asked; with `stand`, he is stood where he is
// kept; `c` his controls.
void tick(Hunt& h, bool stand, const fs::Controls& c) {
  fs::WantedState& w = h.st.wanted;
  if (w.ended == fs::WantedEnd::running && w.heat < h.stars) w.heat = h.stars;
  if (stand) h.st.racers[0].skier = sk::stand_skier(h.st.world(), h.x, h.z, 0, 0);
  h.st.racers[0].body = sk::BodyState{};
  fs::step(h.st, std::span<const fs::Controls>(&c, 1));
  if (h.st.steps % static_cast<int64_t>(5 * fs::kHz) != 0) return;
  std::string row =
      hex(fs::state_hash(h.st, 0xcbf29ce484222325ull)) + " " + std::to_string(h.st.wanted.stars) +
      " " + kHeliModes[static_cast<size_t>(h.st.heli.mode)] + (h.st.heli.army ? "-army" : "");
  for (const fs::ArmyHeli& m : h.st.army.helis) {
    row += " " + std::string(fs::army_heli_mode_name(m.mode));
  }
  h.rows.push_back(row);
}

// Run until the first army machine is set down on the snow (null if none in `limit` s).
const fs::ArmyHeli* until_set(Hunt& h, double limit) {
  const fs::Controls none{};
  while (h.st.t < limit) {
    tick(h, true, none);
    for (const fs::ArmyHeli& m : h.st.army.helis) {
      if (m.mode == fs::ArmyHeliMode::set) return &m;
    }
  }
  return nullptr;
}

// THE THEFT: at three stars, the first machine set down is taken at its pilot's door; he lifts
// off in it, climbs, and lets go of it 60 m up.
struct Theft {
  bool set = false, took = false, raised = false, gone = false;
  bool climbed = false, crashed = false, back = false;
  Hunt h;
};

Theft theft() {
  Theft t;
  t.h = start(3);
  Hunt& h = t.h;
  const fs::ArmyHeli* m = until_set(h, 200);
  t.set = m != nullptr;
  if (!t.set) return t;
  const int32_t id = m->id;
  const fs::Spot door = fs::pilot_door(*m);
  h.x = door.x;
  h.z = door.z;
  fs::Controls press{};
  press.machine = true;
  h.st.racers[0].skier = sk::stand_skier(h.st.world(), h.x, h.z, 0, 0);
  tick(h, false, press);
  t.took = h.st.heli.army && h.st.heli.rider && h.st.army.stolen == 1;
  t.raised = h.st.wanted.stars >= 4;
  t.gone = true;
  for (const fs::ArmyHeli& o : h.st.army.helis) t.gone = t.gone && o.id != id;
  // Up on the collective, then let go of it once 60 m over the snow.
  fs::Controls up{};
  up.lever = 0.85;
  const double ground = h.st.level->ground_at(h.st.heli.body.x, h.st.heli.body.z);
  const double until = h.st.t + 30;
  while (h.st.t < until && h.st.heli.body.y - ground < 60) tick(h, false, up);
  t.climbed = h.st.heli.body.y - ground >= 60;
  tick(h, false, press);
  const double after = h.st.t + 60;
  const fs::Controls none{};
  while (h.st.t < after) {
    tick(h, false, none);
    t.crashed = t.crashed || h.st.heli.mode == fs::HeliMode::wreck;
  }
  t.back = t.crashed && !h.st.heli.army && h.st.heli.mode == fs::HeliMode::parked;
  return t;
}

// THE RAM: at three stars, his machine (boarded on its pad) put level with the first machine
// hunting, its disc at theirs, closing on it; then the fall to the snow.
struct Ram {
  bool hunting = false, rammed = false, crashed = false, raised = false, down = false;
  bool on_snow = false, cleared = false;
  double fell = 0;  // s from the ram to the snow
  Hunt h;
};

Ram ram() {
  Ram r;
  r.h = start(3);
  Hunt& h = r.h;
  const fs::Controls none{};
  int32_t id = -1;
  while (h.st.t < 200 && id < 0) {
    tick(h, true, none);
    for (const fs::ArmyHeli& m : h.st.army.helis) {
      if (m.mode == fs::ArmyHeliMode::hunt && m.on) id = m.id;
    }
  }
  r.hunting = id >= 0;
  if (!r.hunting) return r;
  // Boarded on its pad.
  const ge::core::Vec3 seat = fs::heli_seat(h.st.heli.body);
  h.st.racers[0].skier = sk::stand_skier(h.st.world(), seat.x, seat.z, 0, 0);
  fs::Controls press{};
  press.machine = true;
  tick(h, false, press);
  const int32_t stars = h.st.wanted.stars;
  // Put level with theirs, 20 m off its side, closing at 10 m/s, hovering on the collective.
  const fs::ArmyHeli* m = nullptr;
  for (const fs::ArmyHeli& o : h.st.army.helis) m = o.id == id ? &o : m;
  if (m == nullptr) return r;
  ge::helicopter::HeliState& b = h.st.heli.body;
  const double rx = lm::cos(m->heading);
  const double rz = -lm::sin(m->heading);
  b.x = m->x + rx * 20;
  b.z = m->z + rz * 20;
  b.y = m->y + fs::army_steal::kHubM - ge::helicopter::spec::rotor::kHub;
  b.vx = m->vx - rx * 10;
  b.vy = 0;
  b.vz = m->vz - rz * 10;
  b.heading = m->heading;
  b.spool = 1;
  b.grounded = false;
  const double rammed_at = h.st.t;
  fs::Controls hover{};
  hover.lever = 0.6;
  // Flown at theirs: held level with their disc and closing on it at 10 m/s (a pilot's hands).
  while (h.st.t < rammed_at + 5 && h.st.army.downed == 0) {
    for (const fs::ArmyHeli& o : h.st.army.helis) {
      if (o.id != id) continue;
      const double dx = o.x - b.x;
      const double dz = o.z - b.z;
      const double d = ge::core::hypot(dx, dz);
      b.y = o.y + fs::army_steal::kHubM - ge::helicopter::spec::rotor::kHub;
      b.vy = 0;
      b.vx = o.vx + dx / d * 10;
      b.vz = o.vz + dz / d * 10;
    }
    tick(h, false, hover);
  }
  r.rammed = h.st.army.downed == 1;
  r.crashed = h.st.heli.mode == fs::HeliMode::wreck;
  r.raised = h.st.wanted.stars >= stars && h.st.wanted.stars >= 3;
  const double t0 = h.st.t;
  while (h.st.t < t0 + 120) {
    tick(h, true, none);
    for (const fs::ArmyHeli& o : h.st.army.helis) {
      if (o.id != id || o.mode != fs::ArmyHeliMode::wreck || r.down) continue;
      r.down = true;
      r.fell = h.st.t - t0;
      r.on_snow = o.y == h.st.level->ground_at(o.x, o.z);
    }
  }
  // The wreck lies its time, then is gone.
  while (h.st.t < t0 + fs::army_steal::kWreckS + 40) tick(h, true, none);
  r.cleared = true;
  for (const fs::ArmyHeli& o : h.st.army.helis) r.cleared = r.cleared && o.id != id;
  return r;
}

TEST(a_set_down_machine_is_his_to_take_and_the_theft_raises_four_stars) {
  const Theft t = theft();
  CHECK_MSG(t.set, "no army machine set down on seed 38 at three stars in 200 s");
  CHECK_MSG(t.took, "the press at the pilot's door did not take the machine");
  CHECK_MSG(t.raised, "taking an army machine did not raise him to four stars");
  CHECK_MSG(t.gone, "the machine taken is still in the hunt");
  CHECK_MSG(t.climbed, "the taken machine did not climb 60 m in 30 s on the collective");
  CHECK_MSG(t.crashed, "the taken machine, let go of 60 m up, never came down");
  CHECK_MSG(t.back, "after the taken machine was wrecked the free ride's own is not on its pad");
}

TEST(his_machine_flown_into_theirs_brings_it_down_as_a_wreck) {
  const Ram r = ram();
  CHECK_MSG(r.hunting, "no army machine hunting him on seed 38 at three stars in 200 s");
  CHECK_MSG(r.rammed, "his disc at theirs, closing, brought nothing down in 5 s");
  CHECK_MSG(r.crashed, "his machine flew on after the ram");
  CHECK_MSG(r.raised, "downing an army machine did not hold him at three stars or more");
  CHECK_MSG(r.down && r.on_snow, "the rammed machine never lay on the snow in 120 s");
  CHECK_MSG(r.fell > 0.5, "the rammed machine was on the snow at once");
  CHECK_MSG(r.cleared, "the wreck was never cleared");
}

TEST(the_theft_and_the_ram_on_seed_38_keep_their_golden) {
  const Theft t = theft();
  const Ram r = ram();
  std::vector<std::string> got;
  got.reserve(t.h.rows.size() + r.h.rows.size());
  for (const auto& row : t.h.rows) got.push_back("steal " + row);
  for (const auto& row : r.h.rows) got.push_back("ram " + row);
  if (env_set("GE_GOLDEN_WRITE")) {
    std::ofstream out(ge::test::root_path(kGolden));
    out << "# each 5 s: the scenario, the state digest, the stars, the free ride's helicopter's "
           "mode, every army machine's (fall_line_army_steal_test, seed 38)\n";
    for (const std::string& row : got) out << row << "\n";
    return;
  }
  const auto want = ge::test::golden_words(kGolden);
  CHECK(want.size() == got.size());
  for (size_t i = 0; i < got.size() && i < want.size(); i++) {
    std::string line;
    for (const auto& w : want[i]) line += (line.empty() ? "" : " ") + w;
    CHECK_MSG(line == got[i], "the theft or the ram on seed 38 moved at row " + std::to_string(i) +
                                  ":\n  got  " + got[i] + "\n  want " + line +
                                  "\nIf the move is meant, rewrite " + kGolden +
                                  " with GE_GOLDEN_WRITE=1 and put the army-flight lab's steal "
                                  "and ram rows before and after in the PR.");
  }
}

}  // namespace
