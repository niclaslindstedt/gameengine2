// Growing rooms: the sample level's rooms grow to the bits held in tests/golden/rooms_build.txt on
// any number of threads, every triangle faces into its room, a cave stays inside its footprint,
// and a room's seed (only its seed) changes what grows.
//
// A deliberate change to the builder moves the golden: rewrite it with
// `GE_GOLDEN_WRITE=1 build/<preset>/tests/rooms_build_test`, run the `rooms` lab before and after
// (`--tag=before`, `--compare=before`) and put both in the PR.
//
// ge:summary Test: rooms grow golden on any thread count, face inward, caves stay in, seeds matter
// ge:keywords test rooms build golden digest cave seed facing winding parallel noise3
// ge:skill rooms testing
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "check.hpp"
#include "golden.hpp"
#include "rooms/build.hpp"
#include "rooms/noise3.hpp"
#include "rooms/text.hpp"

namespace {

namespace rm = ge::rooms;
constexpr const char* kGolden = "tests/golden/rooms_build.txt";

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

}  // namespace

TEST(halls_grow_golden_on_any_thread_count) {
  const rm::Level level = halls();
  const auto one = rm::build_level(level, 1);
  const auto four = rm::build_level(level, 4);
  CHECK(one.size() == level.rooms.size());
  std::string table = "# room vertices triangles digest (rooms_build_test, GE_GOLDEN_WRITE=1)\n";
  for (size_t r = 0; r < one.size(); ++r) {
    CHECK(one[r].triangles() > 0);
    CHECK(rm::mesh_digest(one[r]) == rm::mesh_digest(four[r]));
    char row[96];
    std::snprintf(row, sizeof row, "%zx %zx %zx %016llx\n", r, one[r].vertices(),
                  one[r].triangles(), static_cast<unsigned long long>(rm::mesh_digest(one[r])));
    table += row;
  }
  if (env_set("GE_GOLDEN_WRITE")) {
    std::ofstream(ge::test::root_path(kGolden)) << table;
    std::printf("  wrote %s\n", kGolden);
    return;
  }
  const auto rows = ge::test::golden_rows(kGolden);
  CHECK(rows.size() == one.size());
  for (size_t r = 0; r < rows.size() && r < one.size(); ++r) {
    CHECK(rows[r].size() == 4);
    CHECK(rows[r][1] == one[r].vertices());
    CHECK(rows[r][2] == one[r].triangles());
    CHECK(rows[r][3] == rm::mesh_digest(one[r]));
  }
}

TEST(every_triangle_faces_the_way_its_normals_do) {
  const rm::Level level = halls();
  const auto meshes = rm::build_level(level, 2);
  for (size_t r = 0; r < meshes.size(); ++r) {
    const rm::Mesh& m = meshes[r];
    const bool cave = level.rooms[r].shape == rm::Shape::cave;
    size_t against = 0;
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
      const float* a = &m.pos[size_t(m.idx[t]) * 3];
      const float* b = &m.pos[size_t(m.idx[t + 1]) * 3];
      const float* c = &m.pos[size_t(m.idx[t + 2]) * 3];
      const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
      const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
      const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                          e1[0] * e2[1] - e1[1] * e2[0]};
      float s = 0;
      for (int k = 0; k < 3; ++k) {
        for (int v = 0; v < 3; ++v) s += n[k] * m.nrm[size_t(m.idx[t + size_t(v)]) * 3 + size_t(k)];
      }
      against += s < 0;
    }
    // A prism's never; a cave's slivers where a crag's field folds, a few in a hundred at most.
    CHECK(cave ? against * 50 <= m.triangles() : against == 0);
  }
}

TEST(a_prism_wall_faces_into_its_room) {
  const rm::Level level = halls();
  const rm::Mesh hall = rm::build_room(level, 0);
  // The hall is x 0..10, z 0..8: a vertex on its west wall (x = 0) points +x, into the room.
  size_t west = 0;
  for (size_t v = 0; v < hall.vertices(); ++v) {
    const float* p = &hall.pos[v * 3];
    if (p[0] == 0.0f && p[1] > 0.1f && p[1] < 3.9f && p[2] > 0.1f && p[2] < 7.9f) {
      CHECK(hall.nrm[v * 3] == 1.0f);
      ++west;
    }
  }
  CHECK(west >= 2);
}

TEST(a_cave_stays_inside_its_footprint_and_height) {
  const rm::Level level = halls();
  const rm::Mesh cave = rm::build_room(level, 2);
  CHECK(cave.triangles() > 1000);
  for (size_t v = 0; v < cave.vertices(); ++v) {
    const float* p = &cave.pos[v * 3];
    // The footprint's box is x -1..12, z 8..19; its height 0..6; a frame reaches 0.3 m out.
    CHECK(p[0] > -1.31f && p[0] < 12.31f && p[2] > 7.69f && p[2] < 19.31f);
    CHECK(p[1] > -0.05f && p[1] < 6.05f);
  }
}

TEST(only_a_rooms_seed_changes_what_grows) {
  rm::Level level = halls();
  const uint64_t base = rm::mesh_digest(rm::build_room(level, 2));
  CHECK(rm::mesh_digest(rm::build_room(level, 2)) == base);
  const uint64_t hall = rm::mesh_digest(rm::build_room(level, 0));
  level.rooms[2].seed += 1;
  CHECK(rm::mesh_digest(rm::build_room(level, 2)) != base);
  // The brick hall grows nothing from its seed, and a neighbour's seed is not its business.
  CHECK(rm::mesh_digest(rm::build_room(level, 0)) == hall);
  level.rooms[0].seed += 7;
  CHECK(rm::mesh_digest(rm::build_room(level, 0)) == hall);
  // A rock wall does grow from it.
  const uint64_t gallery = rm::mesh_digest(rm::build_room(level, 1));
  level.rooms[1].seed += 1;
  CHECK(rm::mesh_digest(rm::build_room(level, 1)) != gallery);
}

TEST(noise3_stays_in_its_range) {
  float lo = 1, hi = 0;
  for (int i = 0; i < 20000; ++i) {
    const float x = float(i) * 0.137f - 300.0f, y = float(i % 97) * 0.31f, z = float(i % 13) * 1.7f;
    const float v = rm::fbm3(x, y, z, 99u, 4);
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  CHECK(lo >= 0.0f && hi < 1.0f);
  CHECK(hi - lo > 0.5f);
}
