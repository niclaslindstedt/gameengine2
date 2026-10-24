// engine/save/file.hpp: a save file round-trips to the byte, lists from its front, and refuses
// every cut and every flipped byte, a newer chunk, a newer format and another game's save, each
// with a message naming the part.
//
// ge:summary Test: the save file's round trip, peek, and every refusal
// ge:keywords test save file header chunk directory peek refuse truncate flip version game
// ge:skill save testing
#include "save/file.hpp"

#include "check.hpp"

namespace demo {

struct Body {
  std::vector<double> joints;
  int32_t hurt = 0;
};
template <class Io>
void describe(Io& io, Body& b) {
  io(b.joints);
  io.since(2, b.hurt, 0);
}

}  // namespace demo

namespace {

using namespace ge::save;

constexpr uint32_t kBody = tag("BODY");
constexpr uint32_t kNote = tag("NOTE");

Meta meta() {
  Meta m;
  m.game = "demo";
  m.game_version = 4;
  m.title = "Seed 38";
  m.place = "the top";
  m.play_s = 125.5;
  m.saved_unix = 1792750000;
  m.setup = "game demo\nseed 38\n";
  return m;
}

std::vector<uint8_t> file(uint16_t body_version = 2) {
  std::vector<uint8_t> out;
  FileWriter w(out, meta());
  for (uint32_t i = 0; i < 3; i++) {
    demo::Body b;
    for (int k = 0; k < 7 + int(i); k++) b.joints.push_back(k * 0.5 + i);
    b.hurt = int32_t(i);
    w.put(kBody, i, body_version, b);
  }
  w.put(kNote, 0, 1, std::string("odd length"));
  CHECK(w.finish());
  return out;
}

}  // namespace

TEST(file_round_trips) {
  const std::vector<uint8_t> bytes = file();
  CHECK(bytes.size() % 8 == 0);
  FileReader r;
  CHECK(r.open(bytes, "t.save", "demo"));
  CHECK(r.meta().title == "Seed 38" && r.meta().play_s == 125.5 && r.meta().game_version == 4);
  CHECK(r.header().chunks == 5 && r.all(kBody).size() == 3);
  demo::Body b;
  CHECK(r.get(kBody, 2, 2, b));
  CHECK(b.joints.size() == 9 && b.hurt == 2 && b.joints[8] == 6.0);
  std::string note;
  CHECK(r.get(kNote, 0, 1, note) && note == "odd length");
  CHECK(file() == bytes);  // the same state writes the same bytes
  CHECK(tag_name(kBody) == "BODY");
}

TEST(file_lists_from_its_front) {
  const std::vector<uint8_t> bytes = file();
  Header h;
  Meta m;
  std::string problem;
  size_t needed = 0;
  CHECK(!peek(std::span(bytes).first(64), h, m, problem, &needed));
  CHECK(needed == 64 + h.meta_size && needed < 200);
  CHECK(peek(std::span(bytes).first(needed), h, m, problem));
  CHECK(m.place == "the top" && h.kind == Kind::run);
}

TEST(file_refuses_every_cut_and_every_flipped_byte) {
  const std::vector<uint8_t> bytes = file();
  for (size_t cut = 0; cut < bytes.size(); cut++) {
    FileReader r;
    CHECK(!r.open(std::span(bytes).first(cut), "t.save"));
  }
  int refused = 0;
  for (size_t at = 0; at < bytes.size(); at++) {
    std::vector<uint8_t> bad = bytes;
    bad[at] ^= 0x10;
    FileReader r;
    refused += !r.open(bad, "t.save");
  }
  CHECK(refused == int(bytes.size()));
  std::vector<uint8_t> bad = bytes;
  bad[200] ^= 1;
  FileReader r;
  CHECK(!r.open(bad, "t.save"));
  CHECK(r.problem().starts_with("t.save: "));
  CHECK(r.problem().find("damaged") != std::string::npos);
}

TEST(file_reads_older_chunks_and_refuses_newer_ones) {
  const std::vector<uint8_t> now = file();
  // Version 1 had no hurt: its chunk is the joints alone.
  std::vector<uint8_t> out;
  FileWriter w(out, meta());
  w.chunk(kBody, 0, 1)(std::vector<double>{1, 2});
  CHECK(w.finish());
  FileReader r;
  CHECK(r.open(out));
  demo::Body b;
  b.hurt = 99;
  CHECK(r.get(kBody, 0, 2, b) && b.hurt == 0 && b.joints.size() == 2);
  // A chunk newer than the build reads.
  CHECK(r.open(file(3)));
  CHECK(!r.get(kBody, 0, 2, b));
  CHECK(r.problem().find("version 3; this build reads up to version 2") != std::string::npos);
  // A missing chunk, another game, leftover bytes.
  CHECK(r.open(now));
  CHECK(!r.get(tag("NONE"), 0, 1, b) &&
        r.problem().find("has no chunk NONE#0") != std::string::npos);
  CHECK(!r.open(now, "t.save", "other"));
  CHECK(r.problem().find("not of \"other\"") != std::string::npos);
  CHECK(r.open(now));
  int32_t just_one = 0;
  CHECK(!r.get(kBody, 0, 2, just_one) && r.problem().find("left after") != std::string::npos);
}

TEST(file_refuses_a_chunk_written_twice) {
  std::vector<uint8_t> out;
  FileWriter w(out, meta());
  w.put(kNote, 0, 1, 1);
  w.put(kNote, 0, 1, 2);
  std::string problem;
  CHECK(!w.finish(&problem) && problem.find("NOTE#0 was written twice") != std::string::npos);
}
