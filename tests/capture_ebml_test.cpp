// EBML (engine/capture/ebml.hpp): VINTs take the fewest bytes and never the reserved all-ones
// value, a fixed width pads, the unknown size is eight bytes of ones; an element is its ID, its
// size and its body; a master's size is patched on close.
//
// ge:summary Tests: EBML's VINTs, elements, masters closed and the unknown size
// ge:keywords test ebml vint element master unknown-size
// ge:skill replay testing
// ge:test tests/capture_ebml_test.cpp
#include <cstdint>
#include <vector>

#include "capture/ebml.hpp"
#include "check.hpp"

namespace gc = ge::capture;

namespace {

std::vector<uint8_t> vint(uint64_t v, int width = 0) {
  std::vector<uint8_t> out;
  gc::put_vint(out, v, width);
  return out;
}

}  // namespace

TEST(a_vint_takes_the_fewest_bytes_and_never_all_ones) {
  CHECK((vint(0) == std::vector<uint8_t>{0x80}));
  CHECK((vint(126) == std::vector<uint8_t>{0xFE}));
  // 127 in one byte would be all ones, the reserved "unknown": it takes two.
  CHECK((vint(127) == std::vector<uint8_t>{0x40, 0x7F}));
  CHECK((vint(16382) == std::vector<uint8_t>{0x7F, 0xFE}));
  CHECK((vint(16383) == std::vector<uint8_t>{0x20, 0x3F, 0xFF}));
  CHECK((vint(5, 8) == std::vector<uint8_t>{0x01, 0, 0, 0, 0, 0, 0, 0x05}));
  CHECK((vint(gc::kUnknownSize, 8) ==
         std::vector<uint8_t>{0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
}

TEST(an_element_is_its_id_its_size_and_its_body) {
  gc::Ebml e;
  e.uint(0x4286, 1);
  CHECK((e.bytes == std::vector<uint8_t>{0x42, 0x86, 0x81, 0x01}));
  e.bytes.clear();
  e.uint(0xE7, 0x1234);
  CHECK((e.bytes == std::vector<uint8_t>{0xE7, 0x82, 0x12, 0x34}));
  e.bytes.clear();
  e.text(0x4282, "webm");
  CHECK((e.bytes == std::vector<uint8_t>{0x42, 0x82, 0x84, 'w', 'e', 'b', 'm'}));
  e.bytes.clear();
  e.real(0x4489, 1.0);
  CHECK((e.bytes == std::vector<uint8_t>{0x44, 0x89, 0x88, 0x3F, 0xF0, 0, 0, 0, 0, 0, 0}));
}

TEST(a_master_is_closed_with_the_size_of_what_went_in) {
  gc::Ebml e;
  const size_t at = e.open(0x1A45DFA3);
  e.uint(0x4286, 1);
  e.close(at);
  CHECK((e.bytes == std::vector<uint8_t>{0x1A, 0x45, 0xDF, 0xA3, 0x01, 0, 0, 0, 0, 0, 0, 0x04, 0x42,
                                         0x86, 0x81, 0x01}));
  gc::Ebml u;
  const size_t s = u.open_unknown(0x18538067);
  CHECK(u.bytes[s] == 0x01 && u.bytes[s + 7] == 0xFF);
  u.uint(0xE7, 0);
  u.patch_size(s, u.body_since(s));
  CHECK(u.bytes[s] == 0x01 && u.bytes[s + 7] == 0x03);
}
