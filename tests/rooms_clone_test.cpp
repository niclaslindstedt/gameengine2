// A classic map cloned into a room level: the texture map reads as the matcher and an agent write
// it (comments only at a line's start or after a space, its own lines passed over), a texture it
// does not name is refused unless placeholders are asked for, the brush plugin's fixture (written
// as version 29) clones into brushes that all close and round-trips through bytes and text, a body
// stands on every fixture room's floor and at the start, the entities' origins and angles come
// across in the engine's terms, smoothing marks every face, a family not read yet is refused, and
// the clone's bytes are held in tests/golden/rooms_clone.txt.
//
// A deliberate change to the cloner moves the golden: rewrite it with
// `GE_GOLDEN_WRITE=1 build/<preset>/tests/rooms_clone_test` and say why in the PR.
//
// ge:summary Test: a classic map cloned into a room level: texture map, refusals, fixture, golden
// ge:keywords test rooms clone bsp fixture texture-map texmap placeholder entity origin golden
// ge:skill rooms brush testing
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "brush/read.hpp"
#include "brush/write.hpp"
#include "check.hpp"
#include "golden.hpp"
#include "move/spec.hpp"
#include "move/step.hpp"
#include "rooms/clip.hpp"
#include "rooms/clone.hpp"
#include "rooms/codec.hpp"
#include "rooms/text.hpp"

namespace {

namespace rm = ge::rooms;
namespace br = ge::brush;
namespace mv = ge::move;
constexpr const char* kGolden = "tests/golden/rooms_clone.txt";

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

br::Map fixture_map(br::Family family = br::Family::v29) {
  std::vector<uint8_t> bytes;
  std::string error;
  CHECK_MSG(br::write_bsp(br::sample_fixture(), family, &bytes, &error), error);
  br::Map map;
  CHECK_MSG(br::read_bsp(bytes, &map, &error), error);
  return map;
}

// A texture map naming every texture of `map` with a recipe.
rm::TextureMap all_named(const br::Map& map) {
  std::string text = "# every fixture texture, approved\n";
  for (const br::Texture& t : map.textures) {
    text += "texture " + t.name + " " + t.name + "_recipe seed=1 tint=#808080 # pin 0.9\n";
  }
  rm::TextureMap tm;
  std::string error;
  CHECK_MSG(rm::parse_texture_map(text, &tm, &error), error);
  return tm;
}

uint64_t fnv(const std::vector<uint8_t>& b) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (uint8_t c : b) h = (h ^ c) * 0x100000001b3ull;
  return h;
}

}  // namespace

TEST(the_texture_map_reads_as_the_matcher_and_an_agent_write_it) {
  const char* text =
      "# a texture map\n"
      "texture wall1 brick_wall seed=3 tint=#8a5a40 spread=#201810 # pin 0.31\n"
      "liquid *water0 water still_water seed=2\n"
      "sky sky4 night_sky\n"
      "surface grass1 grass meadow height=0.6\n"
      "texture grass1 turf seed=9\n"
      "class wall1 metal panel   # the matcher's own line\n"
      "  \n";
  rm::TextureMap tm;
  std::string error;
  CHECK_MSG(rm::parse_texture_map(text, &tm, &error), error);
  CHECK(tm.rows.size() == 4);
  const rm::TextureChoice* wall = tm.find("wall1");
  CHECK(wall != nullptr && wall->ref == "brick_wall seed=3 tint=#8a5a40 spread=#201810");
  const rm::TextureChoice* water = tm.find("*water0");
  CHECK(water != nullptr && water->kind == rm::TextureKind::liquid && water->liquid == "water" &&
        water->ref == "still_water seed=2");
  const rm::TextureChoice* sky = tm.find("sky4");
  CHECK(sky != nullptr && sky->kind == rm::TextureKind::sky && sky->ref == "night_sky");
  const rm::TextureChoice* grass = tm.find("grass1");
  CHECK(grass != nullptr && grass->surface == "grass meadow height=0.6" &&
        grass->ref == "turf seed=9");
  CHECK(!rm::parse_texture_map("texture lonely\n", &tm, &error));
  CHECK_MSG(error.find("line 1") != std::string::npos, error);
}

TEST(a_texture_the_map_does_not_name_is_refused_unless_placeholders) {
  const br::Map map = fixture_map();
  rm::Level level;
  rm::CloneReport rep;
  std::string error;
  rm::CloneOptions opt;
  CHECK(!rm::clone_map(map, rm::TextureMap{}, opt, &level, &rep, &error));
  CHECK_MSG(error.find(map.textures[0].name) != std::string::npos, error);
  opt.placeholders = true;
  CHECK_MSG(rm::clone_map(map, rm::TextureMap{}, opt, &level, &rep, &error), error);
  CHECK(rep.placeholders.size() == map.textures.size());
}

