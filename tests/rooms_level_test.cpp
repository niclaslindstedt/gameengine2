// The room level format: the sample level parses, prints canonically, round-trips through its
// bytes, stays small, and every malformed file or level is refused with a reason.
//
// ge:summary Test: the room level's text form, bytes, checks and refusals
// ge:keywords test rooms level gelv text codec round-trip validate refuse checksum
// ge:skill rooms testing
#include <fstream>
#include <sstream>
#include <string>

#include "check.hpp"
#include "rooms/codec.hpp"
#include "rooms/level.hpp"
#include "rooms/text.hpp"

namespace {

namespace rm = ge::rooms;

std::string halls_text() {
  std::ifstream in(ge::test::root_path("plugins/rooms/levels/halls.gelv.txt"));
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

rm::Level halls() {
  rm::Level level;
  std::string error;
  CHECK(rm::parse(halls_text(), &level, &error));
  if (!error.empty()) std::printf("  halls: %s\n", error.c_str());
  return level;
}

bool parses(const std::string& text, std::string* error) {
  rm::Level level;
  return rm::parse(text, &level, error);
}

const char* kBox =
    "material m rgb=1,2,3\n"
    "room a at=0,0,0 seed=1 floor=0:m ceiling=3:m\n"
    "corner 0 0 m\ncorner 4 0 m\ncorner 4 4 m\ncorner 0 4 m\n";

}  // namespace

TEST(halls_parse_print_and_parse_again) {
  const rm::Level level = halls();
  CHECK(level.rooms.size() == 10);
  CHECK(level.portals.size() == 11);
  const std::string once = rm::print(level);
  rm::Level again;
  std::string error;
  CHECK(rm::parse(once, &again, &error));
  CHECK(rm::print(again) == once);
  CHECK(again.rooms[2].seed == 88123u);
  CHECK(again.rooms[2].shape == rm::Shape::cave);
  CHECK(again.materials[4].texture == "cave_rock wear=0.8");
}

TEST(halls_bytes_round_trip_and_stay_small) {
  const rm::Level level = halls();
  const auto bytes = rm::encode(level);
  CHECK(bytes.size() < 2048);
  rm::Level back;
  std::string error;
  CHECK(rm::decode(bytes, &back, &error));
  CHECK(rm::encode(back) == bytes);
  CHECK(rm::print(back) == rm::print(level));
}

TEST(portals_find_their_far_wall) {
  const rm::Level level = halls();
  // hall's wall 1 (x = 10) opens into gallery's wall 3, the same line facing back.
  CHECK(level.portals[0].to_wall == 3);
  CHECK(level.portals[1].to_wall == 0);  // hall's north wall into the cave's first wall
  CHECK(level.portals[5].to_wall == 2);  // the corridor's foot into the crypt
}

TEST(damaged_bytes_are_refused_with_a_reason) {
  const auto good = rm::encode(halls());
  rm::Level out;
  std::string error;
  auto bad = good;
  bad[0] = 'X';
  CHECK(!rm::decode(bad, &out, &error) && error.find("magic") != std::string::npos);
  bad = good;
  bad[40] ^= 0x10;
  CHECK(!rm::decode(bad, &out, &error) && error.find("checksum") != std::string::npos);
  bad.assign(good.begin(), good.begin() + 30);
  CHECK(!rm::decode(bad, &out, &error));
  bad = good;
  bad[4] = 9;  // version, with the checksum mended
  {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i + 4 < bad.size(); ++i) h = (h ^ bad[i]) * 0x01000193u;
    for (int k = 0; k < 4; ++k) bad[bad.size() - 4 + size_t(k)] = uint8_t(h >> (8 * k));
  }
  CHECK(!rm::decode(bad, &out, &error) && error.find("version") != std::string::npos);
}

TEST(unsound_levels_are_refused_with_a_reason) {
  std::string error;
  CHECK(parses(kBox, &error));
  // Clockwise corners.
  CHECK(
      !parses("material m rgb=1,2,3\nroom a at=0,0,0 seed=1 floor=0:m ceiling=3:m\n"
              "corner 0 0 m\ncorner 0 4 m\ncorner 4 4 m\ncorner 4 0 m\n",
              &error));
  CHECK(error.find("clockwise") != std::string::npos);
  // Walls that cross (a bow tie).
  CHECK(
      !parses("material m rgb=1,2,3\nroom a at=0,0,0 seed=1 floor=0:m ceiling=3:m\n"
              "corner 0 0 m\ncorner 4 4 m\ncorner 4 0 m\ncorner 0 4 m\n",
              &error));
  // An opening longer than its wall.
  CHECK(!parses(std::string(kBox) + "portal a 0 outside at=3 width=2 sill=0 head=2\n", &error));
  CHECK(error.find("does not fit wall") != std::string::npos);
  // An opening into a room that does not share the wall.
  CHECK(!parses(std::string(kBox) + "room b at=10,0,0 seed=2 floor=0:m ceiling=3:m\n"
                                    "corner 0 0 m\ncorner 4 0 m\ncorner 4 4 m\ncorner 0 4 m\n"
                                    "portal a 1 b at=1 width=1 sill=0 head=2\n",
                &error));
  CHECK(error.find("facing back") != std::string::npos);
  // A cave's mouth with no frame.
  CHECK(!parses(std::string(kBox) + "room b at=4,0,0 seed=2 floor=0:m ceiling=3:m cave=5,10,20\n"
                                    "corner 0 0 m\ncorner 4 0 m\ncorner 4 4 m\ncorner 0 4 m\n"
                                    "portal a 1 b at=1 width=1 sill=0 head=2\n",
                &error));
  CHECK(error.find("trim=") != std::string::npos);
  // A text error names its line.
  CHECK(!parses("material m rgb=1,2,3\nroom a at=0,0,0 floor=0:nope ceiling=3:m\n", &error));
  CHECK(error.starts_with("line 2:"));
}
