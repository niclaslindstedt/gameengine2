// The classic BSP families: the sample fixture written as a map of every family reads back into
// the one view with its rooms, faces, leaves, visibility, lightmaps and entities; the bytes and the
// view hold golden (tests/golden/brush_read.txt); every triangle faces into its room; and the
// reader refuses what it cannot read with a reason and a next step.
//
// A deliberate change to a writer or a reader moves the golden: rewrite it with
// `GE_GOLDEN_WRITE=1 build/<preset>/tests/brush_read_test`, run the `bsp` lab before and after
// (`--tag=before`, `--compare=before`) and say in the PR why the bits moved.
//
// ge:summary Test: fixture maps of every BSP family read back, hold golden, face in, refuse damage
// ge:keywords test brush bsp v29 bsp2 v38 v46 fixture read write golden leaf cluster pvs refuse
// ge:skill brush testing
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "brush/read.hpp"
#include "brush/write.hpp"
#include "check.hpp"
#include "golden.hpp"

namespace {

namespace br = ge::brush;
constexpr const char* kGolden = "tests/golden/brush_read.txt";
constexpr br::Family kFamilies[] = {br::Family::v29, br::Family::bsp2, br::Family::v38,
                                    br::Family::v46};

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

std::vector<uint8_t> written(br::Family f) {
  std::vector<uint8_t> bytes;
  std::string error;
  CHECK(br::write_bsp(br::sample_fixture(), f, &bytes, &error));
  if (!error.empty()) std::printf("  %s\n", error.c_str());
  return bytes;
}

br::Map read(br::Family f) {
  br::Map map;
  std::string error;
  const bool ok = br::read_bsp(written(f), &map, &error);
  if (!ok) std::printf("  %s\n", error.c_str());
  CHECK(ok);
  return map;
}

uint64_t fnv(const std::vector<uint8_t>& b) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (uint8_t c : b) h = (h ^ c) * 0x100000001b3ull;
  return h;
}

bool refused(std::vector<uint8_t> bytes, const char* expect) {
  br::Map map;
  std::string error;
  if (br::read_bsp(bytes, &map, &error)) return false;
  const bool ok =
      error.find(expect) != std::string::npos && error.find("next:") != std::string::npos;
  if (!ok) std::printf("  refusal was: %s\n", error.c_str());
  return ok;
}

}  // namespace

TEST(every_family_reads_the_fixture_back) {
  const br::Fixture fx = br::sample_fixture();
  for (br::Family f : kFamilies) {
    const br::Map m = read(f);
    CHECK(m.family == f);
    CHECK(m.leaves.size() == fx.rooms.size() + 1);
    CHECK(m.clusters == int32_t(fx.rooms.size()));
    CHECK(m.models.size() == 1);
    CHECK(m.triangles() > 80);
    CHECK(m.entities.size() == 2 + fx.lights.size());
    CHECK(m.entities[0].get("classname") == "worldspawn");
    CHECK(m.entities[0].get("message") == fx.message);
    CHECK(m.entities[1].get("classname") == "info_player_start");
    CHECK(m.patches == (f == br::Family::v46 ? 1u : 0u));
    for (const br::Surface& s : m.surfaces) {
      CHECK(s.count % 3 == 0);
      CHECK(s.lightmap >= 0);
    }
  }
}

TEST(a_room_s_centre_is_its_leaf_and_outside_is_solid) {
  const br::Fixture fx = br::sample_fixture();
  for (br::Family f : kFamilies) {
    const br::Map m = read(f);
    for (size_t r = 0; r < fx.rooms.size(); ++r) {
      float p[3];
      for (int k = 0; k < 3; ++k) p[k] = float(fx.rooms[r].min[k] + fx.rooms[r].max[k]) / 2;
      const uint32_t leaf = br::leaf_at(m, p);
      CHECK(leaf == r + 1);
      CHECK(!m.leaves[leaf].solid);
      CHECK(m.leaves[leaf].cluster == int32_t(r));
    }
    const float outside[3] = {-40, -40, 10}, inside_wall[3] = {600, 20, 20};
    CHECK(m.leaves[br::leaf_at(m, outside)].solid);
    CHECK(m.leaves[br::leaf_at(m, inside_wall)].solid);
  }
}

TEST(the_visibility_reaches_two_doorways_deep) {
  for (br::Family f : kFamilies) {
    const br::Map m = read(f);
    // hall 0, east door 1, side room 2, north door 3, corridor 4, vault door 5, vault 6.
    CHECK(br::cluster_sees(m, 0, 2) && br::cluster_sees(m, 2, 0));
    CHECK(br::cluster_sees(m, 0, 4));
    CHECK(br::cluster_sees(m, 4, 6));
    CHECK(!br::cluster_sees(m, 2, 6) && !br::cluster_sees(m, 6, 2));
    CHECK(!br::cluster_sees(m, 0, 6));
    CHECK(!br::cluster_sees(m, -1, 0));
  }
}

