// Rooms in a world: the sample level as the mover meets it builds the same triangles on any number
// of threads, a body stands on a prism's flat floor where the relief would have bumped it, the
// tour reaches every room by its doorways, and the walker walks the whole tour without being held
// up, falling through a floor or leaving the level, to the bits held in
// tests/golden/rooms_clip.txt.
//
// A deliberate change to the clip world, the tour or the walk moves the golden: rewrite it with
// `GE_GOLDEN_WRITE=1 build/<preset>/tests/rooms_clip_test` and say why in the PR.
//
// ge:summary Test: the room level's mover world, the tour of every room and the walk, golden
// ge:keywords test rooms clip collision mesh-world walk tour mover golden every room
// ge:skill rooms move testing
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "check.hpp"
#include "golden.hpp"
#include "move/spec.hpp"
#include "rooms/clip.hpp"
#include "rooms/text.hpp"
#include "rooms/walk.hpp"

namespace {

namespace rm = ge::rooms;
namespace mv = ge::move;
constexpr const char* kGolden = "tests/golden/rooms_clip.txt";

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

rm::Level halls() {
  std::ifstream in(ge::test::root_path("plugins/rooms/levels/halls.gelv.txt"));
  std::stringstream s;
  s << in.rdbuf();
  rm::Level level;
  std::string error;
  CHECK(rm::parse(s.str(), &level, &error));
  return level;
}

uint64_t bits(double v) {
  uint64_t u = 0;
  std::memcpy(&u, &v, sizeof u);
  return u;
}

}  // namespace

TEST(the_clip_world_is_the_same_on_any_thread_count) {
  const rm::Level level = halls();
  const mv::MeshWorld one = rm::clip_world(level, 1);
  const mv::MeshWorld four = rm::clip_world(level, 4);
  CHECK(one.tri.size() > 3000);
  CHECK(one.tri.size() == four.tri.size());
  CHECK(std::memcmp(one.tri.data(), four.tri.data(), one.tri.size() * sizeof(mv::Vec3)) == 0);
}

TEST(a_body_stands_on_a_prisms_flat_floor_under_its_relief) {
  // The cellar's cobbles grow stones 4 cm deep, but the body stands on the stored floor.
  const rm::Level level = halls();
  const mv::MeshWorld w = rm::clip_world(level, 2);
  const mv::World world = mv::as_world(w);
  const mv::MoveSpec spec = *mv::find_preset("walker");
  const mv::MoveState s = mv::stand_at(spec, world, {4, 0.3, -11});
  CHECK(s.grounded);
  CHECK_MSG(s.pos.y >= 0 && s.pos.y < 0.02, "stood at y " + std::to_string(s.pos.y));
  CHECK(rm::room_at(level, {4, 0.5, -11}) == rm::room_index(level, "cellar"));
}

TEST(the_walker_tours_every_room_by_its_doorways_to_golden_bits) {
  const rm::Level level = halls();
  const mv::MeshWorld w = rm::clip_world(level, 2);
  const mv::World world = mv::as_world(w);
  const mv::MoveSpec spec = *mv::find_preset("walker");
  const rm::Tour tour = rm::tour(level, world, spec, {5, 0, 4});
  CHECK_MSG(tour.error.empty(), tour.error);
  std::vector<uint8_t> on(level.rooms.size(), 0);
  for (const rm::TourPoint& p : tour.points) on[size_t(p.room)] = 1;
  for (size_t r = 0; r < on.size(); ++r) CHECK_MSG(on[r], "the tour misses " + level.rooms[r].name);

  rm::WalkOptions options;
  options.sprint = true;
  const rm::Walk walk = rm::walk(level, world, spec, tour, options);
  CHECK_MSG(walk.error.empty(), walk.error);
  CHECK(walk.reached == tour.points.size());
  CHECK_MSG(walk.rooms.size() == level.rooms.size(),
            "entered " + std::to_string(walk.rooms.size()) + " rooms");
  CHECK_MSG(walk.lowest_below_floor < 0.05,
            "went " + std::to_string(walk.lowest_below_floor) + " m under a floor");

  char row[160];
  std::snprintf(row, sizeof row, "%zx %zx %zx %016llx %016llx %016llx\n", tour.points.size(),
                walk.steps, walk.rooms.size(),
                static_cast<unsigned long long>(bits(walk.end.pos.x)),
                static_cast<unsigned long long>(bits(walk.end.pos.y)),
                static_cast<unsigned long long>(bits(walk.end.pos.z)));
  const std::string table =
      std::string("# points steps rooms end-x end-y end-z (rooms_clip_test, GE_GOLDEN_WRITE=1)\n") +
      row;
  if (env_set("GE_GOLDEN_WRITE")) {
    std::ofstream(ge::test::root_path(kGolden)) << table;
    std::printf("  wrote %s\n", kGolden);
    return;
  }
  const auto rows = ge::test::golden_rows(kGolden);
  CHECK(rows.size() == 1);
  if (rows.size() == 1 && rows[0].size() == 6) {
    CHECK(rows[0][0] == tour.points.size());
    CHECK(rows[0][1] == walk.steps);
    CHECK(rows[0][3] == bits(walk.end.pos.x));
    CHECK(rows[0][4] == bits(walk.end.pos.y));
    CHECK(rows[0][5] == bits(walk.end.pos.z));
  }
}