TEST(the_fixture_clones_into_closed_brushes_that_read_back_to_the_bit) {
  const br::Map map = fixture_map();
  rm::Level level;
  rm::CloneReport rep;
  std::string error;
  rm::CloneOptions opt;
  opt.workers = 2;
  CHECK_MSG(rm::clone_map(map, all_named(map), opt, &level, &rep, &error), error);
  CHECK(rep.brushes > 10);
  CHECK(rep.open == 0 && rep.too_many == 0);
  CHECK(rep.placeholders.empty());
  CHECK(level.materials[0].texture == map.textures[0].name + "_recipe seed=1 tint=#808080");
  const std::vector<uint8_t> bytes = rm::encode(level);
  rm::Level back;
  CHECK_MSG(rm::decode(bytes, &back, &error), error);
  CHECK(rm::encode(back) == bytes);
  rm::Level text;
  CHECK_MSG(rm::parse(rm::print(level), &text, &error), error);
  CHECK(rm::encode(text) == bytes);
  // The same clone on any number of workers.
  rm::Level one;
  opt.workers = 1;
  CHECK(rm::clone_map(map, all_named(map), opt, &one, &rep, &error));
  CHECK(rm::encode(one) == bytes);
}

TEST(a_body_stands_on_every_fixture_floor_and_the_entities_come_across) {
  const br::Map map = fixture_map();
  rm::Level level;
  rm::CloneReport rep;
  std::string error;
  CHECK_MSG(rm::clone_map(map, all_named(map), rm::CloneOptions{}, &level, &rep, &error), error);
  const mv::MeshWorld w = rm::clip_world(level, 2);
  const mv::World world = mv::as_world(w);
  const mv::MoveSpec spec = *mv::find_preset("walker");
  for (const br::FixtureRoom& r : br::sample_fixture().rooms) {
    if (r.max[0] - r.min[0] < 128 || r.max[1] - r.min[1] < 128) continue;  // the doorways
    const double x = (r.min[0] + r.max[0]) / 2.0 * br::kUnitM;
    const double z = (r.min[1] + r.max[1]) / 2.0 * br::kUnitM;
    const double floor = r.min[2] * br::kUnitM;
    const mv::MoveState s = mv::stand_at(spec, world, {x, floor + 0.3, z});
    CHECK_MSG(s.grounded && std::fabs(s.pos.y - floor) < 0.02,
              r.name + ": stood at y " + std::to_string(s.pos.y));
  }
  bool start = false, world_message = false;
  for (const rm::Entity& e : level.entities) {
    if (e.classname == "info_player_start") {
      start = true;
      CHECK(e.get("origin") == "6.4 0.6 4.8");
    }
    if (e.classname == "worldspawn") world_message = e.get("message") == "The fixture halls";
  }
  CHECK(start);
  CHECK(world_message);
}

TEST(smoothing_marks_every_face_and_a_family_not_read_yet_is_refused) {
  const br::Map map = fixture_map();
  rm::Level level;
  rm::CloneReport rep;
  std::string error;
  rm::CloneOptions opt;
  opt.smooth = true;
  CHECK_MSG(rm::clone_map(map, all_named(map), opt, &level, &rep, &error), error);
  bool all = !level.faces.empty();
  for (const rm::Face& f : level.faces) all = all && f.smooth;
  CHECK(all);
  const br::Map v38 = fixture_map(br::Family::v38);
  CHECK(!rm::clone_map(v38, rm::TextureMap{}, rm::CloneOptions{}, &level, &rep, &error));
  CHECK_MSG(error.find("38") != std::string::npos, error);
}

TEST(the_fixtures_clone_is_held_to_golden_bits) {
  const br::Map map = fixture_map();
  rm::Level level;
  rm::CloneReport rep;
  std::string error;
  CHECK_MSG(rm::clone_map(map, all_named(map), rm::CloneOptions{}, &level, &rep, &error), error);
  const std::vector<uint8_t> bytes = rm::encode(level);
  char row[128];
  std::snprintf(row, sizeof row, "%zx %zx %zx %016llx\n", rep.brushes, rep.faces, bytes.size(),
                static_cast<unsigned long long>(fnv(bytes)));
  const std::string table =
      std::string("# brushes faces bytes fnv64 (rooms_clone_test, GE_GOLDEN_WRITE=1)\n") + row;
  if (env_set("GE_GOLDEN_WRITE")) {
    std::ofstream(ge::test::root_path(kGolden)) << table;
    std::printf("  wrote %s\n", kGolden);
    return;
  }
  const auto rows = ge::test::golden_rows(kGolden);
  CHECK(rows.size() == 1);
  if (rows.size() == 1 && rows[0].size() == 4) {
    CHECK(rows[0][0] == rep.brushes);
    CHECK(rows[0][1] == rep.faces);
    CHECK(rows[0][2] == bytes.size());
    CHECK(rows[0][3] == fnv(bytes));
  }
}