TEST(every_triangle_faces_into_its_room) {
  for (br::Family f : kFamilies) {
    const br::Map m = read(f);
    for (const br::Surface& s : m.surfaces) {
      for (uint32_t t = s.first; t < s.first + s.count; t += 3) {
        const float* a = m.vertices[m.indices[t]].pos;
        const float* b = m.vertices[m.indices[t + 1]].pos;
        const float* c = m.vertices[m.indices[t + 2]].pos;
        const double e1[3] = {double(b[0]) - a[0], double(b[1]) - a[1], double(b[2]) - a[2]};
        const double e2[3] = {double(c[0]) - a[0], double(c[1]) - a[1], double(c[2]) - a[2]};
        const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                             e1[0] * e2[1] - e1[1] * e2[0]};
        const float* vn = m.vertices[m.indices[t]].nrm;
        CHECK(n[0] * vn[0] + n[1] * vn[1] + n[2] * vn[2] >= 0);
      }
    }
  }
}

TEST(lightmaps_cover_their_surfaces_and_the_lights_show) {
  for (br::Family f : kFamilies) {
    const br::Map m = read(f);
    for (const br::Surface& s : m.surfaces) {
      for (uint32_t t = s.first; t < s.first + s.count; ++t) {
        const br::Vertex& v = m.vertices[m.indices[t]];
        CHECK(v.lm[0] > 0 && v.lm[0] < 1 && v.lm[1] > 0 && v.lm[1] < 1);
      }
      const br::Lightmap& l = m.lightmaps[size_t(s.lightmap)];
      CHECK(l.offset + size_t(l.width) * l.height * 3 <= m.light_rgb.size());
    }
    uint8_t lo = 255, hi = 0;
    for (uint8_t c : m.light_rgb) lo = std::min(lo, c), hi = std::max(hi, c);
    CHECK(hi > 150);
  }
}

TEST(the_29_family_and_bsp2_read_to_the_same_geometry) {
  const br::Map a = read(br::Family::v29), b = read(br::Family::bsp2);
  CHECK(a.vertices.size() == b.vertices.size());
  CHECK(a.indices == b.indices);
  CHECK(a.light_rgb == b.light_rgb);
  CHECK(a.pvs == b.pvs);
  const br::Map c = read(br::Family::v38);
  CHECK(a.indices == c.indices);
  CHECK(a.light_rgb == c.light_rgb);
}

TEST(a_lit_file_drops_into_a_29_map) {
  br::Map m = read(br::Family::v29);
  std::vector<uint8_t> lit = {'Q', 'L', 'I', 'T', 1, 0, 0, 0};
  for (size_t i = 0; i < m.light_rgb.size(); ++i) lit.push_back(uint8_t(i % 3 == 0 ? 200 : 10));
  std::string error;
  CHECK(br::read_lit(lit, &m, &error));
  CHECK(m.light_rgb[0] == 200 && m.light_rgb[1] == 10);
  lit.pop_back();
  CHECK(!br::read_lit(lit, &m, &error));
  CHECK(error.find("next:") != std::string::npos);
}

TEST(the_fixtures_hold_golden) {
  std::string table = "# family bytes triangles file-digest view-digest (brush_read_test)\n";
  std::vector<std::vector<uint64_t>> rows;
  for (br::Family f : kFamilies) {
    const std::vector<uint8_t> bytes = written(f);
    const br::Map m = read(f);
    rows.push_back({uint64_t(f), bytes.size(), m.triangles(), fnv(bytes), br::map_digest(m)});
    char row[128];
    std::snprintf(row, sizeof row, "%x %zx %zx %016llx %016llx\n", unsigned(f), bytes.size(),
                  m.triangles(), static_cast<unsigned long long>(fnv(bytes)),
                  static_cast<unsigned long long>(br::map_digest(m)));
    table += row;
  }
  if (env_set("GE_GOLDEN_WRITE")) {
    std::ofstream(ge::test::root_path(kGolden)) << table;
    std::printf("  wrote %s\n", kGolden);
    return;
  }
  const auto golden = ge::test::golden_rows(kGolden);
  CHECK(golden.size() == rows.size());
  for (size_t r = 0; r < golden.size() && r < rows.size(); ++r) CHECK(golden[r] == rows[r]);
}

TEST(damage_is_refused_with_a_reason) {
  CHECK(refused({1, 2, 3}, "under 8 bytes"));
  CHECK(refused({'X', 'Y', 'Z', 'W', 0, 0, 0, 0}, "unknown first word"));
  CHECK(refused({'I', 'B', 'S', 'P', 47, 0, 0, 0}, "version 47"));
  CHECK(refused({'2', 'P', 'S', 'B', 0, 0, 0, 0}, "2PSB"));
  for (br::Family f : kFamilies) {
    std::vector<uint8_t> b = written(f);
    b.resize(b.size() / 2);
    CHECK(refused(b, "past the end"));
    // The second lump's length made not a whole number of records.
    std::vector<uint8_t> c = written(f);
    const size_t dir = f == br::Family::v29 || f == br::Family::bsp2 ? 4 : 8;
    c[dir + 12] = uint8_t(c[dir + 12] - 1);
    CHECK(refused(c, "whole number"));
  }
  std::string error;
  std::vector<br::Entity> ents;
  CHECK(!br::parse_entities("{\n\"a\" \"b\"\n\"c\"\n}", &ents, &error));
  CHECK(error.find("line 3") != std::string::npos);
  CHECK(br::parse_entities("// a note\n{ \"a\" \"b\" }\n{ }", &ents, &error));
  CHECK(ents.size() == 2 && ents[0].get("a") == "b" && ents[1].pairs.empty());
}
