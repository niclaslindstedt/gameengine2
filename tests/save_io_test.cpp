// engine/save/io.hpp: one describe walked both ways round-trips every kind of field to the
// byte, reads older chunk versions with their defaults, and refuses damaged bytes without
// reading past them.
//
// ge:summary Test: the save Writer and Reader over one describe, versions, refusals, plain types
// ge:keywords test save io describe writer reader since until plain round-trip refuse
// ge:skill save testing
#include "save/io.hpp"

#include <bit>
#include <cmath>
#include <limits>

#include "check.hpp"

namespace demo {

enum class Gear : uint8_t { skis, board, sled };

struct Point {
  double x = 0, y = 0, z = 0;
};
template <class Io>
void describe(Io& io, Point& p) {
  io(p.x);
  io(p.y);
  io(p.z);
}

struct Rider {
  std::string name;
  Point at;
  std::vector<Point> path;
  std::vector<std::string> tags;
  std::array<int32_t, 3> scores{};
  std::optional<Gear> spare;
  std::vector<bool> gates;
  std::pair<uint16_t, bool> lift{};
  bool down = false;
  ge::core::Rng rng{7};
  double best = 0;  // since version 2
};
template <class Io>
void describe(Io& io, Rider& r) {
  io(r.name);
  io(r.at);
  io(r.path);
  io(r.tags);
  io(r.scores);
  io(r.spare);
  io(r.gates);
  io(r.lift);
  io(r.down);
  io(r.rng);
  int32_t lap = 0;  // dropped in version 3
  io.until(3, lap);
  io.since(2, r.best, -1.0);
}

struct Padded {
  uint8_t a = 0;
  double b = 0;
};
template <class Io>
void describe(Io& io, Padded& p) {
  io(p.a);
  io(p.b);
}

}  // namespace demo

GE_SAVE_PLAIN(demo::Point);

namespace {

using namespace ge::save;

demo::Rider sample() {
  demo::Rider r;
  r.name = "skier";
  r.at = {1.5, -0.0, std::numeric_limits<double>::infinity()};
  for (int i = 0; i < 5; i++) r.path.push_back({i * 0.1, i * -2.0, 1e300});
  r.tags = {"", "red", "a longer tag"};
  r.scores = {-1, 0, 1 << 30};
  r.spare = demo::Gear::sled;
  r.gates = {true, false, true, true};
  r.lift = {9, true};
  r.down = true;
  r.rng = ge::core::make_rng(1234);
  r.best = 61.25;
  return r;
}

bool same_bits(double a, double b) {
  return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b);
}

}  // namespace

TEST(io_round_trips_every_kind_of_field) {
  const demo::Rider a = sample();
  std::vector<uint8_t> bytes;
  write(bytes, a, 3);
  demo::Rider b;
  Reader r(bytes.data(), bytes.size(), 3);
  r(b);
  CHECK(r.ok() && r.at_end());
  CHECK(b.name == a.name && b.tags == a.tags && b.scores == a.scores && b.gates == a.gates);
  CHECK(b.spare == a.spare && b.lift == a.lift && b.down && b.rng.a == a.rng.a);
  CHECK(same_bits(b.at.y, -0.0) && std::isinf(b.at.z) && b.best == 61.25);
  CHECK(b.path.size() == 5 && b.path[4].x == a.path[4].x);
  std::vector<uint8_t> again;
  write(again, b, 3);
  CHECK(again == bytes);  // to the byte
}

TEST(io_is_little_endian_and_fixed_width) {
  std::vector<uint8_t> bytes;
  write(bytes, uint32_t{0x01020304}, 1);
  write(bytes, true, 1);
  write(bytes, std::string("ab"), 1);
  const std::vector<uint8_t> want = {4, 3, 2, 1, 1, 2, 0, 0, 0, 'a', 'b'};
  CHECK(bytes == want);
}

TEST(io_reads_an_older_version_with_its_defaults) {
  // Version 1 had the lap and no best: written by hand the way that describe wrote it.
  demo::Rider a = sample();
  std::vector<uint8_t> v1;
  {
    Writer w(v1, 1);
    w(a.name), w(a.at), w(a.path), w(a.tags), w(a.scores), w(a.spare), w(a.gates), w(a.lift);
    w(a.down), w(a.rng), w(int32_t{3});
  }
  demo::Rider b;
  Reader r(v1.data(), v1.size(), 1);
  r(b);
  CHECK(r.ok() && r.at_end());
  CHECK(b.best == -1.0 && b.name == "skier");
  // Version 3 dropped the lap: written without it, it reads without it.
  std::vector<uint8_t> v3;
  write(v3, a, 3);
  CHECK(v3.size() == v1.size() + 4);  // no lap (4 bytes), a best (8)
}

TEST(io_refuses_damage_without_reading_past_the_end) {
  std::vector<uint8_t> bytes;
  write(bytes, sample(), 3);
  for (size_t cut = 0; cut < bytes.size(); cut++) {
    demo::Rider b;
    Reader r(bytes.data(), cut, 3);
    r(b);
    CHECK(!r.ok() || !r.at_end() || cut == bytes.size());
    CHECK(!r.ok());  // every cut is a read past the end
  }
  // A length far past the end is refused before anything is allocated.
  std::vector<uint8_t> huge = {0xff, 0xff, 0xff, 0x7f};
  std::vector<double> v;
  Reader r(huge.data(), huge.size(), 1);
  r(v);
  CHECK(!r.ok() && v.empty());
  CHECK(r.problem().find("runs past the end") != std::string::npos);
  // A bool that is not 0 or 1.
  const uint8_t two = 2;
  bool b = false;
  Reader rb(&two, 1, 1);
  rb(b);
  CHECK(!rb.ok() && rb.problem().find("not 0 or 1") != std::string::npos);
}

TEST(io_plain_types_are_checked) {
  CHECK(plain_problem<demo::Point>().empty());
  CHECK(plain_problem<ge::core::Rng>().empty());
  CHECK(plain_problem<demo::Padded>().find("padding") != std::string::npos);
}
