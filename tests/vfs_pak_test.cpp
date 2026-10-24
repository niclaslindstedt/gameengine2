// The pack archive (engine/vfs/pak.hpp): files written into an archive read back byte for byte
// by path, in order, as views into the archive; and an archive cut short, with a directory past
// its end, a file past its end, a path with no terminator, or no magic is refused with a message
// naming what is wrong, never read out of range.
//
// ge:summary Tests: the pack archive round trip and every damage it refuses
// ge:keywords test vfs pak pack archive round-trip refuse damaged truncated directory
// ge:skill vfs testing
#include <string>
#include <vector>

#include "check.hpp"
#include "vfs/pak.hpp"

namespace vfs = ge::vfs;

namespace {

std::vector<uint8_t> sample() {
  std::vector<uint8_t> out;
  std::string error;
  const bool ok = vfs::write_pak({{"maps/start.bsp", {1, 2, 3, 4, 5}},
                                  {"gfx/palette.lmp", std::vector<uint8_t>(768, 7)},
                                  {"empty.txt", {}}},
                                 &out, &error);
  CHECK_MSG(ok, error);
  return out;
}

bool refused(const std::vector<uint8_t>& bytes, const std::string& word) {
  vfs::Pak pak;
  std::string error;
  if (vfs::read_pak(bytes, &pak, &error)) return false;
  return error.find(word) != std::string::npos;
}

void put(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  for (int k = 0; k < 4; ++k) b[at + size_t(k)] = uint8_t(v >> (8 * k));
}

}  // namespace

TEST(files_written_into_a_pack_read_back_by_path) {
  const std::vector<uint8_t> bytes = sample();
  vfs::Pak pak;
  std::string error;
  CHECK_MSG(vfs::read_pak(bytes, &pak, &error), error);
  CHECK(pak.entries.size() == 3);
  CHECK(pak.entries[1].path == "gfx/palette.lmp");
  const vfs::PakEntry* e = vfs::pak_find(pak, "maps/start.bsp");
  CHECK(e != nullptr);
  if (e) {
    const auto f = vfs::pak_file(bytes, *e);
    CHECK(f.size() == 5 && f[0] == 1 && f[4] == 5);
    CHECK(f.data() == bytes.data() + e->offset);
  }
  CHECK(vfs::pak_file(bytes, pak.entries[1]).size() == 768);
  CHECK(vfs::pak_file(bytes, pak.entries[2]).empty());
  CHECK(vfs::pak_find(pak, "MAPS/START.BSP") == nullptr);
  CHECK(vfs::pak_find(pak, "maps/none.bsp") == nullptr);
}

TEST(a_damaged_pack_is_refused_naming_the_damage) {
  const std::vector<uint8_t> good = sample();
  CHECK(refused({'P', 'A', 'C'}, "magic"));
  std::vector<uint8_t> b = good;
  b[0] = 'Q';
  CHECK(refused(b, "magic"));
  CHECK(refused(std::vector<uint8_t>(good.begin(), good.end() - 1), "past the archive"));
  b = good;
  put(b, 8, 65);
  CHECK(refused(b, "whole"));
  b = good;
  const size_t dir = good.size() - size_t(3) * 64;
  put(b, dir + 56, uint32_t(good.size()));
  put(b, dir + 60, 1);
  CHECK(refused(b, "lies past the end"));
  b = good;
  for (size_t k = 0; k < 56; ++k) b[dir + k] = 'a';
  CHECK(refused(b, "not terminated"));
  b = good;
  b[dir] = 0;
  CHECK(refused(b, "empty path"));
  std::vector<uint8_t> out;
  std::string error;
  CHECK(!vfs::write_pak({{std::string(56, 'x'), {}}}, &out, &error));
  CHECK(error.find("55") != std::string::npos);
}
