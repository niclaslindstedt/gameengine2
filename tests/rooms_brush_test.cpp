// Brushes in a room level: the vault sample (plugins/rooms/levels/vault.gelv.txt) reads back from
// its text and its bytes to the bit, a level without brushes still writes version 1, the CSG
// keeps what can be seen (a floor under water) and drops what cannot (a wall's top under the
// roof, the floor under a pillar, a pool's bottom on the floor), a brush that does not close is
// left out, a body stands on the floor and is held out of a clip brush but sinks through water,
// and the vault's faces build to the bits held in tests/golden/rooms_brush.txt.
//
// A deliberate change to the brush builder moves the golden: rewrite it with
// `GE_GOLDEN_WRITE=1 build/<preset>/tests/rooms_brush_test` and say why in the PR.
//
// ge:summary Test: brushes, groups and entities in a room level: text, bytes, CSG, clip, golden
// ge:keywords test rooms brush brushes csg entity group contents liquid clip vault golden
// ge:skill rooms testing
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "check.hpp"
#include "golden.hpp"
#include "move/spec.hpp"
#include "move/step.hpp"
#include "rooms/brush_build.hpp"
#include "rooms/clip.hpp"
#include "rooms/codec.hpp"
#include "rooms/text.hpp"

namespace {

namespace rm = ge::rooms;
namespace mv = ge::move;
constexpr const char* kGolden = "tests/golden/rooms_brush.txt";

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

std::string read_text(const char* rel) {
  std::ifstream in(ge::test::root_path(rel));
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

rm::Level load(const char* rel) {
  rm::Level level;
  std::string error;
  CHECK_MSG(rm::parse(read_text(rel), &level, &error), error);
  return level;
}

rm::Level vault() { return load("plugins/rooms/levels/vault.gelv.txt"); }

// Whether a triangle of the mesh from brush `b` facing `ny` (+1 up, -1 down) at height `y` covers
// the point (x, z).
bool covers(const rm::BrushMesh& m, uint32_t b, int ny, double y, double x, double z) {
  for (size_t t = 0; t < m.triangles(); ++t) {
    if (m.brush[t] != b) continue;
    const float* p[3];
    for (size_t k = 0; k < 3; ++k) p[k] = &m.pos[size_t(m.idx[t * 3 + k]) * 3];
    const double n = m.nrm[size_t(m.idx[t * 3]) * 3 + 1];
    if (!(ny > 0 ? n > 0.99 : n < -0.99) || p[0][1] < y - 1e-3 || p[0][1] > y + 1e-3) continue;
    int pos = 0, neg = 0;
    for (size_t k = 0; k < 3; ++k) {
      const float* a = p[k];
      const float* c = p[(k + 1) % 3];
      const double cross = (c[0] - a[0]) * (z - a[2]) - (c[2] - a[2]) * (x - a[0]);
      pos += cross > 0;
      neg += cross < 0;
    }
    if (pos == 0 || neg == 0) return true;
  }
  return false;
}

}  // namespace

TEST(the_vault_reads_back_from_its_text_and_bytes_to_the_bit) {
  const rm::Level level = vault();
  CHECK(level.brushes.size() == 13);
  CHECK(level.groups.size() == 1);
  CHECK(level.entities.size() == 2);
  CHECK(level.entities[1].classname == "func_door");
  CHECK(level.entities[1].get("targetname") == "vault door");
  CHECK(level.brushes[11].group == 0);
  CHECK(level.materials[rm::material_index(level, "wall")].texture ==
        "stone_wall seed=3 tint=#8a8478");
  CHECK(level.materials[rm::material_index(level, "grass")].surface ==
        "grass meadow height=0.6 density=300");

  const std::vector<uint8_t> bytes = rm::encode(level);
  CHECK(bytes[4] == 2);
  rm::Level back;
  std::string error;
  CHECK_MSG(rm::decode(bytes, &back, &error), error);
  CHECK(rm::encode(back) == bytes);
  CHECK(rm::print(back) == rm::print(level));

  rm::Level again;
  CHECK_MSG(rm::parse(rm::print(level), &again, &error), error);
  CHECK(rm::encode(again) == bytes);
}

TEST(a_level_without_brushes_still_writes_version_1) {
  const rm::Level halls = load("plugins/rooms/levels/halls.gelv.txt");
  const std::vector<uint8_t> bytes = rm::encode(halls);
  CHECK(bytes[4] == 1);
  rm::Level back;
  std::string error;
  CHECK_MSG(rm::decode(bytes, &back, &error), error);
  CHECK(rm::encode(back) == bytes);
}

TEST(the_csg_keeps_what_is_seen_and_drops_what_is_buried) {
  const rm::Level level = vault();
  const rm::BrushMesh m = rm::build_brushes(level, 2);
  CHECK(m.unbounded == 0);
  CHECK(m.triangles() > 50);
  // The floor (brush 0) shows round the pillar and under the pool, but not under the pillar.
  CHECK(covers(m, 0, 1, 0, 1, 1));
  CHECK(covers(m, 0, 1, 0, 6.5, 2));
  CHECK(!covers(m, 0, 1, 0, 3.3, 2.8));
  // The pool (brush 10) shows its surface, never its bottom on the floor.
  CHECK(covers(m, 10, 1, 0.6, 6.5, 2));
  CHECK(!covers(m, 10, -1, 0, 6.5, 2));
  // The west wall's top (brush 1) is under the roof.
  CHECK(!covers(m, 1, 1, 3, -0.25, 3));
  CHECK(covers(m, 7, 1, 3.5, 2, 3));
  // The clip brush (12) is never drawn.
  for (size_t t = 0; t < m.triangles(); ++t) {
    if (m.brush[t] == 12) CHECK(m.hidden[t] == 1);
  }
  const rm::BrushMesh one = rm::build_brushes(level, 1);
  const rm::BrushMesh four = rm::build_brushes(level, 4);
  CHECK(rm::brush_mesh_digest(one) == rm::brush_mesh_digest(four));
  CHECK(rm::brush_mesh_digest(one) == rm::brush_mesh_digest(m));
}

TEST(a_brush_that_does_not_close_is_left_out) {
  rm::Level level = vault();
  // Brush 9 (the pillar) loses its top: nothing closes it upward.
  const rm::Brush pillar = level.brushes[9];
  const uint32_t top = pillar.first_face + 2;
  level.faces.erase(level.faces.begin() + std::ptrdiff_t(top));
  level.brushes[9].faces = uint8_t(pillar.faces - 1);
  for (size_t b = 10; b < level.brushes.size(); ++b) --level.brushes[b].first_face;
  CHECK(rm::validate(level).empty());
  CHECK(rm::brush_polygons(level, 9).empty());
  CHECK(rm::build_brushes(level, 2).unbounded == 1);
}

TEST(a_bad_brush_is_refused_with_its_reason) {
  rm::Level level = vault();
  level.faces[3].n[0] = 0.5f;
  CHECK(!rm::validate(level).empty());
  level = vault();
  level.brushes[0].faces = 3;
  CHECK(!rm::validate(level).empty());
  level = vault();
  level.entities[0].classname = "two words";
  CHECK(!rm::validate(level).empty());
  rm::Level parsed;
  std::string error;
  CHECK(!rm::parse("face 0,1,0 0 wall s=1,0,0,0 t=0,0,1,0\n", &parsed, &error));
  CHECK_MSG(error.find("line 1") != std::string::npos, error);
}

TEST(a_body_stands_on_the_floor_is_kept_out_of_clip_and_sinks_through_water) {
  const rm::Level level = vault();
  const mv::MeshWorld w = rm::clip_world(level, 2);
  const mv::World world = mv::as_world(w);
  const mv::MoveSpec spec = *mv::find_preset("walker");
  const mv::MoveState s = mv::stand_at(spec, world, {1.5, 0.3, 1.5});
  CHECK(s.grounded);
  CHECK_MSG(s.pos.y >= 0 && s.pos.y < 0.02, "stood at y " + std::to_string(s.pos.y));
  const mv::MoveState pool = mv::stand_at(spec, world, {6.5, 0.3, 2});
  CHECK(pool.grounded);
  CHECK_MSG(pool.pos.y >= 0 && pool.pos.y < 0.02, "stood at y " + std::to_string(pool.pos.y));
  const mv::Box box{{-0.2, 0.1, -0.2}, {0.2, 0.5, 0.2}};
  const mv::Hit hit = world.sweep(box, {0.5, 0, 4.5}, {2.5, 0, 4.5});
  CHECK_MSG(hit.fraction < 1 && hit.end.x < 0.81, "reached x " + std::to_string(hit.end.x));
}

TEST(the_vaults_faces_build_to_golden_bits) {
  const rm::BrushMesh m = rm::build_brushes(vault(), 2);
  char row[96];
  std::snprintf(row, sizeof row, "%zx %016llx\n", m.triangles(),
                static_cast<unsigned long long>(rm::brush_mesh_digest(m)));
  const std::string table =
      std::string("# triangles digest (rooms_brush_test, GE_GOLDEN_WRITE=1)\n") + row;
  if (env_set("GE_GOLDEN_WRITE")) {
    std::ofstream(ge::test::root_path(kGolden)) << table;
    std::printf("  wrote %s\n", kGolden);
    return;
  }
  const auto rows = ge::test::golden_rows(kGolden);
  CHECK(rows.size() == 1);
  if (rows.size() == 1 && rows[0].size() == 2) {
    CHECK(rows[0][0] == m.triangles());
    CHECK(rows[0][1] == rm::brush_mesh_digest(m));
  }
}
